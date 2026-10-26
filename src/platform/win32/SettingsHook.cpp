// Tier 1: the native settings registry hook.
//
// The Ore UI settings screen is data driven from Settings::Registry (docs/RE-settings-screen.md): the left nav
// comes from the "settings-tabs-groups" factory and its section factories, each page is a GroupInfoComponent, and
// a page's rows come from the factory registered under the page id. The registry is built lazily by
// ClientInstance::getSettingsRegistry -> Settings::RegistryBuilder::buildDefaultSettingsRegistry (exported), so
// hooking that one function and editing the fresh registry is enough: wrap the "settings-general-group" factory
// so it appends a "Lever Mods" page entry, and register the "lever-mods" factory that builds the page.
//
// Every embedded copy installs this hook; the election decides which copy injects (the host). The others only pass
// through, so a chain of N hooks still adds the page once.

#include "NativeComponents.h"
#include "internal/Client.h"
#include "internal/Election.h"
#include "internal/Host.h"
#include "internal/Log.h"
#include "internal/Platform.h"

#include "ll/api/memory/Hook.h"
#include "mc/client/settings/IBuilderContext.h"
#include "mc/client/settings/IRegistry.h"
#include "mc/client/settings/Registry.h"
#include "mc/client/settings/RegistryBuilder.h"

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace lever::detail::platform {

namespace {

constexpr const char* kTabsKey      = "settings-tabs-groups";   // lists the nav sections
constexpr const char* kGeneralKey   = "settings-general-group"; // fallback: the "General" section
constexpr const char* kSectionKey   = "settings-lever-group";   // our nav section ("Mods")
constexpr const char* kOverviewKey  = "lever-mods";             // the overview page (one row per mod)

std::weak_ptr<::Settings::IRegistry> gLastRegistry; // the most recent registry we injected into
std::mutex                           gRegistryMutex;

std::string versionString(const lever::Version& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

std::string pageKey(const Mod& mod, const Page& page) { return "lever." + mod.info.id + "." + page.id; }

// The rows of the overview page, built from the host model each time the game materialises the group.
std::vector<native::Element> buildOverview() {
    std::vector<native::Element> out;
    Registry&                    m = host::model();
    std::lock_guard<std::mutex>  lk(m.mutex);
    for (const Mod& mod : m.mods) {
        const ModInfo& i    = mod.info;
        std::string    name = i.displayName + " " + versionString(i.version);
        std::string    desc = i.description;
        if (!i.author.empty()) desc = desc.empty() ? "by " + i.author : desc + " (by " + i.author + ")";
        if (auto e = native::makeText("lever.mods." + i.id, name, desc.empty() ? std::nullopt : std::optional(desc)))
            out.push_back(std::move(e));
    }
    if (out.empty()) {
        if (auto e = native::makeText("lever.mods.none", "No mods registered with Lever", std::nullopt))
            out.push_back(std::move(e));
    }
    logInfo("settings: factory lever-mods invoked (" + std::to_string(out.size()) + " components)");
    native::flushTranslations();
    return out;
}

// The rows of one mod page: its settings in registration order.
std::vector<native::Element> buildPage(std::string modId, std::string pageId) {
    std::vector<native::Element> out;
    Registry&                    m = host::model();
    std::lock_guard<std::mutex>  lk(m.mutex);
    Page*                        page = m.findPage(modId, pageId);
    if (!page) {
        if (auto e = native::makeText("lever." + modId + "." + pageId + ".gone", "This mod is no longer loaded", std::nullopt))
            out.push_back(std::move(e));
        return out;
    }
    int skipped = 0;
    for (const Setting& s : page->settings) {
        const std::string id = "lever." + modId + "." + pageId + "." + s.id;
        if (auto e = native::makeSetting(id, s, native::SettingKey{modId, pageId, s.id})) out.push_back(std::move(e));
        else ++skipped;
    }
    logInfo(
        "settings: factory " + modId + "." + pageId + " invoked (" + std::to_string(out.size()) + " components"
        + (skipped ? ", " + std::to_string(skipped) + " unsupported" : "") + ")"
    );
    native::flushTranslations();
    return out;
}

// The entries of our nav section: the overview page, then every registered page.
std::vector<native::Element> buildSection() {
    std::vector<native::Element> out;
    if (auto e = native::makeGroupInfo(kOverviewKey, "Lever Mods", std::string("Installed mods")))
        out.push_back(std::move(e));
    Registry&                   m = host::model();
    std::lock_guard<std::mutex> lk(m.mutex);
    for (const Mod& mod : m.mods)
        for (const Page& page : mod.pages) {
            if (!(page.surfaces & LEVER_SURFACE_GLOBAL_SETTINGS)) continue;
            const std::string title = mod.pages.size() == 1 ? mod.info.displayName : mod.info.displayName + ": " + page.title;
            if (auto e = native::makeGroupInfo(pageKey(mod, page), title, std::nullopt)) out.push_back(std::move(e));
        }
    native::flushTranslations();
    return out;
}

// Appends GroupInfo(key, name) to whatever factory `wrappedKey` produced.
bool appendEntry(::Settings::Registry& reg, const char* wrappedKey, std::string key, std::string name, std::optional<std::string> desc) {
    auto& factories = *reg.mSettingsFactories;
    auto  it        = factories.find(wrappedKey);
    if (it == factories.end()) return false;
    native::Factory orig = std::move(it->second);
    it->second           = [orig = std::move(orig), key, name, desc]() {
        std::vector<native::Element> items = orig ? orig() : std::vector<native::Element>{};
        if (auto e = native::makeGroupInfo(key, name, desc)) items.push_back(std::move(e));
        return items;
    };
    logInfo(std::string("settings: wrapped ") + wrappedKey + (orig ? " (orig factory ok)" : " (orig factory EMPTY)"));
    return true;
}

// Registers the factory of every model page that the registry does not know yet. Returns how many were added.
int registerPageFactories(::Settings::Registry& reg) {
    auto&                       factories = *reg.mSettingsFactories;
    int                         added     = 0;
    Registry&                   m         = host::model();
    std::lock_guard<std::mutex> lk(m.mutex);
    for (const Mod& mod : m.mods)
        for (const Page& page : mod.pages) {
            const std::string key = pageKey(mod, page);
            if (factories.find(key) != factories.end()) continue;
            std::string modId = mod.info.id, pageId = page.id;
            reg.registerSettingsFactory(key, native::Factory([modId, pageId] { return buildPage(modId, pageId); }));
            ++added;
        }
    return added;
}

void injectInto(::Settings::Registry& reg) {
    // 1. A "Mods" section in the nav, or (fallback) an entry at the end of General.
    if (appendEntry(reg, kTabsKey, kSectionKey, "Mods", std::nullopt)) {
        reg.registerSettingsFactory(kSectionKey, native::Factory(&buildSection));
    } else {
        logWarn(std::string("settings: factory '") + kTabsKey + "' not found; falling back to the General section");
        if (!appendEntry(reg, kGeneralKey, kOverviewKey, "Lever Mods", std::string("Settings of installed mods")))
            logWarn(std::string("settings: factory '") + kGeneralKey + "' not found; the Mods page has no nav entry");
    }
    // 2. The overview page and one factory per registered page.
    reg.registerSettingsFactory(kOverviewKey, native::Factory(&buildOverview));
    const int pages = registerPageFactories(reg);
    logInfo(
        std::string("settings: registered group ") + kOverviewKey + " and " + std::to_string(pages) + " mod page(s); toggles "
        + (native::togglesSupported() ? "native" : "as dropdowns")
    );
}

void onRegistryBuilt(
    const std::shared_ptr<::Settings::IRegistry>&                       reg,
    const std::shared_ptr<::Settings::RegistryBuilder::IBuilderContext>& context
) {
    logInfo("settings: buildDefaultSettingsRegistry hit");
    if (!reg) return;
    if (context) native::setOptionRegistry(&context->getOptions());
    // The settings screen can open from the main menu before any level join: make sure the election ran. Every
    // mod has loaded and enabled by the time a screen exists, so all copies are in the slot.
    client::attachAndFlush();
    if (election::host() != host::bootstrap()) return; // another copy is the host: its hook injects
    {
        std::lock_guard<std::mutex> lk(gRegistryMutex);
        gLastRegistry = reg;
    }
    injectInto(static_cast<::Settings::Registry&>(*reg));
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable" // _AutoHookCount from the LeviLamina macro
#endif
LL_STATIC_HOOK(
    BuildRegistryHook,
    ll::memory::HookPriority::Normal,
    &::Settings::RegistryBuilder::buildDefaultSettingsRegistry,
    ::std::shared_ptr<::Settings::IRegistry>,
    ::std::shared_ptr<::Settings::RegistryBuilder::IBuilderContext> context
) {
    auto reg = origin(context);
    try {
        onRegistryBuilt(reg, context);
    } catch (...) {
        logWarn("settings: exception while injecting into the settings registry; page skipped");
    }
    return reg;
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

bool gHooked = false;

} // namespace

void installSettingsHook() {
    if (gHooked) return;
    try {
        BuildRegistryHook::hook();
        gHooked = true;
        logInfo("settings: hook on Settings::RegistryBuilder::buildDefaultSettingsRegistry installed");
    } catch (...) {
        logWarn("settings: could not hook buildDefaultSettingsRegistry (symbol unresolved?); no Mods page");
    }
}

// Pages registered after the game built its registry: the nav section enumerates the model on every open, so only
// the page factory is missing. Settings added to an existing page need nothing: the page factory rebuilds the rows
// each time the page opens.
void registryChanged() {
    std::shared_ptr<::Settings::IRegistry> reg;
    {
        std::lock_guard<std::mutex> lk(gRegistryMutex);
        reg = gLastRegistry.lock();
    }
    if (!reg) return; // not built yet: injectInto will pick the page up
    try {
        if (int n = registerPageFactories(static_cast<::Settings::Registry&>(*reg)); n > 0)
            logInfo("settings: registered " + std::to_string(n) + " late page(s)");
    } catch (...) {
        logWarn("settings: exception while registering a late page");
    }
}

void notifySettingChanged(const char* modId, const char* pageId, const char* settingId) {
    native::notifyChanged(modId ? modId : "", pageId ? pageId : "", settingId ? settingId : "");
}

void uninstallSettingsHook() {
    if (!gHooked) return;
    try {
        BuildRegistryHook::unhook();
    } catch (...) {}
    gHooked = false;
}

} // namespace lever::detail::platform
