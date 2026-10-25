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

constexpr const char* kSectionKey = "settings-general-group"; // the "General" nav section (always present)
constexpr const char* kPageKey    = "lever-mods";             // our page id = our factory key

std::weak_ptr<::Settings::IRegistry> gLastRegistry; // the most recent registry we injected into
std::mutex                           gRegistryMutex;

std::string versionString(const lever::Version& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

// The rows of the "Lever Mods" page, built from the host model each time the game materialises the group.
std::vector<native::Element> buildModsPage() {
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
    return out;
}

void injectInto(::Settings::Registry& reg) {
    // 1. The page entry: append a GroupInfoComponent to the General section's factory result.
    auto& factories = *reg.mSettingsFactories;
    auto  it        = factories.find(kSectionKey);
    if (it == factories.end()) {
        logWarn(std::string("settings: factory '") + kSectionKey + "' not found; the Mods page has no nav entry");
    } else {
        native::Factory orig = std::move(it->second);
        it->second           = [orig = std::move(orig)]() {
            std::vector<native::Element> items = orig ? orig() : std::vector<native::Element>{};
            if (auto e = native::makeGroupInfo(kPageKey, "Lever Mods", std::string("Settings of installed mods")))
                items.push_back(std::move(e));
            return items;
        };
        logInfo(std::string("settings: wrapped ") + kSectionKey + (orig ? " (orig factory ok)" : " (orig factory EMPTY)"));
    }
    // 2. The page itself.
    reg.registerSettingsFactory(kPageKey, native::Factory(&buildModsPage));
    logInfo(std::string("settings: registered group ") + kPageKey);
}

void onRegistryBuilt(const std::shared_ptr<::Settings::IRegistry>& reg) {
    logInfo("settings: buildDefaultSettingsRegistry hit");
    if (!reg) return;
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
    auto reg = origin(std::move(context));
    try {
        onRegistryBuilt(reg);
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

void uninstallSettingsHook() {
    if (!gHooked) return;
    try {
        BuildRegistryHook::unhook();
    } catch (...) {}
    gHooked = false;
}

} // namespace lever::detail::platform
