"""Checks on build/build.ps1, the one-command build and test driver, and the ASan runtime staging it loads."""
import contextlib
import ctypes
import hashlib
import json
import os
import pathlib
import re
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from scripts.lib.proc_group import spawn_managed_process, windowless  # noqa: E402

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
SCCACHE_PS1 = ROOT / "build" / "sccache.ps1"
SCCACHE_CPP = ROOT / "build" / "sccache-cl.cpp"
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell")
RECTO_PROJECTS = (
    ("recto-pdf", ROOT / "src" / "pdf" / "recto-pdf.vcxproj"),
    ("recto-core", ROOT / "src" / "core" / "recto-core.vcxproj"),
    ("recto-services", ROOT / "src" / "services" / "recto-services.vcxproj"),
    ("recto-ui", ROOT / "src" / "ui" / "recto-ui.vcxproj"),
    ("Recto", ROOT / "src" / "app" / "Recto.vcxproj"),
    ("recto-cli", ROOT / "src" / "cli" / "recto-cli.vcxproj"),
    ("recto-tests", ROOT / "tests" / "recto-tests" / "recto-tests.vcxproj"),
    ("recto-fuzz", ROOT / "fuzz" / "recto-fuzz.vcxproj"),
)


def _vs_install_path() -> str | None:
    if sys.platform != "win32":
        return None
    vswhere = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return None
    out = subprocess.run(
        [str(vswhere), "-version", "[18.0,19.0)", "-latest", "-property", "installationPath"],
        capture_output=True, text=True, timeout=30,
    ).stdout.strip()
    return out or None


def _csc_path() -> Path | None:
    if sys.platform != "win32":
        return None
    for bits in ("Framework64", "Framework"):
        cand = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "Microsoft.NET" / bits / "v4.0.30319" / "csc.exe"
        if cand.is_file():
            return cand
    return None


def _free_localhost_port() -> int:
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            s.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


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
        for script in (BUILD_PS1, PARALLELISM_PS1, ASAN_RUNTIME_PS1, MSBUILD_LOG_PS1, SCCACHE_PS1):
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
        self.assertIn(
            "<RectoObjFolder Condition=\"'$(EnableAnalyze)' != 'true' And '$(EnableASAN)' == 'true'\">obj-asan</RectoObjFolder>",
            props,
        )
        self.assertIn(
            "<RectoBinFolder Condition=\"'$(EnableAnalyze)' != 'true' And '$(EnableASAN)' == 'true'\">bin-asan</RectoBinFolder>",
            props,
        )
        self.assertIn(r"build\$(RectoObjFolder)\$(MSBuildProjectName)", props)
        self.assertIn(r"build\$(RectoBinFolder)\$(Platform)", props)
        self.assertIn(r'build\$binFolder\x64\$cfg\recto-tests\recto-tests.exe', text)

    def test_every_executable_the_tests_launch_gets_the_runtime(self):
        text = BUILD_PS1.read_text(encoding="utf-8")
        staged = set(re.findall(r"'([^']+)'", re.search(r"foreach \(\$exeProj in @\(([^)]*)\)\)", text).group(1)))
        self.assertEqual(staged, {"recto-tests", "recto-cli", "Recto"})

    def test_non_debug_configurations_disable_incremental_linking(self):
        props = (ROOT / "Directory.Build.props").read_text(encoding="utf-8-sig")
        targets = (ROOT / "Directory.Build.targets").read_text(encoding="utf-8-sig")
        self.assertRegex(
            props,
            r"<LinkIncremental Condition=\"[^\"]*'\$\(Configuration\)' != 'Debug'[^\"]*\">false</LinkIncremental>",
        )
        self.assertRegex(
            targets,
            r"Condition=\"[^\"]*'\$\(Configuration\)' != 'Debug'[^\"]*\">\s*<LinkIncremental>false</LinkIncremental>",
        )
        if MSBUILD:
            projects = dict(RECTO_PROJECTS)
            for name in ("Recto", "recto-cli", "recto-tests", "recto-fuzz"):
                with self.subTest(project=name):
                    run = subprocess.run(
                        [MSBUILD, str(projects[name]), "-nologo", "-p:Configuration=Release", "-p:Platform=x64", "-getProperty:LinkIncremental"],
                        capture_output=True, text=True, timeout=60,
                    )
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    self.assertEqual(run.stdout.strip().lower(), "false")
            asan_debug = subprocess.run(
                [MSBUILD, str(projects["recto-tests"]), "-nologo", "-p:Configuration=Debug", "-p:Platform=x64", "-p:EnableASAN=true", "-getProperty:LinkIncremental"],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(asan_debug.returncode, 0, asan_debug.stdout + asan_debug.stderr)
            self.assertEqual(asan_debug.stdout.strip().lower(), "false")
            plain_debug = subprocess.run(
                [MSBUILD, str(projects["recto-tests"]), "-nologo", "-p:Configuration=Debug", "-p:Platform=x64", "-getProperty:LinkIncremental"],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(plain_debug.returncode, 0, plain_debug.stdout + plain_debug.stderr)
            self.assertNotEqual(plain_debug.stdout.strip().lower(), "false")


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
            # Resolve 8.3 short temp paths (e.g. RUNNER~1) so Set-Alias names match build.ps1's Resolve-Path $root.
            base = Path(tmp).resolve()
            ws = base / "repo"
            (ws / "build").mkdir(parents=True)
            (ws / "scripts").mkdir(parents=True)
            for name in ("Recto.sln", "Directory.Build.props", "Directory.Build.targets",
                         "vcpkg.json", "vcpkg-configuration.json"):
                shutil.copy2(ROOT / name, ws / name)
            for name in ("build.ps1", "parallelism.ps1", "asan-runtime.ps1", "msbuild-log.ps1",
                         "sccache.ps1", "sccache-cl.cpp", "tools.lock.json", "bootstrap-tools.ps1", "python.cmd",
                         "test-shards.ps1", "build-mark.ps1"):
                shutil.copy2(ROOT / "build" / name, ws / "build" / name)
            stub_wrapper = base / "wrapper" / "sccache-cl.exe"
            stub_wrapper.parent.mkdir(parents=True)
            stub_wrapper.write_bytes(b"MZ")
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
                             jobs_env: str | None = None,
                             extra_env: dict[str, str | None] | None = None) -> tuple[int, str, list[list[str]]]:
                if calls_file.exists():
                    calls_file.unlink()
                stamp = ws / "build" / "restore.stamp"
                if stamp.exists():
                    stamp.unlink()
                env = os.environ.copy()
                env["ProgramFiles(x86)"] = str(pf86)
                env["RECTO_TOOLS_DIR"] = str(ROOT / ".tools")
                env["RECTO_SCCACHE_WRAPPER_EXE"] = str(stub_wrapper)
                env["RECTO_SCCACHE_NO_DAEMON"] = "1"
                # These cases skip the tests or stop before them; a fixed count keeps the test plan off this host's memory.
                env["RECTO_TEST_SHARDS"] = "1"
                env.pop("RECTO_USE_SCCACHE", None)
                if jobs_env is None:
                    env.pop("RECTO_BUILD_JOBS", None)
                else:
                    env["RECTO_BUILD_JOBS"] = jobs_env
                if extra_env:
                    for k, v in extra_env.items():
                        if v is None:
                            env.pop(k, None)
                        else:
                            env[k] = v
                cmd = (
                    f"Set-Alias -Name '{vs_installer / 'vswhere.exe'}' -Value '{vswhere_ps1}' -Scope Global\n"
                    f"Set-Alias -Name '{msbuild_dir / 'MSBuild.exe'}' -Value '{msbuild_ps1}' -Scope Global\n"
                    f"function global:Get-Counter {{ [pscustomobject]@{{ CounterSamples = @([pscustomobject]@{{ CookedValue = ({counter_body}) }}) }} }}\n"
                    f"function global:Get-CimInstance {{ [pscustomobject]@{{ FreePhysicalMemory = ({cim_kb}) }} }}\n"
                    f"& '{ws / 'build' / 'build.ps1'}' -MSBuild {extra_ps_args}\n"
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
            self.assertNotIn("/p:UseSccache=true", calls[0])
            self.assertFalse(any(a.startswith("/p:CLToolExe=") for a in calls[0]))
            for build_call in calls[1:]:
                self.assertIn(f"/m:{exp_proj}", build_call)
                self.assertIn(f"/p:CL_MPCount={exp_cl}", build_call)
                self.assertIn("/p:UseSccache=true", build_call)
                self.assertIn("/p:CLToolExe=sccache-cl.exe", build_call)
                cl_tool_paths = [a.split("=", 1)[1] for a in build_call if a.startswith("/p:CLToolPath=")]
                self.assertEqual(len(cl_tool_paths), 1, build_call)
                self.assertEqual(Path(cl_tool_paths[0]).resolve(), stub_wrapper.parent.resolve())
                self.assertIn(f"/p:MultiProcMaxCount={exp_total}", build_call)
                self.assertIn("/nr:false", build_call)

            # 1b. -NoSccache and RECTO_USE_SCCACHE=0 bypass sccache; -NoSccache beats RECTO_USE_SCCACHE=1.
            for label, ps_args, ov_env in (
                ("flag", "-Configuration Debug -SkipTests -NoSccache", None),
                ("env0", "-Configuration Debug -SkipTests", {"RECTO_USE_SCCACHE": "0"}),
                ("flag_beats_env", "-Configuration Debug -SkipTests -NoSccache", {"RECTO_USE_SCCACHE": "1"}),
            ):
                with self.subTest(bypass=label):
                    rc, out, calls = invoke_build(ps_args, extra_env=ov_env)
                    self.assertEqual(rc, 0, out)
                    self.assertNotIn("/p:UseSccache=true", calls[-1])
                    self.assertFalse(any(a.startswith("/p:CLToolExe=") for a in calls[-1]))
                    self.assertFalse(any(a.startswith("/p:MultiProcMaxCount=") for a in calls[-1]))

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

            rc, out, calls = invoke_build("-SkipTests", extra_env={"RECTO_USE_SCCACHE": "bogus"})
            self.assertNotEqual(rc, 0, out)
            self.assertIn("RECTO_USE_SCCACHE", out)
            self.assertEqual(calls, [], f"MSBuild should not start on invalid RECTO_USE_SCCACHE: {out}")

            # A bad test shard count or test deadline is refused before anything is built (D00 T04 section 11),
            # and only when tests will run.
            for variable, bad in (("RECTO_TEST_SHARDS", "0"), ("RECTO_TEST_SHARDS", "many"),
                                  ("RECTO_TEST_TIMEOUT_MINUTES", "0"), ("RECTO_TEST_TIMEOUT_MINUTES", "soon")):
                with self.subTest(variable=variable, bad=bad):
                    rc, out, calls = invoke_build("", extra_env={variable: bad})
                    self.assertNotEqual(rc, 0, out)
                    self.assertIn(variable, out)
                    self.assertEqual(calls, [], f"MSBuild should not start on {variable}={bad}: {out}")
            rc, out, calls = invoke_build("-SkipTests", extra_env={"RECTO_TEST_SHARDS": "many"})
            self.assertEqual(rc, 0, out)

    @unittest.skipUnless(os.name == "nt" and POWERSHELL, "needs Windows PowerShell")
    def test_build_ps1_test_timing_report_budget_and_asan_xml(self):
        with tempfile.TemporaryDirectory() as tmp:
            # Resolve 8.3 short temp paths (e.g. RUNNER~1) so Set-Alias names match build.ps1's Resolve-Path $root.
            base = Path(tmp).resolve()
            ws = base / "repo"
            (base / "alias-caller").mkdir()
            (ws / "build").mkdir(parents=True)
            (ws / "scripts").mkdir(parents=True)
            (ws / "tests" / "recto-tests").mkdir(parents=True)
            for name in ("Recto.sln", "Directory.Build.props", "Directory.Build.targets",
                         "vcpkg.json", "vcpkg-configuration.json"):
                shutil.copy2(ROOT / name, ws / name)
            for name in ("build.ps1", "parallelism.ps1", "asan-runtime.ps1", "msbuild-log.ps1",
                         "sccache.ps1", "sccache-cl.cpp", "tools.lock.json", "bootstrap-tools.ps1", "python.cmd",
                         "test-shards.ps1", "build-mark.ps1"):
                shutil.copy2(ROOT / "build" / name, ws / "build" / name)
            shutil.copy2(ROOT / "scripts" / "test-times.py", ws / "scripts" / "test-times.py")
            shutil.copy2(ROOT / "tests" / "recto-tests" / "time-budget.json",
                         ws / "tests" / "recto-tests" / "time-budget.json")
            (ws / "scripts" / "check-ui-boundary.py").write_text("import sys\nsys.exit(0)\n", encoding="utf-8")

            stub_wrapper = base / "wrapper" / "sccache-cl.exe"
            stub_wrapper.parent.mkdir(parents=True)
            stub_wrapper.write_bytes(b"MZ")

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

            msbuild_ps1 = msbuild_dir / "MSBuild.ps1"
            msbuild_ps1.write_text(
                "foreach ($a in $args) {\n"
                "    if ($a -like '/flp:logfile=*') {\n"
                "        $logPath = ($a -split '=', 2)[1].Split(';')[0]\n"
                "        Set-Content -LiteralPath $logPath -Value \"Build succeeded.`n    0 Warning(s)`n    0 Error(s)`n\" -Encoding UTF8\n"
                "    }\n"
                "}\n"
                "$global:LASTEXITCODE = 0\n",
                encoding="utf-8",
            )

            # A real executable: build.ps1 starts the test binary with CreateProcessW inside its job object
            # (build/test-shards.ps1, D00 T04 section 11), which a PowerShell alias cannot stand in for.
            csc = _csc_path()
            self.assertIsNotNone(csc, "csc.exe required to compile the fake recto-tests")
            fake_tests_cs = base / "fake_tests.cs"
            fake_tests_cs.write_text(
                "using System; using System.IO; using System.Text;\n"
                "class T {\n"
                "    static int Main(string[] args) {\n"
                "        string xml = null;\n"
                "        foreach (string a in args) if (a.StartsWith(\"--gtest_output=xml:\")) xml = a.Substring(\"--gtest_output=xml:\".Length);\n"
                "        string mode = Environment.GetEnvironmentVariable(\"FAKE_TEST_XML_MODE\") ?? \"\";\n"
                "        if (Array.IndexOf(args, \"--gtest_list_tests\") >= 0) { Console.WriteLine(\"Suite.\"); Console.WriteLine(\"  \" + mode.Split(new char[] { ':' }, 2)[0]); return 0; }\n"
                "        // A sharded run: the one test lands in shard 0; the other shards and the serial pass (a positive filter\n"
                "        // with FAKE_SHARDED set) report none, as a real shard whose share is empty would.\n"
                "        bool sharded = Environment.GetEnvironmentVariable(\"FAKE_SHARDED\") == \"1\";\n"
                "        string index = Environment.GetEnvironmentVariable(\"GTEST_SHARD_INDEX\");\n"
                "        bool positive = false; foreach (string a in args) if (a.StartsWith(\"--gtest_filter=\") && !a.StartsWith(\"--gtest_filter=-\")) positive = true;\n"
                "        if (sharded && xml != null && ((index != null && index != \"0\") || (index == null && positive))) {\n"
                "            File.WriteAllText(xml, \"<?xml version=\\\"1.0\\\" encoding=\\\"UTF-8\\\"?><testsuites tests=\\\"0\\\" time=\\\"0\\\"></testsuites>\", new UTF8Encoding(false));\n"
                "            return 0;\n"
                "        }\n"
                "        if (xml != null) {\n"
                "            if (mode == \"empty\") File.WriteAllBytes(xml, new byte[0]);\n"
                "            else if (mode == \"malformed\") File.WriteAllText(xml, \"<broken\");\n"
                "            else if (mode != \"missing\") {\n"
                "                string[] parts = mode.Split(new char[] { ':' }, 2);\n"
                "                string n = parts[0], s = parts.Length > 1 ? parts[1] : \"0\";\n"
                "                File.WriteAllText(xml, \"<?xml version=\\\"1.0\\\" encoding=\\\"UTF-8\\\"?><testsuites tests=\\\"1\\\" failures=\\\"0\\\" disabled=\\\"0\\\" errors=\\\"0\\\" time=\\\"\" + s + \"\\\"><testsuite name=\\\"Suite\\\" tests=\\\"1\\\" failures=\\\"0\\\" disabled=\\\"0\\\" errors=\\\"0\\\" time=\\\"\" + s + \"\\\"><testcase name=\\\"\" + n + \"\\\" status=\\\"run\\\" result=\\\"completed\\\" time=\\\"\" + s + \"\\\" classname=\\\"Suite\\\" /></testsuite></testsuites>\", new UTF8Encoding(false));\n"
                "            }\n"
                "        }\n"
                "        string code = Environment.GetEnvironmentVariable(\"FAKE_TEST_EXIT\");\n"
                "        return string.IsNullOrEmpty(code) ? 0 : int.Parse(code);\n"
                "    }\n"
                "}\n",
                encoding="utf-8",
            )

            normal_exe = ws / "build" / "bin" / "x64" / "Debug" / "recto-tests" / "recto-tests.exe"
            asan_exe = ws / "build" / "bin-asan" / "x64" / "Debug" / "recto-tests" / "recto-tests.exe"
            for exe in (normal_exe, asan_exe):
                exe.parent.mkdir(parents=True)
                built = _run_managed([str(csc), "/nologo", "/target:exe", f"/out:{exe}", str(fake_tests_cs)],
                                     cwd=base, env=os.environ.copy(), timeout=60)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            # The ASan build stages the runtime its test binary imports, as dumpbin reports it.
            dumpbin_cs = base / "dumpbin.cs"
            dumpbin_cs.write_text(
                'class D { static int Main() { System.Console.WriteLine("    clang_rt.asan_dynamic-x86_64.dll"); return 0; } }\n',
                encoding="utf-8",
            )
            built = _run_managed([str(csc), "/nologo", "/target:exe", f"/out:{asan_dir / 'dumpbin.exe'}", str(dumpbin_cs)],
                                 cwd=base, env=os.environ.copy(), timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            def run_case(ps_args: str, xml_mode: str, exit_code: int, shards: str = "1") -> tuple[int, str]:
                for xml_file in (ws / "build").glob("test-results-*.xml"):
                    xml_file.unlink()
                env = os.environ.copy()
                env["ProgramFiles(x86)"] = str(pf86)
                env["RECTO_TOOLS_DIR"] = str(ROOT / ".tools")
                env["RECTO_SCCACHE_WRAPPER_EXE"] = str(stub_wrapper)
                env["RECTO_SCCACHE_NO_DAEMON"] = "1"
                # One process unless a case asks for shards: most cases are about the single run's report and budget.
                env["RECTO_TEST_SHARDS"] = shards
                env["FAKE_SHARDED"] = "1" if shards != "1" else "0"
                env["FAKE_TEST_XML_MODE"] = xml_mode
                env["FAKE_TEST_EXIT"] = str(exit_code)
                cmd = (
                    f"Set-Alias -Name '{vs_installer / 'vswhere.exe'}' -Value '{vswhere_ps1}' -Scope Global\n"
                    f"Set-Alias -Name '{msbuild_dir / 'MSBuild.exe'}' -Value '{msbuild_ps1}' -Scope Global\n"
                    "function global:Get-Counter { [pscustomobject]@{ CounterSamples = @([pscustomobject]@{ CookedValue = 9500 }) } }\n"
                    "function global:Get-CimInstance { [pscustomobject]@{ FreePhysicalMemory = 9728000 } }\n"
                    f"& '{base / 'alias-caller' / '..' / 'repo' / 'build' / 'build.ps1'}' -MSBuild {ps_args}\n"
                )
                res = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", cmd],
                                     capture_output=True, text=True, env=env, timeout=120)
                return res.returncode, res.stdout + res.stderr

            # (a) Full Debug run writes test-results-x64-Debug.xml and passes debug budget.
            rc, out = run_case("-Configuration Debug", "Fast:0.100", 0)
            self.assertEqual(rc, 0, out)
            self.assertTrue((ws / "build" / "test-results-x64-Debug.xml").is_file(), out)
            self.assertIn("Suite.Fast", out)

            # (b) Full Debug run fails budget at 25.0s.
            rc, out = run_case("-Configuration Debug", "TooSlow:25.000", 0)
            self.assertNotEqual(rc, 0, out)
            self.assertIn("recto-tests exceeded per-test time budget (debug): Debug|x64", out)
            self.assertIn("Suite.TooSlow", out)

            # (c) Filtered Debug run reports timing without enforcing full-suite budget check.
            rc, out = run_case("-Configuration Debug -TestFilter Foo.*", "TooSlow:25.000", 0)
            self.assertEqual(rc, 0, out)
            self.assertIn("Suite.TooSlow", out)

            # (d) Full -EnableASAN Debug run writes test-results-x64-Debug-asan.xml and checks asan budget.
            rc, out = run_case("-Configuration Debug -EnableASAN", "AsanOk:35.000", 0)
            self.assertEqual(rc, 0, out)
            self.assertTrue((ws / "build" / "test-results-x64-Debug-asan.xml").is_file(), out)
            self.assertFalse((ws / "build" / "test-results-x64-Debug.xml").exists(), out)
            rc, out = run_case("-Configuration Debug -EnableASAN", "AsanTooSlow:65.000", 0)
            self.assertNotEqual(rc, 0, out)
            self.assertIn("recto-tests exceeded per-test time budget (asan): Debug|x64", out)

            # (e) Failing test run with valid XML prints timing report before throwing recto-tests failure.
            rc, out = run_case("-Configuration Debug", "FailingSlow:25.000", 1)
            self.assertNotEqual(rc, 0, out)
            report_pos = out.find("Suite.FailingSlow")
            fail_pos = out.find("recto-tests failed: Debug|x64 (exit 1)")
            self.assertGreaterEqual(report_pos, 0, out)
            self.assertGreater(fail_pos, report_pos, out)

            # (f) Failing test run with missing, empty, or malformed XML retains the original test failure.
            for bad_mode in ("missing", "empty", "malformed"):
                with self.subTest(bad_mode=bad_mode):
                    rc, out = run_case("-Configuration Debug", bad_mode, 1)
                    self.assertNotEqual(rc, 0, out)
                    self.assertIn("recto-tests failed: Debug|x64 (exit 1)", out)

            # (g) A sharded run writes its reports, logs and test list into -TestOutputDir, relative (from the
            # repository root) or absolute, under names a plain and an ASan run never share (D00 T04 section 11).
            shutil.copy2(ROOT / "tests" / "recto-tests" / "serial-tests.txt", ws / "tests" / "recto-tests" / "serial-tests.txt")
            absolute = base / "abs out"
            for ps_args, folder, tag in (("-Configuration Debug -TestOutputDir out\\rel", ws / "out" / "rel", "Debug"),
                                         (f"-Configuration Debug -EnableASAN -TestOutputDir '{absolute}'", absolute, "Debug-asan")):
                with self.subTest(ps_args=ps_args):
                    rc, out = run_case(ps_args, "Sharded:0.100", 0, shards="2")
                    self.assertEqual(rc, 0, out)
                    self.assertIn("2 shard(s) (RECTO_TEST_SHARDS=2)", out)
                    # Two slots work through 2 x RectoTestChunksPerShard (4) gtest shards from a queue (D00 T04 section 15).
                    chunks = 2 * 4
                    self.assertIn(f"OK: 1 listed tests each reported exactly once across {chunks + 1} report(s)", out)
                    names = sorted(p.name for p in folder.iterdir())
                    self.assertEqual(names, sorted(
                        [f"test-results-x64-{tag}-shard{i}.xml" for i in range(chunks)]
                        + [f"test-x64-{tag}-shard{i}.log" for i in range(chunks)]
                        + [f"test-results-x64-{tag}-serial.xml", f"test-x64-{tag}-serial.log", f"test-list-x64-{tag}.txt"]), out)

    @unittest.skipUnless(os.name == "nt" and POWERSHELL, "needs Windows cmd.exe and PowerShell")
    def test_build_cmd_iteration_gate_forwards_debug_x64_and_strips_cmd_quotes_on_test_filter(self):
        csc_candidates = [
            Path(os.environ.get("SystemRoot", r"C:\Windows")) / "Microsoft.NET" / "Framework64" / "v4.0.30319" / "csc.exe",
            Path(os.environ.get("SystemRoot", r"C:\Windows")) / "Microsoft.NET" / "Framework" / "v4.0.30319" / "csc.exe",
        ]
        csc = next((p for p in csc_candidates if p.is_file()), None)
        self.assertIsNotNone(csc, "csc.exe required to compile fake vswhere/MSBuild/recto-tests")

        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp).resolve()
            ws = base / "repo"
            (ws / "build").mkdir(parents=True)
            (ws / "scripts").mkdir(parents=True)
            shutil.copy2(ROOT / "build.cmd", ws / "build.cmd")
            for name in ("Recto.sln", "Directory.Build.props", "Directory.Build.targets",
                         "vcpkg.json", "vcpkg-configuration.json"):
                shutil.copy2(ROOT / name, ws / name)
            for name in ("build.ps1", "parallelism.ps1", "asan-runtime.ps1", "msbuild-log.ps1",
                         "sccache.ps1", "sccache-cl.cpp", "tools.lock.json", "bootstrap-tools.ps1", "python.cmd",
                         "test-shards.ps1", "build-mark.ps1"):
                shutil.copy2(ROOT / "build" / name, ws / "build" / name)
            stub_wrapper = base / "wrapper" / "sccache-cl.exe"
            stub_wrapper.parent.mkdir(parents=True)
            stub_wrapper.write_bytes(b"MZ")
            gen_marker = base / "gen_compile_ran.txt"
            boundary_marker = base / "boundary_ran.txt"
            (ws / "scripts" / "gen-compile-commands.py").write_text(
                f"import pathlib, sys\npathlib.Path(r'{gen_marker}').write_text(sys.executable, encoding='utf-8')\nsys.exit(0)\n",
                encoding="utf-8",
            )
            (ws / "scripts" / "check-ui-boundary.py").write_text(
                f"import pathlib, sys\npathlib.Path(r'{boundary_marker}').write_text(sys.executable, encoding='utf-8')\nsys.exit(0)\n",
                encoding="utf-8",
            )
            shutil.copy2(ROOT / "scripts" / "test-times.py", ws / "scripts" / "test-times.py")

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
                '        if (Environment.GetEnvironmentVariable("RECTO_MOCK_MSBUILD_FAIL") == "1" && Array.IndexOf(args, "-t:Restore") < 0) return 1;\n'
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
                '                File.WriteAllText(xp, "<testsuites tests=\\"3\\" failures=\\"0\\"><testsuite name=\\"PdfError\\" tests=\\"3\\"><testcase name=\\"A\\" status=\\"run\\" result=\\"completed\\" time=\\"0.001\\" classname=\\"PdfError\\" /></testsuite></testsuites>");\n'
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
            env["RECTO_TOOLS_DIR"] = str(ROOT / ".tools")
            env["RECTO_SCCACHE_WRAPPER_EXE"] = str(stub_wrapper)
            env["RECTO_SCCACHE_NO_DAEMON"] = "1"
            env.pop("RECTO_USE_SCCACHE", None)
            # Strip all system Python directories from PATH to prove build.cmd uses build\python.cmd.
            py_names = ("python.exe", "python3.exe", "python.cmd", "python.bat", "py.exe")
            env["PATH"] = os.pathsep.join(
                entry for entry in env.get("PATH", "").split(os.pathsep)
                if entry and not any((Path(entry) / n).exists() for n in py_names)
            )

            for shell_kind, cmd in (
                ("cmd", ["cmd.exe", "/d", "/c", r".\build.cmd -MSBuild -Platform x64 -Configuration Debug -TestFilter 'PdfError.*'"]),
                ("pwsh", [POWERSHELL, "-NoProfile", "-Command", r".\build.cmd -MSBuild -Platform x64 -Configuration Debug -TestFilter 'PdfError.*'"]),
            ):
                with self.subTest(shell=shell_kind):
                    for marker in (msbuild_calls, gtest_calls, gen_marker, boundary_marker):
                        if marker.exists():
                            marker.unlink()
                    run = _run_managed(cmd, cwd=ws, env=env, timeout=90)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    self.assertTrue(gen_marker.is_file(), "gen-compile-commands.py did not run via build\\python.cmd")
                    self.assertTrue(boundary_marker.is_file(), "check-ui-boundary.py did not run via build\\python.cmd")
                    self.assertIn(".tools", gen_marker.read_text(encoding="utf-8").lower())
                    self.assertIn(".tools", boundary_marker.read_text(encoding="utf-8").lower())
                    m_lines = msbuild_calls.read_text(encoding="utf-8").splitlines()
                    # Restore runs on first invocation; only Debug|x64 builds (never Release|x64).
                    self.assertTrue(any("/p:Configuration=Debug" in ln and "/p:Platform=x64" in ln for ln in m_lines), m_lines)
                    self.assertFalse(any("/p:Configuration=Release" in ln for ln in m_lines), m_lines)
                    g_lines = gtest_calls.read_text(encoding="utf-8").splitlines()
                    self.assertEqual(len(g_lines), 1, g_lines)
                    self.assertIn("--gtest_filter=PdfError.*", g_lines[0])
                    self.assertNotIn("'PdfError.*'", g_lines[0])

            # When MSBuild fails after a cached build (stale recto-tests.exe already on disk), build.cmd must fail
            # and must never run the stale test executable as evidence of success.
            for marker in (msbuild_calls, gtest_calls):
                if marker.exists():
                    marker.unlink()
            fail_env = dict(env)
            fail_env["RECTO_MOCK_MSBUILD_FAIL"] = "1"
            fail_run = _run_managed(
                ["cmd.exe", "/d", "/c", r".\build.cmd -MSBuild -Platform x64 -Configuration Debug -TestFilter 'PdfError.*'"],
                cwd=ws, env=fail_env, timeout=90,
            )
            self.assertNotEqual(fail_run.returncode, 0, fail_run.stdout + fail_run.stderr)
            self.assertFalse(gtest_calls.exists(), "stale recto-tests.exe ran after MSBuild failure")

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

    PROJECTS = RECTO_PROJECTS

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


@unittest.skipUnless(MSBUILD and POWERSHELL, "needs MSBuild (Windows with Visual Studio) and PowerShell")
class SccacheConfigurationTests(unittest.TestCase):
    """D00 T04 s7: /Z7 debug info with linker PDBs, UseSccache isolation in props/targets,
    uncached native wrapper bootstrap, isolated sccache server verification, and MSBuild
    cache-hit + incremental .tlog + failure-propagation behavior."""

    @staticmethod
    def _sccache_exe() -> Path | None:
        lock = json.loads((ROOT / "build" / "tools.lock.json").read_text(encoding="utf-8"))
        tools_root = Path(os.environ.get("RECTO_TOOLS_DIR") or (ROOT / ".tools"))
        cand = tools_root / "sccache" / lock["sccache"]["version"] / lock["sccache"]["executable"].replace("/", "\\")
        return cand if cand.is_file() else None

    @staticmethod
    def _port_listening(port: int, timeout: float = 0.2) -> bool:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=timeout):
                return True
        except OSError:
            return False

    @staticmethod
    def _port_bindable(port: int) -> bool:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            try:
                sock.bind(("127.0.0.1", port))
                return True
            except OSError:
                return False

    @staticmethod
    def _deterministic_port(cache_dir: Path) -> int:
        canonical = str(cache_dir.resolve()).rstrip("\\/").lower()
        digest = hashlib.sha256(canonical.encode("utf-8")).digest()
        return 42000 + (int.from_bytes(digest[:4], "little") % 20000)

    @staticmethod
    def _stop_server(sccache_exe: Path, port: int) -> None:
        env = os.environ.copy()
        env["SCCACHE_SERVER_PORT"] = str(port)
        subprocess.run(
            [str(sccache_exe), "--stop-server"],
            cwd=os.environ.get("SystemRoot", r"C:\Windows"),
            env=env, capture_output=True, text=True, timeout=30,
            creationflags=windowless(),
        )

    @classmethod
    def _stop_owned_server(cls, sccache_exe: Path, port: int, allowed_root: Path) -> bool:
        if not cls._port_listening(port):
            return False
        env = os.environ.copy()
        env["SCCACHE_SERVER_PORT"] = str(port)
        try:
            res = subprocess.run(
                [str(sccache_exe), "--show-stats", "--stats-format", "json"],
                cwd=os.environ.get("SystemRoot", r"C:\Windows"),
                env=env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                timeout=3, creationflags=windowless(),
            )
        except (subprocess.TimeoutExpired, OSError):
            return False
        if res.returncode != 0:
            return False
        try:
            data = json.loads(res.stdout)
        except ValueError:
            return False
        raw_loc = str(data.get("cache_location", "")).strip()
        m = re.match(r'^Local disk:\s*"(.*)"$', raw_loc)
        if not m:
            return False
        inner = m.group(1).replace("\\\\", "\\")
        try:
            cache_path = Path(inner).resolve()
            owned = cache_path.is_relative_to(allowed_root.resolve())
        except OSError:
            return False
        if not owned:
            return False
        cls._stop_server(sccache_exe, port)
        return True

    @classmethod
    def _server_stats(cls, sccache_exe: Path, port: int) -> dict:
        if not cls._port_listening(port):
            raise AssertionError(f"Expected sccache server already listening on 127.0.0.1:{port} before --show-stats")
        env = os.environ.copy()
        env["SCCACHE_SERVER_PORT"] = str(port)
        res = subprocess.run(
            [str(sccache_exe), "--show-stats", "--stats-format", "json"],
            cwd=os.environ.get("SystemRoot", r"C:\Windows"),
            env=env, capture_output=True, text=True, timeout=30,
            creationflags=windowless(),
        )
        if res.returncode != 0:
            raise AssertionError(f"sccache --show-stats failed on port {port}: {res.stdout}{res.stderr}")
        return json.loads(res.stdout)

    @classmethod
    def _zero_stats(cls, sccache_exe: Path, port: int) -> None:
        if not cls._port_listening(port):
            raise AssertionError(f"Expected sccache server already listening on 127.0.0.1:{port} before --zero-stats")
        env = os.environ.copy()
        env["SCCACHE_SERVER_PORT"] = str(port)
        res = subprocess.run(
            [str(sccache_exe), "--zero-stats"],
            cwd=os.environ.get("SystemRoot", r"C:\Windows"),
            env=env, capture_output=True, text=True, timeout=30,
            creationflags=windowless(),
        )
        if res.returncode != 0:
            raise AssertionError(f"sccache --zero-stats failed on port {port}: {res.stdout}{res.stderr}")

    @staticmethod
    def _counter_sum(group: dict) -> int:
        return sum(int(v) for v in (group or {}).get("counts", {}).values())

    @staticmethod
    def _write_probe_project(ws: Path, props_path: Path, targets_path: Path, extra_cl: str = "", extra_props: str = "") -> Path:
        proj = ws / "recto-e2e-probe.vcxproj"
        proj.write_text(f"""<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Debug|ARM64"><Configuration>Debug</Configuration><Platform>ARM64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <ProjectGuid>{{11111111-2222-3333-4444-555555555555}}</ProjectGuid>
    <RootNamespace>RectoE2eProbe</RootNamespace>
    <Keyword>Win32Proj</Keyword>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <Import Project="{props_path}" />
  <PropertyGroup Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset>v145</PlatformToolset>
  </PropertyGroup>
  {extra_props}
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <PropertyGroup>
    <OutDir>$(ProjectDir)out\\</OutDir>
    <IntDir>$(ProjectDir)obj\\</IntDir>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>$(ProjectDir);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <ForcedIncludeFiles>forced_inc.h;%(ForcedIncludeFiles)</ForcedIncludeFiles>
      {extra_cl}
    </ClCompile>
    <Link>
      <SubSystem>Console</SubSystem>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="pch.cpp">
      <PrecompiledHeader>Create</PrecompiledHeader>
      <ForcedIncludeFiles />
    </ClCompile>
    <ClCompile Include="unit_a.cpp" />
    <ClCompile Include="unit_b.cpp" />
  </ItemGroup>
  <Import Project="{targets_path}" />
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
""", encoding="utf-8")
        return proj

    def test_msbuild_evaluates_z7_and_use_sccache_metadata(self):
        assert MSBUILD is not None
        # DebugInformationFormat=OldStyle (/Z7) applies to all configs (including ASan) while
        # UseSccache=true enables UseMultiToolTask and disables /MP + /Yc//Yu//Fp while keeping ForcedIncludeFiles.
        for proj_name, vcxproj in RECTO_PROJECTS:
            with self.subTest(project=proj_name, mode="default_ide"):
                run = subprocess.run(
                    [MSBUILD, str(vcxproj), "-p:Configuration=Debug", "-p:Platform=x64",
                     "-getProperty:PreferredToolArchitecture,UseMultiToolTask,EnforceProcessCountAcrossBuilds",
                     "-getItem:ClCompile", "-nologo"],
                    capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120,
                    creationflags=windowless(),
                )
                self.assertEqual(run.returncode, 0, run.stdout[-500:] + run.stderr[-500:])
                data = json.loads(run.stdout)
                self.assertEqual(data["Properties"]["PreferredToolArchitecture"], "x64")
                items = data["Items"].get("ClCompile", [])
                self.assertTrue(items)
                for unit in items:
                    self.assertEqual(unit.get("DebugInformationFormat"), "OldStyle", f"{proj_name}:{unit.get('Identity')}")
                    self.assertEqual(unit.get("MultiProcessorCompilation"), "true", f"{proj_name}:{unit.get('Identity')}")

            matrix = (
                ("Debug", "x64", "false"),
                ("Debug", "x64", "true"),
                ("Release", "x64", "false"),
                ("Debug", "ARM64", "false"),
            ) if proj_name in ("recto-pdf", "recto-ui", "Recto", "recto-tests") else (("Debug", "x64", "false"),)

            for cfg, plat, asan in matrix:
                if plat == "ARM64" and asan == "true":
                    continue
                with self.subTest(project=proj_name, cfg=cfg, plat=plat, asan=asan, mode="UseSccache"):
                    run = subprocess.run(
                        [MSBUILD, str(vcxproj), f"-p:Configuration={cfg}", f"-p:Platform={plat}",
                         f"-p:EnableASAN={asan}", "-p:UseSccache=true",
                         "-getProperty:PreferredToolArchitecture,UseMultiToolTask,EnforceProcessCountAcrossBuilds",
                         "-getItem:ClCompile", "-nologo"],
                        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120,
                        creationflags=windowless(),
                    )
                    self.assertEqual(run.returncode, 0, run.stdout[-500:] + run.stderr[-500:])
                    data = json.loads(run.stdout)
                    props = data["Properties"]
                    self.assertEqual(props["PreferredToolArchitecture"], "x64")
                    self.assertEqual(props["UseMultiToolTask"], "true")
                    self.assertEqual(props["EnforceProcessCountAcrossBuilds"], "true")
                    items = data["Items"].get("ClCompile", [])
                    self.assertTrue(items)
                    for unit in items:
                        ident = Path(unit.get("Identity", "")).name.lower()
                        self.assertEqual(unit.get("DebugInformationFormat"), "OldStyle", f"{proj_name}:{ident}")
                        self.assertEqual(unit.get("MultiProcessorCompilation"), "false", f"{proj_name}:{ident}")
                        self.assertEqual(unit.get("PrecompiledHeader"), "NotUsing", f"{proj_name}:{ident}")
                        self.assertEqual(unit.get("PrecompiledHeaderFile", ""), "", f"{proj_name}:{ident}")
                        self.assertEqual(unit.get("PrecompiledHeaderOutputFile", ""), "", f"{proj_name}:{ident}")
                        self.assertEqual(unit.get("GenerateSourceDependencies"), "true", f"{proj_name}:{ident}")
                        forced = [s.strip().lower() for s in unit.get("ForcedIncludeFiles", "").split(";") if s.strip()]
                        if ident == "pch.cpp":
                            self.assertEqual(forced, [], f"{proj_name}:{ident}")
                        else:
                            self.assertTrue(forced and forced[0] == "pch.h", f"{proj_name}:{ident} forced={forced}")

        # Verify Link retains debug PDB generation alongside /Z7 across Debug, Release, and ASan.
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            probe = t / "recto-link-probe.vcxproj"
            probe.write_text(f"""<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <Import Project="{ROOT / 'Directory.Build.props'}" />
  <PropertyGroup Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <UseDebugLibraries Condition="'$(Configuration)'=='Debug'">true</UseDebugLibraries>
    <UseDebugLibraries Condition="'$(Configuration)'!='Debug'">false</UseDebugLibraries>
    <PlatformToolset>v145</PlatformToolset>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <ItemGroup>
    <Link Include="dummy.obj" />
  </ItemGroup>
  <Import Project="{ROOT / 'Directory.Build.targets'}" />
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
""", encoding="utf-8")
            for cfg, asan in (("Debug", "false"), ("Release", "false"), ("Debug", "true")):
                run = subprocess.run(
                    [MSBUILD, str(probe), "-nologo", "-getItem:Link",
                     f"-p:Configuration={cfg}", f"-p:Platform=x64", f"-p:EnableASAN={asan}"],
                    capture_output=True, text=True, timeout=60, creationflags=windowless(),
                )
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                link_items = json.loads(run.stdout)["Items"].get("Link", [])
                self.assertTrue(link_items, run.stdout)
                self.assertIn(link_items[0].get("GenerateDebugInformation"), ("DebugFull", "true"),
                              f"{cfg} ASan={asan}: {link_items[0]}")

    def test_wrapper_bootstrap_failure_propagation_and_arm64_resolution(self):
        assert MSBUILD is not None and POWERSHELL is not None
        vs = _vs_install_path()
        csc = _csc_path()
        self.assertIsNotNone(vs, "VS 2026 installation required")
        self.assertIsNotNone(csc, "csc.exe required for stub sccache executable")
        assert vs is not None and csc is not None

        arm64_cl_dir = Path(vs) / "VC" / "Tools" / "MSVC"
        arm64_cl = sorted(arm64_cl_dir.glob("*/bin/Hostx64/arm64/cl.exe"))
        self.assertTrue(arm64_cl, f"expected Hostx64\\arm64\\cl.exe under {arm64_cl_dir}")
        arm64_bin_dir = arm64_cl[-1].parent

        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            ws = base / "José-日本語-repo"
            (ws / "build").mkdir(parents=True)
            for name in ("sccache.ps1", "sccache-cl.cpp", "tools.lock.json"):
                shutil.copy2(ROOT / "build" / name, ws / "build" / name)
            self.assertFalse((ws / "build" / "sccache-wrapper").exists())

            def run_bootstrap() -> subprocess.CompletedProcess[str]:
                assert POWERSHELL is not None
                script = f"""
. '{ws / 'build' / 'sccache.ps1'}'
$w = Build-RectoSccacheWrapper -RepoRoot '{ws}' -VsPath '{vs}'
Write-Output "WRAPPER=$w"
"""
                env = os.environ.copy()
                env.pop("RECTO_SCCACHE_WRAPPER_EXE", None)
                return _run_managed(
                    [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                    cwd=ws, env=env, timeout=120,
                )

            # 1. Clean-clone uncached build without pre-existing wrapper binary.
            r1 = run_bootstrap()
            self.assertEqual(r1.returncode, 0, r1.stdout + r1.stderr)
            wrapper_exe = ws / "build" / "sccache-wrapper" / "sccache-cl.exe"
            stamp_file = ws / "build" / "sccache-wrapper" / "sccache-cl.sha256"
            self.assertTrue(wrapper_exe.is_file())
            self.assertTrue(stamp_file.is_file())
            mtime1 = wrapper_exe.stat().st_mtime_ns
            stamp1 = stamp_file.read_text(encoding="utf-8")

            # 2. Unchanged second call skips rebuild cleanly.
            r2 = run_bootstrap()
            self.assertEqual(r2.returncode, 0, r2.stdout + r2.stderr)
            self.assertEqual(wrapper_exe.stat().st_mtime_ns, mtime1)

            # 3. Editing sccache-cl.cpp triggers rebuild and updates sha256 stamp.
            cpp = ws / "build" / "sccache-cl.cpp"
            orig_cpp = cpp.read_text(encoding="utf-8")
            cpp.write_text(orig_cpp + "\n// bootstrap rebuild trigger\n", encoding="utf-8")
            r3 = run_bootstrap()
            self.assertEqual(r3.returncode, 0, r3.stdout + r3.stderr)
            stamp2 = stamp_file.read_text(encoding="utf-8")
            self.assertNotEqual(stamp2, stamp1)

            # 4. Syntax error during bootstrap propagates failure and never updates stamp.
            cpp.write_text(orig_cpp + "\nintentionally_broken_syntax;;;\n", encoding="utf-8")
            r4 = run_bootstrap()
            self.assertNotEqual(r4.returncode, 0, r4.stdout + r4.stderr)
            self.assertIn("Uncached bootstrap compile of build\\sccache-cl.cpp failed", r4.stdout + r4.stderr)
            self.assertEqual(stamp_file.read_text(encoding="utf-8"), stamp2)

            # 5. Wrapper direct failure propagation, ARM64 target resolution, and malformed JSON refusal.
            stub_cs = base / "stub_sccache.cs"
            stub_exe = base / "stub_sccache.exe"
            stub_cs.write_text(
                "using System; using System.IO;\n"
                "class S {\n"
                "    static int Main(string[] args) {\n"
                '        string log = Environment.GetEnvironmentVariable("STUB_LOG");\n'
                '        if (!string.IsNullOrEmpty(log)) File.WriteAllLines(log, args);\n'
                '        string obj = Environment.GetEnvironmentVariable("STUB_OBJ");\n'
                '        if (!string.IsNullOrEmpty(obj)) File.WriteAllText(obj, "OBJ");\n'
                '        string js = Environment.GetEnvironmentVariable("STUB_JSON");\n'
                '        string jtxt = Environment.GetEnvironmentVariable("STUB_JSON_TEXT");\n'
                '        if (!string.IsNullOrEmpty(js) && jtxt != null) File.WriteAllText(js, jtxt);\n'
                '        string err = Environment.GetEnvironmentVariable("STUB_ERR");\n'
                '        if (!string.IsNullOrEmpty(err)) Console.Error.WriteLine(err);\n'
                '        int code = 0;\n'
                '        int.TryParse(Environment.GetEnvironmentVariable("STUB_EXIT") ?? "0", out code);\n'
                "        return code;\n"
                "    }\n"
                "}\n",
                encoding="utf-8",
            )
            rc_csc = subprocess.run(
                [str(csc), "/nologo", "/target:exe", f"/out:{stub_exe}", str(stub_cs)],
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(rc_csc.returncode, 0, rc_csc.stdout + rc_csc.stderr)

            base_env = os.environ.copy()
            base_env.pop("RECTO_REAL_CL_EXE", None)

            # 5a. Missing RECTO_SCCACHE_EXE fails exit 2.
            env_missing = dict(base_env)
            env_missing.pop("RECTO_SCCACHE_EXE", None)
            r_miss = _run_managed([str(wrapper_exe), "/c", "foo.cpp"], cwd=base, env=env_missing, timeout=30)
            self.assertEqual(r_miss.returncode, 2)
            self.assertIn("RECTO_SCCACHE_EXE", r_miss.stderr)

            # 5b. Missing target cl.exe on PATH fails exit 2.
            env_nocl = dict(base_env)
            env_nocl["RECTO_SCCACHE_EXE"] = str(stub_exe)
            env_nocl["PATH"] = str(wrapper_exe.parent)
            r_nocl = _run_managed([str(wrapper_exe), "/c", "foo.cpp"], cwd=base, env=env_nocl, timeout=30)
            self.assertEqual(r_nocl.returncode, 2)
            self.assertIn("cl.exe", r_nocl.stderr)

            # 5c. ARM64 Hostx64\arm64\cl.exe resolution + valid UTF-16LE @rsp + valid JSON tracking succeeds.
            src_cpp = base / "unit.cpp"
            hdr_h = base / "hdr.h"
            obj_file = base / "unit.obj"
            json_file = base / "unit.cpp.json"
            rsp_file = base / "cmd.rsp"
            src_cpp.write_text("int k = 1;\n", encoding="utf-8")
            hdr_h.write_text("#define X 1\n", encoding="utf-8")
            rsp_file.write_bytes(
                b"\xff\xfe" + f'/c /Fo"{obj_file}" /sourceDependencies "{json_file}" "{src_cpp}"'.encode("utf-16-le")
            )
            stub_log = base / "stub_args.txt"
            valid_json = json.dumps({"Version": "1.2", "Data": {"Source": str(src_cpp), "Includes": [str(hdr_h)]}})

            env_ok = dict(base_env)
            env_ok["RECTO_SCCACHE_EXE"] = str(stub_exe)
            env_ok["PATH"] = f"{wrapper_exe.parent}{os.pathsep}{arm64_bin_dir}"
            env_ok["STUB_LOG"] = str(stub_log)
            env_ok["STUB_OBJ"] = str(obj_file)
            env_ok["STUB_JSON"] = str(json_file)
            env_ok["STUB_JSON_TEXT"] = valid_json
            env_ok["STUB_EXIT"] = "0"
            r_arm = _run_managed([str(wrapper_exe), f'@{rsp_file}'], cwd=base, env=env_ok, timeout=30)
            self.assertEqual(r_arm.returncode, 0, r_arm.stdout + r_arm.stderr)
            captured_args = stub_log.read_text(encoding="utf-8").splitlines()
            self.assertEqual(os.path.normcase(captured_args[0]), os.path.normcase(str(arm64_cl[-1])))

            # 5d. Child failure exit 7 + stderr diagnostics propagate without tracking failure masking it.
            if json_file.exists():
                json_file.unlink()
            env_fail = dict(env_ok)
            env_fail.pop("STUB_JSON_TEXT", None)
            env_fail.pop("STUB_OBJ", None)
            env_fail["STUB_ERR"] = "unit.cpp(3): error C2065: synthetic diagnostic"
            env_fail["STUB_EXIT"] = "7"
            r_fail = _run_managed([str(wrapper_exe), f'@{rsp_file}'], cwd=base, env=env_fail, timeout=30)
            self.assertEqual(r_fail.returncode, 7)
            self.assertIn("error C2065: synthetic diagnostic", r_fail.stderr)

            # 5e. Child exits 0, but /sourceDependencies JSON is missing, truncated, or lacks Includes -> exit 3!
            for bad_label, bad_text in (
                ("missing", None),
                ("empty", ""),
                ("truncated", '{"Version":"1.2","Data":{"Source":"x"'),
                ("missing_includes", '{"Version":"1.2","Data":{"Source":"x"}}'),
            ):
                with self.subTest(json_case=bad_label):
                    if json_file.exists():
                        json_file.unlink()
                    env_bad = dict(env_ok)
                    env_bad["STUB_EXIT"] = "0"
                    if bad_text is None:
                        env_bad.pop("STUB_JSON_TEXT", None)
                    else:
                        env_bad["STUB_JSON_TEXT"] = bad_text
                    r_bad = _run_managed([str(wrapper_exe), f'@{rsp_file}'], cwd=base, env=env_bad, timeout=30)
                    self.assertEqual(r_bad.returncode, 3, r_bad.stdout + r_bad.stderr)
                    self.assertIn("/sourceDependencies JSON", r_bad.stderr)

    def test_server_isolation_verifies_cache_dir_and_10g_without_disrupting_other_repo(self):
        assert POWERSHELL is not None
        sccache_exe = self._sccache_exe()
        self.assertIsNotNone(sccache_exe, "bootstrapped .tools sccache.exe required")
        assert sccache_exe is not None

        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            other_cache = base / "other-cache"
            other_cache.mkdir()
            stub_wrapper = base / "sccache-cl.exe"
            stub_wrapper.write_bytes(b"MZ")

            unused_probe_port = _free_localhost_port()
            other_port = _free_localhost_port()
            while other_port == unused_probe_port:
                other_port = _free_localhost_port()
            recto_explicit_port = _free_localhost_port()
            while recto_explicit_port in (other_port, unused_probe_port):
                recto_explicit_port = _free_localhost_port()

            recto_cache: Path | None = None
            p0 = p1 = p2 = 0
            reserved = {unused_probe_port, other_port, recto_explicit_port}
            for idx in range(2048):
                cand_cache = base / f"recto-cache-{idx}"
                det_base = self._deterministic_port(cand_cache)
                c0, c1, c2 = (42000 + ((det_base - 42000 + k) % 20000) for k in range(3))
                if (
                    len({c0, c1, c2} | reserved) == 6
                    and not any(self._port_listening(p) for p in (c0, c1, c2))
                    and all(self._port_bindable(p) for p in (c0, c1, c2))
                ):
                    recto_cache = cand_cache
                    p0, p1, p2 = c0, c1, c2
                    break
            self.assertIsNotNone(recto_cache, "could not allocate 3 free consecutive deterministic sccache ports")
            assert recto_cache is not None
            recto_cache.mkdir()

            cleanup_ports: set[int] = set()
            hung_sock: socket.socket | None = None
            accepted_conns: list[socket.socket] = []
            stop_accept = threading.Event()
            accept_thread: threading.Thread | None = None

            try:
                # 0. Regression: probing an unused port must return $null without auto-starting a daemon.
                probe_script = f"""
. '{SCCACHE_PS1}'
$res = Get-RectoSccacheServerInfo -SccacheExe '{sccache_exe}' -Port {unused_probe_port}
Write-Output ("IS_NULL=" + ($null -eq $res))
"""
                probe_run = _run_managed(
                    [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", probe_script],
                    cwd=ROOT, env=os.environ.copy(), timeout=30,
                )
                self.assertEqual(probe_run.returncode, 0, probe_run.stdout + probe_run.stderr)
                self.assertIn("IS_NULL=True", probe_run.stdout)
                self.assertFalse(self._port_listening(unused_probe_port),
                                 f"Get-RectoSccacheServerInfo unexpectedly started a daemon on port {unused_probe_port}")

                other_env = os.environ.copy()
                other_env["SCCACHE_DIR"] = str(other_cache)
                other_env["SCCACHE_CACHE_SIZE"] = "2G"
                other_env["SCCACHE_SERVER_PORT"] = str(other_port)
                start_other = subprocess.run(
                    [str(sccache_exe), "--start-server"],
                    cwd=Path(os.environ.get("SystemRoot", r"C:\Windows")),
                    env=other_env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                    timeout=30, creationflags=windowless(),
                )
                self.assertEqual(start_other.returncode, 0, start_other.stdout + start_other.stderr)
                cleanup_ports.add(other_port)

                # Bind a silent/hung non-sccache TCP listener on p0 and a live foreign 1G sccache daemon on p1.
                hung_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                hung_sock.bind(("127.0.0.1", p0))
                hung_sock.listen(4)
                hung_sock.settimeout(0.2)

                def accept_hung() -> None:
                    assert hung_sock is not None
                    while not stop_accept.is_set():
                        try:
                            conn, _ = hung_sock.accept()
                            accepted_conns.append(conn)
                        except TimeoutError:
                            continue
                        except OSError:
                            break

                accept_thread = threading.Thread(target=accept_hung, daemon=True)
                accept_thread.start()

                with tempfile.TemporaryDirectory() as foreign_tmp:
                    foreign_cache = Path(foreign_tmp) / "foreign-cache"
                    foreign_cache.mkdir()
                    foreign_started = False
                    try:
                        foreign_env = os.environ.copy()
                        foreign_env["SCCACHE_DIR"] = str(foreign_cache)
                        foreign_env["SCCACHE_CACHE_SIZE"] = "1G"
                        foreign_env["SCCACHE_SERVER_PORT"] = str(p1)
                        start_foreign = subprocess.run(
                            [str(sccache_exe), "--start-server"],
                            cwd=Path(os.environ.get("SystemRoot", r"C:\Windows")),
                            env=foreign_env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                            timeout=30, creationflags=windowless(),
                        )
                        self.assertEqual(start_foreign.returncode, 0, start_foreign.stdout + start_foreign.stderr)
                        foreign_started = True

                        # 1. With SCCACHE_SERVER_PORT unset, Initialize-RectoSccache times out p0 quickly,
                        # skips the foreign p1 server without disrupting it, binds p2 (10G), and leaves other_port (2G) untouched.
                        ps_init = f"""
. '{SCCACHE_PS1}'
$info = Initialize-RectoSccache -RepoRoot '{ROOT}' -VsPath '{_vs_install_path()}'
Write-Output ("PORT=" + $info.ServerPort)
"""
                        r_env = os.environ.copy()
                        r_env["SCCACHE_DIR"] = str(recto_cache)
                        r_env["SCCACHE_CACHE_SIZE"] = "10G"
                        r_env.pop("SCCACHE_SERVER_PORT", None)
                        r_env.pop("RECTO_SCCACHE_NO_DAEMON", None)
                        r_env["RECTO_SCCACHE_WRAPPER_EXE"] = str(stub_wrapper)
                        r_env["RECTO_SCCACHE_PROBE_TIMEOUT_MS"] = "600"
                        run1 = subprocess.run(
                            [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps_init],
                            cwd=ROOT, env=r_env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                            timeout=20, creationflags=windowless(),
                        )
                        self.assertEqual(run1.returncode, 0, run1.stdout + run1.stderr)
                        m = re.search(r"PORT=(\d+)", run1.stdout)
                        self.assertIsNotNone(m, run1.stdout)
                        assert m is not None
                        derived_port = int(m.group(1))
                        cleanup_ports.update({unused_probe_port, other_port, recto_explicit_port, p0, p1, p2, derived_port})
                        self.assertEqual(derived_port, p2)
                        self.assertNotEqual(derived_port, other_port)

                        recto_stats = self._server_stats(sccache_exe, derived_port)
                        self.assertEqual(recto_stats["max_cache_size"], 10 * 1024 ** 3)
                        self.assertIn("recto-cache-", recto_stats["cache_location"].lower())

                        foreign_stats = self._server_stats(sccache_exe, p1)
                        self.assertEqual(foreign_stats["max_cache_size"], 1024 ** 3)
                        self.assertIn("foreign-cache", foreign_stats["cache_location"].lower())

                        other_stats = self._server_stats(sccache_exe, other_port)
                        self.assertEqual(other_stats["max_cache_size"], 2 * 1024 ** 3)
                        self.assertIn("other-cache", other_stats["cache_location"].lower())

                        # 2. A same-cache server running with wrong size (512M) on Recto's explicit port is restarted to 10G.
                        wrong_env = os.environ.copy()
                        wrong_env["SCCACHE_DIR"] = str(recto_cache)
                        wrong_env["SCCACHE_CACHE_SIZE"] = "512M"
                        wrong_env["SCCACHE_SERVER_PORT"] = str(recto_explicit_port)
                        start_wrong = subprocess.run(
                            [str(sccache_exe), "--start-server"],
                            cwd=Path(os.environ.get("SystemRoot", r"C:\Windows")),
                            env=wrong_env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                            timeout=30, creationflags=windowless(),
                        )
                        self.assertEqual(start_wrong.returncode, 0, start_wrong.stdout + start_wrong.stderr)
                        cleanup_ports.add(recto_explicit_port)
                        r_env["SCCACHE_SERVER_PORT"] = str(recto_explicit_port)
                        run2 = subprocess.run(
                            [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps_init],
                            cwd=ROOT, env=r_env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                            timeout=60, creationflags=windowless(),
                        )
                        self.assertEqual(run2.returncode, 0, run2.stdout + run2.stderr)
                        self.assertEqual(self._server_stats(sccache_exe, recto_explicit_port)["max_cache_size"], 10 * 1024 ** 3)

                        # 3. Explicitly pointing SCCACHE_SERVER_PORT at another repo's server fails cleanly without disrupting it.
                        r_env["SCCACHE_SERVER_PORT"] = str(other_port)
                        run3 = subprocess.run(
                            [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps_init],
                            cwd=ROOT, env=r_env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                            timeout=60, creationflags=windowless(),
                        )
                        self.assertNotEqual(run3.returncode, 0, run3.stdout + run3.stderr)
                        self.assertIn("disrupted", (run3.stdout + run3.stderr).lower())
                        self.assertEqual(self._server_stats(sccache_exe, other_port)["max_cache_size"], 2 * 1024 ** 3)

                        # 4. Ownership-verified teardown stops owned test servers only and leaves the foreign p1 server alive.
                        for p in sorted(cleanup_ports):
                            self._stop_owned_server(sccache_exe, p, base)
                        cleanup_ports.clear()
                        self.assertTrue(self._port_listening(p1), "teardown unexpectedly stopped foreign sccache server on p1")
                        self.assertEqual(self._server_stats(sccache_exe, p1)["max_cache_size"], 1024 ** 3)
                    finally:
                        if foreign_started:
                            self._stop_server(sccache_exe, p1)
            finally:
                stop_accept.set()
                if hung_sock is not None:
                    with contextlib.suppress(OSError):
                        hung_sock.close()
                for conn in accepted_conns:
                    with contextlib.suppress(OSError):
                        conn.close()
                if accept_thread is not None:
                    accept_thread.join(timeout=2)
                for p in cleanup_ports:
                    self._stop_owned_server(sccache_exe, p, base)

    def test_msbuild_end_to_end_cache_hits_tlog_invalidation_z7_asan_and_error_refusal(self):
        assert MSBUILD is not None and POWERSHELL is not None
        vs = _vs_install_path()
        sccache_exe = self._sccache_exe()
        self.assertIsNotNone(vs, "VS 2026 required")
        self.assertIsNotNone(sccache_exe, ".tools sccache.exe required")
        assert vs is not None and sccache_exe is not None

        port = _free_localhost_port()
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            ws = base / "proj"
            cache_dir = base / "cache"
            wrapper_dir = base / "wrapper"
            ws.mkdir()
            cache_dir.mkdir()
            wrapper_dir.mkdir()

            props = ROOT / "Directory.Build.props"
            targets = ROOT / "Directory.Build.targets"
            proj = self._write_probe_project(ws, props, targets)

            (ws / "pch.h").write_text("#pragma once\n#include <cstdio>\n", encoding="utf-8")
            (ws / "pch.cpp").write_text('#include "pch.h"\n', encoding="utf-8")
            (ws / "forced_inc.h").write_text("#pragma once\n#define FORCED_VAL 10\n", encoding="utf-8")
            (ws / "direct_inc.h").write_text("#pragma once\ninline int direct_val() { return 101; }\n", encoding="utf-8")
            (ws / "unit_a.cpp").write_text(
                '#include "direct_inc.h"\n'
                'int get_a() { return direct_val(); }\n'
                'int get_f() { return FORCED_VAL; }\n',
                encoding="utf-8",
            )
            (ws / "unit_b.cpp").write_text(
                '#include <cstdio>\n'
                'int get_a();\n'
                'int get_f();\n'
                'int main() {\n'
                '    std::printf("A=%d;B=200;F=%d\\n", get_a(), get_f());\n'
                '    return 0;\n'
                '}\n',
                encoding="utf-8",
            )

            # Always build/verify the current wrapper source before copying into wrapper_dir.
            boot_env = os.environ.copy()
            boot_env.pop("RECTO_SCCACHE_WRAPPER_EXE", None)
            boot = _run_managed(
                [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                 f". '{SCCACHE_PS1}'; $w = Build-RectoSccacheWrapper -RepoRoot '{ROOT}' -VsPath '{vs}'; Write-Output $w"],
                cwd=ROOT, env=boot_env, timeout=120,
            )
            self.assertEqual(boot.returncode, 0, boot.stdout + boot.stderr)
            built_wrapper = Path(boot.stdout.strip().splitlines()[-1])
            wrapper_exe = wrapper_dir / "sccache-cl.exe"
            shutil.copy2(built_wrapper, wrapper_exe)

            try:
                env = os.environ.copy()
                env["SCCACHE_DIR"] = str(cache_dir)
                env["SCCACHE_CACHE_SIZE"] = "10G"
                env["SCCACHE_SERVER_PORT"] = str(port)
                env["RECTO_SCCACHE_EXE"] = str(sccache_exe)
                env.pop("RECTO_REAL_CL_EXE", None)

                start_srv = subprocess.run(
                    [str(sccache_exe), "--start-server"],
                    cwd=Path(os.environ.get("SystemRoot", r"C:\Windows")),
                    env=env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                    timeout=30, creationflags=windowless(),
                )
                self.assertEqual(start_srv.returncode, 0, start_srv.stdout + start_srv.stderr)

                def run_msbuild(platform: str = "x64", asan: str = "false", target: str = "Build",
                                proj_override: Path | None = None) -> subprocess.CompletedProcess[str]:
                    assert MSBUILD is not None
                    cmd = [
                        MSBUILD, str(proj_override or proj), "-nologo", f"-t:{target}",
                        "-p:Configuration=Debug", f"-p:Platform={platform}", f"-p:EnableASAN={asan}",
                        "-p:TreatWarningsAsErrors=true", "-p:UseSccache=true",
                        f"-p:CLToolExe={wrapper_exe.name}", f"-p:CLToolPath={wrapper_exe.parent}",
                        "-p:MultiProcMaxCount=2", "/nr:false",
                    ]
                    return _run_managed(cmd, cwd=ws, env=env, timeout=180)

                out_exe = ws / "out" / "recto-e2e-probe.exe"
                out_pdb = ws / "out" / "recto-e2e-probe.pdb"

                # 1. Cold build: 3 misses, PDB generated, .tlog files record direct + forced headers & JSON/.obj outputs.
                self._zero_stats(sccache_exe, port)
                r1 = run_msbuild()
                self.assertEqual(r1.returncode, 0, r1.stdout + r1.stderr)
                st1 = self._server_stats(sccache_exe, port)["stats"]
                self.assertEqual(st1["compile_requests"], 3)
                self.assertEqual(self._counter_sum(st1["cache_misses"]), 3)
                self.assertEqual(self._counter_sum(st1["cache_hits"]), 0)
                self.assertEqual(st1["non_cacheable_compilations"], 0)
                self.assertTrue(out_exe.is_file())
                self.assertTrue(out_pdb.is_file(), "linker must emit PDB with /Z7")

                read_tlog_paths = sorted((ws / "obj").rglob("*CL.read.*.tlog"))
                write_tlog_paths = sorted((ws / "obj").rglob("*CL.write.*.tlog"))
                self.assertTrue(read_tlog_paths, f"expected CL.read.*.tlog files under {ws / 'obj'}")
                self.assertTrue(write_tlog_paths, f"expected CL.write.*.tlog files under {ws / 'obj'}")
                read_tlogs = "\n".join(p.read_text(encoding="utf-16-le", errors="replace") for p in read_tlog_paths)
                write_tlogs = "\n".join(p.read_text(encoding="utf-16-le", errors="replace") for p in write_tlog_paths)
                self.assertIn("DIRECT_INC.H", read_tlogs.upper())
                self.assertIn("FORCED_INC.H", read_tlogs.upper())
                self.assertIn("UNIT_A.OBJ", write_tlogs.upper())
                self.assertIn("UNIT_A.CPP.JSON", write_tlogs.upper())
                self.assertEqual(_run_managed([str(out_exe)], cwd=ws, env=env, timeout=30).stdout.strip(), "A=101;B=200;F=10")

                # 2. Immediate no-op build issues 0 compile requests.
                self._zero_stats(sccache_exe, port)
                r2 = run_msbuild()
                self.assertEqual(r2.returncode, 0, r2.stdout + r2.stderr)
                self.assertEqual(self._server_stats(sccache_exe, port)["stats"]["compile_requests"], 0)

                # 3. Delete IntDir (`obj`) -> warm rebuild gets 100% cache hits (3/3) and restores .tlog tracking!
                shutil.rmtree(ws / "obj")
                self._zero_stats(sccache_exe, port)
                r3 = run_msbuild()
                self.assertEqual(r3.returncode, 0, r3.stdout + r3.stderr)
                st3 = self._server_stats(sccache_exe, port)["stats"]
                self.assertEqual(st3["compile_requests"], 3)
                self.assertEqual(self._counter_sum(st3["cache_hits"]), 3)
                self.assertEqual(self._counter_sum(st3["cache_misses"]), 0)
                self.assertEqual(st3["non_cacheable_compilations"], 0)

                # 4. Immediate no-op after cache-hit restore issues 0 compile requests (proves FileTracker replay).
                self._zero_stats(sccache_exe, port)
                r4 = run_msbuild()
                self.assertEqual(r4.returncode, 0, r4.stdout + r4.stderr)
                self.assertEqual(self._server_stats(sccache_exe, port)["stats"]["compile_requests"], 0)

                # 5. Editing directly included header after cache restore rebuilds only unit_a.cpp (1 request) and updates exe.
                self._zero_stats(sccache_exe, port)
                (ws / "direct_inc.h").write_text("#pragma once\ninline int direct_val() { return 102; }\n", encoding="utf-8")
                r5 = run_msbuild()
                self.assertEqual(r5.returncode, 0, r5.stdout + r5.stderr)
                st5 = self._server_stats(sccache_exe, port)["stats"]
                self.assertEqual(st5["compile_requests"], 1)
                self.assertEqual(self._counter_sum(st5["cache_misses"]), 1)
                self.assertEqual(_run_managed([str(out_exe)], cwd=ws, env=env, timeout=30).stdout.strip(), "A=102;B=200;F=10")

                # 6. Editing forced-included header rebuilds dependents and next no-op stays at 0 requests.
                self._zero_stats(sccache_exe, port)
                (ws / "forced_inc.h").write_text("#pragma once\n#define FORCED_VAL 25\n", encoding="utf-8")
                r6 = run_msbuild()
                self.assertEqual(r6.returncode, 0, r6.stdout + r6.stderr)
                self.assertGreaterEqual(self._server_stats(sccache_exe, port)["stats"]["compile_requests"], 2)
                self.assertEqual(_run_managed([str(out_exe)], cwd=ws, env=env, timeout=30).stdout.strip(), "A=102;B=200;F=25")

                self._zero_stats(sccache_exe, port)
                r6b = run_msbuild()
                self.assertEqual(r6b.returncode, 0, r6b.stdout + r6b.stderr)
                self.assertEqual(self._server_stats(sccache_exe, port)["stats"]["compile_requests"], 0)

                # 7. Deliberate compiler error after successful cached build fails without rewriting exe.
                exe_mtime = out_exe.stat().st_mtime_ns
                good_a = (ws / "unit_a.cpp").read_text(encoding="utf-8")
                (ws / "unit_a.cpp").write_text(good_a + "\nint broken_fn() { return intentional_compile_break_token; }\n", encoding="utf-8")
                r7 = run_msbuild()
                self.assertNotEqual(r7.returncode, 0, r7.stdout + r7.stderr)
                self.assertIn("error C2065", r7.stdout + r7.stderr)
                self.assertIn("intentional_compile_break_token", r7.stdout + r7.stderr)
                self.assertEqual(out_exe.stat().st_mtime_ns, exe_mtime)
                (ws / "unit_a.cpp").write_text(good_a, encoding="utf-8")

                # 8. ARM64 target compile via MSBuild UseSccache=true hits Hostx64\arm64\cl.exe cleanly.
                self._zero_stats(sccache_exe, port)
                r_arm = run_msbuild(platform="ARM64", target="ClCompile")
                self.assertEqual(r_arm.returncode, 0, r_arm.stdout + r_arm.stderr)
                st_arm = self._server_stats(sccache_exe, port)["stats"]
                self.assertEqual(st_arm["compile_requests"], 3)
                self.assertEqual(st_arm["non_cacheable_compilations"], 0)

                # 8b. Explicit 8.3 short -p:IntDir still records canonical long .tlog write paths.
                local_temp = Path(os.environ.get("LOCALAPPDATA", "")) / "Temp"
                if local_temp.is_dir():
                    short_buf = ctypes.create_unicode_buffer(1024)
                    short_len = ctypes.windll.kernel32.GetShortPathNameW(str(local_temp), short_buf, len(short_buf))
                    if short_len > 0 and "~" in short_buf.value:
                        with tempfile.TemporaryDirectory(dir=short_buf.value) as short_tmp:
                            short_ws = Path(short_tmp) / "p"
                            short_ws.mkdir()
                            short_proj = self._write_probe_project(short_ws, props, targets)
                            for fn in ("pch.h", "pch.cpp", "forced_inc.h", "direct_inc.h", "unit_a.cpp", "unit_b.cpp"):
                                shutil.copy2(ws / fn, short_ws / fn)
                            short_int = f"{short_ws}\\obj_short\\"
                            r_short = _run_managed(
                                [MSBUILD, str(short_proj), "-nologo", "-t:ClCompile",
                                 "-p:Configuration=Debug", "-p:Platform=x64", "-p:UseSccache=true",
                                 f"-p:CLToolExe={wrapper_exe.name}", f"-p:CLToolPath={wrapper_exe.parent}",
                                 f"-p:IntDir={short_int}", "-p:MultiProcMaxCount=2", "/nr:false"],
                                cwd=short_ws, env=env, timeout=180,
                            )
                            self.assertEqual(r_short.returncode, 0, r_short.stdout + r_short.stderr)
                            short_writes = "\n".join(
                                p.read_text(encoding="utf-16-le", errors="replace")
                                for p in sorted(short_ws.rglob("*CL.write.*.tlog"))
                            )
                            self.assertIn("UNIT_A.OBJ", short_writes.upper(), short_writes)
                            self.assertIn("UNIT_A.CPP.JSON", short_writes.upper(), short_writes)

                # 9. /Z7 + EnableASAN=true + linker PDB symbolizes ASan stack trace with exact file:line.
                asan_ws = base / "asan_proj"
                asan_ws.mkdir()
                asan_proj = self._write_probe_project(asan_ws, props, targets)
                (asan_ws / "pch.h").write_text("#pragma once\n#include <cstdio>\n", encoding="utf-8")
                (asan_ws / "pch.cpp").write_text('#include "pch.h"\n', encoding="utf-8")
                (asan_ws / "forced_inc.h").write_text("#pragma once\n", encoding="utf-8")
                (asan_ws / "unit_a.cpp").write_text("int helper(int* p) { return p[0]; }\n", encoding="utf-8")
                (asan_ws / "unit_b.cpp").write_text(
                    "int helper(int* p);\n"
                    "int main() {\n"
                    "    int* buf = new int[2]{1, 2};\n"
                    "    delete[] buf;\n"
                    "    return helper(buf);\n"
                    "}\n",
                    encoding="utf-8",
                )
                r_asan = _run_managed(
                    [MSBUILD, str(asan_proj), "-nologo", "-t:Build",
                     "-p:Configuration=Debug", "-p:Platform=x64", "-p:EnableASAN=true",
                     "-p:TreatWarningsAsErrors=true", "-p:UseSccache=true",
                     f"-p:CLToolExe={wrapper_exe.name}", f"-p:CLToolPath={wrapper_exe.parent}",
                     "-p:MultiProcMaxCount=2", "/nr:false"],
                    cwd=asan_ws, env=env, timeout=180,
                )
                self.assertEqual(r_asan.returncode, 0, r_asan.stdout + r_asan.stderr)
                asan_exe = asan_ws / "out" / "recto-e2e-probe.exe"
                asan_pdb = asan_ws / "out" / "recto-e2e-probe.pdb"
                self.assertTrue(asan_pdb.is_file())
                asan_bins = sorted((Path(vs) / "VC" / "Tools" / "MSVC").glob("*/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll"))
                self.assertTrue(asan_bins, "MSVC x64 ASan runtime DLL required")
                shutil.copy2(asan_bins[-1], asan_exe.parent / asan_bins[-1].name)
                run_env = dict(env)
                run_env["PATH"] = f"{asan_bins[-1].parent}{os.pathsep}{run_env.get('PATH', '')}"
                run_env["ASAN_OPTIONS"] = "symbolize=1"
                crash = _run_managed([str(asan_exe)], cwd=asan_ws, env=run_env, timeout=30)
                self.assertNotEqual(crash.returncode, 0, crash.stdout + crash.stderr)
                combined_asan = crash.stdout + crash.stderr
                self.assertIn("heap-use-after-free", combined_asan)
                self.assertRegex(combined_asan, r"unit_[ab]\.cpp:\d+", combined_asan)
            finally:
                self._stop_server(sccache_exe, port)

    def test_port_bindable_probe_and_deterministic_selection_skip_reserved_ports(self) -> None:
        sccache_exe = self._sccache_exe()
        vs_path = _vs_install_path()
        if sccache_exe is None or vs_path is None:
            self.skipTest("tools or Visual Studio not available (.tools/sccache / vswhere)")

        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            cache_dir = None
            base_port = 0
            stride_port = 0
            for idx in range(2048):
                cand = base / f"excluded-cache-{idx}"
                det = self._deterministic_port(cand)
                first_stride = 42000 + ((det - 42000 + 124) % 20000)
                if (
                    not self._port_listening(det)
                    and self._port_bindable(det)
                    and not self._port_listening(first_stride)
                    and self._port_bindable(first_stride)
                ):
                    cache_dir = cand
                    base_port = det
                    stride_port = first_stride
                    break
            self.assertIsNotNone(cache_dir, "could not find candidate cache dir with free base and stride ports")
            assert cache_dir is not None
            cache_dir.mkdir()

            blocked_ports = [42000 + ((base_port - 42000 + offset) % 20000) for offset in range(106)]
            blocked_literal = ",".join(str(p) for p in blocked_ports)
            ps = (
                f". '{SCCACHE_PS1}'; "
                f"$held = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0); "
                f"$held.ExclusiveAddressUse = $true; $held.Start(); "
                f"try {{ "
                f"  $heldPort = ([System.Net.IPEndPoint]$held.LocalEndpoint).Port; "
                f"  if (Test-RectoLocalTcpPortBindable -Port $heldPort) {{ throw 'expected bound port to be unbindable' }}; "
                f"  $held.Stop(); "
                f"  if (-not (Test-RectoLocalTcpPortBindable -Port $heldPort)) {{ throw 'expected released port to be bindable' }}; "
                f"}} finally {{ $held.Stop() }}; "
                f"$script:origBindable = (Get-Command Test-RectoLocalTcpPortBindable).ScriptBlock; "
                f"$blocked = @({blocked_literal}); "
                f"function Test-RectoLocalTcpPortBindable([Parameter(Mandatory=$true)][int]$Port) {{ "
                f"  if ($blocked -contains $Port) {{ return $false }}; "
                f"  & $script:origBindable -Port $Port "
                f"}}; "
                f"$first = Initialize-RectoSccache -RepoRoot '{ROOT}' -VsPath '{vs_path}'; "
                f"if (-not (Test-RectoLocalTcpPortListening -Port $first.ServerPort)) {{ throw ('expected sccache listening on ' + $first.ServerPort) }}; "
                f"$second = Initialize-RectoSccache -RepoRoot '{ROOT}' -VsPath '{vs_path}'; "
                f"$info = Get-RectoSccacheServerInfo -SccacheExe $env:RECTO_SCCACHE_EXE -Port $second.ServerPort; "
                f"if ($null -eq $info -or -not $info.IsSccache) {{ throw 'expected verified sccache instance on selected port' }}; "
                f"Write-Output ($first.ServerPort.ToString() + '|' + $second.ServerPort.ToString())"
            )
            env = os.environ.copy()
            env.pop("SCCACHE_SERVER_PORT", None)
            env.pop("RECTO_SCCACHE_NO_DAEMON", None)
            env["SCCACHE_DIR"] = str(cache_dir)
            env["SCCACHE_CACHE_SIZE"] = "10G"
            try:
                proc = _run_managed(
                    [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps],
                    cwd=ROOT, env=env, timeout=90,
                )
                self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
                parts = proc.stdout.strip().splitlines()[-1].split("|")
                self.assertEqual(parts, [str(stride_port), str(stride_port)], proc.stdout)
            finally:
                self._stop_server(sccache_exe, stride_port)


def _parse_yaml_scalar(token: str) -> str:
    val = token.strip()
    if len(val) >= 2 and val[0] == val[-1] and val[0] in ("'", '"'):
        return val[1:-1]
    return val


def _parse_yaml_flow_sequence(inner: str) -> list[str]:
    items: list[str] = []
    buf: list[str] = []
    quote: str | None = None
    for ch in inner:
        if quote is not None:
            buf.append(ch)
            if ch == quote:
                quote = None
        elif ch in ("'", '"'):
            quote = ch
            buf.append(ch)
        elif ch == ",":
            token = "".join(buf).strip()
            if token:
                items.append(_parse_yaml_scalar(token))
            buf = []
        else:
            buf.append(ch)
    token = "".join(buf).strip()
    if token:
        items.append(_parse_yaml_scalar(token))
    return items


def _validate_analyze_workflow(workflow_text: str) -> list[str]:
    """Validate the analyze job in .github/workflows/build.yml (D06 T02 s10)."""
    errors: list[str] = []
    on_match = re.search(r"(?m)^on:\s*\n(.*?)(?=^[A-Za-z0-9_-]+:\s*$|\Z)", workflow_text, re.DOTALL)
    if not on_match:
        errors.append("workflow missing top-level 'on:' block")
    else:
        on_block = on_match.group(1)
        push_match = re.search(r"(?m)^  push:\s*\n(.*?)(?=^  [A-Za-z0-9_-]+:\s*$|\Z)", on_block, re.DOTALL)
        push_targets_main = False
        if push_match:
            push_body = push_match.group(1)
            branches_match = re.search(r"(?m)^    branches:[ \t]*(.*)$", push_body)
            branch_items: list[str] = []
            if branches_match:
                inline = re.sub(r"\s+#.*$", "", branches_match.group(1)).strip()
                if inline.startswith("[") and inline.endswith("]"):
                    branch_items = _parse_yaml_flow_sequence(inline[1:-1])
                elif not inline:
                    for raw_line in push_body[branches_match.end():].splitlines():
                        if not raw_line.strip() or raw_line.lstrip().startswith("#"):
                            continue
                        indent = len(raw_line) - len(raw_line.lstrip(" "))
                        if indent < 4:
                            break
                        stripped = raw_line.lstrip(" ")
                        if stripped.startswith("- ") or stripped.startswith("-\t"):
                            item_text = re.sub(r"\s+#.*$", "", stripped[1:]).strip()
                            if item_text:
                                branch_items.append(_parse_yaml_scalar(item_text))
                        else:
                            break
            if branch_items == ["main"]:
                push_targets_main = True
            ignore_match = re.search(r"(?m)^    branches-ignore:[ \t]*(.*)$", push_body)
            if ignore_match:
                push_targets_main = False
        if not push_targets_main:
            errors.append("workflow push trigger does not target main branch")
        if not re.search(r"(?m)^  pull_request:\s*$", on_block):
            errors.append("workflow missing pull_request trigger")
        if not re.search(r"(?m)^  workflow_dispatch:\s*$", on_block):
            errors.append("workflow missing workflow_dispatch trigger")
    m = re.search(r"(?m)^  analyze:\s*\n(.*?)(?=^  [A-Za-z0-9_-]+:\s*$|\Z)", workflow_text, re.DOTALL)
    if not m:
        return errors + ["missing 'analyze:' job in build.yml"]
    job_text = m.group(1)
    header_text = job_text.split("    steps:", 1)[0]
    if re.search(r"(?m)^    if\s*:", header_text):
        errors.append("analyze job has a job-level 'if:' condition that could skip analysis")
    if re.search(r"(?m)^    continue-on-error\s*:\s*true", header_text, re.IGNORECASE):
        errors.append("analyze job sets job-level continue-on-error: true")
    if "ref: ${{ inputs.sha || github.sha }}" not in job_text:
        errors.append("analyze job does not check out inputs.sha || github.sha")
    if "REQUESTED_SHA: ${{ inputs.sha }}" not in job_text:
        errors.append("analyze job does not validate inputs.sha")
    run_match = re.search(
        r"- name: Run MSVC /analyze[^\n]*\n(.*?)(?=\n      - name: |\Z)",
        job_text,
        re.DOTALL,
    )
    if not run_match:
        errors.append("analyze job missing 'Run MSVC /analyze' step")
    else:
        step = run_match.group(1)
        if re.search(r"continue-on-error\s*:\s*true", step, re.IGNORECASE):
            errors.append("analyze step sets continue-on-error: true")
        if re.search(r"(?m)^\s+if\s*:", step):
            errors.append("analyze step has an 'if:' condition that could skip analysis")
        cmd_match = re.search(r"(?m)^        run:\s*(.*)$", step)
        if not cmd_match:
            errors.append("analyze step missing run command")
        else:
            first_val = cmd_match.group(1).strip()
            rest_lines = [
                ln.strip()
                for ln in step[cmd_match.end():].splitlines()
                if ln.strip() and not ln.strip().startswith("#")
            ]
            expected_cmd = "powershell -NoProfile -ExecutionPolicy Bypass -File build/build.ps1 -Analyze"
            if first_val != expected_cmd or rest_lines:
                errors.append(
                    f"analyze step run command must be exactly '{expected_cmd}' with no trailing lines (got {first_val!r}, extra={rest_lines!r})"
                )
    upload_match = re.search(
        r"- name: Upload MSVC /analyze SARIF logs[^\n]*\n(.*?)(?=\n      - name: |\Z)",
        job_text,
        re.DOTALL,
    )
    if not upload_match:
        errors.append("analyze job missing SARIF upload step")
    else:
        up = upload_match.group(1)
        if "if: always()" not in up:
            errors.append("SARIF upload step missing 'if: always()'")
        if "build/analyze/*.sarif" not in up:
            errors.append("SARIF upload step missing 'build/analyze/*.sarif'")
    return errors


class MsvcAnalyzeContract(unittest.TestCase):
    """D06 T02 s10: MSVC /analyze folder isolation, ruleset hygiene, SARIF aggregation and CI workflow."""

    def test_analyze_folders_manifests_and_logs_are_isolated_from_plain_build(self):
        props = (ROOT / "Directory.Build.props").read_text(encoding="utf-8")
        targets = (ROOT / "Directory.Build.targets").read_text(encoding="utf-8")
        build_ps1 = BUILD_PS1.read_text(encoding="utf-8")
        self.assertIn("<RectoBinFolder Condition=\"'$(EnableAnalyze)' == 'true'\">bin-analyze</RectoBinFolder>", props)
        self.assertIn("<RectoObjFolder Condition=\"'$(EnableAnalyze)' == 'true'\">obj-analyze</RectoObjFolder>", props)
        self.assertIn("<_RectoBoundaryFolder Condition=\"'$(EnableAnalyze)' == 'true'\">boundary-analyze</_RectoBoundaryFolder>", targets)
        self.assertIn("$logPrefix = if ($Analyze) { 'msbuild-analyze' } else { 'msbuild' }", build_ps1)
        self.assertIn("$boundaryFolder = if ($Analyze) { 'boundary-analyze' } else { 'boundary' }", build_ps1)
        self.assertNotIn("<RectoObjFolder Condition=\"'$(EnableAnalyze)' == 'true'\">obj</RectoObjFolder>", props)
        self.assertNotIn("<RectoBinFolder Condition=\"'$(EnableAnalyze)' == 'true'\">bin</RectoBinFolder>", props)

    def test_ruleset_includes_native_and_core_check_with_zero_disabled_rules(self):
        import xml.etree.ElementTree as ET

        ruleset = ROOT / "build" / "RectoAnalyze.ruleset"
        self.assertTrue(ruleset.is_file(), f"missing {ruleset}")
        tree = ET.parse(ruleset)
        root = tree.getroot()
        includes = {el.attrib.get("Path", "") for el in root.findall("Include")}
        self.assertIn("NativeRecommendedRules.ruleset", includes)
        rule_actions = {el.attrib.get("Id", ""): el.attrib.get("Action", "") for el in root.findall(".//Rule")}
        for required_rule in ("C26815", "C26816", "C26822", "C26459", "C26483", "C26437", "C26478", "C26494"):
            self.assertEqual(rule_actions.get(required_rule), "Warning", f"missing or weakened {required_rule}")
        for rule_id, action in rule_actions.items():
            self.assertNotIn(action.lower(), ("none", "hidden"), f"ruleset disables {rule_id}")
        ignore_check = subprocess.run(
            ["git", "check-ignore", "build/RectoAnalyze.ruleset"],
            cwd=ROOT, capture_output=True, text=True, timeout=30,
        )
        self.assertNotEqual(ignore_check.returncode, 0, "build/RectoAnalyze.ruleset must be allowlisted in .gitignore")

    def test_merge_analyze_sarif_aggregates_multi_tu_findings_and_rejects_bad_reports(self):
        import importlib.util

        spec = importlib.util.spec_from_file_location("merge_analyze_sarif", ROOT / "scripts" / "merge-analyze-sarif.py")
        assert spec and spec.loader
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest_dir = root / "manifests"
            out_dir = root / "out"
            int_dir = root / "obj" / "recto-pdf"
            manifest_dir.mkdir(parents=True)
            int_dir.mkdir(parents=True)
            (manifest_dir / "recto-pdf.txt").write_text(
                f"project|recto-pdf|{int_dir}|D:\\GitHub\\Recto\\build\\RectoAnalyze.ruleset|true|true\n"
                f"unit|D:\\GitHub\\Recto\\src\\pdf\\a.cpp||true|D:\\GitHub\\Recto\\build\\RectoAnalyze.ruleset|\n"
                f"        C:\\VS\\EspXEngine.dll;\n"
                f"      |{int_dir}\\|a\n"
                f"unit|D:\\GitHub\\Recto\\src\\pdf\\b.cpp||true|D:\\GitHub\\Recto\\build\\RectoAnalyze.ruleset|C:\\VS\\EspXEngine.dll;|{int_dir}\\|b\n",
                encoding="utf-8",
            )

            def write_sarif(
                stem: str,
                rule_id: str | None,
                uri: str,
                line: int,
                *,
                execution_successful: bool = True,
                target_uri: str | None = None,
                rules_list: list[dict] | None = None,
                rule_index: int = 0,
            ) -> Path:
                results = []
                if rule_id:
                    results.append({
                        "ruleId": rule_id,
                        "ruleIndex": rule_index,
                        "message": {"text": f"finding {rule_id}"},
                        "locations": [{"physicalLocation": {"artifactLocation": {"index": 0}, "region": {"startLine": line}}}],
                    })
                doc = {
                    "version": "2.1.0",
                    "runs": [{
                        "tool": {"driver": {"name": "PREfast", "rules": rules_list if rules_list is not None else ([{"id": rule_id}] if rule_id else [])}},
                        "invocations": [{"executionSuccessful": execution_successful}],
                        "artifacts": [{"location": {"uri": target_uri or uri}, "roles": ["analysisTarget"]}],
                        "results": results,
                    }],
                }
                p = int_dir / f"{stem}.nativecodeanalysis.sarif"
                p.write_text(json.dumps(doc), encoding="utf-8")
                return p

            # Two findings across two different TUs both survive in intact runs with ruleIndex and artifacts preserved.
            write_sarif(
                "a", "C6386", "file:///D:/GitHub/Recto/src/pdf/a.cpp", 42,
                rules_list=[{"id": "C6386"}, {"id": "C6001"}], rule_index=0,
            )
            write_sarif("b", "C26800", "file:///D:/GitHub/Recto/src/pdf/b.cpp", 99)
            code, errs, findings = mod.merge_and_verify(manifest_dir, out_dir, ["recto-pdf"], "RectoAnalyze.ruleset")
            self.assertEqual(code, 1)
            self.assertEqual(errs, [])
            self.assertEqual(len(findings), 2)
            merged = json.loads((out_dir / "recto-pdf.sarif").read_text(encoding="utf-8"))
            self.assertEqual(merged["version"], "2.1.0")
            self.assertEqual(len(merged["runs"]), 2)
            run0 = merged["runs"][0]
            self.assertEqual(run0["tool"]["driver"]["rules"][run0["results"][0]["ruleIndex"]]["id"], "C6386")
            self.assertEqual(run0["artifacts"][0]["location"]["uri"], "file:///D:/GitHub/Recto/src/pdf/a.cpp")
            self.assertEqual(merged["runs"][1]["results"][0]["ruleId"], "C26800")

            # Clean multi-TU run exits 0.
            write_sarif("a", None, "file:///D:/GitHub/Recto/src/pdf/a.cpp", 1)
            write_sarif("b", None, "file:///D:/GitHub/Recto/src/pdf/b.cpp", 1)
            code, errs, findings = mod.merge_and_verify(manifest_dir, out_dir, ["recto-pdf"], "RectoAnalyze.ruleset")
            self.assertEqual((code, errs, findings), (0, [], []))

            # Failed analyzer invocation (executionSuccessful=false) is refused.
            write_sarif("a", None, "file:///D:/GitHub/Recto/src/pdf/a.cpp", 1, execution_successful=False)
            code, errs, _ = mod.merge_and_verify(manifest_dir, out_dir, ["recto-pdf"], "RectoAnalyze.ruleset")
            self.assertEqual(code, 1)
            self.assertTrue(any("executionSuccessful" in e for e in errs), errs)

            # Empty runs list is refused.
            (int_dir / "a.nativecodeanalysis.sarif").write_text(json.dumps({"version": "2.1.0", "runs": []}), encoding="utf-8")
            code, errs, _ = mod.merge_and_verify(manifest_dir, out_dir, ["recto-pdf"], "RectoAnalyze.ruleset")
            self.assertEqual(code, 1)
            self.assertTrue(any("invalid or empty SARIF 2.1.0 runs" in e for e in errs), errs)

            # Run missing required SARIF 2.1.0 tool.driver.name or using a non-string name is refused.
            for bad_run in (
                {
                    "invocations": [{"executionSuccessful": True}],
                    "artifacts": [{"location": {"uri": "file:///D:/GitHub/Recto/src/pdf/a.cpp"}, "roles": ["analysisTarget"]}],
                    "results": [],
                },
                {
                    "tool": {"driver": {"name": 42}},
                    "invocations": [{"executionSuccessful": True}],
                    "artifacts": [{"location": {"uri": "file:///D:/GitHub/Recto/src/pdf/a.cpp"}, "roles": ["analysisTarget"]}],
                    "results": [],
                },
                {
                    "tool": {"driver": {"name": True}},
                    "invocations": [{"executionSuccessful": True}],
                    "artifacts": [{"location": {"uri": "file:///D:/GitHub/Recto/src/pdf/a.cpp"}, "roles": ["analysisTarget"]}],
                    "results": [],
                },
            ):
                (int_dir / "a.nativecodeanalysis.sarif").write_text(json.dumps({
                    "version": "2.1.0",
                    "runs": [bad_run],
                }), encoding="utf-8")
                code, errs, _ = mod.merge_and_verify(manifest_dir, out_dir, ["recto-pdf"], "RectoAnalyze.ruleset")
                self.assertEqual(code, 1)
                self.assertTrue(any("tool.driver.name" in e for e in errs), errs)

            # Mismatched analysisTarget is refused.
            write_sarif("a", None, "file:///D:/GitHub/Recto/src/pdf/a.cpp", 1, target_uri="file:///D:/GitHub/Recto/src/pdf/other.cpp")
            code, errs, _ = mod.merge_and_verify(manifest_dir, out_dir, ["recto-pdf"], "RectoAnalyze.ruleset")
            self.assertEqual(code, 1)
            self.assertTrue(any("analysisTarget" in e for e in errs), errs)

            # Restore a.sarif; test allow_extra_projects emits SARIF for rebuilt dependency projects too.
            write_sarif("a", None, "file:///D:/GitHub/Recto/src/pdf/a.cpp", 1)
            dep_dir = root / "obj" / "recto-fuzz"
            dep_dir.mkdir(parents=True)
            (manifest_dir / "recto-fuzz.txt").write_text(
                f"project|recto-fuzz|{dep_dir}|D:\\GitHub\\Recto\\build\\RectoAnalyze.ruleset|true|true\n"
                f"unit|D:\\GitHub\\Recto\\fuzz\\f.cpp||true|D:\\GitHub\\Recto\\build\\RectoAnalyze.ruleset|C:\\VS\\EspXEngine.dll;|{dep_dir}\\|f\n",
                encoding="utf-8",
            )
            (dep_dir / "f.nativecodeanalysis.sarif").write_text(json.dumps({
                "version": "2.1.0",
                "runs": [{
                    "tool": {"driver": {"name": "PREfast"}},
                    "invocations": [{"executionSuccessful": True}],
                    "artifacts": [{"location": {"uri": "file:///D:/GitHub/Recto/fuzz/f.cpp"}, "roles": ["analysisTarget"]}],
                    "results": [],
                }],
            }), encoding="utf-8")
            out_dep = root / "out-dep"
            code, errs, findings = mod.merge_and_verify(
                manifest_dir, out_dep, ["recto-fuzz"], "RectoAnalyze.ruleset", allow_extra_projects=True,
            )
            self.assertEqual((code, errs, findings), (0, [], []))
            self.assertTrue((out_dep / "recto-fuzz.sarif").is_file())
            self.assertTrue((out_dep / "recto-pdf.sarif").is_file())
            (manifest_dir / "recto-fuzz.txt").unlink()

            # Missing or unreadable TU SARIF is refused even with 0 findings.
            (int_dir / "b.nativecodeanalysis.sarif").unlink()
            code, errs, _ = mod.merge_and_verify(manifest_dir, out_dir, ["recto-pdf"], "RectoAnalyze.ruleset")
            self.assertEqual(code, 1)
            self.assertTrue(any("missing per-TU SARIF" in e for e in errs), errs)

            # Stale SARIF (older than run start) is refused.
            p_b = write_sarif("b", None, "file:///D:/GitHub/Recto/src/pdf/b.cpp", 1)
            os.utime(p_b, (1000.0, 1000.0))
            code, errs, _ = mod.merge_and_verify(manifest_dir, out_dir, ["recto-pdf"], "RectoAnalyze.ruleset", min_mtime=2000.0)
            self.assertEqual(code, 1)
            self.assertTrue(any("stale per-TU SARIF" in e for e in errs), errs)

    def test_workflow_analyze_job_propagates_failure_and_checks_out_requested_sha(self):
        workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
        self.assertEqual(_validate_analyze_workflow(workflow), [])
        # Negative controls: disabling failure propagation, job-level skip, or dropping requested SHA must fail validation.
        with_job_if = workflow.replace(
            "  analyze:\n",
            "  analyze:\n    if: false\n",
            1,
        )
        self.assertTrue(any("job-level 'if:'" in e for e in _validate_analyze_workflow(with_job_if)))
        with_job_continue = workflow.replace(
            "  analyze:\n",
            "  analyze:\n    continue-on-error: true\n",
            1,
        )
        self.assertTrue(any("job-level continue-on-error" in e for e in _validate_analyze_workflow(with_job_continue)))
        with_continue = workflow.replace(
            "run: powershell -NoProfile -ExecutionPolicy Bypass -File build/build.ps1 -Analyze",
            "continue-on-error: true\n        run: powershell -NoProfile -ExecutionPolicy Bypass -File build/build.ps1 -Analyze",
        )
        self.assertTrue(any("continue-on-error" in e for e in _validate_analyze_workflow(with_continue)))
        with_swallow = workflow.replace(
            "run: powershell -NoProfile -ExecutionPolicy Bypass -File build/build.ps1 -Analyze",
            "run: powershell -NoProfile -ExecutionPolicy Bypass -File build/build.ps1 -Analyze || true",
        )
        self.assertTrue(any("must be exactly" in e for e in _validate_analyze_workflow(with_swallow)))
        with_trailing_exit0 = workflow.replace(
            "run: powershell -NoProfile -ExecutionPolicy Bypass -File build/build.ps1 -Analyze",
            "run: |\n          powershell -NoProfile -ExecutionPolicy Bypass -File build/build.ps1 -Analyze\n          exit 0",
        )
        self.assertTrue(any("must be exactly" in e for e in _validate_analyze_workflow(with_trailing_exit0)))
        without_main_push = workflow.replace("branches: [main]", "branches: [other]", 1)
        self.assertTrue(any("main branch" in e for e in _validate_analyze_workflow(without_main_push)))
        with_branches_ignore = workflow.replace("branches: [main]", "branches-ignore:\n      - main", 1)
        self.assertTrue(any("main branch" in e for e in _validate_analyze_workflow(with_branches_ignore)))
        with_negated_main = workflow.replace("branches: [main]", "branches: [main, '!main']", 1)
        self.assertTrue(any("main branch" in e for e in _validate_analyze_workflow(with_negated_main)))
        with_quoted_comma_branch = workflow.replace("branches: [main]", "branches: ['release,main,stable']", 1)
        self.assertTrue(any("main branch" in e for e in _validate_analyze_workflow(with_quoted_comma_branch)))
        with_extra_branch = workflow.replace("branches: [main]", "branches: [main, release]", 1)
        self.assertTrue(any("main branch" in e for e in _validate_analyze_workflow(with_extra_branch)))
        with_block_list_main = workflow.replace("branches: [main]", "branches:\n      - main", 1)
        self.assertEqual(_validate_analyze_workflow(with_block_list_main), [])
        with_aligned_block_list_main = workflow.replace("branches: [main]", "branches:\n    - main", 1)
        self.assertEqual(_validate_analyze_workflow(with_aligned_block_list_main), [])
        # Drop inputs.sha only inside the analyze job.
        idx = workflow.index("  analyze:")
        mutated_sha = workflow[:idx] + workflow[idx:].replace("ref: ${{ inputs.sha || github.sha }}", "ref: ${{ github.sha }}", 1)
        self.assertTrue(any("inputs.sha" in e for e in _validate_analyze_workflow(mutated_sha)))


if __name__ == "__main__":
    unittest.main()
