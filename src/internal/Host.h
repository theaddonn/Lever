#pragma once

#include "Model.h"

namespace lever::detail::host {

/// This copy's host table. Always valid; only used by others once this copy wins the election.
const LeverHostTable* table();

/// The union of everything every copy forwarded. Only meaningful in the host copy.
Registry& model();

} // namespace lever::detail::host
