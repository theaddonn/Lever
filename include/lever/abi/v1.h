/*
 * Lever C ABI, interface generation "v1": lever.registry.v1 and lever.host.v1.
 *
 * FROZEN. Plain C: no C++ features, safe inside extern "C". Rules for editing this file:
 *   - Never remove, reorder, retype or change the meaning of an existing field.
 *   - Fields may only be appended at the end of a struct, and an interface's `version` is bumped when that happens
 *     (a descriptor grows without a version bump: its `size` already tells readers).
 *   - Enum / constant values are never reused. Readers must ignore values they do not know.
 *   - Anything that cannot be expressed as an append goes into a new file (abi/v2.h) beside this one.
 * The 64-bit layout is pinned by tests/unit/AbiLayoutTests.cpp.
 */
#ifndef LEVER_ABI_V1_H
#define LEVER_ABI_V1_H

#include "bootstrap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LEVER_IFACE_REGISTRY_V1 "lever.registry.v1"
#define LEVER_IFACE_HOST_V1     "lever.host.v1"

/* Feature flags returned by LeverHostV1::getFeatures. Unknown bits are ignored. */
#define LEVER_FEATURE_GLOBAL_SETTINGS (1ull << 0) /* Mods tab inside the settings screen */
#define LEVER_FEATURE_PAUSE_MENU      (1ull << 1) /* pause-menu entry */
#define LEVER_FEATURE_WORLD_CREATION  (1ull << 2) /* world-creation tab */
#define LEVER_FEATURE_INFO_PAGES      (1ull << 3) /* generated mod info pages */
#define LEVER_FEATURE_JS_INJECTION    (1ull << 4) /* tier 2 (custom widgets) is active */

/* Surface bits (LeverPageDesc::surfaces). Unknown bits are ignored. */
#define LEVER_SURFACE_GLOBAL_SETTINGS (1u << 0)
#define LEVER_SURFACE_PAUSE_MENU      (1u << 1)
#define LEVER_SURFACE_WORLD_CREATION  (1u << 2)

/* Widget kinds (LeverSettingDesc::kind, a uint32_t). A host rejects kinds it does not know with LEVER_E_UNSUPPORTED. */
typedef enum LeverKind {
    LEVER_KIND_TOGGLE = 0,
    LEVER_KIND_SLIDER = 1,
    LEVER_KIND_DROPDOWN = 2,
    LEVER_KIND_TEXT_FIELD = 3,
    LEVER_KIND_BUTTON = 4,
    LEVER_KIND_BANNER = 5,
    LEVER_KIND_HEADER = 6
} LeverKind;

/* LeverSettingDesc::bannerKind (a uint32_t). Unknown values are shown as INFO. */
typedef enum LeverBannerKind {
    LEVER_BANNER_INFO = 0,
    LEVER_BANNER_WARNING = 1,
    LEVER_BANNER_ERROR = 2,
    LEVER_BANNER_SUCCESS = 3
} LeverBannerKind;

/*
 * Descriptors. Every one starts with `size`, the sizeof() of the struct as the CALLER saw it. The host reads
 * min(size, its own sizeof) bytes and treats everything past `size` as zero / default. It rejects a descriptor only
 * when `size` is below the LEVER_*_V1_MIN_SIZE below (LEVER_E_SIZE). All strings are UTF-8, NUL terminated, borrowed
 * for the duration of the call and copied by the host. NULL means empty.
 */

typedef struct LeverModInfo {
    uint32_t    size;      /* sizeof(LeverModInfo) */
    uint32_t    reserved0; /* 0 */
    const char* id;        /* required, unique, e.g. "distanthorizons" */
    const char* displayName; /* default: id */
    const char* author;
    const char* description;
    uint32_t    versionMajor, versionMinor, versionPatch;
    uint32_t    reserved1; /* 0 */
} LeverModInfo;
#define LEVER_MODINFO_V1_MIN_SIZE LEVER_FIELD_END(LeverModInfo, id)

typedef struct LeverPageDesc {
    uint32_t    size; /* sizeof(LeverPageDesc) */
    uint32_t    reserved0;
    const char* modId;  /* required */
    const char* pageId; /* required, unique within the mod */
    const char* title;  /* default: pageId */
    const char* icon;   /* vanilla icon key ("video", "creator", ...) or NULL */
    uint32_t    surfaces; /* LEVER_SURFACE_* mask; default (field absent): LEVER_SURFACE_GLOBAL_SETTINGS */
    uint32_t    reserved1;
} LeverPageDesc;
#define LEVER_PAGEDESC_V1_MIN_SIZE LEVER_FIELD_END(LeverPageDesc, pageId)

/*
 * The callbacks of one widget. The host copies this struct (min(size, own sizeof)) at registration; `user` is passed
 * back to every callback. `release(user)` (optional) runs when the setting is dropped, exactly once, and only after
 * a successful addSetting. Callbacks run on the game's UI thread and must not throw. Which ones apply depends on
 * the widget kind (see LeverSettingDesc).
 */
typedef struct LeverSettingCallbacks {
    uint32_t size; /* sizeof(LeverSettingCallbacks) */
    uint32_t reserved0;
    void*    user;
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
} LeverSettingCallbacks;
#define LEVER_SETTINGCALLBACKS_V1_MIN_SIZE LEVER_FIELD_END(LeverSettingCallbacks, user)

/*
 * One widget. Which fields apply depends on `kind`:
 *   TOGGLE     getBool / setBool
 *   SLIDER     min, max, step, getNumber / setNumber
 *   DROPDOWN   options[optionCount], getIndex / setIndex
 *   TEXT_FIELD maxLength (default 256), getString / setString
 *   BUTTON     onClick
 *   BANNER     bannerKind; `label` carries the text
 *   HEADER     `label` is the title, `description` the subtitle
 */
typedef struct LeverSettingDesc {
    uint32_t           size; /* sizeof(LeverSettingDesc) */
    uint32_t           kind; /* LeverKind */
    const char*        modId;
    const char*        pageId;
    const char*        id; /* unique within the page */
    const char*        label;
    const char*        description;
    double             min, max, step;
    const char* const* options;
    uint32_t           optionCount;
    uint32_t           bannerKind; /* LeverBannerKind */
    uint32_t           maxLength;  /* 0 = default (256) */
    uint32_t           flags;      /* reserved, 0 */
    const LeverSettingCallbacks* callbacks; /* may be NULL (inert widget) */
} LeverSettingDesc;
#define LEVER_SETTINGDESC_V1_MIN_SIZE LEVER_FIELD_END(LeverSettingDesc, id)

/*
 * lever.registry.v1: how a client copy hands its mods to the host. `size` is the sizeof() of the table as built by
 * the host; test members with LEVER_IFACE_HAS. `version` starts at 1 and grows when members are appended.
 */
typedef struct LeverRegistryV1 {
    uint32_t size;    /* sizeof(LeverRegistryV1) */
    uint32_t version; /* 2 */
    int (*registerMod)(const LeverModInfo* info);
    int (*unregisterMod)(const char* modId); /* drops the mod with all pages and settings (release callbacks run) */
    int (*registerPage)(const LeverPageDesc* page);
    int (*setPageSurfaces)(const char* modId, const char* pageId, uint32_t surfaces);
    int (*addSetting)(const LeverSettingDesc* setting);
    /* version 2: tells the host that values changed behind the UI's back (a keybind, a reset, a config reload), so
     * open widgets re-read their getters. NULL or empty pageId / settingId act as wildcards. Game thread only. */
    int (*notifyChanged)(const char* modId, const char* pageId, const char* settingId);
} LeverRegistryV1;

/* lever.host.v1: what the elected host is and can do. */
typedef struct LeverHostV1 {
    uint32_t size;    /* sizeof(LeverHostV1) */
    uint32_t version; /* 1 */
    uint64_t (*getFeatures)(void); /* LEVER_FEATURE_* mask; grows as hooks get implemented */
    int (*isActivated)(void);      /* 1 once the hooks were installed */
} LeverHostV1;

/* True when interface table `t` (of struct type `type`) contains `member` and the member is set. */
#define LEVER_IFACE_HAS(type, t, member)                                                                         \
    ((t) != NULL && (t)->size >= LEVER_FIELD_END(type, member) && (t)->member != NULL)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LEVER_ABI_V1_H */
