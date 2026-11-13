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
        try:
            length = int(headers.get("content-length", "0"))
        except ValueError:
            return {"_malformed": "invalid Content-Length"}
        if length <= 0:
            return {"_malformed": "missing Content-Length"}
        body = sys.stdin.buffer.read(length)
        if not body:
            return None
        try:
            parsed = json.loads(body.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            return {"_malformed": "parse error"}
        if not isinstance(parsed, dict):
            return {"_malformed": "invalid request"}
        return parsed

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

    @staticmethod
    def _cursor_symbol(text: str, line: int, character: int) -> tuple[str | None, str, bool]:
        """Return `(receiver_chain, ident, is_attribute_access)` at 0-based `(line, character)`."""
        lines = text.splitlines()
        if line < 0 or line >= len(lines):
            return None, "", False
        row = lines[line]
        for m in re.finditer(r"[A-Za-z_][A-Za-z0-9_]*", row):
            if m.start() <= character <= m.end():
                prefix = row[:m.start()]
                is_attr = prefix.rstrip().endswith(".")
                recv_match = re.search(
                    r"([A-Za-z_][A-Za-z0-9_]*(?:\s*\.\s*[A-Za-z_][A-Za-z0-9_]*)*)\s*\.\s*$",
                    prefix,
                )
                if recv_match:
                    return re.sub(r"\s+", "", recv_match.group(1)), m.group(0), True
                return None, m.group(0), is_attr
        return None, "", False

    def _parse_file(self, path: Path) -> ast.Module | None:
        try:
            return ast.parse(self._text(path), filename=str(path))
        except SyntaxError:
            return None

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
    def _enclosing_scopes(tree: ast.Module, line1: int, col: int) -> list[ast.AST]:
        scopes: list[ast.AST] = [tree]

        def walk_body(stmts: list[ast.stmt]) -> None:
            for stmt in stmts:
                if isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                    s_ln = getattr(stmt, "lineno", 1)
                    s_col = getattr(stmt, "col_offset", 0)
                    e_ln = getattr(stmt, "end_lineno", s_ln)
                    e_col = getattr(stmt, "end_col_offset", s_col)
                    if (s_ln, s_col) <= (line1, col) <= (e_ln, e_col):
                        scopes.append(stmt)
                        walk_body(stmt.body)
                        return
                else:
                    for attr in ("body", "orelse", "finalbody"):
                        sub = getattr(stmt, attr, None)
                        if isinstance(sub, list) and sub and isinstance(sub[0], ast.stmt):
                            walk_body(sub)
                    handlers = getattr(stmt, "handlers", None)
                    if isinstance(handlers, list):
                        for h in handlers:
                            h_body = getattr(h, "body", None)
                            if isinstance(h_body, list):
                                walk_body(h_body)

        walk_body(tree.body)
        return scopes

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
                    if isinstance(t, ast.Name) and t.id == name:
                        matches.append((mod_path, t))
            elif isinstance(stmt, ast.AnnAssign) and isinstance(stmt.target, ast.Name) and stmt.target.id == name:
                matches.append((mod_path, stmt.target))
            elif isinstance(stmt, ast.ImportFrom):
                for alias in stmt.names:
                    bound = alias.asname or alias.name
                    if bound == name and alias.name != "*":
                        sub = self._resolve_module_file(stmt.module or "", stmt.level or 0, mod_path)
                        if sub is not None:
                            matches.extend(self._top_level_defs_in_module(sub, alias.name, visited))
        return matches

    def _class_member_defs(
        self, cls_path: Path, cls_node: ast.ClassDef, attr: str, visited: set[tuple[Path, str]]
    ) -> list[tuple[Path, ast.AST]]:
        key = (cls_path.resolve(), cls_node.name)
        if key in visited:
            return []
        visited = {*visited, key}
        matches: list[tuple[Path, ast.AST]] = []
        for stmt in self._iter_scope_stmts(cls_node.body):
            if isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)) and stmt.name == attr:
                matches.append((cls_path, stmt))
            elif isinstance(stmt, ast.Assign):
                for t in stmt.targets:
                    if isinstance(t, ast.Name) and t.id == attr:
                        matches.append((cls_path, t))
            elif isinstance(stmt, ast.AnnAssign) and isinstance(stmt.target, ast.Name) and stmt.target.id == attr:
                matches.append((cls_path, stmt.target))
        if matches:
            return matches
        # Check `self.<attr> = ...` assignments inside `__init__`.
        for stmt in cls_node.body:
            if isinstance(stmt, ast.FunctionDef) and stmt.name == "__init__":
                for sub in ast.walk(stmt):
                    if isinstance(sub, ast.Assign):
                        for t in sub.targets:
                            if (
                                isinstance(t, ast.Attribute)
                                and isinstance(t.value, ast.Name)
                                and t.value.id == "self"
                                and t.attr == attr
                            ):
                                matches.append((cls_path, t))
        return matches

    def _lookup_in_scope(
        self,
        scope: ast.AST,
        ident: str,
        path: Path,
        tree: ast.Module,
        path_consts: dict[str, Path],
        str_consts: dict[str, str],
    ) -> tuple[list[tuple[Path, ast.AST]], Path | None, bool, str | None]:
        """Return `(definitions, module_path, is_external_import, constructed_class_expr)` for `ident` in `scope`."""
        if isinstance(scope, (ast.FunctionDef, ast.AsyncFunctionDef)):
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

        body = getattr(scope, "body", None)
        if not isinstance(body, list):
            return [], None, False, None

        defs: list[tuple[Path, ast.AST]] = []
        mod_target: Path | None = None
        is_ext = False
        cls_expr: str | None = None

        for stmt in self._iter_scope_stmts(body):
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
                    if isinstance(t, ast.Name) and t.id == ident:
                        defs.append((path, t))
                        if isinstance(stmt.value, ast.Call):
                            dyn = self._resolve_dynamic_module_call(stmt.value, tree, path, path_consts, str_consts)
                            if dyn is not None:
                                mod_target = dyn
                            elif isinstance(stmt.value.func, (ast.Name, ast.Attribute)):
                                cls_expr = ast.unparse(stmt.value.func)
            elif isinstance(stmt, ast.AnnAssign) and isinstance(stmt.target, ast.Name) and stmt.target.id == ident:
                defs.append((path, stmt.target))
                if isinstance(stmt.annotation, (ast.Name, ast.Attribute)):
                    cls_expr = ast.unparse(stmt.annotation)
                if isinstance(stmt.value, ast.Call):
                    dyn = self._resolve_dynamic_module_call(stmt.value, tree, path, path_consts, str_consts)
                    if dyn is not None:
                        mod_target = dyn

        return defs, mod_target, is_ext, cls_expr

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
    ) -> tuple[Path, ast.ClassDef] | None:
        parts = cls_expr.split(".")
        if len(parts) == 1:
            for sc in self._lexical_scopes_for_lookup(scopes):
                defs, _, _, _ = self._lookup_in_scope(sc, parts[0], path, tree, path_consts, str_consts)
                for def_path, node in defs:
                    if isinstance(node, ast.ClassDef):
                        return def_path, node
            return None
        # Dotted class expression such as `LSP_MCP.LspMcpServer`.
        for sc in self._lexical_scopes_for_lookup(scopes):
            defs, mod_target, is_ext, _ = self._lookup_in_scope(sc, parts[0], path, tree, path_consts, str_consts)
            if is_ext:
                return None
            if mod_target is not None:
                cur_mod = mod_target
                for seg in parts[1:-1]:
                    nxt = self._resolve_module_file(seg, 0, cur_mod)
                    if nxt is None:
                        return None
                    cur_mod = nxt
                for def_path, node in self._top_level_defs_in_module(cur_mod, parts[-1], set()):
                    if isinstance(node, ast.ClassDef):
                        return def_path, node
            if defs:
                break
        return None

    def _resolve_defs_at(self, path: Path, line: int, character: int) -> tuple[str, list[tuple[Path, ast.AST]]]:
        text = self._text(path)
        receiver, ident, is_attr = self._cursor_symbol(text, line, character)
        if not ident:
            return "", []
        tree = self._parse_file(path)
        if tree is None:
            return ident, []

        path_consts, str_consts = self._collect_module_consts(tree, path)
        scopes = self._enclosing_scopes(tree, line + 1, character)

        if is_attr:
            if receiver is None:
                return ident, []
            if receiver in ("self", "cls"):
                for sc in reversed(scopes):
                    if isinstance(sc, ast.ClassDef):
                        return ident, self._class_member_defs(path, sc, ident, set())
                return ident, []

            recv_parts = receiver.split(".")
            head = recv_parts[0]
            for sc in self._lexical_scopes_for_lookup(scopes):
                defs, mod_target, is_ext, cls_expr = self._lookup_in_scope(
                    sc, head, path, tree, path_consts, str_consts
                )
                if not defs and mod_target is None and not is_ext and cls_expr is None:
                    continue
                if is_ext:
                    return ident, []
                if mod_target is not None:
                    cur_mod = mod_target
                    cur_cls: tuple[Path, ast.ClassDef] | None = None
                    for seg in recv_parts[1:]:
                        if cur_cls is not None:
                            sub_defs = self._class_member_defs(cur_cls[0], cur_cls[1], seg, set())
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
                        return ident, self._class_member_defs(cur_cls[0], cur_cls[1], ident, set())
                    return ident, self._top_level_defs_in_module(cur_mod, ident, set())

                # `head` might itself be a local/imported ClassDef (e.g. `StdlibPythonLsp._word_at`).
                cls_direct = next(((p, n) for p, n in defs if isinstance(n, ast.ClassDef)), None)
                if cls_direct is not None and len(recv_parts) == 1:
                    return ident, self._class_member_defs(cls_direct[0], cls_direct[1], ident, set())

                if cls_expr and len(recv_parts) == 1:
                    resolved_cls = self._resolve_class_expr(cls_expr, scopes, path, tree, path_consts, str_consts)
                    if resolved_cls is not None:
                        return ident, self._class_member_defs(resolved_cls[0], resolved_cls[1], ident, set())
                return ident, []
            return ident, []

        for sc in self._lexical_scopes_for_lookup(scopes):
            defs, _, _, _ = self._lookup_in_scope(sc, ident, path, tree, path_consts, str_consts)
            if defs:
                return ident, defs
        return ident, []

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
            ln = max(0, getattr(node, "lineno", 1) - 1)
            col = max(0, getattr(node, "col_offset", 0))
            uri = path_to_uri(def_path)
            key = (uri, ln, col)
            if key in seen:
                continue
            seen.add(key)
            locs.append({
                "uri": uri,
                "range": {"start": {"line": ln, "character": col},
                          "end": {"line": ln, "character": col + max(1, len(ident))}},
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
