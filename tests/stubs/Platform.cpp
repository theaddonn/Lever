// Test double for internal/Platform.h: an in-memory election slot, no events, no logger, no game hooks.

#include "TestSupport.h"
#include "internal/Platform.h"

namespace lever::detail::platform {

void ensureInit() {
    if (!election::hasSlot()) election::useSlot(std::make_unique<lever::test::MemorySlot>());
    election::registerSelf(host::bootstrap());
}

void releaseListeners() {}

void installSettingsHook() {}

void uninstallSettingsHook() {}

void registryChanged() { ++lever::test::registryChanges(); }

void notifySettingChanged(const char* modId, const char* pageId, const char* settingId) {
    lever::test::notifications().push_back({modId ? modId : "", pageId ? pageId : "", settingId ? settingId : ""});
}

} // namespace lever::detail::platform
