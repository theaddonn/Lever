// Host election logic over an ISlot.
//
// Every mod embeds its own copy of Lever, but only one copy may install the game hooks. All copies meet in the slot
// (see Slot.h, platform/win32/SlotWin32.cpp for the named-file-mapping backend):
//
//   - Each copy appends {packed version, pointer to its frozen LeverBootstrap} when first used. Nothing is hooked
//     at this point.
//   - At first real use (Api.cpp: the first level join, or an explicit attachNow()) any copy calls attachOrHost().
//     Under the slot lock the first caller picks the entry with the highest version (the earliest registered wins
//     ties), stores it as the host and, after releasing the lock, calls its bootstrap's activateHost(). Every later
//     caller just reads the stored host.
//   - Because the highest version wins, a host is never older than a client: only "newer host, older client" has
//     to be compatible (docs/DESIGN.md, "ABI compatibility").
//
// Known limitation: if the host copy's DLL is unloaded while others still use it, they keep a dangling bootstrap.
// LeviLamina client mods are only unloaded at shutdown, so withdraw() merely clears the slot entries.

#include "Election.h"

#include "Version.h"

#include <atomic>
#include <mutex>

namespace lever::detail::election {

namespace {

std::mutex                         gMutex; // serialises threads of this copy
std::unique_ptr<ISlot>             gSlot;
std::atomic<const LeverBootstrap*> gHost{nullptr};

struct Guard {
    explicit Guard(ISlot& s) : slot(s) { slot.lock(); }
    ~Guard() { slot.unlock(); }
    Guard(const Guard&)            = delete;
    Guard& operator=(const Guard&) = delete;
    ISlot& slot;
};

std::uint64_t versionOf(const LeverBootstrap* b) { return packVersion(b->libMajor, b->libMinor, b->libPatch); }

} // namespace

bool useSlot(std::unique_ptr<ISlot> slot) {
    std::lock_guard<std::mutex> lk(gMutex);
    if (gSlot || !slot) return gSlot != nullptr;
    {
        Guard     guard(*slot);
        SlotData& d = slot->data();
        if (d.magic == 0) { // first creator: stamp the zeroed block
            d.magic  = kSlotMagic;
            d.layout = kSlotLayout;
        }
        if (d.magic != kSlotMagic || d.layout != kSlotLayout) return false; // foreign data under our name
    }
    gSlot = std::move(slot);
    return true;
}

bool hasSlot() {
    std::lock_guard<std::mutex> lk(gMutex);
    return gSlot != nullptr;
}

void registerSelf(const LeverBootstrap* self) {
    if (!LEVER_BOOTSTRAP_VALID(self)) return;
    std::lock_guard<std::mutex> lk(gMutex);
    if (!gSlot) return;
    Guard      guard(*gSlot);
    SlotData&  d   = gSlot->data();
    const auto key = reinterpret_cast<std::uint64_t>(self);
    for (std::uint32_t i = 0; i < d.count; ++i) {
        if (d.entries[i].bootstrap == key) return;
    }
    if (d.count < kSlotMaxEntries) d.entries[d.count++] = SlotEntry{versionOf(self), key};
}

const LeverBootstrap* attachOrHost() {
    if (const auto* cached = gHost.load(std::memory_order_acquire)) return cached;

    const LeverBootstrap* activate = nullptr;
    const LeverBootstrap* result   = nullptr;
    {
        std::lock_guard<std::mutex> lk(gMutex);
        if (!gSlot) return nullptr;
        Guard     guard(*gSlot);
        SlotData& d = gSlot->data();
        if (d.host == 0) {
            const SlotEntry* best = nullptr;
            for (std::uint32_t i = 0; i < d.count; ++i) {
                if (!best || d.entries[i].version > best->version) best = &d.entries[i]; // ties: earliest wins
            }
            if (!best) return nullptr;
            d.host        = best->bootstrap;
            d.hostVersion = best->version;
            activate      = reinterpret_cast<const LeverBootstrap*>(best->bootstrap);
        }
        result = reinterpret_cast<const LeverBootstrap*>(d.host);
    }
    gHost.store(result, std::memory_order_release);
    // Outside the slot lock: activation installs hooks and may take a while. Registrations forwarded meanwhile are
    // buffered by the host model.
    if (activate && LEVER_BOOTSTRAP_VALID(activate)) activate->activateHost();
    return result;
}

const LeverBootstrap* host() { return gHost.load(std::memory_order_acquire); }

std::uint32_t registeredCopies() {
    std::lock_guard<std::mutex> lk(gMutex);
    if (!gSlot) return 0;
    Guard guard(*gSlot);
    return gSlot->data().count;
}

void withdraw(const LeverBootstrap* self) {
    std::lock_guard<std::mutex> lk(gMutex);
    if (!gSlot) return;
    const auto key = reinterpret_cast<std::uint64_t>(self);
    {
        Guard     guard(*gSlot);
        SlotData& d = gSlot->data();
        for (std::uint32_t i = 0; i < d.count; ++i) {
            if (d.entries[i].bootstrap == key) {
                for (std::uint32_t j = i + 1; j < d.count; ++j) d.entries[j - 1] = d.entries[j]; // keep order
                --d.count;
                break;
            }
        }
        if (d.host == key) d.host = 0; // a later attacher may re-elect
    }
    gHost.store(nullptr, std::memory_order_release);
    gSlot.reset();
}

} // namespace lever::detail::election
