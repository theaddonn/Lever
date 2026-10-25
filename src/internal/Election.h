#pragma once

#include "Slot.h"

#include "lever/abi.h"

#include <memory>

namespace lever::detail::election {

/// Supplies the slot storage (platform::ensureInit does this). Ignored if a slot is already set. Returns false if
/// `slot` holds foreign data and was rejected.
bool useSlot(std::unique_ptr<ISlot> slot);
bool hasSlot();

/// Publishes a copy (version + bootstrap) in the slot. Idempotent and thread-safe. No-op without a slot.
void registerSelf(const LeverBootstrap* self);

/// Runs the election if nobody has been elected yet and returns the host's bootstrap (the caller's own if it won).
/// Idempotent and thread-safe. nullptr if there is no slot or nobody registered.
const LeverBootstrap* attachOrHost();

/// The cached result of attachOrHost(); nullptr until the election has run. Lock-free.
const LeverBootstrap* host();

/// Number of registered copies (0 without a slot).
std::uint32_t registeredCopies();

/// Removes `self` from the slot and drops this process's view of it (called when the DLL unloads; tests use it to reset).
void withdraw(const LeverBootstrap* self);

} // namespace lever::detail::election
