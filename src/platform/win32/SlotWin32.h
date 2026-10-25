#pragma once

#include "internal/Slot.h"

#include <memory>

namespace lever::detail::win32 {

/// Opens (creating it if needed) the process-wide election slot: a named file mapping
///     Local\Lever.g<ABI generation>.p<PID>
/// "Local\" is per logon session, not per process, so two Minecraft instances in one session would otherwise see
/// each other's function pointers; the pid keeps the slot process-wide. The ABI generation keeps incompatible
/// generations apart (they never find each other). nullptr if the mapping cannot be created.
std::unique_ptr<ISlot> openProcessSlot();

} // namespace lever::detail::win32
