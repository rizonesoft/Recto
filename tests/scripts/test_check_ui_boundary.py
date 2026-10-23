"""scripts/check-ui-boundary.py: the D13 boundary proven from compiler and librarian records (D10 T01 §1).

The planted cases compile real translation units with the real cl, lib and dumpbin, in a throwaway tree laid out like
the repository, so each proves the check reads what the tools did, not how the source spells it: an escaped
`#pragma comment(lib)` name, a quoted forced include, and a library merged into another by the librarian are all
caught, and a source the compiler left no report for is refused."""
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
        for sub in ("src/ui", "src/core", "obj/core", "obj/ui"):
            (self.root / sub).mkdir(parents=True)
        (self.root / "src/ui/x.h").write_text("int ui_thing();\n", encoding="utf-8")
        (self.root / "src/ui/u.cpp").write_text("int ui_thing() { return 1; }\n", encoding="utf-8")

    def compile(self, source, out_dir, *options):
        out = subprocess.run([str(self.tools / "cl.exe"), "/nologo", "/c", "/sourceDependencies", f"{out_dir}\\",
                              *options, str(source), f"/Fo{out_dir}\\"], capture_output=True, text=True,
                             cwd=self.root)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)

    def library(self, out, *inputs):
        result = subprocess.run([str(self.tools / "lib.exe"), "/nologo", f"/out:{out}", *map(str, inputs)],
                                capture_output=True, text=True, cwd=self.root)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return out

    def test_an_escaped_directive_and_a_quoted_forced_include_are_caught(self):
        # Neither crossing is spelled `#include "../ui/x.h"` or "recto-ui.lib" anywhere in the source or options text.
        source = self.root / "src/core/a.cpp"
        source.write_text('#pragma comment(lib, "recto-\\165i.lib")\nint a() { return ui_thing(); }\n', encoding="utf-8")
        self.compile(source, self.root / "obj/core", f'/FI{self.root / "src" / "ui" / "x.h"}')
        ui = [self.root / "src/ui"]
        found = boundary.include_crossings("recto-core", {source}, self.root / "obj/core", ui)
        self.assertEqual(found, [f"recto-core: {boundary.normalized(source)} opened "
                                 f"{boundary.normalized(self.root / 'src/ui/x.h')}"])
        library = self.library(self.root / "obj/core/recto-core.lib", self.root / "obj/core/a.obj")
        found = boundary.directive_crossings("recto-core", library, ["recto-ui"], self.tools / "dumpbin.exe")
        self.assertEqual(found, [f"recto-core: {boundary.normalized(library)} carries the linker directive "
                                 "/DEFAULTLIB:recto-ui.lib"])

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
                self.compile(source, self.root / "obj/core", "/std:c++latest")
                library = self.library(self.root / "obj/core" / f"{shape}.lib", self.root / "obj/core" / f"{shape}.obj")
                found = boundary.directive_crossings("recto-core", library, ["recto-ui"], self.tools / "dumpbin.exe")
                self.assertEqual(len(found), 1, found)
                self.assertIn("/DEFAULTLIB:", found[0].upper())

    def test_a_clean_source_is_not_a_crossing(self):
        source = self.root / "src/core/b.cpp"
        source.write_text('#pragma comment(lib, "ole32.lib")\nint b() { return 2; }\n', encoding="utf-8")
        self.compile(source, self.root / "obj/core")
        self.assertEqual(boundary.include_crossings("recto-core", {source}, self.root / "obj/core",
                                                    [self.root / "src/ui"]), [])
        library = self.library(self.root / "obj/core/recto-core.lib", self.root / "obj/core/b.obj")
        self.assertEqual(boundary.directive_crossings("recto-core", library, ["recto-ui"], self.tools / "dumpbin.exe"), [])
        self.assertEqual(boundary.member_crossings("recto-core", library, self.root, [self.root / "obj/ui"],
                                                   self.tools / "lib.exe"), [])

    def test_a_toolkit_library_merged_by_the_librarian_is_caught(self):
        # Lib.AdditionalDependencies=recto-ui.lib: lib copies the toolkit's members into the engine library.
        self.compile(self.root / "src/ui/u.cpp", self.root / "obj/ui")
        toolkit = self.library(self.root / "obj/ui/recto-ui.lib", self.root / "obj/ui/u.obj")
        source = self.root / "src/core/b.cpp"
        source.write_text("int b() { return 2; }\n", encoding="utf-8")
        self.compile(source, self.root / "obj/core")
        library = self.library(self.root / "obj/core/recto-core.lib", self.root / "obj/core/b.obj", toolkit)
        found = boundary.member_crossings("recto-core", library, self.root, [self.root / "obj/ui"], self.tools / "lib.exe")
        self.assertEqual(len(found), 1, found)
        self.assertIn("u.obj", found[0])

    def test_a_source_without_a_compiler_report_is_refused(self):
        source = self.root / "src/core/c.cpp"
        source.write_text("int c() { return 3; }\n", encoding="utf-8")
        with self.assertRaisesRegex(boundary.EvidenceError, "no /sourceDependencies report"):
            boundary.include_crossings("recto-core", {source}, self.root / "obj/core", [self.root / "src/ui"])

    def test_header_units_and_modules_in_a_report_count(self):
        # A report's imported modules and header units are files the translation unit used too.
        source = self.root / "src/core/d.cpp"
        (self.root / "obj/core/d.cpp.json").write_text(json.dumps({"Version": "1.2", "Data": {
            "Source": str(source), "Includes": [], "ImportedModules": [],
            "ImportedHeaderUnits": [{"Header": str(self.root / "src/ui/x.h"), "BMI": str(self.root / "obj/x.h.ifc")}]}}),
            encoding="utf-8")
        found = boundary.include_crossings("recto-core", {source}, self.root / "obj/core", [self.root / "src/ui"])
        self.assertEqual(len(found), 1, found)


if __name__ == "__main__":
    unittest.main()
