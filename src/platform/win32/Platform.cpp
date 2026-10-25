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

#include <windows.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace lever::detail::platform {

namespace {

ll::event::ListenerPtr gJoinListener;
std::mutex             gInitMutex;

std::mutex gFileLogMutex;

// LeviLamina's console sink is not always reachable on the client (e.g. under Proton), so every line also goes to
// <modDir>/logs/lever.log of the mod that owns this copy. Appended; the first line of a process carries the time.
void logToFile(LogLevel level, const std::string& message, const std::filesystem::path& modDir) {
    std::lock_guard<std::mutex> lk(gFileLogMutex);
    std::error_code             ec;
    std::filesystem::create_directories(modDir / "logs", ec);
    std::ofstream out(modDir / "logs" / "lever.log", std::ios::app);
    if (!out) return;
    static bool first = true;
    if (first) {
        first        = false;
        const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char       buf[32]{};
        std::tm    tm{};
        localtime_s(&tm, &t);
        std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
        out << "=== Lever session " << buf << " (pid " << GetCurrentProcessId() << ")\n";
    }
    out << (level == LogLevel::Warn ? "[WARN] " : "[INFO] ") << message << '\n';
}

void logToMod(LogLevel level, const std::string& message) {
    auto mod = ll::mod::NativeMod::current();
    if (!mod) return; // LeviLamina has no mod bound to this module yet
    if (level == LogLevel::Warn) mod->getLogger().warn("[Lever] {}", message);
    else mod->getLogger().info("[Lever] {}", message);
    logToFile(level, message, mod->getModDir());
}

struct UnloadGuard {
    ~UnloadGuard() {
        uninstallSettingsHook();
        election::withdraw(host::bootstrap());
    }
};

} // namespace

void ensureInit() {
    std::lock_guard<std::mutex> lk(gInitMutex);
    static UnloadGuard guard; // withdraws this copy from the slot when its DLL unloads
    setLogSink(&logToMod);
    if (!election::hasSlot()) election::useSlot(win32::openProcessSlot());
    election::registerSelf(host::bootstrap());
    installSettingsHook();
    if (!gJoinListener) {
        // The settings hook elects on first use of the settings screen; this covers the case where no settings
        // screen is opened before a level join. By then every mod has loaded and enabled, i.e. every copy is in
        // the slot.
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
