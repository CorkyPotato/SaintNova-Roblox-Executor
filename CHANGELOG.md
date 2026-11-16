# Changelog

All notable changes to SaintNova Executor are recorded here. Releases use
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed

- Renamed the repository to `SaintNova-Roblox-Executor`; the product name stays
  SaintNova Executor.
- Added `docs/BACKENDS.md`: the `IExecutionBackend` contract, the registration
  point, the Studio reference transport, and the status of a Player backend.

## [0.1.0] - 2026-11-16

First public release. Windows x64, Roblox Studio only.

### Added

- C++20 core with Luau 0.747, exposed through `saintnova.exe` and `libsaintnova.dll`.
- Win32 named-pipe server speaking a JSON protocol with streaming logs, cancel,
  timeout, and reset.
- Local `harness` backend: instances, signals, vectors, scheduler, filesystem,
  clipboard, crypt, and debug helpers with no game process involved.
- `roblox` backend for Roblox Studio play sessions: engine-compiled ModuleScript
  transport, output and error streaming, yields, and cancellation.
- Ready-to-open Studio place with `SaintNovaLoader` and `SaintNovaTemplate`.
- tkinter UI and CLI (`serve`, `run`, `compile`, `repl`, `backends`, `version`).
- Documentation: architecture, protocol, function surface, third-party notices.
- Test suites: Luau API smoke, wave surface, libraries, Python protocol/CLI tests,
  and a UI import check.

### Notes

- The Roblox Player is not supported. This release targets Studio only.
- Functions that are not implemented raise with a reason instead of silently
  doing nothing.

[0.1.0]: https://github.com/CorkyPotato/SaintNova-Roblox-Executor/releases/tag/v0.1.0
