#include "NativeComponents.h"

#include "internal/Host.h"
#include "internal/Log.h"

#include "ll/api/memory/Memory.h"
#include "mc/client/settings/BannerType.h"
#include "mc/client/settings/LocStringData.h"
#include "mc/client/settings/OptionData.h"
#include "mc/deps/core/memory/IMemoryAllocator.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <new>
#include <variant>
#include <vector>

namespace lever::detail::native {

namespace {

// ---- real in-game layout (1.26.51, docs/RE-components.md) ----
constexpr std::size_t kVariantSize = 0x3d0;
constexpr std::size_t kIndexOffset = 0x3c8;
constexpr std::size_t kTypeOffset  = 0x48;  // SettingsType (int) in the component base
constexpr std::size_t kDerived     = 0x1e0; // first derived field

enum VariantIndex : unsigned char {
    kBoolean = 0, kNumberInt = 1, kNumberFloat = 2, kOption = 3, kString = 4, kAction = 5, kText = 6, kGroupInfo = 7, kBanner = 8
};
// SettingsType values (SDK SettingsType.h): Number 0, Boolean 1, String 2, Option 3, Action 4, Text 5, GroupInfo 6, Banner 7
enum SettingsTypeValue : int { kTypeNumber = 0, kTypeBoolean = 1, kTypeString = 2, kTypeBanner = 7 };

// Component vtables (image RVAs). 0 = unknown for this build: that widget kind is not hand-built.
constexpr std::uintptr_t kVtString      = 0xe8bfb90;
constexpr std::uintptr_t kVtBanner      = 0xe8bfc10;
constexpr std::uintptr_t kVtNumberFloat = 0xe7873c0;
// The one vtable whose destructor has the Boolean/Option shape (provider at +0x1e0, std::function at +0x1e8).
// Boolean and Option cannot be told apart statically; booleanVtable() resolves it at runtime against a real
// OptionComponent built with the exported constructor.
constexpr std::uintptr_t kVtBooleanCandidate = 0xe8bfb70;

std::uintptr_t imageBase() {
    static const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    return base;
}

void* allocateElement() {
    // The game frees the element with its own operator delete, so it has to come from the game's allocator
    // regardless of whether the embedding mod overrides operator new.
    void* raw = ::ll::memory::getDefaultAllocator()._allocate(kVariantSize);
    if (raw) std::memset(raw, 0, kVariantSize);
    return raw;
}

Element finish(void* raw, unsigned char index) {
    static_cast<unsigned char*>(raw)[kIndexOffset] = index;
    return Element(reinterpret_cast<Element::element_type*>(raw));
}

template <class T>
T* at(void* raw, std::size_t off) {
    return reinterpret_cast<T*>(static_cast<unsigned char*>(raw) + off);
}

/// Base part of a hand-built component: the exported TextComponent constructor writes the whole 0x1e0-byte base
/// (strings, type Text, state functions, pub/sub), then the real vtable and type are patched in.
void* beginHandBuilt(
    std::string_view           id,
    std::string_view           name,
    std::optional<std::string> description,
    int                        type,
    std::uintptr_t             vtableRva
) {
    void* raw = allocateElement();
    if (!raw) return nullptr;
    reinterpret_cast<::Settings::TextComponent*>(raw)->$ctor(id, name, std::move(description));
    *at<int>(raw, kTypeOffset)      = type;
    *at<std::uintptr_t>(raw, 0)     = imageBase() + vtableRva;
    return raw;
}

// ---- data providers: our own classes with the game's vtable order ----
//
// Layout of Settings::IDataProvider: vtable, std::function<void()> mListener (+0x08, 64 bytes),
// Bedrock::PubSub::Subscription mSubscription (+0x48, 16 bytes). Virtual order: dtor, setChangeListener, canModify.
// The SDK class cannot be used as a base (its Subscription has no usable constructor), so this mirrors it.

struct ProviderBase {
    virtual ~ProviderBase() = default;
    virtual void setChangeListener(std::function<void()> callback) { listener = std::move(callback); }
    virtual bool canModify() const { return true; }

    std::function<void()> listener;
    unsigned char         subscription[16]{};

    explicit ProviderBase(SettingKey k) : key(std::move(k)) {}
    ProviderBase() = default;

    SettingKey key;

    /// Runs fn(setting) under the host model lock if the setting still exists.
    template <class Fn>
    void with(Fn&& fn) const {
        Registry&                   m = host::model();
        std::lock_guard<std::mutex> lk(m.mutex);
        if (Page* p = m.findPage(key.modId, key.pageId))
            if (Setting* s = m.findSetting(*p, key.settingId)) fn(*s);
    }
};
static_assert(offsetof(ProviderBase, listener) == 0x08, "IDataProvider layout");
static_assert(sizeof(std::function<void()>) == 64, "MSVC std::function size");

// IBooleanDataProvider: + getValue, setValue, flush
struct BoolProvider : ProviderBase {
    using ProviderBase::ProviderBase;
    virtual bool getValue() const {
        bool v = false;
        with([&](Setting& s) { if (s.getBool) v = s.getBool(); });
        return v;
    }
    virtual void setValue(bool value) {
        with([&](Setting& s) { if (s.setBool) s.setBool(value); });
    }
    virtual bool flush() { return false; }
};

// INumberDataProvider<float>: + getValue, setValue, getMin, getMax, flush (slots 3..7 as the game calls them);
// extra trailing slots guard against a longer real interface.
struct FloatProvider : ProviderBase {
    using ProviderBase::ProviderBase;
    virtual float getValue() const {
        double v = 0;
        with([&](Setting& s) { if (s.getNumber) v = s.getNumber(); });
        return static_cast<float>(v);
    }
    virtual void setValue(float value) {
        with([&](Setting& s) { if (s.setNumber) s.setNumber(static_cast<double>(value)); });
    }
    virtual float getMin() const {
        double v = 0;
        with([&](Setting& s) { v = s.min; });
        return static_cast<float>(v);
    }
    virtual float getMax() const {
        double v = 1;
        with([&](Setting& s) { v = s.max; });
        return static_cast<float>(v);
    }
    virtual bool flush() { return false; }
    virtual bool reserved0() { return false; }
    virtual bool reserved1() { return false; }
    virtual bool reserved2() { return false; }
};

// IStringDataProvider: + getValue, setValue(string_view), commitValue, flush
struct StringProvider : ProviderBase {
    using ProviderBase::ProviderBase;
    virtual std::string getValue() const {
        std::string v;
        with([&](Setting& s) { if (s.getString) v = s.getString(); });
        return v;
    }
    virtual void setValue(std::string_view value) {
        with([&](Setting& s) { if (s.setString) s.setString(std::string(value)); });
    }
    virtual void commitValue() {}
    virtual bool flush() { return false; }
};

// IOptionsDataProvider: + getOptions, getValue, setValue(int), getInfo, flush
using LabelVariant = std::variant<std::string, std::function<std::string()>, ::Settings::LocStringData>;
struct OptionDataRaw {
    int                         value = 0;
    LabelVariant                label;
    std::optional<LabelVariant> description;
    bool                        disabled = false;
};
static_assert(sizeof(OptionDataRaw) == sizeof(::Settings::OptionData), "OptionData layout");
static_assert(offsetof(OptionDataRaw, label) == 8 && offsetof(OptionDataRaw, description) == 8 + 72, "OptionData layout");

struct OptionsProvider : ProviderBase {
    explicit OptionsProvider(SettingKey k, const std::vector<std::string>& labels) : ProviderBase(std::move(k)) {
        options.reserve(labels.size());
        int i = 0;
        for (const std::string& l : labels) {
            OptionDataRaw o;
            o.value = i++;
            o.label = l;
            options.push_back(std::move(o));
        }
    }
    virtual ::gsl::span<::Settings::OptionData const> getOptions() const {
        return {reinterpret_cast<const ::Settings::OptionData*>(options.data()), options.size()};
    }
    virtual int getValue() const {
        int v = 0;
        with([&](Setting& s) { if (s.getIndex) v = s.getIndex(); });
        return v;
    }
    virtual void setValue(int index) {
        with([&](Setting& s) { if (s.setIndex) s.setIndex(index); });
    }
    virtual std::optional<LabelVariant> const& getInfo() const {
        static const std::optional<LabelVariant> none;
        return none;
    }
    virtual bool flush() { return false; }

    std::vector<OptionDataRaw> options;
};

// IActionDataProvider: + flush
struct ActionProvider : ProviderBase {
    using ProviderBase::ProviderBase;
    virtual bool flush() { return false; }
};

// IDataProvider only (banner)
struct PlainProvider : ProviderBase {
    using ProviderBase::ProviderBase;
};

template <class T>
::std::unique_ptr<T> asSdk(ProviderBase* p) {
    return ::std::unique_ptr<T>(reinterpret_cast<T*>(p));
}

/// Resolves the BooleanComponent vtable (0 = unknown on this build). Builds a throw-away OptionComponent with the
/// exported constructor, reads its vtable and accepts the static candidate only if it is a different class.
std::uintptr_t booleanVtable() {
    static const std::uintptr_t resolved = [] () -> std::uintptr_t {
        void* raw = allocateElement();
        if (!raw) return 0;
        auto* provider = new OptionsProvider(SettingKey{}, {});
        reinterpret_cast<::Settings::OptionComponent*>(raw)->$ctor(
            "lever.probe",
            "probe",
            std::nullopt,
            asSdk<::Settings::IOptionsDataProvider>(provider)
        );
        const std::uintptr_t optionVt = *at<std::uintptr_t>(raw, 0);
        using DeletingDtor            = void* (*)(void*, unsigned);
        reinterpret_cast<DeletingDtor*>(optionVt)[0](raw, 0); // destroy in place (flag 0: no free)
        ::ll::memory::getDefaultAllocator()._release(raw);
        const std::uintptr_t candidate = imageBase() + kVtBooleanCandidate;
        char                 buf[160];
        std::snprintf(
            buf,
            sizeof buf,
            "components: OptionComponent vtable +0x%llx, Boolean candidate +0x%llx -> %s",
            static_cast<unsigned long long>(optionVt - imageBase()),
            static_cast<unsigned long long>(kVtBooleanCandidate),
            optionVt == candidate ? "same class, toggles fall back to dropdowns" : "distinct, native toggles"
        );
        logInfo(buf);
        return optionVt == candidate ? 0 : candidate;
    }();
    return resolved;
}

} // namespace

Element makeText(std::string_view id, std::string_view name, std::optional<std::string> description) {
    void* raw = allocateElement();
    if (!raw) return nullptr;
    reinterpret_cast<::Settings::TextComponent*>(raw)->$ctor(id, name, std::move(description));
    return finish(raw, kText);
}

Element makeGroupInfo(std::string_view id, std::string_view name, std::optional<std::string> description) {
    void* raw = allocateElement();
    if (!raw) return nullptr;
    reinterpret_cast<::Settings::GroupInfoComponent*>(raw)->$ctor(id, name, std::move(description));
    return finish(raw, kGroupInfo);
}

bool togglesSupported() { return booleanVtable() != 0; }

std::optional<std::string> descOf(const Setting& s) {
    return s.description.empty() ? std::nullopt : std::optional(s.description);
}

Element makeToggle(std::string_view id, const Setting& s, SettingKey key) {
    if (!togglesSupported()) return nullptr;
    void* raw = beginHandBuilt(id, s.label, descOf(s), kTypeBoolean, booleanVtable());
    if (!raw) return nullptr;
    // BooleanComponent: +0x1e0 unique_ptr<IBooleanDataProvider>, +0x1e8 std::function (confirmation requests,
    // left empty = zero bytes)
    *at<ProviderBase*>(raw, kDerived) = new BoolProvider(std::move(key));
    return finish(raw, kBoolean);
}

Element makeSlider(std::string_view id, const Setting& s, SettingKey key) {
    void* raw = beginHandBuilt(id, s.label, descOf(s), kTypeNumber, kVtNumberFloat);
    if (!raw) return nullptr;
    // NumberComponent<float>: +0x1e0 provider, +0x1e8 float scale, +0x1ec optional<float> step (+0x1f0 engaged),
    // +0x1f8 std::function formatter (left empty)
    *at<ProviderBase*>(raw, kDerived) = new FloatProvider(std::move(key));
    *at<float>(raw, kDerived + 0x08)  = 1.0f;
    if (s.step > 0) {
        *at<float>(raw, kDerived + 0x0c) = static_cast<float>(s.step);
        *at<bool>(raw, kDerived + 0x10)  = true;
    }
    return finish(raw, kNumberFloat);
}

Element makeDropdown(std::string_view id, const Setting& s, SettingKey key) {
    void* raw = allocateElement();
    if (!raw) return nullptr;
    auto* provider = new OptionsProvider(std::move(key), s.options);
    reinterpret_cast<::Settings::OptionComponent*>(raw)->$ctor(
        id,
        s.label,
        descOf(s),
        asSdk<::Settings::IOptionsDataProvider>(provider)
    );
    return finish(raw, kOption);
}

Element makeTextField(std::string_view id, const Setting& s, SettingKey key) {
    void* raw = beginHandBuilt(id, s.label, descOf(s), kTypeString, kVtString);
    if (!raw) return nullptr;
    // StringComponent: +0x1e0 provider, +0x1e8 optional<string> placeholder, +0x210 optional<int> maxLength,
    // +0x218 optional<function> format validation, +0x260 bool isFormatValid, +0x268 optional<string> fallback
    *at<ProviderBase*>(raw, kDerived) = new StringProvider(std::move(key));
    new (at<void>(raw, kDerived + 0x08)) std::optional<std::string>();
    new (at<void>(raw, kDerived + 0x30)) std::optional<int>(static_cast<int>(s.maxLength));
    new (at<void>(raw, kDerived + 0x38)) std::optional<std::function<std::optional<std::string>(std::string)>>();
    *at<bool>(raw, kDerived + 0x80) = true;
    new (at<void>(raw, kDerived + 0x88)) std::optional<std::string>();
    return finish(raw, kString);
}

Element makeButton(std::string_view id, const Setting& s, SettingKey key) {
    void* raw = allocateElement();
    if (!raw) return nullptr;
    SettingKey k2      = key;
    auto       onClick = [k = std::move(k2)](std::function<void(bool)> done) {
        Registry&                   m = host::model();
        std::function<void()>       cb;
        {
            std::lock_guard<std::mutex> lk(m.mutex);
            if (Page* p = m.findPage(k.modId, k.pageId))
                if (Setting* st = m.findSetting(*p, k.settingId)) cb = st->onClick;
        }
        if (cb) cb();
        if (done) done(true);
    };
    reinterpret_cast<::Settings::ActionComponent*>(raw)->$ctor(
        id,
        s.label,
        descOf(s),
        s.label, // action label (the button text)
        std::function<void(std::function<void(bool)>)>(std::move(onClick)),
        std::nullopt,
        std::nullopt,
        asSdk<::Settings::IActionDataProvider>(new ActionProvider(std::move(key)))
    );
    return finish(raw, kAction);
}

Element makeBanner(std::string_view id, const Setting& s) {
    void* raw = beginHandBuilt(id, s.label, descOf(s), kTypeBanner, kVtBanner);
    if (!raw) return nullptr;
    // BannerComponent: +0x1e0 BannerType, +0x1e8 optional<BannerCta> (empty), +0x250 optional<ConfirmationRequest>
    // (empty), +0x2d8 unique_ptr<IDataProvider>
    int type = static_cast<int>(::Settings::BannerType::Neutral);
    if (s.bannerKind == LEVER_BANNER_WARNING) type = static_cast<int>(::Settings::BannerType::Notice);
    if (s.bannerKind == LEVER_BANNER_ERROR) type = static_cast<int>(::Settings::BannerType::Destructive);
    *at<int>(raw, kDerived)                  = type;
    *at<ProviderBase*>(raw, kDerived + 0xf8) = new PlainProvider();
    return finish(raw, kBanner);
}

Element makeSetting(std::string_view id, const Setting& s, SettingKey key) {
    switch (s.kind) {
    case LEVER_KIND_TOGGLE:
        if (togglesSupported()) return makeToggle(id, s, std::move(key));
        {
            // fallback: a two-entry dropdown bound to the bool callbacks
            Setting d    = s;
            d.options    = {"Off", "On"};
            d.getIndex   = s.getBool ? std::function<int()>([g = s.getBool] { return g() ? 1 : 0; }) : nullptr;
            d.setIndex   = s.setBool ? std::function<void(int)>([st = s.setBool](int v) { st(v != 0); }) : nullptr;
            return makeDropdown(id, d, std::move(key));
        }
    case LEVER_KIND_SLIDER: return makeSlider(id, s, std::move(key));
    case LEVER_KIND_DROPDOWN: return makeDropdown(id, s, std::move(key));
    case LEVER_KIND_TEXT_FIELD: return makeTextField(id, s, std::move(key));
    case LEVER_KIND_BUTTON: return makeButton(id, s, std::move(key));
    case LEVER_KIND_BANNER: return makeBanner(id, s);
    case LEVER_KIND_HEADER: return makeText(id, s.label, descOf(s));
    }
    return nullptr;
}

} // namespace lever::detail::native
