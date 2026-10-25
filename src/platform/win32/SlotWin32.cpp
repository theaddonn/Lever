#include "SlotWin32.h"

#include "lever/abi.h"

#include <cstdio>

#include <Windows.h>

namespace lever::detail::win32 {

namespace {

class MappedSlot final : public ISlot {
public:
    MappedSlot(HANDLE mapping, SlotData* view) : mMapping(mapping), mView(view) {}
    ~MappedSlot() override {
        UnmapViewOfFile(mView);
        CloseHandle(mMapping);
    }

    SlotData& data() override { return *mView; }
    void      lock() override {
        while (InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&mView->lock), 1, 0) != 0) {
            YieldProcessor();
            SwitchToThread();
        }
    }
    void unlock() override { InterlockedExchange(reinterpret_cast<volatile LONG*>(&mView->lock), 0); }

private:
    HANDLE    mMapping;
    SlotData* mView;
};

} // namespace

std::unique_ptr<ISlot> openProcessSlot() {
    wchar_t name[96];
    swprintf(name, 96, L"Local\\Lever.g%u.p%lu", static_cast<unsigned>(LEVER_ABI_GENERATION), GetCurrentProcessId());
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SlotData), name);
    if (!mapping) return nullptr;
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SlotData));
    if (!view) {
        CloseHandle(mapping);
        return nullptr;
    }
    return std::make_unique<MappedSlot>(mapping, static_cast<SlotData*>(view));
}

} // namespace lever::detail::win32
