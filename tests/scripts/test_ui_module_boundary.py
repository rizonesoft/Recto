"""The D13 boundary between the engine libraries and recto-ui, at the project level (D10 T01 §1).

`Ui.Project.IncludeBoundary` (recto-tests) reads the `#include` lines. This test reads what MSBuild itself evaluates for
each module project in all four configurations, before any native compilation: the include directories every source
file is compiled with, and the project references, followed transitively. recto-pdf, recto-core and recto-services may
not see `src/ui` or reach recto-ui; recto-ui may not see `src/pdf`, `src/core` or `src/services` or reach their
projects. A planted project proves each rule fails."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from test_engine_dependency_policy import CONFIGURATIONS, msbuild  # noqa: E402

ENGINE = {"recto-pdf": REPO / "src" / "pdf", "recto-core": REPO / "src" / "core", "recto-services": REPO / "src" / "services"}
UI = {"recto-ui": REPO / "src" / "ui"}
PROJECTS = {name: folder / f"{name}.vcxproj" for name, folder in {**ENGINE, **UI}.items()}


def evaluate(tool, project, configuration, platform):
    """(include directories, referenced project paths) as MSBuild evaluates `project`, every path resolved."""
    out = subprocess.run([tool, str(project), f"-p:Configuration={configuration}", f"-p:Platform={platform}",
                          "-getItem:ClCompile", "-getItem:ProjectReference", "-nologo"],
                         capture_output=True, text=True, encoding="utf-8", errors="replace")
    if out.returncode != 0:
        raise AssertionError(f"MSBuild could not evaluate {project}: {out.stdout[-400:]}{out.stderr[-400:]}")
    items = json.loads(out.stdout)["Items"]
    if not items.get("ClCompile"):
        raise AssertionError(f"{project} evaluated no ClCompile items; the check would see nothing")
    folders = set()
    for item in items.get("ClCompile", []):
        for entry in item.get("AdditionalIncludeDirectories", "").split(";"):
            entry = entry.strip()
            if entry and not entry.startswith("%("):
                folders.add((project.parent / entry).resolve())
    references = {(project.parent / item["Identity"]).resolve() for item in items.get("ProjectReference", [])}
    return folders, references


def inside(path, folder):
    return path == folder.resolve() or folder.resolve() in path.parents


def violations(name, folders, reachable):
    """What crosses the boundary for module `name`, given its include folders and every project it reaches."""
    if name in ENGINE:
        forbidden_folders, forbidden_projects = list(UI.values()), {PROJECTS["recto-ui"].resolve()}
    else:
        forbidden_folders, forbidden_projects = list(ENGINE.values()), {PROJECTS[n].resolve() for n in ENGINE}
    found = [f"{name} includes {folder}" for folder in sorted(folders)
             if any(inside(folder, bad) for bad in forbidden_folders)]
    found += [f"{name} reaches {project.name}" for project in sorted(reachable) if project in forbidden_projects]
    return found


def reachable_from(tool, project, configuration, platform, cache):
    """Every project `project` references, directly or through others."""
    seen, pending = set(), [project.resolve()]
    while pending:
        current = pending.pop()
        key = (current, configuration, platform)
        if key not in cache:
            cache[key] = evaluate(tool, current, configuration, platform)
        for reference in cache[key][1]:
            if reference not in seen:
                seen.add(reference)
                pending.append(reference)
    return seen


@unittest.skipUnless(sys.platform == "win32", "the C++ build and MSBuild are Windows-only (foundation plan D15)")
class UiModuleBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool = msbuild()

    def setUp(self):
        self.assertIsNotNone(self.tool, "MSBuild was not found through vswhere; the boundary cannot be checked")

    def test_the_module_projects_keep_the_boundary_in_every_configuration(self):
        cache = {}
        for name, project in PROJECTS.items():
            for configuration, platform in CONFIGURATIONS:
                with self.subTest(project=name, configuration=configuration, platform=platform):
                    key = (project.resolve(), configuration, platform)
                    cache.setdefault(key, evaluate(self.tool, project, configuration, platform))
                    folders = cache[key][0]
                    reachable = reachable_from(self.tool, project, configuration, platform, cache)
                    self.assertEqual(violations(name, folders, reachable), [])

    def planted(self, body):
        """A throwaway project with `body` inside its ItemGroup, evaluated as Debug|x64."""
        with tempfile.TemporaryDirectory() as folder:
            project = pathlib.Path(folder) / "planted.vcxproj"
            project.write_text('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                               f"  <ItemGroup>\n{body}\n  </ItemGroup>\n</Project>\n", encoding="utf-8")
            folders, references = evaluate(self.tool, project, "Debug", "x64")
        return folders, references

    def test_an_engine_project_that_sees_the_toolkit_fails(self):
        ui_include = REPO / "src" / "ui" / "include"
        folders, references = self.planted(
            f'    <ClCompile Include="a.cpp"><AdditionalIncludeDirectories>{ui_include};%(AdditionalIncludeDirectories)'
            f'</AdditionalIncludeDirectories></ClCompile>\n'
            f'    <ProjectReference Include="{PROJECTS["recto-ui"]}" />')
        self.assertEqual(violations("recto-core", folders, references),
                         [f"recto-core includes {ui_include.resolve()}", "recto-core reaches recto-ui.vcxproj"])

    def test_the_toolkit_seeing_an_engine_library_fails(self):
        pdf = REPO / "src" / "pdf"
        folders, references = self.planted(
            f'    <ClCompile Include="a.cpp"><AdditionalIncludeDirectories>{pdf}</AdditionalIncludeDirectories></ClCompile>\n'
            f'    <ProjectReference Include="{PROJECTS["recto-services"]}" />')
        self.assertEqual(violations("recto-ui", folders, references),
                         [f"recto-ui includes {pdf.resolve()}", "recto-ui reaches recto-services.vcxproj"])

    def test_a_transitive_reference_is_followed(self):
        # recto-services reaches recto-pdf only through recto-core: the walk must find it.
        reachable = reachable_from(self.tool, PROJECTS["recto-services"], "Debug", "x64", {})
        self.assertIn(PROJECTS["recto-pdf"].resolve(), reachable)
        self.assertEqual(violations("recto-ui", set(), reachable), ["recto-ui reaches recto-core.vcxproj",
                                                                    "recto-ui reaches recto-pdf.vcxproj"])


if __name__ == "__main__":
    unittest.main()
