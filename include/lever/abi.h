/*
 * Lever C ABI: umbrella header.
 *
 * Includes the frozen bootstrap table and the latest interface generation (v1). Mods normally use the C++ API in
 * lever/Lever.h; this header is for the library internals, for other languages and for tests.
 *
 * ABI COMPATIBILITY RULES
 *
 *  1. Election. Every mod embeds a copy of Lever. The copy with the highest library version becomes the host, so the
 *     host is never older than a client. Only "a newer host understands an older client" has to work.
 *  2. The bootstrap table (abi/bootstrap.h) is published in the election slot and is frozen forever. All new
 *     capability is reached through bootstrap->queryInterface(name, minVersion).
 *  3. Interfaces are tables of C function pointers named "lever.<topic>.v<N>". Each begins with
 *     `uint32_t size; uint32_t version;`. Adding a member = append + bump `version` (clients that need it pass
 *     that minVersion). A change that cannot be an append gets a new name (v2) served BESIDE v1, never replacing it.
 *  4. Descriptors crossing the boundary (ModInfo, PageDesc, SettingDesc, SettingCallbacks) begin with
 *     `uint32_t size`. The host reads min(size, sizeof(known)) bytes, treats the missing tail as zero / default and
 *     rejects only a size below the v1 minimum (LEVER_E_SIZE). Fields are append-only and never change meaning.
 *  5. Enum and flag values are append-only; readers ignore values they do not know (a host answers
 *     LEVER_E_UNSUPPORTED for an unknown widget kind and the client drops that widget).
 *  6. Strings are UTF-8 `const char*`, borrowed for the call and copied by the host. Nothing allocated in one copy
 *     is ever freed in another (no std:: types, no exceptions, no heap ownership across the boundary).
 *  7. Support window. The host keeps serving `lever.registry.v1` and `lever.host.v1` for at least two major
 *     releases after a v2 appears (LEVER_ABI_MIN_SUPPORTED_INTERFACE below).
 *  8. Clients must treat a missing interface as "not available": log once, keep buffering, never crash.
 */
#ifndef LEVER_ABI_H
#define LEVER_ABI_H

#include "abi/bootstrap.h"
#include "abi/v1.h"

/* Lowest interface generation a host of this library version still serves. Raised only after >= 2 major releases. */
#define LEVER_ABI_MIN_SUPPORTED_INTERFACE 1
/* Highest interface generation this header knows. */
#define LEVER_ABI_LATEST_INTERFACE 1

#endif /* LEVER_ABI_H */
