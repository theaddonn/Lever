// Minimal LeviLamina client mod that embeds Lever. It shows the intended call pattern: register in enable(),
// unregister in disable(), and call notifyChanged() when values change behind the UI's back.

#include "lever/Lever.h"

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
        using namespace lever;
        registerMod({"lever_example", "Lever Example", {0, 1, 0}, "theaddonn", "Shows how to embed Lever."});
        addPage("lever_example", "main", "Example", "creator")
            .showIn(Surface::PauseMenu)
            .addHeader("h1", "General", "Everything is buffered until a host exists")
            .addToggle("enabled", "Enabled", "Master switch", [] { return gEnabled; }, [](bool v) { gEnabled = v; })
            .addSlider("strength", "Strength", "How much", 0.0, 1.0, 0.05, [] { return gStrength; }, [](double v) { gStrength = v; })
            .addDropdown("mode", "Mode", "Pick one", {"Low", "Medium", "High"}, [] { return gMode; }, [](int v) { gMode = v; })
            .addTextField("name", "Name", "Free text", [] { return gName; }, [](const std::string& v) { gName = v; })
            .addButton("reset", "Reset", "Restore defaults", [] {
                gEnabled = true, gStrength = 0.5, gMode = 0, gName = "Steve";
                notifyChanged("lever_example", "main"); // the open widgets re-read their getters
            })
            .addBanner("b1", "Pre-alpha: values are not persisted across restarts.", BannerKind::Warning);
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
