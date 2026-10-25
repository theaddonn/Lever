#pragma once

// Client side of the ABI: this copy's local registry (buffer + source of truth for its own mod's callbacks) and the
// forwarding of it to the host through lever.registry.v1.

#include "Model.h"

namespace lever::detail::client {

/// This copy's registry. Hold its mutex while touching it.
Registry& local();

/// Asks `host` for lever.registry.v1. nullptr (after logging once per process) if the host is not usable or does not
/// serve it; this should not happen by construction, but a missing interface only keeps registrations buffered.
const LeverRegistryV1* registryOf(const LeverBootstrap* host);

/// Sends everything of `reg` not yet forwarded to `iface`, in registration order.
void forwardAll(Registry& reg, const LeverRegistryV1* iface);

/// forwardAll(local(), registry of the elected host). No-op while no host is attached (= buffering).
void flush();

/// Runs the election, then flush(). What the platform calls at the election point (first level join).
void attachAndFlush();

/// Re-arms the log-once warning of registryOf (tests only).
void resetWarnings();

/// Tells the host to drop `modId` (if it was forwarded there).
void unregisterOnHost(const std::string& modId);

} // namespace lever::detail::client
