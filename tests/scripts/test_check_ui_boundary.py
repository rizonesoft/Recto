"""scripts/check-ui-boundary.py: the D13 boundary proven from compiler and librarian records (D10 T01 §1).

The planted cases compile real translation units with the real cl, lib and dumpbin, in a throwaway tree laid out like
the repository, so each proves the check reads what the tools did, not how the source spells it: an escaped
`#pragma comment(lib)` name, a quoted forced include, directive spellings that defeat a text scan, a library merged in
by the librarian, an object added from a neutral folder, a renamed toolkit library named by path, a precompiled header
or module interface from the other side, a stale report left beside the current one, and a unit that reports nothing."""
import importlib.util
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
import unittest.mock

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
             "GenerateSourceDependencies": "false"},
            {"Identity": "c.cpp", "ObjectFileName": "obj\\", "SourceDependenciesFile": "obj\\deps",
             "GenerateSourceDependencies": "true"}])
        self.assertEqual([u.source.name for u in units], ["a.cpp", "b.cpp", "c.cpp"])
        # /sourceDependencies writes a named file exactly as named (round 14); /Fo completes a bare name with .obj.
        self.assertEqual(units[2].report, boundary.normalized(folder / "obj" / "deps"))
        self.assertEqual(units[0].report, boundary.normalized(folder / "obj" / "000-a.cpp.json"))
        self.assertEqual(units[0].obj, boundary.normalized(folder / "obj" / "a.obj"))
        self.assertEqual(units[1].obj, boundary.normalized(folder / "obj" / "b2.obj"))
        self.assertEqual(units[1].report, boundary.normalized(folder / "obj" / "b.cpp.json"))
        self.assertFalse(units[1].generates)

    def test_an_existing_report_folder_needs_no_trailing_separator(self):
        # Round 15: SourceDependenciesFile naming an existing folder without its final backslash is that folder.
        with tempfile.TemporaryDirectory() as folder:
            (pathlib.Path(folder) / "obj").mkdir()
            units = boundary.compiled_units(folder, [{"Identity": "a.cpp", "ObjectFileName": "obj\\",
                                                      "SourceDependenciesFile": "obj", "GenerateSourceDependencies": "true"}])
            self.assertEqual(units[0].report, boundary.normalized(pathlib.Path(folder) / "obj" / "a.cpp.json"))

    def test_a_manifest_is_read_as_written_and_a_missing_one_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            manifest = root / "recto-core.txt"
            with self.assertRaisesRegex(boundary.EvidenceError, "no build manifest"):
                boundary.read_manifest("recto-core", manifest)
            manifest.write_text(f"project|{root / 'recto-core.vcxproj'}|obj\\|bin\\recto-core.lib\n"
                                f"unit|{root / 'core.cpp'}||true|obj\\current-core.cpp.json|obj\\|Create|obj\\core.pch|/utf-8 /DX=a|b\n",
                                encoding="utf-8")
            project = boundary.read_manifest("recto-core", manifest)
            self.assertEqual([u.report.name for u in project.units], ["current-core.cpp.json"])
            self.assertEqual(project.produced, {boundary.normalized(root / "obj" / "core.pch")})
            self.assertEqual(project.library, boundary.normalized(root / "bin" / "recto-core.lib"))
            # AdditionalOptions is the last field and keeps a "|" of its own.
            self.assertEqual(project.units[0].options, "/utf-8 /DX=a|b")

    def test_options_that_could_move_or_hide_a_report_are_refused(self):
        # Round 15: a later /sourceDependencies- sends the fresh report to stdout and leaves an older one in place.
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder) / "a.cpp"
            report = pathlib.Path(folder) / "a.cpp.json"
            report.write_text(json.dumps({"Version": "1.2", "Data": {"Source": str(source), "Includes": []}}),
                              encoding="utf-8")
            for options in ("/utf-8 /sourceDependencies-", '-SourceDependencies "x.json"', "/utf-8 @more.rsp"):
                with self.subTest(options=options):
                    unit = boundary.Unit(boundary.normalized(source), boundary.normalized(pathlib.Path(folder) / "a.obj"),
                                         boundary.normalized(report), True, None, options)
                    with self.assertRaisesRegex(boundary.EvidenceError, "may not be where the build recorded it"):
                        boundary.include_crossings("recto-core", [unit], [])
            clean = boundary.Unit(boundary.normalized(source), boundary.normalized(pathlib.Path(folder) / "a.obj"),
                                  boundary.normalized(report), True, None, "/utf-8 /DSOURCE_DEPENDENCIES=1")
            self.assertEqual(boundary.include_crossings("recto-core", [clean], []), [])

    def test_compiler_environment_switches_are_read_as_arguments(self):
        # Round 16: CL and _CL_ get the AdditionalOptions argument rules, so a quoted spelling and a response file
        # are refused, not missed by a substring search; an unrelated switch is not.
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder) / "a.cpp"
            report = pathlib.Path(folder) / "a.cpp.json"
            report.write_text(json.dumps({"Version": "1.2", "Data": {"Source": str(source), "Includes": []}}),
                              encoding="utf-8")
            unit = boundary.Unit(boundary.normalized(source), boundary.normalized(pathlib.Path(folder) / "a.obj"),
                                 boundary.normalized(report), True, None, "/utf-8")
            cases = [("_CL_", '/source"Dependencies"- /FI"x.h"'), ("CL", "/utf-8 @local-cl.rsp"),
                     ("_CL_", "@R:\\build\\local-cl.rsp"), ("CL", '"-sourceDependencies" x.json')]
            for variable, value in cases:
                with self.subTest(variable=variable, value=value), \
                        unittest.mock.patch.dict(os.environ, {"CL": "", "_CL_": "", variable: value}):
                    with self.assertRaisesRegex(boundary.EvidenceError, f"the {variable} environment variable"):
                        boundary.include_crossings("recto-core", [unit], [])
            with unittest.mock.patch.dict(os.environ, {"CL": "/DSOURCE_DEPENDENCIES=1", "_CL_": "/W4"}):
                self.assertEqual(boundary.include_crossings("recto-core", [unit], []), [])

    def test_build_ps1_checks_each_configuration_from_its_own_manifests(self):
        build = (REPO / "build" / "build.ps1").read_text(encoding="utf-8")
        self.assertIn('$boundaryManifests = Join-Path $root "build\\boundary\\$plat\\$cfg"', build)
        self.assertIn("if (Test-Path $boundaryManifests) { Remove-Item $boundaryManifests -Recurse -Force }", build)
        self.assertLess(build.index("Remove-Item $boundaryManifests"), build.index("& $msbuild @msbuildArgs"))
        self.assertIn("check-ui-boundary.py') --configuration $cfg --platform $plat --vs $vs", build)

    def test_win32_names_drop_trailing_dots_and_spaces(self):
        self.assertIn(boundary.win32_name("recto-ui.lib. "),
                      boundary.library_names([pathlib.Path(tempfile.gettempdir()) / "recto-ui.lib"]))
        self.assertIn("recto-ui", boundary.library_names([pathlib.Path(tempfile.gettempdir()) / "recto-ui.lib"]))

    def test_a_directive_line_keeps_a_name_with_a_space_whole(self):
        # dumpbin prints cl's /DEFAULTLIB:"ui kit.lib" without quotes; Clang's quoted form may appear as written.
        self.assertEqual(boundary.directive_tokens("   /DEFAULTLIB:ui kit.lib"), ["/DEFAULTLIB:ui kit.lib"])
        self.assertEqual(boundary.directive_tokens('/DEFAULTLIB:"ui kit.lib" /DEFAULTLIB:ole32.lib'),
                         ["/DEFAULTLIB:ui kit.lib", "/DEFAULTLIB:ole32.lib"])

    def test_directives_that_make_the_linker_resolve_a_symbol(self):
        self.assertEqual(boundary.directive_symbols("/EXPORT:ui_bridge=?abi_version@core@recto@@YAHXZ"),
                         {"?abi_version@core@recto@@YAHXZ"})
        self.assertEqual(boundary.directive_symbols("/EXPORT:RectoThing,DATA"), {"RectoThing"})
        self.assertEqual(boundary.directive_symbols("/INCLUDE:RectoThing"), {"RectoThing"})
        self.assertEqual(boundary.directive_symbols("/ALTERNATENAME:a=RectoThing"), {"RectoThing"})
        self.assertEqual(boundary.directive_symbols("/DEFAULTLIB:ole32.lib"), set())


@unittest.skipUnless(sys.platform == "win32", "MSBuild is Windows-only (foundation plan D15)")
class ManifestTargetTests(unittest.TestCase):
    """The RectoBoundaryManifest target in Directory.Build.targets records what ClCompile was given."""

    def test_metadata_a_target_changes_before_compiling_is_recorded(self):
        # Round 15: a BeforeTargets="ClCompile" target moves the report; the manifest names the moved report, which is
        # what the compiler would write, not the evaluated default.
        try:
            msbuild = boundary.visual_studio() / "MSBuild" / "Current" / "Bin" / "MSBuild.exe"
        except boundary.EvidenceError as error:
            self.fail(f"MSBuild is needed to prove the manifest target: {error}")
        configuration = f"PlantedManifest{os.getpid()}"
        manifests = REPO / "build" / "boundary" / "x64" / configuration
        self.addCleanup(lambda: __import__("shutil").rmtree(manifests, ignore_errors=True))
        with tempfile.TemporaryDirectory() as folder:
            project = pathlib.Path(folder) / "recto-core.vcxproj"
            project.write_text(f"""<Project DefaultTargets="Build">
  <PropertyGroup><IntDir>$(MSBuildProjectDirectory)\\obj\\</IntDir><OutDir>$(MSBuildProjectDirectory)\\bin\\</OutDir>
    <TargetName>recto-core</TargetName><TargetExt>.lib</TargetExt></PropertyGroup>
  <ItemDefinitionGroup><ClCompile><ObjectFileName>$(IntDir)</ObjectFileName></ClCompile></ItemDefinitionGroup>
  <ItemGroup><ClCompile Include="core.cpp" /></ItemGroup>
  <Target Name="Redirect" BeforeTargets="ClCompile">
    <ItemGroup><ClCompile><SourceDependenciesFile>$(IntDir)current-core.cpp.json</SourceDependenciesFile></ClCompile></ItemGroup>
  </Target>
  <Target Name="ClCompile" />
  <Target Name="Build" DependsOnTargets="ClCompile" />
  <Import Project="{REPO / 'Directory.Build.targets'}" />
</Project>
""", encoding="utf-8")
            out = subprocess.run([str(msbuild), str(project), "-nologo", "-p:Platform=x64",
                                  f"-p:Configuration={configuration}"], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout[-2000:])
            recorded = boundary.read_manifest("recto-core", manifests / "recto-core.txt")
            self.assertEqual([u.report for u in recorded.units],
                             [boundary.normalized(pathlib.Path(folder) / "obj" / "current-core.cpp.json")])
            self.assertTrue(recorded.units[0].generates, "Directory.Build.targets turns report generation on")


@unittest.skipUnless(sys.platform == "win32", "the C++ build tools are Windows-only (foundation plan D15)")
class CompilerReportedBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tools = tools()
        # The standard library and UCRT headers, for the fixtures that include <vector>: the MSVC include folder next
        # to the tools and the Windows SDK line the projects target (Directory.Build.props).
        cls.environment = dict(os.environ)
        if cls.tools is not None:
            sdk = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Windows Kits", "10",
                               "Include", "10.0.26100.0")
            cls.environment["INCLUDE"] = ";".join(str(p) for p in [cls.tools.parents[2] / "include", sdk / "ucrt",
                                                                   sdk / "shared", sdk / "um"])

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
                             cwd=self.root, env=self.environment)
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

    def test_the_other_sides_precompiled_header_is_caught(self):
        # Round 13, compiled for real (round 14): the toolkit builds a PCH in its IntDir from a header in a neutral
        # folder, and an engine unit uses it. The unit opens no file under src/ui; its report names the toolkit's PCH.
        (self.root / "shared/p.h").write_text("inline int shared_thing() { return 4; }\n", encoding="utf-8")
        (self.root / "src/ui/p.cpp").write_text('#include "p.h"\n', encoding="utf-8")
        pch = self.root / "obj/ui/p.pch"
        self.compile(self.root / "src/ui/p.cpp", self.root / "obj/ui", f"/I{self.root / 'shared'}", "/Ycp.h", f"/Fp{pch}")
        source = self.root / "src/core/e.cpp"
        source.write_text('#include "p.h"\nint e() { return shared_thing(); }\n', encoding="utf-8")
        unit = self.compile(source, self.root / "obj/core", f"/I{self.root / 'shared'}", "/Yup.h", f"/Fp{pch}")
        found = boundary.include_crossings("recto-core", [unit], self.forbidden)
        self.assertEqual(found, [f"recto-core: {unit.source} used {boundary.normalized(pch)}"])

    def test_the_other_sides_module_interface_is_caught(self):
        # Round 13, compiled for real (round 14): the toolkit compiles a module interface into its IntDir and an engine
        # unit imports it by /reference, with no toolkit header anywhere.
        interface = self.root / "src/ui/toolkit.ixx"
        interface.write_text("export module recto.ui;\nexport int toolkit_value() { return 5; }\n", encoding="utf-8")
        bmi = self.root / "obj/ui/recto.ui.ifc"
        self.compile(interface, self.root / "obj/ui", "/std:c++latest", "/interface", f"/ifcOutput{bmi}")
        source = self.root / "src/core/m.cpp"
        source.write_text("import recto.ui;\nint m() { return toolkit_value(); }\n", encoding="utf-8")
        unit = self.compile(source, self.root / "obj/core", "/std:c++latest", "/reference", f"recto.ui={bmi}")
        found = boundary.include_crossings("recto-core", [unit], self.forbidden)
        self.assertTrue(found, "an imported toolkit module must be a crossing")
        self.assertTrue(any(str(boundary.normalized(bmi)) in line for line in found), found)

    def test_report_parsing_counts_header_units(self):
        # Parser coverage only (no compile): an imported header unit's header and BMI are paths the unit used.
        source = self.root / "src/core/d.cpp"
        report = self.root / "obj/core/d.cpp.json"
        report.write_text(json.dumps({"Version": "1.2", "Data": {"Source": str(source), "Includes": [],
                                      "ImportedHeaderUnits": [{"Header": str(self.root / "src/ui/x.h"),
                                                               "BMI": str(self.root / "obj/x.h.ifc")}]}}),
                          encoding="utf-8")
        unit = boundary.Unit(boundary.normalized(source), boundary.normalized(self.root / "obj/core/d.obj"),
                             boundary.normalized(report), True)
        self.assertEqual(len(boundary.include_crossings("recto-core", [unit], self.forbidden)), 1)

    def test_a_linker_option_naming_the_toolkit_with_a_trailing_dot_is_caught(self):
        # Round 14: /DEFAULTLIB:recto-ui.lib. through a macro kind; LINK opens recto-ui.lib for it.
        source = self.root / "src/core/dot.cpp"
        source.write_text('#define KIND linker\n#pragma comment(KIND, "/DEFAULTLIB:recto-ui.lib.")\nint dot() { return 0; }\n',
                          encoding="utf-8")
        unit = self.compile(source, self.root / "obj/core")
        library = self.library(self.root / "obj/core/dot.lib", unit.obj)
        self.assertEqual(len(self.directives(library)), 1)

    def test_a_symbol_the_other_side_defines_is_caught(self):
        # Round 14: recto-ui forward-declares an engine function. No header, directive or member crosses; the toolkit
        # library's undefined symbol is defined by the engine library, which is what the linker would resolve.
        (self.root / "src/core/abi.cpp").write_text("namespace recto::core { int abi_version() { return 7; } }\n",
                                                    encoding="utf-8")
        engine_unit = self.compile(self.root / "src/core/abi.cpp", self.root / "obj/core", "/std:c++latest")
        engine = self.library(self.root / "obj/core/recto-core.lib", engine_unit.obj)
        (self.root / "src/ui/v.cpp").write_text("namespace recto::core { int abi_version(); }\n"
                                                "namespace recto::ui { int version() { return recto::core::abi_version(); } }\n",
                                                encoding="utf-8")
        toolkit_unit = self.compile(self.root / "src/ui/v.cpp", self.root / "obj/ui", "/std:c++latest")
        toolkit = self.library(self.root / "obj/ui/recto-ui.lib", toolkit_unit.obj)
        dumpbin = self.tools / "dumpbin.exe"
        engine_table, toolkit_table = boundary.symbol_table(engine, dumpbin), boundary.symbol_table(toolkit, dumpbin)
        self.assertEqual(boundary.symbol_crossings("recto-ui", toolkit_table, {"recto-core": engine_table}),
                         ["recto-ui: needs ?abi_version@core@recto@@YAHXZ, which recto-core defines"])
        self.assertEqual(boundary.symbol_crossings("recto-core", engine_table, {"recto-ui": toolkit_table}), [])

    def tables(self, engine, engine_text, toolkit_text, *options):
        """Compile one engine unit and one toolkit unit into their libraries and return both SymbolTables."""
        dumpbin = self.tools / "dumpbin.exe"
        (self.root / "src/core/e1.cpp").write_text(engine_text, encoding="utf-8")
        (self.root / "src/ui/t1.cpp").write_text(toolkit_text, encoding="utf-8")
        shared = f"/I{self.root / 'shared'}"
        engine_unit = self.compile(self.root / "src/core/e1.cpp", self.root / "obj/core", "/std:c++latest", "/EHsc",
                                   shared, *options)
        toolkit_unit = self.compile(self.root / "src/ui/t1.cpp", self.root / "obj/ui", "/std:c++latest", "/EHsc",
                                    shared, *options)
        engine_library = self.library(self.root / "obj/core" / f"{engine}.lib", engine_unit.obj)
        toolkit_library = self.library(self.root / "obj/ui/recto-ui.lib", toolkit_unit.obj)
        return boundary.symbol_table(engine_library, dumpbin), boundary.symbol_table(toolkit_library, dumpbin)

    def test_a_forward_declared_pdf_function_is_caught_under_function_level_linking(self):
        # recto::pdf::page_count, defined under /Gy (its own COMDAT section, selection "no duplicates"), still belongs
        # to recto-pdf and is defined there uniquely.
        engine, toolkit = self.tables(
            "recto-pdf", "namespace recto::pdf { int page_count() { return 3; } }\n",
            "namespace recto::pdf { int page_count(); }\n"
            "namespace recto::ui { int pages() { return recto::pdf::page_count(); } }\n", "/Gy")
        self.assertEqual(boundary.symbol_crossings("recto-ui", toolkit, {"recto-pdf": engine}),
                         ["recto-ui: needs ?page_count@pdf@recto@@YAHXZ, which recto-pdf defines"])

    def test_shared_std_templates_inline_functions_and_selectany_data_are_not_crossings(self):
        # A header both sides may include: a std:: template the engine instantiates and the toolkit only declares
        # (extern template), an inline function both use, and selectany data. Each leaves the toolkit with undefined
        # symbols the engine library defines; none is code a module owns and defines uniquely.
        (self.root / "shared/common.h").write_text(
            "#pragma once\n#include <vector>\n"
            "struct Shared { int v; };\n"
            "extern template class std::vector<Shared>;\n"
            "inline int shared_inline(int v) { return v + 1; }\n"
            "extern const int kShared;\n", encoding="utf-8")
        engine, toolkit = self.tables(
            "recto-core",
            '#include "common.h"\ntemplate class std::vector<Shared>;\n'
            "__declspec(selectany) extern const int kShared = 5;\n"
            "int engine_use() { return shared_inline(1); }\n",
            '#include "common.h"\n'
            "namespace recto::ui { int toolkit_use() { std::vector<Shared> v(2); v.push_back({1});\n"
            "  return static_cast<int>(v.size()) + shared_inline(2) + kShared; } }\n")
        needed = toolkit.undefined - toolkit.defined
        self.assertTrue(needed & engine.defined, "the fixture must leave the toolkit needing engine-defined symbols")
        self.assertEqual(boundary.symbol_crossings("recto-ui", toolkit, {"recto-core": engine}), [])

    def test_a_toolkit_module_with_its_bmi_in_a_neutral_folder_is_caught_by_name(self):
        # Round 15: the toolkit writes recto.ui.ifc to shared/, outside every forbidden folder; the engine imports it.
        interface = self.root / "src/ui/toolkit.ixx"
        interface.write_text("export module recto.ui;\nexport struct Widget { int width = 3; };\n", encoding="utf-8")
        bmi = self.root / "shared/recto.ui.ifc"
        toolkit_unit = self.compile(interface, self.root / "obj/ui", "/std:c++latest", "/interface", f"/ifcOutput{bmi}")
        source = self.root / "src/core/w2.cpp"
        source.write_text("import recto.ui;\nint w2() { return Widget{}.width; }\n", encoding="utf-8")
        unit = self.compile(source, self.root / "obj/core", "/std:c++latest", "/reference", f"recto.ui={bmi}")
        modules = boundary.provided_modules("recto-ui", [toolkit_unit])
        self.assertEqual(modules, {"recto.ui"})
        self.assertEqual(boundary.include_crossings("recto-core", [unit], self.forbidden, (), modules),
                         [f"recto-core: {unit.source} imports module recto.ui, which the other side provides"])

    def test_a_toolkit_pch_in_a_neutral_folder_is_caught_as_its_product(self):
        # Round 15's ownership point for PCHs: the toolkit writes its PCH to shared/; the engine uses it.
        (self.root / "shared/q.h").write_text("inline int q_thing() { return 6; }\n", encoding="utf-8")
        (self.root / "src/ui/q.cpp").write_text('#include "q.h"\n', encoding="utf-8")
        pch = self.root / "shared/q.pch"
        self.compile(self.root / "src/ui/q.cpp", self.root / "obj/ui", f"/I{self.root / 'shared'}", "/Ycq.h", f"/Fp{pch}")
        source = self.root / "src/core/q2.cpp"
        source.write_text('#include "q.h"\nint q2() { return q_thing(); }\n', encoding="utf-8")
        unit = self.compile(source, self.root / "obj/core", f"/I{self.root / 'shared'}", "/Yuq.h", f"/Fp{pch}")
        self.assertEqual(boundary.include_crossings("recto-core", [unit], self.forbidden), [])
        self.assertEqual(boundary.include_crossings("recto-core", [unit], self.forbidden, {pch}),
                         [f"recto-core: {unit.source} used {boundary.normalized(pch)}"])

    def test_an_export_directive_naming_an_engine_symbol_is_caught(self):
        # Round 15: /EXPORT:alias=<engine symbol> makes LINK resolve the engine symbol with no UNDEF record.
        engine, toolkit = self.tables(
            "recto-core", "namespace recto::core { int abi_version() { return 7; } }\n",
            '#define KIND linker\n#pragma comment(KIND, "/EXPORT:ui_bridge=?abi_version@core@recto@@YAHXZ")\n'
            "namespace recto::ui { int nothing() { return 0; } }\n")
        self.assertNotIn("?abi_version@core@recto@@YAHXZ", toolkit.defined)
        self.assertEqual(boundary.symbol_crossings("recto-ui", toolkit, {"recto-core": engine}),
                         ["recto-ui: needs ?abi_version@core@recto@@YAHXZ, which recto-core defines"])

    def test_an_owned_explicit_instantiation_or_selectany_constant_is_caught(self):
        # Round 15: both sit in pick-any COMDAT sections, yet the toolkit defines neither and needs the engine's.
        engine, toolkit = self.tables(
            "recto-core",
            "namespace recto::core {\ntemplate <class T> int secret(T v) { return v + 7; }\ntemplate int secret<int>(int);\n"
            "__declspec(selectany) extern const int kLimit = 9;\n}\n",
            "namespace recto::core {\ntemplate <class T> int secret(T);\nextern template int secret<int>(int);\n"
            "extern const int kLimit;\n}\n"
            "namespace recto::ui { int probe() { return recto::core::secret(3) + recto::core::kLimit; } }\n")
        found = boundary.symbol_crossings("recto-ui", toolkit, {"recto-core": engine})
        self.assertEqual(len(found), 2, found)
        self.assertTrue(any("secret" in line for line in found), found)
        self.assertTrue(any("kLimit" in line for line in found), found)

    def test_a_quoted_library_name_with_a_space_is_caught(self):
        # Round 15: the toolkit's TargetName is `ui kit`; cl writes /DEFAULTLIB:"ui kit.lib".
        source = self.root / "src/core/space.cpp"
        source.write_text('#define KIND lib\n#pragma comment(KIND, "ui kit.lib")\nint space() { return 0; }\n',
                          encoding="utf-8")
        unit = self.compile(source, self.root / "obj/core")
        library = self.library(self.root / "obj/core/space.lib", unit.obj)
        self.assertEqual(len(self.directives(library, [self.root / "obj/ui/ui kit.lib"])), 1)

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
