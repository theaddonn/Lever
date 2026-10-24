#pragma once

// The in-process model: mods -> pages -> settings. Used twice:
//  - every embedded copy keeps a "local" registry (buffer + source of truth for its own mod's callbacks);
//  - the host copy keeps one more registry holding the union of everything every copy forwarded.

#include "lever/Lever.h"
#include "lever/abi.h"

#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <vector>

namespace lever::detail {

struct Setting {
    std::string           id, label, description;
    LeverKind       kind = LEVER_KIND_TOGGLE;
    double                min = 0, max = 1, step = 0;
    std::vector<std::string> options;
    LeverBannerKind bannerKind = LEVER_BANNER_INFO;
    std::uint32_t         maxLength  = 256;

    std::function<bool()>                 getBool;
    std::function<void(bool)>             setBool;
    std::function<double()>               getNumber;
    std::function<void(double)>           setNumber;
    std::function<int()>                  getIndex;
    std::function<void(int)>              setIndex;
    std::function<std::string()>          getString;
    std::function<void(const std::string&)> setString;
    std::function<void()>                 onClick;

    bool forwarded = false; // local registry only: already sent to the host
};

struct Page {
    Page(const std::string& modId, std::string pageId, std::string pageTitle, std::string pageIcon)
    : id(pageId), title(std::move(pageTitle)), icon(std::move(pageIcon)), handle(modId, std::move(pageId), true) {}

    std::string   id, title, icon;
    std::uint32_t surfaces          = LEVER_SURFACE_GLOBAL_SETTINGS;
    bool          forwarded         = false; // local registry only
    std::uint32_t forwardedSurfaces = 0;     // local registry only
    std::list<Setting> settings;
    lever::Page handle; // what addPage() hands to the consumer
};

struct Mod {
    ModInfo info;
    bool    forwarded = false; // local registry only
    std::list<Page> pages;
};

/// Not internally synchronised: hold `mutex` while touching anything reachable from it. std::list keeps element
/// addresses stable, which the forwarding trampolines and Page handles rely on.
class Registry {
public:
    std::mutex     mutex;
    std::list<Mod> mods;

    Mod*     findMod(std::string_view id);
    Page*    findPage(std::string_view modId, std::string_view pageId);
    Setting* findSetting(const Page& page, std::string_view id);

    /// nullptr if the id is empty or taken.
    Mod* addMod(ModInfo info);
    /// Returns the existing page for a known id.
    Page* addPage(std::string_view modId, std::string_view pageId, std::string title, std::string icon);
    /// nullptr if the mod/page is unknown or the id is taken.
    Setting* addSetting(std::string_view modId, std::string_view pageId, Setting setting);
    bool removeMod(std::string_view id);
};

} // namespace lever::detail
