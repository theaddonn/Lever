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

} // namespace lever::detail::platform
