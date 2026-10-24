// Host election over a named file mapping.
//
// Every mod embeds its own copy of Lever, but only one copy may install the game hooks. All copies meet in a
// small shared-memory slot:
//
//   - The slot is a *named file mapping*. The name carries the ABI generation and the process id:
//         Local\Lever.v<ABI>.p<PID>
//     "Local\" is per logon session, not per process, so two Minecraft instances in one session would otherwise see
//     each other's function pointers. The pid keeps the slot process-wide. The ABI generation keeps incompatible
//     generations apart (they simply never find each other).
//   - Each copy appends {packed version, pointer to its LeverHostTable} when it is first used. Nothing is
//     hooked at this point.
//   - At first real use (see Api.cpp: the first level join, or an explicit attachNow()) any copy may call
//     attachOrHost(). Under the slot's spin lock the first caller picks the entry with the highest version (the
//     earliest registered wins ties), stores it as the host and, after releasing the lock, tells it to activate by
//     calling its table's activateHost(). Every later caller just reads the stored host.
//   - Pointers in the slot are only meaningful inside this process, which is exactly the scope of the name.
//
// Known limitation: if the host copy's DLL is unloaded while others still use it, they keep a dangling table.
// LeviLamina client mods are only unloaded at shutdown, so withdraw() merely clears the slot entries.

#include "Election.h"

#include "Host.h"
#include "Version.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

#include <Windows.h>

namespace lever::detail::election {

namespace {

constexpr std::uint32_t kMagic      = 0x5345524Fu; // "ORES"
constexpr std::uint32_t kLayout     = 1;           // changes together with LEVER_ABI_VERSION, never alone
constexpr std::uint32_t kMaxEntries = 32;

struct Entry {
    std::uint64_t version; // packVersion()
    std::uint64_t table;   // const LeverHostTable*, valid in this process only
};

struct Slot {
    std::uint32_t magic;
    std::uint32_t layout;
    volatile LONG lock;  // 0 = free, 1 = held
    std::uint32_t count; // used entries
    std::uint64_t hostTable;   // 0 until elected
    std::uint64_t hostVersion;
    Entry         entries[kMaxEntries];
};

std::mutex                                  gMutex; // serialises threads of this copy
HANDLE                                      gMapping = nullptr;
Slot*                                       gSlot    = nullptr;
bool                                        gRegistered = false;
std::atomic<const LeverHostTable*>    gHost{nullptr};

struct SlotLock {
    explicit SlotLock(Slot* s) : slot(s) {
        while (InterlockedCompareExchange(&slot->lock, 1, 0) != 0) {
            YieldProcessor();
            SwitchToThread();
        }
    }
    ~SlotLock() { InterlockedExchange(&slot->lock, 0); }
    SlotLock(const SlotLock&)            = delete;
    SlotLock& operator=(const SlotLock&) = delete;
    Slot*     slot;
};

// Create-or-open. The first creator gets zeroed pages; magic/layout are then stamped under the lock.
Slot* openSlot() {
    if (gSlot) return gSlot;
    wchar_t name[96];
    swprintf(name, 96, L"Local\\Lever.v%u.p%lu", static_cast<unsigned>(LEVER_ABI_VERSION), GetCurrentProcessId());
    gMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Slot), name);
    if (!gMapping) return nullptr;
    void* view = MapViewOfFile(gMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Slot));
    if (!view) {
        CloseHandle(gMapping);
        gMapping = nullptr;
        return nullptr;
    }
    auto* slot = static_cast<Slot*>(view);
    {
        SlotLock guard(slot);
        if (slot->magic == 0) {
            slot->magic  = kMagic;
            slot->layout = kLayout;
        }
    }
    if (slot->magic != kMagic || slot->layout != kLayout) { // foreign data under our name: refuse to use it
        UnmapViewOfFile(view);
        CloseHandle(gMapping);
        gMapping = nullptr;
        return nullptr;
    }
    gSlot = slot;
    return gSlot;
}

} // namespace

void registerSelf() {
    std::lock_guard<std::mutex> lk(gMutex);
    if (gRegistered) return;
    Slot* slot = openSlot();
    if (!slot) return;
    const auto self = reinterpret_cast<std::uint64_t>(host::table());
    {
        SlotLock guard(slot);
        bool present = false;
        for (std::uint32_t i = 0; i < slot->count; ++i) present = present || slot->entries[i].table == self;
        if (!present && slot->count < kMaxEntries) slot->entries[slot->count++] = Entry{kSelfVersion, self};
    }
    gRegistered = true;
}

const LeverHostTable* attachOrHost() {
    if (const auto* cached = gHost.load(std::memory_order_acquire)) return cached;

    registerSelf();
    const LeverHostTable* activate = nullptr;
    const LeverHostTable* result   = nullptr;
    {
        std::lock_guard<std::mutex> lk(gMutex);
        if (!gSlot) return nullptr;
        SlotLock guard(gSlot);
        if (gSlot->hostTable == 0) {
            const Entry* best = nullptr;
            for (std::uint32_t i = 0; i < gSlot->count; ++i) {
                if (!best || gSlot->entries[i].version > best->version) best = &gSlot->entries[i]; // ties: earliest wins
            }
            if (!best) return nullptr;
            gSlot->hostTable   = best->table;
            gSlot->hostVersion = best->version;
            activate           = reinterpret_cast<const LeverHostTable*>(best->table);
        }
        result = reinterpret_cast<const LeverHostTable*>(gSlot->hostTable);
    }
    gHost.store(result, std::memory_order_release);
    // Outside the slot lock: activation installs hooks and may take a while. Registrations forwarded meanwhile are
    // buffered by the host model.
    if (activate && LEVER_TABLE_HAS(activate, activateHost)) activate->activateHost();
    return result;
}

const LeverHostTable* hostTable() { return gHost.load(std::memory_order_acquire); }

void withdraw() {
    std::lock_guard<std::mutex> lk(gMutex);
    if (!gSlot) return;
    const auto self = reinterpret_cast<std::uint64_t>(host::table());
    {
        SlotLock guard(gSlot);
        for (std::uint32_t i = 0; i < gSlot->count; ++i) {
            if (gSlot->entries[i].table == self) {
                for (std::uint32_t j = i + 1; j < gSlot->count; ++j) gSlot->entries[j - 1] = gSlot->entries[j]; // keep order
                --gSlot->count;
                break;
            }
        }
        if (gSlot->hostTable == self) gSlot->hostTable = 0; // a later attacher may re-elect
    }
    gHost.store(nullptr, std::memory_order_release);
    UnmapViewOfFile(gSlot);
    CloseHandle(gMapping);
    gSlot       = nullptr;
    gMapping    = nullptr;
    gRegistered = false;
}

} // namespace lever::detail::election
