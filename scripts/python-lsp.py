#!/usr/bin/env python3
"""Portable Python LSP launcher with zero-dependency stdlib fallback.

    python scripts/python-lsp.py [--check] [--builtin]

When `pyright-langserver` is available (`RECTO_PYRIGHT_LANGSERVER`, `PATH`, `~/.local/bin`, or `%APPDATA%/npm`) and
`--builtin` is not passed, forwards stdio to `pyright-langserver --stdio`. Otherwise runs a self-contained pure-stdlib
Python Language Server supporting diagnostics (`ast.parse` + `compile`), document symbols, workspace symbols, go-to
definition, find references, and hover (`ast.get_docstring` + signature) so fresh clones work with zero external
packages.
"""
from __future__ import annotations

import ast
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tokenize
from urllib.parse import unquote, urlparse
import warnings

ROOT = Path(__file__).resolve().parents[1]


def windowless() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def find_pyright() -> str | None:
    override = os.environ.get("RECTO_PYRIGHT_LANGSERVER")
    if override and Path(override).is_file():
        return str(Path(override))
    found = shutil.which("pyright-langserver")
    if found:
        return found
    home = Path.home()
    candidates = [
        home / ".local" / "bin" / ("pyright-langserver.exe" if os.name == "nt" else "pyright-langserver"),
        home / ".local" / "bin" / "pyright-langserver.cmd",
    ]
    appdata = os.environ.get("APPDATA")
    if appdata:
        candidates.append(Path(appdata) / "npm" / "pyright-langserver.cmd")
    for cand in candidates:
        if cand.is_file():
            return str(cand)
    return None


def uri_to_path(uri: str) -> Path:
    parsed = urlparse(uri)
    raw = unquote(parsed.path)
    if os.name == "nt" and raw.startswith("/") and len(raw) >= 3 and raw[2] == ":":
        raw = raw[1:]
    return Path(raw).resolve()


def path_to_uri(path: Path) -> str:
    return path.resolve().as_uri()


class StdlibPythonLsp:
    """Lightweight, zero-dependency LSP server over stdio using Python's `ast` and `tokenize` modules."""

    def __init__(self, root: Path = ROOT):
        self.root = root
        self.docs: dict[Path, str] = {}
        self.running = True

    def _read_msg(self) -> dict | None:
        headers: dict[str, str] = {}
        while True:
            line = sys.stdin.buffer.readline()
            if not line:
                return None
            if line in (b"\r\n", b"\n"):
                break
            decoded = line.decode("ascii", "replace").strip()
            if ":" in decoded:
                k, v = decoded.split(":", 1)
                headers[k.strip().lower()] = v.strip()
        length = int(headers.get("content-length", "0"))
        if length <= 0:
            return None
        body = sys.stdin.buffer.read(length)
        if not body:
            return None
        return json.loads(body.decode("utf-8"))

    def _send(self, payload: dict) -> None:
        raw = json.dumps(payload).encode("utf-8")
        sys.stdout.buffer.write(f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii") + raw)
        sys.stdout.buffer.flush()

    def _text(self, path: Path) -> str:
        if path in self.docs:
            return self.docs[path]
        try:
            return path.read_text(encoding="utf-8")
        except OSError:
            return ""

    def _workspace_py_files(self) -> list[Path]:
        files: list[Path] = []
        for sub in ("scripts", "tests/scripts"):
            folder = self.root / sub
            if folder.is_dir():
                files.extend(sorted(p.resolve() for p in folder.rglob("*.py") if "__pycache__" not in p.parts))
        for extra in self.docs:
            if extra not in files and extra.is_file():
                files.append(extra)
        return files

    def _diagnostics(self, path: Path, text: str) -> list[dict]:
        diags: list[dict] = []
        try:
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", SyntaxWarning)
                tree = ast.parse(text, filename=str(path))
                compile(tree, str(path), "exec")
        except SyntaxError as exc:
            ln = max(0, (exc.lineno or 1) - 1)
            col = max(0, (exc.offset or 1) - 1)
            end_ln = max(ln, (getattr(exc, "end_lineno", None) or (ln + 1)) - 1)
            end_col = max(col + 1, (getattr(exc, "end_offset", None) or (col + 2)) - 1)
            diags.append({
                "range": {"start": {"line": ln, "character": col}, "end": {"line": end_ln, "character": end_col}},
                "severity": 1,
                "source": "recto-python-lsp",
                "message": exc.msg or str(exc),
            })
        return diags

    @staticmethod
    def _doc_symbols(text: str) -> list[dict]:
        try:
            tree = ast.parse(text)
        except SyntaxError:
            return []

        def node_sym(node: ast.AST) -> dict | None:
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                kind = 12
                name = node.name
            elif isinstance(node, ast.ClassDef):
                kind = 5
                name = node.name
            elif isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
                kind = 13
                name = node.targets[0].id
            else:
                return None
            start_ln = max(0, getattr(node, "lineno", 1) - 1)
            start_col = max(0, getattr(node, "col_offset", 0))
            end_ln = max(start_ln, getattr(node, "end_lineno", start_ln + 1) - 1)
            end_col = max(start_col + 1, getattr(node, "end_col_offset", start_col + len(name)))
            rng = {"start": {"line": start_ln, "character": start_col},
                   "end": {"line": end_ln, "character": end_col}}
            children = []
            if isinstance(node, ast.ClassDef):
                for child in node.body:
                    cs = node_sym(child)
                    if cs is not None:
                        children.append(cs)
            out = {"name": name, "kind": kind, "range": rng, "selectionRange": rng}
            if children:
                out["children"] = children
            return out

        out_syms = []
        for stmt in tree.body:
            sym = node_sym(stmt)
            if sym is not None:
                out_syms.append(sym)
        return out_syms

    @staticmethod
    def _word_at(text: str, line: int, character: int) -> str:
        lines = text.splitlines()
        if line < 0 or line >= len(lines):
            return ""
        row = lines[line]
        for m in re.finditer(r"[A-Za-z_][A-Za-z0-9_]*", row):
            if m.start() <= character <= m.end():
                return m.group(0)
        return ""

    def _find_defs(self, ident: str, preferred: Path) -> list[tuple[Path, ast.AST]]:
        if not ident:
            return []
        ordered = [preferred, *(p for p in self._workspace_py_files() if p != preferred)]
        matches: list[tuple[Path, ast.AST]] = []
        for p in ordered:
            try:
                tree = ast.parse(self._text(p))
            except SyntaxError:
                continue
            for node in ast.walk(tree):
                if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)) and node.name == ident:
                    matches.append((p, node))
                elif isinstance(node, ast.Assign):
                    for t in node.targets:
                        if isinstance(t, ast.Name) and t.id == ident and getattr(t, "col_offset", 1) == 0:
                            matches.append((p, t))
        return matches

    def _hover(self, path: Path, line: int, character: int) -> dict | None:
        text = self._text(path)
        ident = self._word_at(text, line, character)
        defs = self._find_defs(ident, path)
        if not defs:
            return None
        def_path, node = defs[0]
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            sig = f"def {node.name}({ast.unparse(node.args)})"
            if node.returns is not None:
                sig += f" -> {ast.unparse(node.returns)}"
            doc = ast.get_docstring(node) or ""
            md = f"```python\n{sig}\n```" + (f"\n\n{doc}" if doc else "")
        elif isinstance(node, ast.ClassDef):
            doc = ast.get_docstring(node) or ""
            md = f"```python\nclass {node.name}\n```" + (f"\n\n{doc}" if doc else "")
        else:
            md = f"```python\n{ident}  # {def_path.name}:{getattr(node, 'lineno', 1)}\n```"
        return {"contents": {"kind": "markdown", "value": md}}

    def _definitions(self, path: Path, line: int, character: int) -> list[dict]:
        ident = self._word_at(self._text(path), line, character)
        locs = []
        for def_path, node in self._find_defs(ident, path):
            ln = max(0, getattr(node, "lineno", 1) - 1)
            col = max(0, getattr(node, "col_offset", 0))
            locs.append({
                "uri": path_to_uri(def_path),
                "range": {"start": {"line": ln, "character": col},
                          "end": {"line": ln, "character": col + len(ident)}},
            })
        return locs

    def _references(self, path: Path, line: int, character: int) -> list[dict]:
        ident = self._word_at(self._text(path), line, character)
        if not ident:
            return []
        refs: list[dict] = []
        for p in [path, *(f for f in self._workspace_py_files() if f != path)]:
            src = self._text(p)
            if ident not in src:
                continue
            try:
                lines_iter = iter(src.splitlines(keepends=True))
                for tok in tokenize.generate_tokens(lambda it=lines_iter: next(it)):
                    if tok.type == tokenize.NAME and tok.string == ident:
                        ln = tok.start[0] - 1
                        col = tok.start[1]
                        refs.append({
                            "uri": path_to_uri(p),
                            "range": {"start": {"line": ln, "character": col},
                                      "end": {"line": ln, "character": col + len(ident)}},
                        })
            except tokenize.TokenError:
                continue
        return refs

    def serve(self) -> int:
        while self.running:
            msg = self._read_msg()
            if msg is None:
                break
            method = msg.get("method")
            msg_id = msg.get("id")
            params = msg.get("params") or {}
            if method == "initialize":
                root_uri = params.get("rootUri")
                if root_uri:
                    self.root = uri_to_path(root_uri)
                self._send({
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "result": {
                        "capabilities": {
                            "textDocumentSync": 1,
                            "documentSymbolProvider": True,
                            "workspaceSymbolProvider": True,
                            "definitionProvider": True,
                            "referencesProvider": True,
                            "hoverProvider": True,
                        },
                        "serverInfo": {"name": "recto-stdlib-python-lsp", "version": "1.0"},
                    },
                })
            elif method == "shutdown":
                self._send({"jsonrpc": "2.0", "id": msg_id, "result": None})
            elif method == "exit":
                return 0
            elif method == "textDocument/didOpen":
                td = params.get("textDocument") or {}
                uri = td.get("uri", "")
                text = td.get("text", "")
                version = td.get("version")
                p = uri_to_path(uri)
                self.docs[p] = text
                pub_params: dict = {"uri": uri, "diagnostics": self._diagnostics(p, text)}
                if isinstance(version, int):
                    pub_params["version"] = version
                self._send({"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics", "params": pub_params})
            elif method == "textDocument/didChange":
                td = params.get("textDocument") or {}
                uri = td.get("uri", "")
                version = td.get("version")
                changes = params.get("contentChanges") or []
                if changes:
                    p = uri_to_path(uri)
                    text = changes[-1].get("text", "")
                    self.docs[p] = text
                    pub_params = {"uri": uri, "diagnostics": self._diagnostics(p, text)}
                    if isinstance(version, int):
                        pub_params["version"] = version
                    self._send({"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics", "params": pub_params})
            elif method == "textDocument/didClose":
                td = params.get("textDocument") or {}
                uri = td.get("uri", "")
                if uri:
                    self.docs.pop(uri_to_path(uri), None)
            elif method == "textDocument/documentSymbol":
                p = uri_to_path((params.get("textDocument") or {}).get("uri", ""))
                self._send({"jsonrpc": "2.0", "id": msg_id, "result": self._doc_symbols(self._text(p))})
            elif method == "workspace/symbol":
                query = (params.get("query") or "").lower()
                found = []
                for p in self._workspace_py_files():
                    for s in self._doc_symbols(self._text(p)):
                        if not query or query in s["name"].lower():
                            found.append({
                                "name": s["name"],
                                "kind": s["kind"],
                                "location": {"uri": path_to_uri(p), "range": s["range"]},
                            })
                self._send({"jsonrpc": "2.0", "id": msg_id, "result": found[:100]})
            elif method == "textDocument/definition":
                p = uri_to_path((params.get("textDocument") or {}).get("uri", ""))
                pos = params.get("position") or {}
                self._send({"jsonrpc": "2.0", "id": msg_id,
                            "result": self._definitions(p, pos.get("line", 0), pos.get("character", 0))})
            elif method == "textDocument/references":
                p = uri_to_path((params.get("textDocument") or {}).get("uri", ""))
                pos = params.get("position") or {}
                self._send({"jsonrpc": "2.0", "id": msg_id,
                            "result": self._references(p, pos.get("line", 0), pos.get("character", 0))})
            elif method == "textDocument/hover":
                p = uri_to_path((params.get("textDocument") or {}).get("uri", ""))
                pos = params.get("position") or {}
                self._send({"jsonrpc": "2.0", "id": msg_id,
                            "result": self._hover(p, pos.get("line", 0), pos.get("character", 0))})
            elif msg_id is not None:
                self._send({"jsonrpc": "2.0", "id": msg_id, "result": None})
        return 0


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    force_builtin = "--builtin" in args
    pyright = None if force_builtin else find_pyright()
    if "--check" in args:
        backend = f"pyright-langserver ({pyright})" if pyright else "stdlib-python-lsp (built-in fallback)"
        print(f"python-lsp: {backend}")
        return 0
    if pyright:
        cmd = [pyright, "--stdio"]
        if os.name == "nt" and pyright.lower().endswith((".cmd", ".bat")):
            cmd = ["cmd.exe", "/d", "/s", "/c", pyright, "--stdio"]
        proc = subprocess.Popen(cmd, stdin=sys.stdin, stdout=sys.stdout, stderr=sys.stderr, creationflags=windowless())
        _job = None
        if os.name == "nt":
            try:
                import importlib.util
                spec = importlib.util.spec_from_file_location("clangd_lsp", Path(__file__).with_name("clangd-lsp.py"))
                if spec and spec.loader:
                    mod = importlib.util.module_from_spec(spec)
                    spec.loader.exec_module(mod)
                    _job = mod.bind_kill_on_close_job(proc)
            except Exception:
                _job = None
        try:
            return proc.wait()
        except KeyboardInterrupt:
            proc.terminate()
            return proc.wait()
    return StdlibPythonLsp(ROOT).serve()


if __name__ == "__main__":
    sys.exit(main())
