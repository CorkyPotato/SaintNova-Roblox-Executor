# Third-party notices

SaintNova itself is provided as-is. The components below are used or bundled; the
notices here are the ones their licences require.

## Luau 0.747 — MIT

`third_party/luau-0.747` is an unmodified copy of the upstream Luau release,
including the Luau VM, compiler, AST, and analysis libraries. Luau is Copyright
(c) 2019-2024 Roblox Corporation and is distributed under the MIT License; see
`third_party/luau-0.747/LICENSE.txt`.

The Luau VM is derived from Lua 5.x, which is also MIT licensed; see
`third_party/luau-0.747/lua_LICENSE.txt` for that notice. The `Luau::compile`
entry point and the `lua_*` C API surface are used as published.

## nlohmann/json v3.11.3 — MIT

`third_party/nlohmann/nlohmann/json.hpp` is the single-header distribution of
nlohmann/json, SPDX `2013-2023 Niels Lohmann`, MIT licensed. It is used for
protocol payload encoding and decoding. The same header embeds Ryu-derived
floating-point formatting code, Copyright (c) 2009 Florian Loitsch, also MIT
licensed. See the header for both notices.

## Windows CNG (`bcrypt`) — operating system API

The crypt library calls `BCryptOpenAlgorithmProvider`, `BCryptHashData`,
`BCryptEncrypt`, `BCryptDecrypt`, and `BCryptGenRandom` from the Windows CNG API.
No third-party cryptography code is vendored; hashing and AES are provided by
the operating system.

## w64devkit / GCC 16.2 — build toolchain, not distributed

`tools/w64devkit` is a portable toolchain used to build SaintNova on machines
without Visual Studio. SaintNova is *built with* it, not redistributed as part of a
product. GCC and libstdc++ are GPLv3 with the GCC Runtime Library Exception, and
the binaries here are linked with `-static-libgcc -static-libstdc++`, which that
exception permits. mingw-w64 headers and import libraries carry their own
permissive licences. If you redistribute a build produced with this toolchain,
review the GCC runtime exception conditions for your distribution.

## CMake — BSD 3-Clause

CMake is used as a build system and is not bundled.

## Python 3 and tkinter — PSF License

The UI (`ui/saintnova_ui.py`, `ui/saintnova_client.py`) and the test suite run on
CPython's standard library only. Python and Tk are covered by the Python
Software Foundation Licence; neither is bundled here.

## Wave documentation — reference only

Function names and contracts in `docs/FUNCTION-SURFACE.md` are described from
Wave's public documentation at
`https://duckys-playground.gitbook.io/wave`. Saved copies for offline reference
live in `docs/wave/`. That documentation is not redistributed as part of any
binary, and the implementations here are independent.
