"""Tests for Recto's portable C++23 (clangd) and Python LSP servers, compilation database generator, and MCP bridge."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
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

    def test_find_vs_install_skips_dotnet_only_installs_and_requires_cpp_headers(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp).resolve()
            pf86 = base / "PF86"
            vswhere = pf86 / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
            vswhere.parent.mkdir(parents=True)
            vswhere.write_text("", encoding="utf-8")

            dotnet_vs = base / "PF" / "Microsoft Visual Studio" / "18" / "Enterprise"
            (dotnet_vs / "MSBuild").mkdir(parents=True)

            cpp_vs = base / "PF" / "Microsoft Visual Studio" / "18" / "Community"
            (cpp_vs / "VC" / "Tools" / "MSVC" / "14.52.37025" / "include").mkdir(parents=True)
            clangd_exe = cpp_vs / "VC" / "Tools" / "Llvm" / "x64" / "bin" / "clangd.exe"
            clangd_exe.parent.mkdir(parents=True)
            clangd_exe.write_text("", encoding="utf-8")

            seen_cmds: list[list[str]] = []

            def fake_run(cmd, *args, **kwargs):
                seen_cmds.append(list(cmd))
                out = f"{dotnet_vs}\n{cpp_vs}\n"
                return subprocess.CompletedProcess(cmd, 0, stdout=out, stderr="")

            env_patch = {
                "VCToolsInstallDir": "",
                "RECTO_CLANGD": "",
                "ProgramFiles": str(base / "PF"),
                "ProgramFiles(x86)": str(pf86),
            }
            with unittest.mock.patch.dict(os.environ, env_patch, clear=False):
                with unittest.mock.patch.object(GEN.subprocess, "run", side_effect=fake_run):
                    self.assertEqual(GEN.find_vs_install(), cpp_vs)
                    self.assertTrue(any("-requires" in c for c in seen_cmds), seen_cmds)
                if os.name == "nt":
                    with unittest.mock.patch.object(CLANGD_LSP, "load_gen", return_value=GEN):
                        with unittest.mock.patch.object(GEN.subprocess, "run", side_effect=fake_run):
                            with unittest.mock.patch.object(CLANGD_LSP.shutil, "which", return_value=None):
                                self.assertEqual(Path(CLANGD_LSP.find_clangd() or ""), clangd_exe)

    def test_ensure_compile_commands_regenerates_after_checkout_move(self):
        with tempfile.TemporaryDirectory() as tmp:
            parent = Path(tmp).resolve()
            checkout_a = parent / "checkout_a"
            src_dir = checkout_a / "src" / "mini"
            src_dir.mkdir(parents=True)
            (src_dir / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
            (checkout_a / "Directory.Build.props").write_text(
                "<Project><PropertyGroup><WindowsTargetPlatformVersion>10.0.26100.0</WindowsTargetPlatformVersion>"
                "</PropertyGroup></Project>\n",
                encoding="utf-8",
            )
            (src_dir / "mini.vcxproj").write_text(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                '  <ItemDefinitionGroup Condition="\'$(Configuration)|$(Platform)\'==\'Debug|x64\'">\n'
                "    <ClCompile><AdditionalIncludeDirectories>$(SolutionDir)src</AdditionalIncludeDirectories></ClCompile>\n"
                "  </ItemDefinitionGroup>\n"
                '  <ItemGroup><ClCompile Include="main.cpp" /></ItemGroup>\n'
                "</Project>\n",
                encoding="utf-8",
            )
            (checkout_a / "Recto.sln").write_text(
                'Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "mini", "src\\mini\\mini.vcxproj", '
                '"{11111111-1111-1111-1111-111111111111}"\nEndProject\n',
                encoding="utf-8",
            )

            db_a = CLANGD_LSP.ensure_compile_commands(checkout_a)
            entries_a = json.loads(db_a.read_text(encoding="utf-8"))
            self.assertTrue(entries_a[0]["directory"].startswith(str(checkout_a)))

            # Move checkout keeping build/compile_commands.json with preserved timestamps.
            checkout_b = parent / "checkout_b"
            shutil.move(str(checkout_a), str(checkout_b))
            db_b = CLANGD_LSP.ensure_compile_commands(checkout_b)
            entries_b = json.loads(db_b.read_text(encoding="utf-8"))
            for item in entries_b:
                self.assertTrue(item["directory"].startswith(str(checkout_b)), item)
                self.assertTrue(item["file"].startswith(str(checkout_b)), item)
                for arg in item["arguments"]:
                    self.assertNotIn(str(checkout_a), arg)
            ok, msg = GEN.check_database(db_b, checkout_b)
            self.assertTrue(ok, msg)

    def test_pch_forced_include_covers_consumers_omits_pch_cpp_and_stays_project_local(self):
        entries = GEN.build_database(ROOT)
        self.assertGreaterEqual(len(entries), 74)
        pch_creators = [e for e in entries if Path(e["file"]).name.lower() == "pch.cpp"]
        consumers = [e for e in entries if Path(e["file"]).name.lower() != "pch.cpp"]
        self.assertEqual(len(pch_creators), 8)
        for creator in pch_creators:
            fi_args = [a for a in creator["arguments"] if a.startswith(("/FI", "-FI", "-include"))]
            self.assertEqual(fi_args, [], f"pch.cpp must not carry /FI: {creator}")
            self.assertIn("-DWIN32_LEAN_AND_MEAN", creator["arguments"])
            self.assertIn("-DNOMINMAX", creator["arguments"])
        for unit in consumers:
            expected_pch = (Path(unit["directory"]) / "pch.h").resolve()
            self.assertTrue(expected_pch.is_file(), f"missing {expected_pch}")
            fi_args = [a for a in unit["arguments"] if a.startswith(("/FI", "-FI"))]
            self.assertEqual(fi_args, [f"/FI{expected_pch}"], f"wrong /FI for {unit['file']}: {fi_args}")
            self.assertIn("-DWIN32_LEAN_AND_MEAN", unit["arguments"])
            self.assertIn("-DNOMINMAX", unit["arguments"])
            if Path(unit["directory"]).name == "recto-tests":
                self.assertIn("-DGTEST_LINKED_AS_SHARED_LIBRARY=1", unit["arguments"])

        # Synthetic multi-project check: proj_b lists ..\proj_a in AdditionalIncludeDirectories and has a consumer in
        # a nested subfolder; it must still force-include proj_b\pch.h (never proj_a\pch.h), omit /FI on pch.cpp, and
        # leave a project without pch.h untouched.
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp).resolve()
            proj_a = ws / "src" / "proj_a"
            proj_b = ws / "src" / "proj_b"
            proj_b_sub = proj_b / "sub"
            proj_c = ws / "src" / "proj_c"
            for d in (proj_a, proj_b_sub, proj_c):
                d.mkdir(parents=True)
            (ws / "Directory.Build.props").write_text(
                "<Project><PropertyGroup><WindowsTargetPlatformVersion>10.0.26100.0</WindowsTargetPlatformVersion>"
                "</PropertyGroup></Project>\n",
                encoding="utf-8",
            )
            (ws / "Directory.Build.targets").write_text(
                "<Project><ItemDefinitionGroup><ClCompile>"
                "<PreprocessorDefinitions>WIN32_LEAN_AND_MEAN;NOMINMAX;%(PreprocessorDefinitions)</PreprocessorDefinitions>"
                "</ClCompile></ItemDefinitionGroup></Project>\n",
                encoding="utf-8",
            )
            (proj_a / "pch.h").write_text("#pragma once\n", encoding="utf-8")
            (proj_a / "pch.cpp").write_text('#include "pch.h"\n', encoding="utf-8")
            (proj_a / "a.cpp").write_text("int a() { return 1; }\n", encoding="utf-8")
            (proj_a / "proj_a.vcxproj").write_text(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                '  <ItemGroup>\n'
                '    <ClCompile Include="pch.cpp"><PrecompiledHeader>Create</PrecompiledHeader><ForcedIncludeFiles /></ClCompile>\n'
                '    <ClCompile Include="a.cpp" />\n'
                '  </ItemGroup>\n'
                '</Project>\n',
                encoding="utf-8",
            )
            (proj_b / "pch.h").write_text("#pragma once\n", encoding="utf-8")
            (proj_b / "pch.cpp").write_text('#include "pch.h"\n', encoding="utf-8")
            (proj_b_sub / "b.cpp").write_text("int b() { return 2; }\n", encoding="utf-8")
            (proj_b / "proj_b.vcxproj").write_text(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                '  <ItemDefinitionGroup><ClCompile>'
                '<AdditionalIncludeDirectories>..\\proj_a;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>'
                '</ClCompile></ItemDefinitionGroup>\n'
                '  <ItemGroup>\n'
                '    <ClCompile Include="pch.cpp"><PrecompiledHeader>Create</PrecompiledHeader><ForcedIncludeFiles /></ClCompile>\n'
                '    <ClCompile Include="sub\\b.cpp" />\n'
                '  </ItemGroup>\n'
                '</Project>\n',
                encoding="utf-8",
            )
            (proj_c / "c.cpp").write_text("int c() { return 3; }\n", encoding="utf-8")
            (proj_c / "proj_c.vcxproj").write_text(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                '  <ItemGroup><ClCompile Include="c.cpp" /></ItemGroup>\n'
                '</Project>\n',
                encoding="utf-8",
            )
            (ws / "Recto.sln").write_text(
                'Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "proj_a", "src\\proj_a\\proj_a.vcxproj", "{11111111-1111-1111-1111-111111111111}"\nEndProject\n'
                'Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "proj_b", "src\\proj_b\\proj_b.vcxproj", "{22222222-2222-2222-2222-222222222222}"\nEndProject\n'
                'Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "proj_c", "src\\proj_c\\proj_c.vcxproj", "{33333333-3333-3333-3333-333333333333}"\nEndProject\n',
                encoding="utf-8",
            )
            by_file = {Path(e["file"]).resolve(): e for e in GEN.build_database(ws)}
            self.assertEqual([a for a in by_file[(proj_b / "pch.cpp").resolve()]["arguments"] if a.startswith("/FI")], [])
            self.assertEqual(
                [a for a in by_file[(proj_b_sub / "b.cpp").resolve()]["arguments"] if a.startswith("/FI")],
                [f"/FI{(proj_b / 'pch.h').resolve()}"],
            )
            self.assertEqual([a for a in by_file[(proj_c / "c.cpp").resolve()]["arguments"] if a.startswith("/FI")], [])

    def test_real_clangd_check_passes_on_consumer_unit_with_forced_pch(self):
        if os.name != "nt":
            self.skipTest("Windows MSVC/SDK headers required for real clangd --check")
        clangd = CLANGD_LSP.find_clangd()
        if not clangd:
            self.skipTest("clangd not installed on this host")
        with tempfile.TemporaryDirectory() as tmp:
            db_dir = Path(tmp).resolve()
            GEN.write_database(db_dir / "compile_commands.json", ROOT)
            check = subprocess.run(
                [clangd, f"--compile-commands-dir={db_dir}", f"--check={ROOT / 'src' / 'core' / 'core.cpp'}"],
                capture_output=True, text=True, timeout=180,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            )
            escaped_pch = str((ROOT / "src" / "core" / "pch.h").resolve()).replace("\\", "\\\\")
            self.assertEqual(check.returncode, 0, check.stdout + check.stderr)
            self.assertIn(f"/FI{escaped_pch}", check.stdout + check.stderr)
            self.assertIn("All checks completed, 0 errors", check.stdout + check.stderr)


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

    def test_builtin_lsp_resolves_receivers_imports_and_lexical_scopes(self):
        lsp = PYTHON_LSP.StdlibPythonLsp(ROOT)
        unattended_run = ROOT / "scripts" / "unattended-run.py"
        unattended_lib = (ROOT / "scripts" / "lib" / "unattended.py").resolve()
        # `creationflags=lib.windowless()` at 1-based line 114, col 49 (0-based 113, 48).
        defs = lsp._definitions(unattended_run, 113, 48)
        self.assertEqual(len(defs), 1, defs)
        self.assertEqual(defs[0]["uri"], unattended_lib.as_uri())
        hover = lsp._hover(unattended_run, 113, 48)
        self.assertIsNotNone(hover)
        assert hover is not None
        self.assertIn("def windowless()", hover["contents"]["value"])

        # Loop-variable binding on live `scripts/run-tests.py` (`triggers = HEAVY_MODULES.get(mod)` -> preceding `for mod`).
        run_tests_py = (ROOT / "scripts" / "run-tests.py").resolve()
        rt_lines = run_tests_py.read_text(encoding="utf-8").splitlines()
        trig_ln = next(i for i, ln in enumerate(rt_lines) if "triggers = HEAVY_MODULES.get(mod)" in ln)
        mod_col = rt_lines[trig_ln].index("(mod)") + 1
        mod_defs = lsp._definitions(run_tests_py, trig_ln, mod_col)
        self.assertEqual(len(mod_defs), 1, mod_defs)
        self.assertEqual(mod_defs[0]["uri"], run_tests_py.as_uri())
        self.assertEqual(mod_defs[0]["range"]["start"]["line"], trig_ln - 1)

        # `global _WIN32` on live `scripts/review-dispatch.py` keeps declaration, `global`, reads, and reassignments unified.
        review_dispatch_py = (ROOT / "scripts" / "review-dispatch.py").resolve()
        rd_lines = review_dispatch_py.read_text(encoding="utf-8").splitlines()
        win32_decl_ln = next(i for i, ln in enumerate(rd_lines) if ln.startswith("_WIN32 = None"))
        win32_refs = lsp._references(review_dispatch_py, win32_decl_ln, 0, include_declaration=True)
        win32_ref_lines = [r["range"]["start"]["line"] for r in win32_refs if r["uri"] == review_dispatch_py.as_uri()]
        expected_win32_lines = [i for i, ln in enumerate(rd_lines) if "_WIN32" in ln]
        self.assertEqual(win32_ref_lines, expected_win32_lines)

        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp)
            pkg = ws / "scripts"
            pkg.mkdir()
            (pkg / "noise.py").write_text(
                'def shared_name() -> str:\n    """Wrong unrelated module."""\n    return "wrong"\n'
                'def loads(s: str) -> str:\n    return s\n',
                encoding="utf-8",
            )
            mod_a = pkg / "helper_mod.py"
            mod_a.write_text(
                'def shared_name() -> int:\n    """Imported target."""\n    return 7\n',
                encoding="utf-8",
            )
            caller = pkg / "caller.py"
            caller.write_text(
                "import json\n"
                "from helper_mod import shared_name\n\n"
                "def outer() -> int:\n"
                "    def shared_name() -> int:\n"
                '        """Inner lexical shadow."""\n'
                "        return 99\n"
                "    _ = json.loads('{}')\n"
                "    return shared_name()\n\n"
                "TOP = shared_name()\n",
                encoding="utf-8",
            )
            scoped = PYTHON_LSP.StdlibPythonLsp(ws)
            # Stdlib `json.loads` on 0-based line 7 col 14 must NOT match `noise.py`'s `def loads`.
            self.assertEqual(scoped._definitions(caller, 7, 14), [])
            # Inside `outer()`, `shared_name()` on 0-based line 8 col 13 resolves to inner lexical `def shared_name` (line 4).
            inner_defs = scoped._definitions(caller, 8, 13)
            self.assertEqual(len(inner_defs), 1)
            self.assertEqual(inner_defs[0]["uri"], caller.resolve().as_uri())
            self.assertEqual(inner_defs[0]["range"]["start"]["line"], 4)
            # At module top-level, `shared_name()` on 0-based line 10 col 8 resolves to `helper_mod.py`, never `noise.py`.
            top_defs = scoped._definitions(caller, 10, 8)
            self.assertEqual(len(top_defs), 1)
            self.assertEqual(top_defs[0]["uri"], mod_a.resolve().as_uri())
            self.assertEqual(top_defs[0]["range"]["start"]["line"], 0)

            bindings_file = pkg / "bindings.py"
            bindings_file.write_text(
                "def demo(pairs, ctx):\n"
                "    first_k, *rest_v = pairs[0]\n"
                "    for k, v in pairs:\n"
                "        _ = k + v + first_k + len(rest_v)\n"
                "    with ctx as handle:\n"
                "        _ = handle\n"
                "    try:\n"
                "        pass\n"
                "    except ValueError as err:\n"
                "        _ = str(err)\n"
                "    vals = [item * 2 for item in pairs]\n"
                "    fn = lambda arg_x: arg_x + len(vals)\n"
                "    if (walrus := fn(1)):\n"
                "        return walrus\n",
                encoding="utf-8",
            )
            # `k` at line 3 col 12 -> `for k, v` at line 2; `first_k` at line 3 col 20 -> line 1;
            # `handle` at line 5 col 12 -> line 4; `err` at line 9 col 16 -> line 8;
            # `item` at line 10 col 12 -> line 10; `arg_x` at line 11 col 23 -> line 11; `walrus` at line 13 col 15 -> line 12.
            for use_ln, use_col, expected_ln in (
                (3, 12, 2),
                (3, 20, 1),
                (5, 12, 4),
                (9, 16, 8),
                (10, 12, 10),
                (11, 23, 11),
                (13, 15, 12),
            ):
                with self.subTest(binding_at=(use_ln, use_col)):
                    b_defs = scoped._definitions(bindings_file, use_ln, use_col)
                    self.assertEqual(len(b_defs), 1, b_defs)
                    self.assertEqual(b_defs[0]["range"]["start"]["line"], expected_ln)

            # 8. Python header scoping: decorators, parameter defaults, annotations, and class bases evaluate in enclosing scope.
            header_scope_file = pkg / "header_scopes.py"
            header_scope_file.write_text(
                "x = 1\n"
                "def wrap(fn):\n"
                "    return fn\n"
                "@wrap\n"
                "def f(x: x = x) -> x:\n"
                "    return x\n"
                "class Base:\n"
                "    pass\n"
                "class Base(Base):\n"
                "    pass\n",
                encoding="utf-8",
            )
            # Line 4: `def f(x: x = x) -> x:` -> param `x` (col 6) -> line 4; `: x` (col 9), `= x` (col 13), `-> x` (col 19) -> line 0;
            # Line 5: `    return x` (col 11) -> line 4; Line 8: `class Base(Base):` -> decl (col 6) -> line 8, base (col 11) -> line 6.
            for h_ln, h_col, expected_ln in (
                (4, 6, 4),
                (4, 9, 0),
                (4, 13, 0),
                (4, 19, 0),
                (5, 11, 4),
                (8, 6, 8),
                (8, 11, 6),
            ):
                with self.subTest(header_pos=(h_ln, h_col)):
                    h_defs = scoped._definitions(header_scope_file, h_ln, h_col)
                    self.assertEqual(len(h_defs), 1, h_defs)
                    self.assertEqual(h_defs[0]["range"]["start"]["line"], expected_ln)

            # 9. Cross-file `from ... import ... as ...` alias references resolve both ways without false positives.
            alias_caller = pkg / "alias_caller.py"
            alias_caller.write_text(
                "from helper_mod import shared_name as sn\n"
                "val = sn()\n",
                encoding="utf-8",
            )
            unrelated_alias = pkg / "unrelated_sn.py"
            unrelated_alias.write_text("sn = 42\n_ = sn\n", encoding="utf-8")

            helper_uri = mod_a.resolve().as_uri()
            alias_uri = alias_caller.resolve().as_uri()
            unrelated_uri = unrelated_alias.resolve().as_uri()

            refs_from_def = scoped._references(mod_a, 0, 4, include_declaration=True)
            ref_sites = {(r["uri"], r["range"]["start"]["line"], r["range"]["start"]["character"]) for r in refs_from_def}
            self.assertIn((helper_uri, 0, 4), ref_sites)
            self.assertIn((alias_uri, 0, 23), ref_sites)  # `shared_name` in `from helper_mod import shared_name as sn`
            self.assertIn((alias_uri, 0, 38), ref_sites)  # `sn` in `as sn`
            self.assertIn((alias_uri, 1, 6), ref_sites)   # `sn` in `val = sn()`
            self.assertTrue(all(r["uri"] != unrelated_uri for r in refs_from_def))

            refs_from_alias_excl = scoped._references(alias_caller, 1, 6, include_declaration=False)
            excl_sites = {(r["uri"], r["range"]["start"]["line"], r["range"]["start"]["character"]) for r in refs_from_alias_excl}
            self.assertNotIn((helper_uri, 0, 4), excl_sites)
            self.assertIn((alias_uri, 1, 6), excl_sites)

            # 10. `nonlocal` reassignments resolve to the enclosing function binding rather than creating a local shadow.
            closure_file = pkg / "closure_nonlocal.py"
            closure_file.write_text(
                "def make_counter():\n"
                "    count = 0\n"
                "    def inc():\n"
                "        nonlocal count\n"
                "        count += 1\n"
                "        return count\n"
                "    return inc\n",
                encoding="utf-8",
            )
            nl_defs = scoped._definitions(closure_file, 4, 8)
            self.assertEqual(len(nl_defs), 1, nl_defs)
            self.assertEqual(nl_defs[0]["range"]["start"]["line"], 1)
            nl_refs = scoped._references(closure_file, 1, 4, include_declaration=True)
            self.assertEqual([r["range"]["start"]["line"] for r in nl_refs], [1, 3, 4, 5])

            # 11. Function-local bindings never fall through to enclosing scopes (`UnboundLocalError`), while class bodies do.
            exec_model_file = pkg / "exec_model.py"
            exec_model_file.write_text(
                "x = 1\n"
                "def fn_self_assign():\n"
                "    x = x + 1\n"
                "def fn_read_before_write():\n"
                "    y = x\n"
                "    x = 2\n"
                "    return y\n"
                "class ClsBody:\n"
                "    a = x\n"
                "    def method_default(self, v=x):\n"
                "        return v\n"
                "    x = x + 1\n"
                "    b = x\n",
                encoding="utf-8",
            )
            # `(2, 8)` (`x = x + 1` RHS in `fn_self_assign`) -> function-local line 2 (never module line 0).
            # `(4, 8)` (`y = x` before `x = 2` in `fn_read_before_write`) -> function-local line 5 (never module line 0).
            # `(8, 8)` (`a = x`), `(9, 31)` (`v=x` method default), and `(11, 8)` (`x = x + 1` RHS) -> module line 0; `(12, 8)` -> line 11.
            for em_ln, em_col, expected_ln in (
                (2, 8, 2),
                (4, 8, 5),
                (8, 8, 0),
                (9, 31, 0),
                (11, 8, 0),
                (12, 8, 11),
            ):
                with self.subTest(exec_model_pos=(em_ln, em_col)):
                    em_defs = scoped._definitions(exec_model_file, em_ln, em_col)
                    self.assertEqual(len(em_defs), 1, em_defs)
                    self.assertEqual(em_defs[0]["range"]["start"]["line"], expected_ln)

            # 12. Inherited class members, diamond C3 MRO, super(), subclass references, and cycle protection.
            base_mod = pkg / "base_mod.py"
            base_mod.write_text(
                "class Base:\n"
                "    def setUp(self):\n"
                "        self.repo = 'root'\n"
                "    def dispatch(self) -> int:\n"
                "        return 1\n"
                "    def over(self) -> int:\n"
                "        return 10\n",
                encoding="utf-8",
            )
            inherit_file = pkg / "inherit_mod.py"
            inherit_file.write_text(
                "import base_mod\n"
                "class Child(base_mod.Base):\n"
                "    def over(self) -> int:\n"
                "        return super().over() + 1\n"
                "    def run(self) -> int:\n"
                "        _ = self.repo\n"
                "        return self.dispatch()\n"
                "class DiamondA:\n"
                "    def pick(self): return 1\n"
                "class DiamondB(DiamondA):\n"
                "    pass\n"
                "class DiamondC(DiamondA):\n"
                "    def pick(self): return 2\n"
                "class DiamondD(DiamondB, DiamondC):\n"
                "    def go(self): return self.pick()\n"
                "class CycleA(CycleB):\n"
                "    def ask(self): return self.missing\n"
                "class CycleB(CycleA):\n"
                "    pass\n",
                encoding="utf-8",
            )
            # `self.dispatch()` in `Child` -> `Base.dispatch` (line 3 in `base_mod.py`).
            inh_defs = scoped._definitions(inherit_file, 6, 20)
            self.assertEqual(len(inh_defs), 1, inh_defs)
            self.assertEqual(inh_defs[0]["uri"], base_mod.resolve().as_uri())
            self.assertEqual(inh_defs[0]["range"]["start"]["line"], 3)
            inh_hover = scoped._hover(inherit_file, 6, 20)
            self.assertIsNotNone(inh_hover)
            assert inh_hover is not None
            self.assertIn("def dispatch(self) -> int", inh_hover["contents"]["value"])
            # `self.repo` in `Child` -> `self.repo` in `Base.setUp` (line 2 in `base_mod.py`).
            repo_defs = scoped._definitions(inherit_file, 5, 17)
            self.assertEqual(len(repo_defs), 1, repo_defs)
            self.assertEqual(repo_defs[0]["uri"], base_mod.resolve().as_uri())
            self.assertEqual(repo_defs[0]["range"]["start"]["line"], 2)
            # `super().over()` in `Child.over` skips `Child.over` and resolves to `Base.over` (line 5 in `base_mod.py`).
            sup_defs = scoped._definitions(inherit_file, 3, 23)
            self.assertEqual(len(sup_defs), 1, sup_defs)
            self.assertEqual(sup_defs[0]["uri"], base_mod.resolve().as_uri())
            self.assertEqual(sup_defs[0]["range"]["start"]["line"], 5)
            # Diamond C3 MRO: `DiamondD(DiamondB, DiamondC)` resolves `self.pick()` to `DiamondC.pick` (line 12, not 8).
            dia_defs = scoped._definitions(inherit_file, 14, 30)
            self.assertEqual(len(dia_defs), 1, dia_defs)
            self.assertEqual(dia_defs[0]["range"]["start"]["line"], 12)
            # Cyclic bases (`CycleA(CycleB)` / `CycleB(CycleA)`) terminate cleanly without RecursionError.
            self.assertEqual(scoped._definitions(inherit_file, 16, 31), [])
            # References from `Base.dispatch` declaration include subclass `self.dispatch()` in `inherit_mod.py`.
            base_refs = scoped._references(base_mod, 3, 8, include_declaration=True)
            ref_uris_lines = {(r["uri"], r["range"]["start"]["line"]) for r in base_refs}
            self.assertIn((base_mod.resolve().as_uri(), 3), ref_uris_lines)
            self.assertIn((inherit_file.resolve().as_uri(), 6), ref_uris_lines)

            # Deep shared-ancestor DAG (`C0..C24` Fibonacci bases) resolves in linear memoized time (< 0.5s).
            dag_lines = ["class C0:\n    value = 1\n", "class C1(C0):\n    pass\n"]
            for idx in range(2, 25):
                dag_lines.append(f"class C{idx}(C{idx - 1}, C{idx - 2}):\n    pass\n")
            dag_lines.append("_ = C24.value\n")
            dag_file = pkg / "dag_mro.py"
            dag_file.write_text("".join(dag_lines), encoding="utf-8")
            dag_text_lines = dag_file.read_text(encoding="utf-8").splitlines()
            dag_use_ln = len(dag_text_lines) - 1
            dag_use_col = dag_text_lines[dag_use_ln].index("value")
            t0 = time.perf_counter()
            dag_defs = scoped._definitions(dag_file, dag_use_ln, dag_use_col)
            dag_elapsed = time.perf_counter() - t0
            self.assertEqual(len(dag_defs), 1, dag_defs)
            self.assertEqual(dag_defs[0]["range"]["start"]["line"], 1)
            self.assertLess(dag_elapsed, 2.0, f"Memoized C3 MRO took {dag_elapsed:.3f}s on 25-deep DAG")

            # Also verify live `Dispatch(Harness)` inherited resolution in `scripts/tests/test_review_dispatch.py`.
            live_rd = ROOT / "scripts" / "tests" / "test_review_dispatch.py"
            rd_lines = live_rd.read_text(encoding="utf-8").splitlines()
            harness_ln = next(i for i, row in enumerate(rd_lines) if row.startswith("    def dispatch(self,"))
            call_ln = next(i for i, row in enumerate(rd_lines) if "self.dispatch(" in row)
            call_col = rd_lines[call_ln].index("dispatch(")
            live_lsp = PYTHON_LSP.StdlibPythonLsp(ROOT)
            live_defs = live_lsp._definitions(live_rd, call_ln, call_col)
            self.assertEqual(len(live_defs), 1, live_defs)
            self.assertEqual(live_defs[0]["range"]["start"]["line"], harness_ln)

    def test_builtin_lsp_indentation_errors_scoped_references_and_utf16_positions(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp)
            scripts_dir = ws / "scripts"
            scripts_dir.mkdir()

            bad_indent = scripts_dir / "bad_indent.py"
            bad_indent.write_text("def first():\n    value = 1\n  return value\n", encoding="utf-8")

            scoped_refs_file = scripts_dir / "scoped_refs.py"
            scoped_refs_file.write_text(
                "def first():\n    value = 1\n    return value\n\n"
                "def second():\n    value = 2\n    return value\n",
                encoding="utf-8",
            )
            (scripts_dir / "unrelated_value.py").write_text("value = 999\n", encoding="utf-8")

            unicode_bmp = scripts_dir / "unicode_bmp.py"
            unicode_bmp.write_text("\u00e9 = 1; target = 2\n", encoding="utf-8")

            unicode_astral = scripts_dir / "unicode_astral.py"
            unicode_astral.write_text('\u00e9 = "\U0001f680"; target = 3\n_ = target\n', encoding="utf-8")

            client = LSP_MCP.LspClient(
                "python",
                [sys.executable, "-B", str(SCRIPTS / "python-lsp.py"), "--builtin"],
                ws,
            )
            try:
                # 1. Invalid indentation publishes a diagnostic AND does not crash `references` or terminate `serve()`.
                indent_diags = client.diagnostics_for(bad_indent, wait_seconds=5.0)
                self.assertEqual(len(indent_diags), 1)
                bad_uri = bad_indent.resolve().as_uri()
                broken_refs = client.request(
                    "textDocument/references",
                    {
                        "textDocument": {"uri": bad_uri},
                        "position": {"line": 1, "character": 4},
                        "context": {"includeDeclaration": True},
                    },
                )
                self.assertEqual([r["range"]["start"]["line"] for r in broken_refs], [1, 2])

                # 2. Scoped references return only the selected function's `value` binding, not `second()` or other files.
                self.assertEqual(client.diagnostics_for(scoped_refs_file, wait_seconds=5.0), [])
                scoped_uri = scoped_refs_file.resolve().as_uri()
                refs_incl = client.request(
                    "textDocument/references",
                    {
                        "textDocument": {"uri": scoped_uri},
                        "position": {"line": 1, "character": 4},
                        "context": {"includeDeclaration": True},
                    },
                )
                self.assertEqual([(r["uri"], r["range"]["start"]["line"]) for r in refs_incl],
                                 [(scoped_uri, 1), (scoped_uri, 2)])
                refs_excl = client.request(
                    "textDocument/references",
                    {
                        "textDocument": {"uri": scoped_uri},
                        "position": {"line": 1, "character": 4},
                        "context": {"includeDeclaration": False},
                    },
                )
                self.assertEqual([(r["uri"], r["range"]["start"]["line"]) for r in refs_excl],
                                 [(scoped_uri, 2)])

                # 3. UTF-16 character columns on BMP (`\u00e9 = 1; target = 2` -> col 7) and astral (`\U0001f680` -> col 10).
                self.assertEqual(client.diagnostics_for(unicode_bmp, wait_seconds=5.0), [])
                bmp_uri = unicode_bmp.resolve().as_uri()
                bmp_syms = client.request("textDocument/documentSymbol", {"textDocument": {"uri": bmp_uri}})
                target_sym = next(s for s in bmp_syms if s["name"] == "target")
                self.assertEqual(target_sym["range"]["start"]["character"], 7)
                self.assertEqual(target_sym["selectionRange"]["start"]["character"], 7)
                bmp_defs = client.request(
                    "textDocument/definition",
                    {"textDocument": {"uri": bmp_uri}, "position": {"line": 0, "character": 7}},
                )
                self.assertEqual(bmp_defs[0]["range"]["start"]["character"], 7)

                self.assertEqual(client.diagnostics_for(unicode_astral, wait_seconds=5.0), [])
                astral_uri = unicode_astral.resolve().as_uri()
                astral_syms = client.request("textDocument/documentSymbol", {"textDocument": {"uri": astral_uri}})
                astral_target = next(s for s in astral_syms if s["name"] == "target")
                self.assertEqual(astral_target["range"]["start"]["character"], 10)
                astral_defs = client.request(
                    "textDocument/definition",
                    {"textDocument": {"uri": astral_uri}, "position": {"line": 0, "character": 10}},
                )
                self.assertEqual(astral_defs[0]["range"]["start"]["character"], 10)
                astral_refs = client.request(
                    "textDocument/references",
                    {
                        "textDocument": {"uri": astral_uri},
                        "position": {"line": 1, "character": 4},
                        "context": {"includeDeclaration": True},
                    },
                )
                self.assertEqual(
                    [(r["range"]["start"]["line"], r["range"]["start"]["character"]) for r in astral_refs],
                    [(0, 10), (1, 4)],
                )

                # 4. Unicode identifier navigation (`café = 1` + `print(café)`).
                unicode_ident = scripts_dir / "unicode_ident.py"
                unicode_ident.write_text("caf\u00e9 = 1\nprint(caf\u00e9)\n", encoding="utf-8")
                self.assertEqual(client.diagnostics_for(unicode_ident, wait_seconds=5.0), [])
                ui_uri = unicode_ident.resolve().as_uri()
                ui_defs = client.request(
                    "textDocument/definition",
                    {"textDocument": {"uri": ui_uri}, "position": {"line": 1, "character": 7}},
                )
                self.assertEqual(len(ui_defs), 1, ui_defs)
                self.assertEqual((ui_defs[0]["range"]["start"]["line"], ui_defs[0]["range"]["start"]["character"]), (0, 0))
                ui_refs = client.request(
                    "textDocument/references",
                    {
                        "textDocument": {"uri": ui_uri},
                        "position": {"line": 1, "character": 7},
                        "context": {"includeDeclaration": True},
                    },
                )
                self.assertEqual(
                    [(r["range"]["start"]["line"], r["range"]["start"]["character"]) for r in ui_refs],
                    [(0, 0), (1, 6)],
                )
            finally:
                client.stop()


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

    def test_mcp_refreshes_previously_opened_dependencies_after_disk_edits(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp)
            pkg = ws / "scripts"
            pkg.mkdir()
            dep_a = pkg / "dep_a.py"
            caller_b = pkg / "caller_b.py"
            dep_a.write_text("def exported() -> int:\n    return 1\n", encoding="utf-8")
            caller_b.write_text("from dep_a import exported\nval = exported()\n", encoding="utf-8")

            srv = LSP_MCP.LspMcpServer(ws)
            srv.clients["python"] = LSP_MCP.LspClient(
                "python",
                [sys.executable, "-B", str(SCRIPTS / "python-lsp.py"), "--builtin"],
                ws,
            )
            try:
                self.assertIn("0 diagnostics", srv.tool_diagnostics({"file": "scripts/dep_a.py"}))
                first_loc = srv.tool_definition({"file": "scripts/caller_b.py", "line": 2, "character": 8})
                self.assertEqual(first_loc, "scripts/dep_a.py:1:1")

                # Edit `dep_a.py` on disk so `exported` shifts to line 3 WITHOUT querying `dep_a.py` directly again.
                dep_a.write_text("# moved line 1\n# moved line 2\ndef exported() -> int:\n    return 2\n", encoding="utf-8")
                updated_loc = srv.tool_definition({"file": "scripts/caller_b.py", "line": 2, "character": 8})
                self.assertEqual(updated_loc, "scripts/dep_a.py:3:1")
            finally:
                srv.shutdown_all()

    def test_mcp_stdio_survives_malformed_json_and_non_dict_requests(self):
        ping_frame_body = b'{"jsonrpc":"2.0","id":9,"method":"ping"}'
        deep_nested = (
            b'{"jsonrpc":"2.0","id":6,"method":"ping","params":{"x":'
            + (b"[" * 20000)
            + (b"]" * 20000)
            + b"}}"
        )
        stdin_payload = (
            b"[]\n"
            b"not-json\n"
            + deep_nested
            + b"\n"
            + b'{"jsonrpc":"2.0","id":7,"method":"initialize","params":[]}\n'
            + b"Content-Length: 9223372036854775808\r\n\r\n"
            + f"Content-Length: {len(ping_frame_body)}\r\n\r\n".encode("ascii")
            + ping_frame_body
            + b'\n{"jsonrpc":"2.0","id":8,"method":"ping"}\n'
        )
        proc = subprocess.run(
            [sys.executable, "-B", str(SCRIPTS / "lsp_mcp.py")],
            input=stdin_payload,
            capture_output=True,
            timeout=15,
        )
        self.assertEqual(proc.returncode, 0, proc.stderr.decode("utf-8", "replace"))
        out_text = proc.stdout.decode("utf-8")
        self.assertIn('"code": -32600', out_text)
        self.assertIn('"code": -32700', out_text)
        self.assertIn('"code": -32602', out_text)
        self.assertIn('"id": 9', out_text)
        self.assertIn('"id": 8', out_text)

        # Oversized Content-Length and deeply nested JSON also must not crash `python-lsp.py --builtin`.
        shutdown_body = b'{"jsonrpc":"2.0","id":1,"method":"shutdown"}'
        exit_body = b'{"jsonrpc":"2.0","method":"exit"}'
        py_lsp_stdin = (
            b"Content-Length: 9223372036854775808\r\n\r\n"
            + f"Content-Length: {len(deep_nested)}\r\n\r\n".encode("ascii")
            + deep_nested
            + f"Content-Length: {len(shutdown_body)}\r\n\r\n".encode("ascii")
            + shutdown_body
            + f"Content-Length: {len(exit_body)}\r\n\r\n".encode("ascii")
            + exit_body
        )
        py_proc = subprocess.run(
            [sys.executable, "-B", str(SCRIPTS / "python-lsp.py"), "--builtin"],
            input=py_lsp_stdin,
            capture_output=True,
            timeout=15,
        )
        self.assertEqual(py_proc.returncode, 0, py_proc.stderr.decode("utf-8", "replace"))
        py_out = py_proc.stdout.decode("utf-8")
        self.assertIn('"code": -32700', py_out)
        self.assertIn('"id": 1', py_out)

    def test_clangd_lsp_check_is_read_only_when_database_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            fake_root = Path(tmp)
            (fake_root / "Recto.sln").write_text("", encoding="utf-8")
            with unittest.mock.patch.object(CLANGD_LSP, "ROOT", fake_root):
                rc = CLANGD_LSP.main(["--check"])
            self.assertEqual(rc, 1)
            self.assertFalse((fake_root / "build" / "compile_commands.json").exists())

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

    def test_python_utf8_bom_and_pep263_encoding_rules(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp).resolve()
            scripts_dir = ws / "scripts"
            scripts_dir.mkdir(parents=True)

            bom_file = scripts_dir / "bom_mod.py"
            bom_file.write_bytes(b"\xef\xbb\xbfdef valid() -> int:\n    return 1\n\nx = valid()\n")

            latin_file = scripts_dir / "latin_mod.py"
            latin_file.write_bytes(b"# -*- coding: latin-1 -*-\ncaf\xe9 = 1\nres = caf\xe9\n")

            conflict_file = scripts_dir / "conflict_mod.py"
            conflict_file.write_bytes(b"\xef\xbb\xbf# -*- coding: latin-1 -*-\nx = 1\n")

            # 1. Direct StdlibPythonLsp disk and in-memory BOM handling.
            lsp = PYTHON_LSP.StdlibPythonLsp(ws)
            self.assertEqual(lsp._diagnostics(bom_file, lsp._text(bom_file)), [])
            self.assertEqual(lsp._diagnostics(bom_file, "\ufeffdef valid() -> int:\n    return 1\n"), [])
            self.assertEqual([s["name"] for s in lsp._doc_symbols(lsp._text(bom_file))], ["valid", "x"])
            self.assertEqual([s["name"] for s in lsp._doc_symbols("\ufeffdef valid():\n    return 1\n")], ["valid"])
            bom_defs = lsp._definitions(bom_file, 3, 4)
            self.assertEqual(len(bom_defs), 1)
            self.assertEqual(bom_defs[0]["range"]["start"], {"line": 0, "character": 0})

            self.assertEqual(lsp._diagnostics(latin_file, lsp._text(latin_file)), [])
            latin_defs = lsp._definitions(latin_file, 2, 6)
            self.assertEqual(len(latin_defs), 1)
            self.assertEqual(latin_defs[0]["range"]["start"], {"line": 1, "character": 0})

            conflict_diags = lsp._diagnostics(conflict_file, lsp._text(conflict_file))
            self.assertEqual(len(conflict_diags), 1)
            self.assertIn("encoding problem", conflict_diags[0]["message"])

            # 2. Full LspMcpServer end-to-end with `--builtin` backend.
            srv = LSP_MCP.LspMcpServer(ws)
            srv.clients["python"] = LSP_MCP.LspClient(
                "python", [sys.executable, str(ROOT / "scripts" / "python-lsp.py"), "--builtin"], ws
            )
            try:
                self.assertEqual(
                    srv.tool_diagnostics({"file": "scripts/bom_mod.py"}),
                    "scripts/bom_mod.py: 0 diagnostics",
                )
                self.assertIn("scripts/bom_mod.py:1 [function] valid", srv.tool_symbols({"file_or_query": "scripts/bom_mod.py"}))
                self.assertEqual(
                    srv.tool_definition({"file": "scripts/bom_mod.py", "line": 4, "character": 5}),
                    "scripts/bom_mod.py:1:1",
                )
                self.assertEqual(
                    srv.tool_diagnostics({"file": "scripts/latin_mod.py"}),
                    "scripts/latin_mod.py: 0 diagnostics",
                )
                self.assertIn(
                    "encoding problem",
                    srv.tool_diagnostics({"file": "scripts/conflict_mod.py"}),
                )
            finally:
                srv.shutdown_all()

    def test_pyright_launcher_with_spaces_in_windows_cmd_path_starts_and_serves_lsp(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp) / "ws"
            (ws / "scripts").mkdir(parents=True)
            good_py = ws / "scripts" / "ok_mod.py"
            good_py.write_text("def greet() -> int:\n    return 42\n", encoding="utf-8")
            bad_py = ws / "scripts" / "bad_mod.py"
            bad_py.write_text("def broken(:\n    pass\n", encoding="utf-8")

            spaced_npm = Path(tmp) / "User Profile With Spaces" / "AppData" / "Roaming" / "npm"
            spaced_npm.mkdir(parents=True)
            builtin_lsp = SCRIPTS / "python-lsp.py"
            if os.name == "nt":
                shim = spaced_npm / "pyright-langserver.cmd"
                shim.write_text(
                    f'@"{sys.executable}" -B "{builtin_lsp}" --builtin %*\r\n',
                    encoding="utf-8",
                )
            else:
                shim = spaced_npm / "pyright-langserver"
                shim.write_text(
                    f'#!/bin/sh\nexec "{sys.executable}" -B "{builtin_lsp}" --builtin "$@"\n',
                    encoding="utf-8",
                )
                shim.chmod(0o755)

            with unittest.mock.patch.dict(os.environ, {"RECTO_PYRIGHT_LANGSERVER": str(shim)}):
                client = LSP_MCP.LspClient(
                    "python",
                    [sys.executable, "-B", str(builtin_lsp)],
                    ws,
                )
                try:
                    self.assertEqual(client.diagnostics_for(good_py, wait_seconds=5.0), [])
                    self.assertEqual(len(client.diagnostics_for(bad_py, wait_seconds=5.0)), 1)
                finally:
                    client.stop()

    def test_builtin_workspace_symbols_include_nested_class_methods_and_native_idle_shutdown(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp) / "ws"
            (ws / "scripts").mkdir(parents=True)
            mod_py = ws / "scripts" / "svc.py"
            mod_py.write_text(
                "class LspClient:\n"
                "    def diagnostics_for(self) -> list[str]:\n"
                "        def inner_helper() -> int:\n"
                "            return 1\n"
                "        return []\n",
                encoding="utf-8",
            )
            builtin_lsp = SCRIPTS / "python-lsp.py"
            client = LSP_MCP.LspClient(
                "python",
                [sys.executable, "-B", str(builtin_lsp), "--builtin"],
                ws,
            )
            try:
                client.ensure_started()
                syms = client.request("workspace/symbol", {"query": "diagnostics_for"}) or []
                self.assertTrue(
                    any(s.get("name") == "diagnostics_for" and s.get("containerName") == "LspClient" for s in syms),
                    syms,
                )
                nested = client.request("workspace/symbol", {"query": "inner_helper"}) or []
                self.assertTrue(
                    any(
                        s.get("name") == "inner_helper"
                        and s.get("containerName") == "LspClient.diagnostics_for"
                        for s in nested
                    ),
                    nested,
                )
            finally:
                client.stop()

            # Native stdio launch (`.lsp.json` path) auto-exits on inactivity while stdin stays open.
            idle_env = dict(os.environ)
            idle_env["RECTO_LSP_IDLE_SEC"] = "0.35"
            proc_builtin = subprocess.Popen(
                [sys.executable, "-B", str(builtin_lsp), "--builtin"],
                cwd=ws,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=idle_env,
            )
            try:
                rc = proc_builtin.wait(timeout=4.0)
                self.assertEqual(rc, 0)
            finally:
                for s in (proc_builtin.stdin, proc_builtin.stdout, proc_builtin.stderr):
                    if s is not None:
                        s.close()
                if proc_builtin.poll() is None:
                    proc_builtin.kill()
                    proc_builtin.wait(timeout=2.0)

            # Native `clangd-lsp.py` / proxied child path also auto-exits and reaps its child on inactivity.
            fake_bin_dir = Path(tmp) / "fake-clangd"
            fake_bin_dir.mkdir(parents=True)
            child_pid_file = Path(tmp) / "clangd_pid.txt"
            sleeper_py = fake_bin_dir / "sleeper.py"
            sleeper_py.write_text(
                "import os, pathlib, sys, time\n"
                f"pathlib.Path(r'{child_pid_file}').write_text(str(os.getpid()), encoding='utf-8')\n"
                "time.sleep(60)\n",
                encoding="utf-8",
            )
            if os.name == "nt":
                fake_clangd = fake_bin_dir / "clangd.cmd"
                fake_clangd.write_text(f'@"{sys.executable}" -B "{sleeper_py}"\r\n', encoding="utf-8")
            else:
                fake_clangd = fake_bin_dir / "clangd"
                fake_clangd.write_text(f'#!/bin/sh\nexec "{sys.executable}" -B "{sleeper_py}"\n', encoding="utf-8")
                fake_clangd.chmod(0o755)
            idle_clangd_env = dict(idle_env)
            idle_clangd_env["RECTO_CLANGD"] = str(fake_clangd)
            proc_clangd = subprocess.Popen(
                [sys.executable, "-B", str(SCRIPTS / "clangd-lsp.py")],
                cwd=ROOT,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=idle_clangd_env,
            )
            try:
                rc_c = proc_clangd.wait(timeout=6.0)
                self.assertEqual(rc_c, 0)
                self.assertTrue(child_pid_file.is_file())
            finally:
                for s in (proc_clangd.stdin, proc_clangd.stdout, proc_clangd.stderr):
                    if s is not None:
                        s.close()
                if proc_clangd.poll() is None:
                    proc_clangd.kill()
                    proc_clangd.wait(timeout=2.0)

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
