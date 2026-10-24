#include "Model.h"

namespace lever::detail {

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

Mod* Registry::addMod(ModInfo info) {
    if (info.id.empty() || findMod(info.id)) return nullptr;
    auto& mod = mods.emplace_back();
    mod.info  = std::move(info);
    return &mod;
}

Page* Registry::addPage(std::string_view modId, std::string_view pageId, std::string title, std::string icon) {
    Mod* mod = findMod(modId);
    if (!mod || pageId.empty()) return nullptr;
    if (Page* existing = findPage(modId, pageId)) return existing;
    return &mod->pages.emplace_back(mod->info.id, std::string(pageId), std::move(title), std::move(icon));
}

Setting* Registry::addSetting(std::string_view modId, std::string_view pageId, Setting setting) {
    Page* page = findPage(modId, pageId);
    if (!page || setting.id.empty() || findSetting(*page, setting.id)) return nullptr;
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
