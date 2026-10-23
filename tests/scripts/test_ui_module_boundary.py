"""The D13 boundary between the engine libraries and recto-ui, at the project level (D10 T01 §1).

`Ui.Project.IncludeBoundary` (recto-tests) reads the `#include` lines. This test reads what MSBuild itself evaluates for
each module project in all four configurations, before any native compilation (CI runs it ahead of the build): the
include folders every source file is compiled with (`AdditionalIncludeDirectories`, `AdditionalUsingDirectories` and
the `IncludePath` and `ExternalIncludePath` properties), the files it force-includes (`ForcedIncludeFiles`,
`ForcedUsingFiles`), every path its `AdditionalOptions` name (`/I`, `/FI`, `/external:I` and the like), the sources it
compiles, and its project references followed transitively. recto-pdf,
recto-core and recto-services may not see `src/ui`, force-include or compile a file there, or reach recto-ui; recto-ui
may not do any of that with `src/pdf`, `src/core` or `src/services` and their projects. Every finding names the rule,
the module, the configuration and, for a reference, the whole chain. A planted project proves each rule fails. Under
ASan, every recto-ui source is compiled with /fsanitize=address."""
import json
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from test_engine_dependency_policy import CONFIGURATIONS, msbuild  # noqa: E402  (sibling test module, shared helpers)

ENGINE = {"recto-pdf": REPO / "src" / "pdf", "recto-core": REPO / "src" / "core", "recto-services": REPO / "src" / "services"}
UI = {"recto-ui": REPO / "src" / "ui"}
FOLDERS = {**ENGINE, **UI}
PROJECTS = {name: folder / f"{name}.vcxproj" for name, folder in FOLDERS.items()}


class Evaluated:
    """What MSBuild evaluates for one project in one configuration, every path resolved."""

    def __init__(self, folders, forced, sources, references, options=frozenset()):
        self.folders, self.forced, self.sources, self.references = folders, forced, sources, references
        self.options = options


def resolved(path):
    """`path` made absolute with links followed; a string no file system accepts is kept as its absolute spelling."""
    try:
        return path.resolve()
    except (OSError, ValueError):
        return pathlib.Path(os.path.abspath(path))


# cl switches that take a path, longest first so /external:I is not read as /e... and /FI is not read as /F.
PATH_SWITCHES = ("external:I", "ifcSearchDir", "headerUnit:angle", "headerUnit:quote", "headerUnit", "reference",
                 "FU", "FI", "AI", "I")


def option_paths(project, options):
    """Every path an AdditionalOptions string names, resolved against the project folder.

    Each token counts as a path as it stands, after a path switch (`/I`, `-FI`, `/external:I`, ...) and after any
    `name=` part (`/reference ui=x.ifc`); a path switch with no attached value takes the next token. A token that is
    not a path resolves to a harmless place beside the project, so this can only over-report a forbidden folder."""
    tokens = [token.strip('"') for token in shlex.split(options, posix=False)]
    found = set()
    for index, token in enumerate(tokens):
        candidates = [token]
        if token[:1] in ("/", "-"):
            for switch in PATH_SWITCHES:
                if token[1:1 + len(switch)] == switch:
                    value = token[1 + len(switch):]
                    if not value and index + 1 < len(tokens):
                        value = tokens[index + 1]
                    candidates.append(value)
                    break
        candidates += [candidate.split("=", 1)[1] for candidate in list(candidates) if "=" in candidate]
        found |= {resolved(project.parent / candidate) for candidate in candidates
                  if candidate and not candidate.startswith(("%(", "$("))}
    return found


def evaluate(tool, project, configuration, platform, *extra):
    out = subprocess.run([tool, str(project), f"-p:Configuration={configuration}", f"-p:Platform={platform}", *extra,
                          "-getItem:ClCompile", "-getItem:ProjectReference", "-getProperty:IncludePath",
                          "-getProperty:ExternalIncludePath", "-nologo"],
                         capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300)
    if out.returncode != 0:
        raise AssertionError(f"MSBuild could not evaluate {project}: {out.stdout[-400:]}{out.stderr[-400:]}")
    data = json.loads(out.stdout)
    items = data["Items"]
    if not items.get("ClCompile"):
        raise AssertionError(f"{project} evaluated no ClCompile items; the check would see nothing")

    def paths(value):
        return {resolved(project.parent / entry.strip()) for entry in value.split(";")
                if entry.strip() and not entry.strip().startswith(("%(", "$("))}

    # MSBuild hands IncludePath to cl as INCLUDE, and the toolset's AddExternalIncludDirectoriesToPaths target appends
    # ExternalIncludePath to it (and hands it as EXTERNAL_INCLUDE) at build time, after evaluation: both are header
    # search folders for every source.
    properties = data.get("Properties", {})
    base_folders = paths(properties.get("IncludePath", "")) | paths(properties.get("ExternalIncludePath", ""))
    folders, forced, sources, options = set(base_folders), set(), set(), set()
    for item in items["ClCompile"]:
        source = resolved(project.parent / item["Identity"])
        item_folders = (paths(item.get("AdditionalIncludeDirectories", "")) |
                        paths(item.get("AdditionalUsingDirectories", "")) | base_folders)
        folders |= item_folders
        sources.add(source)
        # AdditionalOptions reach cl unchanged, so a path any switch there names counts as a folder or file it sees.
        options |= option_paths(project, item.get("AdditionalOptions", ""))
        # /FI names a file the way `#include "name"` does: an absolute path is itself, a relative one is looked up
        # beside the source and then along the include folders. Every place the compiler could find it counts, so a
        # crossing is caught whichever candidate exists.
        for entry in f'{item.get("ForcedIncludeFiles", "")};{item.get("ForcedUsingFiles", "")}'.split(";"):
            entry = entry.strip()
            if not entry or entry.startswith(("%(", "$(")):
                continue
            if pathlib.PureWindowsPath(entry).is_absolute():
                forced.add(resolved(pathlib.Path(entry)))
            else:
                forced |= {resolved(folder / entry) for folder in [source.parent, project.parent, *item_folders]}
    references = {resolved(project.parent / item["Identity"]) for item in items.get("ProjectReference", [])}
    return Evaluated(folders, forced, sources, references, options), items["ClCompile"]


def inside(path, folder):
    return path == folder.resolve() or folder.resolve() in path.parents


def rules(name):
    """(the rule's wording, the folders module `name` may not touch, the projects it may not reach)."""
    if name in ENGINE:
        return ("an engine library may not see, compile or reach recto-ui (D13)", list(UI.values()),
                {PROJECTS["recto-ui"].resolve()})
    return ("recto-ui may not see, compile or reach an engine library (D13)", list(ENGINE.values()),
            {PROJECTS[n].resolve() for n in ENGINE})


def violations(name, configuration, evaluated, chains):
    """Every crossing for module `name`; `chains` maps each reachable project to the reference chain that reaches it."""
    rule, forbidden_folders, forbidden_projects = rules(name)
    where = f"{name} ({configuration})"

    def crosses(path):
        return any(inside(path, bad) for bad in forbidden_folders)

    found = [f"{where}: include folder {p} breaks: {rule}" for p in sorted(evaluated.folders) if crosses(p)]
    found += [f"{where}: forced include {p} breaks: {rule}" for p in sorted(evaluated.forced) if crosses(p)]
    found += [f"{where}: compiler option names {p} breaks: {rule}" for p in sorted(evaluated.options) if crosses(p)]
    found += [f"{where}: compiles {p} breaks: {rule}" for p in sorted(evaluated.sources) if crosses(p)]
    found += [f"{where}: reaches {' -> '.join(c.stem for c in chains[p])} breaks: {rule}"
              for p in sorted(chains) if p in forbidden_projects]
    return found


def reference_chains(tool, project, configuration, platform, cache, first=None):
    """Every project `project` reaches, mapped to the chain of projects from `project` to it."""
    root = project.resolve()
    chains, pending = {}, [(root, [root])]
    while pending:
        current, chain = pending.pop()
        key = (current, configuration, platform)
        if current == root and first is not None:
            references = first.references
        else:
            if key not in cache:
                cache[key] = evaluate(tool, current, configuration, platform)[0]
            references = cache[key].references
        for reference in references:
            if reference not in chains:
                chains[reference] = chain + [reference]
                pending.append((reference, chain + [reference]))
    return chains


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
                    if key not in cache:
                        cache[key] = evaluate(self.tool, project, configuration, platform)[0]
                    chains = reference_chains(self.tool, project, configuration, platform, cache)
                    self.assertEqual(violations(name, f"{configuration}|{platform}", cache[key], chains), [])

    def planted(self, body, *properties):
        """A throwaway project with `body` in its ItemGroup, evaluated as Debug|x64."""
        with tempfile.TemporaryDirectory() as folder:
            project = pathlib.Path(folder) / "planted.vcxproj"
            groups = "".join(f"  <PropertyGroup>{p}</PropertyGroup>\n" for p in properties)
            project.write_text('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                               f"{groups}  <ItemGroup>\n{body}\n  </ItemGroup>\n</Project>\n", encoding="utf-8")
            evaluated = evaluate(self.tool, project, "Debug", "x64")[0]
            return evaluated, reference_chains(self.tool, project, "Debug", "x64", {}, first=evaluated)

    def test_an_engine_project_that_sees_the_toolkit_fails(self):
        ui = REPO / "src" / "ui"
        evaluated, chains = self.planted(
            f'    <ClCompile Include="{ui / "ui.cpp"}"><AdditionalIncludeDirectories>{ui / "include"}</AdditionalIncludeDirectories>'
            f'<ForcedIncludeFiles>{ui / "pch.h"}</ForcedIncludeFiles></ClCompile>\n'
            f'    <ProjectReference Include="{PROJECTS["recto-ui"]}" />',
            f"<IncludePath>{ui / 'include' / 'recto'}</IncludePath>")
        found = violations("recto-core", "Debug|x64", evaluated, chains)
        rule = "an engine library may not see, compile or reach recto-ui (D13)"
        self.assertEqual(found, [
            f"recto-core (Debug|x64): include folder {(ui / 'include').resolve()} breaks: {rule}",
            f"recto-core (Debug|x64): include folder {(ui / 'include' / 'recto').resolve()} breaks: {rule}",
            f"recto-core (Debug|x64): forced include {(ui / 'pch.h').resolve()} breaks: {rule}",
            f"recto-core (Debug|x64): compiles {(ui / 'ui.cpp').resolve()} breaks: {rule}",
            f"recto-core (Debug|x64): reaches planted -> recto-ui breaks: {rule}"])

    def test_a_relative_forced_include_found_through_an_include_folder_fails(self):
        # /FI ui/include/recto/ui/ui.h with src on the include path reaches the toolkit with no #include in any source.
        src = REPO / "src"
        evaluated, chains = self.planted(
            f'    <ClCompile Include="a.cpp"><AdditionalIncludeDirectories>{src}</AdditionalIncludeDirectories>'
            '<ForcedIncludeFiles>ui/include/recto/ui/ui.h</ForcedIncludeFiles></ClCompile>')
        rule = "an engine library may not see, compile or reach recto-ui (D13)"
        self.assertEqual(violations("recto-core", "Debug|x64", evaluated, chains),
                         [f"recto-core (Debug|x64): forced include {(src / 'ui' / 'include' / 'recto' / 'ui' / 'ui.h').resolve()}"
                          f" breaks: {rule}"])

    def test_an_external_include_path_or_a_compiler_option_into_the_toolkit_fails(self):
        # ExternalIncludePath joins the header search path at build time; AdditionalOptions reach cl as written.
        ui = REPO / "src" / "ui"
        evaluated, chains = self.planted(
            f'    <ClCompile Include="a.cpp"><AdditionalOptions>/utf-8 /I "{ui / "include"}" -FI{ui / "pch.h"} '
            f'/external:I {ui} /reference recto.ui={ui / "ui.ifc"} %(AdditionalOptions)</AdditionalOptions></ClCompile>',
            f"<ExternalIncludePath>{ui / 'include' / 'recto'}</ExternalIncludePath>")
        rule = "an engine library may not see, compile or reach recto-ui (D13)"
        named = [(ui / "include").resolve(), (ui / "pch.h").resolve(), ui.resolve(), (ui / "ui.ifc").resolve()]
        self.assertEqual(violations("recto-core", "Debug|x64", evaluated, chains),
                         [f"recto-core (Debug|x64): include folder {(ui / 'include' / 'recto').resolve()} breaks: {rule}",
                          *[f"recto-core (Debug|x64): compiler option names {p} breaks: {rule}" for p in sorted(named)]])

    def test_the_toolkit_seeing_an_engine_library_fails(self):
        pdf = REPO / "src" / "pdf"
        evaluated, chains = self.planted(
            f'    <ClCompile Include="a.cpp"><AdditionalIncludeDirectories>{pdf}</AdditionalIncludeDirectories></ClCompile>\n'
            f'    <ProjectReference Include="{PROJECTS["recto-services"]}" />')
        found = violations("recto-ui", "Debug|x64", evaluated, chains)
        rule = "recto-ui may not see, compile or reach an engine library (D13)"
        # recto-services reaches recto-pdf only through recto-core: the chain names every step.
        self.assertEqual(found, [
            f"recto-ui (Debug|x64): include folder {pdf.resolve()} breaks: {rule}",
            f"recto-ui (Debug|x64): reaches planted -> recto-services -> recto-core breaks: {rule}",
            f"recto-ui (Debug|x64): reaches planted -> recto-services -> recto-core -> recto-pdf breaks: {rule}",
            f"recto-ui (Debug|x64): reaches planted -> recto-services breaks: {rule}"])

    def test_every_toolkit_source_is_instrumented_under_asan(self):
        # Each recto-ui source compiled under -EnableASAN carries /fsanitize=address (x64; ARM64 has no ASan).
        for configuration in ("Debug", "Release"):
            with self.subTest(configuration=configuration):
                items = evaluate(self.tool, PROJECTS["recto-ui"], configuration, "x64", "-p:EnableASAN=true")[1]
                missing = [i["Identity"] for i in items if "/fsanitize=address" not in i.get("AdditionalOptions", "")]
                self.assertEqual(missing, [])
                normal = evaluate(self.tool, PROJECTS["recto-ui"], configuration, "x64", "-p:EnableASAN=false")[1]
                self.assertFalse(any("/fsanitize=address" in i.get("AdditionalOptions", "") for i in normal))


if __name__ == "__main__":
    unittest.main()
