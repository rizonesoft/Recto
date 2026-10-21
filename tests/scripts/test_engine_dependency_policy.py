"""Tests for the PDF engine dependency policy (D00 T01 §2, decisions D3 and D9): Recto builds its own recto-pdf engine
in C++20 with warnings as errors, vendors no engine as a submodule or third_party tree, and names no MuPDF anywhere a
user or the build would see it."""
import json
import os
import pathlib
import re
import subprocess
import sys
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
# mupdf and artifex anywhere (libmupdf.lib, mupdf.h); jbig2dec as a whole word only, because the PDF filter name
# JBIG2Decode, which recto-pdf implements itself, contains it.
MUPDF = re.compile(r"mupdf|artifex|\bjbig2dec\b", re.IGNORECASE)
CONFIGURATIONS = [(configuration, platform) for configuration in ("Debug", "Release") for platform in ("x64", "ARM64")]
REQUIRED = {"LanguageStandard": "stdcpp20", "WarningLevel": "Level4", "TreatWarningAsError": "true"}
# The other metadata that could undermine C++20 with /W4 /WX, held to the values in use: CompileAs picks the language
# (CompileAsC is /TC), DisableSpecificWarnings silences warnings (/wd), WarningVersion caps them (/Wv), and the external
# warning level could lower warnings for headers. Absent is allowed only where it means the default.
ALLOWED_METADATA = {"CompileAs": {"Default", "CompileAsCpp"}, "DisableSpecificWarnings": {None, ""},
                    "WarningVersion": {None, ""}, "ExternalWarningLevel": {"InheritWarningLevel"}}
# The only switches AdditionalOptions may carry. An allow-list, not a pattern of bad switches: quoting, prefixes and
# spellings cannot sneak an override (/std:c++17, /W0, "/WX-") past it. A new switch is added here deliberately.
ALLOWED_OPTIONS = {"/utf-8", "/bigobj", "/await:strict"}


def msbuild():
    """MSBuild from the newest Visual Studio, found the way the build finds it (vswhere)."""
    vswhere = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not vswhere.is_file():
        return None
    found = subprocess.run([str(vswhere), "-latest", "-prerelease", "-requires", "Microsoft.Component.MSBuild",
                            "-find", r"MSBuild\**\Bin\MSBuild.exe"], capture_output=True, text=True).stdout.splitlines()
    return found[0].strip() if found else None


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

    @unittest.skipUnless(sys.platform == "win32", "the C++ build and MSBuild are Windows-only (foundation plan D15)")
    def test_every_module_compiles_as_cpp20_with_warnings_as_errors(self):
        # MSBuild itself evaluates each project, conditions included, and reports every source file's ClCompile
        # metadata at evaluation time, so a setting commented out, conditioned away or overridden per file fails, and
        # so does a switch in AdditionalOptions that would override it. Targets that rewrite flags during a build are
        # outside what evaluation shows; the build itself enforces the result (/WX fails it on any warning).
        tool = msbuild()
        self.assertIsNotNone(tool, "MSBuild was not found through vswhere; the policy cannot be checked")
        projects = tracked("*.vcxproj")
        self.assertTrue(projects)
        for project in projects:
            for configuration, platform in CONFIGURATIONS:
                with self.subTest(project=project, configuration=configuration, platform=platform):
                    out = subprocess.run([tool, str(REPO / project), f"-p:Configuration={configuration}",
                                          f"-p:Platform={platform}", "-getItem:ClCompile", "-nologo"],
                                         capture_output=True, text=True, encoding="utf-8", errors="replace")
                    self.assertEqual(out.returncode, 0, out.stdout[-500:] + out.stderr[-500:])
                    items = json.loads(out.stdout)["Items"].get("ClCompile", [])
                    self.assertTrue(items, f"{project} has no ClCompile items")
                    for item in items:
                        for name, value in REQUIRED.items():
                            self.assertEqual(item.get(name), value, f"{item.get('Identity')}: {name}")
                        for name, allowed in ALLOWED_METADATA.items():
                            self.assertIn(item.get(name), allowed, f"{item.get('Identity')}: {name}")
                        options = {token.strip('"').lower() for token in item.get("AdditionalOptions", "").split()}
                        self.assertLessEqual(options, ALLOWED_OPTIONS,
                                             f"{item.get('Identity')}: AdditionalOptions has unlisted switches")

if __name__ == "__main__":
    unittest.main()
