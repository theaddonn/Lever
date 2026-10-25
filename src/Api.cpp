// C++ API -> local model -> (when a host is attached) lever.registry.v1 calls on the host (see internal/Client.cpp).
// Platform specifics (election slot, level-join event, logger) live behind internal/Platform.h.

#include "lever/Lever.h"

#include "internal/Client.h"
#include "internal/Election.h"
#include "internal/Host.h"
#include "internal/Log.h"
#include "internal/Model.h"
#include "internal/Platform.h"
#include "internal/Version.h"

#include <memory>
#include <mutex>

namespace lever {

namespace {

using detail::Mod;
using detail::Registry;
using detail::Setting;
using detail::client::local;

// Returned for unknown mod ids so callers can keep chaining without null checks.
Page& invalidPage() {
    static Page p("", "", false);
    return p;
}

void touch() {
    detail::platform::ensureInit();
    detail::client::flush();
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
    case Surface::GlobalSettings: return LEVER_SURFACE_GLOBAL_SETTINGS;
    case Surface::PauseMenu: return LEVER_SURFACE_PAUSE_MENU;
    case Surface::WorldCreation: return LEVER_SURFACE_WORLD_CREATION;
    }
    return 0;
}

Setting make(std::string_view id, LeverKind kind, std::string_view label, std::string_view description) {
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
    if (mValid) addSetting(mModId, mId, make(id, LEVER_KIND_HEADER, title, subtitle));
    return *this;
}

Page& Page::addBanner(std::string_view id, std::string_view text, BannerKind kind) {
    if (!mValid) return *this;
    Setting s = make(id, LEVER_KIND_BANNER, text, {});
    switch (kind) {
    case BannerKind::Info: s.bannerKind = LEVER_BANNER_INFO; break;
    case BannerKind::Warning: s.bannerKind = LEVER_BANNER_WARNING; break;
    case BannerKind::Error: s.bannerKind = LEVER_BANNER_ERROR; break;
    case BannerKind::Success: s.bannerKind = LEVER_BANNER_SUCCESS; break;
    }
    addSetting(mModId, mId, std::move(s));
    return *this;
}

Page& Page::addToggle(std::string_view id, std::string_view label, std::string_view description, BoolGetter getter, BoolSetter setter) {
    if (!mValid) return *this;
    Setting s = make(id, LEVER_KIND_TOGGLE, label, description);
    s.getBool = std::move(getter);
    s.setBool = std::move(setter);
    if (s.getBool && s.setBool) addSetting(mModId, mId, std::move(s));
    return *this;
}

Page& Page::addSlider(std::string_view id, std::string_view label, std::string_view description, double min, double max, double step, NumberGetter getter, NumberSetter setter) {
    if (!mValid) return *this;
    Setting s = make(id, LEVER_KIND_SLIDER, label, description);
    s.min = min, s.max = max, s.step = step;
    s.getNumber = std::move(getter);
    s.setNumber = std::move(setter);
    if (s.getNumber && s.setNumber) addSetting(mModId, mId, std::move(s));
    return *this;
}

Page& Page::addDropdown(std::string_view id, std::string_view label, std::string_view description, std::vector<std::string> options, IndexGetter getter, IndexSetter setter) {
    if (!mValid) return *this;
    Setting s = make(id, LEVER_KIND_DROPDOWN, label, description);
    s.options  = std::move(options);
    s.getIndex = std::move(getter);
    s.setIndex = std::move(setter);
    if (s.getIndex && s.setIndex) addSetting(mModId, mId, std::move(s));
    return *this;
}

Page& Page::addTextField(std::string_view id, std::string_view label, std::string_view description, StringGetter getter, StringSetter setter, std::uint32_t maxLength) {
    if (!mValid) return *this;
    Setting s = make(id, LEVER_KIND_TEXT_FIELD, label, description);
    s.maxLength = maxLength;
    s.getString = std::move(getter);
    s.setString = std::move(setter);
    if (s.getString && s.setString) addSetting(mModId, mId, std::move(s));
    return *this;
}

Page& Page::addButton(std::string_view id, std::string_view label, std::string_view description, Action onClick) {
    if (!mValid) return *this;
    Setting s = make(id, LEVER_KIND_BUTTON, label, description);
    s.onClick = std::move(onClick);
    if (s.onClick) addSetting(mModId, mId, std::move(s));
    return *this;
}


// ---- Free functions ----

std::string_view libraryVersion() { return LEVER_VERSION_STRING; }

bool registerMod(const ModInfo& info) {
    detail::platform::ensureInit();
    bool ok;
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        ok = local().addMod(info) != nullptr;
    }
    if (ok) touch();
    return ok;
}

void unregisterMod(std::string_view modId) {
    const std::string id(modId);
    bool              empty;
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        Mod* mod = local().findMod(id);
        if (!mod) return;
        // Tell the host first so no callback into this DLL can happen after we return.
        if (mod->forwarded) detail::client::unregisterOnHost(id);
        local().removeMod(id);
        empty = local().mods.empty();
    }
    // Last mod gone: stop listening so no callback outlives our DLL.
    if (empty) detail::platform::releaseListeners();
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
    detail::platform::ensureInit();
    const bool ok = detail::election::attachOrHost() != nullptr;
    detail::client::flush();
    return ok;
}

HostInfo hostInfo() {
    HostInfo info;
    const LeverBootstrap* b = detail::election::host();
    if (!LEVER_BOOTSTRAP_VALID(b)) return info;
    info.attached   = true;
    info.isThisCopy = b == detail::host::bootstrap();
    info.version    = {b->libMajor, b->libMinor, b->libPatch};
    if (const auto* h = static_cast<const LeverHostV1*>(b->queryInterface(LEVER_IFACE_HOST_V1, 1));
        LEVER_IFACE_HAS(LeverHostV1, h, getFeatures))
        info.features = h->getFeatures();
    return info;
}

} // namespace lever
