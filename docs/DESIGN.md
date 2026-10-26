# Design

Status: pre-alpha. The consumer API, the C ABI, the host election, the in-process model and tier 1 (the native settings pages) are implemented and verified in game. Everything below the "Tiers" heading is the plan; items marked **open** are not verified yet.

## Goal

A build-time static library that Minecraft Bedrock LeviLamina 26.51.6 **client** mods embed to get, inside the vanilla Ore UI:

- a "Mods" tab in the settings screen with one page per mod page and a generated info page per mod (name, version, author, description),
- an entry in the pause menu,
- a tab in the world-creation screen.

It must stay redistributable: nothing is ever written into the game folder.

## How the vanilla settings screen works

The Ore UI settings screen is data-driven from native code:

- `vanilla.menus.settingsGroupQuery(groupId)` lists the `SettingsId`s of a group.
- Typed queries `settingsBooleanQuery`, `settingsNumberQuery`, `settingsOptionQuery`, `settingsStringQuery`, `settingsTextQuery`, `settingsActionQuery`, `settingsBannerQuery` read label, description, value and constraints from `Settings::IRegistry`.
- Writes go through `OreUI::UpdateSettingsCommand` (bool, option, string and number variants).
- The JS side maps page ids to icons (accessibility, video, creator, addon, server, ...).

Relevant SDK headers: `mc/client/gui/oreui/binding/queries/menus/Settings*.h`, `mc/client/gui/oreui/binding/FacetRegistry.h`, `commands/menus/UpdateSettingsCommand.h`.

### Open questions

1. Is the navigation page list (the left-hand list of settings pages) produced natively or assembled in JS? Decides whether tier 1 can add a tab at all.
2. What is the registration API of `Settings::IRegistry` (adding groups and settings after start-up, id allocation, persistence)?
3. Is the pause menu Ore UI at all, or still the legacy screen?
4. How can localisation strings be injected for labels and descriptions (key lookup versus literal text)?

## Tiers

1. **Native only.** Register groups and settings in `Settings::IRegistry`; the vanilla widgets render them with no JS changes. Cheapest and most robust, but limited to what the vanilla queries can express and depends on open questions 1-2.
2. **JS injection.** Hook the load of `data/gui/dist/hbui/index-*.js` and `gameplay-*.js` at runtime and append a plugin script that registers the Mods tab, per-mod pages, info pages, the pause-menu button and the world-creation tab. The script is backed by custom native facets registered through `FacetRegistry`. Because the bundle is altered in memory while it is read, the game folder is never modified and the result stays redistributable.
3. **DDUI compositions.** Mojang's data-driven UI as a possible no-JS route for whole screens. Not evaluated yet.

Tier 1 is attempted first; tier 2 covers whatever tier 1 cannot (the extra tabs, info pages, pause menu). The `features` mask of `lever.host.v1` tells mods which surfaces the active host actually supports.

## Prior art: Dear-OreUI

[Dear-OreUI](https://github.com/copper-lamp/Dear-OreUI) (CC0-1.0, LeviLamina 26.10.x) injects JavaScript into
the live Ore UI views at runtime instead of patching bundle files: it hooks the TechStack / SceneProvider /
Router chain and `OreUI::View::initialize`, waits for `OnReadyForBindings`, runs scripts through
`CoherentHostBridge::sendScript`, and bridges JS calls back to native (`facet:request` ->
`OreUIFacetBridge`). It verified this on the world list page only, states that JsonUI pages (main menu,
in-game screens) are a different stack it does not cover, and does not touch the settings screen.

Tier 2 of this library should port that pipeline (view capture, `sendScript`, the JS<->native bridge, the
vtable fingerprint check before installing a hook, JS console/exception routing to the native logger)
rather than splicing `hbui` bundles on load, re-verified against LeviLamina 26.51.6. The licence allows
copying; keep a credit line in the source files that derive from it. Tier 1 (native settings registry) and
the embedded-library host election stay as designed here; Dear-OreUI is a runtime dependency mod.

## Host election

Several mods embed Lever, each with its own copy of the code and its own statics. Exactly one copy per process must install the hooks.

1. **Slot.** A named file mapping holds the rendezvous data. Name: `Local\Lever.g<ABI generation>.p<PID>`. The pid suffix exists because `Local\` is scoped to the logon session, not the process, so two game instances would otherwise share raw function pointers. The generation in the name means a (never planned) change of the bootstrap struct meets no old copies. The slot layout (`src/internal/Slot.h`) is frozen with the generation.
2. **Registration.** On first use each copy appends `{packed version, pointer to its LeverBootstrap}` to the slot, under a spin lock stored in the mapping. It hooks nothing.
3. **Election.** At first real use (currently: the first level join; later also the first settings-screen construction) any copy calls `attachOrHost()`. Under the lock the first caller selects the entry with the highest version (earliest registration wins ties) and records it as host, then calls that bootstrap's `activateHost()` outside the lock. That function installs the hooks exactly once. Every later call just reads the recorded host.
4. **Forwarding.** All copies keep a local model of their own mod(s) (the callbacks live in the mod's DLL). Once a host is known they replay it, in order, through the host's `lever.registry.v1` interface. Before that, registrations are buffered, so API calls are safe at any time after the mod is loaded. The host merges the registrations of every copy into one model.
5. **Unload.** A mod calls `unregisterMod` before its DLL goes away; the host drops the mod and releases the callback cookies. If the host DLL itself unloads while others live, the slot is cleared but other copies keep a stale pointer. **Open:** LeviLamina client mods are only unloaded at shutdown, so this is accepted for now; a refcount or a host-handover protocol would fix it.

## ABI compatibility

Headers: `include/lever/abi.h` (umbrella, holds the rules as a comment block), `abi/bootstrap.h` (frozen), `abi/v1.h` (interface generation 1). Plain C, no C++ features.

### The election argument

Every mod embeds its own copy; the highest library version wins the election, so **the host is never older than any client**. Only "a newer host understands an older client" ever has to work. There is no requirement that a client copes with an older host, which is what keeps the rules below cheap.

### Shape

1. **Bootstrap (frozen).** `LeverBootstrap { size, abiGeneration, libMajor/Minor/Patch, queryInterface, activateHost, registeredCopies }` is what each copy puts into the election slot. It **never changes shape**; `LEVER_ABI_GENERATION` only moves if this struct itself would have to change (not planned), and then copies of different generations simply never meet because the generation is part of the slot name. All new capability goes through `queryInterface`.
2. **Interfaces.** `bootstrap->queryInterface("lever.registry.v1", minVersion)` returns a table of C function pointers or `NULL`. Served today: `lever.registry.v1` (`registerMod`, `unregisterMod`, `registerPage`, `setPageSurfaces`, `addSetting`) and `lever.host.v1` (`getFeatures`, `isActivated`). Every interface struct starts with `uint32_t size; uint32_t version;`. The name suffix is the breaking generation, `version` the additive revision within it. A query succeeds only if the name matches exactly and `version >= minVersion`.
3. **Descriptors.** `LeverModInfo`, `LeverPageDesc`, `LeverSettingDesc` and `LeverSettingCallbacks` start with `uint32_t size`, the `sizeof` the writing copy knew. The host copies `min(size, sizeof(known))` bytes into a zeroed struct, so the tail an older copy did not have reads as zero / default, and it never reads past `size`. Only a `size` below the v1 minimum (`LEVER_*_V1_MIN_SIZE`: the required prefix such as the ids) is rejected, with `LEVER_E_SIZE`. Defaults for absent optional fields: mod `displayName` = id, page `title` = page id, page `surfaces` = global settings, `maxLength` = 256, no callbacks.
4. **Callbacks** travel in their own sized `LeverSettingCallbacks` struct with the `void* user` cookie; the host copies it at registration and runs `release(user)` exactly once when the setting is dropped (never for a rejected registration).
5. **Strings** are UTF-8 `const char*`, borrowed for the call, copied by the host (tested by mutating the source after registration). Nothing allocated in one copy is freed in another; no `std::` types, no exceptions across the boundary.

### Evolution rules

- Append-only: never remove, reorder, retype or re-interpret a field; new fields go at the end of a struct (descriptor: nothing else needed; interface: bump `version`).
- Enum and flag values are append-only and never reused; readers ignore values they do not know (an unknown widget kind gets `LEVER_E_UNSUPPORTED` and the client drops that widget, an unknown banner kind renders as info, unknown feature and surface bits are ignored).
- Something that cannot be an append becomes a new interface name (`lever.registry.v2`, `abi/v2.h`) served **beside** v1, never replacing it. The host keeps serving `v1` for at least two major releases after `v2` ships (`LEVER_ABI_MIN_SUPPORTED_INTERFACE`).
- A client that does not find an interface it needs logs once and keeps buffering; it never crashes. By construction this does not happen (host >= client).
- The 64-bit layout of v1 is pinned by `tests/unit/AbiLayoutTests.cpp`; changing a published struct fails CI.

### C++ API deprecation convention

The C++ API in `lever/Lever.h` is source-compatible across releases. To retire a call: keep it working, mark it `[[deprecated("use X instead; removed in <next major>")]]` in a minor release, and remove it no earlier than the following major release, i.e. a deprecation lives for at least one minor cycle. The C ABI is never deprecated by this mechanism; its rules are above. Every deprecation gets a CHANGELOG entry under "Deprecated".

### Tests

`tests/` is a separate xmake project built on the host (Linux, clang 22, ASan/UBSan), run in CI as `host-tests`. It compiles the portable core (`src/Api.cpp`, `src/internal/*`) with a stub `Platform` (in-memory election slot, no LeviLamina). Covered: shorter-`size` descriptors from an "older copy" accepted with defaults (exact-size heap blocks so an over-read trips ASan), unknown interface name / too-high `minVersion` returns `NULL`, sizes below the v1 minimum rejected with `LEVER_E_SIZE`, strings copied, election order, buffering and forwarding, and a host without the registry interface.

```
cd tests
xmake f -P . -p linux -a x86_64 -m debug --cc=clang-22 --cxx=clang++-22 --ld=clang++-22 -y
xmake -P . -y && xmake run -P . lever_tests
```

## Source layout

| File | Role |
| --- | --- |
| `include/lever/Lever.h` | C++ consumer API (`lever` namespace) |
| `include/lever/abi.h`, `abi/bootstrap.h`, `abi/v1.h` | C ABI between copies (umbrella, frozen bootstrap, interface generation 1) |
| `src/Api.cpp` | C++ API onto the local model |
| `src/internal/Client.cpp` | local registry, forwarding through `lever.registry.v1`, C trampolines |
| `src/internal/Host.cpp` | `queryInterface` dispatch, v1 interface implementations, bootstrap, hook installation stubs |
| `src/internal/Marshal.h` | sized-descriptor reading |
| `src/internal/Registry.cpp`, `Model.h` | mods, pages and settings model, used locally and by the host |
| `src/internal/Election.cpp`, `Slot.h` | election logic over an `ISlot` |
| `src/internal/Platform.h`, `Log.h` | seams to Windows / LeviLamina (election slot backend, join event, logger) |
| `src/platform/win32/` | the Windows implementations of those seams (named file mapping, LeviLamina events and logger) |
| `tests/` | host unit tests (doctest) |

## Tier 1 status (native settings page)

Implemented in `src/platform/win32/SettingsHook.cpp` + `NativeComponents.cpp` (evidence in
`docs/RE-settings-screen.md` and `docs/RE-components.md`):

- Every copy hooks `Settings::RegistryBuilder::buildDefaultSettingsRegistry` (exported) at init. The game builds its
  settings registry lazily on the first settings access, so the hook runs before any settings screen exists. Inside
  the hook the election runs if it has not yet, and only the elected host injects; the other copies pass through.
- Nav: the `settings-tabs-groups` factory (the list of nav sections) is wrapped to append a "Mods" section
  (`settings-lever-group`), whose factory lists the overview page `lever-mods` and one entry per registered page
  (`lever.<mod>.<page>`, titled with the mod's display name, or "Mod: Page" when a mod has several pages). Fallback
  when the tabs factory is missing: the overview entry is appended to the General section.
- Pages: the overview page has one text row per registered mod; each mod page is built from the host model on
  demand: toggle (BooleanComponent), slider (NumberComponent<float>), dropdown (OptionComponent), text field
  (StringComponent), button (ActionComponent), banner (BannerComponent), header (TextComponent).
- Component elements are built in game-sized raw storage (0x3d0 bytes, index at +0x3c8) from the game's allocator.
  Text/GroupInfo/Option/Action use their exported constructors; Boolean/Number/String/Banner are hand built: base via
  `TextComponent::$ctor`, then the real vtable, type and derived fields. Data providers are our own classes with the
  game's vtable order; they resolve the setting in the host model on every call, so a mod that unregistered is never
  called back. The Boolean vtable is discovered at runtime from a probe element built by the exported `FactoryUtil::addBoolean`;
  if that fails, toggles render as two-entry dropdowns. Descriptions are registered as runtime translations because
  the game drops a description whose I18n lookup returns the input unchanged.
- `getFeatures` reports `LEVER_FEATURE_GLOBAL_SETTINGS | LEVER_FEATURE_INFO_PAGES`.

- Late registrations: a page added after the registry was built gets its factory registered into the live registry
  (`platform::registryChanged`, called by the host after every accepted `registerPage`); the nav section and the
  page rows are rebuilt from the model every time they open, so mods, settings and surfaces need nothing more.
- External value changes: `lever::notifyChanged(mod, page, setting)` (interface `lever.registry.v1` version 2,
  `notifyChanged`) fires the change listener of every live provider with a matching key, which is the same
  publisher the vanilla providers use after a write, so the queries re-read the getters. Older hosts without the
  append are tolerated (the call is dropped).

Not yet: the nav icon is the placeholder (the JS icon table is keyed by vanilla page ids, see `docs/RE-components.md`); labels are literal strings (a localisation key passed
as the label is resolved by the game, so mods can ship keys through a resource pack); `installTier2Hooks()` is still
a stub; the pause-menu and world-creation surfaces are recorded but not rendered.

## In-game check (notifyChanged)

Open the Lever Example page, flip the toggle, move the slider, pick "High" and edit the name, then press "Reset".
Expected: all four widgets snap back to their defaults (on, 0.50, Low, Steve) without leaving the page. The log
shows nothing new for this (the notification goes straight to the game's publisher). If the widgets only update
after reopening the page, the change listener wiring (`wireChangeListener`) or the live-provider set is at fault.

## In-game check (widgets)

Deploy `bin/LeverExample/` (built with `--example=y`) to `mods/LeverExample/`, start the game, open Settings from
the main menu. Expected: a "Mods" section at the bottom of the nav with "Lever Mods" (overview) and "Lever Example"
entries. The example page shows a header, a toggle, a slider (0..1, step 0.05), a dropdown (Low/Medium/High), a
text field, a "Reset" button and a warning banner; changing a value and reopening the page keeps it. Log lines in
`mods/LeverExample/logs/lever.log`: `components: OptionComponent vtable ... distinct, native toggles` (or the
fallback text), `settings: registered group lever-mods and 1 mod page(s); toggles native`,
`settings: factory lever_example.main invoked (7 components)`. A crash on opening the page or on leaving the
settings screen (component destruction) is the thing to report, with the crash file.

## In-game check (first run, done 2026-10-25: passed)

Deploy `bin/LeverExample/` (built with `--example=y`) to `mods/LeverExample/`, start the game, open Settings
from the main menu. Expected: a "Lever Mods" entry at the end of the General section; opening it shows one row
"Lever Example 0.1.0" with the description. Log lines (`mods/LeverExample/logs/lever.log`, also the LeviLamina logger with prefix `[Lever]`), in order:
`settings: hook on Settings::RegistryBuilder::buildDefaultSettingsRegistry installed` (at mod enable),
`settings: buildDefaultSettingsRegistry hit`, `this copy (Lever 0.1.0) is the host`,
`settings: wrapped settings-general-group (orig factory ok)`, `settings: registered group lever-mods`,
`settings: factory lever-mods invoked (1 components)` (when the page opens). If the entry shows a raw key or
nothing, or the game crashes on opening Settings, send the log and the crash file.
