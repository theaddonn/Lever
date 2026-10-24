#pragma once

#include <cstdint>

#ifndef LEVER_VERSION_MAJOR
#define LEVER_VERSION_MAJOR 0
#define LEVER_VERSION_MINOR 0
#define LEVER_VERSION_PATCH 0
#define LEVER_VERSION_STRING "0.0.0"
#endif

namespace lever::detail {

/// major.minor.patch packed so a plain integer compare orders versions (each component < 65536).
constexpr std::uint64_t packVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) {
    return (std::uint64_t{major & 0xFFFFu} << 32) | (std::uint64_t{minor & 0xFFFFu} << 16) | (patch & 0xFFFFu);
}

constexpr std::uint64_t kSelfVersion =
    packVersion(LEVER_VERSION_MAJOR, LEVER_VERSION_MINOR, LEVER_VERSION_PATCH);

} // namespace lever::detail
