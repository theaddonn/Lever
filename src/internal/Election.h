#pragma once

#include "oresettings/abi.h"

namespace ore_settings::detail::election {

/// Publishes this copy (version + host table) in the process-wide slot. Idempotent and thread-safe.
void registerSelf();

/// Runs the election if nobody has been elected yet and returns the host's table (this copy's own table if it won).
/// Idempotent and thread-safe. Returns nullptr only if the slot could not be created.
const OreSettingsHostTable* attachOrHost();

/// The cached result of attachOrHost(); nullptr until the election has run. Lock-free.
const OreSettingsHostTable* hostTable();

/// Removes this copy from the slot (called automatically when the DLL unloads).
void withdraw();

} // namespace ore_settings::detail::election
