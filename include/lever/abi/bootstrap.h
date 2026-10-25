/*
 * Lever C ABI: the frozen bootstrap table.
 *
 * ============================================================================================================
 *  THIS FILE NEVER CHANGES SHAPE.
 *
 *  Every mod embeds its own copy of Lever. At election time the copy with the highest library version becomes the
 *  host, so the host is never older than any client that talks to it. The only compatibility direction the ABI has
 *  to provide is therefore "a newer host understands an older client". To make that possible the one thing every
 *  copy must agree on, the struct published in the election slot, is frozen for good: the layout below is the
 *  layout of generation 1 forever. LEVER_ABI_GENERATION only changes if this struct itself would have to change,
 *  which is not planned. Copies of different generations live in different slots (the generation is part of the
 *  slot name) and never see each other.
 *
 *  New capability is NEVER added here. It is added as a new interface (or a new revision of one) that clients fetch
 *  through `queryInterface`. See lever/abi.h for the compatibility rules.
 * ============================================================================================================
 */
#ifndef LEVER_ABI_BOOTSTRAP_H
#define LEVER_ABI_BOOTSTRAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LEVER_ABI_GENERATION 1

/* Offset just past `member`; used to test whether a (possibly shorter, older) struct contains it. */
#define LEVER_FIELD_END(type, member) (offsetof(type, member) + sizeof(((type*)0)->member))

/* Result codes returned by interface functions. New codes may be appended; unknown codes mean "failed". */
#define LEVER_OK            0
#define LEVER_E_INVALID     1 /* null or malformed argument */
#define LEVER_E_NOT_FOUND   2 /* unknown mod or page */
#define LEVER_E_DUPLICATE   3 /* id already registered */
#define LEVER_E_UNSUPPORTED 4 /* well-formed but not understood (for example an enum value from the future) */
#define LEVER_E_SIZE        5 /* descriptor `size` is below the minimum the interface accepts */

typedef struct LeverBootstrap {
    uint32_t size;          /* sizeof(LeverBootstrap) */
    uint32_t abiGeneration; /* LEVER_ABI_GENERATION */
    uint32_t libMajor, libMinor, libPatch; /* version of the library copy that built this table */
    uint32_t reserved;      /* 0 */

    /*
     * Returns the interface table registered under `name` (for example "lever.registry.v1") if the host serves it
     * and the table's own `version` field is >= minVersion; otherwise NULL. The returned pointer stays valid for
     * the life of the process. Names are matched exactly; a breaking redesign gets a new name ("...v2"), an
     * additive change bumps `version` inside the same name.
     */
    const void* (*queryInterface)(const char* name, uint32_t minVersion);

    /* Called once by whichever copy resolves the election, on the winner's bootstrap. Installs the game hooks. */
    void (*activateHost)(void);

    /* Number of copies currently registered in the election slot (diagnostics). */
    uint32_t (*registeredCopies)(void);
} LeverBootstrap;

/* A bootstrap is usable when it speaks this generation and is complete. */
#define LEVER_BOOTSTRAP_VALID(b)                                                                                 \
    ((b) != NULL && (b)->abiGeneration == LEVER_ABI_GENERATION && (b)->size >= sizeof(LeverBootstrap)            \
     && (b)->queryInterface != NULL && (b)->activateHost != NULL)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LEVER_ABI_BOOTSTRAP_H */
