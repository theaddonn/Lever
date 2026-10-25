// Election logic over an in-memory slot, plus the client path (buffering, forwarding, missing interface).

#include <doctest.h>

#include "TestSupport.h"

#include "lever/Lever.h"

#include <atomic>

using namespace lever;
using lever::detail::LogLevel;
namespace client = lever::detail::client;
namespace election = lever::detail::election;
namespace host = lever::detail::host;
using lever::detail::Mod;
using lever::detail::Setting;
using lever::test::MemorySlot;
using lever::test::resetAll;

namespace {

std::atomic<int> gActivations{0};
void             fakeActivate() { ++gActivations; }
const void*      noInterfaces(const char*, std::uint32_t) { return nullptr; }
std::uint32_t    fakeCopies() { return 0; }

LeverBootstrap fakeCopy(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) {
    LeverBootstrap b{};
    b.size             = sizeof(b);
    b.abiGeneration    = LEVER_ABI_GENERATION;
    b.libMajor         = major, b.libMinor = minor, b.libPatch = patch;
    b.queryInterface   = &noInterfaces;
    b.activateHost     = &fakeActivate;
    b.registeredCopies = &fakeCopies;
    return b;
}

} // namespace

TEST_CASE("election: the highest version wins, earliest registration wins ties, activation happens once") {
    resetAll();
    REQUIRE(election::useSlot(std::make_unique<MemorySlot>()));

    LeverBootstrap old1 = fakeCopy(0, 1, 0), newest = fakeCopy(0, 3, 1), tie = fakeCopy(0, 3, 1), mid = fakeCopy(0, 2, 9);
    election::registerSelf(&old1);
    election::registerSelf(&newest);
    election::registerSelf(&tie);
    election::registerSelf(&mid);
    election::registerSelf(&mid); // idempotent
    CHECK(election::registeredCopies() == 4);
    CHECK(election::host() == nullptr);

    gActivations = 0;
    CHECK(election::attachOrHost() == &newest);
    CHECK(election::attachOrHost() == &newest);
    CHECK(election::host() == &newest);
    CHECK(gActivations == 1);
    election::withdraw(&old1);
}

TEST_CASE("election: components are compared numerically, not as strings") {
    resetAll();
    REQUIRE(election::useSlot(std::make_unique<MemorySlot>()));
    LeverBootstrap a = fakeCopy(0, 9, 0), b = fakeCopy(0, 10, 0);
    election::registerSelf(&a);
    election::registerSelf(&b);
    CHECK(election::attachOrHost() == &b);
    election::withdraw(&a);
}

TEST_CASE("election: a slot with foreign data is refused") {
    resetAll();
    auto foreign = std::make_unique<MemorySlot>();
    foreign->data().magic  = 0xDEADBEEF;
    foreign->data().layout = 1;
    CHECK_FALSE(election::useSlot(std::move(foreign)));
    CHECK_FALSE(election::hasSlot());
    CHECK(election::attachOrHost() == nullptr);
}

TEST_CASE("election: invalid bootstraps are not registered") {
    resetAll();
    REQUIRE(election::useSlot(std::make_unique<MemorySlot>()));
    LeverBootstrap wrongGen = fakeCopy(9, 9, 9);
    wrongGen.abiGeneration  = 2;
    LeverBootstrap noActivate = fakeCopy(9, 9, 9);
    noActivate.activateHost   = nullptr;
    election::registerSelf(&wrongGen);
    election::registerSelf(&noActivate);
    election::registerSelf(nullptr);
    CHECK(election::registeredCopies() == 0);
}

TEST_CASE("client: registrations are buffered until the host is attached, then forwarded in order") {
    resetAll();
    CHECK(registerMod({"mymod", "My Mod", {1, 2, 3}, "me", "does things"}));
    addPage("mymod", "main", "Main", "creator")
        .showIn(Surface::PauseMenu)
        .addToggle("on", "On", "", [] { return true; }, [](bool) {});
    {
        std::lock_guard<std::mutex> lk(host::model().mutex);
        CHECK(host::model().mods.empty()); // no election yet: buffered locally
    }
    CHECK_FALSE(hostInfo().attached);

    REQUIRE(attachNow());
    const HostInfo hi = hostInfo();
    CHECK(hi.attached);
    CHECK(hi.isThisCopy);
    CHECK(hi.version.minor == 1);
    CHECK(hi.features == (LEVER_FEATURE_GLOBAL_SETTINGS | LEVER_FEATURE_INFO_PAGES));

    std::lock_guard<std::mutex> lk(host::model().mutex);
    Mod* m = host::model().findMod("mymod");
    REQUIRE(m);
    CHECK(m->info.displayName == "My Mod");
    CHECK(m->info.version.patch == 3);
    detail::Page* p = host::model().findPage("mymod", "main");
    REQUIRE(p);
    CHECK(p->icon == "creator");
    CHECK(p->surfaces == (LEVER_SURFACE_GLOBAL_SETTINGS | LEVER_SURFACE_PAUSE_MENU));
    Setting* s = host::model().findSetting(*p, "on");
    REQUIRE(s);
    CHECK(s->getBool());
}

TEST_CASE("client: a later change after attach is forwarded, unregister reaches the host") {
    resetAll();
    registerMod({"m", "M", {}, "", ""});
    Page& page = addPage("m", "p", "P");
    REQUIRE(attachNow());
    page.showOnlyIn(Surface::WorldCreation);
    page.addButton("b", "B", "", [] {});
    {
        std::lock_guard<std::mutex> lk(host::model().mutex);
        CHECK(host::model().findPage("m", "p")->surfaces == LEVER_SURFACE_WORLD_CREATION);
        CHECK(host::model().findSetting(*host::model().findPage("m", "p"), "b") != nullptr);
    }
    unregisterMod("m");
    std::lock_guard<std::mutex> lk(host::model().mutex);
    CHECK(host::model().mods.empty());
}

TEST_CASE("client: a host without lever.registry.v1 is logged once and registrations stay buffered") {
    resetAll();
    LeverBootstrap bare = fakeCopy(9, 0, 0); // a bootstrap whose queryInterface knows nothing

    CHECK(client::registryOf(&bare) == nullptr);
    CHECK(client::registryOf(&bare) == nullptr);
    CHECK(client::registryOf(nullptr) == nullptr);
    int warnings = 0;
    for (const auto& l : test::logLines()) warnings += l.level == LogLevel::Warn ? 1 : 0;
    CHECK(warnings == 1);

    // Forwarding to a missing interface is a harmless no-op and keeps the entries unforwarded.
    registerMod({"keep", "Keep", {}, "", ""});
    client::forwardAll(client::local(), nullptr);
    std::lock_guard<std::mutex> lk(client::local().mutex);
    REQUIRE(client::local().findMod("keep"));
    CHECK_FALSE(client::local().findMod("keep")->forwarded);
}

TEST_CASE("client: a registry table missing members is treated as absent") {
    resetAll();
    static LeverRegistryV1 partial{}; // only the first two members: size ends before registerPage
    partial.size           = LEVER_FIELD_END(LeverRegistryV1, unregisterMod);
    partial.version        = 1;
    partial.registerMod    = host::registryV1()->registerMod;
    partial.unregisterMod  = host::registryV1()->unregisterMod;
    partial.registerPage   = host::registryV1()->registerPage; // present in memory but beyond `size`
    static const void* table = &partial;
    LeverBootstrap b = fakeCopy(9, 0, 0);
    b.queryInterface = [](const char*, std::uint32_t) -> const void* { return table; };
    CHECK(client::registryOf(&b) == nullptr);
}

TEST_CASE("client: the host interface reports activation") {
    resetAll();
    const auto* h = static_cast<const LeverHostV1*>(host::bootstrap()->queryInterface(LEVER_IFACE_HOST_V1, 1));
    REQUIRE(h);
    CHECK(h->size == sizeof(LeverHostV1));
    CHECK(h->getFeatures() == (LEVER_FEATURE_GLOBAL_SETTINGS | LEVER_FEATURE_INFO_PAGES));
}
