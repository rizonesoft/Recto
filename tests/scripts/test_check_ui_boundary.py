"""scripts/check-ui-boundary.py: the D13 boundary proven from compiler and librarian records (D10 T01 §1).

The planted cases compile real translation units with the real cl, lib and dumpbin, in a throwaway tree laid out like
the repository, so each proves the check reads what the tools did, not how the source spells it: an escaped
`#pragma comment(lib)` name, a quoted forced include, directive spellings that defeat a text scan, a library merged in
by the librarian, an object added from a neutral folder, a renamed toolkit library named by path, a precompiled header
or module interface from the other side, a stale report left beside the current one, and a unit that reports nothing."""
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts" / "check-ui-boundary.py"


def load():
    spec = importlib.util.spec_from_file_location("check_ui_boundary", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


boundary = load()


def tools():
    try:
        return boundary.tool_folder(boundary.visual_studio())
    except boundary.EvidenceError:
        return None


class EvaluationTests(unittest.TestCase):
    """How evaluated ClCompile items become the units the proof must cover (no tools needed)."""

    def test_excluded_items_are_not_units_and_outputs_follow_the_metadata(self):
        folder = pathlib.Path(tempfile.gettempdir())
        units = boundary.compiled_units(folder, [
            {"Identity": "a.cpp", "ObjectFileName": "obj\\", "SourceDependenciesFile": "obj\\000-a.cpp.json",
             "GenerateSourceDependencies": "true"},
            {"Identity": "x64_only.cpp", "ObjectFileName": "obj\\", "SourceDependenciesFile": "obj\\",
             "GenerateSourceDependencies": "true", "ExcludedFromBuild": "true"},
            {"Identity": "sub\\b.cpp", "ObjectFileName": "obj\\b2", "SourceDependenciesFile": "obj\\",
             "GenerateSourceDependencies": "false"}])
        self.assertEqual([u.source.name for u in units], ["a.cpp", "b.cpp"])
        self.assertEqual(units[0].report, boundary.normalized(folder / "obj" / "000-a.cpp.json"))
        self.assertEqual(units[0].obj, boundary.normalized(folder / "obj" / "a.obj"))
        self.assertEqual(units[1].obj, boundary.normalized(folder / "obj" / "b2.obj"))
        self.assertEqual(units[1].report, boundary.normalized(folder / "obj" / "b.cpp.json"))
        self.assertFalse(units[1].generates)

    def test_the_build_properties_reach_the_evaluation(self):
        arguments = boundary.evaluation_arguments("msbuild", "p.vcxproj", "Release", "x64", ["EnableASAN=true"])
        self.assertIn("-p:EnableASAN=true", arguments)
        build = (REPO / "build" / "build.ps1").read_text(encoding="utf-8")
        self.assertIn('check-ui-boundary.py\') --configuration $cfg --platform $plat --vs $vs --property '
                      '"EnableASAN=$desiredAsanState"', build)


@unittest.skipUnless(sys.platform == "win32", "the C++ build tools are Windows-only (foundation plan D15)")
class CompilerReportedBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tools = tools()

    def setUp(self):
        self.assertIsNotNone(self.tools, "no Visual Studio 2026 MSVC tools were found; the boundary cannot be proven")
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = pathlib.Path(folder.name)
        for sub in ("src/ui", "src/core", "obj/core", "obj/ui", "shared"):
            (self.root / sub).mkdir(parents=True)
        (self.root / "src/ui/x.h").write_text("int ui_thing();\n", encoding="utf-8")
        (self.root / "src/ui/u.cpp").write_text("int ui_thing() { return 1; }\n", encoding="utf-8")
        self.forbidden = [self.root / "src/ui", self.root / "obj/ui"]

    def compile(self, source, out_dir, *options):
        out = subprocess.run([str(self.tools / "cl.exe"), "/nologo", "/c", "/sourceDependencies", f"{out_dir}\\",
                              *options, str(source), f"/Fo{out_dir}\\"], capture_output=True, text=True,
                             cwd=self.root)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        return boundary.Unit(boundary.normalized(source), boundary.normalized(out_dir / f"{source.stem}.obj"),
                             boundary.normalized(out_dir / f"{source.name}.json"), True)

    def library(self, out, *inputs):
        result = subprocess.run([str(self.tools / "lib.exe"), "/nologo", f"/out:{out}", *map(str, inputs)],
                                capture_output=True, text=True, cwd=self.root)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return out

    def directives(self, library, libraries=None):
        return boundary.directive_crossings("recto-core", library, libraries or [self.root / "obj/ui/recto-ui.lib"],
                                            self.forbidden, self.tools / "dumpbin.exe")

    def members(self, library, own):
        return boundary.member_crossings("recto-core", library, self.root, own, self.tools / "lib.exe")

    def test_an_escaped_directive_and_a_quoted_forced_include_are_caught(self):
        # Neither crossing is spelled `#include "../ui/x.h"` or "recto-ui.lib" anywhere in the source or options text.
        source = self.root / "src/core/a.cpp"
        source.write_text('#pragma comment(lib, "recto-\\165i.lib")\nint a() { return ui_thing(); }\n', encoding="utf-8")
        unit = self.compile(source, self.root / "obj/core", f'/FI{self.root / "src" / "ui" / "x.h"}')
        self.assertEqual(boundary.include_crossings("recto-core", [unit], self.forbidden),
                         [f"recto-core: {unit.source} used {boundary.normalized(self.root / 'src/ui/x.h')}"])
        library = self.library(self.root / "obj/core/recto-core.lib", unit.obj)
        self.assertEqual(self.directives(library), [f"recto-core: {boundary.normalized(library)} carries the linker "
                                                    "directive /DEFAULTLIB:recto-ui.lib"])

    def test_directive_spellings_that_defeat_a_text_scan_are_caught(self):
        # The text scanner's misses from review round 12: an embedded NUL, a macro for the kind, a _Pragma operand from
        # a macro, and a __pragma after a multi-line raw string. Whatever the compiler makes of each, the directive
        # it writes into the object is what the librarian reports.
        shapes = {
            "nul": '#pragma comment(lib, "recto-ui.lib\\0suffix")\n',
            "kind": '#define KIND lib\n#pragma comment(KIND, "recto-ui.lib")\n',
            "operand": '#define P "comment(lib, \\"recto-ui.lib\\")"\n_Pragma(P)\n',
            "raw": '[[maybe_unused]] const char* doc = R"(\n)"; __pragma(comment(lib, "recto-ui.lib"))\n',
        }
        for shape, text in shapes.items():
            with self.subTest(shape=shape):
                source = self.root / "src/core" / f"{shape}.cpp"
                source.write_text(text + f"int {shape}() {{ return 0; }}\n", encoding="utf-8")
                unit = self.compile(source, self.root / "obj/core", "/std:c++latest")
                library = self.library(self.root / "obj/core" / f"{shape}.lib", unit.obj)
                found = self.directives(library)
                self.assertEqual(len(found), 1, found)
                self.assertIn("/DEFAULTLIB:", found[0].upper())

    def test_a_renamed_toolkit_library_named_by_path_is_caught(self):
        # Round 13: the toolkit's TargetName is `widgets`, and the directive names its output by absolute path.
        widgets = self.root / "obj/ui/widgets.lib"
        source = self.root / "src/core/w.cpp"
        source.write_text(f'#pragma comment(lib, "{str(widgets).replace(chr(92), chr(92) * 2)}")\nint w() {{ return 0; }}\n',
                          encoding="utf-8")
        unit = self.compile(source, self.root / "obj/core")
        library = self.library(self.root / "obj/core/w.lib", unit.obj)
        self.assertEqual(len(self.directives(library, [widgets])), 1)

    def test_a_clean_source_is_not_a_crossing(self):
        source = self.root / "src/core/b.cpp"
        source.write_text('#pragma comment(lib, "ole32.lib")\nint b() { return 2; }\n', encoding="utf-8")
        unit = self.compile(source, self.root / "obj/core")
        self.assertEqual(boundary.include_crossings("recto-core", [unit], self.forbidden), [])
        library = self.library(self.root / "obj/core/recto-core.lib", unit.obj)
        self.assertEqual(self.directives(library), [])
        self.assertEqual(self.members(library, [unit.obj]), [])

    def test_a_toolkit_library_merged_by_the_librarian_is_caught(self):
        # Lib.AdditionalDependencies=recto-ui.lib: lib copies the toolkit's members into the engine library.
        toolkit_unit = self.compile(self.root / "src/ui/u.cpp", self.root / "obj/ui")
        toolkit = self.library(self.root / "obj/ui/recto-ui.lib", toolkit_unit.obj)
        source = self.root / "src/core/b.cpp"
        source.write_text("int b() { return 2; }\n", encoding="utf-8")
        unit = self.compile(source, self.root / "obj/core")
        library = self.library(self.root / "obj/core/recto-core.lib", unit.obj, toolkit)
        found = self.members(library, [unit.obj])
        self.assertEqual(len(found), 1, found)
        self.assertIn("u.obj", found[0])

    def test_an_object_added_from_a_neutral_folder_is_caught(self):
        # Round 13: a toolkit object written to build/shared and added to the engine library as a Lib item.
        shared = self.compile(self.root / "src/ui/u.cpp", self.root / "shared")
        source = self.root / "src/core/b.cpp"
        source.write_text("int b() { return 2; }\n", encoding="utf-8")
        unit = self.compile(source, self.root / "obj/core")
        library = self.library(self.root / "obj/core/recto-core.lib", unit.obj, shared.obj)
        found = self.members(library, [unit.obj])
        self.assertEqual(len(found), 1, found)
        self.assertIn("shared", found[0])

    def test_the_other_sides_precompiled_header_or_module_counts(self):
        # Round 13: a PCH or an imported module's BMI built by the toolkit is a crossing with no toolkit header opened.
        source = self.root / "src/core/d.cpp"
        report = self.root / "obj/core/d.cpp.json"
        unit = boundary.Unit(boundary.normalized(source), boundary.normalized(self.root / "obj/core/d.obj"),
                             boundary.normalized(report), True)
        for key, value in {"PCH": str(self.root / "obj/ui/pch.pch"),
                           "ImportedModules": [{"Name": "recto.ui", "BMI": str(self.root / "obj/ui/toolkit.ixx.ifc")}],
                           "ImportedHeaderUnits": [{"Header": str(self.root / "src/ui/x.h"),
                                                    "BMI": str(self.root / "obj/x.h.ifc")}]}.items():
            with self.subTest(key=key):
                report.write_text(json.dumps({"Version": "1.2", "Data": {"Source": str(source), "Includes": [],
                                                                        key: value}}), encoding="utf-8")
                self.assertEqual(len(boundary.include_crossings("recto-core", [unit], self.forbidden)), 1)

    def test_a_stale_report_elsewhere_is_never_read_in_place_of_the_current_one(self):
        # Round 13: SourceDependenciesFile moved to 000-a.cpp.json; the old a.cpp.json (clean) is still in the folder.
        source = self.root / "src/core/a.cpp"
        source.write_text('#include "../ui/x.h"\nint a() { return ui_thing(); }\n', encoding="utf-8")
        current = self.root / "obj/core/000-a.cpp.json"
        subprocess.run([str(self.tools / "cl.exe"), "/nologo", "/c", f"/sourceDependencies{current}", str(source),
                        f"/Fo{self.root / 'obj/core'}\\"], capture_output=True, text=True, cwd=self.root, check=True)
        (self.root / "obj/core/a.cpp.json").write_text(json.dumps({"Version": "1.2", "Data": {
            "Source": str(source), "Includes": []}}), encoding="utf-8")
        units = boundary.compiled_units(self.root, [{"Identity": "src\\core\\a.cpp", "ObjectFileName": "obj\\core\\",
                                                     "SourceDependenciesFile": "obj\\core\\000-a.cpp.json",
                                                     "GenerateSourceDependencies": "true"}])
        self.assertEqual(len(boundary.include_crossings("recto-core", units, self.forbidden)), 1)

    def test_a_unit_that_reports_nothing_is_refused(self):
        source = self.root / "src/core/c.cpp"
        unit = boundary.Unit(boundary.normalized(source), boundary.normalized(self.root / "obj/core/c.obj"),
                             boundary.normalized(self.root / "obj/core/c.cpp.json"), True)
        with self.assertRaisesRegex(boundary.EvidenceError, "no /sourceDependencies report"):
            boundary.include_crossings("recto-core", [unit], self.forbidden)
        silent = boundary.Unit(unit.source, unit.obj, unit.report, False)
        with self.assertRaisesRegex(boundary.EvidenceError, "without GenerateSourceDependencies"):
            boundary.include_crossings("recto-core", [silent], self.forbidden)


if __name__ == "__main__":
    unittest.main()
