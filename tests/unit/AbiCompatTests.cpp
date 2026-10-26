// Compatibility of the host with older clients, and the queryInterface contract.

#include <doctest.h>

#include "TestSupport.h"

#include "lever/Lever.h"

#include <cstring>
#include <string>


using namespace lever::detail;
using lever::detail::Page;
using lever::detail::Mod;
using lever::detail::Setting;
using lever::test::resetAll;
using lever::test::Truncated;

namespace {

const LeverRegistryV1* registry() {
    return static_cast<const LeverRegistryV1*>(host::bootstrap()->queryInterface(LEVER_IFACE_REGISTRY_V1, 1));
}

LeverModInfo fullMod(const char* id = "m") {
    LeverModInfo mi{};
    mi.size         = sizeof(mi);
    mi.id           = id;
    mi.displayName  = "Name";
    mi.author       = "Me";
    mi.description  = "Desc";
    mi.versionMajor = 1, mi.versionMinor = 2, mi.versionPatch = 3;
    return mi;
}

LeverPageDesc fullPage(const char* modId = "m", const char* pageId = "p") {
    LeverPageDesc pd{};
    pd.size     = sizeof(pd);
    pd.modId    = modId;
    pd.pageId   = pageId;
    pd.title    = "Title";
    pd.icon     = "video";
    pd.surfaces = LEVER_SURFACE_PAUSE_MENU;
    return pd;
}

LeverSettingDesc fullSetting(const char* id = "s") {
    LeverSettingDesc d{};
    d.size        = sizeof(d);
    d.kind        = LEVER_KIND_TEXT_FIELD;
    d.modId       = "m";
    d.pageId      = "p";
    d.id          = id;
    d.label       = "Label";
    d.description = "Description";
    d.maxLength   = 64;
    return d;
}

int releases = 0;
void countRelease(void*) { ++releases; }

} // namespace

TEST_CASE("compat: old client with shorter descriptors is accepted and defaults are applied") {
    resetAll();
    const LeverRegistryV1* r = registry();
    REQUIRE(r);

    // Mod info that only knows `id`: everything else defaults.
    LeverModInfo fm = fullMod("old");
    {
        Truncated<LeverModInfo> t(fm, LEVER_MODINFO_V1_MIN_SIZE);
        CHECK(r->registerMod(t.get()) == LEVER_OK);
    }
    // A slightly longer one (knows author, not the version).
    fm = fullMod("old2");
    {
        Truncated<LeverModInfo> t(fm, LEVER_FIELD_END(LeverModInfo, description));
        CHECK(r->registerMod(t.get()) == LEVER_OK);
    }
    {
        std::lock_guard<std::mutex> lk(host::model().mutex);
        Mod* m = host::model().findMod("old");
        REQUIRE(m);
        CHECK(m->info.displayName == "old"); // default: the id
        CHECK(m->info.author.empty());
        CHECK(m->info.description.empty());
        CHECK(m->info.version.major == 0);
        Mod* m2 = host::model().findMod("old2");
        REQUIRE(m2);
        CHECK(m2->info.displayName == "Name");
        CHECK(m2->info.description == "Desc");
        CHECK(m2->info.version.major == 0); // beyond the supplied size: zero, not the 1 in the source
    }

    // Page that only knows modId/pageId: title defaults to the page id, surfaces to the global settings tab.
    LeverPageDesc fp = fullPage("old", "pg");
    {
        Truncated<LeverPageDesc> t(fp, LEVER_PAGEDESC_V1_MIN_SIZE);
        CHECK(r->registerPage(t.get()) == LEVER_OK);
    }
    // A page that knows everything up to the icon but not `surfaces`.
    fp = fullPage("old", "pg2");
    {
        Truncated<LeverPageDesc> t(fp, LEVER_FIELD_END(LeverPageDesc, icon));
        CHECK(r->registerPage(t.get()) == LEVER_OK);
    }
    // And a page that does carry surfaces == 0 explicitly: it is honoured (not replaced by the default).
    fp          = fullPage("old", "pg3");
    fp.surfaces = 0;
    CHECK(r->registerPage(&fp) == LEVER_OK);
    {
        std::lock_guard<std::mutex> lk(host::model().mutex);
        Page* p = host::model().findPage("old", "pg");
        REQUIRE(p);
        CHECK(p->title == "pg");
        CHECK(p->icon.empty());
        CHECK(p->surfaces == LEVER_SURFACE_GLOBAL_SETTINGS);
        Page* p2 = host::model().findPage("old", "pg2");
        REQUIRE(p2);
        CHECK(p2->title == "Title");
        CHECK(p2->icon == "video");
        CHECK(p2->surfaces == LEVER_SURFACE_GLOBAL_SETTINGS);
        CHECK(host::model().findPage("old", "pg3")->surfaces == 0u);
    }

    // Setting with only the required prefix: label/description empty, maxLength default, no callbacks.
    LeverSettingDesc fs = fullSetting("a");
    fs.modId            = "old";
    fs.pageId           = "pg";
    {
        Truncated<LeverSettingDesc> t(fs, LEVER_SETTINGDESC_V1_MIN_SIZE);
        CHECK(r->addSetting(t.get()) == LEVER_OK);
    }
    // Setting that knows label but nothing after description.
    fs.id = "b";
    {
        Truncated<LeverSettingDesc> t(fs, LEVER_FIELD_END(LeverSettingDesc, description));
        CHECK(r->addSetting(t.get()) == LEVER_OK);
    }
    {
        std::lock_guard<std::mutex> lk(host::model().mutex);
        Page* p = host::model().findPage("old", "pg");
        REQUIRE(p);
        Setting* a = host::model().findSetting(*p, "a");
        REQUIRE(a);
        CHECK(a->kind == LEVER_KIND_TEXT_FIELD);
        CHECK(a->label.empty());
        CHECK(a->description.empty());
        CHECK(a->maxLength == 256);
        CHECK(a->options.empty());
        CHECK(!a->getString);
        Setting* b = host::model().findSetting(*p, "b");
        REQUIRE(b);
        CHECK(b->label == "Label");
        CHECK(b->description == "Description");
        CHECK(b->maxLength == 256); // the source said 64 but lies beyond the supplied size
    }
}

TEST_CASE("compat: a short callback struct leaves the missing callbacks empty") {
    resetAll();
    const LeverRegistryV1* r = registry();
    REQUIRE(r);
    LeverModInfo mi = fullMod();
    REQUIRE(r->registerMod(&mi) == LEVER_OK);
    LeverPageDesc pd = fullPage();
    REQUIRE(r->registerPage(&pd) == LEVER_OK);

    struct State {
        int  clicks = 0;
        bool value  = false;
    } state;
    LeverSettingCallbacks cb{};
    cb.size    = sizeof(cb);
    cb.user    = &state;
    cb.onClick = [](void* u) { ++static_cast<State*>(u)->clicks; };
    cb.getBool = [](void* u) { return static_cast<State*>(u)->value ? 1 : 0; };
    cb.release = &countRelease;

    // The "old" copy only knew the callbacks up to getBool.
    Truncated<LeverSettingCallbacks> shortCb(cb, LEVER_FIELD_END(LeverSettingCallbacks, getBool));
    LeverSettingDesc d = fullSetting("btn");
    d.kind             = LEVER_KIND_BUTTON;
    d.callbacks        = shortCb.get();
    releases           = 0;
    REQUIRE(r->addSetting(&d) == LEVER_OK);
    {
        std::lock_guard<std::mutex> lk(host::model().mutex);
        Setting* s = host::model().findSetting(*host::model().findPage("m", "p"), "btn");
        REQUIRE(s);
        CHECK(!s->onClick);  // beyond the declared size
        CHECK(!s->setBool);
        REQUIRE(s->getBool);
        state.value = true;
        CHECK(s->getBool());
    }
    CHECK(releases == 0); // release also lay beyond the declared size: it is never called
}

TEST_CASE("compat: queryInterface returns null for unknown names and too-high versions") {
    resetAll();
    const LeverBootstrap* b = host::bootstrap();
    REQUIRE(LEVER_BOOTSTRAP_VALID(b));

    const void* reg = b->queryInterface(LEVER_IFACE_REGISTRY_V1, 1);
    REQUIRE(reg);
    CHECK(static_cast<const LeverRegistryV1*>(reg)->version == 2);
    CHECK(b->queryInterface(LEVER_IFACE_REGISTRY_V1, 0) == reg);
    CHECK(b->queryInterface(LEVER_IFACE_REGISTRY_V1, 2) == reg);       // the version-2 append is served
    CHECK(b->queryInterface(LEVER_IFACE_HOST_V1, 1) != nullptr);

    CHECK(b->queryInterface(LEVER_IFACE_REGISTRY_V1, 3) == nullptr);   // newer revision than served
    CHECK(b->queryInterface(LEVER_IFACE_REGISTRY_V1, 0xFFFFFFFFu) == nullptr);
    CHECK(b->queryInterface("lever.registry.v2", 1) == nullptr);       // no such generation (yet)
    CHECK(b->queryInterface("lever.registry", 1) == nullptr);          // exact match only
    CHECK(b->queryInterface("lever.registry.v1 ", 1) == nullptr);
    CHECK(b->queryInterface("", 1) == nullptr);
    CHECK(b->queryInterface(nullptr, 1) == nullptr);
    CHECK(b->queryInterface("nonsense", 1) == nullptr);
}

TEST_CASE("compat: descriptors below the v1 minimum size are rejected with an error code") {
    resetAll();
    const LeverRegistryV1* r = registry();
    REQUIRE(r);

    LeverModInfo fm = fullMod();
    {
        Truncated<LeverModInfo> t(fm, LEVER_MODINFO_V1_MIN_SIZE - 1);
        CHECK(r->registerMod(t.get()) == LEVER_E_SIZE);
    }
    {
        Truncated<LeverModInfo> t(fm, sizeof(uint32_t)); // just the size field
        CHECK(r->registerMod(t.get()) == LEVER_E_SIZE);
    }
    fm.size = 0;
    CHECK(r->registerMod(&fm) == LEVER_E_SIZE);
    CHECK(r->registerMod(nullptr) == LEVER_E_INVALID);

    LeverModInfo ok = fullMod();
    REQUIRE(r->registerMod(&ok) == LEVER_OK);

    LeverPageDesc fp = fullPage();
    {
        Truncated<LeverPageDesc> t(fp, LEVER_PAGEDESC_V1_MIN_SIZE - 1);
        CHECK(r->registerPage(t.get()) == LEVER_E_SIZE);
    }
    CHECK(r->registerPage(nullptr) == LEVER_E_INVALID);

    LeverSettingDesc fs = fullSetting();
    {
        Truncated<LeverSettingDesc> t(fs, LEVER_SETTINGDESC_V1_MIN_SIZE - 1);
        CHECK(r->addSetting(t.get()) == LEVER_E_SIZE);
    }
    CHECK(r->addSetting(nullptr) == LEVER_E_INVALID);

    // A callback struct below its minimum rejects the whole setting and nothing is released.
    LeverPageDesc pd = fullPage();
    REQUIRE(r->registerPage(&pd) == LEVER_OK);
    LeverSettingCallbacks cb{};
    cb.size    = sizeof(cb);
    cb.release = &countRelease;
    Truncated<LeverSettingCallbacks> tiny(cb, LEVER_SETTINGCALLBACKS_V1_MIN_SIZE - 1);
    fs           = fullSetting("withcb");
    fs.callbacks = tiny.get();
    releases     = 0;
    CHECK(r->addSetting(&fs) == LEVER_E_SIZE);
    CHECK(releases == 0);

    // Nothing from the rejected calls leaked into the model.
    std::lock_guard<std::mutex> lk(host::model().mutex);
    CHECK(host::model().mods.size() == 1);
    CHECK(host::model().findPage("m", "p")->settings.empty());
}

TEST_CASE("compat: strings and option arrays are copied at registration") {
    resetAll();
    const LeverRegistryV1* r = registry();
    REQUIRE(r);

    std::string id = "copy", name = "Display", author = "Author", desc = "Description";
    LeverModInfo mi = fullMod();
    mi.id = id.c_str(), mi.displayName = name.c_str(), mi.author = author.c_str(), mi.description = desc.c_str();
    REQUIRE(r->registerMod(&mi) == LEVER_OK);

    std::string pid = "page", title = "Page Title", icon = "creator";
    LeverPageDesc pd = fullPage();
    pd.modId = id.c_str(), pd.pageId = pid.c_str(), pd.title = title.c_str(), pd.icon = icon.c_str();
    REQUIRE(r->registerPage(&pd) == LEVER_OK);

    std::string sid = "set", label = "Label text", sdesc = "Setting desc";
    std::string optA = "Alpha", optB = "Beta";
    const char* options[] = {optA.c_str(), optB.c_str()};
    LeverSettingDesc sd{};
    sd.size = sizeof(sd), sd.kind = LEVER_KIND_DROPDOWN;
    sd.modId = id.c_str(), sd.pageId = pid.c_str(), sd.id = sid.c_str();
    sd.label = label.c_str(), sd.description = sdesc.c_str();
    sd.options = options, sd.optionCount = 2;
    REQUIRE(r->addSetting(&sd) == LEVER_OK);

    // Scribble over and free every source buffer.
    for (std::string* s : {&id, &name, &author, &desc, &pid, &title, &icon, &sid, &label, &sdesc, &optA, &optB}) {
        std::fill(s->begin(), s->end(), 'X');
        s->shrink_to_fit();
        *s = std::string();
    }
    options[0] = options[1] = nullptr;

    std::lock_guard<std::mutex> lk(host::model().mutex);
    Mod* m = host::model().findMod("copy");
    REQUIRE(m);
    CHECK(m->info.displayName == "Display");
    CHECK(m->info.author == "Author");
    CHECK(m->info.description == "Description");
    Page* p = host::model().findPage("copy", "page");
    REQUIRE(p);
    CHECK(p->title == "Page Title");
    CHECK(p->icon == "creator");
    Setting* s = host::model().findSetting(*p, "set");
    REQUIRE(s);
    CHECK(s->label == "Label text");
    CHECK(s->description == "Setting desc");
    REQUIRE(s->options.size() == 2);
    CHECK(s->options[0] == "Alpha");
    CHECK(s->options[1] == "Beta");
}

TEST_CASE("compat: unknown widget kinds are unsupported, release runs once and only after acceptance") {
    resetAll();
    const LeverRegistryV1* r = registry();
    REQUIRE(r);
    LeverModInfo mi = fullMod();
    REQUIRE(r->registerMod(&mi) == LEVER_OK);
    LeverPageDesc pd = fullPage();
    REQUIRE(r->registerPage(&pd) == LEVER_OK);

    LeverSettingCallbacks cb{};
    cb.size    = sizeof(cb);
    cb.release = &countRelease;

    releases = 0;
    LeverSettingDesc d = fullSetting("future");
    d.kind             = 99; // a kind from a newer library
    d.callbacks        = &cb;
    CHECK(r->addSetting(&d) == LEVER_E_UNSUPPORTED);
    CHECK(releases == 0);

    d      = fullSetting("one");
    d.callbacks = &cb;
    CHECK(r->addSetting(&d) == LEVER_OK);
    CHECK(r->addSetting(&d) == LEVER_E_DUPLICATE); // rejected: ownership stays with the caller
    CHECK(releases == 0);

    CHECK(r->unregisterMod("m") == LEVER_OK);
    CHECK(releases == 1);
    CHECK(r->unregisterMod("m") == LEVER_E_NOT_FOUND);
    CHECK(releases == 1);
}

TEST_CASE("compat: duplicate and unknown parents report distinct codes") {
    resetAll();
    const LeverRegistryV1* r = registry();
    REQUIRE(r);
    LeverModInfo mi = fullMod();
    CHECK(r->registerMod(&mi) == LEVER_OK);
    CHECK(r->registerMod(&mi) == LEVER_E_DUPLICATE);
    LeverPageDesc orphan = fullPage("nomod", "p");
    CHECK(r->registerPage(&orphan) == LEVER_E_NOT_FOUND);
    LeverSettingDesc s = fullSetting();
    CHECK(r->addSetting(&s) == LEVER_E_NOT_FOUND); // page "p" does not exist yet
    CHECK(r->setPageSurfaces("m", "nope", 1) == LEVER_E_NOT_FOUND);
}
