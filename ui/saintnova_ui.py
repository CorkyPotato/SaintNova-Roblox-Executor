"""SaintNova desktop UI.

A tkinter front end over the named-pipe protocol. It can start the local
server itself, run scripts against any registered backend, stream output,
cancel a running job, and reset the harness VM.

Run with:  python ui/saintnova_ui.py
"""

from __future__ import annotations

import os
import subprocess
import sys
import time
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from saintnova_client import MessageType, PipeConnection

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EXE = ROOT / "build" / "bin" / "saintnova.exe"
DEFAULT_PIPE = "saintnova-default"

SAMPLE = """-- SaintNova sample
local part = Instance.new("Part")
part.Name = "SaintNovaPart"
part.Size = Vector3.new(4, 1, 2)
part.Parent = workspace
print("created", part:GetFullName(), part.Size)

local signal = Signal.new()
signal:Connect(function(value)
    print("signal fired with", value)
end)
firesignal(signal, 42)
"""


class SaintNovaUi(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("SaintNova")
        self.geometry("1000x720")
        self.minsize(760, 520)

        self.connection: PipeConnection | None = None
        self.hello: dict = {}
        self.backends: dict[str, dict] = {}
        self.current_request: str | None = None
        self.request_started: float = 0.0
        self.server_process: subprocess.Popen | None = None
        self._polling = False

        self.pipe_var = tk.StringVar(value=DEFAULT_PIPE)
        self.backend_var = tk.StringVar(value="")
        self.timeout_var = tk.StringVar(value="5000")
        self.status_var = tk.StringVar(value="disconnected")
        self.result_var = tk.StringVar(value="")
        self.console_title = "SaintNova"

        self._build_style()
        self._build_layout()
        self._set_connected(False)
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.after(60, self._poll_events)

    # -- layout ------------------------------------------------------------
    def _build_style(self) -> None:
        style = ttk.Style(self)
        try:
            style.theme_use("vista")
        except tk.TclError:
            pass
        style.configure("Status.TLabel", padding=(6, 2))
        style.configure("Toolbar.TFrame", padding=6)

    def _build_layout(self) -> None:
        toolbar = ttk.Frame(self, style="Toolbar.TFrame")
        toolbar.pack(side=tk.TOP, fill=tk.X)

        ttk.Label(toolbar, text="Pipe").pack(side=tk.LEFT)
        self.pipe_entry = ttk.Entry(toolbar, textvariable=self.pipe_var, width=22)
        self.pipe_entry.pack(side=tk.LEFT, padx=(4, 8))

        self.connect_button = ttk.Button(toolbar, text="Connect", command=self._connect_clicked)
        self.connect_button.pack(side=tk.LEFT)
        self.start_button = ttk.Button(toolbar, text="Start server", command=self._start_server)
        self.start_button.pack(side=tk.LEFT, padx=(4, 12))

        ttk.Label(toolbar, text="Backend").pack(side=tk.LEFT)
        self.backend_box = ttk.Combobox(toolbar, textvariable=self.backend_var, state="readonly", width=18)
        self.backend_box.pack(side=tk.LEFT, padx=(4, 12))
        self.backend_box.bind("<<ComboboxSelected>>", lambda _event: self._backend_changed())

        ttk.Label(toolbar, text="Timeout ms").pack(side=tk.LEFT)
        self.timeout_spin = ttk.Spinbox(
            toolbar, from_=50, to=600000, increment=500, textvariable=self.timeout_var, width=8
        )
        self.timeout_spin.pack(side=tk.LEFT, padx=(4, 12))

        self.run_button = ttk.Button(toolbar, text="Run  F5", command=self._run)
        self.run_button.pack(side=tk.LEFT)
        self.cancel_button = ttk.Button(toolbar, text="Cancel", command=self._cancel)
        self.cancel_button.pack(side=tk.LEFT, padx=(4, 0))
        self.reset_button = ttk.Button(toolbar, text="Reset VM", command=self._reset)
        self.reset_button.pack(side=tk.LEFT, padx=(4, 0))

        actions = ttk.Frame(self, style="Toolbar.TFrame")
        actions.pack(side=tk.TOP, fill=tk.X)
        ttk.Button(actions, text="Open…", command=self._open).pack(side=tk.LEFT)
        ttk.Button(actions, text="Save…", command=self._save).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Button(actions, text="Clear log", command=self._clear_log).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Label(actions, text="F5 runs; scripts share one Luau global environment.").pack(side=tk.LEFT, padx=12)

        panes = ttk.PanedWindow(self, orient=tk.VERTICAL)
        panes.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=6, pady=(0, 4))

        editor_frame = ttk.Frame(panes)
        self.editor = tk.Text(editor_frame, undo=True, wrap=tk.NONE, font=("Consolas", 10), tabs=("4c",))
        editor_scroll = ttk.Scrollbar(editor_frame, orient=tk.VERTICAL, command=self.editor.yview)
        self.editor.configure(yscrollcommand=editor_scroll.set)
        self.editor.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        editor_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.editor.insert("1.0", SAMPLE)
        self.editor.bind("<Tab>", self._insert_tab)
        panes.add(editor_frame, weight=3)

        log_frame = ttk.Frame(panes)
        self.log = tk.Text(log_frame, wrap=tk.WORD, state=tk.DISABLED, font=("Consolas", 9))
        log_scroll = ttk.Scrollbar(log_frame, orient=tk.VERTICAL, command=self.log.yview)
        self.log.configure(yscrollcommand=log_scroll.set)
        self.log.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        log_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.log.tag_configure("info", foreground="#1b1b1b")
        self.log.tag_configure("warn", foreground="#a05a00")
        self.log.tag_configure("error", foreground="#b00020")
        self.log.tag_configure("debug", foreground="#666666")
        self.log.tag_configure("console", foreground="#005f73")
        self.log.tag_configure("system", foreground="#555555", font=("Consolas", 9, "italic"))
        panes.add(log_frame, weight=2)

        status = ttk.Frame(self)
        status.pack(side=tk.BOTTOM, fill=tk.X)
        ttk.Label(status, textvariable=self.status_var, style="Status.TLabel").pack(side=tk.LEFT)
        ttk.Label(status, textvariable=self.result_var, style="Status.TLabel").pack(side=tk.RIGHT)

        self.bind("<F5>", lambda _event: self._run())
        self.bind("<Control-Return>", lambda _event: self._run())
        self.bind("<Shift-F5>", lambda _event: self._cancel())

    def _insert_tab(self, _event) -> str:
        self.editor.insert(tk.INSERT, "    ")
        return "break"

    # -- connection --------------------------------------------------------
    def _connect_clicked(self) -> None:
        if self.connection is not None:
            self._disconnect()
            return
        self._begin_connect(retries=1)

    def _begin_connect(self, retries: int = 10) -> None:
        pipe = self.pipe_var.get().strip() or DEFAULT_PIPE
        try:
            connection = PipeConnection(pipe).connect(timeout=0.4)
        except ConnectionError as exc:
            if retries > 0:
                self.status_var.set(f"waiting for {pipe}…")
                self.after(150, lambda: self._begin_connect(retries - 1))
            else:
                self.status_var.set("disconnected")
                messagebox.showerror("SaintNova", str(exc), parent=self)
            return

        self.connection = connection
        self._set_connected(True)
        self._log_line("system", f"connected to \\\\.\\pipe\\{pipe}")
        connection.hello()

    def _disconnect(self) -> None:
        if self.connection is not None:
            self.connection.close()
            self.connection = None
        self.hello = {}
        self.backends = {}
        self.backend_box["values"] = []
        self.backend_var.set("")
        self._set_connected(False)
        self.status_var.set("disconnected")

    def _set_connected(self, connected: bool) -> None:
        state = tk.NORMAL if connected else tk.DISABLED
        for widget in (self.run_button, self.cancel_button, self.reset_button, self.backend_box):
            widget.configure(state=state if widget is not self.backend_box else ("readonly" if connected else tk.DISABLED))
        self.connect_button.configure(text="Disconnect" if connected else "Connect")
        self.pipe_entry.configure(state=tk.DISABLED if connected else tk.NORMAL)
        self.start_button.configure(state=tk.DISABLED if connected else tk.NORMAL)

    def _start_server(self) -> None:
        exe = Path(os.environ.get("SAINTNOVA_EXE", DEFAULT_EXE))
        if not exe.exists():
            messagebox.showerror("SaintNova", f"server executable not found:\n{exe}", parent=self)
            return
        pipe = self.pipe_var.get().strip() or DEFAULT_PIPE
        try:
            self.server_process = subprocess.Popen(
                [str(exe), "serve", "--pipe", pipe],
                cwd=str(ROOT),
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
        except OSError as exc:
            messagebox.showerror("SaintNova", f"could not start the server: {exc}", parent=self)
            return
        self._log_line("system", f"started server pid {self.server_process.pid} on {pipe}")
        self._begin_connect(retries=20)

    def _on_close(self) -> None:
        if self.connection is not None:
            try:
                self.connection.shutdown()
            except Exception:
                pass
            self.connection.close()
        if self.server_process is not None and self.server_process.poll() is None:
            self.server_process.terminate()
            try:
                self.server_process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.server_process.kill()
        self.destroy()

    # -- commands ----------------------------------------------------------
    def _run(self) -> None:
        if self.connection is None:
            messagebox.showinfo("SaintNova", "Connect to a server first.", parent=self)
            return
        if self.current_request is not None:
            return
        script = self.editor.get("1.0", "end-1c")
        if not script.strip():
            return
        try:
            timeout_ms = max(50, int(self.timeout_var.get()))
        except ValueError:
            messagebox.showerror("SaintNova", "Timeout must be an integer number of milliseconds.", parent=self)
            return
        backend = self.backend_var.get()
        try:
            self.current_request = self.connection.execute(
                script, backend=backend, timeout_ms=timeout_ms, chunk="=saintnova.ui"
            )
        except (ConnectionError, ValueError) as exc:
            messagebox.showerror("SaintNova", str(exc), parent=self)
            return
        self.request_started = time.monotonic()
        self.result_var.set("running…")
        self.status_var.set(f"running ({backend or 'default backend'})")
        self.run_button.configure(state=tk.DISABLED)

    def _cancel(self) -> None:
        if self.connection is None or self.current_request is None:
            return
        self.connection.cancel(self.backend_var.get())
        self.status_var.set("cancelling…")

    def _reset(self) -> None:
        if self.connection is None:
            return
        if messagebox.askyesno("SaintNova", "Reset the backend VM? All globals and pending tasks are lost.", parent=self):
            self.connection.reset(self.backend_var.get())
            self._log_line("system", "reset requested")

    def _open(self) -> None:
        path = filedialog.askopenfilename(
            parent=self, filetypes=[("Luau scripts", "*.luau *.lua"), ("All files", "*.*")]
        )
        if not path:
            return
        try:
            text = Path(path).read_text(encoding="utf-8")
        except OSError as exc:
            messagebox.showerror("SaintNova", str(exc), parent=self)
            return
        self.editor.delete("1.0", tk.END)
        self.editor.insert("1.0", text)
        self.title(f"SaintNova — {Path(path).name}")

    def _save(self) -> None:
        path = filedialog.asksaveasfilename(
            parent=self, defaultextension=".luau", filetypes=[("Luau scripts", "*.luau"), ("All files", "*.*")]
        )
        if not path:
            return
        try:
            Path(path).write_text(self.editor.get("1.0", "end-1c"), encoding="utf-8")
        except OSError as exc:
            messagebox.showerror("SaintNova", str(exc), parent=self)
            return
        self._log_line("system", f"saved {path}")

    def _clear_log(self) -> None:
        self.log.configure(state=tk.NORMAL)
        self.log.delete("1.0", tk.END)
        self.log.configure(state=tk.DISABLED)

    # -- events ------------------------------------------------------------
    def _poll_events(self) -> None:
        if self.connection is not None:
            for _ in range(200):
                event = self.connection.next_event(timeout=0)
                if event is None:
                    break
                self._handle_event(event)
        self.after(60, self._poll_events)

    def _handle_event(self, event: dict) -> None:
        if event["event"] == "closed":
            self._log_line("system", "disconnected from the server")
            self.connection = None
            self._set_connected(False)
            self.status_var.set("disconnected")
            return

        type_id = event["type"]
        payload = event.get("payload") or {}
        name = event.get("name", str(type_id))

        if type_id == MessageType.LOG:
            self._handle_log(payload)
        elif type_id == MessageType.EXECUTE_RESULT:
            self._handle_result(payload)
        elif type_id == MessageType.HELLO_ACK:
            self._handle_hello(payload)
        elif type_id == MessageType.BACKENDS:
            self._handle_backends(payload.get("backends", []))
        elif type_id == MessageType.ERROR:
            message = payload.get("message", "unknown error")
            self._log_line("error", f"server error [{payload.get('code', '?')}]: {message}")
            if payload.get("id") == self.current_request:
                self.current_request = None
                self.run_button.configure(state=tk.NORMAL)
                self.status_var.set("error")
        elif type_id == MessageType.PONG:
            if "ok" in payload:
                self._log_line("system", "server shutdown acknowledged")
            else:
                self._log_line("system", f"pong ({payload.get('t', '?')})")

    def _handle_log(self, payload: dict) -> None:
        source = payload.get("source") or ""
        if source == "console.clear":
            self._clear_log()
            return
        if source == "console.title":
            self.console_title = payload.get("text") or "SaintNova"
            self.title(f"SaintNova — {self.console_title}")
            return
        level = payload.get("level", "info")
        text = payload.get("text", "")
        tag = "console" if source == "console" and level == "info" else level
        self._log_line(tag, text)

    def _handle_result(self, payload: dict) -> None:
        if payload.get("id") != self.current_request:
            return
        self.current_request = None
        elapsed = time.monotonic() - self.request_started
        ok = payload.get("ok", False)
        duration = payload.get("duration_ms", elapsed * 1000.0)
        error = payload.get("error") or ""
        if ok:
            self.result_var.set(f"ok in {duration:.1f} ms")
            self.status_var.set("ready")
        else:
            self.result_var.set(f"failed in {duration:.1f} ms")
            self.status_var.set("script failed")
            self._log_line("error", error or "script failed")
        self.run_button.configure(state=tk.NORMAL)

    def _handle_hello(self, payload: dict) -> None:
        self.hello = payload
        self.status_var.set(
            f"connected — {payload.get('product', 'SaintNova')} {payload.get('version', '')} "
            f"(protocol {payload.get('protocol', '?')})"
        )
        self._handle_backends(payload.get("backends", []))

    def _handle_backends(self, entries: list[dict]) -> None:
        self.backends = {entry.get("id", ""): entry for entry in entries if entry.get("id")}
        values = []
        for backend_id, entry in self.backends.items():
            suffix = "" if entry.get("available", False) else " (unavailable)"
            values.append(f"{backend_id}{suffix}")
        self.backend_box["values"] = values
        current = self.backend_var.get().split(" ")[0]
        if current in self.backends:
            self.backend_var.set(next(v for v in values if v.startswith(current)))
        elif values:
            self.backend_var.set(values[0])
        self._backend_changed()

    def _backend_changed(self) -> None:
        backend_id = self.backend_var.get().split(" ")[0]
        entry = self.backends.get(backend_id)
        if entry is not None:
            self.status_var.set(f"{backend_id}: {entry.get('detail', '')}")

    def _log_line(self, level: str, text: str) -> None:
        if level not in {"info", "warn", "error", "debug", "console", "system"}:
            level = "info"
        self.log.configure(state=tk.NORMAL)
        stamp = time.strftime("%H:%M:%S")
        self.log.insert(tk.END, f"[{stamp}] {text}\n", level)
        self.log.see(tk.END)
        self.log.configure(state=tk.DISABLED)


def main() -> int:
    app = SaintNovaUi()
    app.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
