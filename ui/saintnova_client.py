"""Client for the SaintNova named-pipe protocol.

The framing matches include/saintnova/protocol.hpp: an 8-byte little-endian
header (uint32 payload length, uint16 message type, uint16 flags) followed by
a UTF-8 JSON object.

Transport note
--------------
Windows serialises all I/O on a file object opened for synchronous I/O, so a
thread blocked in ``ReadFile`` prevents any ``WriteFile`` on the same handle
from starting. A pipe client that reads and writes from different threads must
therefore open the handle with ``FILE_FLAG_OVERLAPPED``. ``open(path, "r+b")``
does not, which is why this module talks to kernel32 through ctypes instead of
using a buffered file object.

This module has no GUI dependencies. A reader thread turns incoming frames
into events on a queue so a UI can drain them from its own loop.
"""

from __future__ import annotations

import ctypes
import json
import queue
import struct
import sys
import threading
import time
import uuid
from ctypes import wintypes
from enum import IntEnum

HEADER = struct.Struct("<IHH")
MAX_PAYLOAD = 16 * 1024 * 1024


class MessageType(IntEnum):
    HELLO = 0x0001
    HELLO_ACK = 0x0002
    EXECUTE = 0x0010
    EXECUTE_RESULT = 0x0011
    CANCEL = 0x0012
    LOG = 0x0020
    LIST_BACKENDS = 0x0030
    BACKENDS = 0x0031
    RESET = 0x0032
    PING = 0x0040
    PONG = 0x0041
    SHUTDOWN = 0x0050
    ERROR = 0x00FF


MESSAGE_NAMES = {int(value): value.name for value in MessageType}


def encode_frame(message_type: int, payload: dict | bytes | str | None = None, flags: int = 0) -> bytes:
    if payload is None:
        body = b""
    elif isinstance(payload, bytes):
        body = payload
    elif isinstance(payload, str):
        body = payload.encode("utf-8")
    else:
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    if len(body) > MAX_PAYLOAD:
        raise ValueError(f"payload is {len(body)} bytes; the protocol limit is {MAX_PAYLOAD}")
    return HEADER.pack(len(body), int(message_type), flags) + body


def decode_header(data: bytes) -> tuple[int, int, int]:
    if len(data) != HEADER.size:
        raise ValueError("header must be exactly 8 bytes")
    length, message_type, flags = HEADER.unpack(data)
    if length > MAX_PAYLOAD:
        raise ValueError(f"frame announces {length} bytes; the protocol limit is {MAX_PAYLOAD}")
    return length, message_type, flags


# ---------------------------------------------------------------------------
# Win32 pipe transport
# ---------------------------------------------------------------------------

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
FILE_FLAG_OVERLAPPED = 0x40000000
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

ERROR_IO_PENDING = 997
ERROR_PIPE_BUSY = 231
ERROR_BROKEN_PIPE = 109
ERROR_PIPE_NOT_CONNECTED = 233
ERROR_OPERATION_ABORTED = 995
ERROR_NO_DATA = 232
WAIT_OBJECT_0 = 0

_DISCONNECT_ERRORS = {ERROR_BROKEN_PIPE, ERROR_PIPE_NOT_CONNECTED, ERROR_NO_DATA, ERROR_OPERATION_ABORTED}


class _Overlapped(ctypes.Structure):
    _fields_ = [
        ("Internal", ctypes.c_void_p),
        ("InternalHigh", ctypes.c_void_p),
        ("Offset", wintypes.DWORD),
        ("OffsetHigh", wintypes.DWORD),
        ("hEvent", wintypes.HANDLE),
    ]


def _load_kernel32() -> "ctypes.WinDLL":
    if sys.platform != "win32":
        raise RuntimeError("the SaintNova pipe transport requires Windows")
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)  # type: ignore[attr-defined]

    kernel32.CreateFileW.argtypes = [
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.LPVOID,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.HANDLE,
    ]
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.ReadFile.argtypes = [
        wintypes.HANDLE,
        wintypes.LPVOID,
        wintypes.DWORD,
        wintypes.LPDWORD,
        ctypes.POINTER(_Overlapped),
    ]
    kernel32.ReadFile.restype = wintypes.BOOL
    kernel32.WriteFile.argtypes = kernel32.ReadFile.argtypes
    kernel32.WriteFile.restype = wintypes.BOOL
    kernel32.GetOverlappedResult.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(_Overlapped),
        wintypes.LPDWORD,
        wintypes.BOOL,
    ]
    kernel32.GetOverlappedResult.restype = wintypes.BOOL
    kernel32.CreateEventW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
    kernel32.CreateEventW.restype = wintypes.HANDLE
    kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel32.WaitForSingleObject.restype = wintypes.DWORD
    kernel32.CancelIoEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(_Overlapped)]
    kernel32.CancelIoEx.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    kernel32.WaitNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD]
    kernel32.WaitNamedPipeW.restype = wintypes.BOOL
    return kernel32


_kernel32 = _load_kernel32()
_get_last_error = ctypes.get_last_error


class _Pipe:
    """A duplex handle with overlapped reads and writes."""

    def __init__(self, path: str) -> None:
        self.path = path
        self.handle: int | None = None
        self._closed = False

    def open(self, timeout: float = 2.0) -> "_Pipe":
        deadline = time.monotonic() + timeout
        last_error = 0
        while True:
            handle = _kernel32.CreateFileW(
                self.path,
                GENERIC_READ | GENERIC_WRITE,
                0,
                None,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                None,
            )
            if handle is not None and handle != INVALID_HANDLE_VALUE:
                self.handle = handle
                return self

            last_error = _get_last_error()
            if last_error != ERROR_PIPE_BUSY:
                # The server has not created an instance yet; retry until the
                # deadline rather than failing on the first attempt.
                if time.monotonic() >= deadline:
                    raise ConnectionError(
                        f"cannot open {self.path}: WinError {last_error} ({ctypes.FormatError(last_error).strip()})"
                    )
                time.sleep(0.02)
                continue

            remaining_ms = max(1, int((deadline - time.monotonic()) * 1000))
            if _kernel32.WaitNamedPipeW(self.path, min(remaining_ms, 100)) == 0:
                if time.monotonic() >= deadline:
                    raise ConnectionError(f"timed out waiting for {self.path}")
                time.sleep(0.01)

    def _run(self, read: bool, buffer: ctypes.Array, size: int) -> int:
        if self.handle is None:
            raise ConnectionError("the pipe is closed")

        overlapped = _Overlapped()
        event = _kernel32.CreateEventW(None, True, False, None)
        if not event:
            raise ConnectionError("CreateEventW failed")
        overlapped.hEvent = event
        try:
            operation = _kernel32.ReadFile if read else _kernel32.WriteFile
            transferred = wintypes.DWORD(0)
            started = operation(self.handle, buffer, size, ctypes.byref(transferred), ctypes.byref(overlapped))

            if not started:
                error = _get_last_error()
                if error != ERROR_IO_PENDING:
                    if error in _DISCONNECT_ERRORS:
                        raise ConnectionError("the server closed the pipe")
                    raise ConnectionError(
                        f"{'read' if read else 'write'} failed: WinError {error} "
                        f"({ctypes.FormatError(error).strip()})"
                    )
                if _kernel32.WaitForSingleObject(event, 0xFFFFFFFF) != WAIT_OBJECT_0:
                    raise ConnectionError("waiting for pipe I/O failed")

                completed = wintypes.DWORD(0)
                if not _kernel32.GetOverlappedResult(self.handle, ctypes.byref(overlapped), ctypes.byref(completed), False):
                    error = _get_last_error()
                    if error in _DISCONNECT_ERRORS:
                        raise ConnectionError("the server closed the pipe")
                    raise ConnectionError(
                        f"{'read' if read else 'write'} failed: WinError {error} "
                        f"({ctypes.FormatError(error).strip()})"
                    )
                transferred = completed

            return int(transferred.value)
        finally:
            _kernel32.CloseHandle(event)

    def read(self, size: int) -> bytes:
        buffer = ctypes.create_string_buffer(size)
        received = self._run(True, buffer, size)
        if received == 0:
            raise ConnectionError("the server closed the pipe")
        return buffer.raw[:received]

    def write(self, data: bytes) -> None:
        view = memoryview(data)
        offset = 0
        while offset < len(view):
            chunk = bytes(view[offset : offset + (1 << 20)])
            buffer = ctypes.create_string_buffer(chunk, len(chunk))
            offset += self._run(False, buffer, len(chunk))

    def cancel(self) -> None:
        if self.handle is not None:
            _kernel32.CancelIoEx(self.handle, None)

    def close(self) -> None:
        if self.handle is not None and not self._closed:
            self._closed = True
            _kernel32.CloseHandle(self.handle)
        self.handle = None


class PipeConnection:
    """A single client connection to an SaintNova server.

    Events are dictionaries with an ``event`` key:

    * ``{"event": "message", "type": int, "name": str, "payload": dict}``
    * ``{"event": "closed", "error": str | None}``

    ``payload`` is ``{}`` when the frame body is empty.
    """

    def __init__(self, pipe_name: str = "saintnova-default") -> None:
        self.pipe_name = pipe_name
        self.path = rf"\\.\pipe\{pipe_name}"
        self._pipe: _Pipe | None = None
        self._send_lock = threading.Lock()
        self._reader: threading.Thread | None = None
        self._running = False
        self.events: queue.Queue[dict] = queue.Queue()

    # -- lifecycle ---------------------------------------------------------
    def connect(self, timeout: float = 2.0) -> "PipeConnection":
        pipe = _Pipe(self.path)
        try:
            pipe.open(timeout=timeout)
        except ConnectionError:
            raise
        self._pipe = pipe
        self._running = True
        self._reader = threading.Thread(target=self._reader_loop, name="saintnova-pipe-reader", daemon=True)
        self._reader.start()
        return self

    def close(self) -> None:
        self._running = False
        pipe = self._pipe
        if pipe is None:
            return

        # Cancel the reader's pending read, then wait for it to leave read()
        # before the handle is closed. The retry loop covers the narrow window
        # where the reader was just about to issue a fresh read.
        reader = self._reader
        for _ in range(50):
            pipe.cancel()
            if reader is None or not reader.is_alive() or reader is threading.current_thread():
                break
            time.sleep(0.005)
        if reader is not None and reader is not threading.current_thread():
            reader.join(timeout=2.0)

        with self._send_lock:
            pipe.close()
        self._pipe = None
        self._reader = None

    def __enter__(self) -> "PipeConnection":
        return self

    def __exit__(self, *exc_info) -> None:
        self.close()

    @property
    def connected(self) -> bool:
        return self._pipe is not None and self._running

    # -- framing -----------------------------------------------------------
    def _read_exact(self, size: int) -> bytes:
        assert self._pipe is not None
        chunks = bytearray()
        while len(chunks) < size:
            chunks.extend(self._pipe.read(size - len(chunks)))
        return bytes(chunks)

    def _reader_loop(self) -> None:
        error: str | None = None
        try:
            while self._running:
                header = self._read_exact(HEADER.size)
                length, message_type, _flags = decode_header(header)
                body = self._read_exact(length) if length else b""
                try:
                    payload = json.loads(body.decode("utf-8")) if body else {}
                except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                    self.events.put(
                        {
                            "event": "message",
                            "type": int(MessageType.ERROR),
                            "name": "ERROR",
                            "payload": {"code": "bad_json", "message": str(exc)},
                        }
                    )
                    continue
                self.events.put(
                    {
                        "event": "message",
                        "type": message_type,
                        "name": MESSAGE_NAMES.get(message_type, f"0x{message_type:04X}"),
                        "payload": payload,
                    }
                )
        except Exception as exc:  # connection closed, decode failure, ...
            if self._running:
                error = str(exc)
        finally:
            self._running = False
            self.events.put({"event": "closed", "error": error})

    def send(self, message_type: int, payload: dict | bytes | str | None = None) -> None:
        pipe = self._pipe
        if pipe is None:
            raise ConnectionError("not connected")
        frame = encode_frame(message_type, payload)
        with self._send_lock:
            pipe.write(frame)

    # -- protocol operations ----------------------------------------------
    def hello(self) -> None:
        self.send(MessageType.HELLO, {})

    def list_backends(self) -> None:
        self.send(MessageType.LIST_BACKENDS, {})

    def execute(
        self,
        script: str,
        *,
        backend: str = "",
        timeout_ms: int = 5000,
        chunk: str = "saintnova.ui",
        request_id: str | None = None,
    ) -> str:
        request_id = request_id or uuid.uuid4().hex[:12]
        self.send(
            MessageType.EXECUTE,
            {
                "id": request_id,
                "script": script,
                "backend": backend,
                "timeout_ms": int(timeout_ms),
                "chunk": chunk,
            },
        )
        return request_id

    def cancel(self, backend: str = "") -> None:
        self.send(MessageType.CANCEL, {"backend": backend})

    def reset(self, backend: str = "") -> None:
        self.send(MessageType.RESET, {"backend": backend})

    def ping(self) -> None:
        self.send(MessageType.PING, {})

    def shutdown(self) -> None:
        self.send(MessageType.SHUTDOWN, {})

    # -- event helpers -----------------------------------------------------
    def next_event(self, timeout: float | None = None) -> dict | None:
        try:
            return self.events.get(timeout=timeout)
        except queue.Empty:
            return None

    def wait_for(
        self,
        name: str,
        *,
        request_id: str | None = None,
        timeout: float = 30.0,
        collect: list[dict] | None = None,
    ) -> dict:
        """Waits for a message type by name, skipping unrelated events."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = max(0.0, deadline - time.monotonic())
            event = self.next_event(timeout=remaining)
            if event is None:
                raise TimeoutError(f"timed out waiting for {name}")
            if event["event"] == "closed":
                raise ConnectionError(event.get("error") or "the server closed the pipe")
            payload = event.get("payload") or {}
            if collect is not None and event["name"] == "LOG":
                collect.append(payload)
            if event["name"] == name and (request_id is None or payload.get("id") == request_id):
                return event
