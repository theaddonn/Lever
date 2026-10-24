/*
 * Lever C ABI.
 *
 * Every mod embeds its own copy of the library, so copies built from different library versions have to talk to each
 * other inside one process. They do it through the plain-C function table declared here. Rules:
 *
 *  - Only C types cross the boundary (no std::, no exceptions, no ownership transfer of heap memory).
 *  - Every struct begins with a `size` field. A reader only touches fields that lie inside `size`, a writer fills
 *    `size = sizeof(struct)`. New fields are appended; existing ones never move.
 *  - `LeverHostTable::features` advertises what the host can actually do, so a newer mod can degrade
 *    gracefully on an older host.
 *  - Breaking changes bump LEVER_ABI_VERSION, which is also part of the shared slot name, so incompatible
 *    generations never see each other.
 *  - Strings are UTF-8, NUL terminated, copied by the callee before the call returns. Callbacks are invoked on the
 *    game's UI/main thread unless stated otherwise and must not throw.
 */
#ifndef LEVER_ABI_H
#define LEVER_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LEVER_ABI_VERSION 1

/* Result codes returned by table functions. */
#define LEVER_OK            0
#define LEVER_E_INVALID     1 /* null/malformed argument or struct too small */
#define LEVER_E_NOT_FOUND   2 /* unknown mod or page */
#define LEVER_E_DUPLICATE   3 /* id already registered */

/* Feature flags advertised by a host (LeverHostTable::features). */
#define LEVER_FEATURE_GLOBAL_SETTINGS (1ull << 0) /* Mods tab inside the settings screen */
#define LEVER_FEATURE_PAUSE_MENU      (1ull << 1) /* pause-menu entry */
#define LEVER_FEATURE_WORLD_CREATION  (1ull << 2) /* world-creation tab */
#define LEVER_FEATURE_INFO_PAGES      (1ull << 3) /* generated mod info pages */
#define LEVER_FEATURE_JS_INJECTION    (1ull << 4) /* tier 2 (custom widgets) is active */

/* Surface bits (LeverPageDesc::surfaces). */
#define LEVER_SURFACE_GLOBAL_SETTINGS (1u << 0)
#define LEVER_SURFACE_PAUSE_MENU      (1u << 1)
#define LEVER_SURFACE_WORLD_CREATION  (1u << 2)

typedef enum LeverKind {
    LEVER_KIND_TOGGLE = 0,
    LEVER_KIND_SLIDER = 1,
    LEVER_KIND_DROPDOWN = 2,
    LEVER_KIND_TEXT_FIELD = 3,
    LEVER_KIND_BUTTON = 4,
    LEVER_KIND_BANNER = 5,
    LEVER_KIND_HEADER = 6
} LeverKind;

typedef enum LeverBannerKind {
    LEVER_BANNER_INFO = 0,
    LEVER_BANNER_WARNING = 1,
    LEVER_BANNER_ERROR = 2,
    LEVER_BANNER_SUCCESS = 3
} LeverBannerKind;

typedef struct LeverModInfo {
    uint32_t    size; /* sizeof(LeverModInfo) */
    const char* id;   /* unique, e.g. "distanthorizons" */
    const char* displayName;
    const char* author;
    const char* description;
    uint32_t    versionMajor, versionMinor, versionPatch;
} LeverModInfo;

typedef struct LeverPageDesc {
    uint32_t    size; /* sizeof(LeverPageDesc) */
    const char* modId;
    const char* pageId; /* unique within the mod */
    const char* title;
    const char* icon;     /* vanilla icon key ("video", "creator", ...) or NULL */
    uint32_t    surfaces; /* LEVER_SURFACE_* mask */
} LeverPageDesc;

/*
 * One widget. Which fields apply depends on `kind`:
 *   TOGGLE     getBool / setBool
 *   SLIDER     min, max, step, getNumber / setNumber
 *   DROPDOWN   options[optionCount], getIndex / setIndex
 *   TEXT_FIELD maxLength, getString / setString
 *   BUTTON     onClick
 *   BANNER     bannerKind; `label` carries the text
 *   HEADER     `label` is the title, `description` the subtitle
 * `user` is passed back to every callback. `release(user)` (optional) is called when the setting is dropped.
 */
typedef struct LeverSettingDesc {
    uint32_t        size; /* sizeof(LeverSettingDesc) */
    const char*     modId;
    const char*     pageId;
    const char*     id; /* unique within the page */
    LeverKind kind;
    const char*     label;
    const char*     description;

    double min, max, step;

    const char* const* options;
    uint32_t           optionCount;

    LeverBannerKind bannerKind;
    uint32_t              maxLength;

    void* user;
    int (*getBool)(void* user);
    void (*setBool)(void* user, int value);
    double (*getNumber)(void* user);
    void (*setNumber)(void* user, double value);
    int32_t (*getIndex)(void* user);
    void (*setIndex)(void* user, int32_t index);
    /* Copies up to cap-1 bytes + NUL into buf (buf may be NULL when cap is 0) and returns the full length. */
    size_t (*getString)(void* user, char* buf, size_t cap);
    void (*setString)(void* user, const char* value);
    void (*onClick)(void* user);
    void (*release)(void* user);
} LeverSettingDesc;

/*
 * The host's entry points. Published through the shared process slot (see docs/DESIGN.md). Check `size` before
 * touching a member: a host built from an older library has a shorter table.
 */
typedef struct LeverHostTable {
    uint32_t size; /* sizeof(LeverHostTable) of the copy that built it */
    uint32_t abiVersion;
    uint32_t libVersionMajor, libVersionMinor, libVersionPatch;
    uint64_t features; /* LEVER_FEATURE_* */

    int (*registerMod)(const LeverModInfo* info);
    int (*registerPage)(const LeverPageDesc* page);
    int (*setPageSurfaces)(const char* modId, const char* pageId, uint32_t surfaces);
    int (*addSetting)(const LeverSettingDesc* setting);
    /* Drops a mod with all its pages and settings (release callbacks run). */
    int (*unregisterMod)(const char* modId);
    /* Called once by whichever copy resolves the election, on the winner's table. Installs the hooks. */
    int (*activateHost)(void);
} LeverHostTable;

/* True when `table` is large enough to contain `member` and speaks this ABI generation. */
#define LEVER_TABLE_HAS(table, member)                                                                           \
    ((table) != NULL && (table)->abiVersion == LEVER_ABI_VERSION                                                \
     && (table)->size >= offsetof(LeverHostTable, member) + sizeof((table)->member)                              \
     && (table)->member != NULL)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LEVER_ABI_H */
