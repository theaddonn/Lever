// Minimal LeviLamina client mod that embeds OreSettings. It shows the intended call pattern; the pages only appear
// in game once the library's hooks are implemented (see docs/DESIGN.md).

#include "oresettings/OreSettings.h"

#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"

#include <string>

namespace example {

namespace {
bool        gEnabled = true;
double      gStrength = 0.5;
int         gMode = 0;
std::string gName = "Steve";
} // namespace

class ExampleMod {
public:
    static ExampleMod& getInstance() {
        static ExampleMod instance;
        return instance;
    }

    ExampleMod() : mSelf(*ll::mod::NativeMod::current()) {}

    bool load() { return true; }

    bool enable() {
        using namespace ore_settings;
        registerMod({"oresettings_example", "OreSettings Example", {0, 1, 0}, "theaddonn", "Shows how to embed OreSettings."});
        addPage("oresettings_example", "main", "Example", "creator")
            .showIn(Surface::PauseMenu)
            .addHeader("h1", "General", "Everything is buffered until a host exists")
            .addToggle("enabled", "Enabled", "Master switch", [] { return gEnabled; }, [](bool v) { gEnabled = v; })
            .addSlider("strength", "Strength", "How much", 0.0, 1.0, 0.05, [] { return gStrength; }, [](double v) { gStrength = v; })
            .addDropdown("mode", "Mode", "Pick one", {"Low", "Medium", "High"}, [] { return gMode; }, [](int v) { gMode = v; })
            .addTextField("name", "Name", "Free text", [] { return gName; }, [](const std::string& v) { gName = v; })
            .addButton("reset", "Reset", "Restore defaults", [] { gEnabled = true; gStrength = 0.5; gMode = 0; })
            .addBanner("b1", "Pre-alpha: nothing is displayed yet.", BannerKind::Warning);
        mSelf.getLogger().info("OreSettings {} embedded", libraryVersion());
        return true;
    }

    bool disable() {
        ore_settings::unregisterMod("oresettings_example"); // the callbacks point into this DLL
        return true;
    }

private:
    ll::mod::NativeMod& mSelf;
};

} // namespace example

LL_REGISTER_MOD(example::ExampleMod, example::ExampleMod::getInstance());
