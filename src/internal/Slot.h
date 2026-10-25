#pragma once

// The election slot: a small block of memory shared by every Lever copy in the process.
//
// FROZEN LAYOUT (generation 1). The slot is found by name (platform/win32 uses Local\Lever.g<generation>.p<pid>),
// so a layout change is a new generation. Entries point at each copy's frozen LeverBootstrap.

#include <cstdint>

namespace lever::detail {

constexpr std::uint32_t kSlotMagic      = 0x5345524Fu; // "ORES"
constexpr std::uint32_t kSlotLayout     = 1;
constexpr std::uint32_t kSlotMaxEntries = 32;

struct SlotEntry {
    std::uint64_t version;   // packVersion() of the copy
    std::uint64_t bootstrap; // const LeverBootstrap*, meaningful in this process only
};

struct SlotData {
    std::uint32_t magic;
    std::uint32_t layout;
    std::int32_t  lock;  // 0 = free, 1 = held; manipulated by ISlot::lock/unlock only
    std::uint32_t count; // used entries
    std::uint64_t host;  // 0 until elected
    std::uint64_t hostVersion;
    SlotEntry     entries[kSlotMaxEntries];
};

/// Storage backend of the slot. Win32: a named file mapping. Tests: heap memory.
class ISlot {
public:
    virtual ~ISlot() = default;
    virtual SlotData& data() = 0;
    virtual void      lock() = 0;
    virtual void      unlock() = 0;
};

} // namespace lever::detail
