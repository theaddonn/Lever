#pragma once

#include <string>

namespace lever::detail {

enum class LogLevel { Info, Warn };

/// Where log lines go. The LeviLamina build installs a sink that writes to the logger of the embedding mod; the
/// host tests install a capturing one. Without a sink messages are dropped.
using LogSink = void (*)(LogLevel level, const std::string& message);

void setLogSink(LogSink sink);

void logInfo(const std::string& message);
void logWarn(const std::string& message);

} // namespace lever::detail
