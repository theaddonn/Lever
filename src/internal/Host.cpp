// Host side: the interfaces other copies call (marshalling of v1 descriptors into the model), the queryInterface
// dispatch table and the frozen bootstrap table.
//
// Every copy contains this code and publishes a bootstrap, but only the elected copy's is ever used by the others.
// The hook installation points below are intentionally stubs for now; see docs/DESIGN.md for the plan.

#include "Host.h"

#include "Election.h"
#include "Log.h"
#include "Marshal.h"
#include "Version.h"

#include <atomic>
#include <cstring>
#include <memory>

namespace lever::detail::host {

namespace {

// Keeps a copied callback set alive and calls `release` exactly once when the last std::function wrapper is destroyed.
struct Cookie {
    LeverSettingCallbacks cb{};
    ~Cookie() {
        if (cb.release) cb.release(cb.user);
    }
};

Setting toSetting(const LeverSettingDesc& d, const std::shared_ptr<Cookie>& c) {
    Setting s;
    s.owner       = c;
    s.id          = copyString(d.id);
    s.label       = copyString(d.label);
    s.description = copyString(d.description);
    s.kind        = static_cast<LeverKind>(d.kind);
    if (LEVER_DESC_HAS(LeverSettingDesc, d, step)) {
        s.min = d.min, s.max = d.max, s.step = d.step;
    }
    s.bannerKind = d.bannerKind <= LEVER_BANNER_SUCCESS ? static_cast<LeverBannerKind>(d.bannerKind) : LEVER_BANNER_INFO;
    s.maxLength  = d.maxLength ? d.maxLength : 256;
    if (LEVER_DESC_HAS(LeverSettingDesc, d, optionCount)) {
        for (std::uint32_t i = 0; d.options && i < d.optionCount; ++i) s.options.push_back(copyString(d.options[i]));
    }

    // Each wrapper captures the shared cookie so the user pointer stays valid while the setting exists. Members past
    // the callback struct's `size` read as zero, i.e. absent.
    const LeverSettingCallbacks& cb = c->cb;
    if (cb.getBool) s.getBool = [c] { return c->cb.getBool(c->cb.user) != 0; };
    if (cb.setBool) s.setBool = [c](bool v) { c->cb.setBool(c->cb.user, v ? 1 : 0); };
    if (cb.getNumber) s.getNumber = [c] { return c->cb.getNumber(c->cb.user); };
    if (cb.setNumber) s.setNumber = [c](double v) { c->cb.setNumber(c->cb.user, v); };
    if (cb.getIndex) s.getIndex = [c] { return static_cast<int>(c->cb.getIndex(c->cb.user)); };
    if (cb.setIndex) s.setIndex = [c](int v) { c->cb.setIndex(c->cb.user, static_cast<std::int32_t>(v)); };
    if (cb.getString) s.getString = [c] {
        const std::size_t need = c->cb.getString(c->cb.user, nullptr, 0);
        std::string       out(need + 1, '\0');
        c->cb.getString(c->cb.user, out.data(), out.size());
        out.resize(need);
        return out;
    };
    if (cb.setString) s.setString = [c](const std::string& v) { c->cb.setString(c->cb.user, v.c_str()); };
    if (cb.onClick) s.onClick = [c] { c->cb.onClick(c->cb.user); };
    return s;
}

int hostRegisterMod(const LeverModInfo* info) {
    LeverModInfo d;
    if (int rc = readDescriptor(info, LEVER_MODINFO_V1_MIN_SIZE, d); rc != LEVER_OK) return rc;
    if (!d.id || !*d.id) return LEVER_E_INVALID;
    ModInfo mi;
    mi.id          = copyString(d.id);
    mi.displayName = d.displayName && *d.displayName ? copyString(d.displayName) : mi.id;
    mi.author      = copyString(d.author);
    mi.description = copyString(d.description);
    mi.version     = {d.versionMajor, d.versionMinor, d.versionPatch};
    std::lock_guard<std::mutex> lk(model().mutex);
    return model().addMod(std::move(mi)) ? LEVER_OK : LEVER_E_DUPLICATE;
}

int hostRegisterPage(const LeverPageDesc* page) {
    LeverPageDesc d;
    if (int rc = readDescriptor(page, LEVER_PAGEDESC_V1_MIN_SIZE, d); rc != LEVER_OK) return rc;
    if (!d.modId || !d.pageId || !*d.pageId) return LEVER_E_INVALID;
    std::lock_guard<std::mutex> lk(model().mutex);
    if (!model().findMod(d.modId)) return LEVER_E_NOT_FOUND;
    if (model().findPage(d.modId, d.pageId)) return LEVER_E_DUPLICATE;
    std::string title = d.title && *d.title ? copyString(d.title) : copyString(d.pageId);
    Page*       p     = model().addPage(d.modId, d.pageId, std::move(title), copyString(d.icon));
    if (!p) return LEVER_E_INVALID;
    p->surfaces = LEVER_DESC_HAS(LeverPageDesc, d, surfaces) ? d.surfaces : LEVER_SURFACE_GLOBAL_SETTINGS;
    return LEVER_OK;
}

int hostSetPageSurfaces(const char* modId, const char* pageId, std::uint32_t surfaces) {
    if (!modId || !pageId) return LEVER_E_INVALID;
    std::lock_guard<std::mutex> lk(model().mutex);
    Page* p = model().findPage(modId, pageId);
    if (!p) return LEVER_E_NOT_FOUND;
    p->surfaces = surfaces;
    return LEVER_OK;
}

int hostAddSetting(const LeverSettingDesc* desc) {
    LeverSettingDesc d;
    if (int rc = readDescriptor(desc, LEVER_SETTINGDESC_V1_MIN_SIZE, d); rc != LEVER_OK) return rc;
    if (!d.modId || !d.pageId || !d.id || !*d.id) return LEVER_E_INVALID;
    if (d.kind > LEVER_KIND_HEADER) return LEVER_E_UNSUPPORTED; // a kind from the future: the client drops the widget

    auto cookie = std::make_shared<Cookie>();
    if (LEVER_DESC_HAS(LeverSettingDesc, d, callbacks) && d.callbacks) {
        if (int rc = readDescriptor(d.callbacks, LEVER_SETTINGCALLBACKS_V1_MIN_SIZE, cookie->cb); rc != LEVER_OK) {
            return rc; // the cookie still has a zero release: nothing to hand back
        }
    }

    std::lock_guard<std::mutex> lk(model().mutex);
    if (!model().findPage(d.modId, d.pageId)) {
        cookie->cb.release = nullptr; // not accepted: ownership stays with the caller
        return LEVER_E_NOT_FOUND;
    }
    Setting* s = model().addSetting(d.modId, d.pageId, toSetting(d, cookie));
    if (!s) {
        cookie->cb.release = nullptr;
        return LEVER_E_DUPLICATE;
    }
    return LEVER_OK;
}

int hostUnregisterMod(const char* modId) {
    if (!modId) return LEVER_E_INVALID;
    std::lock_guard<std::mutex> lk(model().mutex);
    return model().removeMod(modId) ? LEVER_OK : LEVER_E_NOT_FOUND;
}

// ---- Hook installation: STUBS. Nothing is hooked yet. ----

void installTier1Hooks() {
    // The native settings hook (platform/win32/SettingsHook.cpp) is installed by every copy at init; from now on
    // this copy is the one that injects the Mods page when the game builds its settings registry. Implemented so
    // far: the "Lever Mods" page under General with one text row per registered mod (info pages). Typed
    // settings (toggle/slider/dropdown/text/button/banner) are not rendered yet: their component constructors are
    // not exported (docs/RE-settings-screen.md).
    logInfo("tier 1: native settings page active (info rows only; typed settings pending)");
}

void installTier2Hooks() {
    // TODO(tier2): splice hbui bundle
    //   Hook the load of data/gui/dist/hbui/index-*.js and gameplay-*.js and append a plugin script that registers
    //   the Mods tab, pause-menu button and world-creation tab, backed by facets registered via FacetRegistry.
    //   Never write into the game folder.
    logInfo("tier 2 (hbui bundle splice) is not implemented yet");
}

std::atomic<bool> gActivated{false};

void hostActivate() {
    if (gActivated.exchange(true)) return; // hooks are installed exactly once
    logInfo(std::string("this copy (Lever ") + LEVER_VERSION_STRING + ") is the host");
    installTier1Hooks();
    installTier2Hooks();
}

std::uint64_t hostFeatures() { return LEVER_FEATURE_GLOBAL_SETTINGS | LEVER_FEATURE_INFO_PAGES; } // grows with the hooks
int           hostIsActivated() { return gActivated.load() ? 1 : 0; }

const LeverHostV1 kHostV1 = [] {
    LeverHostV1 v{};
    v.size        = sizeof(LeverHostV1);
    v.version     = 1;
    v.getFeatures = &hostFeatures;
    v.isActivated = &hostIsActivated;
    return v;
}();

const LeverRegistryV1 kRegistryV1 = [] {
    LeverRegistryV1 v{};
    v.size            = sizeof(LeverRegistryV1);
    v.version         = 1;
    v.registerMod     = &hostRegisterMod;
    v.unregisterMod   = &hostUnregisterMod;
    v.registerPage    = &hostRegisterPage;
    v.setPageSurfaces = &hostSetPageSurfaces;
    v.addSetting      = &hostAddSetting;
    return v;
}();

// The dispatch table. A new generation (v2) is a new row beside these, never a replacement. Each served table
// carries its own `version`; a query succeeds when that is >= the caller's minVersion.
struct InterfaceRow {
    const char*   name;
    const void*   table;
    std::uint32_t version;
};

const InterfaceRow kInterfaces[] = {
    {LEVER_IFACE_REGISTRY_V1, &kRegistryV1, kRegistryV1.version},
    {LEVER_IFACE_HOST_V1, &kHostV1, kHostV1.version},
};

std::uint32_t hostRegisteredCopies() { return election::registeredCopies(); }

} // namespace

const void* queryInterface(const char* name, std::uint32_t minVersion) {
    if (!name) return nullptr;
    for (const InterfaceRow& row : kInterfaces) {
        if (std::strcmp(row.name, name) == 0) return row.version >= minVersion ? row.table : nullptr;
    }
    return nullptr;
}

const LeverRegistryV1* registryV1() { return &kRegistryV1; }

Registry& model() {
    static Registry registry;
    return registry;
}

const LeverBootstrap* bootstrap() {
    static const LeverBootstrap b = [] {
        LeverBootstrap v{};
        v.size             = sizeof(LeverBootstrap);
        v.abiGeneration    = LEVER_ABI_GENERATION;
        v.libMajor         = LEVER_VERSION_MAJOR;
        v.libMinor         = LEVER_VERSION_MINOR;
        v.libPatch         = LEVER_VERSION_PATCH;
        v.queryInterface   = &queryInterface;
        v.activateHost     = &hostActivate;
        v.registeredCopies = &hostRegisteredCopies;
        return v;
    }();
    return &b;
}

} // namespace lever::detail::host
