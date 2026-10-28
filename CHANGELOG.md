# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-10-28

One settings page per mod, dependent rows and a higher-level consumer API. The C++ API changed (see Changed); the
C ABI only grew.

### Added

- Dependent rows: `Widget::visibleWhen`, `enabledWhen` and `state` hide, grey out or control a row from a condition;
  conditions are re-evaluated after every edit made on the mod's page and on `notifyChanged()`. ABI: `getState`
  appended to `LeverSettingCallbacks` (descriptor growth, no interface version bump) and `LeverState`.
- `onChange` hooks on `Widget`, `Section` and `Mod`, run after a write made through the screen (the place to persist).
- Rows bound to variables by reference (`toggle(id, label, bool&)`, `slider(id, label, T&, min, max, step)`,
  `dropdown(id, label, T&, options)` for integers and enums, `textField(id, label, std::string&)`), next to the
  getter/setter forms.
- `Widget::label`, `description` and `showSteps` may be called after registration; changes to an already forwarded
  row go through the new `updateSetting` (`lever.registry.v1` version 3). Older hosts keep the registered values.
- `LEVER_SETTING_FLAG_SHOW_STEPS` / `HIDE_STEPS` in `LeverSettingDesc::flags`.
- Host tests for states, hooks, modifiers, variable bindings and the slider grid.

### Changed

- One nav entry per mod: a mod's pages are now sections of its single settings page (titled groups; a mod with one
  section shows its rows directly). The ABI keeps calling them pages.
- C++ API: `registerMod` returns a `Mod&` handle; `Mod::section()` / `addSection()` replace `addPage()`; the row
  methods (`toggle`, `slider`, `dropdown`, `textField`, `button`, `header`, `banner`) replace the `add*` family,
  take no description argument and return a `Widget&` for chained modifiers (`description(...)` is one of them).
  `Page` is now `Section`.
- Sliders draw their step dividers only when they have at most 10 positions (many ticks made the track look darker);
  internally every slider runs in index units with the real value printed by the formatter, which also fixes the
  knob position for any step.

## [0.1.0] - 2026-10-28

First alpha: the native settings pages (tier 1) work in Minecraft Bedrock 1.26.51 with LeviLamina 26.51.6.

### Added

- Consumer C++ API (`lever/Lever.h`).
- Long-term-compatible C ABI: frozen bootstrap table (`lever/abi/bootstrap.h`) in the election slot, versioned interfaces `lever.registry.v1` and `lever.host.v1` fetched through `queryInterface` (`lever/abi/v1.h`), size-prefixed descriptors that a newer host accepts from older copies, umbrella `lever/abi.h` with the compatibility rules.
- Host (Linux) unit tests in `tests/` and a `host-tests` CI job.
- C++ API deprecation convention in `docs/DESIGN.md`.
- Host election (slot name `Local\Lever.g<generation>.p<pid>`) over a per-process named file mapping, with the Win32 and LeviLamina parts behind `internal/Platform.h`; registrations are buffered until a host exists.
- In-process model of mods, pages and settings, and the host-side table implementation.
- Example client mod (`--example=y`).
- Design notes in `docs/DESIGN.md`.
- Tier 1: `Settings::RegistryBuilder::buildDefaultSettingsRegistry` is hooked; the host copy adds a "Mods" nav section with an overview page and one page per registered mod page, rendering toggles, sliders, dropdowns, text fields, buttons, banners and headers with the vanilla widgets.
- `manifest.json` template for the example mod packer.
- `lever::notifyChanged(mod, page, setting)` / `Page::notifyChanged()`: open widgets re-read their getters after a value changed outside the UI. Appended to `lever.registry.v1` as version 2 (`notifyChanged`); older hosts are tolerated.
- Pages registered after the game built its settings registry are added to the live registry, so a late mod still gets its page without a game restart.

### Fixed

- Text fields: keystrokes are buffered in the provider and the mod's setter runs once when the field loses focus (the game sends `updateString` per key and `commitString` on blur), instead of once per keystroke.

- Sliders: the value label is formatted to the step's decimals (the number formatter returns `std::optional<std::string>`), and the float32 step is nudged so the Ore UI slider keeps its last position and the knob rests on the value.
