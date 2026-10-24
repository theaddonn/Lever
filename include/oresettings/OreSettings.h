#pragma once

/**
 * OreSettings: settings pages and mod info pages inside the vanilla Ore UI settings screen, a pause-menu entry and a
 * world-creation tab, for LeviLamina Minecraft Bedrock client mods.
 *
 * The library is linked statically into every mod that uses it. At runtime exactly one copy per process becomes the
 * "host" (it installs the game hooks once); the other copies forward to it through the C ABI in abi.h. The election
 * happens lazily at first use, so you never need to coordinate with other mods:
 *
 *  - Every function below is safe to call at any time after your mod is loaded (typically from `load()` or
 *    `enable()`). Registrations are buffered locally until a host exists and are then flushed in order.
 *  - Callbacks (getters, setters, onClick) run on the game's UI thread. Keep them short and do not throw.
 *  - Your mod must call `unregisterMod` from `disable()`/`unload()` (or at least before its DLL is unloaded), because
 *    the callbacks point into your DLL.
 *  - Mods built against an older or newer OreSettings interoperate; `hostInfo().features` tells you what the active
 *    host can do.
 */

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

namespace ore_settings {

struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
};

/// Where a page is shown.
enum class Surface {
    GlobalSettings, ///< "Mods" tab in the main settings screen (default for every page)
    PauseMenu,      ///< entry in the in-game pause menu
    WorldCreation,  ///< tab in the world-creation screen
};

/// Colour/intent of a banner.
enum class BannerKind { Info, Warning, Error, Success };

/// Shown on the generated info page of the mod (name, version, author, description).
struct ModInfo {
    std::string id; ///< unique, lowercase, e.g. "distanthorizons"
    std::string displayName;
    Version     version;
    std::string author;
    std::string description;
};

/// What the active host supports (mirrors the ORESETTINGS_FEATURE_* flags in abi.h).
struct HostInfo {
    bool          attached = false; ///< false while registrations are still buffered
    bool          isThisCopy = false; ///< true if this mod's embedded copy is the host
    Version       version;
    std::uint64_t features = 0;
};

using BoolGetter   = std::function<bool()>;
using BoolSetter   = std::function<void(bool)>;
using NumberGetter = std::function<double()>;
using NumberSetter = std::function<void(double)>;
using IndexGetter  = std::function<int()>;
using IndexSetter  = std::function<void(int)>;
using StringGetter = std::function<std::string()>;
using StringSetter = std::function<void(const std::string&)>;
using Action       = std::function<void()>;

/**
 * A page of a mod. Obtained from addPage(); the reference stays valid until the mod is unregistered. All add*
 * methods return the page so calls can be chained. A setting whose id is already used on the page is ignored.
 */
class Page {
public:
    /// False for the inert page returned when the mod id was unknown; every call on it is a no-op.
    [[nodiscard]] bool valid() const { return mValid; }
    [[nodiscard]] const std::string& id() const { return mId; }

    /// Additionally show this page on `surface` (every page is on Surface::GlobalSettings by default).
    Page& showIn(Surface surface);
    /// Show this page only on `surface`.
    Page& showOnlyIn(Surface surface);

    Page& addHeader(std::string_view id, std::string_view title, std::string_view subtitle = {});
    Page& addBanner(std::string_view id, std::string_view text, BannerKind kind = BannerKind::Info);
    Page& addToggle(std::string_view id, std::string_view label, std::string_view description, BoolGetter getter, BoolSetter setter);
    Page& addSlider(std::string_view id, std::string_view label, std::string_view description, double min, double max, double step, NumberGetter getter, NumberSetter setter);
    Page& addDropdown(std::string_view id, std::string_view label, std::string_view description, std::vector<std::string> options, IndexGetter getter, IndexSetter setter);
    Page& addTextField(std::string_view id, std::string_view label, std::string_view description, StringGetter getter, StringSetter setter, std::uint32_t maxLength = 256);
    Page& addButton(std::string_view id, std::string_view label, std::string_view description, Action onClick);

    Page(std::string modId, std::string pageId, bool valid) : mModId(std::move(modId)), mId(std::move(pageId)), mValid(valid) {}

private:
    std::string mModId;
    std::string mId;
    bool        mValid;
};

/// The version of the OreSettings copy embedded in this mod.
[[nodiscard]] std::string_view libraryVersion();

/// Registers a mod. Returns false if the id is empty or already registered by this copy.
bool registerMod(const ModInfo& info);

/// Removes a mod with all its pages and settings. Call before your DLL unloads.
void unregisterMod(std::string_view modId);

/**
 * Adds a page to a registered mod. `icon` is a vanilla icon key (for example "video", "creator", "addon") or empty
 * for the default. Returns the existing page if the id is already used; returns an invalid page if the mod is unknown.
 */
Page& addPage(std::string_view modId, std::string_view pageId, std::string_view title, std::string_view icon = {});

/// Runs the host election now instead of waiting for the first use. Normally not needed. Returns whether a host is attached.
bool attachNow();

/// Information about the active host (or `attached == false` while still buffering).
[[nodiscard]] HostInfo hostInfo();

} // namespace ore_settings
