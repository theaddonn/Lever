#pragma once

// Everything that needs Windows or LeviLamina lives behind this interface so the rest of the library (model,
// registry, ABI marshalling, interface dispatch, election logic, client forwarding) builds and tests on any host.
// The real implementation is platform/win32/; tests/stubs/Platform.cpp is the test double.

namespace lever::detail::platform {

/// Lazy per-copy initialisation, idempotent: installs the log sink, opens the election slot, registers this copy in
/// it, installs the native settings hook and arranges for the election to run at the first level join.
void ensureInit();

/// Called when the last mod of this copy unregistered: drop event listeners so no callback outlives the DLL.
void releaseListeners();

/// Installs this copy's hook on the game's settings-registry builder (idempotent). Every copy hooks; inside the
/// hook only the elected host injects, so the chain of hooks adds the Mods page exactly once.
void installSettingsHook();

/// Removes the hook (DLL unload).
void uninstallSettingsHook();

} // namespace lever::detail::platform
