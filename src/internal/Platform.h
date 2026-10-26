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

/// A page was added to the host model after the game may already have built its settings registry: register the
/// page's factory into the live registry (host copy only; a no-op before the registry exists).
void registryChanged();

/// Makes the open widgets of the given setting (empty pageId / settingId = wildcard) re-read their getters.
void notifySettingChanged(const char* modId, const char* pageId, const char* settingId);

} // namespace lever::detail::platform
