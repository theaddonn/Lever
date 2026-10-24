#pragma once

#include <string>

namespace lever::detail {

/// Logs through the logger of the mod this copy is embedded in. Silently drops the message if LeviLamina has no
/// mod bound to this module yet.
void logInfo(const std::string& message);
void logWarn(const std::string& message);

} // namespace lever::detail
