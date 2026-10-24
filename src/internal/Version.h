#pragma once

#include <cstdint>

#ifndef ORESETTINGS_VERSION_MAJOR
#define ORESETTINGS_VERSION_MAJOR 0
#define ORESETTINGS_VERSION_MINOR 0
#define ORESETTINGS_VERSION_PATCH 0
#define ORESETTINGS_VERSION_STRING "0.0.0"
#endif

namespace ore_settings::detail {

/// major.minor.patch packed so a plain integer compare orders versions (each component < 65536).
constexpr std::uint64_t packVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) {
    return (std::uint64_t{major & 0xFFFFu} << 32) | (std::uint64_t{minor & 0xFFFFu} << 16) | (patch & 0xFFFFu);
}

constexpr std::uint64_t kSelfVersion =
    packVersion(ORESETTINGS_VERSION_MAJOR, ORESETTINGS_VERSION_MINOR, ORESETTINGS_VERSION_PATCH);

} // namespace ore_settings::detail
