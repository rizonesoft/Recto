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
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tokenize
import unicodedata
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


def _read_python_source(path: Path) -> str:
    """Decode Python source from disk per PEP 263 (UTF-8 BOM and coding cookies)."""
    try:
        with tokenize.open(path) as handle:
            return handle.read()
    except (SyntaxError, LookupError, UnicodeDecodeError):
        try:
            return path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return ""
    except OSError:
        return ""


class StdlibPythonLsp:
    """Lightweight, zero-dependency LSP server over stdio using Python's `ast` and `tokenize` modules."""

    def __init__(self, root: Path = ROOT):
        self.root = root
        self.docs: dict[Path, str] = {}
        self._ast_cache: dict[Path, tuple[int, str, ast.Module | None]] = {}
        self.running = True

    MAX_FRAME_BYTES = 16 * 1024 * 1024

    def _read_msg(self) -> dict | None:
        headers: dict[str, str] = {}
        saw_any = False
        while True:
            line = sys.stdin.buffer.readline(65536)
            if not line:
                return None
            saw_any = True
            if line in (b"\r\n", b"\n"):
                break
            decoded = line.decode("ascii", "replace").strip()
            if ":" in decoded:
                k, v = decoded.split(":", 1)
                headers[k.strip().lower()] = v.strip()
        if not headers:
            return None if not saw_any else {"_malformed": "missing Content-Length"}
        try:
            length = int(headers.get("content-length", "0"))
        except ValueError:
            return {"_malformed": "invalid Content-Length"}
        if length <= 0 or length > self.MAX_FRAME_BYTES:
            return {"_malformed": "invalid or oversized Content-Length"}
        body = sys.stdin.buffer.read(length)
        if not body:
            return None
        try:
            parsed = json.loads(body.decode("utf-8"))
        except (UnicodeDecodeError, ValueError, RecursionError):
            return {"_malformed": "parse error"}
        if not isinstance(parsed, dict):
            return {"_malformed": "invalid request"}
        return parsed

    def _send(self, payload: dict) -> None:
        raw = json.dumps(payload).encode("utf-8")
        sys.stdout.buffer.write(f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii") + raw)
        sys.stdout.buffer.flush()

    @staticmethod
    def _normalize_python_text(text: str) -> str:
        """Strip a PEP 263-valid initial UTF-8 BOM (`U+FEFF`), keeping conflicting BOM+cookie headers intact."""
        if not text.startswith("\ufeff"):
            return text
        head = "".join(text.splitlines(keepends=True)[:2]).encode("utf-8")
        try:
            tokenize.detect_encoding(io.BytesIO(head).readline)
        except (SyntaxError, LookupError):
            return text
        return text[1:]

    def _text(self, path: Path) -> str:
        resolved = path.resolve()
        if resolved in self.docs:
            return self._normalize_python_text(self.docs[resolved])
        if path in self.docs:
            return self._normalize_python_text(self.docs[path])
        return _read_python_source(resolved)

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

    @staticmethod
    def _utf16_len(text: str) -> int:
        return len(text.encode("utf-16-le")) // 2

    @staticmethod
    def _utf8_col_to_utf16(line_text: str, utf8_col: int) -> int:
        if utf8_col <= 0 or not line_text:
            return 0
        prefix = line_text.encode("utf-8")[:utf8_col].decode("utf-8", errors="ignore")
        return len(prefix.encode("utf-16-le")) // 2

    @staticmethod
    def _codepoint_col_to_utf16(line_text: str, cp_col: int) -> int:
        if cp_col <= 0 or not line_text:
            return 0
        return len(line_text[:cp_col].encode("utf-16-le")) // 2

    @staticmethod
    def _utf16_to_codepoint_and_utf8(line_text: str, utf16_col: int) -> tuple[int, int]:
        if utf16_col <= 0 or not line_text:
            return 0, 0
        units = 0
        for idx, ch in enumerate(line_text):
            units += 2 if ord(ch) > 0xFFFF else 1
            if units > utf16_col:
                return idx, len(line_text[:idx].encode("utf-8"))
            if units == utf16_col:
                cp = idx + 1
                return cp, len(line_text[:cp].encode("utf-8"))
        return len(line_text), len(line_text.encode("utf-8"))

    def _diagnostics(self, path: Path, text: str) -> list[dict]:
        diags: list[dict] = []
        raw_text = text
        text = self._normalize_python_text(raw_text)
        try:
            head = "".join(raw_text.splitlines(keepends=True)[:2]).encode("utf-8")
            tokenize.detect_encoding(io.BytesIO(head).readline)
            if "\ufffd" in raw_text and path.is_file():
                try:
                    fallback_disk = path.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    fallback_disk = None
                if fallback_disk == raw_text:
                    try:
                        with tokenize.open(path) as handle:
                            handle.read()
                    except UnicodeDecodeError:
                        try:
                            ast.parse(path.read_bytes(), filename=str(path))
                        except OSError:
                            pass
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", SyntaxWarning)
                tree = ast.parse(text, filename=str(path))
                compile(tree, str(path), "exec")
        except SyntaxError as exc:
            lines = text.splitlines()
            ln = max(0, (exc.lineno or 1) - 1)
            cp_col = max(0, (exc.offset or 1) - 1)
            end_ln = max(ln, (getattr(exc, "end_lineno", None) or (ln + 1)) - 1)
            raw_end_offset = getattr(exc, "end_offset", None)
            cp_end = max(cp_col + 1 if end_ln == ln else 0, (raw_end_offset - 1) if raw_end_offset else (cp_col + 1))
            start_row = lines[ln] if ln < len(lines) else ""
            end_row = lines[end_ln] if end_ln < len(lines) else ""
            u16_start = self._codepoint_col_to_utf16(start_row, cp_col)
            u16_end = max(
                u16_start + 1 if end_ln == ln else 0,
                self._codepoint_col_to_utf16(end_row, cp_end),
            )
            diags.append({
                "range": {
                    "start": {"line": ln, "character": u16_start},
                    "end": {"line": end_ln, "character": u16_end},
                },
                "severity": 1,
                "source": "recto-python-lsp",
                "message": exc.msg or str(exc),
            })
        return diags

    @classmethod
    def _doc_symbols(cls, text: str) -> list[dict]:
        text = cls._normalize_python_text(text)
        try:
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", SyntaxWarning)
                tree = ast.parse(text)
        except SyntaxError:
            return []
        lines = text.splitlines()

        def node_sym(node: ast.AST) -> dict | None:
            sel_node: ast.AST = node
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                kind = 12
                name = node.name
            elif isinstance(node, ast.ClassDef):
                kind = 5
                name = node.name
            elif isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
                kind = 13
                name = node.targets[0].id
                sel_node = node.targets[0]
            else:
                return None
            start_ln = max(0, getattr(node, "lineno", 1) - 1)
            start_utf8 = max(0, getattr(node, "col_offset", 0))
            end_ln = max(start_ln, getattr(node, "end_lineno", start_ln + 1) - 1)
            end_utf8 = max(start_utf8 + 1, getattr(node, "end_col_offset", start_utf8 + len(name.encode("utf-8"))))
            start_row = lines[start_ln] if start_ln < len(lines) else ""
            end_row = lines[end_ln] if end_ln < len(lines) else ""
            start_col = cls._utf8_col_to_utf16(start_row, start_utf8)
            end_col = max(start_col + 1 if end_ln == start_ln else 0, cls._utf8_col_to_utf16(end_row, end_utf8))
            rng = {"start": {"line": start_ln, "character": start_col},
                   "end": {"line": end_ln, "character": end_col}}

            sel_ln = max(0, getattr(sel_node, "lineno", start_ln + 1) - 1)
            sel_row = lines[sel_ln] if sel_ln < len(lines) else ""
            sel_utf8 = max(0, getattr(sel_node, "col_offset", start_utf8))
            sel_start = cls._utf8_col_to_utf16(sel_row, sel_utf8)
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                cp_start, _ = cls._utf16_to_codepoint_and_utf8(sel_row, sel_start)
                hdr_match = re.match(r"(?:async\s+def|def|class)\s+", sel_row[cp_start:])
                if hdr_match:
                    sel_start = cls._codepoint_col_to_utf16(sel_row, cp_start + hdr_match.end())
            sel_rng = {
                "start": {"line": sel_ln, "character": sel_start},
                "end": {"line": sel_ln, "character": sel_start + max(1, cls._utf16_len(name))},
            }
            children = []
            if isinstance(node, ast.ClassDef):
                for child in node.body:
                    cs = node_sym(child)
                    if cs is not None:
                        children.append(cs)
            out = {"name": name, "kind": kind, "range": rng, "selectionRange": sel_rng}
            if children:
                out["children"] = children
            return out

        out_syms = []
        for stmt in tree.body:
            sym = node_sym(stmt)
            if sym is not None:
                out_syms.append(sym)
        return out_syms

    @classmethod
    def _word_at(cls, text: str, line: int, character: int) -> str:
        text = cls._normalize_python_text(text)
        lines = text.splitlines()
        if line < 0 or line >= len(lines):
            return ""
        row = lines[line]
        cp_col, _ = cls._utf16_to_codepoint_and_utf8(row, character)
        for m in re.finditer(r"\w+", row):
            tok = m.group(0)
            if m.start() <= cp_col <= m.end() and tok.isidentifier():
                return unicodedata.normalize("NFKC", tok)
        return ""

    @classmethod
    def _cursor_symbol(cls, text: str, line: int, character: int) -> tuple[str | None, str, bool, int]:
        """Return `(receiver_chain, ident, is_attribute_access, utf8_col)` at 0-based `(line, utf16_character)`."""
        text = cls._normalize_python_text(text)
        lines = text.splitlines()
        if line < 0 or line >= len(lines):
            return None, "", False, 0
        row = lines[line]
        cp_col, utf8_col = cls._utf16_to_codepoint_and_utf8(row, character)
        for m in re.finditer(r"\w+", row):
            raw_tok = m.group(0)
            if m.start() <= cp_col <= m.end() and raw_tok.isidentifier():
                ident = unicodedata.normalize("NFKC", raw_tok)
                prefix = row[:m.start()]
                is_attr = prefix.rstrip().endswith(".")
                if re.search(r"\bsuper\s*\(\s*\)\s*\.\s*$", prefix):
                    return "super()", ident, True, utf8_col
                recv_match = re.search(
                    r"([^\W\d]\w*(?:\s*\.\s*[^\W\d]\w*)*)\s*\.\s*$",
                    prefix,
                )
                if recv_match:
                    raw_chain = re.sub(r"\s+", "", recv_match.group(1))
                    segs = [unicodedata.normalize("NFKC", s) for s in raw_chain.split(".")]
                    if all(s.isidentifier() for s in segs):
                        return ".".join(segs), ident, True, utf8_col
                return None, ident, is_attr, utf8_col
        return None, "", False, utf8_col

    def _parse_file(self, path: Path) -> ast.Module | None:
        key = path.resolve()
        text = self._text(path)
        cached = self._ast_cache.get(key)
        if cached is not None and cached[0] == len(text) and cached[1] == text:
            return cached[2]
        try:
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", SyntaxWarning)
                tree: ast.Module | None = ast.parse(text, filename=str(path))
        except SyntaxError:
            tree = None
        self._ast_cache[key] = (len(text), text, tree)
        return tree

    @staticmethod
    def _iter_scope_stmts(stmts: list[ast.stmt]) -> list[ast.stmt]:
        """Flatten control-flow blocks inside a single scope without descending into nested functions or classes."""
        flat: list[ast.stmt] = []
        for stmt in stmts:
            flat.append(stmt)
            if isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                continue
            for attr in ("body", "orelse", "finalbody"):
                sub = getattr(stmt, attr, None)
                if isinstance(sub, list) and sub and isinstance(sub[0], ast.stmt):
                    flat.extend(StdlibPythonLsp._iter_scope_stmts(sub))
            handlers = getattr(stmt, "handlers", None)
            if isinstance(handlers, list):
                for h in handlers:
                    h_body = getattr(h, "body", None)
                    if isinstance(h_body, list):
                        flat.extend(StdlibPythonLsp._iter_scope_stmts(h_body))
            cases = getattr(stmt, "cases", None)
            if isinstance(cases, list):
                for c in cases:
                    c_body = getattr(c, "body", None)
                    if isinstance(c_body, list):
                        flat.extend(StdlibPythonLsp._iter_scope_stmts(c_body))
        return flat

    @staticmethod
    def _eval_path_expr(
        expr: ast.AST,
        current_file: Path,
        path_consts: dict[str, Path],
        str_consts: dict[str, str],
    ) -> tuple[Path | None, bool]:
        """Evaluate static `Path` expressions (`HERE / 'lib' / 'unattended.py'`, `Path(__file__).parents[2]`, etc.)."""
        if isinstance(expr, ast.Constant) and isinstance(expr.value, str):
            return Path(expr.value), False
        if isinstance(expr, ast.Name):
            if expr.id == "__file__":
                return current_file.resolve(), True
            if expr.id in path_consts:
                return path_consts[expr.id], True
            if expr.id in str_consts:
                return Path(str_consts[expr.id]), False
            return None, False
        if isinstance(expr, ast.Call):
            if (
                (isinstance(expr.func, ast.Name) and expr.func.id == "Path")
                or (
                    isinstance(expr.func, ast.Attribute)
                    and isinstance(expr.func.value, ast.Name)
                    and expr.func.value.id == "pathlib"
                    and expr.func.attr == "Path"
                )
            ):
                if expr.args:
                    sub, _ = StdlibPythonLsp._eval_path_expr(expr.args[0], current_file, path_consts, str_consts)
                    if sub is not None:
                        return sub, True
                return None, False
            if isinstance(expr.func, ast.Attribute):
                base, is_p = StdlibPythonLsp._eval_path_expr(expr.func.value, current_file, path_consts, str_consts)
                if base is not None and is_p:
                    if expr.func.attr in ("resolve", "absolute", "expanduser"):
                        return base.resolve(), True
                    if expr.func.attr == "with_name" and len(expr.args) == 1:
                        arg_p, _ = StdlibPythonLsp._eval_path_expr(expr.args[0], current_file, path_consts, str_consts)
                        if arg_p is not None:
                            return base.with_name(str(arg_p)), True
                    if expr.func.attr == "with_suffix" and len(expr.args) == 1:
                        arg_p, _ = StdlibPythonLsp._eval_path_expr(expr.args[0], current_file, path_consts, str_consts)
                        if arg_p is not None:
                            return base.with_suffix(str(arg_p)), True
                    if expr.func.attr == "joinpath":
                        cur = base
                        for a in expr.args:
                            part, _ = StdlibPythonLsp._eval_path_expr(a, current_file, path_consts, str_consts)
                            if part is None:
                                return None, False
                            cur = cur / part
                        return cur, True
        if isinstance(expr, ast.Attribute) and expr.attr == "parent":
            base, is_p = StdlibPythonLsp._eval_path_expr(expr.value, current_file, path_consts, str_consts)
            if base is not None and is_p:
                return base.parent, True
        if (
            isinstance(expr, ast.Subscript)
            and isinstance(expr.value, ast.Attribute)
            and expr.value.attr == "parents"
            and isinstance(expr.slice, ast.Constant)
            and isinstance(expr.slice.value, int)
        ):
            base, is_p = StdlibPythonLsp._eval_path_expr(expr.value.value, current_file, path_consts, str_consts)
            idx = expr.slice.value
            if base is not None and is_p and 0 <= idx < len(base.parents):
                return base.parents[idx], True
        if isinstance(expr, ast.BinOp) and isinstance(expr.op, ast.Div):
            left, lp = StdlibPythonLsp._eval_path_expr(expr.left, current_file, path_consts, str_consts)
            right, rp = StdlibPythonLsp._eval_path_expr(expr.right, current_file, path_consts, str_consts)
            if left is not None and right is not None and (lp or rp):
                return left / right, True
        return None, False

    @classmethod
    def _collect_module_consts(cls, tree: ast.Module, current_file: Path) -> tuple[dict[str, Path], dict[str, str]]:
        path_consts: dict[str, Path] = {}
        str_consts: dict[str, str] = {}
        for stmt in cls._iter_scope_stmts(tree.body):
            target_name: str | None = None
            val: ast.AST | None = None
            if isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and isinstance(stmt.targets[0], ast.Name):
                target_name = stmt.targets[0].id
                val = stmt.value
            elif isinstance(stmt, ast.AnnAssign) and isinstance(stmt.target, ast.Name) and stmt.value is not None:
                target_name = stmt.target.id
                val = stmt.value
            if target_name and val is not None:
                if isinstance(val, ast.Constant) and isinstance(val.value, str):
                    str_consts[target_name] = val.value
                else:
                    p, is_path = cls._eval_path_expr(val, current_file, path_consts, str_consts)
                    if p is not None and is_path:
                        path_consts[target_name] = p
        return path_consts, str_consts

    def _resolve_module_file(self, mod_name: str, level: int, current_file: Path) -> Path | None:
        cur_resolved = current_file.resolve()
        bases: list[Path] = []
        if level > 0:
            ancestors = cur_resolved.parents
            if level - 1 < len(ancestors):
                bases.append(ancestors[level - 1])
        else:
            bases.extend([
                cur_resolved.parent,
                self.root.resolve(),
                (self.root / "scripts").resolve(),
                (self.root / "scripts" / "lib").resolve(),
            ])
        parts = [p for p in mod_name.split(".") if p] if mod_name else []
        for base in bases:
            target_base = base.joinpath(*parts) if parts else base
            py_cand = target_base.with_suffix(".py") if parts else (target_base / "__init__.py")
            if py_cand.is_file():
                return py_cand.resolve()
            pkg_init = target_base / "__init__.py"
            if pkg_init.is_file():
                return pkg_init.resolve()
        return None

    def _resolve_dynamic_module_call(
        self,
        call: ast.Call,
        tree: ast.Module,
        current_file: Path,
        path_consts: dict[str, Path],
        str_consts: dict[str, str],
    ) -> Path | None:
        all_args = [*call.args, *(kw.value for kw in call.keywords)]
        search_bases = [*path_consts.values(), current_file.resolve().parent, self.root.resolve()]
        for arg in all_args:
            cand, is_p = self._eval_path_expr(arg, current_file, path_consts, str_consts)
            if cand is None:
                continue
            if cand.is_absolute() and cand.suffix == ".py" and cand.is_file():
                return cand.resolve()
            if not cand.is_absolute() and (is_p or cand.suffix == ".py" or "/" in str(cand) or "\\" in str(cand)):
                for base in search_bases:
                    joined = (base / cand).resolve()
                    if joined.suffix == ".py" and joined.is_file():
                        return joined
        if isinstance(call.func, ast.Name) and not all_args:
            for stmt in tree.body:
                if isinstance(stmt, ast.FunctionDef) and stmt.name == call.func.id:
                    for sub in ast.walk(stmt):
                        if isinstance(sub, ast.Call) and sub is not call:
                            found = self._resolve_dynamic_module_call(sub, tree, current_file, path_consts, str_consts)
                            if found is not None:
                                return found
        return None

    @staticmethod
    def _node_contains(node: ast.AST, line1: int, col: int) -> bool:
        s_ln = getattr(node, "lineno", 0)
        if not s_ln:
            return False
        s_col = getattr(node, "col_offset", 0)
        e_ln = getattr(node, "end_lineno", s_ln)
        e_col = getattr(node, "end_col_offset", s_col)
        return (s_ln, s_col) <= (line1, col) <= (e_ln, e_col)

    _TYPE_PARAM_NODES: tuple[type[ast.AST], ...] = tuple(
        getattr(ast, attr) for attr in ("TypeVar", "ParamSpec", "TypeVarTuple") if hasattr(ast, attr)
    )

    @classmethod
    def _enclosing_scopes(cls, tree: ast.Module, line1: int, col: int) -> list[ast.AST]:
        scopes: list[ast.AST] = [tree]

        def walk_node(node: ast.AST) -> None:
            for child in ast.iter_child_nodes(node):
                if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)):
                    # Decorators and parameter default expressions are evaluated in the enclosing scope.
                    strict_outer: list[ast.AST] = [
                        *child.decorator_list,
                        *child.args.defaults,
                        *(d for d in child.args.kw_defaults if d is not None),
                    ]
                    hit_outer = next((e for e in strict_outer if cls._node_contains(e, line1, col)), None)
                    if hit_outer is not None:
                        walk_node(hit_outer)
                        return
                    if cls._node_contains(child, line1, col):
                        type_params = list(getattr(child, "type_params", ()) or ())
                        all_fn_args = [
                            *child.args.posonlyargs,
                            *child.args.args,
                            *child.args.kwonlyargs,
                            *([child.args.vararg] if child.args.vararg else []),
                            *([child.args.kwarg] if child.args.kwarg else []),
                        ]
                        ann_exprs: list[ast.AST] = [
                            *type_params,
                            *(a.annotation for a in all_fn_args if a.annotation is not None),
                            *([child.returns] if child.returns is not None else []),
                        ]
                        hit_ann = next((e for e in ann_exprs if cls._node_contains(e, line1, col)), None)
                        if hit_ann is not None:
                            scopes.extend(type_params)
                            walk_node(hit_ann)
                            return
                        scopes.extend(type_params)
                        scopes.append(child)
                        walk_node(child)
                        return
                elif isinstance(child, ast.ClassDef):
                    hit_dec = next((d for d in child.decorator_list if cls._node_contains(d, line1, col)), None)
                    if hit_dec is not None:
                        walk_node(hit_dec)
                        return
                    if cls._node_contains(child, line1, col):
                        type_params = list(getattr(child, "type_params", ()) or ())
                        cls_header_exprs: list[ast.AST] = [
                            *type_params,
                            *child.bases,
                            *(kw.value for kw in child.keywords),
                        ]
                        hit_cls_hdr = next((e for e in cls_header_exprs if cls._node_contains(e, line1, col)), None)
                        if hit_cls_hdr is not None:
                            scopes.extend(type_params)
                            walk_node(hit_cls_hdr)
                            return
                        scopes.extend(type_params)
                        scopes.append(child)
                        walk_node(child)
                        return
                elif isinstance(child, (ast.ListComp, ast.SetComp, ast.DictComp, ast.GeneratorExp)):
                    if cls._node_contains(child, line1, col):
                        # PEP 289: the outermost iterable `generators[0].iter` is evaluated in the enclosing scope.
                        first_iter = child.generators[0].iter if child.generators else None
                        if first_iter is not None and cls._node_contains(first_iter, line1, col):
                            walk_node(first_iter)
                        else:
                            scopes.append(child)
                            walk_node(child)
                        return
                elif isinstance(child, ast.Lambda):
                    if cls._node_contains(child, line1, col):
                        lam_defaults = [d for d in (*child.args.defaults, *child.args.kw_defaults) if d is not None]
                        hit_lam_def = next((d for d in lam_defaults if cls._node_contains(d, line1, col)), None)
                        if hit_lam_def is not None:
                            walk_node(hit_lam_def)
                            return
                        scopes.append(child)
                        walk_node(child)
                        return
                else:
                    walk_node(child)

        walk_node(tree)
        return scopes

    @classmethod
    def _extract_target_names(cls, target: ast.AST) -> list[ast.Name]:
        if isinstance(target, ast.Name):
            return [target]
        if isinstance(target, (ast.Tuple, ast.List)):
            names: list[ast.Name] = []
            for elt in target.elts:
                names.extend(cls._extract_target_names(elt))
            return names
        if isinstance(target, ast.Starred):
            return cls._extract_target_names(target.value)
        return []

    @classmethod
    def _iter_shallow_exprs(cls, node: ast.AST) -> list[ast.AST]:
        """Collect expressions inside `node` without crossing into nested function, class, or lambda scopes."""
        out: list[ast.AST] = []
        for child in ast.iter_child_nodes(node):
            if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef, ast.Lambda)):
                continue
            out.append(child)
            out.extend(cls._iter_shallow_exprs(child))
        return out

    def _top_level_defs_in_module(
        self, mod_path: Path, name: str, visited: set[Path]
    ) -> list[tuple[Path, ast.AST]]:
        mod_path = mod_path.resolve()
        if mod_path in visited:
            return []
        visited = {*visited, mod_path}
        tree = self._parse_file(mod_path)
        if tree is None:
            return []
        matches: list[tuple[Path, ast.AST]] = []
        for stmt in self._iter_scope_stmts(tree.body):
            if isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)) and stmt.name == name:
                matches.append((mod_path, stmt))
            elif isinstance(stmt, ast.Assign):
                for t in stmt.targets:
                    for nm in self._extract_target_names(t):
                        if nm.id == name:
                            matches.append((mod_path, nm))
            elif isinstance(stmt, ast.AnnAssign):
                for nm in self._extract_target_names(stmt.target):
                    if nm.id == name:
                        matches.append((mod_path, nm))
            elif isinstance(stmt, ast.ImportFrom):
                for alias in stmt.names:
                    bound = alias.asname or alias.name
                    if bound == name and alias.name != "*":
                        sub = self._resolve_module_file(stmt.module or "", stmt.level or 0, mod_path)
                        if sub is not None:
                            matches.extend(self._top_level_defs_in_module(sub, alias.name, visited))
        return matches

    @staticmethod
    def _cls_key(cls_path: Path, cls_node: ast.ClassDef) -> tuple[Path, int, int, str]:
        return (
            cls_path.resolve(),
            getattr(cls_node, "lineno", 0),
            getattr(cls_node, "col_offset", 0),
            cls_node.name,
        )

    @classmethod
    def _expr_to_dotted_name(cls, expr: ast.AST) -> str | None:
        while isinstance(expr, ast.Subscript):
            expr = expr.value
        if isinstance(expr, ast.Name):
            return expr.id
        if isinstance(expr, ast.Attribute):
            prefix = cls._expr_to_dotted_name(expr.value)
            if prefix:
                return f"{prefix}.{expr.attr}"
        return None

    def _direct_class_member_defs(
        self, cls_path: Path, cls_node: ast.ClassDef, attr: str
    ) -> list[tuple[Path, ast.AST]]:
        cls_path = cls_path.resolve()
        matches: list[tuple[Path, ast.AST]] = []
        for stmt in self._iter_scope_stmts(cls_node.body):
            if isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)) and stmt.name == attr:
                matches.append((cls_path, stmt))
            elif isinstance(stmt, ast.Assign):
                for t in stmt.targets:
                    for nm in self._extract_target_names(t):
                        if nm.id == attr:
                            matches.append((cls_path, nm))
            elif isinstance(stmt, ast.AnnAssign):
                for nm in self._extract_target_names(stmt.target):
                    if nm.id == attr:
                        matches.append((cls_path, nm))
        if matches:
            return matches

        # Check `self.<attr> = ...` inside `__init__` first, then other methods on `cls_node`.
        methods = [s for s in cls_node.body if isinstance(s, (ast.FunctionDef, ast.AsyncFunctionDef))]
        ordered_methods = [
            *(m for m in methods if m.name == "__init__"),
            *(m for m in methods if m.name != "__init__"),
        ]
        for method in ordered_methods:
            for m_stmt in self._iter_scope_stmts(method.body):
                if isinstance(m_stmt, ast.Assign):
                    for t in m_stmt.targets:
                        if (
                            isinstance(t, ast.Attribute)
                            and isinstance(t.value, ast.Name)
                            and t.value.id == "self"
                            and t.attr == attr
                        ):
                            matches.append((cls_path, t))
                elif isinstance(m_stmt, ast.AnnAssign):
                    t = m_stmt.target
                    if (
                        isinstance(t, ast.Attribute)
                        and isinstance(t.value, ast.Name)
                        and t.value.id == "self"
                        and t.attr == attr
                    ):
                        matches.append((cls_path, t))
            if matches:
                return matches
        return matches

    def _resolve_class_bases(
        self,
        cls_path: Path,
        cls_node: ast.ClassDef,
        visited: set[tuple[Path, int, int, str]] | None = None,
        memo: dict[tuple[Path, int, int, str], list[tuple[Path, ast.ClassDef]]] | None = None,
        cycle_box: list[bool] | None = None,
    ) -> list[tuple[Path, ast.ClassDef]]:
        cls_path = cls_path.resolve()
        tree = self._parse_file(cls_path)
        if tree is None or not cls_node.bases:
            return []
        path_consts, str_consts = self._collect_module_consts(tree, cls_path)
        out: list[tuple[Path, ast.ClassDef]] = []
        out_keys: set[tuple[Path, int, int, str]] = set()
        for base_expr in cls_node.bases:
            dotted = self._expr_to_dotted_name(base_expr)
            if not dotted:
                continue
            b_line1 = getattr(base_expr, "lineno", getattr(cls_node, "lineno", 1))
            b_col = getattr(base_expr, "col_offset", getattr(cls_node, "col_offset", 0))
            scopes = self._enclosing_scopes(tree, b_line1, b_col)
            resolved = self._resolve_class_expr(
                dotted,
                scopes,
                cls_path,
                tree,
                path_consts,
                str_consts,
                cursor_pos=(b_line1, b_col),
                visited=visited,
                memo=memo,
                cycle_box=cycle_box,
            )
            if resolved is not None:
                res_pair = (resolved[0].resolve(), resolved[1])
                res_key = self._cls_key(res_pair[0], res_pair[1])
                if res_key not in out_keys:
                    out_keys.add(res_key)
                    out.append(res_pair)
        return out

    def _class_mro_inner(
        self,
        cls_path: Path,
        cls_node: ast.ClassDef,
        active: set[tuple[Path, int, int, str]],
        memo: dict[tuple[Path, int, int, str], list[tuple[Path, ast.ClassDef]]],
    ) -> tuple[list[tuple[Path, ast.ClassDef]], bool]:
        resolved_path = cls_path.resolve()
        key = self._cls_key(resolved_path, cls_node)
        if key in active:
            return [], True
        cached = memo.get(key)
        if cached is not None:
            return list(cached), False

        next_active = {*active, key}
        cycle_box = [False]
        raw_bases = self._resolve_class_bases(
            resolved_path,
            cls_node,
            visited=next_active,
            memo=memo,
            cycle_box=cycle_box,
        )
        hit_cycle = cycle_box[0]
        bases: list[tuple[Path, ast.ClassDef]] = []
        for bp, bn in raw_bases:
            if self._cls_key(bp, bn) in next_active:
                hit_cycle = True
            else:
                bases.append((bp, bn))

        if not bases:
            single = [(resolved_path, cls_node)]
            if not hit_cycle:
                memo[key] = single
            return list(single), hit_cycle

        keyed_seqs: list[list[tuple[tuple[Path, ast.ClassDef], tuple[Path, int, int, str]]]] = []
        for bp, bn in bases:
            sub_mro, sub_cycle = self._class_mro_inner(bp, bn, next_active, memo)
            hit_cycle = hit_cycle or sub_cycle
            if sub_mro:
                keyed_seqs.append([(item, self._cls_key(item[0], item[1])) for item in sub_mro])
        base_seq = [((bp.resolve(), bn), self._cls_key(bp, bn)) for bp, bn in bases]
        if base_seq:
            keyed_seqs.append(base_seq)

        mro: list[tuple[Path, ast.ClassDef]] = [(resolved_path, cls_node)]
        seen_mro: set[tuple[Path, int, int, str]] = {key}
        while keyed_seqs:
            tail_keys = {k for seq in keyed_seqs for _, k in seq[1:]}
            chosen_pair: tuple[tuple[Path, ast.ClassDef], tuple[Path, int, int, str]] | None = None
            for seq in keyed_seqs:
                cand, cand_k = seq[0]
                if cand_k not in tail_keys:
                    chosen_pair = (cand, cand_k)
                    break
            if chosen_pair is None:
                chosen_pair = keyed_seqs[0][0]
            chosen, chosen_k = chosen_pair
            if chosen_k not in seen_mro:
                seen_mro.add(chosen_k)
                mro.append(chosen)
            next_seqs: list[list[tuple[tuple[Path, ast.ClassDef], tuple[Path, int, int, str]]]] = []
            for seq in keyed_seqs:
                filtered = [pair for pair in seq if pair[1] != chosen_k]
                if filtered:
                    next_seqs.append(filtered)
            keyed_seqs = next_seqs

        if not hit_cycle:
            memo[key] = mro
        return list(mro), hit_cycle

    def _class_mro(
        self,
        cls_path: Path,
        cls_node: ast.ClassDef,
        active: set[tuple[Path, int, int, str]] | None = None,
        memo: dict[tuple[Path, int, int, str], list[tuple[Path, ast.ClassDef]]] | None = None,
    ) -> list[tuple[Path, ast.ClassDef]]:
        """Compute cycle-safe, memoized Python C3 MRO for `(cls_path, cls_node)` across same-file and imported bases."""
        mro, _ = self._class_mro_inner(
            cls_path,
            cls_node,
            set() if active is None else set(active),
            {} if memo is None else memo,
        )
        return mro

    def _class_member_defs(
        self,
        cls_path: Path,
        cls_node: ast.ClassDef,
        attr: str,
        visited: set[tuple[Path, int, int, str]] | None = None,
        memo: dict[tuple[Path, int, int, str], list[tuple[Path, ast.ClassDef]]] | None = None,
        cycle_box: list[bool] | None = None,
    ) -> list[tuple[Path, ast.AST]]:
        seen = set() if visited is None else set(visited)
        if self._cls_key(cls_path, cls_node) in seen:
            if cycle_box is not None:
                cycle_box[0] = True
            return []
        shared_memo = {} if memo is None else memo
        mro, hit_cycle = self._class_mro_inner(cls_path, cls_node, seen, shared_memo)
        if hit_cycle and cycle_box is not None:
            cycle_box[0] = True
        for m_path, m_cls in mro:
            matches = self._direct_class_member_defs(m_path, m_cls, attr)
            if matches:
                return matches
        return []

    @staticmethod
    def _has_postponed_annotations(tree: ast.Module) -> bool:
        if sys.version_info >= (3, 14):
            return True
        for stmt in tree.body:
            if isinstance(stmt, ast.ImportFrom) and stmt.module == "__future__":
                if any(alias.name == "annotations" for alias in stmt.names):
                    return True
        return False

    @classmethod
    def _fn_only_annotation_contains(cls, fn: ast.FunctionDef | ast.AsyncFunctionDef, line1: int, col: int) -> bool:
        """True only for parameter/return type annotations (never defaults or decorators, which evaluate eagerly)."""
        all_fn_args = [
            *fn.args.posonlyargs,
            *fn.args.args,
            *fn.args.kwonlyargs,
            *([fn.args.vararg] if fn.args.vararg else []),
            *([fn.args.kwarg] if fn.args.kwarg else []),
        ]
        ann_exprs: list[ast.AST] = [
            *(a.annotation for a in all_fn_args if a.annotation is not None),
            *([fn.returns] if fn.returns is not None else []),
        ]
        return any(cls._node_contains(e, line1, col) for e in ann_exprs)

    @classmethod
    def _stmt_header_expr_contains(cls, stmt: ast.AST, line1: int, col: int) -> bool:
        """True if `(line1, col)` sits inside a declaration header expression evaluated BEFORE `stmt` binds its name."""
        if isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef)):
            all_fn_args = [
                *stmt.args.posonlyargs,
                *stmt.args.args,
                *stmt.args.kwonlyargs,
                *([stmt.args.vararg] if stmt.args.vararg else []),
                *([stmt.args.kwarg] if stmt.args.kwarg else []),
            ]
            exprs: list[ast.AST] = [
                *stmt.decorator_list,
                *(getattr(stmt, "type_params", ()) or ()),
                *stmt.args.defaults,
                *(d for d in stmt.args.kw_defaults if d is not None),
                *(a.annotation for a in all_fn_args if a.annotation is not None),
                *([stmt.returns] if stmt.returns is not None else []),
            ]
            return any(cls._node_contains(e, line1, col) for e in exprs)
        if isinstance(stmt, ast.ClassDef):
            cls_exprs: list[ast.AST] = [
                *stmt.decorator_list,
                *(getattr(stmt, "type_params", ()) or ()),
                *stmt.bases,
                *(kw.value for kw in stmt.keywords),
            ]
            return any(cls._node_contains(e, line1, col) for e in cls_exprs)
        if isinstance(stmt, ast.Assign) and stmt.value is not None:
            return cls._node_contains(stmt.value, line1, col)
        if isinstance(stmt, ast.AnnAssign):
            return (
                (stmt.annotation is not None and cls._node_contains(stmt.annotation, line1, col))
                or (stmt.value is not None and cls._node_contains(stmt.value, line1, col))
            )
        return False

    def _lookup_in_scope(
        self,
        scope: ast.AST,
        ident: str,
        path: Path,
        tree: ast.Module,
        path_consts: dict[str, Path],
        str_consts: dict[str, str],
        cursor_pos: tuple[int, int] | None = None,
    ) -> tuple[list[tuple[Path, ast.AST]], Path | None, bool, str | None]:
        """Return `(definitions, module_path, is_external_import, constructed_class_expr)` for `ident` in `scope`."""
        if self._TYPE_PARAM_NODES and isinstance(scope, self._TYPE_PARAM_NODES):
            if getattr(scope, "name", None) == ident:
                return [(path, scope)], None, False, None
            return [], None, False, None

        if isinstance(scope, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)):
            all_args = [
                *scope.args.posonlyargs,
                *scope.args.args,
                *scope.args.kwonlyargs,
                *([scope.args.vararg] if scope.args.vararg else []),
                *([scope.args.kwarg] if scope.args.kwarg else []),
            ]
            for a in all_args:
                if a.arg == ident:
                    ann_cls = ast.unparse(a.annotation) if isinstance(a.annotation, (ast.Name, ast.Attribute)) else None
                    return [(path, a)], None, False, ann_cls
            if isinstance(scope, ast.Lambda):
                lam_defs: list[tuple[Path, ast.AST]] = [
                    (path, sub.target)
                    for sub in self._iter_shallow_exprs(scope.body)
                    if isinstance(sub, ast.NamedExpr) and isinstance(sub.target, ast.Name) and sub.target.id == ident
                ]
                return lam_defs, None, False, None

        if isinstance(scope, (ast.ListComp, ast.SetComp, ast.DictComp, ast.GeneratorExp)):
            comp_defs: list[tuple[Path, ast.AST]] = []
            for gen in scope.generators:
                for nm in self._extract_target_names(gen.target):
                    if nm.id == ident:
                        comp_defs.append((path, nm))
            return comp_defs, None, False, None

        body = getattr(scope, "body", None)
        if not isinstance(body, list):
            return [], None, False, None

        defs: list[tuple[Path, ast.AST]] = []
        mod_target: Path | None = None
        is_ext = False
        cls_expr: str | None = None

        scope_stmts = self._iter_scope_stmts(body)
        # Python execution model (`docs.python.org/3/reference/executionmodel.html#resolution-of-names`):
        # - Function scopes bind local names across the entire block at compile time regardless of statement order.
        # - Module and class bodies bind names sequentially as statements execute.
        is_sequential_ns = isinstance(scope, (ast.Module, ast.ClassDef)) and cursor_pos is not None
        in_fn_annotation = (
            isinstance(scope, ast.ClassDef)
            and cursor_pos is not None
            and self._has_postponed_annotations(tree)
            and any(
                isinstance(s, (ast.FunctionDef, ast.AsyncFunctionDef))
                and self._fn_only_annotation_contains(s, *cursor_pos)
                for s in scope_stmts
            )
        )

        for stmt in scope_stmts:
            if (
                isinstance(scope, ast.ClassDef)
                and cursor_pos is not None
                and not in_fn_annotation
                and getattr(stmt, "lineno", 0) > cursor_pos[0]
            ):
                # In a class body, assignments/defs after the current line have not bound class attributes yet.
                break
            if is_sequential_ns and cursor_pos is not None and self._stmt_header_expr_contains(stmt, *cursor_pos):
                binds_ident = (
                    (isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)) and stmt.name == ident)
                    or (
                        isinstance(stmt, ast.Assign)
                        and any(nm.id == ident for t in stmt.targets for nm in self._extract_target_names(t))
                    )
                    or (
                        isinstance(stmt, ast.AnnAssign)
                        and any(nm.id == ident for nm in self._extract_target_names(stmt.target))
                    )
                )
                if binds_ident:
                    # Module/class header and RHS expressions execute before `stmt` (or later statements) rebinds `ident`.
                    break
            if isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)) and stmt.name == ident:
                defs.append((path, stmt))
            elif isinstance(stmt, ast.Import):
                for alias in stmt.names:
                    bound = alias.asname or alias.name.split(".")[0]
                    if bound == ident:
                        mod_qname = alias.name if alias.asname else alias.name.split(".")[0]
                        mod_file = self._resolve_module_file(mod_qname, 0, path)
                        if mod_file is not None:
                            mod_tree = self._parse_file(mod_file)
                            defs.append((mod_file, mod_tree or stmt))
                            mod_target = mod_file
                        else:
                            defs.append((path, stmt))
                            is_ext = True
            elif isinstance(stmt, ast.ImportFrom):
                for alias in stmt.names:
                    if alias.name == "*":
                        star_file = self._resolve_module_file(stmt.module or "", stmt.level or 0, path)
                        if star_file is not None:
                            defs.extend(self._top_level_defs_in_module(star_file, ident, set()))
                        continue
                    bound = alias.asname or alias.name
                    if bound == ident:
                        base_mod = stmt.module or ""
                        lvl = stmt.level or 0
                        sub_qname = f"{base_mod}.{alias.name}".lstrip(".") if base_mod else alias.name
                        sub_file = self._resolve_module_file(sub_qname, lvl, path)
                        mod_file = self._resolve_module_file(base_mod, lvl, path) if (base_mod or lvl) else None
                        if mod_file is not None:
                            sym_defs = self._top_level_defs_in_module(mod_file, alias.name, set())
                            if sym_defs:
                                defs.extend(sym_defs)
                                if sub_file is not None:
                                    mod_target = sub_file
                                continue
                        if sub_file is not None:
                            sub_tree = self._parse_file(sub_file)
                            defs.append((sub_file, sub_tree or stmt))
                            mod_target = sub_file
                        else:
                            defs.append((path, stmt))
                            is_ext = True
            elif isinstance(stmt, ast.Assign):
                for t in stmt.targets:
                    for nm in self._extract_target_names(t):
                        if nm.id == ident:
                            defs.append((path, nm))
                            if isinstance(t, ast.Name) and isinstance(stmt.value, ast.Call):
                                dyn = self._resolve_dynamic_module_call(stmt.value, tree, path, path_consts, str_consts)
                                if dyn is not None:
                                    mod_target = dyn
                                elif isinstance(stmt.value.func, (ast.Name, ast.Attribute)):
                                    cls_expr = ast.unparse(stmt.value.func)
            elif isinstance(stmt, ast.AnnAssign):
                for nm in self._extract_target_names(stmt.target):
                    if nm.id == ident:
                        defs.append((path, nm))
                        if isinstance(stmt.target, ast.Name) and isinstance(stmt.annotation, (ast.Name, ast.Attribute)):
                            cls_expr = ast.unparse(stmt.annotation)
                        if isinstance(stmt.target, ast.Name) and isinstance(stmt.value, ast.Call):
                            dyn = self._resolve_dynamic_module_call(stmt.value, tree, path, path_consts, str_consts)
                            if dyn is not None:
                                mod_target = dyn
            elif isinstance(stmt, ast.AugAssign):
                if isinstance(stmt.target, ast.Name) and stmt.target.id == ident:
                    defs.append((path, stmt.target))
            elif isinstance(stmt, (ast.For, ast.AsyncFor)):
                for nm in self._extract_target_names(stmt.target):
                    if nm.id == ident:
                        defs.append((path, nm))
            elif isinstance(stmt, (ast.With, ast.AsyncWith)):
                for item in stmt.items:
                    if item.optional_vars is not None:
                        for nm in self._extract_target_names(item.optional_vars):
                            if nm.id == ident:
                                defs.append((path, nm))
            elif isinstance(stmt, (ast.Try, ast.TryStar)):
                for handler in stmt.handlers:
                    if isinstance(handler, ast.ExceptHandler) and handler.name == ident:
                        defs.append((path, handler))
            elif isinstance(stmt, ast.Match):
                for case in stmt.cases:
                    for pnode in ast.walk(case.pattern):
                        if isinstance(pnode, (ast.MatchAs, ast.MatchStar)) and pnode.name == ident:
                            defs.append((path, pnode))
                        elif isinstance(pnode, ast.MatchMapping) and pnode.rest == ident:
                            defs.append((path, pnode))

            # PEP 572: `:=` walrus expressions bind in the containing function/module scope.
            if not isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef,
                                     ast.If, ast.For, ast.AsyncFor, ast.While, ast.With, ast.AsyncWith,
                                     ast.Try, ast.TryStar, ast.Match)):
                for expr in self._iter_shallow_exprs(stmt):
                    if isinstance(expr, ast.NamedExpr) and isinstance(expr.target, ast.Name) and expr.target.id == ident:
                        defs.append((path, expr.target))
            elif isinstance(stmt, (ast.If, ast.While)):
                test_expr = stmt.test
                if isinstance(test_expr, ast.NamedExpr) and isinstance(test_expr.target, ast.Name) and test_expr.target.id == ident:
                    defs.append((path, test_expr.target))
                for expr in self._iter_shallow_exprs(test_expr):
                    if isinstance(expr, ast.NamedExpr) and isinstance(expr.target, ast.Name) and expr.target.id == ident:
                        defs.append((path, expr.target))

        return defs, mod_target, is_ext, cls_expr

    def _scope_directives(self, scope: ast.AST) -> tuple[set[str], set[str]]:
        """Return `(global_names, nonlocal_names)` declared directly inside `scope`."""
        body = getattr(scope, "body", None)
        if not isinstance(body, list) or isinstance(scope, ast.Module):
            return set(), set()
        globals_set: set[str] = set()
        nonlocals_set: set[str] = set()
        for stmt in self._iter_scope_stmts(body):
            if isinstance(stmt, ast.Global):
                globals_set.update(stmt.names)
            elif isinstance(stmt, ast.Nonlocal):
                nonlocals_set.update(stmt.names)
        return globals_set, nonlocals_set

    def _global_declared_defs_in_module(self, path: Path, tree: ast.Module, ident: str) -> list[tuple[Path, ast.AST]]:
        """Fallback when a module binds `ident` only inside functions via `global <ident>` without a top-level target."""
        found: list[tuple[Path, ast.AST]] = []
        for node in ast.walk(tree):
            if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                continue
            g_names, _ = self._scope_directives(node)
            if ident not in g_names:
                continue
            for stmt in self._iter_scope_stmts(node.body):
                if isinstance(stmt, ast.Assign):
                    for t in stmt.targets:
                        for nm in self._extract_target_names(t):
                            if nm.id == ident:
                                found.append((path, nm))
                elif isinstance(stmt, ast.AnnAssign):
                    for nm in self._extract_target_names(stmt.target):
                        if nm.id == ident:
                            found.append((path, nm))
        return found

    def _lookup_across_scopes(
        self,
        scopes: list[ast.AST],
        ident: str,
        path: Path,
        tree: ast.Module,
        path_consts: dict[str, Path],
        str_consts: dict[str, str],
        cursor_pos: tuple[int, int] | None = None,
    ) -> tuple[list[tuple[Path, ast.AST]], Path | None, bool, str | None]:
        """Resolve `ident` across lexical `scopes` while honoring `global` and `nonlocal` declarations."""
        force_global = False
        for sc in self._lexical_scopes_for_lookup(scopes):
            if force_global and not isinstance(sc, ast.Module):
                continue
            g_names, nl_names = self._scope_directives(sc)
            if ident in g_names:
                force_global = True
                continue
            if ident in nl_names:
                continue
            defs, mod_target, is_ext, cls_expr = self._lookup_in_scope(
                sc, ident, path, tree, path_consts, str_consts, cursor_pos=cursor_pos
            )
            if isinstance(sc, ast.Module) and not defs and not mod_target and not is_ext and cls_expr is None:
                defs = self._global_declared_defs_in_module(path, tree, ident)
            if defs or mod_target is not None or is_ext or cls_expr is not None:
                return defs, mod_target, is_ext, cls_expr
        return [], None, False, None

    def _lexical_scopes_for_lookup(self, scopes: list[ast.AST]) -> list[ast.AST]:
        """Python LEGB: a `ClassDef` body is only searched for unqualified names when the cursor is directly inside it."""
        ordered: list[ast.AST] = []
        for idx in range(len(scopes) - 1, -1, -1):
            sc = scopes[idx]
            if isinstance(sc, ast.ClassDef) and idx != len(scopes) - 1:
                continue
            ordered.append(sc)
        return ordered

    def _resolve_class_expr(
        self,
        cls_expr: str,
        scopes: list[ast.AST],
        path: Path,
        tree: ast.Module,
        path_consts: dict[str, Path],
        str_consts: dict[str, str],
        *,
        cursor_pos: tuple[int, int] | None = None,
        visited: set[tuple[Path, int, int, str]] | None = None,
        memo: dict[tuple[Path, int, int, str], list[tuple[Path, ast.ClassDef]]] | None = None,
        cycle_box: list[bool] | None = None,
    ) -> tuple[Path, ast.ClassDef] | None:
        parts = cls_expr.split(".")
        if len(parts) == 1:
            defs, _, _, _ = self._lookup_across_scopes(
                scopes, parts[0], path, tree, path_consts, str_consts, cursor_pos=cursor_pos
            )
            for def_path, node in defs:
                if isinstance(node, ast.ClassDef):
                    return def_path, node
            return None
        # Dotted class expression such as `LSP_MCP.LspMcpServer` or `Outer.Inner`.
        defs, mod_target, is_ext, _ = self._lookup_across_scopes(
            scopes, parts[0], path, tree, path_consts, str_consts, cursor_pos=cursor_pos
        )
        if is_ext:
            return None
        if mod_target is not None:
            cur_mod = mod_target
            cur_cls: tuple[Path, ast.ClassDef] | None = None
            for seg in parts[1:]:
                if cur_cls is not None:
                    sub_defs = self._class_member_defs(
                        cur_cls[0], cur_cls[1], seg, visited, memo=memo, cycle_box=cycle_box
                    )
                    cls_match = next(((p, n) for p, n in sub_defs if isinstance(n, ast.ClassDef)), None)
                    if cls_match is None:
                        return None
                    cur_cls = cls_match
                else:
                    sym_defs = self._top_level_defs_in_module(cur_mod, seg, set())
                    cls_match = next(((p, n) for p, n in sym_defs if isinstance(n, ast.ClassDef)), None)
                    if cls_match is not None:
                        cur_cls = cls_match
                        continue
                    sub_mod = self._resolve_module_file(seg, 0, cur_mod)
                    if sub_mod is not None:
                        cur_mod = sub_mod
                        continue
                    return None
            return cur_cls
        cls_head = next(((p, n) for p, n in defs if isinstance(n, ast.ClassDef)), None)
        if cls_head is not None:
            cur_cls = cls_head
            for seg in parts[1:]:
                sub_defs = self._class_member_defs(
                    cur_cls[0], cur_cls[1], seg, visited, memo=memo, cycle_box=cycle_box
                )
                nxt_cls = next(((p, n) for p, n in sub_defs if isinstance(n, ast.ClassDef)), None)
                if nxt_cls is None:
                    return None
                cur_cls = nxt_cls
            return cur_cls
        return None

    def _resolve_defs_at(
        self,
        path: Path,
        line: int,
        character: int,
        *,
        mro_memo: dict[tuple[Path, int, int, str], list[tuple[Path, ast.ClassDef]]] | None = None,
    ) -> tuple[str, list[tuple[Path, ast.AST]]]:
        text = self._text(path)
        receiver, ident, is_attr, utf8_col = self._cursor_symbol(text, line, character)
        if not ident:
            return "", []
        tree = self._parse_file(path)
        if tree is None:
            return ident, []

        path = path.resolve()
        path_consts, str_consts = self._collect_module_consts(tree, path)
        scopes = self._enclosing_scopes(tree, line + 1, utf8_col)
        memo = {} if mro_memo is None else mro_memo

        if is_attr:
            if receiver is None:
                return ident, []
            if receiver == "super()":
                for sc in reversed(scopes):
                    if isinstance(sc, ast.ClassDef):
                        for m_path, m_cls in self._class_mro(path, sc, memo=memo)[1:]:
                            m_defs = self._direct_class_member_defs(m_path, m_cls, ident)
                            if m_defs:
                                return ident, m_defs
                        return ident, []
                return ident, []
            if receiver in ("self", "cls"):
                for sc in reversed(scopes):
                    if isinstance(sc, ast.ClassDef):
                        return ident, self._class_member_defs(path, sc, ident, set(), memo=memo)
                return ident, []

            recv_parts = receiver.split(".")
            head = recv_parts[0]
            defs, mod_target, is_ext, cls_expr = self._lookup_across_scopes(
                scopes, head, path, tree, path_consts, str_consts, cursor_pos=(line + 1, utf8_col)
            )
            if not defs and mod_target is None and not is_ext and cls_expr is None:
                return ident, []
            if is_ext:
                return ident, []
            if mod_target is not None:
                cur_mod = mod_target
                cur_cls: tuple[Path, ast.ClassDef] | None = None
                for seg in recv_parts[1:]:
                    if cur_cls is not None:
                        sub_defs = self._class_member_defs(cur_cls[0], cur_cls[1], seg, set(), memo=memo)
                        cls_match = next(((p, n) for p, n in sub_defs if isinstance(n, ast.ClassDef)), None)
                        if cls_match is None:
                            return ident, []
                        cur_cls = cls_match
                    else:
                        sym_defs = self._top_level_defs_in_module(cur_mod, seg, set())
                        cls_match = next(((p, n) for p, n in sym_defs if isinstance(n, ast.ClassDef)), None)
                        if cls_match is not None:
                            cur_cls = cls_match
                            continue
                        sub_mod = self._resolve_module_file(seg, 0, cur_mod)
                        if sub_mod is not None:
                            cur_mod = sub_mod
                            continue
                        return ident, []
                if cur_cls is not None:
                    return ident, self._class_member_defs(cur_cls[0], cur_cls[1], ident, set(), memo=memo)
                return ident, self._top_level_defs_in_module(cur_mod, ident, set())

            cls_direct = next(((p, n) for p, n in defs if isinstance(n, ast.ClassDef)), None)
            if cls_direct is not None and len(recv_parts) == 1:
                return ident, self._class_member_defs(cls_direct[0], cls_direct[1], ident, set(), memo=memo)

            if cls_expr and len(recv_parts) == 1:
                resolved_cls = self._resolve_class_expr(
                    cls_expr, scopes, path, tree, path_consts, str_consts, memo=memo
                )
                if resolved_cls is not None:
                    return ident, self._class_member_defs(resolved_cls[0], resolved_cls[1], ident, set(), memo=memo)
            return ident, []

        # Declaration header of a function, method, or class (`def method(self):` inside `class Foo:`).
        inner = scopes[-1]
        if (
            isinstance(inner, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef))
            and inner.name == ident
            and getattr(inner, "lineno", 0) == line + 1
        ):
            in_param = isinstance(inner, (ast.FunctionDef, ast.AsyncFunctionDef)) and any(
                self._node_contains(a, line + 1, utf8_col)
                for a in (
                    *inner.args.posonlyargs,
                    *inner.args.args,
                    *inner.args.kwonlyargs,
                    *([inner.args.vararg] if inner.args.vararg else []),
                    *([inner.args.kwarg] if inner.args.kwarg else []),
                )
            )
            if not in_param:
                return ident, [(path, inner)]

        # Original symbol/module name in `from mod import orig as alias` or `import mod as alias`.
        inner_body = getattr(inner, "body", None)
        if isinstance(inner_body, list):
            for stmt in self._iter_scope_stmts(inner_body):
                s_ln = getattr(stmt, "lineno", 0)
                e_ln = getattr(stmt, "end_lineno", s_ln)
                if not (s_ln <= line + 1 <= e_ln):
                    continue
                if isinstance(stmt, ast.ImportFrom):
                    for alias in stmt.names:
                        if alias.name == ident and alias.name != "*":
                            mod_file = self._resolve_module_file(stmt.module or "", stmt.level or 0, path)
                            if mod_file is not None:
                                sym_defs = self._top_level_defs_in_module(mod_file, alias.name, set())
                                if sym_defs:
                                    return ident, sym_defs
                elif isinstance(stmt, ast.Import):
                    for alias in stmt.names:
                        if alias.asname and ident in (alias.name, alias.name.split(".")[-1]):
                            mod_file = self._resolve_module_file(alias.name, 0, path)
                            if mod_file is not None:
                                mod_tree = self._parse_file(mod_file)
                                return ident, [(mod_file, mod_tree or stmt)]

        defs, _, _, _ = self._lookup_across_scopes(
            scopes, ident, path, tree, path_consts, str_consts, cursor_pos=(line + 1, utf8_col)
        )
        return ident, defs

    def _hover(self, path: Path, line: int, character: int) -> dict | None:
        ident, defs = self._resolve_defs_at(path, line, character)
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
        elif isinstance(node, ast.Module):
            doc = ast.get_docstring(node) or ""
            md = f"```python\nmodule {def_path.name}\n```" + (f"\n\n{doc}" if doc else "")
        else:
            md = f"```python\n{ident}  # {def_path.name}:{getattr(node, 'lineno', 1)}\n```"
        return {"contents": {"kind": "markdown", "value": md}}

    def _definitions(self, path: Path, line: int, character: int) -> list[dict]:
        ident, defs = self._resolve_defs_at(path, line, character)
        locs = []
        seen: set[tuple[str, int, int]] = set()
        for def_path, node in defs:
            def_lines = self._text(def_path).splitlines()
            ln = max(0, getattr(node, "lineno", 1) - 1)
            def_row = def_lines[ln] if ln < len(def_lines) else ""
            utf8_col = max(0, getattr(node, "col_offset", 0))
            u16_col = self._utf8_col_to_utf16(def_row, utf8_col)
            uri = path_to_uri(def_path)
            key = (uri, ln, u16_col)
            if key in seen:
                continue
            seen.add(key)
            locs.append({
                "uri": uri,
                "range": {"start": {"line": ln, "character": u16_col},
                          "end": {"line": ln, "character": u16_col + max(1, self._utf16_len(ident))}},
            })
        return locs

    @classmethod
    def _safe_name_tokens(cls, src: str, ident: str) -> list[tuple[int, int, int]]:
        """Yield `(line0, cp_start, cp_end)` for `ident` tokens without failing on invalid indentation or syntax."""
        return [(ln0, cs, ce) for ln0, cs, ce, _ in cls._safe_name_tokens_multi(src, {ident})]

    @staticmethod
    def _safe_name_tokens_multi(src: str, names: set[str]) -> list[tuple[int, int, int, str]]:
        """Yield `(line0, cp_start, cp_end, token)` for any `NAME` token in `names`."""
        if not names:
            return []
        src = StdlibPythonLsp._normalize_python_text(src)
        tokens: list[tuple[int, int, int, str]] = []
        last_line0 = -1
        lines = src.splitlines(keepends=True)
        it = iter(lines)
        try:
            for tok in tokenize.generate_tokens(lambda: next(it)):
                last_line0 = max(last_line0, tok.start[0] - 1)
                if tok.type == tokenize.NAME:
                    norm_tok = tok.string if tok.string.isascii() else unicodedata.normalize("NFKC", tok.string)
                    if norm_tok in names:
                        tokens.append((tok.start[0] - 1, tok.start[1], tok.end[1], norm_tok))
            return tokens
        except (tokenize.TokenError, IndentationError, SyntaxError):
            pass
        # Fallback lexical scan for lines past a broken indentation or unterminated token during live editing.
        alt = "|".join(re.escape(n) for n in sorted(names, key=len, reverse=True) if n)
        if not alt:
            return tokens
        pat = re.compile(rf"\b(?:{alt})\b")
        plain_lines = src.splitlines()
        for ln0 in range(max(0, last_line0 + 1), len(plain_lines)):
            code_part = plain_lines[ln0].split("#", 1)[0]
            for m in pat.finditer(code_part):
                tokens.append((ln0, m.start(), m.end(), m.group(0)))
        return tokens

    @classmethod
    def _declared_symbol_name(cls, def_path: Path, node: ast.AST) -> str | None:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            return node.name
        if isinstance(node, ast.Name):
            return node.id
        if isinstance(node, ast.arg):
            return node.arg
        if isinstance(node, ast.Attribute):
            return node.attr
        if isinstance(node, (ast.ExceptHandler, ast.MatchAs, ast.MatchStar)):
            return node.name
        if isinstance(node, ast.MatchMapping):
            return node.rest
        if cls._TYPE_PARAM_NODES and isinstance(node, cls._TYPE_PARAM_NODES):
            return getattr(node, "name", None)
        if isinstance(node, ast.Module):
            return def_path.parent.name if def_path.stem == "__init__" else def_path.stem
        return None

    def _expand_import_alias_names(self, cand_files: list[Path], seed_names: set[str]) -> set[str]:
        """Expand `seed_names` across `import ... as ...` and `from ... import ... as ...` aliases in `cand_files`."""
        names = {n for n in seed_names if n}
        alias_pairs: list[tuple[str, str]] = []
        for cand_file in cand_files:
            tree = self._parse_file(cand_file)
            if tree is None:
                continue
            for node in ast.walk(tree):
                if isinstance(node, ast.ImportFrom):
                    for alias in node.names:
                        if alias.asname and alias.name != "*":
                            alias_pairs.append((alias.name, alias.asname))
                elif isinstance(node, ast.Import):
                    for alias in node.names:
                        if alias.asname:
                            alias_pairs.append((alias.name.split(".")[-1], alias.asname))
                            alias_pairs.append((alias.name.split(".")[0], alias.asname))
        changed = True
        while changed:
            changed = False
            for orig, asname in alias_pairs:
                if orig in names and asname not in names:
                    names.add(asname)
                    changed = True
                elif asname in names and orig not in names:
                    names.add(orig)
                    changed = True
        return names

    @staticmethod
    def _def_key(def_path: Path, node: ast.AST) -> tuple[Path, int, int]:
        return (def_path.resolve(), getattr(node, "lineno", 1), getattr(node, "col_offset", 0))

    def _local_function_span(
        self, path: Path, target_defs: list[tuple[Path, ast.AST]]
    ) -> tuple[int, int] | None:
        """If every definition is bound inside an enclosing function of `path`, return its 0-based `(start, end)` lines."""
        resolved = path.resolve()
        tree = self._parse_file(resolved)
        if tree is None or not target_defs:
            return None
        min_ln0: int | None = None
        max_ln0: int | None = None
        for dp, dn in target_defs:
            if dp.resolve() != resolved:
                return None
            if isinstance(dn, ast.Attribute):
                return None
            if self._TYPE_PARAM_NODES and isinstance(dn, self._TYPE_PARAM_NODES):
                return None
            enc = self._enclosing_scopes(tree, getattr(dn, "lineno", 1), getattr(dn, "col_offset", 0))
            binding_scope = enc[-2] if (len(enc) >= 2 and enc[-1] is dn) else enc[-1]
            decl_sym = self._declared_symbol_name(dp, dn)
            if decl_sym:
                g_names, _ = self._scope_directives(binding_scope)
                if decl_sym in g_names:
                    return None
            if not isinstance(
                binding_scope,
                (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda,
                 ast.ListComp, ast.SetComp, ast.DictComp, ast.GeneratorExp),
            ):
                return None
            s0 = max(0, getattr(binding_scope, "lineno", 1) - 1)
            e0 = max(s0, getattr(binding_scope, "end_lineno", s0 + 1) - 1)
            min_ln0 = s0 if min_ln0 is None else min(min_ln0, s0)
            max_ln0 = e0 if max_ln0 is None else max(max_ln0, e0)
        return (min_ln0, max_ln0) if min_ln0 is not None and max_ln0 is not None else None

    def _references(
        self, path: Path, line: int, character: int, *, include_declaration: bool = True
    ) -> list[dict]:
        path = path.resolve()
        text = self._text(path)
        mro_memo: dict[tuple[Path, int, int, str], list[tuple[Path, ast.ClassDef]]] = {}
        ident, target_defs = self._resolve_defs_at(path, line, character, mro_memo=mro_memo)
        if not ident:
            return []
        if not target_defs:
            # If `path` is currently syntactically invalid mid-edit, return safe same-file token matches; otherwise [].
            if self._parse_file(path) is not None:
                return []
            src_lines = text.splitlines()
            broken_refs: list[dict] = []
            for ln0, cp_start, cp_end in self._safe_name_tokens(text, ident):
                row = src_lines[ln0] if ln0 < len(src_lines) else ""
                broken_refs.append({
                    "uri": path_to_uri(path),
                    "range": {
                        "start": {"line": ln0, "character": self._codepoint_col_to_utf16(row, cp_start)},
                        "end": {"line": ln0, "character": self._codepoint_col_to_utf16(row, cp_end)},
                    },
                })
            return broken_refs

        target_keys = {self._def_key(dp, dn) for dp, dn in target_defs}
        local_span = self._local_function_span(path, target_defs)
        cand_files = (
            [path]
            if local_span is not None
            else [path, *(f.resolve() for f in self._workspace_py_files() if f.resolve() != path)]
        )
        seed_names = {ident}
        for dp, dn in target_defs:
            decl_name = self._declared_symbol_name(dp, dn)
            if decl_name:
                seed_names.add(decl_name)
        candidate_names = self._expand_import_alias_names(cand_files, seed_names)

        refs: list[dict] = []
        for cand_file in cand_files:
            src = self._text(cand_file)
            norm_src = src if src.isascii() else unicodedata.normalize("NFKC", src)
            present_names = {n for n in candidate_names if n in norm_src}
            if not present_names:
                continue
            src_lines = src.splitlines()
            for ln0, cp_start, cp_end, tok_str in self._safe_name_tokens_multi(src, present_names):
                if local_span is not None and not (local_span[0] <= ln0 <= local_span[1]):
                    continue
                row = src_lines[ln0] if ln0 < len(src_lines) else ""
                u16_start = self._codepoint_col_to_utf16(row, cp_start)
                u16_end = self._codepoint_col_to_utf16(row, cp_end)
                _, cand_defs = self._resolve_defs_at(cand_file, ln0, u16_start, mro_memo=mro_memo)
                cand_keys = {self._def_key(dp, dn) for dp, dn in cand_defs}
                if not (cand_keys & target_keys):
                    continue
                if not include_declaration:
                    tok_utf8 = len(row[:cp_start].encode("utf-8"))
                    tok_end_utf8 = len(row[:cp_end].encode("utf-8"))
                    is_decl = any(
                        dp.resolve() == cand_file
                        and getattr(dn, "lineno", 1) - 1 == ln0
                        and (
                            getattr(dn, "col_offset", -1) == tok_utf8
                            or (
                                isinstance(
                                    dn,
                                    (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef,
                                     ast.ExceptHandler, ast.MatchAs, ast.MatchStar),
                                )
                                and dn.name == tok_str
                            )
                            or (
                                isinstance(dn, ast.Attribute)
                                and dn.attr == tok_str
                                and getattr(dn, "end_col_offset", -1) == tok_end_utf8
                            )
                        )
                        for dp, dn in target_defs
                    )
                    if is_decl:
                        continue
                refs.append({
                    "uri": path_to_uri(cand_file),
                    "range": {
                        "start": {"line": ln0, "character": u16_start},
                        "end": {"line": ln0, "character": u16_end},
                    },
                })
        return refs

    def serve(self) -> int:
        while self.running:
            msg = self._read_msg()
            if msg is None:
                break
            if "_malformed" in msg:
                self._send({
                    "jsonrpc": "2.0",
                    "id": None,
                    "error": {"code": -32700, "message": str(msg["_malformed"])},
                })
                continue
            method = msg.get("method")
            msg_id = msg.get("id")
            raw_params = msg.get("params")
            params = raw_params if isinstance(raw_params, dict) else {}
            try:
                if method == "initialize":
                    root_uri = params.get("rootUri")
                    if root_uri:
                        self.root = uri_to_path(root_uri)
                    self._send({
                        "jsonrpc": "2.0",
                        "id": msg_id,
                        "result": {
                            "capabilities": {
                                "positionEncoding": "utf-16",
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
                    self.docs[p] = self._normalize_python_text(text)
                    self._ast_cache.pop(p, None)
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
                        self.docs[p] = self._normalize_python_text(text)
                        self._ast_cache.pop(p, None)
                        pub_params = {"uri": uri, "diagnostics": self._diagnostics(p, text)}
                        if isinstance(version, int):
                            pub_params["version"] = version
                        self._send({"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics", "params": pub_params})
                elif method == "textDocument/didClose":
                    td = params.get("textDocument") or {}
                    uri = td.get("uri", "")
                    if uri:
                        closed = uri_to_path(uri)
                        self.docs.pop(closed, None)
                        self._ast_cache.pop(closed, None)
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
                    ctx = params.get("context")
                    inc_decl = bool(ctx.get("includeDeclaration", True)) if isinstance(ctx, dict) else True
                    self._send({
                        "jsonrpc": "2.0",
                        "id": msg_id,
                        "result": self._references(
                            p, pos.get("line", 0), pos.get("character", 0), include_declaration=inc_decl
                        ),
                    })
                elif method == "textDocument/hover":
                    p = uri_to_path((params.get("textDocument") or {}).get("uri", ""))
                    pos = params.get("position") or {}
                    self._send({"jsonrpc": "2.0", "id": msg_id,
                                "result": self._hover(p, pos.get("line", 0), pos.get("character", 0))})
                elif msg_id is not None:
                    self._send({"jsonrpc": "2.0", "id": msg_id, "result": None})
            except Exception as exc:
                if msg_id is not None:
                    self._send({
                        "jsonrpc": "2.0",
                        "id": msg_id,
                        "error": {"code": -32603, "message": str(exc)},
                    })
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
