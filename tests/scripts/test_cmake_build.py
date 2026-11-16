"""Checks on the CMake and Ninja build (D00 T04 section 8) that need no compiler.

- Every project's CMakeLists.txt compiles the same sources as its .vcxproj: both builds list sources explicitly until
  the MSBuild path is retired, so a file added to one and not the other is caught here, not in a parity run.
- CMakePresets.json names the Ninja and CMake that build/tools.lock.json pins, so a pin bump cannot leave the presets
  on an old folder.
- cmake/memory-gate.cmake, which runs before every build, refuses below one compiler and one link above the reserve
  and refuses when the configured pools no longer fit, and lets a build through otherwise.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
LOCK = json.loads((ROOT / "build" / "tools.lock.json").read_text(encoding="utf-8"))
CMAKE = ROOT / ".tools" / "cmake" / LOCK["cmake"]["version"] / LOCK["cmake"]["executable"]
PROJECTS = {
    "recto-pdf": ROOT / "src" / "pdf" / "recto-pdf.vcxproj",
    "recto-core": ROOT / "src" / "core" / "recto-core.vcxproj",
    "recto-services": ROOT / "src" / "services" / "recto-services.vcxproj",
    "recto-ui": ROOT / "src" / "ui" / "recto-ui.vcxproj",
    "Recto": ROOT / "src" / "app" / "Recto.vcxproj",
    "recto-cli": ROOT / "src" / "cli" / "recto-cli.vcxproj",
    "recto-tests": ROOT / "tests" / "recto-tests" / "recto-tests.vcxproj",
    "recto-fuzz": ROOT / "fuzz" / "recto-fuzz.vcxproj",
}


def vcxproj_sources(project: Path) -> set[str]:
    """The .cpp files a project compiles, its wildcards expanded as MSBuild expands them (in the project's folder)."""
    tree = ET.parse(project)
    found: set[str] = set()
    for element in tree.iter():
        raw = element.get("Include")
        if element.tag.rsplit("}", 1)[-1] != "ClCompile" or not raw:
            continue
        include = raw.replace("\\", "/")
        if any(ch in include for ch in "*?"):
            found.update(p.name for p in project.parent.glob(include))
        else:
            found.add(include)
    return found


def cmake_sources(target: str, cmakelists: Path) -> set[str]:
    """The .cpp files listed in the add_library or add_executable block for `target`, plus what its globs match."""
    text = cmakelists.read_text(encoding="utf-8")
    block = re.search(r"add_(?:library|executable)\(\s*" + re.escape(target) + r"\b(.*?)\)", text, re.S)
    if block is None:
        raise AssertionError(f"{cmakelists} declares no target {target}")
    found = {tok for tok in block.group(1).split() if tok.endswith(".cpp")}
    for var, pattern in re.findall(r"file\(GLOB\s+(\w+)\s+CONFIGURE_DEPENDS\s+\"\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^\"]+)\"\)", text):
        if "${" + var + "}" in block.group(1):
            found.update(p.name for p in cmakelists.parent.glob(pattern))
    return found


class SourceParity(unittest.TestCase):
    def test_every_cmake_target_compiles_the_sources_its_vcxproj_does(self):
        for target, project in PROJECTS.items():
            with self.subTest(target=target):
                self.assertEqual(cmake_sources(target, project.parent / "CMakeLists.txt"), vcxproj_sources(project))

    def test_a_source_missing_from_cmake_is_caught(self):
        # The comparison is not vacuous: a list without one file differs.
        sources = vcxproj_sources(PROJECTS["recto-pdf"])
        self.assertIn("document.cpp", sources)
        self.assertNotEqual(sources - {"document.cpp"}, sources)


class PresetPins(unittest.TestCase):
    def test_presets_name_the_pinned_ninja_and_cmake(self):
        presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        base = next(p for p in presets["configurePresets"] if p["name"] == "base")
        ninja = f"${{sourceDir}}/.tools/ninja/{LOCK['ninja']['version']}/{LOCK['ninja']['executable']}"
        cmake = f"${{sourceDir}}/.tools/cmake/{LOCK['cmake']['version']}/{LOCK['cmake']['executable']}"
        self.assertEqual(base["cacheVariables"]["CMAKE_MAKE_PROGRAM"], ninja)
        self.assertEqual(base["cmakeExecutable"], cmake)

    def test_a_tool_pin_bump_moves_the_preset_paths_in_the_same_change(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location("check_tool_pins", ROOT / "scripts" / "check-tool-pins.py")
        tools = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(tools)
        text = (ROOT / "CMakePresets.json").read_text(encoding="utf-8")
        self.assertEqual(tools.rewrite_presets(text, LOCK), text, "the presets are not in the layout a rewrite keeps")
        bumped = json.loads(json.dumps(LOCK))
        bumped["ninja"]["version"] = "1.99.0"
        bumped["cmake"]["version"] = "4.9.0"
        bumped["cmake"]["executable"] = "cmake-4.9.0-windows-x86_64/bin/cmake.exe"
        out = json.loads(tools.rewrite_presets(text, bumped))
        base = next(p for p in out["configurePresets"] if p["name"] == "base")
        self.assertEqual(base["cacheVariables"]["CMAKE_MAKE_PROGRAM"], "${sourceDir}/.tools/ninja/1.99.0/ninja.exe")
        self.assertEqual(base["cmakeExecutable"], "${sourceDir}/.tools/cmake/4.9.0/cmake-4.9.0-windows-x86_64/bin/cmake.exe")
        self.assertEqual(out["buildPresets"], json.loads(text)["buildPresets"])


@unittest.skipUnless(os.name == "nt" and CMAKE.is_file(), "needs the pinned CMake (build/bootstrap-tools.ps1)")
class MemoryGate(unittest.TestCase):
    def gate(self, free: int, compiles: int = 4, links: int = 1) -> subprocess.CompletedProcess[str]:
        env = {**os.environ, "RECTO_ASSUME_FREE_MB": str(free)}
        return subprocess.run(
            [str(CMAKE), "-DRECTO_RESERVE_MB=2048", "-DRECTO_COMPILER_MB=1536", "-DRECTO_LINK_MB=1024",
             f"-DRECTO_POOL_COMPILE={compiles}", f"-DRECTO_POOL_LINK={links}",
             "-P", str(ROOT / "cmake" / "memory-gate.cmake")],
            capture_output=True, text=True, timeout=60, env=env, creationflags=0x08000000)

    @staticmethod
    def said(res: subprocess.CompletedProcess[str]) -> str:
        return " ".join(res.stderr.split())  # CMake wraps its messages

    def test_enough_memory_for_the_pools_passes(self):
        # 2,048 + 4 * 1,536 + 1,024 = 9,216 MB; up to one compiler's 1,536 MB below it is ordinary movement.
        self.assertEqual(self.gate(9216).returncode, 0)
        self.assertEqual(self.gate(7680).returncode, 0)

    def test_pools_that_no_longer_fit_are_refused_with_the_command_that_resizes_them(self):
        res = self.gate(7679)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("need 9216 MB but 7679 MB is free now", self.said(res))
        self.assertIn("build\\build.ps1", self.said(res))

    def test_below_one_compiler_and_one_link_is_refused(self):
        res = self.gate(4607, compiles=1)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("below the 4608 MB minimum", self.said(res))
        self.assertEqual(self.gate(4608, compiles=1).returncode, 0)


POWERSHELL = __import__("shutil").which("powershell") or __import__("shutil").which("pwsh") or ""


@unittest.skipUnless(os.name == "nt" and bool(POWERSHELL), "needs Windows PowerShell")
class CMakePools(unittest.TestCase):
    """build/cmake-build.ps1's Resolve-RectoCMakePools: one budget over compiles and links (section 8)."""

    def pools(self, args: str) -> subprocess.CompletedProcess[str]:
        script = (f". '{ROOT / 'build' / 'parallelism.ps1'}'; . '{ROOT / 'build' / 'cmake-build.ps1'}'; "
                  f"Resolve-RectoCMakePools {args} | ConvertTo-Json -Compress")
        return subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                              capture_output=True, text=True, timeout=60, creationflags=0x08000000)

    def sized(self, args: str) -> dict:
        res = self.pools(args)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        return json.loads(res.stdout.strip())

    def test_compiles_and_links_share_one_budget(self):
        cases = [
            # free, cores -> (compiles, links): 2,048 reserve, 1,536 per compile, 1,024 per link
            (4608, 12, 1, 1),     # the minimum: one of each
            (6400, 12, 2, 1),     # (6400 - 2048 - 1024) / 1536 = 2; two links need 2*1024 + 2*1536 = 5,120 of 4,352
            (7168, 12, 2, 2),     # budget 5,120 holds two links and two compilers: (5120 - 2 * 1024) / 1536 = 2
            (25000, 12, 12, 2),   # capped by the cores
            (25000, 4, 4, 2),
        ]
        for free, cores, compiles, links in cases:
            with self.subTest(free=free, cores=cores):
                got = self.sized(f"-Cores {cores} -JobsEnv '' -MaxCpuCount 0 -AvailableMb {free}")
                self.assertEqual((got["Compiles"], got["Links"]), (compiles, links))
                budget = free - 2048
                self.assertLessEqual(got["Compiles"] * 1536 + got["Links"] * 1024, max(budget, 1536 + 1024))

    def test_below_one_compiler_and_one_link_is_refused(self):
        res = self.pools("-Cores 12 -JobsEnv '' -MaxCpuCount 0 -AvailableMb 4607")
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("4,607 MB is below the 4,608 MB minimum", " ".join((res.stdout + res.stderr).split()))

    def test_the_overrides_set_the_compile_pool_and_are_validated(self):
        self.assertEqual(self.sized("-Cores 12 -JobsEnv '3' -MaxCpuCount 0 -AvailableMb 25000")["Compiles"], 3)
        self.assertEqual(self.sized("-Cores 12 -JobsEnv '' -MaxCpuCount 5 -AvailableMb 25000")["Compiles"], 5)
        for args in ("-Cores 12 -JobsEnv '13' -MaxCpuCount 0 -AvailableMb 25000",
                     "-Cores 12 -JobsEnv 'x' -MaxCpuCount 0 -AvailableMb 25000",
                     "-Cores 12 -JobsEnv '' -MaxCpuCount 13 -AvailableMb 25000"):
            with self.subTest(args=args):
                self.assertNotEqual(self.pools(args).returncode, 0)

def _visual_studio() -> Path | None:
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        return None
    out = subprocess.run([str(vswhere), "-version", "[18.0,19.0)", "-latest", "-property", "installationPath"],
                         capture_output=True, text=True, timeout=60, creationflags=0x08000000).stdout.strip()
    return Path(out) if out else None


VS = _visual_studio() if os.name == "nt" else None


@unittest.skipUnless(VS is not None and CMAKE.is_file(), "needs Visual Studio 2026 and the pinned CMake")
class DirectPresets(unittest.TestCase):
    """A preset run straight from a developer prompt, as Visual Studio's Open Folder runs it, with no build.ps1: it sizes
    its own pools, refuses on low memory (at configure and again when a build starts), refuses a preset for another
    architecture than the prompt's, and refuses a CMake other than the pinned one."""

    def run_in_prompt(self, arch: str, args: list[str], free: int | None = None, cmake: Path = CMAKE,
                      timeout: float = 600) -> subprocess.CompletedProcess[str]:
        vcvars = VS / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
        quoted = " ".join(f'"{a}"' for a in args)
        env = {**os.environ}
        env.pop("RECTO_BUILD_JOBS", None)
        if free is not None:
            env["RECTO_ASSUME_FREE_MB"] = str(free)
        # A batch file, not a /c string: Python's argument quoting and cmd's quote rules do not mix.
        script = self.binary.parent / f"{self.binary.name}-{arch}.cmd"
        script.write_text(f'@echo off\r\ncall "{vcvars}" {arch} 10.0.26100.0 >nul 2>&1 || exit /b 97\r\n'
                          f'cd /d "{ROOT}"\r\n"{cmake}" {quoted}\r\n', encoding="ascii")
        try:
            res = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT, capture_output=True, text=True,
                                 timeout=timeout, env=env, creationflags=0x08000000)
        finally:
            script.unlink(missing_ok=True)
        res.said = " ".join((res.stdout + res.stderr).split())  # type: ignore[attr-defined]
        return res

    def setUp(self):
        import tempfile
        self._tmp = tempfile.TemporaryDirectory(prefix="recto-preset-")
        self.binary = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_configure_sizes_its_own_pools_and_refuses_below_the_minimum(self):
        refused = self.run_in_prompt("x64", ["--preset", "x64-debug", "-B", str(self.binary)], free=4607)
        self.assertNotEqual(refused.returncode, 0)
        self.assertIn("4607 MB free is below the 4608 MB minimum", refused.said)
        sized = self.run_in_prompt("x64", ["--preset", "x64-debug", "-B", str(self.binary)], free=9000)
        self.assertEqual(sized.returncode, 0, sized.said[-2000:])
        cores = os.cpu_count() or 1
        compiles = min(cores, (9000 - 2048 - 2 * 1024) // 1536)  # 6,952 MB holds two links and two compilers
        self.assertIn(f"Recto pools: free 9000 MB, {cores} cores: compile={compiles}, link=2", sized.said)
        # The memory taken since configure is seen when the build starts, with no reconfigure.
        gated = self.run_in_prompt("x64", ["--build", str(self.binary), "--target", "recto-memory-gate"], free=4000)
        self.assertNotEqual(gated.returncode, 0)
        self.assertIn("4000 MB free is below the 4608 MB minimum", gated.said)

    def test_a_preset_for_another_architecture_than_the_prompt_is_refused(self):
        res = self.run_in_prompt("x64", ["--preset", "arm64-debug", "-B", str(self.binary)], free=9000)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("The preset builds ARM64 but this developer environment's cl.exe targets x64", res.said)

    def test_a_cmake_other_than_the_pinned_one_is_refused(self):
        bundled = VS / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe"
        if not bundled.is_file():
            self.skipTest("this Visual Studio bundles no CMake")
        res = self.run_in_prompt("x64", ["--preset", "x64-debug", "-B", str(self.binary)], free=9000, cmake=bundled)
        self.assertNotEqual(res.returncode, 0)
        # A bundled CMake new enough to read the presets reaches Recto's own check; an older one stops at the
        # presets' or cmake_minimum_required's version. Either way it never configures.
        self.assertTrue("Run build\\cmake.cmd, which runs the pinned one" in res.said
                        or "version" in res.said.lower(), res.said[-1500:])

@unittest.skipUnless(os.name == "nt" and bool(POWERSHELL), "needs Windows PowerShell")
class CMakeBuildFailures(unittest.TestCase):
    """build/cmake-build.ps1's Invoke-RectoCMakeBuild stops the run on a failed build or any warning line, so
    build.ps1 never goes on to run stale tests (section 8)."""

    def run_build(self, build_exit: int, build_output: str) -> subprocess.CompletedProcess[str]:
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            (base / "build").mkdir()
            fake = base / "fake-cmake.cmd"
            # Configure succeeds; --build prints `build_output` and exits `build_exit`.
            fake.write_text("@echo off\r\nif \"%1\"==\"--build\" goto build\r\nexit /b 0\r\n:build\r\n"
                            f"echo {build_output}\r\nexit /b {build_exit}\r\n", encoding="ascii")
            script = (f". '{ROOT / 'build' / 'cmake-build.ps1'}'; "
                      "$pools = [pscustomobject]@{ Compiles = 1; Links = 1 }; "
                      f"Invoke-RectoCMakeBuild -Root '{base}' -CMake '{fake}' -Preset x64-debug -Pools $pools -Sccache $true; "
                      "'BUILD RETURNED'")
            return subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                                  capture_output=True, text=True, timeout=60, creationflags=0x08000000)

    def test_a_clean_build_returns(self):
        res = self.run_build(0, "[86/86] Linking CXX executable recto-tests.exe")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("BUILD RETURNED", res.stdout)

    def test_a_failed_compile_stops_before_anything_else_runs(self):
        res = self.run_build(1, "FAILED: src/pdf/CMakeFiles/recto-pdf.dir/document.cpp.obj")
        self.assertNotEqual(res.returncode, 0)
        self.assertNotIn("BUILD RETURNED", res.stdout)
        self.assertIn("build failed: x64-debug (exit 1)", res.stdout + res.stderr)

    def test_a_warning_line_fails_a_build_that_exited_zero(self):
        res = self.run_build(0, "cl : Command line warning D9025 : overriding '/external:W0' with '/external:W4'")
        self.assertNotEqual(res.returncode, 0)
        self.assertNotIn("BUILD RETURNED", res.stdout)
        self.assertIn("build produced 1 warning line(s): x64-debug", " ".join((res.stdout + res.stderr).split()))

@unittest.skipUnless(os.name == "nt" and bool(POWERSHELL), "needs Windows PowerShell")
class CompileDatabaseScope(unittest.TestCase):
    """Only a full x64 Debug build without ASan writes build/compile_commands.json (sections 8 and 12)."""

    def test_only_the_full_x64_debug_build_writes_the_database(self):
        cases = {
            ("x64", "Debug", "$false", "''"): True,
            ("x64", "Release", "$false", "''"): False,
            ("ARM64", "Debug", "$false", "''"): False,
            ("x64", "Debug", "$true", "''"): False,
            ("x64", "Debug", "$false", "'recto-pdf'"): False,
        }
        lines = "; ".join(f"Test-RectoWritesCompileDatabase -Platform {p} -Configuration {c} -Asan {a} -Project {j}"
                          for p, c, a, j in cases)
        script = f". '{ROOT / 'build' / 'cmake-build.ps1'}'; {lines}"
        res = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                             capture_output=True, text=True, timeout=60, creationflags=0x08000000)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual([line == "True" for line in res.stdout.split()], list(cases.values()))


LIVE_DB = ROOT / "build" / "cmake" / "x64-debug" / "compile_commands.json"


@unittest.skipUnless(LIVE_DB.is_file(), "needs a CMake x64-debug build (build.ps1 -CMake -Quick)")
class LiveCompileDatabase(unittest.TestCase):
    """The CMake database names every source the projects compile, each with a complete cl line (section 8)."""

    def test_every_eligible_source_has_one_complete_compile_line(self):
        entries = json.loads(LIVE_DB.read_text(encoding="utf-8"))
        files = [os.path.normcase(str(Path(e["file"]).resolve())) for e in entries]
        self.assertEqual(len(files), len(set(files)), "a source is listed twice")
        expected = {os.path.normcase(str((project.parent / name).resolve()))
                    for project in PROJECTS.values() for name in vcxproj_sources(project)}
        self.assertEqual(set(files), expected)
        for entry in entries:
            with self.subTest(file=Path(entry["file"]).name):
                command = entry["command"]
                self.assertIn("cl.exe", command)
                self.assertRegex(command, r"-std:c\+\+23")
                self.assertIn("/FIpch.h", command)
                self.assertIn("/external:I", command, "the SDK and CRT folders are not named")

if __name__ == "__main__":
    unittest.main()
