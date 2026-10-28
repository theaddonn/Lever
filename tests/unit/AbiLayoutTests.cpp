// Pins the 64-bit layout of the frozen v1 ABI. If one of these fails, a published struct was changed.

#include <doctest.h>

#include "lever/abi.h"

#include <cstddef>

#define OFF(T, m) offsetof(T, m)

#if INTPTR_MAX == INT64_MAX

static_assert(sizeof(LeverBootstrap) == 48);
static_assert(OFF(LeverBootstrap, size) == 0 && OFF(LeverBootstrap, abiGeneration) == 4);
static_assert(OFF(LeverBootstrap, libMajor) == 8 && OFF(LeverBootstrap, libMinor) == 12 && OFF(LeverBootstrap, libPatch) == 16);
static_assert(OFF(LeverBootstrap, queryInterface) == 24 && OFF(LeverBootstrap, activateHost) == 32);
static_assert(OFF(LeverBootstrap, registeredCopies) == 40);

static_assert(sizeof(LeverModInfo) == 56);
static_assert(OFF(LeverModInfo, id) == 8 && OFF(LeverModInfo, displayName) == 16 && OFF(LeverModInfo, author) == 24);
static_assert(OFF(LeverModInfo, description) == 32 && OFF(LeverModInfo, versionMajor) == 40);
static_assert(LEVER_MODINFO_V1_MIN_SIZE == 16);

static_assert(sizeof(LeverPageDesc) == 48);
static_assert(OFF(LeverPageDesc, modId) == 8 && OFF(LeverPageDesc, pageId) == 16 && OFF(LeverPageDesc, title) == 24);
static_assert(OFF(LeverPageDesc, icon) == 32 && OFF(LeverPageDesc, surfaces) == 40);
static_assert(LEVER_PAGEDESC_V1_MIN_SIZE == 24);

static_assert(sizeof(LeverSettingCallbacks) == 104);
static_assert(OFF(LeverSettingCallbacks, user) == 8 && OFF(LeverSettingCallbacks, getBool) == 16);
static_assert(OFF(LeverSettingCallbacks, onClick) == 80 && OFF(LeverSettingCallbacks, release) == 88);
static_assert(OFF(LeverSettingCallbacks, getState) == 96); // 0.2.0 append
static_assert(LEVER_SETTINGCALLBACKS_V1_MIN_SIZE == 16);

static_assert(sizeof(LeverSettingDesc) == 104);
static_assert(OFF(LeverSettingDesc, kind) == 4 && OFF(LeverSettingDesc, modId) == 8 && OFF(LeverSettingDesc, id) == 24);
static_assert(OFF(LeverSettingDesc, label) == 32 && OFF(LeverSettingDesc, min) == 48 && OFF(LeverSettingDesc, step) == 64);
static_assert(OFF(LeverSettingDesc, options) == 72 && OFF(LeverSettingDesc, optionCount) == 80);
static_assert(OFF(LeverSettingDesc, bannerKind) == 84 && OFF(LeverSettingDesc, maxLength) == 88);
static_assert(OFF(LeverSettingDesc, callbacks) == 96);
static_assert(LEVER_SETTINGDESC_V1_MIN_SIZE == 32);

static_assert(sizeof(LeverRegistryV1) == 64);
static_assert(OFF(LeverRegistryV1, version) == 4 && OFF(LeverRegistryV1, registerMod) == 8);
static_assert(OFF(LeverRegistryV1, unregisterMod) == 16 && OFF(LeverRegistryV1, registerPage) == 24);
static_assert(OFF(LeverRegistryV1, setPageSurfaces) == 32 && OFF(LeverRegistryV1, addSetting) == 40);
static_assert(OFF(LeverRegistryV1, notifyChanged) == 48); // version 2 append
static_assert(OFF(LeverRegistryV1, updateSetting) == 56); // version 3 append

static_assert(sizeof(LeverHostV1) == 24);
static_assert(OFF(LeverHostV1, getFeatures) == 8 && OFF(LeverHostV1, isActivated) == 16);

#endif

TEST_CASE("abi: generation and constants are frozen") {
    CHECK(LEVER_ABI_GENERATION == 1);
    CHECK(LEVER_ABI_MIN_SUPPORTED_INTERFACE == 1);
    CHECK(LEVER_KIND_HEADER == 6);
    CHECK(LEVER_SURFACE_WORLD_CREATION == 4u);
    CHECK(LEVER_E_SIZE == 5);
    CHECK(LEVER_STATE_HIDDEN == 2);
    CHECK(LEVER_SETTING_FLAG_HIDE_STEPS == 2u);
}
