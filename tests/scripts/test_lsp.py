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
import unittest.mock

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

            # Changing compiler flags or directory without regenerating fails --check.
            tampered = json.loads(out.read_text(encoding="utf-8"))
            tampered[0]["arguments"] = [
                "/std:c++14" if a == "/std:c++23preview" else a for a in tampered[0]["arguments"]
            ]
            out.write_text(json.dumps(tampered, indent=2) + "\n", encoding="utf-8")
            ok_stale, msg_stale = GEN.check_database(out, ROOT)
            self.assertFalse(ok_stale)
            self.assertIn("stale directory or compiler arguments", msg_stale)

    def test_find_vs_install_resolves_vctoolsinstalldir_to_vs_root(self):
        with tempfile.TemporaryDirectory() as tmp:
            vs_root = (Path(tmp) / "VS2026" / "Community").resolve()
            msvc_ver = vs_root / "VC" / "Tools" / "MSVC" / "14.52.37025"
            (msvc_ver / "include").mkdir(parents=True)
            with unittest.mock.patch.dict(os.environ, {"VCToolsInstallDir": str(msvc_ver) + os.sep}):
                self.assertEqual(GEN.find_vs_install(), vs_root)
                if os.name == "nt":
                    self.assertIn(str(msvc_ver / "include"), GEN.discover_system_includes("99.0.0.0"))


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

                for name, compiler_err_src in (
                    ("outside_return.py", "return 1\n"),
                    ("outside_break.py", "break\n"),
                    ("bad_nonlocal.py", "def f():\n    nonlocal missing\n"),
                ):
                    with self.subTest(compiler_error=name):
                        err_file = scripts_dir / name
                        err_file.write_text(compiler_err_src, encoding="utf-8")
                        err_diags = client.diagnostics_for(err_file, wait_seconds=5.0)
                        self.assertEqual(len(err_diags), 1, err_diags)
                        self.assertEqual(err_diags[0]["severity"], 1)

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
        if os.name == "nt":
            self.assertEqual(
                LSP_MCP._uri_key("file:///d%3A/Recto/scripts/ci_status.py"),
                LSP_MCP._uri_key("file:///D:/Recto/scripts/ci_status.py"),
            )
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
                "params": {"name": "lsp_diagnostics", "arguments": {"file": "scripts/lsp_mcp.py"}},
            })
            self.assertFalse(py_diag["result"]["isError"], py_diag)
            self.assertIn("scripts/lsp_mcp.py: 0 diagnostics", py_diag["result"]["content"][0]["text"])

            py_syms = srv.handle_rpc({
                "jsonrpc": "2.0",
                "id": 5,
                "method": "tools/call",
                "params": {"name": "lsp_symbols", "arguments": {"file_or_query": "scripts/lsp_mcp.py"}},
            })
            self.assertFalse(py_syms["result"]["isError"], py_syms)
            self.assertIn("LspMcpServer", py_syms["result"]["content"][0]["text"])
        finally:
            py_proc = srv.clients["python"].proc
            srv.shutdown_all()
            if py_proc is not None:
                self.assertIsNotNone(py_proc.poll())

    def test_diagnostics_for_fails_closed_on_timeout_stale_version_or_server_exit(self):
        fake_server_code = (
            "import json, sys\n"
            "mode = sys.argv[1]\n"
            "def read_msg():\n"
            "    hdr = {}\n"
            "    while True:\n"
            "        line = sys.stdin.buffer.readline()\n"
            "        if not line: return None\n"
            "        if line in (b'\\r\\n', b'\\n'): break\n"
            "        if b':' in line:\n"
            "            k, v = line.decode('ascii').split(':', 1)\n"
            "            hdr[k.strip().lower()] = v.strip()\n"
            "    body = sys.stdin.buffer.read(int(hdr.get('content-length', '0')))\n"
            "    return json.loads(body.decode('utf-8')) if body else None\n"
            "def send(obj):\n"
            "    raw = json.dumps(obj).encode('utf-8')\n"
            "    sys.stdout.buffer.write(f'Content-Length: {len(raw)}\\r\\n\\r\\n'.encode('ascii') + raw)\n"
            "    sys.stdout.buffer.flush()\n"
            "while True:\n"
            "    msg = read_msg()\n"
            "    if msg is None: break\n"
            "    m = msg.get('method')\n"
            "    if m == 'initialize':\n"
            "        send({'jsonrpc': '2.0', 'id': msg['id'], 'result': {'capabilities': {'textDocumentSync': 1}}})\n"
            "    elif m == 'shutdown':\n"
            "        send({'jsonrpc': '2.0', 'id': msg['id'], 'result': None})\n"
            "    elif m == 'exit':\n"
            "        break\n"
            "    elif m == 'textDocument/didOpen':\n"
            "        if mode == 'crash': sys.exit(1)\n"
            "        if mode == 'stale':\n"
            "            uri = msg['params']['textDocument']['uri']\n"
            "            send({'jsonrpc': '2.0', 'method': 'textDocument/publishDiagnostics',\n"
            "                  'params': {'uri': uri, 'version': 1, 'diagnostics': []}})\n"
            "    elif m == 'textDocument/didChange':\n"
            "        if mode == 'stale':\n"
            "            uri = msg['params']['textDocument']['uri']\n"
            "            send({'jsonrpc': '2.0', 'method': 'textDocument/publishDiagnostics',\n"
            "                  'params': {'uri': uri, 'version': 1, 'diagnostics': []}})\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp)
            sample = ws / "check.py"
            sample.write_text("x = 1\n", encoding="utf-8")

            # 1. Silent server never publishes diagnostics -> TimeoutError (and MCP returns isError=True).
            srv = LSP_MCP.LspMcpServer(ws)
            srv.clients["python"] = LSP_MCP.LspClient("python", [sys.executable, "-c", fake_server_code, "silent"], ws)
            try:
                with self.assertRaises(TimeoutError):
                    srv.clients["python"].diagnostics_for(sample, wait_seconds=0.25)
            finally:
                srv.shutdown_all()

            # 2. Stale version published on didChange -> TimeoutError instead of falsely returning version 1's [].
            stale_client = LSP_MCP.LspClient("python", [sys.executable, "-c", fake_server_code, "stale"], ws)
            try:
                self.assertEqual(stale_client.diagnostics_for(sample, wait_seconds=2.0), [])
                sample.write_text("return 1\n", encoding="utf-8")
                with self.assertRaises(TimeoutError):
                    stale_client.diagnostics_for(sample, wait_seconds=0.25)
            finally:
                stale_client.stop()

            # 3. Crashing server on didOpen -> RuntimeError (and MCP handle_rpc sets isError=True, never "0 diagnostics").
            crash_srv = LSP_MCP.LspMcpServer(ws)
            crash_srv.clients["python"] = LSP_MCP.LspClient("python", [sys.executable, "-c", fake_server_code, "crash"], ws)
            try:
                reply = crash_srv.handle_rpc({
                    "jsonrpc": "2.0",
                    "id": 99,
                    "method": "tools/call",
                    "params": {"name": "lsp_diagnostics", "arguments": {"file": "check.py"}},
                })
                self.assertTrue(reply["result"]["isError"], reply)
                self.assertNotIn("0 diagnostics", reply["result"]["content"][0]["text"])
            finally:
                crash_srv.shutdown_all()

    def test_stop_sends_lsp_shutdown_and_exit_before_clearing_proc(self):
        import time

        handshake_server = (
            "import json, pathlib, sys, time\n"
            "log_file = pathlib.Path(sys.argv[1])\n"
            "seen = []\n"
            "def read_msg():\n"
            "    hdr = {}\n"
            "    while True:\n"
            "        line = sys.stdin.buffer.readline()\n"
            "        if not line: return None\n"
            "        if line in (b'\\r\\n', b'\\n'): break\n"
            "        if b':' in line:\n"
            "            k, v = line.decode('ascii').split(':', 1)\n"
            "            hdr[k.strip().lower()] = v.strip()\n"
            "    body = sys.stdin.buffer.read(int(hdr.get('content-length', '0')))\n"
            "    return json.loads(body.decode('utf-8')) if body else None\n"
            "def send(obj):\n"
            "    raw = json.dumps(obj).encode('utf-8')\n"
            "    sys.stdout.buffer.write(f'Content-Length: {len(raw)}\\r\\n\\r\\n'.encode('ascii') + raw)\n"
            "    sys.stdout.buffer.flush()\n"
            "while True:\n"
            "    msg = read_msg()\n"
            "    if msg is None:\n"
            "        time.sleep(10)\n"
            "        break\n"
            "    m = msg.get('method')\n"
            "    seen.append(m)\n"
            "    log_file.write_text(json.dumps(seen), encoding='utf-8')\n"
            "    if m == 'initialize':\n"
            "        send({'jsonrpc': '2.0', 'id': msg['id'], 'result': {'capabilities': {'textDocumentSync': 1}}})\n"
            "    elif m == 'shutdown':\n"
            "        send({'jsonrpc': '2.0', 'id': msg['id'], 'result': None})\n"
            "    elif m == 'exit':\n"
            "        sys.exit(0)\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp)
            methods_log = ws / "methods.json"
            client = LSP_MCP.LspClient("python", [sys.executable, "-c", handshake_server, str(methods_log)], ws)
            client.ensure_started()
            proc = client.proc
            self.assertIsNotNone(proc)
            t0 = time.monotonic()
            client.stop()
            elapsed = time.monotonic() - t0
            self.assertIsNone(client.proc)
            self.assertIsNotNone(proc.poll())
            self.assertLess(elapsed, 2.5, "stop() fell through to 3s timeout instead of clean LSP shutdown/exit")
            self.assertEqual(
                json.loads(methods_log.read_text(encoding="utf-8")),
                ["initialize", "initialized", "shutdown", "exit"],
            )

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
