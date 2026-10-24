# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Consumer C++ API (`oresettings/OreSettings.h`) and versioned C ABI (`oresettings/abi.h`).
- Host election over a per-process named file mapping, with buffering until a host exists.
- In-process model of mods, pages and settings, and the host-side table implementation.
- Example client mod (`--example=y`).
- Design notes in `docs/DESIGN.md`. Game hooks are stubs.
