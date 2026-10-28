#include "Model.h"

namespace lever::detail {

int stateOf(const Setting& s) {
    if (s.stateFn) {
        switch (s.stateFn()) {
        case State::Disabled: return LEVER_STATE_DISABLED;
        case State::Hidden: return LEVER_STATE_HIDDEN;
        case State::Enabled: break;
        }
        return LEVER_STATE_ENABLED;
    }
    if (s.visibleWhen && !s.visibleWhen()) return LEVER_STATE_HIDDEN;
    if (s.enabledWhen && !s.enabledWhen()) return LEVER_STATE_DISABLED;
    if (s.getState) {
        const int v = s.getState();
        return v == LEVER_STATE_DISABLED || v == LEVER_STATE_HIDDEN ? v : LEVER_STATE_ENABLED;
    }
    return LEVER_STATE_ENABLED;
}

Mod* Registry::findMod(std::string_view id) {
    for (auto& m : mods) {
        if (m.info.id == id) return &m;
    }
    return nullptr;
}

Page* Registry::findPage(std::string_view modId, std::string_view pageId) {
    Mod* mod = findMod(modId);
    if (!mod) return nullptr;
    for (auto& p : mod->pages) {
        if (p.id == pageId) return &p;
    }
    return nullptr;
}

Setting* Registry::findSetting(const Page& page, std::string_view id) {
    for (auto& s : const_cast<Page&>(page).settings) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

Setting* Registry::findSetting(std::string_view modId, std::string_view pageId, std::string_view id) {
    Page* page = findPage(modId, pageId);
    return page ? findSetting(*page, id) : nullptr;
}

Mod* Registry::addMod(ModInfo info) {
    if (info.id.empty() || findMod(info.id)) return nullptr;
    auto& mod  = mods.emplace_back();
    mod.info   = std::move(info);
    mod.handle = lever::Mod(mod.info.id, true);
    return &mod;
}

Page* Registry::addPage(std::string_view modId, std::string_view pageId, std::string title, std::string icon) {
    Mod* mod = findMod(modId);
    if (!mod || pageId.empty()) return nullptr;
    if (Page* existing = findPage(modId, pageId)) return existing;
    return &mod->pages.emplace_back(mod->info.id, std::string(pageId), std::move(title), std::move(icon));
}

Setting* Registry::addSetting(std::string_view modId, std::string_view pageId, Setting setting) {
    Mod* mod = findMod(modId);
    if (!mod) return nullptr;
    Page* page = findPage(modId, pageId);
    if (!page || setting.id.empty() || findSetting(*page, setting.id)) return nullptr;
    setting.mod    = mod;
    setting.page   = page;
    setting.handle = lever::Widget(mod->info.id, page->id, setting.id, true);
    return &page->settings.emplace_back(std::move(setting));
}

bool Registry::removeMod(std::string_view id) {
    for (auto it = mods.begin(); it != mods.end(); ++it) {
        if (it->info.id == id) {
            mods.erase(it);
            return true;
        }
    }
    return false;
}

} // namespace lever::detail
