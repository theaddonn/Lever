#include "Log.h"

#include <atomic>

namespace lever::detail {

namespace {
std::atomic<LogSink> gSink{nullptr};
}

void setLogSink(LogSink sink) { gSink.store(sink); }

void logInfo(const std::string& message) {
    if (auto sink = gSink.load()) sink(LogLevel::Info, message);
}

void logWarn(const std::string& message) {
    if (auto sink = gSink.load()) sink(LogLevel::Warn, message);
}

} // namespace lever::detail
