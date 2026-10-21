"""Tests for the PDF engine dependency policy (D00 T01 §2, decisions D3 and D9): Recto builds its own recto-pdf engine
in C++20 with warnings as errors, vendors no engine as a submodule or third_party tree, and names no MuPDF anywhere a
user or the build would see it."""
import pathlib
import re
import subprocess
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
# Whole words: the PDF filter name JBIG2Decode, which recto-pdf implements itself, is not the jbig2dec library.
MUPDF = re.compile(r"\b(mupdf|artifex|jbig2dec)\b", re.IGNORECASE)


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
        # The flags are ClCompile item metadata, so they live in Directory.Build.targets (see Directory.Build.props).
        targets = (REPO / "Directory.Build.targets").read_text(encoding="utf-8")
        self.assertRegex(targets, r"<LanguageStandard>stdcpp20</LanguageStandard>")
        self.assertRegex(targets, r"<WarningLevel>Level4</WarningLevel>")
        self.assertRegex(targets, r"<TreatWarningAsError>true</TreatWarningAsError>")


if __name__ == "__main__":
    unittest.main()
