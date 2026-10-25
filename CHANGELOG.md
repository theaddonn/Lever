# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Consumer C++ API (`lever/Lever.h`).
- Long-term-compatible C ABI: frozen bootstrap table (`lever/abi/bootstrap.h`) in the election slot, versioned interfaces `lever.registry.v1` and `lever.host.v1` fetched through `queryInterface` (`lever/abi/v1.h`), size-prefixed descriptors that a newer host accepts from older copies, umbrella `lever/abi.h` with the compatibility rules.
- Host (Linux) unit tests in `tests/` and a `host-tests` CI job.
- C++ API deprecation convention in `docs/DESIGN.md`.
- Host election (slot name `Local\Lever.g<generation>.p<pid>`) over a per-process named file mapping, with the Win32 and LeviLamina parts behind `internal/Platform.h`; registrations are buffered until a host exists.
- In-process model of mods, pages and settings, and the host-side table implementation.
- Example client mod (`--example=y`).
- Design notes in `docs/DESIGN.md`. Game hooks are stubs.
