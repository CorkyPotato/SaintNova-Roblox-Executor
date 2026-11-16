# Pipe protocol

SaintNova's UI never touches the executor directly. It speaks a small framed
protocol over a Windows named pipe, so the same core serves the tkinter client,
a test script, a C# host, or anything else that can open a handle.

## Transport

- Named pipe, `\\.\pipe\<name>`, default `saintnova-default`.
- Byte mode, duplex, message boundaries are not preserved by the transport.
- Both sides open the handle with `FILE_FLAG_OVERLAPPED`. This is a correctness
  requirement, not an optimisation: Windows serialises all I/O on a synchronous
  file object, so a reader blocked in `ReadFile` would prevent the executor from
  writing a log line on the same connection. See `ARCHITECTURE.md`.
- Multiple clients may connect at once. The server accepts each on its own
  thread. Executions are serialised across clients because the harness VM is not
  re-entrant; a second client blocks until the first finishes (`busy` applies
  per connection, not globally).

## Framing

Every message is an 8-byte little-endian header followed by a UTF-8 JSON object:

| Offset | Size | Type | Field |
| --- | --- | --- | --- |
| 0 | 4 | `uint32` | payload length in bytes (header excluded) |
| 4 | 2 | `uint16` | message type |
| 6 | 2 | `uint16` | flags, reserved, must be 0 |
| 8 | *len* | bytes | JSON object, may be empty |

Limits: payloads above 16 MiB (`kMaxPayload`) are a protocol error; the peer is
disconnected rather than trusted with the allocation. An empty payload is
equivalent to `{}`.

A `Hello` on the wire is exactly:

```
02 00 00 00  01 00  00 00  7B 7D
```

## Message types

| Value | Name | Direction | Payload |
| --- | --- | --- | --- |
| `0x0001` | `Hello` | client → server | `{}` |
| `0x0002` | `HelloAck` | server → client | see below |
| `0x0010` | `Execute` | client → server | see below |
| `0x0011` | `ExecuteResult` | server → client | see below |
| `0x0012` | `Cancel` | client → server | `{"backend": "harness"}` (optional) |
| `0x0020` | `Log` | server → client | see below |
| `0x0030` | `ListBackends` | client → server | `{}` |
| `0x0031` | `Backends` | server → client | `{"backends": [BackendInfo]}` |
| `0x0032` | `Reset` | client → server | `{"backend": "harness"}` (optional) |
| `0x0040` | `Ping` | client → server | `{}` |
| `0x0041` | `Pong` | server → client | `{"t": 1699999999999}` |
| `0x0050` | `Shutdown` | client → server | `{}` |
| `0x00FF` | `Error` | server → client | `{"code": "...", "message": "...", "id": "..."}` |

### `HelloAck`

```json
{
  "protocol": 1,
  "product": "SaintNova",
  "version": "0.1.0",
  "pid": 12345,
  "pipe": "saintnova-default",
  "backends": [
    {
      "id": "harness",
      "name": "Local Luau harness",
      "kind": "harness",
      "available": true,
      "detail": "In-process Luau 0.747 VM with a Roblox-shaped API subset. No game process is touched."
    }
  ]
}
```

`pid` identifies the server process, which matters when the UI started it
itself. `backends` is the same array `ListBackends` returns.

### `Execute`

```json
{
  "id": "b1f2c3",
  "script": "print('hi')",
  "backend": "harness",
  "timeout_ms": 5000,
  "chunk": "=saintnova.ui"
}
```

`id` is client-chosen and echoed on every `Log` and on the result. `script` is
required and must be a string. `backend` selects a registered backend; empty
means the default (the first registered). `timeout_ms` of `0` or absent means
5000 ms. `chunk` becomes the chunk name in error messages and `debug.info`; a
leading `=` makes Luau display it verbatim.

### `Log`

```json
{"id": "b1f2c3", "level": "info", "source": "print", "text": "hi"}
```

`level` is `trace`, `debug`, `info`, `warn`, or `error`. `source` is `print`,
`warn`, `console`, `console.clear`, `console.title`, `runtime`, `task`,
`backend`, or `system`; the UI uses it for tags and for the clear/retitle
pseudo-messages. Every log line for a request arrives before that request's
`ExecuteResult`.

### `ExecuteResult`

```json
{
  "id": "b1f2c3",
  "ok": true,
  "duration_ms": 0.42,
  "error": "",
  "output": [{"id": "b1f2c3", "level": "info", "source": "print", "text": "hi"}]
}
```

`ok` is false for a compile error, a runtime error, a timeout, a cancellation,
or an unavailable backend, with the reason in `error`. `output` repeats every
line already streamed, so a client that joined late or dropped a frame can
reconstruct the run. Entries have the same shape as `Log` payloads.

## Errors

| Code | Meaning |
| --- | --- |
| `bad_payload` | The body was not valid JSON, or was not a JSON object. |
| `bad_request` | A required field was missing or the wrong type. |
| `busy` | This connection already has a job running. |
| `unknown_message` | The message type is not recognised. |

Errors carry the request `id` when one was known, so a client can fail the right
job. An error does not close the connection.

## Ordering and cancellation

- A connection runs one job at a time. A second `Execute` on the same connection
  is rejected with `busy` and does not disturb the running job.
- `Cancel` sets a flag polled by the VM. The VM checks it at every instruction
  boundary, so cancellation is prompt even inside `while true do end`. The
  result is `ok: false` with `error` containing `cancelled`.
- A `Cancel` that arrives while the connection is idle is discarded when the
  next job begins; it cannot poison a future run.
- `Reset` recreates the backend's VM. Pending tasks, globals, `shared`, and
  module caches are lost. The reply is a `Backends` frame.
- `Shutdown` replies `Pong` with `{"ok": true}` and asks the host process to
  exit. The DLL's embedded server honours it; `saintnova serve` checks the flag
  every 50 ms.

## Client implementation notes

`ui/saintnova_client.py` is the reference client. Two details are load-bearing:

1. Open with `FILE_FLAG_OVERLAPPED` and wait per operation. Do not use
   `open(path, "r+b")`; the resulting synchronous handle deadlocks as soon as a
   read is pending while another thread writes.
2. When closing, call `CancelIoEx` and join the reader thread *before*
   `CloseHandle`. A pending overlapped read keeps a pointer to the read buffer;
   closing the handle first would leave the kernel writing into freed memory.
