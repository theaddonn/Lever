#pragma once

/**
 * Lever: a settings page for your mod inside the vanilla Ore UI settings screen (Settings -> Mods), for LeviLamina
 * Minecraft Bedrock client mods.
 *
 * The library is linked statically into every mod that uses it. At runtime exactly one copy per process becomes the
 * "host" (it installs the game hooks once); the other copies forward to it through the C ABI in abi.h. The election
 * happens lazily at first use, so you never need to coordinate with other mods.
 *
 *  - Every function below is safe to call at any time after your mod is loaded (typically from `enable()`).
 *    Registrations are buffered locally until a host exists and are then flushed in order; sections added after the
 *    settings screen was first built appear the next time it opens.
 *  - Callbacks (getters, setters, conditions, onChange, onClick) run on the game's UI thread. Keep them short and do
 *    not throw. Variables bound by reference must outlive the registration (globals or a long-lived config object).
 *  - Your mod must call `unregisterMod` from `disable()`/`unload()` (or at least before its DLL is unloaded), because
 *    the callbacks point into your DLL.
 *  - Mods built against an older or newer Lever interoperate; `hostInfo().features` tells you what the active
 *    host can do.
 *
 * Shape of a registration:
 *
 *     auto& mod = lever::registerMod({"mymod", "My Mod", {1, 0, 0}, "me", "What it does."});
 *     auto& general = mod.section("general", "General");
 *     general.toggle("enabled", "Enabled", gEnabled).description("Master switch");
 *     general.slider("strength", "Strength", gStrength, 0.0, 1.0, 0.05).visibleWhen([] { return gEnabled; });
 *     general.toggle("autoThreads", "Automatic thread count", gAutoThreads);
 *     general.slider("threads", "Threads", gThreads, 1, 32, 1).visibleWhen([] { return !gAutoThreads; });
 *     mod.onChange([] { saveConfig(); });
 *
 * Every mod gets one entry in the "Mods" list; its sections are rendered as titled groups of that page (a mod with a
 * single section shows its rows directly). Conditions (visibleWhen / enabledWhen / state) are re-evaluated after
 * every write made through the UI on the mod's page and on notifyChanged(), so rows can appear, disappear or grey
 * out depending on other values.
 */

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace lever {

struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
};

/// Where a section is shown. Only GlobalSettings is rendered by the current host; the others are recorded for later.
enum class Surface {
    GlobalSettings, ///< the mod's page under Settings -> Mods (default for every section)
    PauseMenu,      ///< entry in the in-game pause menu
    WorldCreation,  ///< tab in the world-creation screen
};

/// Colour/intent of a banner.
enum class BannerKind { Info, Warning, Error, Success };

/// Dynamic state of a row.
enum class State { Enabled, Disabled, Hidden };

/// Shown on the mod list (name, version, author, description).
struct ModInfo {
    std::string id; ///< unique, lowercase, e.g. "distanthorizons"
    std::string displayName;
    Version     version;
    std::string author;
    std::string description;
};

/// What the active host supports (mirrors the LEVER_FEATURE_* flags in abi.h).
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
using Condition    = std::function<bool()>;
using StateFn      = std::function<State()>;

class Section;

/**
 * One row of a section, returned by the Section::* methods. Modifiers can be chained and may be called at any time
 * (also long after registration: label and description changes show the next time the page is built, conditions
 * and hooks take effect at once). The reference stays valid until the mod is unregistered.
 */
class Widget {
public:
    /// False for the inert widget returned when the section was invalid or the id was a duplicate.
    [[nodiscard]] bool valid() const { return mValid; }
    [[nodiscard]] const std::string& id() const { return mId; }

    Widget& label(std::string_view text);
    Widget& description(std::string_view text);

    /// Row hidden while `condition` returns false.
    Widget& visibleWhen(Condition condition);
    /// Row greyed out (not editable) while `condition` returns false.
    Widget& enabledWhen(Condition condition);
    /// Full control over the state; takes precedence over visibleWhen/enabledWhen.
    Widget& state(StateFn fn);

    /// Runs after the UI wrote a value through this widget (or clicked the button), after the setter.
    Widget& onChange(Action fn);

    /// Sliders: force the step dividers on or off (default: drawn only for sliders with few positions).
    Widget& showSteps(bool show);

    /// Re-reads this widget's getter and state now (after a change made outside the UI). Game thread only.
    Widget& notifyChanged();

    /// The section this widget belongs to (to continue adding rows in one chain).
    Section& section();

    Widget(std::string modId, std::string sectionId, std::string id, bool valid)
    : mModId(std::move(modId)), mSectionId(std::move(sectionId)), mId(std::move(id)), mValid(valid) {}

private:
    std::string mModId, mSectionId, mId;
    bool        mValid;
};

namespace detail {
template <class T>
T fromDouble(double v) {
    if constexpr (std::is_same_v<T, bool>) return v != 0;
    else if constexpr (std::is_integral_v<T>) return static_cast<T>(std::llround(v));
    else return static_cast<T>(v);
}
} // namespace detail

/**
 * A titled group of rows on the mod's page. Obtained from Mod::section(); the reference stays valid until the mod is
 * unregistered. A row whose id is already used in the section is ignored (an invalid Widget comes back).
 */
class Section {
public:
    /// False for the inert section returned when the mod id was unknown; every call on it is a no-op.
    [[nodiscard]] bool valid() const { return mValid; }
    [[nodiscard]] const std::string& id() const { return mId; }

    /// Additionally show this section on `surface` (every section is on Surface::GlobalSettings by default).
    Section& showIn(Surface surface);
    /// Show this section only on `surface`.
    Section& showOnlyIn(Surface surface);

    /// Runs after the UI wrote a value through any widget of this section (after the widget's own onChange).
    Section& onChange(Action fn);

    Widget& header(std::string_view id, std::string_view title, std::string_view subtitle = {});
    Widget& banner(std::string_view id, std::string_view text, BannerKind kind = BannerKind::Info);
    Widget& button(std::string_view id, std::string_view label, Action onClick);

    // Rows bound to callbacks.
    Widget& toggle(std::string_view id, std::string_view label, BoolGetter getter, BoolSetter setter);
    Widget& slider(std::string_view id, std::string_view label, double min, double max, double step, NumberGetter getter, NumberSetter setter);
    Widget& dropdown(std::string_view id, std::string_view label, std::vector<std::string> options, IndexGetter getter, IndexSetter setter);
    Widget& textField(std::string_view id, std::string_view label, StringGetter getter, StringSetter setter, std::uint32_t maxLength = 256);

    // Rows bound to a variable (read on every query, written by the UI). The variable must outlive the registration.
    Widget& toggle(std::string_view id, std::string_view label, bool& value) {
        return toggle(id, label, [&value] { return value; }, [&value](bool v) { value = v; });
    }
    template <class T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    Widget& slider(std::string_view id, std::string_view label, T& value, double min, double max, double step) {
        return slider(
            id, label, min, max, step,
            [&value] { return static_cast<double>(value); },
            [&value](double v) { value = detail::fromDouble<T>(v); }
        );
    }
    template <class T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
    Widget& dropdown(std::string_view id, std::string_view label, T& value, std::vector<std::string> options) {
        return dropdown(
            id, label, std::move(options),
            [&value] { return static_cast<int>(value); },
            [&value](int v) { value = static_cast<T>(v); }
        );
    }
    Widget& textField(std::string_view id, std::string_view label, std::string& value, std::uint32_t maxLength = 256) {
        return textField(id, label, [&value] { return value; }, [&value](const std::string& v) { value = v; }, maxLength);
    }

    /// Re-reads the getters and states of this section (or of one widget) now. Game thread only.
    Section& notifyChanged(std::string_view widgetId = {});

    Section(std::string modId, std::string sectionId, bool valid)
    : mModId(std::move(modId)), mId(std::move(sectionId)), mValid(valid) {}

private:
    std::string mModId;
    std::string mId;
    bool        mValid;
};

/// A registered mod. Obtained from registerMod(); the reference stays valid until unregisterMod().
class Mod {
public:
    /// False for the inert handle returned when registration failed; every call on it is a no-op.
    [[nodiscard]] bool valid() const { return mValid; }
    [[nodiscard]] const std::string& id() const { return mId; }

    /// Adds (or returns the existing) section `sectionId` titled `title`.
    Section& section(std::string_view sectionId, std::string_view title);

    /// Runs after the UI wrote a value through any widget of this mod (after the widget's and section's onChange).
    /// The natural place to persist the configuration.
    Mod& onChange(Action fn);

    /// Re-reads every getter and state of this mod now. Game thread only.
    Mod& notifyChanged();

    Mod(std::string modId, bool valid) : mId(std::move(modId)), mValid(valid) {}

private:
    std::string mId;
    bool        mValid;
};

/// The version of the Lever copy embedded in this mod.
[[nodiscard]] std::string_view libraryVersion();

/// Registers a mod. Returns an invalid handle if the id is empty or already registered by this copy.
Mod& registerMod(const ModInfo& info);

/// Removes a mod with all its sections and rows. Call before your DLL unloads.
void unregisterMod(std::string_view modId);

/// Adds a section without going through the Mod handle. Returns the existing section if the id is already used;
/// returns an invalid section if the mod is unknown.
Section& addSection(std::string_view modId, std::string_view sectionId, std::string_view title);

/**
 * Tells open widgets to re-read their getters and states after values changed outside the UI (a keybind toggled an
 * option, a reset button restored defaults, a config file was reloaded). Empty `sectionId` / `widgetId` mean "all".
 * A no-op when no host is attached or the host predates this call. Game thread only.
 */
void notifyChanged(std::string_view modId, std::string_view sectionId = {}, std::string_view widgetId = {});

/// Runs the host election now instead of waiting for the first use. Normally not needed. Returns whether a host is attached.
bool attachNow();

/// Information about the active host (or `attached == false` while still buffering).
[[nodiscard]] HostInfo hostInfo();

} // namespace lever
