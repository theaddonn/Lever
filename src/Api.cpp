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

using detail::Registry;
using detail::Setting;
using detail::client::local;

// Inert handles for unknown ids so callers can keep chaining without null checks.
Mod& invalidMod() {
    static Mod m("", false);
    return m;
}
Section& invalidSection() {
    static Section s("", "", false);
    return s;
}
Widget& invalidWidget() {
    static Widget w("", "", "", false);
    return w;
}

void touch() {
    detail::platform::ensureInit();
    detail::client::flush();
}

Widget& addSetting(const std::string& modId, const std::string& sectionId, Setting s) {
    Widget* handle = nullptr;
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        if (Setting* added = local().addSetting(modId, sectionId, std::move(s))) handle = &added->handle;
    }
    if (!handle) {
        detail::logWarn("ignored row on " + modId + "/" + sectionId + " (unknown section or duplicate id)");
        return invalidWidget();
    }
    touch();
    return *handle;
}

void setSurfaces(const std::string& modId, const std::string& sectionId, std::uint32_t mask, bool exclusive) {
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        if (auto* p = local().findPage(modId, sectionId)) p->surfaces = exclusive ? mask : (p->surfaces | mask);
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

Setting make(std::string_view id, LeverKind kind, std::string_view label, std::string_view description = {}) {
    Setting s;
    s.id          = std::string(id);
    s.kind        = kind;
    s.label       = std::string(label);
    s.description = std::string(description);
    return s;
}

/// Runs fn on the local setting (under the model lock). `isStatic` marks a change the host must be told about
/// (label, description, flags...); dynamic members (conditions, hooks) are read through the trampolines live.
template <class Fn>
void editSetting(const std::string& modId, const std::string& sectionId, const std::string& id, bool isStatic, Fn&& fn) {
    bool forward = false;
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        Setting* s = local().findSetting(modId, sectionId, id);
        if (!s) return;
        fn(*s);
        if (isStatic && s->forwarded) s->dirty = forward = true;
    }
    if (forward) touch();
}

} // namespace

// ---- Widget ----

Widget& Widget::label(std::string_view text) {
    if (mValid) editSetting(mModId, mSectionId, mId, true, [&](Setting& s) { s.label = std::string(text); });
    return *this;
}

Widget& Widget::description(std::string_view text) {
    if (mValid) editSetting(mModId, mSectionId, mId, true, [&](Setting& s) { s.description = std::string(text); });
    return *this;
}

Widget& Widget::visibleWhen(Condition condition) {
    if (mValid) editSetting(mModId, mSectionId, mId, false, [&](Setting& s) { s.visibleWhen = std::move(condition); });
    return *this;
}

Widget& Widget::enabledWhen(Condition condition) {
    if (mValid) editSetting(mModId, mSectionId, mId, false, [&](Setting& s) { s.enabledWhen = std::move(condition); });
    return *this;
}

Widget& Widget::state(StateFn fn) {
    if (mValid) editSetting(mModId, mSectionId, mId, false, [&](Setting& s) { s.stateFn = std::move(fn); });
    return *this;
}

Widget& Widget::onChange(Action fn) {
    if (mValid) editSetting(mModId, mSectionId, mId, false, [&](Setting& s) { s.onChange = std::move(fn); });
    return *this;
}

Widget& Widget::showSteps(bool show) {
    if (mValid)
        editSetting(mModId, mSectionId, mId, true, [&](Setting& s) {
            s.flags &= ~(LEVER_SETTING_FLAG_SHOW_STEPS | LEVER_SETTING_FLAG_HIDE_STEPS);
            s.flags |= show ? LEVER_SETTING_FLAG_SHOW_STEPS : LEVER_SETTING_FLAG_HIDE_STEPS;
        });
    return *this;
}

Widget& Widget::notifyChanged() {
    if (mValid) lever::notifyChanged(mModId, mSectionId, mId);
    return *this;
}

Section& Widget::section() {
    if (!mValid) return invalidSection();
    std::lock_guard<std::mutex> lk(local().mutex);
    if (auto* p = local().findPage(mModId, mSectionId)) return p->handle;
    return invalidSection();
}

// ---- Section ----

Section& Section::showIn(Surface surface) {
    if (mValid) setSurfaces(mModId, mId, surfaceBit(surface), false);
    return *this;
}

Section& Section::showOnlyIn(Surface surface) {
    if (mValid) setSurfaces(mModId, mId, surfaceBit(surface), true);
    return *this;
}

Section& Section::onChange(Action fn) {
    if (!mValid) return *this;
    std::lock_guard<std::mutex> lk(local().mutex);
    if (auto* p = local().findPage(mModId, mId)) p->onChange = std::move(fn);
    return *this;
}

Widget& Section::header(std::string_view id, std::string_view title, std::string_view subtitle) {
    if (!mValid) return invalidWidget();
    return addSetting(mModId, mId, make(id, LEVER_KIND_HEADER, title, subtitle));
}

Widget& Section::banner(std::string_view id, std::string_view text, BannerKind kind) {
    if (!mValid) return invalidWidget();
    Setting s = make(id, LEVER_KIND_BANNER, text);
    switch (kind) {
    case BannerKind::Info: s.bannerKind = LEVER_BANNER_INFO; break;
    case BannerKind::Warning: s.bannerKind = LEVER_BANNER_WARNING; break;
    case BannerKind::Error: s.bannerKind = LEVER_BANNER_ERROR; break;
    case BannerKind::Success: s.bannerKind = LEVER_BANNER_SUCCESS; break;
    }
    return addSetting(mModId, mId, std::move(s));
}

Widget& Section::button(std::string_view id, std::string_view label, Action onClick) {
    if (!mValid || !onClick) return invalidWidget();
    Setting s = make(id, LEVER_KIND_BUTTON, label);
    s.onClick = std::move(onClick);
    return addSetting(mModId, mId, std::move(s));
}

Widget& Section::toggle(std::string_view id, std::string_view label, BoolGetter getter, BoolSetter setter) {
    if (!mValid || !getter || !setter) return invalidWidget();
    Setting s = make(id, LEVER_KIND_TOGGLE, label);
    s.getBool = std::move(getter);
    s.setBool = std::move(setter);
    return addSetting(mModId, mId, std::move(s));
}

Widget& Section::slider(std::string_view id, std::string_view label, double min, double max, double step, NumberGetter getter, NumberSetter setter) {
    if (!mValid || !getter || !setter) return invalidWidget();
    Setting s = make(id, LEVER_KIND_SLIDER, label);
    s.min = min, s.max = max, s.step = step;
    s.getNumber = std::move(getter);
    s.setNumber = std::move(setter);
    return addSetting(mModId, mId, std::move(s));
}

Widget& Section::dropdown(std::string_view id, std::string_view label, std::vector<std::string> options, IndexGetter getter, IndexSetter setter) {
    if (!mValid || !getter || !setter) return invalidWidget();
    Setting s = make(id, LEVER_KIND_DROPDOWN, label);
    s.options  = std::move(options);
    s.getIndex = std::move(getter);
    s.setIndex = std::move(setter);
    return addSetting(mModId, mId, std::move(s));
}

Widget& Section::textField(std::string_view id, std::string_view label, StringGetter getter, StringSetter setter, std::uint32_t maxLength) {
    if (!mValid || !getter || !setter) return invalidWidget();
    Setting s   = make(id, LEVER_KIND_TEXT_FIELD, label);
    s.maxLength = maxLength;
    s.getString = std::move(getter);
    s.setString = std::move(setter);
    return addSetting(mModId, mId, std::move(s));
}

Section& Section::notifyChanged(std::string_view widgetId) {
    if (mValid) lever::notifyChanged(mModId, mId, widgetId);
    return *this;
}

// ---- Mod ----

Section& Mod::section(std::string_view sectionId, std::string_view title) {
    if (!mValid) return invalidSection();
    return addSection(mId, sectionId, title);
}

Mod& Mod::onChange(Action fn) {
    if (!mValid) return *this;
    std::lock_guard<std::mutex> lk(local().mutex);
    if (auto* m = local().findMod(mId)) m->onChange = std::move(fn);
    return *this;
}

Mod& Mod::notifyChanged() {
    if (mValid) lever::notifyChanged(mId);
    return *this;
}

// ---- Free functions ----

std::string_view libraryVersion() { return LEVER_VERSION_STRING; }

Mod& registerMod(const ModInfo& info) {
    detail::platform::ensureInit();
    Mod* handle = nullptr;
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        if (detail::Mod* m = local().addMod(info)) handle = &m->handle;
    }
    if (!handle) {
        detail::logWarn("registerMod ignored (empty or duplicate id '" + info.id + "')");
        return invalidMod();
    }
    touch();
    return *handle;
}

void unregisterMod(std::string_view modId) {
    const std::string id(modId);
    bool              empty;
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        detail::Mod* mod = local().findMod(id);
        if (!mod) return;
        // Tell the host first so no callback into this DLL can happen after we return.
        if (mod->forwarded) detail::client::unregisterOnHost(id);
        local().removeMod(id);
        empty = local().mods.empty();
    }
    // Last mod gone: stop listening so no callback outlives our DLL.
    if (empty) detail::platform::releaseListeners();
}

Section& addSection(std::string_view modId, std::string_view sectionId, std::string_view title) {
    Section* handle = nullptr;
    {
        std::lock_guard<std::mutex> lk(local().mutex);
        if (auto* p = local().addPage(modId, sectionId, std::string(title), std::string())) handle = &p->handle;
    }
    if (!handle) return invalidSection();
    touch();
    return *handle;
}

void notifyChanged(std::string_view modId, std::string_view sectionId, std::string_view widgetId) {
    if (modId.empty()) return;
    detail::client::notifyChangedOnHost(std::string(modId), std::string(sectionId), std::string(widgetId));
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
