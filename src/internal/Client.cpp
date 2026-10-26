#include "Client.h"

#include "Election.h"
#include "Log.h"

#include <atomic>
#include <cstring>

namespace lever::detail::client {

namespace {

// ---- C trampolines: `user` is the stable address of the local Setting. ----

int          tBool(void* u) { return static_cast<Setting*>(u)->getBool() ? 1 : 0; }
void         tSetBool(void* u, int v) { static_cast<Setting*>(u)->setBool(v != 0); }
double       tNumber(void* u) { return static_cast<Setting*>(u)->getNumber(); }
void         tSetNumber(void* u, double v) { static_cast<Setting*>(u)->setNumber(v); }
std::int32_t tIndex(void* u) { return static_cast<std::int32_t>(static_cast<Setting*>(u)->getIndex()); }
void         tSetIndex(void* u, std::int32_t v) { static_cast<Setting*>(u)->setIndex(static_cast<int>(v)); }
std::size_t  tString(void* u, char* buf, std::size_t cap) {
    const std::string s = static_cast<Setting*>(u)->getString();
    if (buf && cap > 0) {
        const std::size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
        std::memcpy(buf, s.data(), n);
        buf[n] = '\0';
    }
    return s.size();
}
void tSetString(void* u, const char* v) { static_cast<Setting*>(u)->setString(v ? v : ""); }
void tClick(void* u) { static_cast<Setting*>(u)->onClick(); }

void forwardSetting(const LeverRegistryV1* r, const Mod& mod, const detail::Page& page, Setting& s) {
    std::vector<const char*> opts;
    for (const auto& o : s.options) opts.push_back(o.c_str());

    LeverSettingCallbacks cb{};
    cb.size = sizeof(cb);
    cb.user = &s;
    if (s.getBool) cb.getBool = &tBool;
    if (s.setBool) cb.setBool = &tSetBool;
    if (s.getNumber) cb.getNumber = &tNumber;
    if (s.setNumber) cb.setNumber = &tSetNumber;
    if (s.getIndex) cb.getIndex = &tIndex;
    if (s.setIndex) cb.setIndex = &tSetIndex;
    if (s.getString) cb.getString = &tString;
    if (s.setString) cb.setString = &tSetString;
    if (s.onClick) cb.onClick = &tClick;

    LeverSettingDesc d{};
    d.size        = sizeof(d);
    d.kind        = static_cast<std::uint32_t>(s.kind);
    d.modId       = mod.info.id.c_str();
    d.pageId      = page.id.c_str();
    d.id          = s.id.c_str();
    d.label       = s.label.c_str();
    d.description = s.description.c_str();
    d.min = s.min, d.max = s.max, d.step = s.step;
    d.options     = opts.empty() ? nullptr : opts.data();
    d.optionCount = static_cast<std::uint32_t>(opts.size());
    d.bannerKind  = static_cast<std::uint32_t>(s.bannerKind);
    d.maxLength   = s.maxLength;
    d.callbacks   = &cb;
    const int rc  = r->addSetting(&d);
    // UNSUPPORTED = the host does not know this widget kind: dropped for good, not worth retrying.
    if (rc == LEVER_OK || rc == LEVER_E_UNSUPPORTED) s.forwarded = true;
}

bool complete(const LeverRegistryV1* r) {
    return LEVER_IFACE_HAS(LeverRegistryV1, r, registerMod) && LEVER_IFACE_HAS(LeverRegistryV1, r, unregisterMod)
        && LEVER_IFACE_HAS(LeverRegistryV1, r, registerPage) && LEVER_IFACE_HAS(LeverRegistryV1, r, setPageSurfaces)
        && LEVER_IFACE_HAS(LeverRegistryV1, r, addSetting);
}

std::atomic<bool> gWarned{false};

} // namespace

void resetWarnings() { gWarned.store(false); }

Registry& local() {
    static Registry r;
    return r;
}

const LeverRegistryV1* registryOf(const LeverBootstrap* host) {
    const void*              found = nullptr;
    if (LEVER_BOOTSTRAP_VALID(host)) found = host->queryInterface(LEVER_IFACE_REGISTRY_V1, 1);
    const auto* r = static_cast<const LeverRegistryV1*>(found);
    if (r && complete(r)) return r;
    if (!gWarned.exchange(true)) {
        logWarn(std::string("the host does not serve ") + LEVER_IFACE_REGISTRY_V1
                + "; registrations stay buffered and will not be displayed");
    }
    return nullptr;
}

void forwardAll(Registry& reg, const LeverRegistryV1* iface) {
    if (!iface) return;
    std::lock_guard<std::mutex> lk(reg.mutex);
    for (Mod& mod : reg.mods) {
        if (!mod.forwarded) {
            LeverModInfo mi{};
            mi.size         = sizeof(mi);
            mi.id           = mod.info.id.c_str();
            mi.displayName  = mod.info.displayName.c_str();
            mi.author       = mod.info.author.c_str();
            mi.description  = mod.info.description.c_str();
            mi.versionMajor = mod.info.version.major;
            mi.versionMinor = mod.info.version.minor;
            mi.versionPatch = mod.info.version.patch;
            if (iface->registerMod(&mi) != LEVER_OK) continue; // retry on the next flush
            mod.forwarded = true;
        }
        for (auto& page : mod.pages) {
            if (!page.forwarded) {
                LeverPageDesc pd{};
                pd.size     = sizeof(pd);
                pd.modId    = mod.info.id.c_str();
                pd.pageId   = page.id.c_str();
                pd.title    = page.title.c_str();
                pd.icon     = page.icon.empty() ? nullptr : page.icon.c_str();
                pd.surfaces = page.surfaces;
                if (iface->registerPage(&pd) != LEVER_OK) continue;
                page.forwarded         = true;
                page.forwardedSurfaces = page.surfaces;
            } else if (page.forwardedSurfaces != page.surfaces) {
                if (iface->setPageSurfaces(mod.info.id.c_str(), page.id.c_str(), page.surfaces) == LEVER_OK)
                    page.forwardedSurfaces = page.surfaces;
            }
            for (auto& s : page.settings) {
                if (!s.forwarded) forwardSetting(iface, mod, page, s);
            }
        }
    }
}

void flush() {
    const LeverBootstrap* host = election::host();
    if (!host) return;
    forwardAll(local(), registryOf(host));
}

void attachAndFlush() {
    election::attachOrHost();
    flush();
}

void unregisterOnHost(const std::string& modId) {
    const LeverBootstrap* host = election::host();
    if (!host) return;
    if (const LeverRegistryV1* r = registryOf(host)) r->unregisterMod(modId.c_str());
}

void notifyChangedOnHost(const std::string& modId, const std::string& pageId, const std::string& settingId) {
    const LeverBootstrap* host = election::host();
    if (!host) return;
    const LeverRegistryV1* r = registryOf(host);
    if (!LEVER_IFACE_HAS(LeverRegistryV1, r, notifyChanged)) return; // an older host: nothing to refresh
    r->notifyChanged(modId.c_str(), pageId.c_str(), settingId.c_str());
}

} // namespace lever::detail::client
