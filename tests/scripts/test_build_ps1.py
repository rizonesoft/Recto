"""Checks on build/build.ps1, the one-command build and test driver, and the ASan runtime staging it loads."""
import json
import os
import pathlib
import re
from pathlib import Path
import shutil
import subprocess
import sys
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


def _msbuild():
    vswhere = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return None
    found = subprocess.run([str(vswhere), "-latest", "-prerelease", "-find", r"MSBuild\**\Bin\amd64\MSBuild.exe"],
                           capture_output=True, text=True).stdout.splitlines()
    return found[0] if found else None


MSBUILD = _msbuild() if sys.platform == "win32" else None


@unittest.skipUnless(MSBUILD, "needs MSBuild (Windows with Visual Studio)")
class LibraryPathDefaults(unittest.TestCase):
    """RectoDropAbsentUnusedLibraryPaths in Directory.Build.targets, run by MSBuild on a throwaway project through the
    dependency chain the toolset sets LIB with: it drops the ATL/MFC and .NET Framework library defaults only when
    absent, and keeps every other entry, an absent one included, in order."""

    def library_path(self, platform: str, atl_exists: bool) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            atl = tmp / "atlmfc" / "lib" / platform
            netfx = tmp / "netfx"
            kept, mistyped = tmp / "lib" / platform, tmp / "mistyped"
            kept.mkdir(parents=True)
            if atl_exists:
                atl.mkdir(parents=True)
            proj = tmp / "probe.proj"
            proj.write_text(f"""<Project>
  <PropertyGroup>
    <PlatformTarget>{platform}</PlatformTarget>
    <VC_LibraryPath_ATL_{platform}>{atl};</VC_LibraryPath_ATL_{platform}>
    <NETFXKitsDir>{netfx}\\</NETFXKitsDir>
    <LibraryPath>{kept};{atl};{mistyped};{netfx}\\lib\\um\\{platform.lower()};</LibraryPath>
  </PropertyGroup>
  <Import Project="{ROOT / 'Directory.Build.targets'}" />
  <!-- Shaped like the toolset's SetBuildDefaultEnvironmentVariables (Microsoft.Cpp.Current.targets): it depends on
       $(SetBuildDefaultEnvironmentVariablesDependsOn) and copies LibraryPath into LIB, so the probe sees what LIB
       would hold only if Directory.Build.targets hooks its target into that chain. -->
  <Target Name="ProbeSetLib" DependsOnTargets="$(SetBuildDefaultEnvironmentVariablesDependsOn)">
    <PropertyGroup><ProbeLIB>$(LibraryPath)</ProbeLIB></PropertyGroup>
  </Target>
</Project>
""", encoding="utf-8")
            run = subprocess.run([MSBUILD, str(proj), "-nologo", "-t:ProbeSetLib", "-getProperty:ProbeLIB"],
                                 capture_output=True, text=True, timeout=300)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            names = {str(kept): "kept", str(atl): "atl", str(mistyped): "mistyped"}
            return [names.get(p, "netfx" if p.endswith("um\\" + platform.lower()) else p)
                    for p in run.stdout.strip().split(";") if p]

    def test_absent_unused_defaults_go_and_everything_else_stays(self):
        self.assertEqual(self.library_path("ARM64", atl_exists=False), ["kept", "mistyped"])
        self.assertEqual(self.library_path("ARM64", atl_exists=True), ["kept", "atl", "mistyped"])
        self.assertEqual(self.library_path("x64", atl_exists=False), ["kept", "mistyped"])


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
        self.assertIn("$env:VSLANG = '1033'", text)
        self.assertNotIn("asan_dbg_dynamic", text)

    def test_ram_bounded_parallelism_restore_stamp_and_quick_mode(self):
        text = BUILD_PS1.read_text(encoding="utf-8")
        self.assertIn("[switch]$Quick", text)
        self.assertIn("[switch]$ForceRestore", text)
        self.assertIn("[int]$MaxCpuCount = 0", text)
        self.assertIn("'/nr:false'", text)
        self.assertIn('"/m:$jobs"', text)
        self.assertIn("build\\restore.stamp", text)
        self.assertIn("Win32_OperatingSystem", text)


class AsanCoverage(unittest.TestCase):
    """-EnableASAN instruments into its own folders and stages the runtime for every C++ project in Recto.sln,
    so a module added to the solution cannot silently stay uninstrumented (D10 T01 §1 and §2)."""

    # Projects built with their own sanitizer setup instead, and why.
    OWN_INSTRUMENTATION = {"recto-fuzz": "built Release|x64 with /fsanitize=address,fuzzer by its own job (D06 T02 §3)"}

    @staticmethod
    def solution_projects():
        text = (ROOT / "Recto.sln").read_text(encoding="utf-8-sig")
        return set(re.findall(r'^Project\("\{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942\}"\) = "([^"]+)"', text, re.M))

    def test_every_solution_project_is_instrumented(self):
        text = BUILD_PS1.read_text(encoding="utf-8")
        instrumented = set(re.findall(r"'([^']+)'", re.search(r"\$asanProjects = @\(([^)]*)\)", text).group(1)))
        projects = self.solution_projects() - set(self.OWN_INSTRUMENTATION)
        self.assertTrue({"recto-ui", "Recto", "recto-tests"} <= projects)
        self.assertEqual(instrumented, projects)
        self.assertIn('"-t:$($asanProjects -join \';\')"', text)  # the ASan build is exactly those projects

    def test_switching_modes_deletes_nothing(self):
        # An ASan build has its own folders (D00 T04 §1): no state file, and no build deletes the other mode's output.
        text = BUILD_PS1.read_text(encoding="utf-8")
        self.assertNotIn("asan-state", text)
        self.assertIsNone(re.search(r"Remove-Item\s+\$(objDir|binDir)", text))
        props = (ROOT / "Directory.Build.props").read_text(encoding="utf-8-sig")
        self.assertRegex(props, r"<RectoObjFolder Condition=\"'\$\(EnableASAN\)' == 'true'\">obj-asan</RectoObjFolder>")
        self.assertRegex(props, r"<RectoBinFolder Condition=\"'\$\(EnableASAN\)' == 'true'\">bin-asan</RectoBinFolder>")
        self.assertIn(r"build\$(RectoObjFolder)\$(MSBuildProjectName)", props)
        self.assertIn(r"build\$(RectoBinFolder)\$(Platform)", props)
        self.assertIn(r'build\$binFolder\x64\$cfg\recto-tests\recto-tests.exe', text)

    def test_every_executable_the_tests_launch_gets_the_runtime(self):
        text = BUILD_PS1.read_text(encoding="utf-8")
        staged = set(re.findall(r"'([^']+)'", re.search(r"foreach \(\$exeProj in @\(([^)]*)\)\)", text).group(1)))
        self.assertEqual(staged, {"recto-tests", "recto-cli", "Recto"})


class BuildEvidence(unittest.TestCase):
    """build/build-evidence.ps1 against a fake build tree: it records hashes and object write times, lists every change,
    and refuses a record or a comparison with no object file, which would read as "nothing compiled" (D00 T04 §1)."""

    SCRIPT = ROOT / "build" / "build-evidence.ps1"

    def run_script(self, *args):
        result = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(self.SCRIPT), *args],
                                capture_output=True, text=True, timeout=120)
        return result.returncode, result.stdout + result.stderr

    def tree(self, root):
        obj = root / "build" / "obj" / "recto-pdf" / "x64" / "Debug"
        bin_ = root / "build" / "bin" / "x64" / "Debug" / "recto-tests"
        obj.mkdir(parents=True)
        bin_.mkdir(parents=True)
        (obj / "a.obj").write_bytes(b"a")
        (obj / "b.obj").write_bytes(b"b")
        (bin_ / "recto-tests.exe").write_bytes(b"exe")
        return obj, bin_

    def test_lists_every_difference(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            obj, bin_ = self.tree(root)
            code, out = self.run_script("-Record", str(root / "one.json"), "-Root", str(root))
            self.assertEqual(code, 0, out)
            code, out = self.run_script("-Compare", str(root / "one.json"), str(root / "one.json"))
            self.assertEqual(code, 0, out)
            self.assertIn("changed 0, appeared 0, gone 0, object files rewritten 0", out)
            (bin_ / "recto-tests.exe").write_bytes(b"other")  # an overwritten binary
            (bin_ / "new.dll").write_bytes(b"new")
            (obj / "b.obj").unlink()
            os.utime(obj / "a.obj", (1_000_000_000, 1_000_000_000))  # rewritten
            code, out = self.run_script("-Record", str(root / "two.json"), "-Root", str(root))
            self.assertEqual(code, 0, out)
            code, out = self.run_script("-Compare", str(root / "one.json"), str(root / "two.json"))
            self.assertEqual(code, 0, out)
            self.assertIn("changed 1, appeared 1, gone 1, object files rewritten 1", out)
            self.assertIn(r"changed build\bin\x64\Debug\recto-tests\recto-tests.exe", out)
            self.assertIn(r"appeared build\bin\x64\Debug\recto-tests\new.dll", out)
            self.assertIn(r"gone build\obj\recto-pdf\x64\Debug\b.obj", out)
            self.assertIn(r"rewritten build\obj\recto-pdf\x64\Debug\a.obj", out)

    def test_a_short_form_root_cuts_paths_right(self):
        # CI's temporary folder comes in 8.3 form (C:\Users\RUNNER~1\...): the paths a record holds must still be
        # relative to the build folder.
        import ctypes
        with tempfile.TemporaryDirectory(dir=os.environ.get("LOCALAPPDATA")) as tmp:
            long_root = Path(tmp) / "a-folder-name-past-eight"
            long_root.mkdir()
            buffer = ctypes.create_unicode_buffer(1024)
            ctypes.windll.kernel32.GetShortPathNameW(str(long_root), buffer, 1024)
            short = Path(buffer.value)
            if not buffer.value or short.name == long_root.name:
                self.skipTest("this volume keeps no 8.3 names")
            self.tree(long_root)
            code, out = self.run_script("-Record", str(short / "one.json"), "-Root", str(short))
            self.assertEqual(code, 0, out)
            paths = sorted(entry["path"] for entry in json.loads((long_root / "one.json").read_text(encoding="utf-8-sig")))
            self.assertEqual(paths, [r"build\bin\x64\Debug\recto-tests\recto-tests.exe", r"build\obj\recto-pdf\x64\Debug\a.obj",
                                     r"build\obj\recto-pdf\x64\Debug\b.obj"])

    def test_refuses_evidence_of_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            code, out = self.run_script("-Record", str(root / "none.json"), "-Root", str(root))
            self.assertEqual(code, 2, out)
            self.assertIn("no object files", out)
            self.assertFalse((root / "none.json").exists())
            (root / "empty.json").write_text("[]", encoding="utf-8")
            self.tree(root)
            code, _ = self.run_script("-Record", str(root / "one.json"), "-Root", str(root))
            self.assertEqual(code, 0)
            code, out = self.run_script("-Compare", str(root / "empty.json"), str(root / "one.json"))
            self.assertEqual(code, 2, out)
            self.assertIn("holds no object files", out)
            code, out = self.run_script("-Record", str(root / "x.json"), "-Root", str(root), "-Configuration", "Debgu")
            self.assertNotEqual(code, 0, out)

if __name__ == "__main__":
    unittest.main()
