#pragma once

#include "internal/Client.h"
#include "internal/Election.h"
#include "internal/Host.h"
#include "internal/Log.h"
#include "internal/Slot.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lever::test {

/// Heap-backed election slot.
class MemorySlot final : public detail::ISlot {
public:
    MemorySlot() { std::memset(&mData, 0, sizeof(mData)); }
    detail::SlotData& data() override { return mData; }
    void              lock() override { mMutex.lock(); }
    void              unlock() override { mMutex.unlock(); }

private:
    detail::SlotData mData;
    std::mutex       mMutex;
};

struct LogLine {
    detail::LogLevel level;
    std::string      text;
};

inline std::vector<LogLine>& logLines() {
    static std::vector<LogLine> lines;
    return lines;
}

inline void captureLog(detail::LogLevel level, const std::string& text) { logLines().push_back({level, text}); }

/// What the platform stub received from the host: notifySettingChanged calls and registryChanged counts.
struct Notification {
    std::string modId, pageId, settingId;
};
inline std::vector<Notification>& notifications() {
    static std::vector<Notification> v;
    return v;
}
inline int& registryChanges() {
    static int n = 0;
    return n;
}

/// Puts the library back into its pristine state and captures log output. Call at the start of every test that
/// touches global state (host model, local registry, election).
inline void resetAll() {
    lever::detail::election::withdraw(detail::host::bootstrap());
    {
        std::lock_guard<std::mutex> lk(detail::host::model().mutex);
        detail::host::model().mods.clear();
    }
    {
        std::lock_guard<std::mutex> lk(detail::client::local().mutex);
        detail::client::local().mods.clear();
    }
    detail::client::resetWarnings();
    notifications().clear();
    registryChanges() = 0;
    logLines().clear();
    detail::setLogSink(&captureLog);
}

/// A descriptor of type T whose memory is exactly `declaredSize` bytes (a heap block of precisely that length, so an
/// over-read is caught by AddressSanitizer), copied from the prefix of `full` and with `size` patched. Simulates a
/// descriptor written by an older copy whose struct was shorter.
template <class T>
class Truncated {
public:
    Truncated(const T& full, std::size_t declaredSize) : mSize(declaredSize) {
        mBlock = static_cast<unsigned char*>(std::malloc(declaredSize));
        std::memcpy(mBlock, &full, declaredSize);
        const auto sz = static_cast<std::uint32_t>(declaredSize);
        std::memcpy(mBlock, &sz, sizeof(sz)); // `size` is always the first member
    }
    ~Truncated() { std::free(mBlock); }
    Truncated(const Truncated&)            = delete;
    Truncated& operator=(const Truncated&) = delete;
    const T*   get() const { return reinterpret_cast<const T*>(mBlock); }

private:
    unsigned char* mBlock;
    std::size_t    mSize;
};

} // namespace lever::test
