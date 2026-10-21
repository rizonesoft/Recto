"""Tests for the PDF engine dependency policy (D00 T01 §2, decisions D3 and D9): Recto builds its own recto-pdf engine
in C++20 with warnings as errors, vendors no engine as a submodule or third_party tree, and names no MuPDF anywhere a
user or the build would see it."""
import pathlib
import re
import subprocess
import unittest
import xml.etree.ElementTree as ElementTree

REPO = pathlib.Path(__file__).resolve().parents[2]
# mupdf and artifex anywhere (libmupdf.lib, mupdf.h); jbig2dec as a whole word only, because the PDF filter name
# JBIG2Decode, which recto-pdf implements itself, contains it.
MUPDF = re.compile(r"mupdf|artifex|\bjbig2dec\b", re.IGNORECASE)
MSBUILD = "{http://schemas.microsoft.com/developer/msbuild/2003}"


def tracked(*pathspecs):
    out = subprocess.run(["git", "-C", str(REPO), "ls-files", "-z", "--", *pathspecs], capture_output=True,
                         text=True, encoding="utf-8", check=True).stdout
    return [name for name in out.split("\0") if name]


class EngineDependencyPolicyTest(unittest.TestCase):
    def test_no_submodule_or_vendored_engine(self):
        index = subprocess.run(["git", "-C", str(REPO), "ls-files", "--stage"], capture_output=True, text=True,
                               encoding="utf-8", check=True).stdout
        gitlinks = [line for line in index.splitlines() if line.startswith("160000 ")]
        self.assertEqual(gitlinks, [], "a submodule gitlink is in the index")
        self.assertFalse((REPO / ".gitmodules").exists())
        self.assertEqual(tracked("third_party"), [])

    def test_build_files_name_no_mupdf(self):
        for name in tracked("*.vcxproj", "*.props", "*.targets", "*.sln", "vcpkg.json", "*.cpp", "*.h", "*.cmd",
                            "*.ps1", ".github/workflows/*.yml"):
            with self.subTest(file=name):
                self.assertIsNone(MUPDF.search((REPO / name).read_text(encoding="utf-8", errors="replace")))

    def test_every_module_compiles_as_cpp20_with_warnings_as_errors(self):
        # The flags are ClCompile item metadata in Directory.Build.targets (see Directory.Build.props). The XML is
        # parsed, so a commented-out setting does not count; the plain-language check catches settings put elsewhere.
        root = ElementTree.parse(REPO / "Directory.Build.targets").getroot()
        settings = {}
        for compile_item in root.iter():
            if compile_item.tag.replace(MSBUILD, "") != "ClCompile":
                continue
            for child in compile_item:
                settings.setdefault(child.tag.replace(MSBUILD, ""), []).append((child.text or "").strip())
        self.assertIn("stdcpp20", settings.get("LanguageStandard", []))
        self.assertIn("Level4", settings.get("WarningLevel", []))
        self.assertIn("true", settings.get("TreatWarningAsError", []))
        for name, values in settings.items():
            if name in ("LanguageStandard", "WarningLevel", "TreatWarningAsError"):
                self.assertEqual(len(set(values)), 1, f"{name} is set to more than one value: {values}")

if __name__ == "__main__":
    unittest.main()
