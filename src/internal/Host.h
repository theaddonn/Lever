#pragma once

#include "Model.h"

namespace lever::detail::host {

/// This copy's frozen bootstrap table. Always valid; only used by others once this copy wins the election.
const LeverBootstrap* bootstrap();

/// Interface dispatch behind bootstrap()->queryInterface: nullptr for an unknown name, a null name, or when the
/// served table's `version` is below `minVersion`.
const void* queryInterface(const char* name, std::uint32_t minVersion);

/// The registry interface this copy serves (also reachable via queryInterface("lever.registry.v1", 1)).
const LeverRegistryV1* registryV1();

/// The union of everything every copy forwarded. Only meaningful in the host copy.
Registry& model();

} // namespace lever::detail::host
