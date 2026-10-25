#pragma once

// Reading descriptors that cross the copy boundary. A descriptor begins with `uint32_t size`, the sizeof() of the
// struct as the writing copy knew it. A reader copies min(size, its own sizeof) bytes into a zeroed local struct,
// so fields the writer did not have read as zero, and never touches memory past `size`.

#include "lever/abi.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace lever::detail {

/// Copies the descriptor at `src` into `out` (zero-filled tail). `out.size` becomes the number of bytes that were
/// really supplied, so LEVER_DESC_HAS works on it.
/// Returns LEVER_OK, LEVER_E_INVALID for a null pointer or LEVER_E_SIZE when `src->size < minSize`.
template <class T>
int readDescriptor(const T* src, std::size_t minSize, T& out) {
    if (!src) return LEVER_E_INVALID;
    const std::uint32_t declared = src->size; // the first four bytes are always readable: size is the first member
    if (declared < minSize) return LEVER_E_SIZE;
    const std::size_t n = declared < sizeof(T) ? declared : sizeof(T);
    std::memset(static_cast<void*>(&out), 0, sizeof(T));
    std::memcpy(static_cast<void*>(&out), static_cast<const void*>(src), n);
    out.size = static_cast<std::uint32_t>(n);
    return LEVER_OK;
}

/// True when the (possibly shorter) descriptor `d` contains `member`.
#define LEVER_DESC_HAS(type, d, member) ((d).size >= LEVER_FIELD_END(type, member))

/// NULL -> empty. The result is an independent copy.
inline std::string copyString(const char* s) { return s ? std::string(s) : std::string(); }

} // namespace lever::detail
