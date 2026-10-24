// Host side: the function table other copies call, and the model holding every copy's registrations.
//
// Every copy contains this code and exposes a table, but only the elected copy's table is ever used by the others.
// The hook installation points below are intentionally stubs for now; see docs/DESIGN.md for the plan.

#include "Host.h"

#include "Log.h"
#include "Version.h"

#include <atomic>
#include <memory>

namespace ore_settings::detail::host {

namespace {

// Keeps a C callback set alive and calls `release` exactly once when the last std::function wrapper is destroyed.
struct Cookie {
    OreSettingsSettingDesc desc{};
    ~Cookie() {
        if (desc.release) desc.release(desc.user);
    }
};

std::string str(const char* s) { return s ? std::string(s) : std::string(); }

Setting toSetting(const std::shared_ptr<Cookie>& c) {
    const OreSettingsSettingDesc& d = c->desc;
    Setting                       s;
    s.id          = str(d.id);
    s.label       = str(d.label);
    s.description = str(d.description);
    s.kind        = d.kind;
    s.min = d.min, s.max = d.max, s.step = d.step;
    s.bannerKind = d.bannerKind;
    s.maxLength  = d.maxLength;
    for (std::uint32_t i = 0; d.options && i < d.optionCount; ++i) s.options.push_back(str(d.options[i]));

    // Each wrapper captures the shared cookie so the user pointer stays valid while the setting exists.
    if (d.getBool) s.getBool = [c] { return c->desc.getBool(c->desc.user) != 0; };
    if (d.setBool) s.setBool = [c](bool v) { c->desc.setBool(c->desc.user, v ? 1 : 0); };
    if (d.getNumber) s.getNumber = [c] { return c->desc.getNumber(c->desc.user); };
    if (d.setNumber) s.setNumber = [c](double v) { c->desc.setNumber(c->desc.user, v); };
    if (d.getIndex) s.getIndex = [c] { return static_cast<int>(c->desc.getIndex(c->desc.user)); };
    if (d.setIndex) s.setIndex = [c](int v) { c->desc.setIndex(c->desc.user, static_cast<std::int32_t>(v)); };
    if (d.getString) s.getString = [c] {
        const size_t need = c->desc.getString(c->desc.user, nullptr, 0);
        std::string  out(need + 1, '\0');
        c->desc.getString(c->desc.user, out.data(), out.size());
        out.resize(need);
        return out;
    };
    if (d.setString) s.setString = [c](const std::string& v) { c->desc.setString(c->desc.user, v.c_str()); };
    if (d.onClick) s.onClick = [c] { c->desc.onClick(c->desc.user); };
    return s;
}

int hostRegisterMod(const OreSettingsModInfo* info) {
    if (!info || info->size < sizeof(OreSettingsModInfo) || !info->id) return ORESETTINGS_E_INVALID;
    ModInfo mi;
    mi.id          = str(info->id);
    mi.displayName = str(info->displayName);
    mi.author      = str(info->author);
    mi.description = str(info->description);
    mi.version     = {info->versionMajor, info->versionMinor, info->versionPatch};
    std::lock_guard<std::mutex> lk(model().mutex);
    return model().addMod(std::move(mi)) ? ORESETTINGS_OK : ORESETTINGS_E_DUPLICATE;
}

int hostRegisterPage(const OreSettingsPageDesc* page) {
    if (!page || page->size < sizeof(OreSettingsPageDesc) || !page->modId || !page->pageId) return ORESETTINGS_E_INVALID;
    std::lock_guard<std::mutex> lk(model().mutex);
    if (!model().findMod(page->modId)) return ORESETTINGS_E_NOT_FOUND;
    if (model().findPage(page->modId, page->pageId)) return ORESETTINGS_E_DUPLICATE;
    Page* p = model().addPage(page->modId, page->pageId, str(page->title), str(page->icon));
    if (!p) return ORESETTINGS_E_INVALID;
    p->surfaces = page->surfaces;
    return ORESETTINGS_OK;
}

int hostSetPageSurfaces(const char* modId, const char* pageId, std::uint32_t surfaces) {
    if (!modId || !pageId) return ORESETTINGS_E_INVALID;
    std::lock_guard<std::mutex> lk(model().mutex);
    Page* p = model().findPage(modId, pageId);
    if (!p) return ORESETTINGS_E_NOT_FOUND;
    p->surfaces = surfaces;
    return ORESETTINGS_OK;
}

int hostAddSetting(const OreSettingsSettingDesc* desc) {
    if (!desc || desc->size < sizeof(OreSettingsSettingDesc) || !desc->modId || !desc->pageId || !desc->id)
        return ORESETTINGS_E_INVALID;
    auto cookie  = std::make_shared<Cookie>();
    cookie->desc = *desc;
    std::lock_guard<std::mutex> lk(model().mutex);
    if (!model().findPage(desc->modId, desc->pageId)) return ORESETTINGS_E_NOT_FOUND;
    // On failure the rejected cookie is destroyed and would call release(): detach it so ownership stays with the caller.
    Setting* s = model().addSetting(desc->modId, desc->pageId, toSetting(cookie));
    if (!s) {
        cookie->desc.release = nullptr;
        return ORESETTINGS_E_DUPLICATE;
    }
    return ORESETTINGS_OK;
}

int hostUnregisterMod(const char* modId) {
    if (!modId) return ORESETTINGS_E_INVALID;
    std::lock_guard<std::mutex> lk(model().mutex);
    return model().removeMod(modId) ? ORESETTINGS_OK : ORESETTINGS_E_NOT_FOUND;
}

// ---- Hook installation: STUBS. Nothing is hooked yet. ----

void installTier1Hooks() {
    // TODO(tier1): hook Settings::IRegistry
    //   Register the "Mods" group, one group per page and the typed settings from model() in the native settings
    //   registry so the vanilla settingsGroupQuery / settings*Query facets list them; route UpdateSettingsCommand
    //   writes for our SettingsIds back to the setters in model().
    logInfo("tier 1 (Settings::IRegistry) hooks are not implemented yet");
}

void installTier2Hooks() {
    // TODO(tier2): splice hbui bundle
    //   Hook the load of data/gui/dist/hbui/index-*.js and gameplay-*.js and append a plugin script that registers
    //   the Mods tab, pause-menu button and world-creation tab, backed by facets registered via FacetRegistry.
    //   Never write into the game folder.
    logInfo("tier 2 (hbui bundle splice) is not implemented yet");
}

int hostActivate() {
    static std::atomic<bool> activated{false};
    if (activated.exchange(true)) return ORESETTINGS_OK; // hooks are installed exactly once
    logInfo(std::string("this copy (OreSettings ") + ORESETTINGS_VERSION_STRING + ") is the host");
    installTier1Hooks();
    installTier2Hooks();
    return ORESETTINGS_OK;
}

} // namespace

Registry& model() {
    static Registry registry;
    return registry;
}

const OreSettingsHostTable* table() {
    static const OreSettingsHostTable t = [] {
        OreSettingsHostTable v{};
        v.size            = sizeof(OreSettingsHostTable);
        v.abiVersion      = ORESETTINGS_ABI_VERSION;
        v.libVersionMajor = ORESETTINGS_VERSION_MAJOR;
        v.libVersionMinor = ORESETTINGS_VERSION_MINOR;
        v.libVersionPatch = ORESETTINGS_VERSION_PATCH;
        v.features        = 0; // grows as the hooks above get implemented
        v.registerMod     = &hostRegisterMod;
        v.registerPage    = &hostRegisterPage;
        v.setPageSurfaces = &hostSetPageSurfaces;
        v.addSetting      = &hostAddSetting;
        v.unregisterMod   = &hostUnregisterMod;
        v.activateHost    = &hostActivate;
        return v;
    }();
    return &t;
}

} // namespace ore_settings::detail::host
