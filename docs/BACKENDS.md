# Backends

SaintNova is built around one seam: `IExecutionBackend`. The protocol server,
CLI, UI, job lifecycle, and logging do not know what environment a script runs
in. The v0.1.0 release ships two backends:

| id | Target | Status |
| --- | --- | --- |
| `harness` | In-process Luau 0.747 VM with a Roblox-shaped API | Working |
| `roblox` | Roblox Studio play session | Working |

A Roblox Player backend is **not shipped**. This document defines the contract
so one can be built without changing the rest of the stack.

## The contract

`include/saintnova/backend.hpp`:

```cpp
class IExecutionBackend {
public:
    virtual ~IExecutionBackend() = default;

    virtual BackendInfo info() const = 0;
    virtual ExecutionResult execute(const ExecutionRequest& request,
                                    const OutputSink& sink,
                                    std::atomic<bool>* cancel) = 0;
    virtual void cancel() = 0;
    virtual void reset() {}
};
```

Rules:

- `info()` is called often; keep it cheap. `available` must be honest. If the
  target is not attached, return `false` and put the reason in `detail`. The
  core refuses to dispatch to an unavailable backend.
- `execute()` runs one request to completion. Emit each line through `sink` as
  it is produced, and append it to `result.output` as well, so a client that
  reconnects late still sees the full transcript.
- `cancel` is an atomic flag. Poll it at safe points. A cancelled run returns
  `ok = false` with error `"cancelled"`.
- `reset()` drops persistent state. It is optional.
- `BackendInfo::id` is the string used by `--backend`, the UI, and the protocol.
  Registering the same id twice replaces the earlier backend.
- `BackendKind` is descriptive. A Player backend can report
  `BackendKind::Roblox` and use its own `id`; add an enum value only if you need
  a new family in logs.

`ExecutionRequest` carries `id`, `script`, `chunk_name`, and `timeout_ms`.
`ExecutionResult` carries `id`, `ok`, `duration_us`, `error`, and `output`.

## Registering a backend

`make_default_core()` in `src/core.cpp` is the only place that knows the
concrete backends:

```cpp
core->register_backend(std::make_unique<HarnessBackend>());
core->register_backend(std::make_unique<RobloxBackend>());
```

Add your backend there. The first registered backend becomes the default. No
protocol, UI, or CLI change is needed: `saintnova backends` lists it and
`saintnova run --backend <id>` dispatches to it.

## The reference live-client backend

`RobloxBackend` targets `RobloxStudioBeta.exe`. Its transport is:

1. Find the Studio process and the loader's Lua globals (`SaintNovaGo`,
   `SaintNovaState`, `SaintNovaResult`, `SaintNovaLog`).
2. Find the `ReplicatedStorage.SaintNovaTemplate` Source buffer, which begins
   with the `--SAINTNOVA` marker.
3. Wrap the user script in `return function() ... end`, write it into the
   buffer padded to the template length, and set the go flag.
4. The in-place loader clones the template and requires the clone, so the engine
   compiles the Source inside the play session. It captures `print`/`warn` and
   writes state, result, and log back to the globals.
5. The backend reads those values and streams them as `OutputLine`s.

The loader is `harness/client_loader.luau`; the place is
`place/saintnova_client.rbxlx`. The backend reports `available = false` with a
specific reason when Studio or the loader is missing; it never guesses.

## A Player backend

Not shipped. The original workspace explored a Player transport, but it did not
reach a stable release, and none of that code is in this repository. The blocker
is the client's integrity checks; the approaches that were tried did not produce
a transport that could be shipped.

What is ready is everything above the seam: named-pipe protocol, job lifecycle,
timeouts, cancellation, UI, CLI, and output streaming. A Player backend only has
to implement `IExecutionBackend` and register itself. The Studio backend is the
reference for the shape of a live-client transport.

The public status and build plan live in
[SaintNova-Player-Backend](https://github.com/CorkyPotato/SaintNova-Player-Backend).

If you build one, keep the availability rule: `available = false` with a
specific `detail` is better than a backend that pretends and fails later.

## Testing

```powershell
saintnova.exe backends
saintnova.exe run examples\basics.luau --backend harness
saintnova.exe run examples\basics.luau --backend roblox
python -m unittest discover -s tests -p "test_*.py"
```

The Python suite covers framing, pipe lifecycle, cancel, timeout, and CLI exit
codes. Luau conformance scripts live in `tests/*.luau`.
