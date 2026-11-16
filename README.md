# SaintNova Executor

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/CorkyPotato/SaintNova-Executor)](https://github.com/CorkyPotato/SaintNova-Executor/releases)

A Luau execution framework for **Roblox Studio** with a replaceable backend, a
local harness, a named-pipe protocol, and a tkinter UI.

SaintNova runs Lua in a Roblox Studio play session through its own backend. It
compiles scripts with the engine by rewriting a prepared ModuleScript's
`Source`, executes them in the live client, and streams output, errors, and
yields back to the CLI or UI. It does not target the Roblox Player.

## Download

Prebuilt Windows x64 release: [v0.1.0](https://github.com/CorkyPotato/SaintNova-Executor/releases/tag/v0.1.0).
The zip includes the executable, Studio place, UI, docs, and quickstart. Build
from source below if you prefer.

## Status

| Piece | Status |
| --- | --- |
| C++ core with Luau 0.747 and a Win32 named-pipe server | Working |
| Local harness backend (instances, signals, filesystem, clipboard, crypt, debug) | Working |
| Named-pipe protocol with log streaming, cancel, timeout, reset | Working |
| tkinter UI (run, cancel, reset, backends, log pane) | Working |
| CLI (`serve`, `run`, `compile`, `repl`, `backends`, `version`) | Working |
| Embeddable DLL with a C surface | Working |
| Roblox Studio backend | Working in a play session: engine-compiled ModuleScript transport, output streaming, errors, yields |
| Roblox Player backend | Not supported |

Unsupported functions are not silent no-ops. Each one raises with a reason; see
[`docs/FUNCTION-SURFACE.md`](docs/FUNCTION-SURFACE.md).

## Build

Requirements:

- CMake 3.20 or newer
- Ninja
- A C++20 compiler. The bundled script uses `gcc`/`g++` from `PATH`; MinGW-w64
  (GCC 13 or newer) is the tested toolchain.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

Outputs:

- `build\bin\saintnova.exe`
- `build\bin\libsaintnova.dll` (MinGW; `saintnova.dll` with MSVC)

With Visual Studio, configure CMake manually and use the generated configuration
path:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Studio quickstart

1. Open `place\saintnova_client.rbxlx` in Roblox Studio and press **Play**.
   Wait for `SAINTNOVA-READY` in the Output window.
2. Start the server:

   ```powershell
   build\bin\saintnova.exe serve
   ```

3. Start the UI:

   ```powershell
   python ui\saintnova_ui.py
   ```

   Choose the `roblox` backend and run a script.

From the CLI:

```powershell
build\bin\saintnova.exe run examples\basics.luau --backend roblox
```

The Studio place contains `StarterPlayerScripts.SaintNovaLoader` and
`ReplicatedStorage.SaintNovaTemplate`. The backend rewrites the template's
`Source` in the Studio process; the engine compiles it inside the play session.

## The place

`place\saintnova_client.rbxlx` is a ready-to-open Studio place with the loader
and template installed. The loader source is also in
`harness\client_loader.luau`.

## Documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — process structure, backends, threading.
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — named-pipe framing and messages.
- [`docs/FUNCTION-SURFACE.md`](docs/FUNCTION-SURFACE.md) — implemented and refusing functions.
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) — third-party licences.

## Provenance

See [`NOTICE.md`](NOTICE.md) for provenance and source attribution.

## License

MIT licensed; see [`LICENSE`](LICENSE). Third-party components keep their own
licenses; see `THIRD_PARTY_NOTICES.md`.
