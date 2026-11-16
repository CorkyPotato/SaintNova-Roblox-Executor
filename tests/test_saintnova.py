from __future__ import annotations

import os
import struct
import subprocess
import sys
import time
import unittest
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "bin" / "saintnova.exe"
sys.path.insert(0, str(ROOT / "ui"))

from saintnova_client import (  # noqa: E402
    HEADER,
    MAX_PAYLOAD,
    MessageType,
    PipeConnection,
    decode_header,
    encode_frame,
)


def unique_pipe() -> str:
    return f"saintnova-test-{os.getpid()}-{uuid.uuid4().hex[:8]}"


class Server:
    def __init__(self) -> None:
        self.pipe = unique_pipe()
        self.process: subprocess.Popen | None = None
        self.client: PipeConnection | None = None
        self.hello: dict = {}

    def __enter__(self) -> "Server":
        self.process = subprocess.Popen(
            [str(EXE), "serve", "--pipe", self.pipe],
            cwd=str(ROOT),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self.client = PipeConnection(self.pipe).connect(timeout=6.0)
        self.client.hello()
        self.hello = self.client.wait_for("HELLO_ACK", timeout=5.0)["payload"]
        return self

    def __exit__(self, *exc_info) -> None:
        if self.client is not None:
            self.client.close()
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()

    def execute(self, script: str, *, timeout_ms: int = 5000, backend: str = "") -> tuple[dict, list[dict]]:
        assert self.client is not None
        request_id = self.client.execute(script, timeout_ms=timeout_ms, backend=backend, chunk="=test")
        logs: list[dict] = []
        result = self.client.wait_for("EXECUTE_RESULT", request_id=request_id, timeout=timeout_ms / 1000 + 10, collect=logs)
        return result["payload"], logs


@unittest.skipUnless(EXE.exists(), f"server binary not built at {EXE}")
class ProtocolUnitTests(unittest.TestCase):
    def test_header_round_trip(self):
        frame = encode_frame(MessageType.EXECUTE, {"id": "x", "script": "print(1)"})
        length, message_type, flags = decode_header(frame[: HEADER.size])
        self.assertEqual(length, len(frame) - HEADER.size)
        self.assertEqual(message_type, int(MessageType.EXECUTE))
        self.assertEqual(flags, 0)

    def test_oversized_frame_rejected(self):
        with self.assertRaises(ValueError):
            encode_frame(MessageType.EXECUTE, b"x" * (MAX_PAYLOAD + 1))
        with self.assertRaises(ValueError):
            decode_header(struct.pack("<IHH", MAX_PAYLOAD + 1, int(MessageType.EXECUTE), 0))

    def test_empty_payload(self):
        self.assertEqual(len(encode_frame(MessageType.PING)), HEADER.size)


@unittest.skipUnless(EXE.exists(), f"server binary not built at {EXE}")
class PipeServerTests(unittest.TestCase):
    def test_hello_and_backends(self):
        with Server() as server:
            self.assertEqual(server.hello["protocol"], 1)
            self.assertEqual(server.hello["product"], "SaintNova")
            backends = {entry["id"]: entry for entry in server.hello["backends"]}
            self.assertIn("harness", backends)
            self.assertTrue(backends["harness"]["available"])
            self.assertIn("roblox", backends)
            roblox = backends["roblox"]
            self.assertIsInstance(roblox["available"], bool)
            # Availability depends on the machine: a live Studio is optional.
            # What must hold is that the backend explains itself either way.
            self.assertTrue(roblox["detail"])

            server.client.list_backends()
            reply = server.client.wait_for("BACKENDS", timeout=5.0)["payload"]
            self.assertTrue(any(entry["id"] == "harness" for entry in reply["backends"]))

    def test_execute_streams_logs(self):
        with Server() as server:
            result, logs = server.execute("print('one')\nwarn('two')\nreturn 3")
            self.assertTrue(result["ok"], result)
            self.assertEqual(result["error"], "")
            texts = [line["text"] for line in logs]
            self.assertIn("one", texts)
            self.assertIn("two", texts)
            self.assertGreater(result["duration_ms"], 0)

    def test_script_error_is_reported(self):
        with Server() as server:
            result, _logs = server.execute("error('boom')")
            self.assertFalse(result["ok"])
            self.assertIn("boom", result["error"])

    def test_bad_json_is_rejected(self):
        with Server() as server:
            server.client.send(MessageType.EXECUTE, b"{not json")
            error = server.client.wait_for("ERROR", timeout=5.0)["payload"]
            self.assertEqual(error["code"], "bad_payload")

    def test_timeout(self):
        with Server() as server:
            result, _logs = server.execute("while true do end", timeout_ms=300)
            self.assertFalse(result["ok"])
            self.assertIn("timed out", result["error"])

    def test_cancel(self):
        with Server() as server:
            request_id = server.client.execute(
                "while true do task.wait(0.02) end", timeout_ms=30000, chunk="=test"
            )
            time.sleep(0.3)
            server.client.cancel()
            result = server.client.wait_for("EXECUTE_RESULT", request_id=request_id, timeout=10.0)["payload"]
            self.assertFalse(result["ok"])
            self.assertIn("cancelled", result["error"])

            # The connection must remain usable after a cancel.
            after, _logs = server.execute("print('alive')")
            self.assertTrue(after["ok"], after)

    def test_busy_connection(self):
        with Server() as server:
            first = server.client.execute("task.wait(0.4) print('first')", chunk="=test")
            server.client.execute("print('second')", chunk="=test")
            error = server.client.wait_for("ERROR", timeout=5.0)["payload"]
            self.assertEqual(error["code"], "busy")
            result = server.client.wait_for("EXECUTE_RESULT", request_id=first, timeout=10.0)["payload"]
            self.assertTrue(result["ok"], result)

    def test_reset_clears_globals(self):
        with Server() as server:
            first, _logs = server.execute("shared.marker = 7\nleaked = 42")
            self.assertTrue(first["ok"], first)
            server.client.reset()
            server.client.wait_for("BACKENDS", timeout=10.0)
            result, logs = server.execute("print(leaked, shared.marker)")
            self.assertTrue(result["ok"], result)
            texts = [line["text"] for line in logs]
            self.assertTrue(any(text.startswith("nil") for text in texts), texts)

    def test_unavailable_backend(self):
        with Server() as server:
            result, _logs = server.execute("print('x')", backend="does-not-exist")
            self.assertFalse(result["ok"])
            self.assertIn("unknown backend", result["error"].lower())

    def test_ping(self):
        with Server() as server:
            server.client.ping()
            pong = server.client.wait_for("PONG", timeout=5.0)["payload"]
            self.assertIn("t", pong)


@unittest.skipUnless(EXE.exists(), f"server binary not built at {EXE}")
class CliTests(unittest.TestCase):
    def run_cli(self, *args: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [str(EXE), *args],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=60,
        )

    def test_version(self):
        result = self.run_cli("version")
        self.assertEqual(result.returncode, 0)
        self.assertIn("SaintNova", result.stdout)

    def test_backends(self):
        result = self.run_cli("backends")
        self.assertEqual(result.returncode, 0)
        self.assertIn("harness", result.stdout)
        roblox_line = next((line for line in result.stdout.splitlines() if line.startswith("roblox")), "")
        self.assertTrue("available" in roblox_line, result.stdout)

    def test_run_script(self):
        result = self.run_cli("run", str(ROOT / "examples" / "basics.luau"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ok in", result.stdout)

    def test_runtime_failure_exit_code(self):
        script = ROOT / "build" / "tests" / "runtime_error.luau"
        script.parent.mkdir(parents=True, exist_ok=True)
        script.write_text("error('deliberate')\n", encoding="utf-8")
        result = self.run_cli("run", str(script))
        self.assertEqual(result.returncode, 1)
        self.assertIn("deliberate", result.stderr)

    def test_timeout_exit_code(self):
        script = ROOT / "build" / "tests" / "timeout.luau"
        script.parent.mkdir(parents=True, exist_ok=True)
        script.write_text("while true do end\n", encoding="utf-8")
        result = self.run_cli("run", str(script), "--timeout", "200")
        self.assertEqual(result.returncode, 1)
        self.assertIn("timed out", result.stderr)

    def test_full_api_smoke(self):
        result = self.run_cli("run", str(ROOT / "tests" / "api_smoke.luau"), "--timeout", "10000")
        self.assertEqual(result.returncode, 0, result.stderr)
        combined = result.stdout + result.stderr
        self.assertIn("smoke complete", combined)
        self.assertNotIn("FAIL ", combined)

    def test_wave_surface(self):
        result = self.run_cli("run", str(ROOT / "tests" / "surface.luau"), "--timeout", "10000")
        self.assertEqual(result.returncode, 0, result.stderr)
        combined = result.stdout + result.stderr
        self.assertIn("surface complete", combined)
        self.assertNotIn("FAIL ", combined)
        self.assertIn("120 implemented", combined)
        self.assertIn("57 explicitly unsupported", combined)
        self.assertIn("179 documented", combined)

    def test_library_surface(self):
        result = self.run_cli("run", str(ROOT / "tests" / "libs_smoke.luau"), "--timeout", "10000")
        self.assertEqual(result.returncode, 0, result.stderr)
        combined = result.stdout + result.stderr
        self.assertIn("libs complete", combined)
        self.assertNotIn("FAIL ", combined)


if __name__ == "__main__":
    unittest.main(verbosity=2)
