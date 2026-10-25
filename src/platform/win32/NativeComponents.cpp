#include "NativeComponents.h"

#include "internal/Log.h"

#include "ll/api/memory/Memory.h"
#include "mc/client/settings/GroupInfoComponent.h"
#include "mc/client/settings/TextComponent.h"
#include "mc/deps/core/memory/IMemoryAllocator.h"

#include <cstring>

namespace lever::detail::native {

namespace {

// Real (in-game) layout of std::variant<BooleanComponent, NumberComponent<int>, NumberComponent<float>,
// OptionComponent, StringComponent, ActionComponent, TextComponent, GroupInfoComponent, BannerComponent>.
constexpr std::size_t   kVariantSize    = 0x3d0;
constexpr std::size_t   kIndexOffset    = 0x3c8;
constexpr unsigned char kTextIndex      = 6;
constexpr unsigned char kGroupInfoIndex = 7;

void* allocateElement() {
    // The game frees the element with its own operator delete, so it has to come from the game's allocator
    // regardless of whether the embedding mod overrides operator new.
    void* raw = ::ll::memory::getDefaultAllocator()._allocate(kVariantSize);
    if (raw) std::memset(raw, 0, kVariantSize);
    return raw;
}

Element finish(void* raw, unsigned char index) {
    static_cast<unsigned char*>(raw)[kIndexOffset] = index;
    return Element(reinterpret_cast<Element::element_type*>(raw));
}

} // namespace

Element makeText(std::string_view id, std::string_view name, std::optional<std::string> description) {
    void* raw = allocateElement();
    if (!raw) return nullptr;
    reinterpret_cast<::Settings::TextComponent*>(raw)->$ctor(id, name, std::move(description));
    return finish(raw, kTextIndex);
}

Element makeGroupInfo(std::string_view id, std::string_view name, std::optional<std::string> description) {
    void* raw = allocateElement();
    if (!raw) return nullptr;
    reinterpret_cast<::Settings::GroupInfoComponent*>(raw)->$ctor(id, name, std::move(description));
    return finish(raw, kGroupInfoIndex);
}

} // namespace lever::detail::native
