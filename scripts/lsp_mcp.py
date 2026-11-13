#!/usr/bin/env python3
"""Low-memory, self-contained MCP server exposing C++23 (`clangd`) and Python LSP tools over stdio.

Tools exposed (all strictly read-only):
  - `lsp_diagnostics(file)`
  - `lsp_symbols(file_or_query)`
  - `lsp_definition(file, line, character)`
  - `lsp_references(file, line, character)`
  - `lsp_hover(file, line, character)`

Line and character arguments are 1-based (`line >= 1`, `character >= 1`) to match editor and grep line numbers.
Language servers (`scripts/clangd-lsp.py` and `scripts/python-lsp.py`) start lazily on first request for that language
and shut down automatically after 5 minutes of inactivity so idle sessions consume near-zero RAM. Standard library only.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time
from urllib.parse import unquote, urlparse

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
IDLE_SHUTDOWN_SECONDS = 300
CPP_EXTS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".ixx", ".cppm"}
PY_EXTS = {".py", ".pyi"}
SEVERITY_NAMES = {1: "error", 2: "warning", 3: "info", 4: "hint"}
SYMBOL_KINDS = {
    1: "file", 2: "module", 3: "namespace", 4: "package", 5: "class", 6: "method", 7: "property",
    8: "field", 9: "constructor", 10: "enum", 11: "interface", 12: "function", 13: "variable",
    14: "constant", 22: "enum-member", 23: "struct", 26: "type-param",
}


def windowless() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def _uri_key(uri: str) -> str:
    parsed = urlparse(uri)
    if parsed.scheme.lower() == "file":
        raw = unquote(parsed.path)
        if os.name == "nt" and raw.startswith("/") and len(raw) >= 3 and raw[2] == ":":
            raw = raw[1:]
        return os.path.normcase(os.path.normpath(raw))
    return unquote(uri).lower()


def uri_to_rel(uri: str, root: Path) -> str:
    parsed = urlparse(uri)
    raw = unquote(parsed.path)
    if os.name == "nt" and raw.startswith("/") and len(raw) >= 3 and raw[2] == ":":
        raw = raw[1:]
    p = Path(raw).resolve()
    try:
        return p.relative_to(root.resolve()).as_posix()
    except ValueError:
        return p.as_posix()


class LspClient:
    """Thread-safe JSON-RPC LSP client managing one child language server process."""

    def __init__(self, lang: str, cmd: list[str], root: Path):
        self.lang = lang
        self.cmd = cmd
        self.root = root
        self.proc: subprocess.Popen | None = None
        self._lifecycle_lock = threading.Lock()
        self._lock = threading.Lock()
        self._next_id = 1
        self._pending: dict[int, queue.Queue] = {}
        self._diag_cond = threading.Condition()
        self._sync_seq = 0
        self._diagnostics: dict[str, tuple[int, int | None, list[dict]]] = {}
        self._closed_reason: str | None = None
        self._opened: dict[str, int] = {}
        self.last_used = time.monotonic()

    def ensure_started(self) -> None:
        with self._lifecycle_lock:
            if self.proc is not None and self.proc.poll() is None:
                self.last_used = time.monotonic()
                return
            if self.proc is not None and self.proc.stdout is not None:
                try:
                    self.proc.stdout.close()
                except Exception:
                    pass
            with self._diag_cond:
                self._opened.clear()
                self._diagnostics.clear()
                self._closed_reason = None
            proc = subprocess.Popen(
                self.cmd,
                cwd=self.root,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                creationflags=windowless(),
            )
            self.proc = proc
            reader = threading.Thread(target=self._reader_loop, args=(proc,), daemon=True)
            reader.start()
            try:
                self.request("initialize", {
                    "processId": os.getpid(),
                    "rootUri": self.root.resolve().as_uri(),
                    "capabilities": {
                        "textDocument": {
                            "publishDiagnostics": {"relatedInformation": True, "versionSupport": True},
                            "documentSymbol": {"hierarchicalDocumentSymbolSupport": True},
                            "hover": {"contentFormat": ["markdown", "plaintext"]},
                        }
                    },
                }, timeout=45.0)
                self.notify("initialized", {})
            except Exception:
                self.proc = None
                try:
                    proc.kill()
                    proc.wait(timeout=2)
                except Exception:
                    pass
                try:
                    if proc.stdin is not None:
                        proc.stdin.close()
                    if proc.stdout is not None:
                        proc.stdout.close()
                except Exception:
                    pass
                raise
            self.last_used = time.monotonic()

    def _reader_loop(self, proc: subprocess.Popen) -> None:
        assert proc.stdout is not None
        stream = proc.stdout
        while True:
            headers: dict[str, str] = {}
            while True:
                try:
                    line = stream.readline()
                except ValueError:
                    line = b""
                if not line:
                    self._fail_all("language server exited")
                    return
                if line in (b"\r\n", b"\n"):
                    break
                text = line.decode("ascii", "replace").strip()
                if ":" in text:
                    k, v = text.split(":", 1)
                    headers[k.strip().lower()] = v.strip()
            length = int(headers.get("content-length", "0"))
            if length <= 0:
                continue
            try:
                body = stream.read(length)
            except ValueError:
                body = b""
            if not body:
                self._fail_all("language server stream closed")
                return
            try:
                msg = json.loads(body.decode("utf-8", "replace"))
            except ValueError:
                continue
            if "id" in msg and ("result" in msg or "error" in msg):
                q = self._pending.pop(msg["id"], None)
                if q is not None:
                    q.put(msg)
            elif msg.get("method") == "textDocument/publishDiagnostics":
                params = msg.get("params") or {}
                uri = params.get("uri", "")
                pub_ver = params.get("version")
                diags = params.get("diagnostics") or []
                with self._diag_cond:
                    self._diagnostics[_uri_key(uri)] = (
                        self._sync_seq,
                        int(pub_ver) if isinstance(pub_ver, int) else None,
                        diags,
                    )
                    self._diag_cond.notify_all()

    def _fail_all(self, reason: str) -> None:
        for _, q in list(self._pending.items()):
            q.put({"error": {"message": reason}})
        self._pending.clear()
        with self._diag_cond:
            self._closed_reason = reason
            self._diag_cond.notify_all()

    def _write(self, payload: dict) -> None:
        assert self.proc is not None and self.proc.stdin is not None
        raw = json.dumps(payload).encode("utf-8")
        header = f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii")
        self.proc.stdin.write(header + raw)
        self.proc.stdin.flush()

    def notify(self, method: str, params: dict) -> None:
        with self._lock:
            self._write({"jsonrpc": "2.0", "method": method, "params": params})

    def request(self, method: str, params: dict, timeout: float = 30.0):
        with self._lock:
            req_id = self._next_id
            self._next_id += 1
            q: queue.Queue = queue.Queue(maxsize=1)
            self._pending[req_id] = q
            self._write({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params})
        try:
            reply = q.get(timeout=timeout)
        except queue.Empty as exc:
            self._pending.pop(req_id, None)
            raise TimeoutError(f"{self.lang} LSP timed out on {method} after {timeout}s") from exc
        if "error" in reply:
            err = reply["error"]
            raise RuntimeError(err.get("message", str(err)) if isinstance(err, dict) else str(err))
        return reply.get("result")

    def sync_file(self, path: Path) -> tuple[str, int, int]:
        self.ensure_started()
        uri = path.resolve().as_uri()
        key = _uri_key(uri)
        text = path.read_text(encoding="utf-8", errors="replace")
        lang_id = "python" if path.suffix.lower() in PY_EXTS else "cpp"
        with self._diag_cond:
            self._sync_seq += 1
            expected_seq = self._sync_seq
            ver = self._opened.get(key, 0) + 1
            self._opened[key] = ver
        if ver == 1:
            self.notify("textDocument/didOpen", {
                "textDocument": {"uri": uri, "languageId": lang_id, "version": 1, "text": text}
            })
        else:
            self.notify("textDocument/didChange", {
                "textDocument": {"uri": uri, "version": ver},
                "contentChanges": [{"text": text}],
            })
        return uri, expected_seq, ver

    def diagnostics_for(self, path: Path, wait_seconds: float = 30.0) -> list[dict]:
        uri, expected_seq, expected_ver = self.sync_file(path)
        key = _uri_key(uri)
        deadline = time.monotonic() + wait_seconds
        with self._diag_cond:
            while True:
                entry = self._diagnostics.get(key)
                if entry is not None:
                    arrived_seq, pub_ver, diags = entry
                    if arrived_seq >= expected_seq and (pub_ver is None or pub_ver >= expected_ver):
                        return diags
                if self._closed_reason is not None or self.proc is None or self.proc.poll() is not None:
                    detail = self._closed_reason or "process terminated"
                    raise RuntimeError(f"{self.lang} language server exited before publishing diagnostics ({detail})")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(
                        f"{self.lang} language server timed out waiting for diagnostics after {wait_seconds:g}s"
                    )
                self._diag_cond.wait(timeout=min(0.25, remaining))

    def stop(self) -> None:
        with self._lifecycle_lock:
            proc = self.proc
            if proc is None:
                return
            try:
                if proc.poll() is None:
                    try:
                        self.request("shutdown", {}, timeout=2.0)
                        self.notify("exit", {})
                    except Exception:
                        pass
                    try:
                        if proc.stdin is not None:
                            proc.stdin.close()
                    except Exception:
                        pass
                    try:
                        proc.wait(timeout=3)
                    except Exception:
                        if os.name == "nt":
                            subprocess.run(
                                ["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                                capture_output=True,
                                creationflags=windowless(),
                            )
                        try:
                            proc.kill()
                            proc.wait(timeout=2)
                        except Exception:
                            pass
            finally:
                self.proc = None
                try:
                    if proc.stdin is not None:
                        proc.stdin.close()
                except Exception:
                    pass
                try:
                    if proc.stdout is not None:
                        proc.stdout.close()
                except Exception:
                    pass


class LspMcpServer:
    def __init__(self, root: Path = ROOT, idle_seconds: float = IDLE_SHUTDOWN_SECONDS):
        self.root = root.resolve()
        self.idle_seconds = idle_seconds
        self.clients: dict[str, LspClient] = {
            "cpp": LspClient("cpp", [sys.executable, "-B", str(HERE / "clangd-lsp.py")], self.root),
            "python": LspClient("python", [sys.executable, "-B", str(HERE / "python-lsp.py")], self.root),
        }
        reaper = threading.Thread(target=self._reap_idle, daemon=True)
        reaper.start()

    def _reap_idle(self) -> None:
        while True:
            time.sleep(max(1.0, min(30.0, self.idle_seconds / 2)))
            now = time.monotonic()
            for client in self.clients.values():
                if client.proc is not None and now - client.last_used > self.idle_seconds:
                    client.stop()

    def shutdown_all(self) -> None:
        for client in self.clients.values():
            client.stop()

    def _resolve_file(self, rel_or_abs: str) -> tuple[Path, LspClient]:
        p = Path(rel_or_abs)
        target = p.resolve() if p.is_absolute() else (self.root / p).resolve()
        if not target.is_relative_to(self.root):
            raise ValueError(f"file must lie inside {self.root}: {rel_or_abs}")
        if not target.is_file():
            raise FileNotFoundError(f"file does not exist: {rel_or_abs}")
        ext = target.suffix.lower()
        if ext in CPP_EXTS:
            return target, self.clients["cpp"]
        if ext in PY_EXTS:
            return target, self.clients["python"]
        raise ValueError(f"unsupported file extension {ext!r} (expected C/C++ or Python): {rel_or_abs}")

    def tool_diagnostics(self, args: dict) -> str:
        target, client = self._resolve_file(str(args["file"]))
        diags = client.diagnostics_for(target)
        rel = target.relative_to(self.root).as_posix()
        if not diags:
            return f"{rel}: 0 diagnostics"
        lines = [f"{rel}: {len(diags)} diagnostic(s)"]
        for d in diags[:50]:
            pos = ((d.get("range") or {}).get("start") or {})
            ln = pos.get("line", 0) + 1
            col = pos.get("character", 0) + 1
            sev = SEVERITY_NAMES.get(d.get("severity", 1), "diagnostic")
            lines.append(f"  {rel}:{ln}:{col}: {sev}: {d.get('message', '').strip()}")
        return "\n".join(lines)

    def tool_symbols(self, args: dict) -> str:
        raw = str(args.get("file_or_query") or "").strip()
        cand = (self.root / raw).resolve() if raw else None
        if cand is not None and cand.is_file() and cand.is_relative_to(self.root):
            target, client = self._resolve_file(raw)
            uri, *_ = client.sync_file(target)
            res = client.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}}) or []
            rel = target.relative_to(self.root).as_posix()
            lines: list[str] = []

            CONTAINER_KINDS = {2, 3, 4, 5, 10, 11, 23}

            def walk(items: list[dict], prefix: str = "") -> None:
                for item in items:
                    rng = (item.get("selectionRange") or item.get("range") or
                           (item.get("location") or {}).get("range") or {})
                    ln = (rng.get("start") or {}).get("line", 0) + 1
                    raw_kind = item.get("kind", 0)
                    kind = SYMBOL_KINDS.get(raw_kind, "symbol")
                    name = f"{prefix}{item.get('name', '?')}"
                    lines.append(f"{rel}:{ln} [{kind}] {name}")
                    if raw_kind in CONTAINER_KINDS:
                        walk(item.get("children") or [], prefix=f"{name}.")

            walk(res)
            return "\n".join(lines[:150]) if lines else f"{rel}: no symbols"

        lang = str(args.get("language") or "python").lower()
        client = self.clients["cpp" if lang in ("cpp", "c++", "c") else "python"]
        if client.lang == "cpp":
            seed = next((self.root / "src/pdf").glob("*.cpp"), None)
            if seed is not None:
                client.sync_file(seed)
        else:
            client.ensure_started()
        res = client.request("workspace/symbol", {"query": raw}) or []
        if not res:
            return f"no workspace symbols matching {raw!r}"
        lines = []
        for item in res[:100]:
            loc = item.get("location") or {}
            ln = ((loc.get("range") or {}).get("start") or {}).get("line", 0) + 1
            kind = SYMBOL_KINDS.get(item.get("kind", 0), "symbol")
            lines.append(f"{uri_to_rel(loc.get('uri', ''), self.root)}:{ln} [{kind}] {item.get('name', '?')}")
        return "\n".join(lines)

    def _pos_request(self, method: str, args: dict, extra_params: dict | None = None):
        target, client = self._resolve_file(str(args["file"]))
        uri, *_ = client.sync_file(target)
        line = max(0, int(args.get("line", 1)) - 1)
        col = max(0, int(args.get("character", 1)) - 1)
        params = {"textDocument": {"uri": uri}, "position": {"line": line, "character": col}}
        if extra_params:
            params.update(extra_params)
        return client.request(method, params)

    def _format_locations(self, res) -> str:
        if not res:
            return "no locations found"
        items = res if isinstance(res, list) else [res]
        lines = []
        for item in items[:100]:
            uri = item.get("targetUri") or item.get("uri") or ""
            rng = item.get("targetSelectionRange") or item.get("range") or {}
            start = rng.get("start") or {}
            lines.append(f"{uri_to_rel(uri, self.root)}:{start.get('line', 0) + 1}:{start.get('character', 0) + 1}")
        return "\n".join(lines)

    def tool_definition(self, args: dict) -> str:
        return self._format_locations(self._pos_request("textDocument/definition", args))

    def tool_references(self, args: dict) -> str:
        return self._format_locations(
            self._pos_request("textDocument/references", args, {"context": {"includeDeclaration": True}})
        )

    def tool_hover(self, args: dict) -> str:
        res = self._pos_request("textDocument/hover", args)
        if not res:
            return "no hover info"
        contents = res.get("contents")
        if isinstance(contents, dict):
            return str(contents.get("value", "")).strip() or "no hover info"
        if isinstance(contents, list):
            parts = [c.get("value", "") if isinstance(c, dict) else str(c) for c in contents]
            return "\n\n".join(p.strip() for p in parts if p.strip()) or "no hover info"
        return str(contents).strip() or "no hover info"

    TOOLS = [
        {
            "name": "lsp_diagnostics",
            "description": "Return C++23 (clangd) or Python compiler and type diagnostics for a repository file.",
            "inputSchema": {
                "type": "object",
                "properties": {"file": {"type": "string", "description": "Repository-relative file path"}},
                "required": ["file"],
            },
        },
        {
            "name": "lsp_symbols",
            "description": "List document symbols when given a file path, or search workspace symbols by query.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file_or_query": {"type": "string", "description": "Relative file path or symbol query"},
                    "language": {"type": "string", "enum": ["cpp", "python"], "description": "Language for workspace query"},
                },
                "required": ["file_or_query"],
            },
        },
        {
            "name": "lsp_definition",
            "description": "Jump to definition at 1-based line and character in a C++23 or Python file.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file": {"type": "string"},
                    "line": {"type": "integer", "minimum": 1},
                    "character": {"type": "integer", "minimum": 1},
                },
                "required": ["file", "line", "character"],
            },
        },
        {
            "name": "lsp_references",
            "description": "Find all references to the symbol at 1-based line and character.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file": {"type": "string"},
                    "line": {"type": "integer", "minimum": 1},
                    "character": {"type": "integer", "minimum": 1},
                },
                "required": ["file", "line", "character"],
            },
        },
        {
            "name": "lsp_hover",
            "description": "Return type signature and documentation at 1-based line and character.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file": {"type": "string"},
                    "line": {"type": "integer", "minimum": 1},
                    "character": {"type": "integer", "minimum": 1},
                },
                "required": ["file", "line", "character"],
            },
        },
    ]

    def handle_rpc(self, req: dict) -> dict | None:
        method = req.get("method")
        req_id = req.get("id")
        params = req.get("params") or {}
        if method == "initialize":
            return {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "protocolVersion": params.get("protocolVersion") or "2024-11-05",
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "recto-lsp", "version": "1.0.0"},
                },
            }
        if method == "tools/list":
            return {"jsonrpc": "2.0", "id": req_id, "result": {"tools": self.TOOLS}}
        if method == "tools/call":
            name = str(params.get("name") or "")
            raw_args = params.get("arguments")
            args = raw_args if isinstance(raw_args, dict) else {}
            dispatch = {
                "lsp_diagnostics": self.tool_diagnostics,
                "lsp_symbols": self.tool_symbols,
                "lsp_definition": self.tool_definition,
                "lsp_references": self.tool_references,
                "lsp_hover": self.tool_hover,
            }
            fn = dispatch.get(name)
            if fn is None:
                return {"jsonrpc": "2.0", "id": req_id,
                        "error": {"code": -32601, "message": f"unknown tool: {name}"}}
            try:
                out = fn(args)
                return {"jsonrpc": "2.0", "id": req_id,
                        "result": {"content": [{"type": "text", "text": out}], "isError": False}}
            except Exception as exc:
                return {"jsonrpc": "2.0", "id": req_id,
                        "result": {"content": [{"type": "text", "text": f"error: {exc}"}], "isError": True}}
        if method == "ping":
            return {"jsonrpc": "2.0", "id": req_id, "result": {}}
        if req_id is not None:
            return {"jsonrpc": "2.0", "id": req_id, "error": {"code": -32601, "message": f"unknown method: {method}"}}
        return None

    def serve_stdio(self) -> int:
        """Support both newline-delimited JSON-RPC (MCP 2024-11-05 / 2025-03-26 stdio) and Content-Length framing."""
        try:
            while True:
                first = sys.stdin.buffer.readline()
                if not first:
                    break
                stripped = first.strip()
                if not stripped:
                    continue
                if stripped.lower().startswith(b"content-length:"):
                    length = int(stripped.split(b":", 1)[1].strip())
                    while True:
                        hdr = sys.stdin.buffer.readline()
                        if not hdr or hdr in (b"\r\n", b"\n"):
                            break
                    raw = sys.stdin.buffer.read(length)
                    framed = True
                else:
                    raw = stripped
                    framed = False
                try:
                    req = json.loads(raw.decode("utf-8"))
                except ValueError:
                    continue
                reply = self.handle_rpc(req)
                if reply is not None:
                    encoded = json.dumps(reply).encode("utf-8")
                    if framed:
                        sys.stdout.buffer.write(f"Content-Length: {len(encoded)}\r\n\r\n".encode("ascii") + encoded)
                    else:
                        sys.stdout.buffer.write(encoded + b"\n")
                    sys.stdout.buffer.flush()
        finally:
            self.shutdown_all()
        return 0


def main() -> int:
    return LspMcpServer(ROOT).serve_stdio()


if __name__ == "__main__":
    sys.exit(main())
