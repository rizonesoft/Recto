"""Tests for Recto's portable C++23 (clangd) and Python LSP servers, compilation database generator, and MCP bridge."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"


def _load(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


GEN = _load("gen_compile_commands", "gen-compile-commands.py")
CLANGD_LSP = _load("clangd_lsp", "clangd-lsp.py")
PYTHON_LSP = _load("python_lsp", "python-lsp.py")
LSP_MCP = _load("lsp_mcp", "lsp_mcp.py")


class PortableConfigs(unittest.TestCase):
    """Every LSP and MCP config in the repository is valid JSON and free of machine-specific paths."""

    CONFIGS = (
        ROOT / ".lsp.json",
        ROOT / ".mcp.json",
        ROOT / "pyrightconfig.json",
        ROOT / ".agents" / "plugins" / "recto-lsp" / "plugin.json",
        ROOT / ".agents" / "plugins" / "recto-lsp" / "mcp_config.json",
    )

    def test_configs_parse_and_have_no_hardcoded_machine_paths(self):
        for cfg in self.CONFIGS:
            with self.subTest(config=cfg.relative_to(ROOT).as_posix()):
                self.assertTrue(cfg.is_file(), f"missing {cfg}")
                raw = cfg.read_text(encoding="utf-8")
                parsed = json.loads(raw)
                self.assertIsInstance(parsed, dict)
                lowered = raw.lower()
                self.assertNotIn("c:\\users", lowered)
                self.assertNotIn("c:/users", lowered)
                self.assertNotIn("d:\\recto", lowered)
                self.assertNotIn("d:/recto", lowered)
                self.assertNotIn("derickpayne", lowered)


class CompileCommandsGenerator(unittest.TestCase):
    """scripts/gen-compile-commands.py covers every Recto.sln C++ project, including wildcard test globs."""

    def test_database_covers_all_solution_projects_and_wildcards(self):
        entries = GEN.build_database(ROOT)
        self.assertGreaterEqual(len(entries), 60)
        files = {Path(e["file"]).resolve() for e in entries}
        self.assertIn((ROOT / "src" / "pdf" / "error.cpp").resolve(), files)
        self.assertIn((ROOT / "src" / "app" / "main.cpp").resolve(), files)
        # Wildcard glob in recto-tests.vcxproj (`ui_*_tests.cpp`) must expand to concrete translation units.
        ui_tests = [f for f in files if f.name.startswith("ui_") and f.name.endswith("_tests.cpp")]
        self.assertGreaterEqual(len(ui_tests), 2)
        for entry in entries:
            self.assertIn("/std:c++23preview", entry["arguments"])

    def test_write_and_check_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "compile_commands.json"
            ok, msg = GEN.check_database(out, ROOT)
            self.assertFalse(ok)
            self.assertIn("missing", msg)
            GEN.write_database(out, ROOT)
            ok, msg = GEN.check_database(out, ROOT)
            self.assertTrue(ok, msg)


class BuiltinStdlibPythonLsp(unittest.TestCase):
    """scripts/python-lsp.py --builtin runs with zero external dependencies when pyright-langserver is absent."""

    def test_builtin_lsp_diagnostics_symbols_definition_references_and_hover(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp)
            scripts_dir = ws / "scripts"
            scripts_dir.mkdir()
            good = scripts_dir / "sample.py"
            good.write_text(
                'def compute_value(x: int) -> int:\n'
                '    """Double the input."""\n'
                '    return x * 2\n\n'
                'RESULT = compute_value(21)\n',
                encoding="utf-8",
            )
            bad = scripts_dir / "broken.py"
            bad.write_text("def broken(:\n    pass\n", encoding="utf-8")

            client = LSP_MCP.LspClient(
                "python",
                [sys.executable, "-B", str(SCRIPTS / "python-lsp.py"), "--builtin"],
                ws,
            )
            try:
                self.assertEqual(client.diagnostics_for(good, wait_seconds=5.0), [])
                bad_diags = client.diagnostics_for(bad, wait_seconds=5.0)
                self.assertEqual(len(bad_diags), 1)
                self.assertEqual(bad_diags[0]["severity"], 1)

                uri = good.resolve().as_uri()
                syms = client.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
                names = {s["name"] for s in syms}
                self.assertEqual(names, {"compute_value", "RESULT"})

                # Hover and go-to-definition on `compute_value` call at line 4 (0-based), col 12
                hover = client.request(
                    "textDocument/hover",
                    {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 12}},
                )
                self.assertIn("def compute_value(x: int) -> int", hover["contents"]["value"])
                self.assertIn("Double the input.", hover["contents"]["value"])

                defs = client.request(
                    "textDocument/definition",
                    {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 12}},
                )
                self.assertEqual(len(defs), 1)
                self.assertEqual(defs[0]["range"]["start"]["line"], 0)

                refs = client.request(
                    "textDocument/references",
                    {
                        "textDocument": {"uri": uri},
                        "position": {"line": 0, "character": 5},
                        "context": {"includeDeclaration": True},
                    },
                )
                self.assertEqual(len(refs), 2)
            finally:
                proc = client.proc
                client.stop()
                if proc is not None:
                    self.assertIsNotNone(proc.poll())


class LspMcpBridge(unittest.TestCase):
    """scripts/lsp_mcp.py serves MCP JSON-RPC tools over Python and C++23 language servers and cleans up children."""

    def test_mcp_protocol_and_python_tools(self):
        srv = LSP_MCP.LspMcpServer(ROOT)
        try:
            init = srv.handle_rpc({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
            self.assertEqual(init["result"]["serverInfo"]["name"], "recto-lsp")

            tools_reply = srv.handle_rpc({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
            tool_names = {t["name"] for t in tools_reply["result"]["tools"]}
            self.assertEqual(
                tool_names,
                {"lsp_diagnostics", "lsp_symbols", "lsp_definition", "lsp_references", "lsp_hover"},
            )

            # Outside-repo path traversal must return an error tool result.
            outside = srv.handle_rpc({
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {"name": "lsp_diagnostics", "arguments": {"file": "../outside.py"}},
            })
            self.assertTrue(outside["result"]["isError"])

            py_diag = srv.handle_rpc({
                "jsonrpc": "2.0",
                "id": 4,
                "method": "tools/call",
                "params": {"name": "lsp_diagnostics", "arguments": {"file": "scripts/ci_status.py"}},
            })
            self.assertFalse(py_diag["result"]["isError"], py_diag)
            self.assertIn("scripts/ci_status.py: 0 diagnostics", py_diag["result"]["content"][0]["text"])

            py_syms = srv.handle_rpc({
                "jsonrpc": "2.0",
                "id": 5,
                "method": "tools/call",
                "params": {"name": "lsp_symbols", "arguments": {"file_or_query": "scripts/ci_status.py"}},
            })
            self.assertFalse(py_syms["result"]["isError"], py_syms)
            self.assertIn("main", py_syms["result"]["content"][0]["text"])
        finally:
            py_proc = srv.clients["python"].proc
            srv.shutdown_all()
            if py_proc is not None:
                self.assertIsNotNone(py_proc.poll())

    @unittest.skipUnless(CLANGD_LSP.find_clangd(), "clangd not installed on this machine")
    def test_cpp23_clangd_diagnostics_symbols_and_hover(self):
        GEN.write_database(ROOT / "build" / "compile_commands.json", ROOT)
        srv = LSP_MCP.LspMcpServer(ROOT)
        try:
            cpp_diag = srv.handle_rpc({
                "jsonrpc": "2.0",
                "id": 10,
                "method": "tools/call",
                "params": {"name": "lsp_diagnostics", "arguments": {"file": "src/pdf/error.cpp"}},
            })
            self.assertFalse(cpp_diag["result"]["isError"], cpp_diag)
            self.assertIn("src/pdf/error.cpp: 0 diagnostics", cpp_diag["result"]["content"][0]["text"])

            cpp_syms = srv.handle_rpc({
                "jsonrpc": "2.0",
                "id": 11,
                "method": "tools/call",
                "params": {"name": "lsp_symbols", "arguments": {"file_or_query": "src/pdf/error.cpp"}},
            })
            self.assertFalse(cpp_syms["result"]["isError"], cpp_syms)
            self.assertIn("classify_diagnostic_id", cpp_syms["result"]["content"][0]["text"])

            # Hover on `classify_diagnostic_id` at line 701, col 16 in src/pdf/error.cpp
            cpp_hover = srv.handle_rpc({
                "jsonrpc": "2.0",
                "id": 12,
                "method": "tools/call",
                "params": {
                    "name": "lsp_hover",
                    "arguments": {"file": "src/pdf/error.cpp", "line": 701, "character": 16},
                },
            })
            self.assertFalse(cpp_hover["result"]["isError"], cpp_hover)
            self.assertIn("classify_diagnostic_id", cpp_hover["result"]["content"][0]["text"])
        finally:
            cpp_proc = srv.clients["cpp"].proc
            srv.shutdown_all()
            if cpp_proc is not None:
                self.assertIsNotNone(cpp_proc.poll())


if __name__ == "__main__":
    unittest.main()
