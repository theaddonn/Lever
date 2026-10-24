# Design

Status: pre-alpha. The consumer API, the C ABI, the host election and the in-process model are implemented. The game hooks are stubs (`src/internal/Host.cpp`). Everything below the "Tiers" heading is the plan; items marked **open** are not verified yet.

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

Tier 1 is attempted first; tier 2 covers whatever tier 1 cannot (the extra tabs, info pages, pause menu). The `features` bitmask in the ABI tells mods which surfaces the active host actually supports.

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

1. **Slot.** A named file mapping holds the rendezvous data. Name: `Local\Lever.v<ABI>.p<PID>`. The brief called for `Local\Lever.v1`; the pid suffix was added because `Local\` is scoped to the logon session, not the process, so two game instances would otherwise share raw function pointers. The ABI generation in the name means breaking changes (bump `LEVER_ABI_VERSION`) never meet old copies.
2. **Registration.** On first use each copy appends `{packed version, pointer to its LeverHostTable}` to the slot, under a spin lock stored in the mapping. It hooks nothing.
3. **Election.** At first real use (currently: the first level join; later also the first settings-screen construction) any copy calls `attachOrHost()`. Under the lock the first caller selects the entry with the highest version (earliest registration wins ties) and records it as host, then calls that table's `activateHost()` outside the lock. That function installs the hooks exactly once. Every later call just reads the recorded host.
4. **Forwarding.** All copies keep a local model of their own mod(s) (the callbacks live in the mod's DLL). Once a host is known they replay it, in order, through the host's C table. Before that, registrations are buffered, so API calls are safe at any time after the mod is loaded. The host merges the registrations of every copy into one model.
5. **Unload.** A mod calls `unregisterMod` before its DLL goes away; the host drops the mod and releases the callback cookies. If the host DLL itself unloads while others live, the slot is cleared but other copies keep a stale pointer. **Open:** LeviLamina client mods are only unloaded at shutdown, so this is accepted for now; a refcount or a host-handover protocol would fix it.

## C ABI

`include/lever/abi.h`: a plain struct of function pointers with a `size` field and a `features` mask. Descriptor structs carry `const char*` strings (copied by the callee) and C callbacks with a `void* user` cookie. Rules:

- Appending fields to a struct is not a breaking change: readers only touch members inside `size`.
- A newer mod on an older host (or the reverse) works; missing functionality shows up as a missing feature flag or a table member outside `size` (`LEVER_TABLE_HAS`).
- Changing the meaning or layout of existing fields is breaking: bump `LEVER_ABI_VERSION`, which changes the slot name.

## Source layout

| File | Role |
| --- | --- |
| `include/lever/Lever.h` | C++ consumer API (`lever` namespace) |
| `include/lever/abi.h` | C ABI between copies |
| `src/Api.cpp` | C++ API to local model to host table; trampolines; lazy init |
| `src/internal/Registry.cpp`, `Model.h` | mods, pages and settings model, used locally and by the host |
| `src/internal/Election.cpp` | named file mapping slot, `attachOrHost()` |
| `src/internal/Host.cpp` | host table implementation, hook installation stubs |

## Stubbed

- `installTier1Hooks()` (`TODO(tier1): hook Settings::IRegistry`) and `installTier2Hooks()` (`TODO(tier2): splice hbui bundle`) only log a message.
- The host table advertises `features = 0` until those exist.
- Info pages are part of the ABI contract (mod info is registered) but nothing renders them yet.
- The election is triggered by the first level join only.
