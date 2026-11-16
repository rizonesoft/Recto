#!/usr/bin/env python3
"""Tests for the compile database clangd reads (build/compile_commands.json, written by the CMake build of
D00 T04 section 8 from a full x64 Debug build) and for `build/clangd.cmd`. The MSBuild generator
(`scripts/build-compile-commands.py` and the `RectoCompileCommands` target) retired with the switch to CMake; the
database's scope and contents are proven in `tests/scripts/test_cmake_build.py` (CompileDatabaseScope,
LiveCompileDatabase)."""
from __future__ import annotations

import ctypes
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell")


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load {path}")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


PROC_GROUP = _load_module("proc_group", SCRIPTS / "lib" / "proc_group.py")


def _run_managed(
    cmd: list[str],
    *,
    cwd: Path = ROOT,
    env: dict[str, str] | None = None,
    timeout: float = 60.0,
) -> subprocess.CompletedProcess[str]:
    managed = PROC_GROUP.spawn_managed_process(cmd, cwd=cwd, env=env, capture_output=True)
    try:
        stdout, stderr = managed.proc.communicate(timeout=timeout)
        return subprocess.CompletedProcess(cmd, int(managed.proc.returncode or 0), stdout or "", stderr or "")
    except subprocess.TimeoutExpired as exc:
        out, err = managed.drain_after_terminate()
        raise subprocess.TimeoutExpired(cmd, timeout, output=out, stderr=err) from exc
    finally:
        managed.terminate_and_close()


def _pid_alive(pid: int) -> bool:
    if os.name == "nt":
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        STILL_ACTIVE = 259
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not handle:
            return False
        try:
            code = ctypes.c_uint32()
            if kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
                return code.value == STILL_ACTIVE
            return False
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False




CLANGD_LSP = _load_module("clangd_lsp", SCRIPTS / "clangd-lsp.py")
# One source of each module, checked by clangd against the database the build wrote.
MODULE_SOURCES = ("src/pdf/error.cpp", "src/core/core.cpp", "src/services/services.cpp", "src/ui/ui.cpp",
                  "src/app/main.cpp", "src/cli/main.cpp", "tests/recto-tests/test_support.cpp", "fuzz/fuzz_smoke.cpp")


class ClangdAndProcessTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows build/clangd.cmd wrapper")
    def test_clangd_cmd_missing_component_version_and_diagnostics(self):
        clangd_cmd = ROOT / "build" / "clangd.cmd"
        self.assertTrue(clangd_cmd.is_file())

        with tempfile.TemporaryDirectory() as empty_vs:
            env = os.environ.copy()
            env["RECTO_VS_INSTALL"] = empty_vs
            missing = _run_managed(
                ["cmd", "/c", str(clangd_cmd), "--version"],
                cwd=ROOT, env=env, timeout=30,
            )
            self.assertEqual(missing.returncode, 2)
            self.assertIn("Microsoft.VisualStudio.Component.VC.Llvm.Clang", missing.stderr + missing.stdout)

        clangd_exe = CLANGD_LSP.find_clangd()
        if not clangd_exe:
            self.skipTest("clangd not installed on this host")

        ver = _run_managed(
            ["cmd", "/c", str(clangd_cmd), "--version"],
            cwd=ROOT, timeout=30,
        )
        self.assertEqual(ver.returncode, 0, ver.stdout + ver.stderr)
        self.assertIn("clangd version", ver.stdout + ver.stderr)

        # Unknown arguments and compile errors on any line must be caught cleanly, on a source of every module.
        if (ROOT / "build" / "compile_commands.json").is_file():
            for source in MODULE_SOURCES:
                with self.subTest(source=source):
                    ok_chk = _run_managed(
                        ["cmd", "/c", str(clangd_cmd), f"--check={source}"],
                        cwd=ROOT, timeout=180,
                    )
                    combined = ok_chk.stdout + "\n" + ok_chk.stderr
                    self.assertEqual(ok_chk.returncode, 0, combined[-3000:])
                    self.assertNotIn("unknown argument", combined.lower())
                    self.assertNotIn("argument unused during compilation", combined.lower())
                    self.assertIn("All checks completed, 0 errors", combined)

        # Planted error beyond line 1 (line 20) still fails --check cleanly even with --check-lines=1-1.
        with tempfile.TemporaryDirectory() as tmp_cpp_dir:
            bad_cpp = Path(tmp_cpp_dir).resolve() / "bad_line20.cpp"
            lines = ["// line"] * 19 + ['int broken = "not_an_int";']
            bad_cpp.write_text("\n".join(lines) + "\n", encoding="utf-8")
            bad_chk = _run_managed(
                ["cmd", "/c", str(clangd_cmd), f"--check={bad_cpp}"],
                cwd=ROOT, timeout=60,
            )
            bad_out = bad_chk.stdout + "\n" + bad_chk.stderr
            self.assertNotEqual(bad_chk.returncode, 0)
            self.assertIn("Line 20", bad_out)


    def test_run_managed_timeout_reaps_descendant_processes_and_releases_temp_locks(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp).resolve()
            pid_file = ws / "grandchild.pid"
            lock_file = ws / "locked.txt"
            grandchild = ws / "grandchild.py"
            parent = ws / "parent.py"

            grandchild.write_text(
                "import os, pathlib, sys, time\n"
                "lock_p = pathlib.Path(sys.argv[1])\n"
                "pid_p = pathlib.Path(sys.argv[2])\n"
                "with lock_p.open('w', encoding='utf-8') as fh:\n"
                "    pid_p.write_text(str(os.getpid()), encoding='utf-8')\n"
                "    fh.write('holding lock\\n')\n"
                "    fh.flush()\n"
                "    time.sleep(30)\n",
                encoding="utf-8",
            )
            parent.write_text(
                "import subprocess, sys, time\n"
                "p = subprocess.Popen([sys.executable, '-B', sys.argv[1], sys.argv[2], sys.argv[3]])\n"
                "time.sleep(30)\n",
                encoding="utf-8",
            )

            if os.name == "nt":
                cmd = ["cmd", "/c", sys.executable, "-B", str(parent), str(grandchild), str(lock_file), str(pid_file)]
            else:
                cmd = ["sh", "-c", f'"{sys.executable}" -B "{parent}" "{grandchild}" "{lock_file}" "{pid_file}"']

            with self.assertRaises(subprocess.TimeoutExpired):
                _run_managed(cmd, cwd=ws, timeout=2.0)

            self.assertTrue(pid_file.is_file(), "grandchild did not start before timeout")
            gpid = int(pid_file.read_text(encoding="utf-8").strip())
            self.assertFalse(_pid_alive(gpid), f"descendant pid {gpid} survived _run_managed timeout")
            lock_file.unlink()


class CMakeDatabaseIsKept(unittest.TestCase):
    """A build/compile_commands.json the CMake build wrote (D00 T04 section 8) is kept by the .vcxproj generator that
    scripts/clangd-lsp.py runs at start only while it is current; a stale one fails --check and is regenerated."""

    def _gen(self):
        spec = importlib.util.spec_from_file_location("gen_compile_commands", ROOT / "scripts" / "gen-compile-commands.py")
        mod = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(mod)
        return mod

    def _tree(self, tmp):
        root = Path(tmp).resolve()
        folder = root / "build" / "cmake" / "x64-debug"
        folder.mkdir(parents=True)
        (root / "src").mkdir()
        (root / "cmake").mkdir()
        (root / "CMakeLists.txt").write_text("project(x)\n", encoding="utf-8")
        (root / "Recto.sln").write_text("", encoding="utf-8")
        sources = [str(root / "src" / name) for name in ("a.cpp", "b.cpp")]
        db = root / "build" / "compile_commands.json"
        db.write_text(json.dumps([{"directory": str(folder), "command": f"cl.exe /c {s}", "file": s} for s in sources]),
                      encoding="utf-8")
        return root, db, sources

    def test_a_current_cmake_database_survives_and_another_does_not_count(self):
        gen = self._gen()
        with tempfile.TemporaryDirectory() as tmp:
            root, db, sources = self._tree(tmp)
            gen.build_database = lambda r: [{"file": s} for s in sources]
            writes = []
            gen._write_entries = lambda path, entries: writes.append(path)
            before = db.read_bytes()
            self.assertTrue(gen.is_cmake_database(db, root))
            self.assertIsNone(gen.cmake_database_problem(db, root))
            self.assertEqual(gen.ensure_database(db, root), db)
            self.assertEqual((db.read_bytes(), writes), (before, []))
            ok, msg = gen.check_database(db, root)
            self.assertTrue(ok, msg)
            self.assertIn("ok (CMake): 2 translation units", msg)
            # A build without sccache adds each target's generated precompiled-header unit: still current; any other
            # unit the projects do not compile makes it stale.
            folder = root / "build" / "cmake" / "x64-debug"
            pch = str(folder / "src" / "CMakeFiles" / "a.dir" / "cmake_pch.cxx")
            db.write_text(json.dumps(json.loads(before) + [{"directory": str(folder), "command": "cl.exe /Yc", "file": pch}]),
                          encoding="utf-8")
            self.assertIsNone(gen.cmake_database_problem(db, root))
            other = str(folder / "src" / "generated.cpp")
            db.write_text(json.dumps(json.loads(before) + [{"directory": str(folder), "command": "cl.exe", "file": other}]),
                          encoding="utf-8")
            self.assertIn("extra generated.cpp", gen.cmake_database_problem(db, root))
            db.write_bytes(before)
            entries = json.loads(db.read_text(encoding="utf-8")) + [
                {"directory": str(root / "src"), "command": "cl.exe /c c.cpp", "file": str(root / "src" / "c.cpp")}]
            db.write_text(json.dumps(entries), encoding="utf-8")
            self.assertFalse(gen.is_cmake_database(db, root))
            db.write_text("not json", encoding="utf-8")
            self.assertFalse(gen.is_cmake_database(db, root))

    def test_a_stale_cmake_database_fails_check_and_is_regenerated(self):
        gen = self._gen()
        with tempfile.TemporaryDirectory() as tmp:
            root, db, sources = self._tree(tmp)
            # A source added since the build wrote it.
            gen.build_database = lambda r: [{"file": s} for s in sources + [str(root / "src" / "new.cpp")]]
            writes = []
            gen._write_entries = lambda path, entries: writes.append(path)
            ok, msg = gen.check_database(db, root)
            self.assertFalse(ok)
            self.assertIn("stale CMake database: it names 2 sources where the projects compile 3 (missing new.cpp)", msg)
            gen.ensure_database(db, root)
            self.assertEqual(writes, [db])
            # A CMake input changed after it was written.
            gen.build_database = lambda r: [{"file": s} for s in sources]
            later = db.stat().st_mtime + 10
            os.utime(root / "CMakeLists.txt", (later, later))
            ok, msg = gen.check_database(db, root)
            self.assertFalse(ok)
            self.assertIn("CMakeLists.txt changed after it was written", msg)

if __name__ == "__main__":
    unittest.main()