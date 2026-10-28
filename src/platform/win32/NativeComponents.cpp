#include "NativeComponents.h"

#include "internal/Host.h"
#include "internal/Log.h"
#include "internal/SliderGrid.h"

#include "ll/api/memory/Memory.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/settings/BannerType.h"
#include "mc/client/settings/BooleanConfirmationRequest.h"
#include "mc/client/settings/FactoryUtil.h"
#include "mc/options/option_types/Option.h"
#include "mc/options/option_types/OptionID.h"
#include "mc/options/option_types/OptionType.h"
#include "mc/client/settings/LocStringData.h"
#include "mc/client/settings/OptionData.h"
#include "mc/deps/core/memory/IMemoryAllocator.h"
#include "mc/locale/I18n.h"
#include "mc/locale/Localization.h"

#include <windows.h>

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <new>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace lever::detail::native {

namespace {

// ---- real in-game layout (1.26.51) ----
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
// The BooleanComponent vtable is not identifiable statically (the only vtable with its destructor shape turned out
// to be OptionComponent's). booleanVtable() discovers it at runtime from a throw-away element built by the exported
// Settings::FactoryUtil::addBoolean with a vanilla boolean OptionID and our own provider.
::IOptionRegistry* gOptions = nullptr;

// Descriptions are resolved through I18n by the settings queries and DROPPED when the lookup returns the input
// unchanged (a missing key comes back as itself), while names are shown verbatim. So every
// description is registered as a runtime translation under a key derived from the component id, and the key is
// what the component carries. Pending entries are flushed into I18n after each factory run.
std::mutex                                   gTranslationsMutex;
std::unordered_map<std::string, std::string> gTranslations;        // everything registered so far (re-appended)
bool                                         gTranslationsDirty = false;

std::optional<std::string> describe(std::string_view id, const std::string& text) {
    if (text.empty()) return std::nullopt;
    std::string key = std::string(id) + ".description";
    std::lock_guard<std::mutex> lk(gTranslationsMutex);
    auto it = gTranslations.find(key);
    if (it == gTranslations.end() || it->second != text) {
        gTranslations[key]  = text;
        gTranslationsDirty = true;
    }
    return key;
}

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

struct ProviderBase;
void wireChangeListener(void* raw, ProviderBase* provider);

// The vanilla constructors install `provider->setChangeListener([this] { publish(this + 0x140); })` (OptionComponent
// ctor 0x4224e10, lambda invoke 0xc0c3a0 -> 0x134ac0): the base's pub/sub block at +0x140 is what the settings
// queries subscribe to, so a provider that does not reach it never updates the screen. Hand-built components wire
// the same thing.
constexpr std::uintptr_t kPublishChanged = 0x134ac0;

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
    ProviderBase(const ProviderBase&)            = delete;
    ProviderBase& operator=(const ProviderBase&) = delete;

    SettingKey key;

    /// Runs fn(setting) under the host model lock if the setting still exists. fn must not call back into the mod:
    /// a callback may re-enter the host (notifyChanged, Widget::label), which takes this lock again.
    template <class Fn>
    void with(Fn&& fn) const {
        Registry&                   m = host::model();
        std::lock_guard<std::mutex> lk(m.mutex);
        if (Setting* s = m.findSetting(key.modId, key.pageId, key.settingId)) fn(*s);
    }

    /// A copy of one of the setting's callbacks (empty if the setting is gone), to be called without the lock.
    template <class F>
    F grab(F Setting::*member) const {
        F fn;
        with([&](Setting& s) { fn = s.*member; });
        return fn;
    }
};
static_assert(offsetof(ProviderBase, listener) == 0x08, "IDataProvider layout");

static_assert(sizeof(std::function<void()>) == 64, "MSVC std::function size");

void wireChangeListener(void* raw, ProviderBase* provider) {
    const std::uintptr_t fn = imageBase() + kPublishChanged;
    provider->setChangeListener(std::function<void()>([raw, fn] {
        reinterpret_cast<void (*)(void*)>(fn)(static_cast<unsigned char*>(raw) + 0x140);
    }));
}

// ---- dynamic state (Enabled / Disabled / Hidden) ----
//
// Component base +0xf8..+0x138: std::optional<std::function<SettingsState(const Component&, const SettingsState&)>>
// (the std::function is 0x40 bytes with its impl pointer at +0x130; the engaged byte is +0x138). The shared
// getDefaultState (vtable slot 1) asks the class for its default and, when the optional is engaged, hands it to this
// function and returns the result. Native SettingsState values: Hidden 0, Disabled 1, Enabled 2. The queries
// re-read the state whenever the component's pub/sub block publishes, i.e. on the same path as a value change.
constexpr std::size_t kStateFnOffset      = 0xf8;
constexpr std::size_t kStateEngagedOffset = 0x138;
constexpr int         kNativeHidden = 0, kNativeDisabled = 1, kNativeEnabled = 2;

struct NativeComponent; // opaque: the game's Settings::Component, passed by reference
using StateFn = std::function<int(const NativeComponent&, const int&)>;
static_assert(sizeof(StateFn) == 64, "MSVC std::function size");

// Every component we built that is alive right now (the game destroys them with the page). The state functor owns a
// Token, so a component's destruction (which destroys the functor) removes it. Leaked on purpose (shutdown order);
// recursive because a publish may synchronously rebuild components on the same thread.
struct LiveComponents {
    std::recursive_mutex                        mutex;
    std::unordered_map<void*, SettingKey>       map;
};
LiveComponents& liveComponents() {
    static auto* live = new LiveComponents();
    return *live;
}
struct Token {
    void* raw;
    explicit Token(void* r) : raw(r) {}
    ~Token() {
        LiveComponents&                        live = liveComponents();
        std::lock_guard<std::recursive_mutex> lk(live.mutex);
        live.map.erase(raw);
    }
    Token(const Token&)            = delete;
    Token& operator=(const Token&) = delete;
};

int nativeState(int leverState) {
    switch (leverState) {
    case LEVER_STATE_DISABLED: return kNativeDisabled;
    case LEVER_STATE_HIDDEN: return kNativeHidden;
    default: return kNativeEnabled;
    }
}

/// Installs the state override on a built component: the setting's current LEVER_STATE_* (its getState callback)
/// wins over the class default unless it is Enabled, in which case the class default (e.g. Disabled for a read-only
/// provider) stays. The callback runs without the model lock: it belongs to the mod and may re-enter the host.
void installState(void* raw, const SettingKey& key) {
    {
        LiveComponents&                        live = liveComponents();
        std::lock_guard<std::recursive_mutex> lk(live.mutex);
        live.map[raw] = key;
    }
    auto token = std::make_shared<Token>(raw);
    new (at<void>(raw, kStateFnOffset)) StateFn([key, token](const NativeComponent&, const int& defaultState) {
        std::function<int()> getState;
        {
            Registry&                   m = host::model();
            std::lock_guard<std::mutex> lk(m.mutex);
            if (Setting* s = m.findSetting(key.modId, key.pageId, key.settingId)) getState = s->getState;
        }
        int st = LEVER_STATE_ENABLED;
        if (getState) {
            const int v = getState();
            if (v == LEVER_STATE_DISABLED || v == LEVER_STATE_HIDDEN) st = v;
        }
        return st == LEVER_STATE_ENABLED ? defaultState : nativeState(st);
    });
    *at<unsigned char>(raw, kStateEngagedOffset) = 1;
}

void publish(void* raw) {
    reinterpret_cast<void (*)(void*)>(imageBase() + kPublishChanged)(static_cast<unsigned char*>(raw) + 0x140);
}

/// Publishes every live component whose key matches (empty pageId / settingId = any): values and states are re-read.
void publishMatching(std::string_view modId, std::string_view pageId, std::string_view settingId) {
    LiveComponents&                        live = liveComponents();
    std::lock_guard<std::recursive_mutex> lk(live.mutex);
    std::vector<void*> hit; // snapshot: a publish may rebuild components and change the map
    for (auto& [raw, key] : live.map)
        if (key.modId == modId && (pageId.empty() || key.pageId == pageId) && (settingId.empty() || key.settingId == settingId))
            hit.push_back(raw);
    for (void* raw : hit)
        if (live.map.count(raw)) publish(raw);
}

/// After a write through a provider: every row of the mod re-reads (its sections share one page), so rows
/// depending on the new value update wherever they are.
void afterWrite(const SettingKey& key) { publishMatching(key.modId, {}, {}); }

// IBooleanDataProvider: + getValue, setValue, flush
struct BoolProvider : ProviderBase {
    using ProviderBase::ProviderBase;
    virtual bool getValue() const {
        auto get = grab(&Setting::getBool);
        return get ? get() : false;
    }
    virtual void setValue(bool value) {
        if (auto set = grab(&Setting::setBool)) set(value);
        afterWrite(key);
    }
    virtual bool flush() { return false; }
};

// INumberDataProvider<float>: + getValue, setValue, getMin, getMax, flush (slots 3..7 as the game calls them);
// extra trailing slots guard against a longer real interface.
//
// Speaks index units (SliderGrid): the UI sees 0..positions-1 with a step of 1, which keeps the knob exact for any
// step and lets us choose whether the dividers are drawn (they follow the presence of the native step).
struct FloatProvider : ProviderBase {
    using ProviderBase::ProviderBase;
    SliderGrid grid() const {
        SliderGrid g;
        with([&](Setting& s) { g = SliderGrid::of(s.min, s.max, s.step); });
        return g;
    }
    virtual float getValue() const {
        auto get = grab(&Setting::getNumber);
        return get ? static_cast<float>(grid().indexOf(get())) : 0.0f;
    }
    virtual void setValue(float index) {
        if (auto set = grab(&Setting::setNumber)) set(grid().valueAt(static_cast<int>(std::lround(index))));
        afterWrite(key);
    }
    virtual float getMin() const { return 0.0f; }
    virtual float getMax() const { return static_cast<float>(grid().positions - 1); }
    virtual bool flush() { return false; }
    virtual bool reserved0() { return false; }
    virtual bool reserved1() { return false; }
    virtual bool reserved2() { return false; }
    virtual bool reserved3() { return false; }
    virtual bool reserved4() { return false; }
};

// IStringDataProvider: + getValue, setValue(string_view), commitValue, flush
// The text field sends every keystroke as setValue (JS onChange -> updateString) and one commitValue when it loses
// focus (onBlur -> commitString). Keystrokes are buffered here and the mod's setter runs once, on commit, so a mod
// that saves a file in its setter is not hit per key.
struct StringProvider : ProviderBase {
    using ProviderBase::ProviderBase;
    std::optional<std::string> pending; // text typed since the last commit

    virtual std::string getValue() const {
        if (pending) return *pending;
        auto get = grab(&Setting::getString);
        return get ? get() : std::string();
    }
    virtual void setValue(std::string_view value) { pending = std::string(value); }
    virtual void commitValue() {
        if (!pending) return;
        std::string v = std::move(*pending);
        pending.reset();
        if (auto set = grab(&Setting::setString)) set(v);
        afterWrite(key);
    }
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
        if (asToggle) {
            auto get = grab(&Setting::getBool);
            return get && get() ? 1 : 0;
        }
        auto get = grab(&Setting::getIndex);
        return get ? get() : 0;
    }
    virtual void setValue(int index) {
        if (asToggle) {
            if (auto set = grab(&Setting::setBool)) set(index != 0);
        } else if (auto set = grab(&Setting::setIndex)) set(index);
        afterWrite(key);
    }
    virtual std::optional<LabelVariant> const& getInfo() const {
        static const std::optional<LabelVariant> none;
        return none;
    }
    virtual bool flush() { return false; }

    std::vector<OptionDataRaw> options;
    bool                       asToggle = false; // maps a bool setting onto Off/On
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

/// Resolves the BooleanComponent vtable (0 = unknown on this build): builds a throw-away BooleanComponent through
/// the exported FactoryUtil::addBoolean (needs any valid vanilla boolean OptionID and the option registry), reads
/// its vtable, destroys it in place and frees the storage.
std::uintptr_t booleanVtable() {
    static const std::uintptr_t resolved = []() -> std::uintptr_t {
        if (!gOptions) {
            logWarn("components: no option registry; toggles fall back to dropdowns");
            return 0;
        }
        std::optional<::OptionID> found;
        for (int i = 0; i < 4096 && !found; ++i) {
            const auto id = static_cast<::OptionID>(i);
            if (!gOptions->isValidOption(id)) continue;
            std::optional<::Option*> o = gOptions->getIfValid(id);
            if (!o || !*o || !(*o)->mImpl) continue;
            if ((*o)->mImpl->mOptionType == ::OptionType::Boolean) found = id;
        }
        if (!found) {
            logWarn("components: no vanilla boolean option found; toggles fall back to dropdowns");
            return 0;
        }
        std::vector<Element> tmp;
        ::Settings::FactoryUtil::addBoolean(
            tmp,
            "lever.probe",
            *found,
            *gOptions,
            std::optional<std::unique_ptr<::Settings::IBooleanDataProvider>>(
                asSdk<::Settings::IBooleanDataProvider>(new BoolProvider(SettingKey{}))
            )
        );
        if (tmp.empty() || !tmp.front()) {
            logWarn("components: addBoolean produced no element; toggles fall back to dropdowns");
            return 0;
        }
        void* raw = tmp.front().release();
        for (auto& e : tmp) (void)e.release(); // never run the SDK deleter on game elements
        const unsigned char  index = *at<unsigned char>(raw, kIndexOffset);
        const int            type  = *at<int>(raw, kTypeOffset);
        const std::uintptr_t vt    = *at<std::uintptr_t>(raw, 0);
        using DeletingDtor         = void* (*)(void*, unsigned);
        reinterpret_cast<DeletingDtor*>(vt)[0](raw, 0); // destroy in place (flag 0: no free)
        ::ll::memory::getDefaultAllocator()._release(raw);
        char buf[200];
        std::snprintf(
            buf,
            sizeof buf,
            "components: addBoolean probe with OptionID %d -> variant index %u, type %d, vtable +0x%llx -> %s",
            static_cast<int>(*found),
            index,
            type,
            static_cast<unsigned long long>(vt - imageBase()),
            index == kBoolean ? "native toggles" : "unexpected, toggles fall back to dropdowns"
        );
        logInfo(buf);
        return index == kBoolean ? vt - imageBase() : 0;
    }();
    return resolved;
}

} // namespace

void setOptionRegistry(::IOptionRegistry* options) { gOptions = options; }

Element makeText(std::string_view id, std::string_view name, std::optional<std::string> description) {
    void* raw = allocateElement();
    if (!raw) return nullptr;
    if (description) description = describe(id, *description);
    reinterpret_cast<::Settings::TextComponent*>(raw)->$ctor(id, name, std::move(description));
    return finish(raw, kText);
}

Element makeGroupInfo(std::string_view id, std::string_view name, std::optional<std::string> description) {
    void* raw = allocateElement();
    if (!raw) return nullptr;
    if (description) description = describe(id, *description);
    reinterpret_cast<::Settings::GroupInfoComponent*>(raw)->$ctor(id, name, std::move(description));
    return finish(raw, kGroupInfo);
}

bool togglesSupported() { return booleanVtable() != 0; }

void notifyChanged(std::string_view modId, std::string_view pageId, std::string_view settingId) {
    if (modId.empty()) return;
    publishMatching(modId, pageId, settingId);
}


void flushTranslations() {
    std::unordered_map<std::string, std::string> copy;
    {
        std::lock_guard<std::mutex> lk(gTranslationsMutex);
        if (!gTranslationsDirty) return;
        gTranslationsDirty = false;
        copy               = gTranslations;
    }
    // I18n::appendAdditionalTranslations did not make the keys resolvable for the settings queries (2026-10-25), so
    // the strings go straight into the current language's table, which is what I18n::get consults. Same thread as
    // the lookups (the UI thread runs the factories and the queries).
    ::std::shared_ptr<::Localization const> lang = ::getI18n().getCurrentLanguage().get();
    if (!lang) return;
    auto& strings = *const_cast<::Localization&>(*lang).mStrings;
    for (auto& [k, v] : copy) strings[k] = v;
    logInfo("components: registered " + std::to_string(copy.size()) + " runtime translation(s) into language table");
}

std::optional<std::string> descOf(std::string_view id, const Setting& s) { return describe(id, s.description); }

Element makeToggle(std::string_view id, const Setting& s, SettingKey key) {
    if (!togglesSupported()) return nullptr;
    void* raw = beginHandBuilt(id, s.label, descOf(id, s), kTypeBoolean, booleanVtable());
    if (!raw) return nullptr;
    // BooleanComponent: +0x1e0 unique_ptr<IBooleanDataProvider>, +0x1e8 std::function<vector<
    // BooleanConfirmationRequest>()> (the query invokes it unconditionally: an empty std::function throws)
    auto* provider                    = new BoolProvider(std::move(key));
    *at<ProviderBase*>(raw, kDerived) = provider;
    wireChangeListener(raw, provider);
    new (at<void>(raw, kDerived + 0x08)) std::function<std::vector<::Settings::BooleanConfirmationRequest>()>(
        [] { return std::vector<::Settings::BooleanConfirmationRequest>{}; }
    );
    return finish(raw, kBoolean);
}

Element makeSlider(std::string_view id, const Setting& s, SettingKey key) {
    void* raw = beginHandBuilt(id, s.label, descOf(id, s), kTypeNumber, kVtNumberFloat);
    if (!raw) return nullptr;
    // NumberComponent<float>: +0x1e0 provider, +0x1e8 float scale, +0x1ec optional<float> step (+0x1f0 engaged),
    // +0x1f8 std::function formatter. The provider speaks index units (see FloatProvider), so scale is 1 and the
    // step, when present, is 1. The JavaScript draws the divider ticks iff a step is present (and uses 1 otherwise),
    // which is how SliderGrid::showSteps takes effect.
    auto* provider                    = new FloatProvider(key);
    *at<ProviderBase*>(raw, kDerived) = provider;
    wireChangeListener(raw, provider);
    *at<float>(raw, kDerived + 0x08) = 1.0f;
    const SliderGrid grid            = SliderGrid::of(s.min, s.max, s.step);
    if (grid.showSteps(s.flags)) {
        *at<float>(raw, kDerived + 0x0c) = 1.0f;
        *at<bool>(raw, kDerived + 0x10)  = true;
    }
    // Value text formatter at +0x1f8: SettingsNumberQuery calls it as optional<string>(const float& value, const
    // float& min, const float& max) with the (index) value. A disengaged result would make the JavaScript print the
    // raw float, so it always answers with the real value printed to the grid's decimals.
    new (at<void>(raw, kDerived + 0x18))
        std::function<std::optional<std::string>(const float&, const float&, const float&)>(
            [key](const float& index, const float&, const float&) {
                std::string text;
                Registry&                   m = host::model();
                std::lock_guard<std::mutex> lk(m.mutex);
                if (Setting* st = m.findSetting(key.modId, key.pageId, key.settingId))
                    text = SliderGrid::of(st->min, st->max, st->step).format(static_cast<int>(std::lround(index)));
                return std::optional<std::string>(std::move(text));
            }
        );
    return finish(raw, kNumberFloat);
}

Element makeDropdown(std::string_view id, const Setting& s, SettingKey key, bool asToggle) {
    void* raw = allocateElement();
    if (!raw) return nullptr;
    auto* provider     = new OptionsProvider(std::move(key), s.options);
    provider->asToggle = asToggle;
    reinterpret_cast<::Settings::OptionComponent*>(raw)->$ctor(
        id,
        s.label,
        descOf(id, s),
        asSdk<::Settings::IOptionsDataProvider>(provider)
    );
    return finish(raw, kOption);
}

Element makeTextField(std::string_view id, const Setting& s, SettingKey key) {
    void* raw = beginHandBuilt(id, s.label, descOf(id, s), kTypeString, kVtString);
    if (!raw) return nullptr;
    // StringComponent: +0x1e0 provider, +0x1e8 optional<string> placeholder, +0x210 optional<int> maxLength,
    // +0x218 optional<function> format validation, +0x260 bool isFormatValid, +0x268 optional<string> fallback
    auto* provider                    = new StringProvider(std::move(key));
    *at<ProviderBase*>(raw, kDerived) = provider;
    wireChangeListener(raw, provider);
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
        descOf(id, s),
        s.label, // action label (the button text)
        std::function<void(std::function<void(bool)>)>(std::move(onClick)),
        std::nullopt,
        std::nullopt,
        asSdk<::Settings::IActionDataProvider>(new ActionProvider(std::move(key)))
    );
    return finish(raw, kAction);
}

Element makeBanner(std::string_view id, const Setting& s) {
    void* raw = beginHandBuilt(id, s.label, descOf(id, s), kTypeBanner, kVtBanner);
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
    Element e;
    switch (s.kind) {
    case LEVER_KIND_TOGGLE:
        if (togglesSupported()) e = makeToggle(id, s, key);
        else {
            // fallback: a two-entry dropdown bound to the bool callbacks
            Setting d = s;
            d.options = {"Off", "On"};
            e         = makeDropdown(id, d, key, /*asToggle=*/true);
        }
        break;
    case LEVER_KIND_SLIDER: e = makeSlider(id, s, key); break;
    case LEVER_KIND_DROPDOWN: e = makeDropdown(id, s, key); break;
    case LEVER_KIND_TEXT_FIELD: e = makeTextField(id, s, key); break;
    case LEVER_KIND_BUTTON: e = makeButton(id, s, key); break;
    case LEVER_KIND_BANNER: e = makeBanner(id, s); break;
    case LEVER_KIND_HEADER: e = makeText(id, s.label, descOf(id, s)); break;
    }
    if (e) installState(e.get(), key); // every row can be hidden / disabled by its conditions
    return e;
}

} // namespace lever::detail::native
