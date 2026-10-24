/*
 * OreSettings C ABI.
 *
 * Every mod embeds its own copy of the library, so copies built from different library versions have to talk to each
 * other inside one process. They do it through the plain-C function table declared here. Rules:
 *
 *  - Only C types cross the boundary (no std::, no exceptions, no ownership transfer of heap memory).
 *  - Every struct begins with a `size` field. A reader only touches fields that lie inside `size`, a writer fills
 *    `size = sizeof(struct)`. New fields are appended; existing ones never move.
 *  - `OreSettingsHostTable::features` advertises what the host can actually do, so a newer mod can degrade
 *    gracefully on an older host.
 *  - Breaking changes bump ORESETTINGS_ABI_VERSION, which is also part of the shared slot name, so incompatible
 *    generations never see each other.
 *  - Strings are UTF-8, NUL terminated, copied by the callee before the call returns. Callbacks are invoked on the
 *    game's UI/main thread unless stated otherwise and must not throw.
 */
#ifndef ORESETTINGS_ABI_H
#define ORESETTINGS_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORESETTINGS_ABI_VERSION 1

/* Result codes returned by table functions. */
#define ORESETTINGS_OK            0
#define ORESETTINGS_E_INVALID     1 /* null/malformed argument or struct too small */
#define ORESETTINGS_E_NOT_FOUND   2 /* unknown mod or page */
#define ORESETTINGS_E_DUPLICATE   3 /* id already registered */

/* Feature flags advertised by a host (OreSettingsHostTable::features). */
#define ORESETTINGS_FEATURE_GLOBAL_SETTINGS (1ull << 0) /* Mods tab inside the settings screen */
#define ORESETTINGS_FEATURE_PAUSE_MENU      (1ull << 1) /* pause-menu entry */
#define ORESETTINGS_FEATURE_WORLD_CREATION  (1ull << 2) /* world-creation tab */
#define ORESETTINGS_FEATURE_INFO_PAGES      (1ull << 3) /* generated mod info pages */
#define ORESETTINGS_FEATURE_JS_INJECTION    (1ull << 4) /* tier 2 (custom widgets) is active */

/* Surface bits (OreSettingsPageDesc::surfaces). */
#define ORESETTINGS_SURFACE_GLOBAL_SETTINGS (1u << 0)
#define ORESETTINGS_SURFACE_PAUSE_MENU      (1u << 1)
#define ORESETTINGS_SURFACE_WORLD_CREATION  (1u << 2)

typedef enum OreSettingsKind {
    ORESETTINGS_KIND_TOGGLE = 0,
    ORESETTINGS_KIND_SLIDER = 1,
    ORESETTINGS_KIND_DROPDOWN = 2,
    ORESETTINGS_KIND_TEXT_FIELD = 3,
    ORESETTINGS_KIND_BUTTON = 4,
    ORESETTINGS_KIND_BANNER = 5,
    ORESETTINGS_KIND_HEADER = 6
} OreSettingsKind;

typedef enum OreSettingsBannerKind {
    ORESETTINGS_BANNER_INFO = 0,
    ORESETTINGS_BANNER_WARNING = 1,
    ORESETTINGS_BANNER_ERROR = 2,
    ORESETTINGS_BANNER_SUCCESS = 3
} OreSettingsBannerKind;

typedef struct OreSettingsModInfo {
    uint32_t    size; /* sizeof(OreSettingsModInfo) */
    const char* id;   /* unique, e.g. "distanthorizons" */
    const char* displayName;
    const char* author;
    const char* description;
    uint32_t    versionMajor, versionMinor, versionPatch;
} OreSettingsModInfo;

typedef struct OreSettingsPageDesc {
    uint32_t    size; /* sizeof(OreSettingsPageDesc) */
    const char* modId;
    const char* pageId; /* unique within the mod */
    const char* title;
    const char* icon;     /* vanilla icon key ("video", "creator", ...) or NULL */
    uint32_t    surfaces; /* ORESETTINGS_SURFACE_* mask */
} OreSettingsPageDesc;

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
typedef struct OreSettingsSettingDesc {
    uint32_t        size; /* sizeof(OreSettingsSettingDesc) */
    const char*     modId;
    const char*     pageId;
    const char*     id; /* unique within the page */
    OreSettingsKind kind;
    const char*     label;
    const char*     description;

    double min, max, step;

    const char* const* options;
    uint32_t           optionCount;

    OreSettingsBannerKind bannerKind;
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
} OreSettingsSettingDesc;

/*
 * The host's entry points. Published through the shared process slot (see docs/DESIGN.md). Check `size` before
 * touching a member: a host built from an older library has a shorter table.
 */
typedef struct OreSettingsHostTable {
    uint32_t size; /* sizeof(OreSettingsHostTable) of the copy that built it */
    uint32_t abiVersion;
    uint32_t libVersionMajor, libVersionMinor, libVersionPatch;
    uint64_t features; /* ORESETTINGS_FEATURE_* */

    int (*registerMod)(const OreSettingsModInfo* info);
    int (*registerPage)(const OreSettingsPageDesc* page);
    int (*setPageSurfaces)(const char* modId, const char* pageId, uint32_t surfaces);
    int (*addSetting)(const OreSettingsSettingDesc* setting);
    /* Drops a mod with all its pages and settings (release callbacks run). */
    int (*unregisterMod)(const char* modId);
    /* Called once by whichever copy resolves the election, on the winner's table. Installs the hooks. */
    int (*activateHost)(void);
} OreSettingsHostTable;

/* True when `table` is large enough to contain `member` and speaks this ABI generation. */
#define ORESETTINGS_TABLE_HAS(table, member)                                                                           \
    ((table) != NULL && (table)->abiVersion == ORESETTINGS_ABI_VERSION                                                \
     && (table)->size >= offsetof(OreSettingsHostTable, member) + sizeof((table)->member)                              \
     && (table)->member != NULL)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ORESETTINGS_ABI_H */
