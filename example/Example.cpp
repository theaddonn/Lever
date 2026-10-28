// Minimal LeviLamina client mod that embeds Lever. It shows the intended call pattern: register in enable(),
// unregister in disable(), bind rows to variables, make rows depend on each other, persist from onChange() and call
// notifyChanged() when values change behind the UI's back.

#include "lever/Lever.h"

#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"

#include <string>

namespace example {

namespace {
bool        gEnabled     = true;
double      gStrength    = 0.5;
int         gMode        = 0;
std::string gName        = "Steve";
bool        gAutoThreads = true;
int         gThreads     = 4;
int         gBrightness  = 50;
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
        using namespace lever;
        Mod& mod = registerMod({"lever_example", "Lever Example", {0, 2, 0}, "theaddonn", "Shows how to embed Lever."});

        Section& general = mod.section("general", "General");
        general.banner("b1", "Pre-alpha: values are not persisted across restarts.", BannerKind::Warning);
        general.toggle("enabled", "Enabled", gEnabled).description("Master switch; the rows below follow it");
        general.slider("strength", "Strength", gStrength, 0.0, 1.0, 0.05)
            .description("21 positions: no divider ticks")
            .enabledWhen([] { return gEnabled; });
        general.slider("brightness", "Brightness", gBrightness, 0, 100, 10)
            .description("11 positions: ticks forced on")
            .showSteps(true)
            .enabledWhen([] { return gEnabled; });
        general.dropdown("mode", "Mode", gMode, {"Low", "Medium", "High"}).visibleWhen([] { return gEnabled; });
        general.textField("name", "Name", gName).description("Applied when the field loses focus");

        Section& threads = mod.section("threads", "Threads");
        threads.toggle("auto", "Automatic thread count", gAutoThreads).description("Off shows a manual override");
        threads.slider("count", "Threads", gThreads, 1, 32, 1).visibleWhen([] { return !gAutoThreads; });
        threads.button("reset", "Reset", [&] {
            gEnabled = true, gStrength = 0.5, gMode = 0, gName = "Steve", gAutoThreads = true, gThreads = 4, gBrightness = 50;
            mod.notifyChanged(); // the open rows re-read their values and states
        });

        mod.onChange([this] { mSelf.getLogger().info("a value changed through the settings screen"); });
        mSelf.getLogger().info("Lever {} embedded", libraryVersion());
        return true;
    }

    bool disable() {
        lever::unregisterMod("lever_example"); // the callbacks point into this DLL
        return true;
    }

private:
    ll::mod::NativeMod& mSelf;
};

} // namespace example

LL_REGISTER_MOD(example::ExampleMod, example::ExampleMod::getInstance());
