#include "Log.h"

#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"

namespace lever::detail {

void logInfo(const std::string& message) {
    if (auto mod = ll::mod::NativeMod::current()) mod->getLogger().info("[Lever] {}", message);
}

void logWarn(const std::string& message) {
    if (auto mod = ll::mod::NativeMod::current()) mod->getLogger().warn("[Lever] {}", message);
}

} // namespace lever::detail
