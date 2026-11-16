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
        return subprocess.run(
            [str(CMAKE), "-DRECTO_RESERVE_MB=2048", "-DRECTO_COMPILER_MB=1536", "-DRECTO_LINK_MB=1024",
             f"-DRECTO_POOL_COMPILE={compiles}", f"-DRECTO_POOL_LINK={links}", f"-DRECTO_ASSUME_FREE_MB={free}",
             "-P", str(ROOT / "cmake" / "memory-gate.cmake")],
            capture_output=True, text=True, timeout=60, creationflags=0x08000000)

    @staticmethod
    def said(res: subprocess.CompletedProcess[str]) -> str:
        return " ".join(res.stderr.split())  # CMake wraps its messages

    def test_enough_memory_for_the_pools_passes(self):
        # 2,048 + 4 * 1,536 + 1,024 = 9,216 MB
        self.assertEqual(self.gate(9216).returncode, 0)

    def test_pools_that_no_longer_fit_are_refused_with_the_command_that_resizes_them(self):
        res = self.gate(9215)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("need 9216 MB but 9215 MB is free now", self.said(res))
        self.assertIn("build\\build.ps1", self.said(res))

    def test_below_one_compiler_and_one_link_is_refused(self):
        res = self.gate(4607, compiles=1)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("below the 4608 MB minimum", self.said(res))
        self.assertEqual(self.gate(4608, compiles=1).returncode, 0)


if __name__ == "__main__":
    unittest.main()
