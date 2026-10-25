// Windows + LeviLamina implementation of internal/Platform.h.

#include "SlotWin32.h"

#include "internal/Client.h"
#include "internal/Election.h"
#include "internal/Host.h"
#include "internal/Log.h"
#include "internal/Platform.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"

#include <mutex>

namespace lever::detail::platform {

namespace {

ll::event::ListenerPtr gJoinListener;
std::mutex             gInitMutex;

void logToMod(LogLevel level, const std::string& message) {
    auto mod = ll::mod::NativeMod::current();
    if (!mod) return; // LeviLamina has no mod bound to this module yet
    if (level == LogLevel::Warn) mod->getLogger().warn("[Lever] {}", message);
    else mod->getLogger().info("[Lever] {}", message);
}

struct UnloadGuard {
    ~UnloadGuard() { election::withdraw(host::bootstrap()); }
};

} // namespace

void ensureInit() {
    std::lock_guard<std::mutex> lk(gInitMutex);
    static UnloadGuard guard; // withdraws this copy from the slot when its DLL unloads
    setLogSink(&logToMod);
    if (!election::hasSlot()) election::useSlot(win32::openProcessSlot());
    election::registerSelf(host::bootstrap());
    if (!gJoinListener) {
        // First use of the settings UI is not hookable yet (see Host.cpp), so elect when the player first joins a
        // level: by then every mod has loaded and enabled, i.e. every copy is in the slot.
        gJoinListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientJoinLevelEvent>(
            [](ll::event::ClientJoinLevelEvent&) { client::attachAndFlush(); }
        );
    }
}

void releaseListeners() {
    std::lock_guard<std::mutex> lk(gInitMutex);
    if (gJoinListener) {
        ll::event::EventBus::getInstance().removeListener(gJoinListener);
        gJoinListener.reset();
    }
}

} // namespace lever::detail::platform
