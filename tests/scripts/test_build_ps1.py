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
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from scripts.lib.proc_group import spawn_managed_process  # noqa: E402

BUILD_PS1 = ROOT / "build" / "build.ps1"


def _run_managed(cmd: list[str], *, cwd: Path, env: dict[str, str], timeout: float) -> subprocess.CompletedProcess[str]:
    managed = spawn_managed_process(cmd, cwd=cwd, env=env, capture_output=True)
    try:
        stdout, stderr = managed.proc.communicate(timeout=timeout)
        return subprocess.CompletedProcess(cmd, managed.proc.returncode, stdout or "", stderr or "")
    except subprocess.TimeoutExpired:
        out, err = managed.drain_after_terminate()
        raise subprocess.TimeoutExpired(cmd, timeout, output=out, stderr=err)
    finally:
        managed.terminate_and_close()
PARALLELISM_PS1 = ROOT / "build" / "parallelism.ps1"
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
        for script in (BUILD_PS1, PARALLELISM_PS1, ASAN_RUNTIME_PS1, MSBUILD_LOG_PS1):
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
        self.assertIn(". (Join-Path $PSScriptRoot 'parallelism.ps1')", text)
        self.assertIn("Resolve-BuildParallelism -MaxCpuCount $MaxCpuCount", text)
        self.assertIn("'/nr:false'", text)
        self.assertIn('"/m:$jobs"', text)
        self.assertIn('"/p:CL_MPCount=$clMpCount"', text)
        self.assertIn("build\\restore.stamp", text)

    @unittest.skipUnless(POWERSHELL, "needs PowerShell")
    def test_default_platform_is_x64_and_arm64_overrides_stay_supported(self):
        probe = (
            f"$ast = [System.Management.Automation.Language.Parser]::ParseFile('{BUILD_PS1}', [ref]$null, [ref]$null)\n"
            "$p = $ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'Platform' }\n"
            "$vs = $p.Attributes | Where-Object { $_ -is [System.Management.Automation.Language.AttributeAst] -and $_.TypeName.Name -eq 'ValidateSet' }\n"
            "Write-Output ('DEFAULT=' + $p.DefaultValue.Extent.Text)\n"
            "Write-Output ('VALID=' + (($vs.PositionalArguments | ForEach-Object { $_.Value }) -join '|'))\n"
        )
        run = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", probe],
                             capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        lines = dict(line.split("=", 1) for line in run.stdout.strip().splitlines() if "=" in line)
        self.assertEqual(lines.get("DEFAULT"), "@('x64')")
        self.assertEqual(set(lines.get("VALID", "").split("|")), {"x64", "ARM64", "x64,ARM64", "ARM64,x64"})

        ps1_text = BUILD_PS1.read_text(encoding="utf-8")
        self.assertIn("if ($Platform -contains 'ARM64')", ps1_text)
        self.assertNotIn("with the C++ x64 and ARM64 tools", ps1_text)

        cmd_text = (ROOT / "build.cmd").read_text(encoding="utf-8")
        self.assertIn("for x64 in Debug and Release", cmd_text)
        self.assertIn("-Platform x64,ARM64", cmd_text)

        ci_text = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
        self.assertEqual(len(re.findall(r"build/build\.ps1\s+-Configuration\s+(?:Debug|Release)\s+-Platform\s+x64,ARM64", ci_text)), 2)

    @unittest.skipUnless(POWERSHELL, "needs PowerShell")
    def test_project_mode_resolves_solution_targets_and_relinks_recto_tests(self):
        text = BUILD_PS1.read_text(encoding="utf-8")
        fn_match = re.search(r"(function Resolve-SolutionTarget\b.*?^})", text, re.M | re.S)
        self.assertIsNotNone(fn_match, "Resolve-SolutionTarget helper missing from build/build.ps1")
        fn_body = fn_match.group(1)
        probe = (
            f"{fn_body}\n"
            f"$r = '{ROOT}'\n"
            "Write-Output (Resolve-SolutionTarget -RepoRoot $r -Requested 'recto-pdf')\n"
            "Write-Output (Resolve-SolutionTarget -RepoRoot $r -Requested 'src\\pdf\\recto-pdf.vcxproj')\n"
            "Write-Output (Resolve-SolutionTarget -RepoRoot $r -Requested 'src/app/Recto.vcxproj')\n"
        )
        ok = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", probe],
                            capture_output=True, text=True, timeout=60)
        self.assertEqual(ok.returncode, 0, ok.stdout + ok.stderr)
        self.assertEqual(ok.stdout.strip().splitlines(), ["recto-pdf", "recto-pdf", "Recto"])

        bad = subprocess.run(
            [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
             f"{fn_body}\nResolve-SolutionTarget -RepoRoot '{ROOT}' -Requested 'no-such-project'"],
            capture_output=True, text=True, timeout=60,
        )
        self.assertNotEqual(bad.returncode, 0)
        self.assertIn("Unknown project", bad.stdout + bad.stderr)

        # When running tests on x64 with -Project, recto-tests must be appended to -t: so it is relinked before running.
        self.assertIn("if (-not $SkipTests -and $plat -eq 'x64' -and ($targets -notcontains 'recto-tests'))", text)
        self.assertIn("$targets += 'recto-tests'", text)

    @unittest.skipUnless(POWERSHELL, "needs PowerShell")
    def test_restore_current_detects_cleared_nuget_package_cache(self):
        text = BUILD_PS1.read_text(encoding="utf-8")
        fp_match = re.search(r"(function Get-RestoreFingerprint\b.*?^})", text, re.M | re.S)
        rc_match = re.search(r"(function Test-RestoreCurrent\b.*?^})", text, re.M | re.S)
        self.assertIsNotNone(fp_match)
        self.assertIsNotNone(rc_match)
        assert fp_match is not None and rc_match is not None and POWERSHELL is not None

        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp) / "repo"
            pkg_cache = Path(tmp) / "nuget"
            proj_dir = ws / "src" / "app"
            obj_dir = proj_dir / "obj"
            obj_dir.mkdir(parents=True)
            (ws / "Recto.sln").write_text("", encoding="utf-8")
            (ws / "Directory.Build.props").write_text("", encoding="utf-8")
            (ws / "Directory.Build.targets").write_text("", encoding="utf-8")
            (ws / "vcpkg.json").write_text("{}", encoding="utf-8")
            (ws / "vcpkg-configuration.json").write_text("{}", encoding="utf-8")
            (proj_dir / "Recto.vcxproj").write_text(
                '<Project><ItemGroup><PackageReference Include="Sample.Pkg" Version="1.0.0" /></ItemGroup></Project>',
                encoding="utf-8",
            )
            (obj_dir / "Recto.vcxproj.nuget.g.props").write_text("<Project />", encoding="utf-8")
            (obj_dir / "Recto.vcxproj.nuget.g.targets").write_text("<Project />", encoding="utf-8")

            pkg_dir = pkg_cache / "sample.pkg" / "1.0.0"
            (pkg_dir / "build" / "native").mkdir(parents=True)
            (pkg_dir / ".nupkg.metadata").write_text("{}", encoding="utf-8")
            (pkg_dir / "build" / "native" / "Sample.Pkg.targets").write_text("<Project />", encoding="utf-8")

            assets = {
                "version": 3,
                "packageFolders": {str(pkg_cache) + "\\": {}},
                "libraries": {
                    "Sample.Pkg/1.0.0": {
                        "type": "package",
                        "path": "sample.pkg/1.0.0",
                        "files": [".nupkg.metadata", "build/native/Sample.Pkg.targets"],
                    }
                },
            }
            (obj_dir / "project.assets.json").write_text(json.dumps(assets), encoding="utf-8")
            stamp = ws / "build" / "restore.stamp"
            stamp.parent.mkdir(parents=True)

            ps_probe = (
                f"{fp_match.group(1)}\n{rc_match.group(1)}\n"
                f"$meta = Get-RestoreFingerprint -RepoRoot '{ws}'\n"
                f"Set-Content -Path '{stamp}' -Value $meta.Hash -NoNewline\n"
                f"$before = Test-RestoreCurrent -RepoRoot '{ws}' -StampFile '{stamp}' -Meta $meta\n"
                f"Remove-Item -Recurse -Force '{pkg_dir}'\n"
                f"$after = Test-RestoreCurrent -RepoRoot '{ws}' -StampFile '{stamp}' -Meta $meta\n"
                "Write-Output \"$before,$after\"\n"
            )
            done = subprocess.run(
                [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps_probe],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
            self.assertEqual(done.stdout.strip(), "True,False")


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


class BuildParallelism(unittest.TestCase):
    """Memory-bounded MSBuild (/m) and compiler (/MP + CL_MPCount) parallelism in Directory.Build.targets,
    build/parallelism.ps1, and build/build.ps1 (D00 T04 §3)."""

    @staticmethod
    def _has_shared_mp(targets_xml: str) -> bool:
        import xml.etree.ElementTree as ET
        ns = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}
        root = ET.fromstring(targets_xml)
        for group in root.findall("ItemDefinitionGroup") + root.findall("ms:ItemDefinitionGroup", ns):
            cond = group.attrib.get("Condition", "")
            if "MSBuildProjectName.StartsWith('recto-')" in cond and "EnableASAN" not in cond:
                for cl in list(group):
                    if cl.tag.endswith("ClCompile"):
                        for child in list(cl):
                            if child.tag.endswith("MultiProcessorCompilation") and (child.text or "").strip() == "true":
                                return True
        return False

    def test_directory_build_targets_enables_multiprocessor_compilation(self):
        targets_text = (ROOT / "Directory.Build.targets").read_text(encoding="utf-8")
        self.assertTrue(self._has_shared_mp(targets_text))
        stripped = targets_text.replace("<MultiProcessorCompilation>true</MultiProcessorCompilation>", "")
        self.assertFalse(self._has_shared_mp(stripped))
        if MSBUILD:
            with tempfile.TemporaryDirectory() as tmp:
                proj = Path(tmp) / "recto-probe.vcxproj"
                proj.write_text(f"""<Project>
  <PropertyGroup>
    <Configuration>Debug</Configuration>
    <Platform>x64</Platform>
  </PropertyGroup>
  <ItemGroup><ClCompile Include="sample.cpp" /></ItemGroup>
  <Import Project="{ROOT / 'Directory.Build.targets'}" />
  <Target Name="ProbeMp">
    <Message Importance="High" Text="MP=%(ClCompile.MultiProcessorCompilation)" />
  </Target>
</Project>
""", encoding="utf-8")
                for asan in ("false", "true"):
                    run = subprocess.run(
                        [MSBUILD, str(proj), "-nologo", "-t:ProbeMp", f"/p:EnableASAN={asan}"],
                        capture_output=True, text=True, timeout=120,
                    )
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    self.assertIn("MP=true", run.stdout)

    @unittest.skipUnless(POWERSHELL, "needs PowerShell")
    def test_formula_split_and_minimum_memory_floor(self):
        probe = f"""
. '{PARALLELISM_PS1}'
$cases = @(
    @(3584, 12),
    @(5119, 12),
    @(5120, 12),
    @(6656, 12),
    @(9500, 12),
    @(11264, 12),
    @(25000, 12),
    @(9500, 1)
)
$out = @()
foreach ($c in $cases) {{
    $r = Resolve-BuildParallelism -Cores $c[1] -JobsEnv '' -MaxCpuCount 0 -AvailableMb $c[0]
    $out += [ordered]@{{
        free = $c[0]; cores = $c[1]; total = $r.TotalCompilers;
        m = $r.Projects; cl = $r.ClMpCount; header = $r.Header
    }}
}}
$out | ConvertTo-Json -Compress
"""
        run = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", probe],
                             capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        rows = {(r["free"], r["cores"]): r for r in json.loads(run.stdout.strip())}
        self.assertEqual((rows[(3584, 12)]["total"], rows[(3584, 12)]["m"], rows[(3584, 12)]["cl"]), (1, 1, 1))
        self.assertEqual(rows[(3584, 12)]["header"], "free 3,584 MB, 12 cores: /m:1, CL_MPCount=1")
        self.assertEqual((rows[(5119, 12)]["total"], rows[(5119, 12)]["m"], rows[(5119, 12)]["cl"]), (1, 1, 1))
        self.assertEqual((rows[(5120, 12)]["total"], rows[(5120, 12)]["m"], rows[(5120, 12)]["cl"]), (2, 1, 2))
        self.assertEqual((rows[(6656, 12)]["total"], rows[(6656, 12)]["m"], rows[(6656, 12)]["cl"]), (3, 1, 3))
        self.assertEqual((rows[(9500, 12)]["total"], rows[(9500, 12)]["m"], rows[(9500, 12)]["cl"]), (4, 2, 2))
        self.assertEqual(rows[(9500, 12)]["header"], "free 9,500 MB, 12 cores: /m:2, CL_MPCount=2")
        self.assertEqual((rows[(11264, 12)]["total"], rows[(11264, 12)]["m"], rows[(11264, 12)]["cl"]), (6, 3, 2))
        self.assertEqual((rows[(25000, 12)]["total"], rows[(25000, 12)]["m"], rows[(25000, 12)]["cl"]), (12, 3, 4))
        self.assertEqual((rows[(9500, 1)]["total"], rows[(9500, 1)]["m"], rows[(9500, 1)]["cl"]), (1, 1, 1))
        for r in rows.values():
            self.assertLessEqual(r["m"] * r["cl"], r["total"])
            if r["total"] < 6:
                self.assertLessEqual(r["m"], 2)

        low = subprocess.run(
            [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
             f". '{PARALLELISM_PS1}'; Resolve-BuildParallelism -Cores 12 -JobsEnv '' -MaxCpuCount 0 -AvailableMb 3583"],
            capture_output=True, text=True, timeout=60,
        )
        self.assertNotEqual(low.returncode, 0)
        msg = low.stdout + low.stderr
        self.assertIn("3,583 MB", msg)
        self.assertIn("3,584 MB", msg)
        self.assertIn("RECTO_BUILD_JOBS=1", msg)

    @unittest.skipUnless(POWERSHELL, "needs PowerShell")
    def test_memory_probe_counter_fallback_and_dual_failure(self):
        ok_script = f"""
. '{PARALLELISM_PS1}'
$fromCounter = Resolve-BuildParallelism -Cores 12 -JobsEnv '' -MaxCpuCount 0 -CounterProbe {{ 6000.9 }} -CimProbe {{ throw 'cim broken' }}
$fromCim = Resolve-BuildParallelism -Cores 12 -JobsEnv '' -MaxCpuCount 0 -CounterProbe {{ throw 'counter broken' }} -CimProbe {{ 9500.0 }}
@($fromCounter, $fromCim) | ConvertTo-Json -Compress
"""
        ok = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ok_script],
                            capture_output=True, text=True, timeout=60)
        self.assertEqual(ok.returncode, 0, ok.stdout + ok.stderr)
        c_row, w_row = json.loads(ok.stdout.strip())
        self.assertEqual((c_row["FreeMb"], c_row["Projects"], c_row["ClMpCount"]), (6000, 1, 2))
        self.assertEqual((w_row["FreeMb"], w_row["Projects"], w_row["ClMpCount"]), (9500, 2, 2))

        for bad_expr in (
            "-CounterProbe { throw 'no counter' } -CimProbe { throw 'no cim' }",
            "-CounterProbe { 0 } -CimProbe { [double]::NaN }",
        ):
            bad = subprocess.run(
                [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                 f". '{PARALLELISM_PS1}'; Resolve-BuildParallelism -Cores 12 -JobsEnv '' -MaxCpuCount 0 {bad_expr}"],
                capture_output=True, text=True, timeout=60,
            )
            self.assertNotEqual(bad.returncode, 0)
            err = bad.stdout + bad.stderr
            self.assertIn("Available MBytes", err)
            self.assertIn("Win32_OperatingSystem.FreePhysicalMemory", err)
            self.assertIn("RECTO_BUILD_JOBS", err)

    @unittest.skipUnless(POWERSHELL, "needs PowerShell")
    def test_build_ps1_behavioral_msbuild_arguments_precedence_and_refusals(self):
        host_cores = max(1, os.cpu_count() or 1)
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            ws = base / "repo"
            (ws / "build").mkdir(parents=True)
            (ws / "scripts").mkdir(parents=True)
            for name in ("Recto.sln", "Directory.Build.props", "Directory.Build.targets",
                         "vcpkg.json", "vcpkg-configuration.json"):
                shutil.copy2(ROOT / name, ws / name)
            for name in ("build.ps1", "parallelism.ps1", "asan-runtime.ps1", "msbuild-log.ps1"):
                shutil.copy2(ROOT / "build" / name, ws / "build" / name)
            (ws / "scripts" / "check-ui-boundary.py").write_text("import sys\nsys.exit(0)\n", encoding="utf-8")

            pf86 = base / "pf86"
            vs_installer = pf86 / "Microsoft Visual Studio" / "Installer"
            vs_installer.mkdir(parents=True)
            vs_root = base / "vs2026"
            msbuild_dir = vs_root / "MSBuild" / "Current" / "Bin"
            msbuild_dir.mkdir(parents=True)
            asan_dir = vs_root / "VC" / "Tools" / "MSVC" / "14.51" / "bin" / "Hostx64" / "x64"
            asan_dir.mkdir(parents=True)
            (asan_dir / "clang_rt.asan_dynamic-x86_64.dll").write_bytes(b"MZ")
            vswhere_ps1 = vs_installer / "vswhere.ps1"
            vswhere_ps1.write_text(f"Write-Output '{vs_root}'\n$global:LASTEXITCODE = 0\n", encoding="utf-8")

            calls_file = base / "msbuild_calls.jsonl"
            msbuild_ps1 = msbuild_dir / "MSBuild.ps1"
            msbuild_ps1.write_text(
                f"$line = (@($args) | ConvertTo-Json -Compress)\n"
                f"if ($args.Count -eq 1) {{ $line = '[' + $line + ']' }}\n"
                f"Add-Content -LiteralPath '{calls_file}' -Value $line -Encoding UTF8\n"
                "foreach ($a in $args) {\n"
                "    if ($a -like '/flp:logfile=*') {\n"
                "        $logPath = ($a -split '=', 2)[1].Split(';')[0]\n"
                "        Set-Content -LiteralPath $logPath -Value \"Build succeeded.`n    0 Warning(s)`n    0 Error(s)`n\" -Encoding UTF8\n"
                "    }\n"
                "}\n"
                "$global:LASTEXITCODE = 0\n",
                encoding="utf-8",
            )

            def invoke_build(extra_ps_args: str, counter_body: str = "9500", cim_kb: str = "9728000",
                             jobs_env: str | None = None) -> tuple[int, str, list[list[str]]]:
                if calls_file.exists():
                    calls_file.unlink()
                stamp = ws / "build" / "restore.stamp"
                if stamp.exists():
                    stamp.unlink()
                env = os.environ.copy()
                env["ProgramFiles(x86)"] = str(pf86)
                if jobs_env is None:
                    env.pop("RECTO_BUILD_JOBS", None)
                else:
                    env["RECTO_BUILD_JOBS"] = jobs_env
                cmd = (
                    f"Set-Alias -Name '{vs_installer / 'vswhere.exe'}' -Value '{vswhere_ps1}' -Scope Global\n"
                    f"Set-Alias -Name '{msbuild_dir / 'MSBuild.exe'}' -Value '{msbuild_ps1}' -Scope Global\n"
                    f"function global:Get-Counter {{ [pscustomobject]@{{ CounterSamples = @([pscustomobject]@{{ CookedValue = ({counter_body}) }}) }} }}\n"
                    f"function global:Get-CimInstance {{ [pscustomobject]@{{ FreePhysicalMemory = ({cim_kb}) }} }}\n"
                    f"& '{ws / 'build' / 'build.ps1'}' {extra_ps_args}\n"
                )
                res = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", cmd],
                                     capture_output=True, text=True, env=env, timeout=120)
                recorded = [json.loads(line) for line in calls_file.read_text(encoding="utf-8").splitlines() if line.strip()] \
                    if calls_file.exists() else []
                return res.returncode, res.stdout + res.stderr, recorded

            # 1. Default solution build + NuGet restore at 9,500 MB free.
            exp_total = min(host_cores, (9500 - 2048) // 1536)
            exp_proj = min(host_cores, 1 if exp_total < 4 else (2 if exp_total < 6 else 3))
            exp_cl = max(1, exp_total // exp_proj)
            rc, out, calls = invoke_build("-SkipTests")
            self.assertEqual(rc, 0, out)
            self.assertIn(f"free 9,500 MB, {host_cores} cores: /m:{exp_proj}, CL_MPCount={exp_cl}", out)
            self.assertEqual(len(calls), 3)  # Restore, Debug|x64, Release|x64
            self.assertIn("-t:Restore", calls[0])
            self.assertIn(f"/m:{exp_proj}", calls[0])
            self.assertIn("/nr:false", calls[0])
            for build_call in calls[1:]:
                self.assertIn(f"/m:{exp_proj}", build_call)
                self.assertIn(f"/p:CL_MPCount={exp_cl}", build_call)
                self.assertIn("/nr:false", build_call)

            # 2. -Project and -EnableASAN paths, plus -MaxCpuCount 0 automatic convention.
            target_jobs = min(host_cores, 2)
            rc, out, calls = invoke_build("-Project recto-pdf -Configuration Debug -SkipTests -MaxCpuCount 0",
                                          counter_body="5500")
            self.assertEqual(rc, 0, out)
            self.assertIn("-t:recto-pdf", calls[-1])
            self.assertIn("/m:1", calls[-1])
            self.assertIn(f"/p:CL_MPCount={target_jobs}", calls[-1])

            rc, out, calls = invoke_build("-EnableASAN -Configuration Debug -SkipTests", counter_body="5500")
            self.assertEqual(rc, 0, out)
            self.assertIn("/p:EnableASAN=true", calls[-1])
            self.assertIn("/m:1", calls[-1])
            self.assertIn(f"/p:CL_MPCount={target_jobs}", calls[-1])

            # 3. Precedence: RECTO_BUILD_JOBS overrides -MaxCpuCount; -MaxCpuCount > 0 overrides low auto memory.
            rc, out, calls = invoke_build(f"-Configuration Debug -SkipTests -MaxCpuCount {target_jobs}",
                                          counter_body="2000", cim_kb="2048000")
            self.assertEqual(rc, 0, out)
            self.assertIn(f"(-MaxCpuCount {target_jobs})", out)
            self.assertIn(f"/p:CL_MPCount={target_jobs}", calls[-1])

            rc, out, calls = invoke_build(f"-Configuration Debug -SkipTests -MaxCpuCount {host_cores}",
                                          counter_body="2000", jobs_env="1")
            self.assertEqual(rc, 0, out)
            self.assertIn("(RECTO_BUILD_JOBS=1)", out)
            self.assertIn("/m:1", calls[-1])
            self.assertIn("/p:CL_MPCount=1", calls[-1])

            # 4. Pre-MSBuild refusals: MSBuild must never be invoked.
            for label, extra, counter_expr, cim_expr, env_val in (
                ("env=0", "-SkipTests", "9500", "9728000", "0"),
                ("env=-1", "-SkipTests", "9500", "9728000", "-1"),
                ("env=abc", "-SkipTests", "9500", "9728000", "abc"),
                ("env>cores", "-SkipTests", "9500", "9728000", str(host_cores + 1)),
                ("cli=-1", "-SkipTests -MaxCpuCount -1", "9500", "9728000", None),
                ("cli>cores", f"-SkipTests -MaxCpuCount {host_cores + 1}", "9500", "9728000", None),
                ("low_mem", "-SkipTests", "3583", "3668992", None),
                ("probe_fail", "-SkipTests", "throw 'no counter'", "throw 'no cim'", None),
            ):
                with self.subTest(case=label):
                    rc, out, calls = invoke_build(extra, counter_body=counter_expr, cim_kb=cim_expr, jobs_env=env_val)
                    self.assertNotEqual(rc, 0, out)
                    self.assertEqual(calls, [], f"MSBuild should not start on {label}: {out}")

    @unittest.skipUnless(os.name == "nt" and POWERSHELL, "needs Windows cmd.exe and PowerShell")
    def test_build_cmd_iteration_gate_forwards_debug_x64_and_strips_cmd_quotes_on_test_filter(self):
        csc_candidates = [
            Path(os.environ.get("SystemRoot", r"C:\Windows")) / "Microsoft.NET" / "Framework64" / "v4.0.30319" / "csc.exe",
            Path(os.environ.get("SystemRoot", r"C:\Windows")) / "Microsoft.NET" / "Framework" / "v4.0.30319" / "csc.exe",
        ]
        csc = next((p for p in csc_candidates if p.is_file()), None)
        self.assertIsNotNone(csc, "csc.exe required to compile fake vswhere/MSBuild/recto-tests")

        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            ws = base / "repo"
            (ws / "build").mkdir(parents=True)
            (ws / "scripts").mkdir(parents=True)
            shutil.copy2(ROOT / "build.cmd", ws / "build.cmd")
            for name in ("Recto.sln", "Directory.Build.props", "Directory.Build.targets",
                         "vcpkg.json", "vcpkg-configuration.json"):
                shutil.copy2(ROOT / name, ws / name)
            for name in ("build.ps1", "parallelism.ps1", "asan-runtime.ps1", "msbuild-log.ps1"):
                shutil.copy2(ROOT / "build" / name, ws / "build" / name)
            (ws / "scripts" / "check-ui-boundary.py").write_text("import sys\nsys.exit(0)\n", encoding="utf-8")

            pf86 = base / "pf86"
            vs_installer = pf86 / "Microsoft Visual Studio" / "Installer"
            vs_installer.mkdir(parents=True)
            vs_root = base / "vs2026"
            msbuild_dir = vs_root / "MSBuild" / "Current" / "Bin"
            msbuild_dir.mkdir(parents=True)

            vswhere_cs = base / "vswhere.cs"
            vs_escaped = str(vs_root).replace("\\", "\\\\")
            vswhere_cs.write_text(
                f'class V {{ static int Main() {{ System.Console.WriteLine("{vs_escaped}"); return 0; }} }}\n',
                encoding="utf-8",
            )
            subprocess.run([str(csc), "/nologo", "/target:exe", f"/out:{vs_installer / 'vswhere.exe'}", str(vswhere_cs)],
                           check=True, capture_output=True, text=True, timeout=30)

            msbuild_calls = base / "msbuild_calls.txt"
            calls_escaped = str(msbuild_calls).replace("\\", "\\\\")
            msbuild_cs = base / "msbuild.cs"
            msbuild_cs.write_text(
                "using System; using System.IO;\n"
                "class M {\n"
                "    static int Main(string[] args) {\n"
                f'        File.AppendAllLines("{calls_escaped}", new string[] {{ string.Join(" ", args) }});\n'
                "        foreach (string a in args) {\n"
                '            if (a.StartsWith("/flp:logfile=")) {\n'
                "                string lp = a.Substring(\"/flp:logfile=\".Length).Split(';')[0];\n"
                '                File.WriteAllText(lp, "Build succeeded.\\n    0 Warning(s)\\n    0 Error(s)\\n");\n'
                "            }\n"
                "        }\n"
                "        return 0;\n"
                "    }\n"
                "}\n",
                encoding="utf-8",
            )
            compile_env = os.environ.copy()
            r1 = _run_managed(
                [str(csc), "/nologo", "/target:exe", f"/out:{msbuild_dir / 'MSBuild.exe'}", str(msbuild_cs)],
                cwd=base,
                env=compile_env,
                timeout=30,
            )
            self.assertEqual(r1.returncode, 0, r1.stdout + r1.stderr)

            gtest_dir = ws / "build" / "bin" / "x64" / "Debug" / "recto-tests"
            gtest_dir.mkdir(parents=True)
            gtest_calls = base / "gtest_calls.txt"
            gcalls_escaped = str(gtest_calls).replace("\\", "\\\\")
            gtest_cs = base / "gtest.cs"
            gtest_cs.write_text(
                "using System; using System.IO;\n"
                "class G {\n"
                "    static int Main(string[] args) {\n"
                f'        File.AppendAllLines("{gcalls_escaped}", new string[] {{ string.Join("|", args) }});\n'
                "        foreach (string a in args) {\n"
                '            if (a.StartsWith("--gtest_output=xml:")) {\n'
                "                string xp = a.Substring(\"--gtest_output=xml:\".Length);\n"
                '                File.WriteAllText(xp, "<testsuites tests=\\"3\\" failures=\\"0\\"></testsuites>");\n'
                "            }\n"
                "        }\n"
                "        return 0;\n"
                "    }\n"
                "}\n",
                encoding="utf-8",
            )
            r2 = _run_managed(
                [str(csc), "/nologo", "/target:exe", f"/out:{gtest_dir / 'recto-tests.exe'}", str(gtest_cs)],
                cwd=base,
                env=compile_env,
                timeout=30,
            )
            self.assertEqual(r2.returncode, 0, r2.stdout + r2.stderr)

            env = os.environ.copy()
            env["ProgramFiles(x86)"] = str(pf86)
            env["RECTO_BUILD_JOBS"] = "1"

            for shell_kind, cmd in (
                ("cmd", ["cmd.exe", "/d", "/c", r".\build.cmd -Platform x64 -Configuration Debug -TestFilter 'PdfError.*'"]),
                ("pwsh", [POWERSHELL, "-NoProfile", "-Command", r".\build.cmd -Platform x64 -Configuration Debug -TestFilter 'PdfError.*'"]),
            ):
                with self.subTest(shell=shell_kind):
                    if msbuild_calls.exists():
                        msbuild_calls.unlink()
                    if gtest_calls.exists():
                        gtest_calls.unlink()
                    run = _run_managed(cmd, cwd=ws, env=env, timeout=60)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    m_lines = msbuild_calls.read_text(encoding="utf-8").splitlines()
                    # Restore runs on first invocation; only Debug|x64 builds (never Release|x64).
                    self.assertTrue(any("/p:Configuration=Debug" in ln and "/p:Platform=x64" in ln for ln in m_lines), m_lines)
                    self.assertFalse(any("/p:Configuration=Release" in ln for ln in m_lines), m_lines)
                    g_lines = gtest_calls.read_text(encoding="utf-8").splitlines()
                    self.assertEqual(len(g_lines), 1, g_lines)
                    self.assertIn("--gtest_filter=PdfError.*", g_lines[0])
                    self.assertNotIn("'PdfError.*'", g_lines[0])

    def test_run_managed_timeout_reaps_descendant_holding_temp_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            locked = base / "locked.txt"
            ready = base / "ready.txt"
            grandchild = (
                "import pathlib, time; "
                f"f = open({str(locked)!r}, 'w', encoding='utf-8'); "
                f"tmp = pathlib.Path({str(ready) + '.tmp'!r}); "
                "tmp.write_text('1', encoding='utf-8'); "
                f"tmp.replace({str(ready)!r}); "
                "time.sleep(30)"
            )
            parent = (
                "import pathlib, subprocess, sys, time; "
                f"p = subprocess.Popen([sys.executable, '-c', {grandchild!r}]); "
                f"r = pathlib.Path({str(ready)!r}); "
                "deadline = time.monotonic() + 10.0\n"
                "while not r.is_file() and time.monotonic() < deadline:\n"
                "    time.sleep(0.02)\n"
                "time.sleep(30)"
            )
            with self.assertRaises(subprocess.TimeoutExpired):
                _run_managed([sys.executable, "-c", parent], cwd=base, env=os.environ.copy(), timeout=3.0)
            self.assertTrue(ready.is_file())
            locked.unlink()


@unittest.skipUnless(MSBUILD, "needs MSBuild (Windows with Visual Studio)")
class PrecompiledHeadersConfiguration(unittest.TestCase):
    """Every Recto.sln project builds through its own system-only pch.h/pch.cpp with /Yc on pch.cpp and /Yu + /FIpch.h
    on consumer units, shared WIN32_LEAN_AND_MEAN/NOMINMAX definitions, and zero project or TU-configured headers."""

    PROJECTS = (
        ("recto-pdf", ROOT / "src" / "pdf" / "recto-pdf.vcxproj"),
        ("recto-core", ROOT / "src" / "core" / "recto-core.vcxproj"),
        ("recto-services", ROOT / "src" / "services" / "recto-services.vcxproj"),
        ("recto-ui", ROOT / "src" / "ui" / "recto-ui.vcxproj"),
        ("Recto", ROOT / "src" / "app" / "Recto.vcxproj"),
        ("recto-cli", ROOT / "src" / "cli" / "recto-cli.vcxproj"),
        ("recto-tests", ROOT / "tests" / "recto-tests" / "recto-tests.vcxproj"),
        ("recto-fuzz", ROOT / "fuzz" / "recto-fuzz.vcxproj"),
    )

    def test_every_project_pch_header_contains_only_system_and_third_party_headers(self):
        forbidden_angles = ("recto/", "traceloggingprovider.h", "xmmintrin.h", "sanitizer/")
        for proj_name, vcxproj in self.PROJECTS:
            pch_h = vcxproj.parent / "pch.h"
            pch_cpp = vcxproj.parent / "pch.cpp"
            with self.subTest(project=proj_name):
                self.assertTrue(pch_h.is_file(), f"missing {pch_h}")
                self.assertTrue(pch_cpp.is_file(), f"missing {pch_cpp}")
                text = pch_h.read_text(encoding="utf-8")
                quoted = re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M)
                self.assertEqual(quoted, [], f"{pch_h} must not include quoted project headers: {quoted}")
                angles = re.findall(r"^\s*#\s*include\s*<([^>]+)>", text, re.M)
                self.assertTrue(angles, f"{pch_h} includes no system or standard headers")
                for inc in angles:
                    low = inc.strip().replace("\\", "/").lower()
                    for bad in forbidden_angles:
                        self.assertNotIn(bad, low, f"{pch_h} includes forbidden or TU-configured header <{inc}>")

    def test_msbuild_evaluates_create_on_pch_cpp_and_forced_use_on_consumers(self):
        for proj_name, vcxproj in self.PROJECTS:
            for cfg, plat in (("Debug", "x64"), ("Release", "x64"), ("Debug", "ARM64")):
                with self.subTest(project=proj_name, configuration=cfg, platform=plat):
                    run = subprocess.run(
                        [MSBUILD, str(vcxproj), f"-p:Configuration={cfg}", f"-p:Platform={plat}",
                         "-getItem:ClCompile", "-nologo"],
                        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120,
                    )
                    self.assertEqual(run.returncode, 0, run.stdout[-500:] + run.stderr[-500:])
                    items = json.loads(run.stdout)["Items"].get("ClCompile", [])
                    creators = [it for it in items if Path(it["Identity"]).name.lower() == "pch.cpp"]
                    consumers = [it for it in items if Path(it["Identity"]).name.lower() != "pch.cpp"]
                    self.assertEqual(len(creators), 1, f"{proj_name} must have exactly one pch.cpp item")
                    self.assertTrue(consumers, f"{proj_name} has no consumer ClCompile items")

                    creator = creators[0]
                    self.assertEqual(creator.get("PrecompiledHeader"), "Create")
                    self.assertEqual(creator.get("PrecompiledHeaderFile"), "pch.h")
                    self.assertTrue(creator.get("PrecompiledHeaderOutputFile", "").lower().endswith("\\pch.pch"))
                    creator_forced = [s.strip() for s in creator.get("ForcedIncludeFiles", "").split(";") if s.strip()]
                    self.assertEqual(creator_forced, [], f"{proj_name} pch.cpp must not force-include pch.h")

                    proj_dir_norm = os.path.normcase(os.path.normpath(str(vcxproj.parent)))
                    for unit in consumers:
                        ident = unit.get("Identity")
                        self.assertEqual(unit.get("PrecompiledHeader"), "Use", f"{proj_name}:{ident}")
                        self.assertEqual(unit.get("PrecompiledHeaderFile"), "pch.h", f"{proj_name}:{ident}")
                        self.assertTrue(unit.get("PrecompiledHeaderOutputFile", "").lower().endswith("\\pch.pch"),
                                        f"{proj_name}:{ident}")
                        forced = [s.strip() for s in unit.get("ForcedIncludeFiles", "").split(";") if s.strip()]
                        self.assertEqual(forced[0], "pch.h", f"{proj_name}:{ident} ForcedIncludeFiles={forced}")
                        inc_dirs = [s.strip() for s in unit.get("AdditionalIncludeDirectories", "").split(";") if s.strip()]
                        self.assertEqual(os.path.normcase(os.path.normpath(inc_dirs[0])), proj_dir_norm,
                                         f"{proj_name}:{ident} must search own project dir first: {inc_dirs}")
                        defs = {s.strip() for s in unit.get("PreprocessorDefinitions", "").split(";") if s.strip()}
                        self.assertIn("WIN32_LEAN_AND_MEAN", defs, f"{proj_name}:{ident}")
                        self.assertIn("NOMINMAX", defs, f"{proj_name}:{ident}")
                        if proj_name == "recto-tests":
                            self.assertIn("GTEST_LINKED_AS_SHARED_LIBRARY=1", defs, f"{proj_name}:{ident}")


if __name__ == "__main__":
    unittest.main()
