#pragma once

#include "Model.h"

namespace ore_settings::detail::host {

/// This copy's host table. Always valid; only used by others once this copy wins the election.
const OreSettingsHostTable* table();

/// The union of everything every copy forwarded. Only meaningful in the host copy.
Registry& model();

} // namespace ore_settings::detail::host
