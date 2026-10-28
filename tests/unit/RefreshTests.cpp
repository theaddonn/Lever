// Late registration and value-changed notifications: what reaches the platform layer, through the ABI and through
// the C++ API.

#include <doctest.h>

#include "TestSupport.h"

#include "lever/Lever.h"

using namespace lever::detail;
using lever::test::notifications;
using lever::test::registryChanges;
using lever::test::resetAll;

namespace {

const LeverRegistryV1* registry() {
    return static_cast<const LeverRegistryV1*>(host::bootstrap()->queryInterface(LEVER_IFACE_REGISTRY_V1, 2));
}

} // namespace

TEST_CASE("refresh: every accepted page registration tells the platform the registry changed") {
    resetAll();
    const LeverRegistryV1* r = registry();
    REQUIRE(r);
    LeverModInfo mi{};
    mi.size = sizeof(mi), mi.id = "m";
    REQUIRE(r->registerMod(&mi) == LEVER_OK);
    CHECK(registryChanges() == 0); // a mod alone has no page factory

    LeverPageDesc pd{};
    pd.size = sizeof(pd), pd.modId = "m", pd.pageId = "p";
    CHECK(r->registerPage(&pd) == LEVER_OK);
    CHECK(registryChanges() == 1);
    CHECK(r->registerPage(&pd) == LEVER_E_DUPLICATE); // rejected: no change
    pd.modId = "nomod";
    CHECK(r->registerPage(&pd) == LEVER_E_NOT_FOUND);
    CHECK(registryChanges() == 1);
}

TEST_CASE("refresh: notifyChanged forwards the ids, maps NULL to wildcards and rejects an empty mod") {
    resetAll();
    const LeverRegistryV1* r = registry();
    REQUIRE(r);
    REQUIRE(LEVER_IFACE_HAS(LeverRegistryV1, r, notifyChanged));

    CHECK(r->notifyChanged("m", "p", "s") == LEVER_OK);
    CHECK(r->notifyChanged("m", nullptr, nullptr) == LEVER_OK);
    CHECK(r->notifyChanged("", "p", "s") == LEVER_E_INVALID);
    CHECK(r->notifyChanged(nullptr, "p", "s") == LEVER_E_INVALID);
    REQUIRE(notifications().size() == 2);
    CHECK(notifications()[0].modId == "m");
    CHECK(notifications()[0].pageId == "p");
    CHECK(notifications()[0].settingId == "s");
    CHECK(notifications()[1].modId == "m");
    CHECK(notifications()[1].pageId.empty());
    CHECK(notifications()[1].settingId.empty());
}

TEST_CASE("refresh: the C++ API reaches the host through the election, and is a no-op before it") {
    resetAll();
    lever::notifyChanged("m", "p"); // no host attached yet: dropped, no crash
    CHECK(notifications().empty());

    lever::Mod& mod = lever::registerMod({"m", "Mod", {1, 0, 0}, "", ""});
    REQUIRE(mod.valid());
    lever::Section& page = mod.section("p", "Page");
    REQUIRE(page.valid());
    lever::Widget& w = page.button("s", "S", [] {});
    REQUIRE(w.valid());
    REQUIRE(lever::attachNow());

    page.notifyChanged("s");
    lever::notifyChanged("m");
    page.notifyChanged();
    w.notifyChanged();
    mod.notifyChanged();
    lever::notifyChanged(""); // invalid: ignored
    REQUIRE(notifications().size() == 5);
    CHECK(notifications()[0].pageId == "p");
    CHECK(notifications()[0].settingId == "s");
    CHECK(notifications()[1].pageId.empty());
    CHECK(notifications()[2].pageId == "p");
    CHECK(notifications()[2].settingId.empty());
    CHECK(notifications()[3].settingId == "s");
    CHECK(notifications()[4].modId == "m");
    CHECK(notifications()[4].pageId.empty());

    lever::unregisterMod("m");
}

TEST_CASE("refresh: a host that predates notifyChanged is tolerated") {
    resetAll();
    // Serve a version-1 table (without the append) as if the elected host were an older library.
    LeverRegistryV1 old{};
    old.size = LEVER_FIELD_END(LeverRegistryV1, addSetting);
    old.version = 1;
    const LeverRegistryV1* oldTable = &old;
    CHECK_FALSE(LEVER_IFACE_HAS(LeverRegistryV1, oldTable, notifyChanged));
    // A table that is big enough but has the slot unset is "not available" as well.
    LeverRegistryV1 unset{};
    unset.size = sizeof(unset), unset.version = 3;
    const LeverRegistryV1* unsetTable = &unset;
    CHECK_FALSE(LEVER_IFACE_HAS(LeverRegistryV1, unsetTable, notifyChanged));
}
