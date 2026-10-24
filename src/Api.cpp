// C++ API -> local model -> (when a host is attached) C ABI calls on the host's table.

#include "oresettings/OreSettings.h"

#include "internal/Election.h"
#include "internal/Host.h"
#include "internal/Log.h"
#include "internal/Model.h"
#include "internal/Version.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"

#include <cstring>
#include <memory>
#include <mutex>

namespace ore_settings {

namespace {

using detail::Mod;
using detail::Registry;
using detail::Setting;

Registry& local() {
    static Registry r;
    return r;
}

// Returned for unknown mod ids so callers can keep chaining without null checks.
Page& invalidPage() {
    static Page p("", "", false);
    return p;
}

// ---- C trampolines: `user` is the stable address of the local Setting. ----

int  tBool(void* u) { return static_cast<Setting*>(u)->getBool() ? 1 : 0; }
void tSetBool(void* u, int v) { static_cast<Setting*>(u)->setBool(v != 0); }
double tNumber(void* u) { return static_cast<Setting*>(u)->getNumber(); }
void   tSetNumber(void* u, double v) { static_cast<Setting*>(u)->setNumber(v); }
std::int32_t tIndex(void* u) { return static_cast<std::int32_t>(static_cast<Setting*>(u)->getIndex()); }
void         tSetIndex(void* u, std::int32_t v) { static_cast<Setting*>(u)->setIndex(static_cast<int>(v)); }
size_t tString(void* u, char* buf, size_t cap) {
    const std::string s = static_cast<Setting*>(u)->getString();
    if (buf && cap > 0) {
        const size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
        std::memcpy(buf, s.data(), n);
        buf[n] = '\0';
    }
    return s.size();
}
void tSetString(void* u, const char* v) { static_cast<Setting*>(u)->setString(v ? v : ""); }
void tClick(void* u) { static_cast<Setting*>(u)->onClick(); }

void forwardSetting(const OreSettingsHostTable* t, const Mod& mod, const detail::Page& page, Setting& s) {
    std::vector<const char*> opts;
    for (const auto& o : s.options) opts.push_back(o.c_str());

    OreSettingsSettingDesc d{};
    d.size        = sizeof(d);
    d.modId       = mod.info.id.c_str();
    d.pageId      = page.id.c_str();
    d.id          = s.id.c_str();
    d.kind        = s.kind;
    d.label       = s.label.c_str();
    d.description = s.description.c_str();
    d.min = s.min, d.max = s.max, d.step = s.step;
    d.options     = opts.empty() ? nullptr : opts.data();
    d.optionCount = static_cast<std::uint32_t>(opts.size());
    d.bannerKind  = s.bannerKind;
    d.maxLength   = s.maxLength;
    d.user        = &s;
    if (s.getBool) d.getBool = &tBool;
    if (s.setBool) d.setBool = &tSetBool;
    if (s.getNumber) d.getNumber = &tNumber;
    if (s.setNumber) d.setNumber = &tSetNumber;
    if (s.getIndex) d.getIndex = &tIndex;
    if (s.setIndex) d.setIndex = &tSetIndex;
    if (s.getString) d.getString = &tString;
    if (s.setString) d.setString = &tSetString;
    if (s.onClick) d.onClick = &tClick;
    if (t->addSetting(&d) == ORESETTINGS_OK) s.forwarded = true;
}

// Sends everything not yet forwarded, in registration order. No-op while no host is attached (= buffering).
void flush() {
    const OreSettingsHostTable* t = detail::election::hostTable();
    if (!t || !ORESETTINGS_TABLE_HAS(t, addSetting) || !ORESETTINGS_TABLE_HAS(t, registerPage)
        || !ORESETTINGS_TABLE_HAS(t, registerMod) || !ORESETTINGS_TABLE_HAS(t, setPageSurfaces))
        return;

    std::lock_guard<std::mutex> lk(local().mutex);
    for (Mod& mod : local().mods) {
        if (!mod.forwarded) {
            OreSettingsModInfo mi{};
            mi.size         = sizeof(mi);
            mi.id           = mod.info.id.c_str();
            mi.displayName  = mod.info.displayName.c_str();
            mi.author       = mod.info.author.c_str();
            mi.description  = mod.info.description.c_str();
            mi.versionMajor = mod.info.version.major;
            mi.versionMinor = mod.info.version.minor;
            mi.versionPatch = mod.info.version.patch;
            if (t->registerMod(&mi) != ORESETTINGS_OK) continue; // retry on the next flush
            mod.forwarded = true;
        }
        for (auto& page : mod.pages) {
            if (!page.forwarded) {
                OreSettingsPageDesc pd{};
                pd.size     = sizeof(pd);
                pd.modId    = mod.info.id.c_str();
                pd.pageId   = page.id.c_str();
                pd.title    = page.title.c_str();
                pd.icon     = page.icon.empty() ? nullptr : page.icon.c_str();
                pd.surfaces = page.surfaces;
                if (t->registerPage(&pd) != ORESETTINGS_OK) continue;
                page.forwarded         = true;
                page.forwardedSurfaces = page.surfaces;
            } else if (page.forwardedSurfaces != page.surfaces) {
                if (t->setPageSurfaces(mod.info.id.c_str(), page.id.c_str(), page.surfaces) == ORESETTINGS_OK)
                    page.forwardedSurfaces = page.surfaces;
            }
            for (auto& s : page.settings) {
                if (!s.forwarded) forwardSetting(t, mod, page, s);
            }
        }
    }
}

// ---- Lazy per-copy initialisation ----

ll::event::ListenerPtr gJoinListener;
std::mutex             gInitMutex;

struct UnloadGuard {
    ~UnloadGuard() { detail::election::withdraw(); }
};

void ensureInit() {
    std::lock_guard<std::mutex> lk(gInitMutex);
    static UnloadGuard guard; // withdraws this copy from the slot when its DLL unloads
    detail::election::registerSelf();
    if (!gJoinListener) {
        // First use of the settings UI is not hookable yet (see Host.cpp), so elect when the player first joins a
        // level: by then every mod has loaded and enabled, i.e. every copy is in the slot.
        gJoinListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientJoinLevelEvent>(
            [](ll::event::ClientJoinLevelEvent&) {
                detail::election::attachOrHost();
                flush();
            }
        );
    }
}

void touch() {
    ensureInit();
    flush();
}

void addSetting(const std::string& modId, const std::string& pageId, Setting s) {
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        if (!local().addSetting(modId, pageId, std::move(s))) {
            detail::logWarn("ignored setting on " + modId + "/" + pageId + " (unknown page or duplicate id)");
            return;
        }
    }
    touch();
}

void setSurfaces(const std::string& modId, const std::string& pageId, std::uint32_t mask, bool exclusive) {
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        if (auto* p = local().findPage(modId, pageId)) p->surfaces = exclusive ? mask : (p->surfaces | mask);
    }
    touch();
}

std::uint32_t surfaceBit(Surface s) {
    switch (s) {
    case Surface::GlobalSettings: return ORESETTINGS_SURFACE_GLOBAL_SETTINGS;
    case Surface::PauseMenu: return ORESETTINGS_SURFACE_PAUSE_MENU;
    case Surface::WorldCreation: return ORESETTINGS_SURFACE_WORLD_CREATION;
    }
    return 0;
}

Setting make(std::string_view id, OreSettingsKind kind, std::string_view label, std::string_view description) {
    Setting s;
    s.id          = std::string(id);
    s.kind        = kind;
    s.label       = std::string(label);
    s.description = std::string(description);
    return s;
}

} // namespace

// ---- Page ----

Page& Page::showIn(Surface surface) {
    if (mValid) setSurfaces(mModId, mId, surfaceBit(surface), false);
    return *this;
}

Page& Page::showOnlyIn(Surface surface) {
    if (mValid) setSurfaces(mModId, mId, surfaceBit(surface), true);
    return *this;
}

Page& Page::addHeader(std::string_view id, std::string_view title, std::string_view subtitle) {
    if (mValid) addSetting(mModId, mId, make(id, ORESETTINGS_KIND_HEADER, title, subtitle));
    return *this;
}

Page& Page::addBanner(std::string_view id, std::string_view text, BannerKind kind) {
    if (!mValid) return *this;
    Setting s = make(id, ORESETTINGS_KIND_BANNER, text, {});
    switch (kind) {
    case BannerKind::Info: s.bannerKind = ORESETTINGS_BANNER_INFO; break;
    case BannerKind::Warning: s.bannerKind = ORESETTINGS_BANNER_WARNING; break;
    case BannerKind::Error: s.bannerKind = ORESETTINGS_BANNER_ERROR; break;
    case BannerKind::Success: s.bannerKind = ORESETTINGS_BANNER_SUCCESS; break;
    }
    addSetting(mModId, mId, std::move(s));
    return *this;
}

Page& Page::addToggle(std::string_view id, std::string_view label, std::string_view description, BoolGetter getter, BoolSetter setter) {
    if (!mValid) return *this;
    Setting s = make(id, ORESETTINGS_KIND_TOGGLE, label, description);
    s.getBool = std::move(getter);
    s.setBool = std::move(setter);
    if (s.getBool && s.setBool) addSetting(mModId, mId, std::move(s));
    return *this;
}

Page& Page::addSlider(std::string_view id, std::string_view label, std::string_view description, double min, double max, double step, NumberGetter getter, NumberSetter setter) {
    if (!mValid) return *this;
    Setting s = make(id, ORESETTINGS_KIND_SLIDER, label, description);
    s.min = min, s.max = max, s.step = step;
    s.getNumber = std::move(getter);
    s.setNumber = std::move(setter);
    if (s.getNumber && s.setNumber) addSetting(mModId, mId, std::move(s));
    return *this;
}

Page& Page::addDropdown(std::string_view id, std::string_view label, std::string_view description, std::vector<std::string> options, IndexGetter getter, IndexSetter setter) {
    if (!mValid) return *this;
    Setting s = make(id, ORESETTINGS_KIND_DROPDOWN, label, description);
    s.options  = std::move(options);
    s.getIndex = std::move(getter);
    s.setIndex = std::move(setter);
    if (s.getIndex && s.setIndex) addSetting(mModId, mId, std::move(s));
    return *this;
}

Page& Page::addTextField(std::string_view id, std::string_view label, std::string_view description, StringGetter getter, StringSetter setter, std::uint32_t maxLength) {
    if (!mValid) return *this;
    Setting s = make(id, ORESETTINGS_KIND_TEXT_FIELD, label, description);
    s.maxLength = maxLength;
    s.getString = std::move(getter);
    s.setString = std::move(setter);
    if (s.getString && s.setString) addSetting(mModId, mId, std::move(s));
    return *this;
}

Page& Page::addButton(std::string_view id, std::string_view label, std::string_view description, Action onClick) {
    if (!mValid) return *this;
    Setting s = make(id, ORESETTINGS_KIND_BUTTON, label, description);
    s.onClick = std::move(onClick);
    if (s.onClick) addSetting(mModId, mId, std::move(s));
    return *this;
}

// ---- Free functions ----

std::string_view libraryVersion() { return ORESETTINGS_VERSION_STRING; }

bool registerMod(const ModInfo& info) {
    bool ok;
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        ok = local().addMod(info) != nullptr;
    }
    if (ok) touch();
    return ok;
}

void unregisterMod(std::string_view modId) {
    // Tell the host first so no callback into this DLL can happen after we return.
    const OreSettingsHostTable* t = detail::election::hostTable();
    const std::string           id(modId);
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        Mod* mod = local().findMod(id);
        if (!mod) return;
        if (mod->forwarded && ORESETTINGS_TABLE_HAS(t, unregisterMod)) t->unregisterMod(id.c_str());
        local().removeMod(id);
    }
    // Last mod gone: stop listening so no callback outlives our DLL.
    bool empty;
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        empty = local().mods.empty();
    }
    if (empty) {
        std::lock_guard<std::mutex> lk(gInitMutex);
        if (gJoinListener) {
            ll::event::EventBus::getInstance().removeListener(gJoinListener);
            gJoinListener.reset();
        }
    }
}

Page& addPage(std::string_view modId, std::string_view pageId, std::string_view title, std::string_view icon) {
    Page* handle = nullptr;
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        if (auto* p = local().addPage(modId, pageId, std::string(title), std::string(icon))) handle = &p->handle;
    }
    if (!handle) return invalidPage();
    touch();
    return *handle;
}

bool attachNow() {
    ensureInit();
    const bool ok = detail::election::attachOrHost() != nullptr;
    flush();
    return ok;
}

HostInfo hostInfo() {
    HostInfo info;
    const OreSettingsHostTable* t = detail::election::hostTable();
    if (!t) return info;
    info.attached   = true;
    info.isThisCopy = t == detail::host::table();
    info.version    = {t->libVersionMajor, t->libVersionMinor, t->libVersionPatch};
    info.features   = t->size >= offsetof(OreSettingsHostTable, features) + sizeof(t->features) ? t->features : 0;
    return info;
}

} // namespace ore_settings
