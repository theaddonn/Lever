// The 0.2.0 additions: dynamic states (visibleWhen / enabledWhen / state), onChange hooks, modifiers after
// forwarding (updateSetting), slider flags and the slider grid.

#include <doctest.h>

#include "TestSupport.h"

#include "internal/SliderGrid.h"
#include "lever/Lever.h"

#include <string>
#include <vector>

using namespace lever::detail;
using lever::test::resetAll;

namespace {

Setting* hostSetting(const char* mod, const char* page, const char* id) {
    return host::model().findSetting(mod, page, id);
}

} // namespace

TEST_CASE("state: stateOf combines stateFn, conditions and the forwarded callback") {
    Setting s;
    CHECK(stateOf(s) == LEVER_STATE_ENABLED);
    bool visible = true, enabled = true;
    s.visibleWhen = [&] { return visible; };
    s.enabledWhen = [&] { return enabled; };
    CHECK(stateOf(s) == LEVER_STATE_ENABLED);
    enabled = false;
    CHECK(stateOf(s) == LEVER_STATE_DISABLED);
    visible = false;
    CHECK(stateOf(s) == LEVER_STATE_HIDDEN); // hidden wins over disabled
    s.stateFn = [] { return lever::State::Enabled; };
    CHECK(stateOf(s) == LEVER_STATE_ENABLED); // the explicit state function takes precedence
    s.stateFn = [] { return lever::State::Disabled; };
    CHECK(stateOf(s) == LEVER_STATE_DISABLED);
    s.stateFn = nullptr, s.visibleWhen = nullptr, s.enabledWhen = nullptr;
    s.getState = [] { return 2; };
    CHECK(stateOf(s) == LEVER_STATE_HIDDEN);
    s.getState = [] { return 42; }; // unknown value from a newer mod: enabled
    CHECK(stateOf(s) == LEVER_STATE_ENABLED);
}

TEST_CASE("state: conditions installed after forwarding are seen by the host through getState") {
    resetAll();
    bool autoThreads = true;
    int  threads     = 4;
    lever::Mod&     mod = lever::registerMod({"m", "M", {}, "", ""});
    lever::Section& sec = mod.section("general", "General");
    sec.toggle("auto", "Automatic", autoThreads);
    lever::Widget& manual = sec.slider("threads", "Threads", threads, 1, 32, 1);
    REQUIRE(lever::attachNow()); // forwarded now, before the condition exists

    manual.visibleWhen([&] { return !autoThreads; }); // installed afterwards: no second round trip needed
    std::lock_guard<std::mutex> lk(host::model().mutex);
    Setting* hs = hostSetting("m", "general", "threads");
    REQUIRE(hs);
    REQUIRE(hs->getState);
    CHECK(stateOf(*hs) == LEVER_STATE_HIDDEN);
    autoThreads = false;
    CHECK(stateOf(*hs) == LEVER_STATE_ENABLED);
    manual.enabledWhen([&] { return threads > 1; });
    threads = 1;
    CHECK(stateOf(*hs) == LEVER_STATE_DISABLED);
    Setting* ha = hostSetting("m", "general", "auto");
    REQUIRE(ha);
    CHECK(stateOf(*ha) == LEVER_STATE_ENABLED); // no condition: enabled
}

TEST_CASE("hooks: a write through the host runs widget, section and mod onChange in that order") {
    resetAll();
    std::vector<std::string> log;
    bool                     value = false;
    double                   number = 0;
    std::string              text;
    lever::Mod&     mod = lever::registerMod({"m", "M", {}, "", ""});
    lever::Section& sec = mod.section("s", "S");
    sec.toggle("t", "T", value).onChange([&] { log.push_back("widget"); });
    sec.slider("n", "N", number, 0, 10, 1);
    sec.textField("x", "X", text);
    sec.button("b", "B", [&] { log.push_back("click"); });
    sec.onChange([&] { log.push_back("section"); });
    mod.onChange([&] { log.push_back("mod"); });
    REQUIRE(lever::attachNow());

    std::lock_guard<std::mutex> lk(host::model().mutex);
    hostSetting("m", "s", "t")->setBool(true);
    CHECK(value);
    CHECK(log == std::vector<std::string>{"widget", "section", "mod"});
    log.clear();
    hostSetting("m", "s", "n")->setNumber(7);
    CHECK(number == 7);
    CHECK(log == std::vector<std::string>{"section", "mod"}); // no widget hook on this one
    log.clear();
    hostSetting("m", "s", "x")->setString("hi");
    CHECK(text == "hi");
    CHECK(log == std::vector<std::string>{"section", "mod"});
    log.clear();
    hostSetting("m", "s", "b")->onClick();
    CHECK(log == std::vector<std::string>{"click", "section", "mod"});
    // Getters do not fire hooks.
    log.clear();
    CHECK(hostSetting("m", "s", "t")->getBool());
    CHECK(log.empty());
}

TEST_CASE("modifiers: label, description and showSteps reach the host, before and after forwarding") {
    resetAll();
    double          v   = 0;
    lever::Mod&     mod = lever::registerMod({"m", "M", {}, "", ""});
    lever::Section& sec = mod.section("s", "S");
    lever::Widget&  w   = sec.slider("n", "N", v, 0, 100, 1).description("before");
    REQUIRE(lever::attachNow());
    {
        std::lock_guard<std::mutex> lk(host::model().mutex);
        Setting* hs = hostSetting("m", "s", "n");
        REQUIRE(hs);
        CHECK(hs->description == "before");
        CHECK(hs->flags == 0);
    }
    w.description("after").label("Renamed").showSteps(true);
    {
        std::lock_guard<std::mutex> lk(host::model().mutex);
        Setting* hs = hostSetting("m", "s", "n");
        CHECK(hs->description == "after");
        CHECK(hs->label == "Renamed");
        CHECK(hs->flags == LEVER_SETTING_FLAG_SHOW_STEPS);
    }
    w.showSteps(false);
    {
        std::lock_guard<std::mutex> lk(host::model().mutex);
        CHECK(hostSetting("m", "s", "n")->flags == LEVER_SETTING_FLAG_HIDE_STEPS);
    }
    {
        std::lock_guard<std::mutex> lk(client::local().mutex);
        CHECK_FALSE(client::local().findSetting("m", "s", "n")->dirty);
    }
}

TEST_CASE("modifiers: updateSetting validates its input") {
    resetAll();
    const auto* r = static_cast<const LeverRegistryV1*>(host::bootstrap()->queryInterface(LEVER_IFACE_REGISTRY_V1, 3));
    REQUIRE(r);
    REQUIRE(LEVER_IFACE_HAS(LeverRegistryV1, r, updateSetting));
    LeverSettingDesc d{};
    d.size = sizeof(d), d.modId = "m", d.pageId = "p", d.id = "s";
    CHECK(r->updateSetting(nullptr) == LEVER_E_INVALID);
    CHECK(r->updateSetting(&d) == LEVER_E_NOT_FOUND);
    d.id = "";
    CHECK(r->updateSetting(&d) == LEVER_E_INVALID);
}

TEST_CASE("modifiers: a host without updateSetting drops the change and clears dirty") {
    resetAll();
    lever::Mod&     mod = lever::registerMod({"m", "M", {}, "", ""});
    lever::Section& sec = mod.section("s", "S");
    lever::Widget&  w   = sec.button("b", "B", [] {});
    // Forward through a version-2 table (no updateSetting) by hand.
    LeverRegistryV1 old       = *host::registryV1();
    old.size                  = LEVER_FIELD_END(LeverRegistryV1, notifyChanged);
    old.version               = 2;
    old.updateSetting         = nullptr;
    client::forwardAll(client::local(), &old);
    {
        std::lock_guard<std::mutex> lk(client::local().mutex);
        Setting* ls = client::local().findSetting("m", "s", "b");
        REQUIRE(ls);
        CHECK(ls->forwarded);
        ls->dirty = true;
    }
    REQUIRE(lever::attachNow()); // the real host (version 3) from here on
    w.label("New");              // delivered through updateSetting
    {
        std::lock_guard<std::mutex> lk(host::model().mutex);
        CHECK(hostSetting("m", "s", "b")->label == "New");
    }
    {
        std::lock_guard<std::mutex> lk(client::local().mutex);
        Setting* ls = client::local().findSetting("m", "s", "b");
        ls->label   = "Older";
        ls->dirty   = true;
    }
    client::forwardAll(client::local(), &old); // the old host: nothing to call, dirty is cleared anyway
    {
        std::lock_guard<std::mutex> lk(client::local().mutex);
        CHECK_FALSE(client::local().findSetting("m", "s", "b")->dirty);
    }
    std::lock_guard<std::mutex> lk(host::model().mutex);
    CHECK(hostSetting("m", "s", "b")->label == "New");
}

TEST_CASE("handles: invalid handles are inert, duplicates are refused, section() chains back") {
    resetAll();
    lever::Mod& mod = lever::registerMod({"m", "M", {}, "", ""});
    REQUIRE(mod.valid());
    lever::Mod& dup = lever::registerMod({"m", "M", {}, "", ""});
    CHECK_FALSE(dup.valid());
    CHECK_FALSE(lever::registerMod({"", "M", {}, "", ""}).valid());
    CHECK_FALSE(dup.section("x", "X").valid());
    CHECK_FALSE(lever::addSection("nomod", "x", "X").valid());

    lever::Section& sec = mod.section("s", "S");
    bool            a = false, b = false;
    lever::Widget&  wa = sec.toggle("a", "A", a);
    lever::Widget&  wb = wa.section().toggle("b", "B", b);
    CHECK(wa.valid());
    CHECK(wb.valid());
    CHECK(&wa.section() == &sec);
    CHECK_FALSE(sec.toggle("a", "A again", a).valid()); // duplicate id
    CHECK_FALSE(sec.toggle("c", "C", nullptr, nullptr).valid());
    CHECK(&mod.section("s", "ignored title") == &sec); // existing section comes back
    {
        std::lock_guard<std::mutex> lk(client::local().mutex);
        CHECK(client::local().findPage("m", "s")->title == "S");
        CHECK(client::local().findPage("m", "s")->settings.size() == 2);
    }
    lever::unregisterMod("m");
}

TEST_CASE("bindings: variable-bound rows read and write the variable with rounding") {
    resetAll();
    int         count = 3;
    float       ratio = 0.5f;
    enum class Mode { A, B, C } mode = Mode::B;
    unsigned char small = 7;
    std::string name = "x";
    lever::Mod&     mod = lever::registerMod({"m", "M", {}, "", ""});
    lever::Section& sec = mod.section("s", "S");
    sec.slider("count", "Count", count, 0, 10, 1);
    sec.slider("ratio", "Ratio", ratio, 0, 1, 0.25);
    sec.dropdown("mode", "Mode", mode, {"A", "B", "C"});
    sec.dropdown("small", "Small", small, {"0", "1", "2", "3", "4", "5", "6", "7", "8"});
    sec.textField("name", "Name", name, 16);
    REQUIRE(lever::attachNow());
    std::lock_guard<std::mutex> lk(host::model().mutex);
    CHECK(hostSetting("m", "s", "count")->getNumber() == 3);
    hostSetting("m", "s", "count")->setNumber(6.6);
    CHECK(count == 7);
    hostSetting("m", "s", "ratio")->setNumber(0.75);
    CHECK(ratio == 0.75f);
    CHECK(hostSetting("m", "s", "mode")->getIndex() == 1);
    hostSetting("m", "s", "mode")->setIndex(2);
    CHECK(mode == Mode::C);
    hostSetting("m", "s", "small")->setIndex(2);
    CHECK(small == 2);
    CHECK(hostSetting("m", "s", "name")->maxLength == 16);
    hostSetting("m", "s", "name")->setString("y");
    CHECK(name == "y");
}

TEST_CASE("slider grid: positions, index mapping, decimals and divider rule") {
    SliderGrid g = SliderGrid::of(0.0, 1.0, 0.05);
    CHECK(g.positions == 21);
    CHECK(g.decimals() == 2);
    CHECK(g.indexOf(0.35) == 7);
    CHECK(g.indexOf(-5) == 0);
    CHECK(g.indexOf(99) == 20);
    CHECK(g.valueAt(7) == 0.35);
    CHECK(g.valueAt(20) == 1.0);
    CHECK(g.valueAt(-1) == 0.0);
    CHECK(g.format(7) == "0.35");
    CHECK_FALSE(g.showSteps(0)); // 21 positions: no dividers by default
    CHECK(g.showSteps(LEVER_SETTING_FLAG_SHOW_STEPS));

    SliderGrid i = SliderGrid::of(2, 12, 2);
    CHECK(i.positions == 6);
    CHECK(i.decimals() == 0);
    CHECK(i.indexOf(7) == 3); // halfway between 6 and 8: rounds away from zero
    CHECK(i.valueAt(5) == 12);
    CHECK(i.format(1) == "4");
    CHECK(i.showSteps(0));
    CHECK_FALSE(i.showSteps(LEVER_SETTING_FLAG_HIDE_STEPS));

    SliderGrid f = SliderGrid::of(0.5, 2.0, 0.1);
    CHECK(f.positions == 16);
    CHECK(f.decimals() == 1);
    CHECK(f.valueAt(15) == 2.0);
    CHECK(f.format(3) == "0.8");

    SliderGrid degenerate = SliderGrid::of(5, 5, 1);
    CHECK(degenerate.positions == 1);
    CHECK(degenerate.valueAt(3) == 5);
    SliderGrid noStep = SliderGrid::of(0, 3, 0);
    CHECK(noStep.step == 1);
    CHECK(noStep.positions == 4);
    SliderGrid huge = SliderGrid::of(0, 1e9, 1);
    CHECK(huge.positions == SliderGrid::kMaxPositions);
}
