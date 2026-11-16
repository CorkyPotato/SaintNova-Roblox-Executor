# Architecture

SaintNova is an execution framework with a replaceable backend. The working
backends are the local harness (a Luau VM with a Roblox-shaped API) and the
Roblox Studio backend, which executes scripts in a live Studio play session.

```
  hosts                     core                            backends
  -----                     ----                            --------
  saintnova.exe  ---------> SaintNovaServer ----> ExecutorCore ----> HarnessBackend ---> Luau 0.747 VM
                ---------->                    |                 `--> RobloxBackend ---> Studio play session
  libsaintnova.dll ---      |                 |
                          +-> named pipe \\.\pipe\saintnova-*
                                ^
                                |
                        ui/saintnova_client.py <-> ui/saintnova_ui.py
```

Nothing above `IExecutionBackend` knows whether a script runs in a VM on this
machine or inside a game process. That seam is the point of the project.

## Source map

| Path | Responsibility |
| --- | --- |
| `include/saintnova/backend.hpp` | `IExecutionBackend` — the replaceable seam. |
| `include/saintnova/core.hpp`, `src/core.cpp` | Backend registry, default selection, dispatch, `reset`/`cancel` fan-out. |
| `include/saintnova/protocol.hpp`, `src/protocol.cpp` | Frame encode/decode, payload cap, message names. |
| `include/saintnova/pipe_server.hpp`, `src/pipe_server.cpp` | Win32 named pipe listener, per-client reader threads, overlapped I/O. |
| `include/saintnova/server_app.hpp`, `src/server_app.cpp` | Wire ⇄ core translation, JSON payloads, per-client job lifecycle. |
| `include/saintnova/luau_vm.hpp`, `src/luau_vm.cpp` | Luau state, scheduler, timeout/cancel, prelude loading, output capture. |
| `include/saintnova/harness_api.hpp`, `src/harness_api.cpp` | Native functions: filesystem, clipboard, identity, HWID, metatables, closures, message box. |
| `include/saintnova/crypt_api.hpp`, `src/crypt_api.cpp` | Native crypt library: base64, SHA-384, CSPRNG, key derivation, AES-256-CBC (Windows CNG). |
| `harness/sandbox.luau` | The Roblox-shaped prelude written in Luau (instances, Vector3/CFrame, signals, Enum, Wave surface). |
| `include/saintnova/harness_backend.hpp`, `src/harness_backend.cpp` | Adapts `LuauVm` to `IExecutionBackend`. |
| `include/saintnova/roblox_backend.hpp`, `src/roblox_backend.cpp` | Studio backend: attaches to a Studio play session and executes through the engine-compiled ModuleScript transport. |
| `src/main.cpp` | CLI: `serve`, `run`, `compile`, `backends`, `repl`, `version`. |
| `src/dllmain.cpp` | `saintnova_version`, `saintnova_last_error`, `saintnova_start`, `saintnova_stop`. |
| `ui/` | Reference client and tkinter UI. |
| `tests/` | Protocol/pipe/CLI tests and Luau conformance scripts. |

## Threading model

Four kinds of thread, with one rule between them: only the job thread touches
the VM.

| Thread | Count | Does |
| --- | --- | --- |
| Accept | 1 | Creates pipe instances and waits for connections. |
| Reader | 1 per client | Reads frames, dispatches them, never executes. |
| Job | 1 per client at a time | Runs the script and streams `Log` frames. |
| UI/main | external | Drains an event queue, sends commands. |

Executions from *different* clients are serialised by `execute_mutex_` in
`SaintNovaServer`, because the harness backend owns a single Luau state and Luau
states are not re-entrant. Cancellation still works during a wait because it
only reads an atomic that the interrupt hook polls.

Within one client, `try_begin_job` enforces one job at a time; a second
`Execute` is answered with `busy` rather than being queued, so a client always
knows which request an `id` refers to.

### Why the pipe uses overlapped I/O

Both ends open pipe handles with `FILE_FLAG_OVERLAPPED`. That is not tuning; it
is required for correctness.

Windows serialises I/O on a file object opened for synchronous I/O. With a
synchronous handle, a reader thread parked in `ReadFile` prevents any
`WriteFile` on the same handle from starting. The failure mode is total: the
client sends `Hello`, the server reads it and writes `HelloAck` *from the reader
thread* (fine), then the client's reader blocks in `ReadFile` while the main
thread tries to send `Execute` — and the write never returns. The same happens
server-side as soon as the job thread emits a log line while the reader is
waiting for the next frame.

This was observed, not theorised: a `.NET` `NamedPipeClientStream` client worked
(a single read, then a single write, from one thread), while Python's
`open(path, "r+b")` and a raw `os.write` both hung. `PipeConnection` was rewritten
to use `CreateFileW` with `FILE_FLAG_OVERLAPPED` plus `ReadFile`/`WriteFile`
against an `OVERLAPPED` with a manual-reset event, waited on immediately. Each
direction has at most one operation outstanding, so one event per call is
enough.

Closing needs the same care: `CancelIoEx` aborts the pending read, the reader
thread is joined, and only then is the handle closed. A pending overlapped read
holds a pointer to its buffer; closing first would let the kernel write into
freed memory.

## Execution lifecycle

1. Client sends `Execute{id, script, backend, timeout_ms, chunk}`.
2. `SaintNovaServer` validates the payload, builds an `ExecutionRequest`, claims the
   connection's job slot, and spawns the job thread.
3. The job thread takes `execute_mutex_` and calls `ExecutorCore::execute`.
4. `ExecutorCore` resolves the backend (empty id means default) and calls
   `IExecutionBackend::execute` with an `OutputSink` and the connection's cancel
   flag.
5. The harness backend compiles the script to Luau bytecode
   (`optimizationLevel = 1`, `debugLevel = 1` so errors keep file and line),
   loads it into a fresh coroutine, and resumes it.
6. `print`/`warn`/`rconsole*` call `__saintnova.emit`, which appends to the result's
   output and invokes the sink, which serialises a `Log` frame.
7. When the main coroutine finishes, the scheduler keeps running pending
   `task.spawn`/`delay` threads until the queue is empty, the deadline passes, or
   a cancel arrives.
8. The result becomes `ExecuteResult{id, ok, duration_ms, error, output}`.

## The VM

- Luau 0.747, vendored and pinned, compiled from source into `saintnova_core`.
- `luaL_openlibs`, then `_G` aliased to the globals table, then `shared`, then
  the native harness functions, then the crypt library, then the prelude.
- The prelude is embedded at **configure time** by `configure_file`, so editing
  `harness/sandbox.luau` requires re-running CMake, not just the build.
- Scripts compile at optimization level 1 and **debug level 2**, so errors carry
  `file:line` and `debug.getupvalue`/`getinfo` can report local and upvalue
  names.
- Formatted output: `print` joins its arguments with tabs, matching Luau's CLI,
  so `print("a", 1)` reads as `a	1` in the log.

### Instances, aliases, and the cache

Instances are tables with a shared metatable. `cloneref` produces an *alias*: an
empty table carrying the same metatable plus a weak reference to the canonical
instance. Every internal helper (`propsOf`, `classOf`, `childList`, `detach`)
resolves through that reference, so an alias is fully usable while keeping a
distinct identity — `alias == original` is false, which is the whole point of
`cloneref`, and `compareinstances` looks through it.

The registry maps slot ids to canonical instances. `cache.invalidate` marks a
slot hidden, and `cache.replace` repoints one slot at another instance; in both
cases the query functions (`getinstances`, `getscripts`, …) reflect the change
while references the script already holds keep working. Aliases never enter the
registry.

### Scheduler

`task.wait` yields a number: the delay in seconds. `task.spawn`, `task.defer`,
`task.delay`, and `task.cancel` create or cancel coroutine threads. The VM
records every created thread in the registry with `lua_ref`, because a `lua_State*`
is not a GC root by itself; without anchoring, a suspended coroutine can be
collected while the scheduler still holds its pointer. `finish_thread` unrefs on
completion.

`pump()` runs after the main coroutine returns and resumes due threads in
deadline order. A thread that errors is reported through `warn` (source `task`)
and dropped; it cannot take down the run.

### Timeout and cancellation

`lua_callbacks(L)->interrupt` is installed. Luau calls it at instruction
boundaries with `gc < 0`; GC safepoints (`gc >= 0`) are ignored. The hook checks
the external cancel flag (set by a `Cancel` frame) and the wall-clock deadline,
then raises a Luau error. That is why `while true do end` aborts within the
configured timeout, with no cooperation from the script.

## Sandbox boundaries

| Capability | Boundary |
| --- | --- |
| Filesystem | Rooted at `%LOCALAPPDATA%\SaintNova\workspace` (or `SAINTNOVA_WORKSPACE`). Absolute paths, drive names, and `..` are rejected. 8 MiB per file, 10 000 listing entries. |
| Clipboard | Win32, Unicode, 4 MiB cap, retried briefly when another process holds it. |
| Network | None. `httpget`, `request`, `syn.request`, `WebSocket.connect` raise. |
| Input | None. Key/mouse functions raise; the operator's desktop is never synthesised into. |
| Process | None. The harness does not open, read, or inject into another process. |
| UI | `messagebox` is a real Win32 dialog and blocks the VM until dismissed. |

These are deliberate. A sandbox that silently pretends to have a capability is
worse than one that refuses, because the failure surfaces later, somewhere else.

## Hosts

### CLI

```
saintnova serve [--pipe NAME]         host the pipe server
saintnova run <file> [--timeout MS] [--backend ID]
saintnova compile <file> [-o OUT]     Luau bytecode
saintnova backends                    list backends and availability
saintnova repl                        interactive session
saintnova version
```

`run` exits 0 on success and 1 on a compile error, runtime error, timeout, or
unavailable backend, with the reason on stderr.

### DLL

`libsaintnova.dll` exposes a C surface for hosts that want the server embedded:

```c
const char* saintnova_version(void);
const char* saintnova_last_error(void);
int  saintnova_start(const char* pipe_name);  // 0 ok, 1 failed, 2 already running
void saintnova_stop(void);
```

It is the same `SaintNovaServer` the CLI uses, so the two cannot drift.

## Known limits

- The Roblox backend targets Roblox Studio (`RobloxStudioBeta.exe`) only. It
  does not attach to the Player and contains no client-integrity bypass. A
  Player backend is an extension point, not a shipped feature; see
  `docs/BACKENDS.md`.
- One VM per harness backend: scripts share globals until `reset`.
- `hookfunction` cannot work: Luau closures are immutable from Luau. The
  function exists and raises.
- `RunService` signals are registered but never fire; there is no frame loop.
- Physics, rendering, replication, and asset loading are absent by design.
