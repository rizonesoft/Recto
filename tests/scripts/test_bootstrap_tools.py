"""Tests for D00 T04 section 6: repo-scoped pinned Ninja, CMake, sccache and CPython under .tools/."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
LOCK_FILE = ROOT / "build" / "tools.lock.json"
BOOTSTRAP_PS1 = ROOT / "build" / "bootstrap-tools.ps1"
PYTHON_CMD = ROOT / "build" / "python.cmd"
RUN_PYTHON_SH = ROOT / "scripts" / "run-python.sh"
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell")


def _no_window() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def _strip_system_python_from_path(raw_path: str) -> str:
    """Return PATH with every directory containing a python/python3/py launcher removed."""
    kept: list[str] = []
    names = ("python.exe", "python3.exe", "python.cmd", "python.bat", "py.exe", "python", "python3")
    for entry in raw_path.split(os.pathsep):
        if not entry:
            continue
        folder = Path(entry)
        try:
            has_py = any((folder / name).exists() for name in names)
        except OSError:
            has_py = False
        if not has_py:
            kept.append(entry)
    return os.pathsep.join(kept)


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().lower()


def _write_zip(path: Path, members: dict[str, bytes]) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for rel, content in members.items():
            zf.writestr(rel, content)
    return _sha256_bytes(path.read_bytes())


class ToolsLockAndWorkflowContract(unittest.TestCase):
    """build/tools.lock.json, .gitignore, and GitHub workflows follow D00 T04 section 6."""

    def test_lock_has_four_pinned_https_entries_with_valid_sha256(self):
        self.assertTrue(LOCK_FILE.is_file(), f"missing {LOCK_FILE}")
        doc = json.loads(LOCK_FILE.read_text(encoding="utf-8"))
        self.assertEqual(set(doc), {"ninja", "cmake", "sccache", "cpython"})
        for tool, item in doc.items():
            with self.subTest(tool=tool):
                self.assertEqual(set(item), {"version", "url", "sha256", "executable"})
                self.assertRegex(item["version"], r"^\d+(?:\.\d+)+$")
                self.assertTrue(item["url"].startswith("https://"), item["url"])
                self.assertRegex(item["sha256"], r"^[0-9a-f]{64}$")
                self.assertNotIn("..", item["executable"])
                self.assertFalse(Path(item["executable"]).is_absolute())

    def test_gitignore_tracks_lock_bootstrap_and_python_cmd_while_ignoring_tools_dir(self):
        gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8").splitlines()
        self.assertIn(".tools/", gitignore)
        for tracked in ("!build/tools.lock.json", "!build/bootstrap-tools.ps1", "!build/python.cmd"):
            self.assertIn(tracked, gitignore)

    def test_workflows_cache_and_bootstrap_tools_and_pins_runs_tool_check(self):
        plan_yml = (ROOT / ".github" / "workflows" / "plan.yml").read_text(encoding="utf-8")
        build_yml = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
        pins_yml = (ROOT / ".github" / "workflows" / "pins.yml").read_text(encoding="utf-8")

        self.assertNotIn("actions/setup-python", plan_yml)
        self.assertNotIn("run: |", plan_yml)
        self.assertIn("build/bootstrap-tools.ps1", plan_yml)
        self.assertIn(r"run: .\build\python.cmd scripts/run-tests.py --full", plan_yml)

        self.assertEqual(build_yml.count("pwsh -NoProfile -File build/bootstrap-tools.ps1"), 2)
        self.assertIn(r".\build\python.cmd -m unittest tests.scripts.test_engine_dependency_policy", build_yml)
        self.assertNotIn("run: python -m unittest", build_yml)

        self.assertIn("check: tools", pins_yml)
        self.assertIn("python scripts/check-tool-pins.py pr", pins_yml)
        self.assertIn("python-version: '3.15'", pins_yml)


@unittest.skipUnless(POWERSHELL, "needs PowerShell")
class BootstrapToolsScriptTests(unittest.TestCase):
    """build/bootstrap-tools.ps1 fetches, verifies SHA-256, configures CPython ._pth, and refuses bad archives."""

    def _build_fixture_lock(
        self,
        archives_dir: Path,
        *,
        ninja_ver: str = "1.13.2",
        cpython_ver: str = "3.15.2",
    ) -> tuple[Path, dict]:
        ninja_zip = archives_dir / f"ninja-{ninja_ver}.zip"
        ninja_sha = _write_zip(ninja_zip, {"ninja.exe": f"ninja-{ninja_ver}".encode("ascii")})

        cmake_zip = archives_dir / "cmake.zip"
        cmake_sha = _write_zip(cmake_zip, {"cmake-4.5.0-windows-x86_64/bin/cmake.exe": b"cmake-bin"})

        sccache_zip = archives_dir / "sccache.zip"
        sccache_sha = _write_zip(sccache_zip, {"sccache-v0.19.0-x86_64-pc-windows-msvc/sccache.exe": b"sccache-bin"})

        cpython_zip = archives_dir / f"cpython-{cpython_ver}.zip"
        cpython_sha = _write_zip(
            cpython_zip,
            {
                "python.exe": f"cpython-{cpython_ver}".encode("ascii"),
                "pythonw.exe": f"cpythonw-{cpython_ver}".encode("ascii"),
                "python315.zip": b"stdlib",
                "python315._pth": b"python315.zip\r\n.\r\n\r\n# Uncomment to run site.main() automatically\r\n#import site\r\n",
            },
        )
        doc = {
            "ninja": {
                "version": ninja_ver,
                "url": ninja_zip.resolve().as_uri(),
                "sha256": ninja_sha,
                "executable": "ninja.exe",
            },
            "cmake": {
                "version": "4.5.0",
                "url": cmake_zip.resolve().as_uri(),
                "sha256": cmake_sha,
                "executable": "cmake-4.5.0-windows-x86_64/bin/cmake.exe",
            },
            "sccache": {
                "version": "0.19.0",
                "url": sccache_zip.resolve().as_uri(),
                "sha256": sccache_sha,
                "executable": "sccache-v0.19.0-x86_64-pc-windows-msvc/sccache.exe",
            },
            "cpython": {
                "version": cpython_ver,
                "url": cpython_zip.resolve().as_uri(),
                "sha256": cpython_sha,
                "executable": "python.exe",
            },
        }
        lock_path = archives_dir / "tools.lock.json"
        lock_path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
        return lock_path, doc

    def _run_bootstrap(self, lock_path: Path, tools_dir: Path) -> subprocess.CompletedProcess:
        assert POWERSHELL is not None
        return subprocess.run(
            [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(BOOTSTRAP_PS1),
             "-LockFile", str(lock_path), "-ToolsDir", str(tools_dir)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=120,
            creationflags=_no_window(),
        )

    def test_empty_tools_fetches_all_four_second_run_downloads_nothing_and_version_bump_cleans_old(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            archives = base / "archives"
            tools_dir = base / ".tools"
            lock_path, _ = self._build_fixture_lock(archives)

            first = self._run_bootstrap(lock_path, tools_dir)
            self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
            self.assertTrue((tools_dir / "ninja" / "1.13.2" / "ninja.exe").is_file())
            self.assertTrue((tools_dir / "cmake" / "4.5.0" / "cmake-4.5.0-windows-x86_64" / "bin" / "cmake.exe").is_file())
            self.assertTrue((tools_dir / "sccache" / "0.19.0" / "sccache-v0.19.0-x86_64-pc-windows-msvc" / "sccache.exe").is_file())
            self.assertTrue((tools_dir / "cpython" / "3.15.2" / "python.exe").is_file())
            self.assertTrue((tools_dir / "cpython" / "3.15.2" / "sitecustomize.py").is_file())
            pth_lines = (tools_dir / "cpython" / "3.15.2" / "python315._pth").read_text(encoding="utf-8").splitlines()
            self.assertIn("import site", pth_lines)
            self.assertNotIn("#import site", pth_lines)

            # Delete all source zip archives: a second run must succeed without downloading anything.
            for zf in archives.glob("*.zip"):
                zf.unlink()
            second = self._run_bootstrap(lock_path, tools_dir)
            self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
            self.assertEqual(second.stdout.strip(), "")

            # Bump ninja version: old 1.13.2 directory is replaced by 1.14.0.
            lock_path_v2, _ = self._build_fixture_lock(archives, ninja_ver="1.14.0")
            third = self._run_bootstrap(lock_path_v2, tools_dir)
            self.assertEqual(third.returncode, 0, third.stdout + third.stderr)
            self.assertTrue((tools_dir / "ninja" / "1.14.0" / "ninja.exe").is_file())
            self.assertFalse((tools_dir / "ninja" / "1.13.2").exists())

    def test_corrupted_sha256_fails_naming_tool_expected_and_actual_and_leaves_no_tool_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            archives = base / "archives"
            tools_dir = base / ".tools"
            lock_path, doc = self._build_fixture_lock(archives)
            actual_hash = doc["sccache"]["sha256"]
            wrong_digit = "0" if actual_hash[-1] != "0" else "1"
            expected_hash = actual_hash[:-1] + wrong_digit
            doc["sccache"]["sha256"] = expected_hash
            lock_path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")

            bad = self._run_bootstrap(lock_path, tools_dir)
            self.assertNotEqual(bad.returncode, 0)
            combined = bad.stdout + bad.stderr
            self.assertIn("sccache", combined)
            self.assertIn(expected_hash, combined)
            self.assertIn(actual_hash, combined)
            self.assertFalse((tools_dir / "sccache").exists(), "corrupted download must leave no folder for that tool")
            self.assertEqual(list(tools_dir.glob(".download-*")), [])
            self.assertEqual(list(tools_dir.glob(".staging-*")), [])

    def test_zip_slip_archive_is_refused_and_leaves_no_tool_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            archives = base / "archives"
            tools_dir = base / ".tools"
            lock_path, doc = self._build_fixture_lock(archives)
            evil_zip = archives / "evil-ninja.zip"
            evil_sha = _write_zip(evil_zip, {"../../escaped.txt": b"nope"})
            doc["ninja"]["url"] = evil_zip.resolve().as_uri()
            doc["ninja"]["sha256"] = evil_sha
            lock_path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")

            bad = self._run_bootstrap(lock_path, tools_dir)
            self.assertNotEqual(bad.returncode, 0)
            self.assertFalse((tools_dir / "ninja").exists())
            self.assertFalse((base / "escaped.txt").exists())

    def test_download_body_stall_times_out_and_cleans_partial_destination(self):
        assert POWERSHELL is not None
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / "partial.zip"
            ps_code = (
                f". '{BOOTSTRAP_PS1}' -NoRun\n"
                "$src = @'\n"
                "using System;\n"
                "using System.IO;\n"
                "using System.Net;\n"
                "using System.Net.Http;\n"
                "using System.Threading;\n"
                "using System.Threading.Tasks;\n"
                "public sealed class StalledBodyStream : Stream {\n"
                "    private bool _sentFirst;\n"
                "    private bool _closed;\n"
                "    private readonly object _sync = new object();\n"
                "    public override bool CanRead { get { return true; } }\n"
                "    public override bool CanSeek { get { return false; } }\n"
                "    public override bool CanWrite { get { return false; } }\n"
                "    public override long Length { get { throw new NotSupportedException(); } }\n"
                "    public override long Position { get { throw new NotSupportedException(); } set { throw new NotSupportedException(); } }\n"
                "    public override void Flush() {}\n"
                "    public override int Read(byte[] buffer, int offset, int count) {\n"
                "        if (!_sentFirst) { _sentFirst = true; buffer[offset] = 0x50; return 1; }\n"
                "        lock (_sync) { if (!_closed) { Monitor.Wait(_sync, 30000); } }\n"
                "        throw new ObjectDisposedException(nameof(StalledBodyStream));\n"
                "    }\n"
                "    public override async Task<int> ReadAsync(byte[] buffer, int offset, int count, CancellationToken cancellationToken) {\n"
                "        if (!_sentFirst) { _sentFirst = true; buffer[offset] = 0x50; return 1; }\n"
                "        await Task.Delay(TimeSpan.FromSeconds(30), cancellationToken);\n"
                "        return 0;\n"
                "    }\n"
                "    public override long Seek(long offset, SeekOrigin origin) { throw new NotSupportedException(); }\n"
                "    public override void SetLength(long value) { throw new NotSupportedException(); }\n"
                "    public override void Write(byte[] buffer, int offset, int count) { throw new NotSupportedException(); }\n"
                "    protected override void Dispose(bool disposing) { lock (_sync) { _closed = true; Monitor.PulseAll(_sync); } base.Dispose(disposing); }\n"
                "}\n"
                "public sealed class StalledBodyHandler : HttpMessageHandler {\n"
                "    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken) {\n"
                "        var resp = new HttpResponseMessage(HttpStatusCode.OK) { Content = new StreamContent(new StalledBodyStream()) };\n"
                "        return Task.FromResult(resp);\n"
                "    }\n"
                "}\n"
                "'@\n"
                "if ($PSVersionTable.PSVersion.Major -lt 6) {\n"
                "    Add-Type -ReferencedAssemblies System.Net.Http -TypeDefinition $src\n"
                "} else {\n"
                "    Add-Type -TypeDefinition $src\n"
                "}\n"
                "$handler = [StalledBodyHandler]::new()\n"
                "$dlSw = [System.Diagnostics.Stopwatch]::StartNew()\n"
                "$dlErr = ''\n"
                "try {\n"
                f"    Download-Archive -Url 'https://example.invalid/tool.zip' -Destination '{dest}' -TimeoutSec 0.6 -Handler $handler\n"
                "} catch {\n"
                "    $dlErr = $_.Exception.Message\n"
                "} finally {\n"
                "    $dlSw.Stop()\n"
                "    $handler.Dispose()\n"
                "}\n"
                "Write-Output ('DL_ELAPSED=' + $dlSw.Elapsed.TotalSeconds.ToString([System.Globalization.CultureInfo]::InvariantCulture))\n"
                "if ($dlErr) { Write-Error $dlErr; exit 1 }\n"
                "exit 0\n"
            )
            script = Path(tmp) / "stall-test.ps1"
            script.write_text(ps_code, encoding="utf-8")
            run = subprocess.run(
                [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)],
                capture_output=True,
                text=True,
                timeout=90,
                creationflags=_no_window(),
            )
            self.assertNotEqual(run.returncode, 0)
            self.assertIn("Download timed out", run.stdout + run.stderr)
            elapsed_lines = [ln.split("=", 1)[1] for ln in run.stdout.splitlines() if ln.startswith("DL_ELAPSED=")]
            self.assertEqual(len(elapsed_lines), 1, run.stdout + run.stderr)
            self.assertLess(float(elapsed_lines[0]), 10.0)
            self.assertFalse(dest.exists(), "partial download file must be removed on timeout")


@unittest.skipUnless(os.name == "nt", "Windows .tools/ CPython and launcher integration")
class PinnedCpythonAndNoSystemPythonTests(unittest.TestCase):
    """Real .tools/ CPython path-initialization contract and system-Python-absent execution across hooks and LSP/MCP."""

    def _stripped_env(self, **extra: str) -> dict[str, str]:
        env = dict(os.environ)
        env["PATH"] = _strip_system_python_from_path(env.get("PATH", ""))
        env.pop("PYTHONPATH", None)
        env.pop("PYTHONSAFEPATH", None)
        env.update(extra)
        return env

    def test_python_cmd_prints_pinned_version_and_satisfies_sys_path_and_isolation_flags(self):
        lock = json.loads(LOCK_FILE.read_text(encoding="utf-8"))
        pinned_ver = lock["cpython"]["version"]

        # Verify no python/python3 is resolvable on the stripped PATH.
        stripped = self._stripped_env()
        self.assertIsNone(shutil.which("python", path=stripped["PATH"]))
        self.assertIsNone(shutil.which("python3", path=stripped["PATH"]))

        ver_run = subprocess.run(
            ["cmd.exe", "/d", "/c", str(PYTHON_CMD), "-c", "import site, sys; print(sys.version.split()[0], site.ENABLE_USER_SITE)"],
            cwd=ROOT,
            env=stripped,
            capture_output=True,
            text=True,
            timeout=120,
            creationflags=_no_window(),
        )
        self.assertEqual(ver_run.returncode, 0, ver_run.stderr)
        self.assertEqual(ver_run.stdout.strip(), f"{pinned_ver} False")

        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            # 1. Direct script in a subdirectory imports a sibling module and spawns [sys.executable, ...].
            sub = base / "sub"
            sub.mkdir()
            (sub / "sibling_helper.py").write_text("VAL = 'from-sibling'\n", encoding="utf-8")
            (sub / "entry.py").write_text(
                "import sibling_helper, subprocess, sys\n"
                "child = subprocess.run([sys.executable, '-c', 'import os, sys; print(os.path.abspath(sys.path[0]))'], "
                "cwd=r'" + str(base) + "', capture_output=True, text=True, check=True)\n"
                "print(sibling_helper.VAL + '|' + child.stdout.strip())\n",
                encoding="utf-8",
            )
            r_script = subprocess.run(
                ["cmd.exe", "/d", "/c", str(PYTHON_CMD), str(sub / "entry.py")],
                cwd=ROOT,
                env=stripped,
                capture_output=True,
                text=True,
                timeout=60,
                creationflags=_no_window(),
            )
            self.assertEqual(r_script.returncode, 0, r_script.stderr)
            prefix_val, _, raw_child_path = r_script.stdout.strip().partition("|")
            self.assertEqual(prefix_val, "from-sibling")
            self.assertEqual(Path(raw_child_path).resolve(), base.resolve())

            # 2. Dotted `-m pkg.mod` imports from CWD, and PYTHONPATH prepends custom dir.
            pkg = base / "mypkg"
            pkg.mkdir()
            (pkg / "__init__.py").write_text("", encoding="utf-8")
            (pkg / "runner.py").write_text("import extra_mod; print('MOD:' + extra_mod.TAG)\n", encoding="utf-8")
            extra_dir = base / "pp_dir"
            extra_dir.mkdir()
            (extra_dir / "extra_mod.py").write_text("TAG = 'from-pythonpath'\n", encoding="utf-8")

            r_mod = subprocess.run(
                ["cmd.exe", "/d", "/c", str(PYTHON_CMD), "-m", "mypkg.runner"],
                cwd=base,
                env=self._stripped_env(PYTHONPATH=str(extra_dir)),
                capture_output=True,
                text=True,
                timeout=60,
                creationflags=_no_window(),
            )
            self.assertEqual(r_mod.returncode, 0, r_mod.stderr)
            self.assertEqual(r_mod.stdout.strip(), "MOD:from-pythonpath")

            # 3. Explicit -E ignores PYTHONPATH; explicit -P and PYTHONSAFEPATH=1 omit script/cwd entry; -I does both.
            for flag_args, extra_env in (
                (["-E", "-c", "import extra_mod"], {"PYTHONPATH": str(extra_dir)}),
                (["-I", "-c", "import extra_mod"], {"PYTHONPATH": str(extra_dir)}),
                (["-P", str(sub / "entry.py")], {}),
                ([str(sub / "entry.py")], {"PYTHONSAFEPATH": "1"}),
            ):
                with self.subTest(flags=flag_args, env=extra_env):
                    r_iso = subprocess.run(
                        ["cmd.exe", "/d", "/c", str(PYTHON_CMD), *flag_args],
                        cwd=base,
                        env=self._stripped_env(**extra_env),
                        capture_output=True,
                        text=True,
                        timeout=60,
                        creationflags=_no_window(),
                    )
                    self.assertNotEqual(r_iso.returncode, 0)
                    self.assertIn("ModuleNotFoundError", r_iso.stderr)

    def test_pre_commit_and_lsp_mcp_configs_run_with_system_python_absent_and_posix_fallback_works(self):
        stripped = self._stripped_env()

        # 1. LSP and MCP servers start and answer JSON-RPC `initialize` via their exact config `command` + `args`.
        def _lsp_frame(obj: dict) -> bytes:
            raw = json.dumps(obj).encode("utf-8")
            return f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii") + raw

        init_req = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {"processId": None, "rootUri": None, "capabilities": {}},
        }
        lsp_input = (
            _lsp_frame(init_req)
            + _lsp_frame({"jsonrpc": "2.0", "id": 2, "method": "shutdown", "params": None})
            + _lsp_frame({"jsonrpc": "2.0", "method": "exit", "params": None})
        )

        lsp_cfg = json.loads((ROOT / ".lsp.json").read_text(encoding="utf-8"))
        py_entry = lsp_cfg["python"]
        lsp_proc = subprocess.run(
            [py_entry["command"], *py_entry["args"], "--builtin"],
            input=lsp_input,
            cwd=ROOT,
            env=stripped,
            capture_output=True,
            timeout=60,
            creationflags=_no_window(),
        )
        self.assertEqual(lsp_proc.returncode, 0, lsp_proc.stderr.decode("utf-8", errors="replace"))
        self.assertIn(b'"capabilities"', lsp_proc.stdout)

        cpp_entry = lsp_cfg["cpp"]
        cpp_check = subprocess.run(
            [cpp_entry["command"], *cpp_entry["args"], "--check"],
            cwd=ROOT,
            env=stripped,
            capture_output=True,
            timeout=60,
            creationflags=_no_window(),
        )
        self.assertIn(cpp_check.returncode, (0, 1), cpp_check.stderr.decode("utf-8", errors="replace"))
        self.assertTrue(cpp_check.stdout or cpp_check.stderr)

        mcp_line = json.dumps(init_req).encode("utf-8") + b"\n"
        for mcp_rel in (Path(".mcp.json"), Path(".agents/plugins/recto-lsp/mcp_config.json")):
            with self.subTest(mcp=mcp_rel.as_posix()):
                mcp_cfg = json.loads((ROOT / mcp_rel).read_text(encoding="utf-8"))
                srv = mcp_cfg["mcpServers"]["recto-lsp"]
                mcp_proc = subprocess.run(
                    [srv["command"], *srv["args"]],
                    input=mcp_line,
                    cwd=(ROOT / mcp_rel).parent,
                    env=stripped,
                    capture_output=True,
                    timeout=60,
                    creationflags=_no_window(),
                )
                self.assertEqual(mcp_proc.returncode, 0, mcp_proc.stderr.decode("utf-8", errors="replace"))
                self.assertIn(b'"serverInfo"', mcp_proc.stdout)

        # 2. scripts/hooks/pre-commit runs in a temp repo without system python on PATH.
        git_exe = shutil.which("git")
        self.assertIsNotNone(git_exe)
        assert git_exe is not None
        sh_exe = Path(git_exe).resolve().parents[1] / "usr" / "bin" / "sh.exe"
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "repo"
            (repo / "scripts" / "hooks").mkdir(parents=True)
            (repo / "build").mkdir(parents=True)
            (repo / "todo").mkdir(parents=True)
            shutil.copy2(ROOT / "scripts" / "hooks" / "pre-commit", repo / "scripts" / "hooks" / "pre-commit")
            shutil.copy2(RUN_PYTHON_SH, repo / "scripts" / "run-python.sh")
            for bname in ("tools.lock.json", "bootstrap-tools.ps1", "python.cmd"):
                shutil.copy2(ROOT / "build" / bname, repo / "build" / bname)
            (repo / "scripts" / "todo-graph.py").write_text("import sys\nsys.exit(0)\n", encoding="utf-8")
            (repo / "scripts" / "check-error-code-names.py").write_text("import sys\nsys.exit(0)\n", encoding="utf-8")
            (repo / "todo" / "note.md").write_text("hi\n", encoding="utf-8")
            for g_args in (["init", "-q"], ["config", "user.email", "t@t"], ["config", "user.name", "t"],
                           ["config", "commit.gpgsign", "false"], ["config", "core.hooksPath", "scripts/hooks"],
                           ["add", "todo/note.md"]):
                subprocess.run(["git", *g_args], cwd=repo, check=True, capture_output=True, creationflags=_no_window())
            commit_ok = subprocess.run(
                ["git", "commit", "-m", "test pre-commit without system python"],
                cwd=repo,
                env=dict(stripped, RECTO_TOOLS_DIR=str(ROOT / ".tools")),
                capture_output=True,
                text=True,
                timeout=60,
                creationflags=_no_window(),
            )
            self.assertEqual(commit_ok.returncode, 0, commit_ok.stderr)

            # 3. scripts/run-python.sh POSIX fallback (OS=POSIX) invokes python3 from PATH.
            fake_bin = Path(tmp) / "posix-bin"
            fake_bin.mkdir()
            (fake_bin / "python3").write_text("#!/bin/sh\nif [ \"$1\" = \"-c\" ] && [ -z \"$2\" ]; then exit 0; fi\necho \"POSIX_PY:$*\"\n",
                                             encoding="utf-8", newline="\n")
            if sh_exe.is_file():
                posix_env = dict(stripped, OS="POSIX", PATH=str(fake_bin) + os.pathsep + str(sh_exe.parent))
                posix_run = subprocess.run(
                    [str(sh_exe), str(RUN_PYTHON_SH), "hello-posix"],
                    cwd=ROOT,
                    env=posix_env,
                    capture_output=True,
                    text=True,
                    timeout=30,
                    creationflags=_no_window(),
                )
                self.assertEqual(posix_run.returncode, 0, posix_run.stderr)
                self.assertEqual(posix_run.stdout.strip(), "POSIX_PY:hello-posix")

    @unittest.skipUnless(shutil.which("pwsh"), "install.ps1 requires PowerShell 7 (pwsh)")
    def test_install_ps1_registers_stable_cpython_current_junction_and_honors_recto_tools_dir(self):
        pwsh = shutil.which("pwsh")
        assert pwsh is not None
        stripped = self._stripped_env()
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp).resolve()
            repo = base / "repo"
            (repo / ".conclave" / "automations").mkdir(parents=True)
            (repo / "build").mkdir(parents=True)
            (repo / "scripts").mkdir(parents=True)
            for fname in ("install.ps1", "run.py", "alert.py"):
                shutil.copy2(ROOT / ".conclave" / "automations" / fname, repo / ".conclave" / "automations" / fname)
            shutil.copy2(BOOTSTRAP_PS1, repo / "build" / "bootstrap-tools.ps1")
            (repo / "scripts" / "noop.py").write_text("print('ok')\n", encoding="utf-8")
            (repo / ".conclave" / "automations" / "tasks.json").write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "folder": "\\Demo\\",
                        "tasks": [
                            {
                                "name": "demo-pins",
                                "description": "Check demo pins",
                                "command": ["python", "scripts/noop.py"],
                                "trigger": {"weekly": "Monday", "at": "07:00"},
                                "timeout_minutes": 5,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            # Seed an override RECTO_TOOLS_DIR using the real .tools/ CPython binary payload for 3.15.2, then upgrade to 3.15.99.
            real_lock = json.loads(LOCK_FILE.read_text(encoding="utf-8"))
            real_ver = real_lock["cpython"]["version"]
            real_cpython_dir = ROOT / ".tools" / "cpython" / real_ver
            self.assertTrue(real_cpython_dir.is_dir(), f"expected bootstrapped CPython at {real_cpython_dir}")

            archives = base / "archives"
            cpython_entries = {
                p.name: p.read_bytes()
                for p in real_cpython_dir.iterdir()
                if p.is_file() and p.name not in (".verified-sha256", "sitecustomize.py")
            }
            # Restore original commented ._pth in archive so bootstrap exercises Configure-EmbeddableCpython.
            for k in list(cpython_entries):
                if k.endswith("._pth"):
                    cpython_entries[k] = b"python315.zip\r\n.\r\n#import site\r\n"
            cp_v1_zip = archives / "cpython-v1.zip"
            cp_v1_sha = _write_zip(cp_v1_zip, cpython_entries)
            cp_v2_zip = archives / "cpython-v2.zip"
            cp_v2_sha = _write_zip(cp_v2_zip, cpython_entries)

            ninja_zip = archives / "ninja.zip"
            ninja_sha = _write_zip(ninja_zip, {"ninja.exe": b"n"})
            cmake_zip = archives / "cmake.zip"
            cmake_sha = _write_zip(cmake_zip, {"cmake.exe": b"c"})
            sccache_zip = archives / "sccache.zip"
            sccache_sha = _write_zip(sccache_zip, {"sccache.exe": b"s"})

            def _make_lock(cp_ver: str, cp_zip: Path, cp_sha: str) -> dict:
                return {
                    "ninja": {"version": "1.13.2", "url": ninja_zip.resolve().as_uri(), "sha256": ninja_sha, "executable": "ninja.exe"},
                    "cmake": {"version": "4.5.0", "url": cmake_zip.resolve().as_uri(), "sha256": cmake_sha, "executable": "cmake.exe"},
                    "sccache": {"version": "0.19.0", "url": sccache_zip.resolve().as_uri(), "sha256": sccache_sha, "executable": "sccache.exe"},
                    "cpython": {"version": cp_ver, "url": cp_zip.resolve().as_uri(), "sha256": cp_sha, "executable": "python.exe"},
                }

            repo_lock = repo / "build" / "tools.lock.json"
            repo_lock.write_text(json.dumps(_make_lock("3.15.2", cp_v1_zip, cp_v1_sha), indent=2) + "\n", encoding="utf-8")

            override_tools = base / "custom-tools-dir"
            fake_existing = base / "existing.json"
            fake_existing.write_text("[]", encoding="utf-8")
            fake_log = base / "scheduler.jsonl"
            fake_log.write_text("", encoding="utf-8")

            env = dict(
                stripped,
                RECTO_TOOLS_DIR=str(override_tools),
                FAKE_EXISTING=str(fake_existing),
                FAKE_LOG=str(fake_log),
                FAKE_SCRIPT=str(repo / ".conclave" / "automations" / "install.ps1"),
            )
            install_run = subprocess.run(
                [pwsh, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ROOT / "tests" / "scripts" / "fixtures" / "fake-scheduler.ps1")],
                cwd=repo,
                env=env,
                capture_output=True,
                text=True,
                timeout=120,
                creationflags=_no_window(),
            )
            self.assertEqual(install_run.returncode, 0, install_run.stdout + install_run.stderr)
            self.assertFalse((repo / ".tools").exists(), "install.ps1 must honor RECTO_TOOLS_DIR rather than creating <repo>/.tools")

            records = [json.loads(line) for line in fake_log.read_text(encoding="utf-8").splitlines() if line.strip()]
            self.assertEqual(len(records), 1)
            registered_exe = Path(records[0]["execute"])
            expected_current_w = override_tools / "cpython" / "current" / "pythonw.exe"
            self.assertEqual(registered_exe, expected_current_w)
            self.assertTrue(registered_exe.is_file())

            # Upgrade CPython in lock to 3.15.99 and run bootstrap-tools.ps1: 3.15.2 is deleted, yet registered_exe still works.
            repo_lock.write_text(json.dumps(_make_lock("3.15.99", cp_v2_zip, cp_v2_sha), indent=2) + "\n", encoding="utf-8")
            upgrade_run = subprocess.run(
                [pwsh, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(repo / "build" / "bootstrap-tools.ps1")],
                cwd=repo,
                env=env,
                capture_output=True,
                text=True,
                timeout=120,
                creationflags=_no_window(),
            )
            self.assertEqual(upgrade_run.returncode, 0, upgrade_run.stdout + upgrade_run.stderr)
            self.assertFalse((override_tools / "cpython" / "3.15.2").exists(), "old CPython version folder must be pruned")
            self.assertTrue((override_tools / "cpython" / "3.15.99" / "pythonw.exe").is_file())
            self.assertTrue(registered_exe.is_file(), "registered Task Scheduler executable must survive CPython upgrades")

            # Triggering the registered action runs run.py cleanly with the upgraded interpreter.
            out_marker = repo / "build" / "automations" / "demo-pins" / "last.json"
            trigger_run = subprocess.run(
                [str(registered_exe), str(repo / ".conclave" / "automations" / "run.py"), "demo-pins"],
                cwd=repo,
                env=stripped,
                capture_output=True,
                text=True,
                timeout=60,
                creationflags=_no_window(),
            )
            self.assertEqual(trigger_run.returncode, 0, trigger_run.stderr)
            self.assertEqual(json.loads(out_marker.read_text(encoding="utf-8"))["exit"], 0)


if __name__ == "__main__":
    unittest.main()
