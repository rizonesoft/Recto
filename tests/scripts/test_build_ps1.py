"""Checks on build/build.ps1, the one-command build and test driver, and the ASan runtime staging it loads."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD_PS1 = ROOT / "build" / "build.ps1"
ASAN_RUNTIME_PS1 = ROOT / "build" / "asan-runtime.ps1"
MSBUILD_LOG_PS1 = ROOT / "build" / "msbuild-log.ps1"
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell")


@unittest.skipUnless(POWERSHELL, "needs PowerShell")
class AsanRuntimeStaging(unittest.TestCase):
    """Copy-AsanRuntime, driven with a fake dumpbin: it stages exactly the clang_rt.asan DLLs the executable imports
    (since VS 2022 17.7 one clang_rt.asan_dynamic for Debug and Release alike) and refuses anything else."""

    def stage(self, imports: list[str], present: list[str]) -> tuple[int, str, set[str]]:
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            (tmp / "asan").mkdir()
            (tmp / "out").mkdir()
            for name in present:
                (tmp / "asan" / name).write_bytes(b"runtime " + name.encode())
            (tmp / "out" / "app.exe").write_bytes(b"MZ")
            listing = "".join(f"Write-Output '    {name}'\n" for name in ["KERNEL32.dll", *imports])
            (tmp / "dumpbin.ps1").write_text(f"Write-Output 'Image has the following dependencies:'\n{listing}exit 0\n",
                                             encoding="utf-8")
            script = (f". '{ASAN_RUNTIME_PS1}'; Copy-AsanRuntime -ExePath '{tmp / 'out' / 'app.exe'}' "
                      f"-AsanBinDir '{tmp / 'asan'}' -Dumpbin '{tmp / 'dumpbin.ps1'}'")
            run = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                                 capture_output=True, text=True, timeout=120)
            staged = {p.name for p in (tmp / "out").iterdir()} - {"app.exe"}
            for name in staged:
                self.assertEqual((tmp / "out" / name).read_bytes(), b"runtime " + name.encode())
            return run.returncode, run.stdout + run.stderr, staged

    def test_the_imported_runtime_is_staged(self):
        code, output, staged = self.stage(["clang_rt.asan_dynamic-x86_64.dll"],
                                          ["clang_rt.asan_dynamic-x86_64.dll", "clang_rt.asan_dbg_dynamic-x86_64.dll"])
        self.assertEqual(code, 0, output)
        self.assertEqual(staged, {"clang_rt.asan_dynamic-x86_64.dll"})  # only what it imports

    def test_an_executable_without_an_asan_import_refuses(self):
        code, output, staged = self.stage([], ["clang_rt.asan_dynamic-x86_64.dll"])
        self.assertNotEqual(code, 0)
        self.assertIn("imports no ASan runtime", output)
        self.assertEqual(staged, set())

    def test_a_missing_runtime_refuses(self):
        code, output, staged = self.stage(["clang_rt.asan_dynamic-x86_64.dll"], [])
        self.assertNotEqual(code, 0)
        self.assertIn("ASan runtime DLL not found", output)


@unittest.skipUnless(POWERSHELL, "needs PowerShell")
class MSBuildWarningCheck(unittest.TestCase):
    """Assert-NoMSBuildWarnings on sample logs: a warning from any tool fails the build, as does a log with no
    summary; a clean log passes."""

    def check(self, log_text: str) -> subprocess.CompletedProcess:
        with tempfile.TemporaryDirectory() as tmp:
            log = pathlib.Path(tmp) / "msbuild.log"
            log.write_text(log_text, encoding="utf-8")
            script = f". '{MSBUILD_LOG_PS1}'; Assert-NoMSBuildWarnings -LogFile '{log}' -What 'Debug|ARM64'"
            return subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                                  capture_output=True, text=True, timeout=120)

    def test_a_clean_log_passes(self):
        run = self.check("Build succeeded.\n    0 Warning(s)\n    0 Error(s)\n")
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_a_tool_warning_fails(self):
        run = self.check("ROSLYNCODETASKFACTORYCSHARPCOMPILER : warning CS1668: Invalid search path 'X'\n"
                         "Build succeeded.\n    1 Warning(s)\n    0 Error(s)\n")
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("1 warning(s)", run.stdout + run.stderr)
        self.assertIn("CS1668", run.stdout + run.stderr)

    def test_a_log_without_a_summary_fails(self):
        run = self.check("Build started.\n")
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("no warning summary", run.stdout + run.stderr)


class BuildScript(unittest.TestCase):
    @unittest.skipUnless(POWERSHELL, "needs PowerShell")
    def test_the_scripts_parse(self):
        for script in (BUILD_PS1, ASAN_RUNTIME_PS1, MSBUILD_LOG_PS1):
            check = (f"$errors = $null; [void][System.Management.Automation.Language.Parser]::ParseFile('{script}', "
                     f"[ref]$null, [ref]$errors); $errors | ForEach-Object {{ $_.Message }}; exit $errors.Count")
            run = subprocess.run([POWERSHELL, "-NoProfile", "-Command", check], capture_output=True, text=True,
                                 timeout=120)
            with self.subTest(script=script.name):
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_build_stages_through_copy_asan_runtime(self):
        text = BUILD_PS1.read_text(encoding="utf-8")
        self.assertIn(". (Join-Path $PSScriptRoot 'asan-runtime.ps1')", text)
        self.assertIn("Copy-AsanRuntime -ExePath $exe", text)
        self.assertIn(". (Join-Path $PSScriptRoot 'msbuild-log.ps1')", text)
        self.assertIn("Assert-NoMSBuildWarnings -LogFile $logFile", text)
        self.assertNotIn("asan_dbg_dynamic", text)


if __name__ == "__main__":
    unittest.main()
