#pragma once

// Everything that needs Windows or LeviLamina lives behind this interface so the rest of the library (model,
// registry, ABI marshalling, interface dispatch, election logic, client forwarding) builds and tests on any host.
// The real implementation is platform/win32/Platform.cpp; tests/stubs/Platform.cpp is the test double.

namespace lever::detail::platform {

/// Lazy per-copy initialisation, idempotent: installs the log sink, opens the election slot, registers this copy in
/// it and arranges for the election to run at the first level join.
void ensureInit();

/// Called when the last mod of this copy unregistered: drop event listeners so no callback outlives the DLL.
void releaseListeners();

} // namespace lever::detail::platform
