"""The D13 boundary between the engine libraries and recto-ui, at the project level (D10 T01 §1).

`Ui.Project.IncludeBoundary` (recto-tests) reads the `#include` lines. This test reads what MSBuild itself evaluates for
each module project in all four configurations, before any native compilation (CI runs it ahead of the build): the
include folders every source file is compiled with (`AdditionalIncludeDirectories`, `AdditionalUsingDirectories` and
the `IncludePath` and `ExternalIncludePath` properties), the files it force-includes (`ForcedIncludeFiles`,
`ForcedUsingFiles`), every path its `AdditionalOptions` name (`/I`, `/FI`, `/external:I` and the like), the sources it
compiles, the libraries it links or bundles (`AdditionalDependencies`, `AdditionalLibraryDirectories`, the
`LibraryPath` property and the `/LIBPATH:` and `/DEFAULTLIB:` options of its Link and Lib settings, read through
`fixtures/link_probe.targets`), and its project references followed transitively. recto-pdf, recto-core and
recto-services may not see `src/ui`, force-include, compile or link a file there, link recto-ui's library, or reach
recto-ui; recto-ui may not do any of that with `src/pdf`, `src/core` or `src/services` and their projects. Every finding names the rule,
the module, the configuration and, for a reference, the whole chain. A planted project proves each rule fails. Under
ASan, every recto-ui source is compiled with /fsanitize=address."""
import json
import os
import pathlib
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

    def __init__(self, folders, forced, sources, references, options=frozenset(), libraries=frozenset(),
                 library_folders=frozenset(), output=None):
        self.folders, self.forced, self.sources, self.references = folders, forced, sources, references
        self.options, self.libraries, self.library_folders, self.output = options, libraries, library_folders, output


def resolved(path):
    """`path` made absolute with links followed; a string no file system accepts is kept as its absolute spelling."""
    try:
        return path.resolve()
    except (OSError, ValueError):
        return pathlib.Path(os.path.abspath(path))


# cl, link and lib switches that take a path, by what the path is: a search folder, a file looked up the way a quoted
# #include is (/FI, /FU), or any other file. Matching ignores case (link and lib accept any case; for cl it can only
# over-report) and tries the longest switch first, so /external:I is not read as /e... and /FI is not read as /F.
FOLDER_SWITCHES = ("external:I", "ifcSearchDir", "LIBPATH:", "imsvc", "AI", "I")
LOOKUP_SWITCHES = ("FU", "FI")
FILE_SWITCHES = ("headerUnit:angle", "headerUnit:quote", "headerUnit", "reference", "DEFAULTLIB:")
SWITCHES = sorted([(s, "folder") for s in FOLDER_SWITCHES] + [(s, "lookup") for s in LOOKUP_SWITCHES] +
                  [(s, "file") for s in FILE_SWITCHES], key=lambda pair: -len(pair[0]))
LINK_PROBE = pathlib.Path(__file__).resolve().parent / "fixtures" / "link_probe.targets"


def command_line_tokens(text):
    """`text` split into arguments the way the Microsoft C runtime splits a command line, which is how cl, link and lib
    read their options: white space outside quotes separates; a quote opens or closes quoting anywhere in an argument
    (`/I"a b"` is `/Ia b`) and is dropped; `""` inside quotes is one literal quote; 2n backslashes before a quote are n
    backslashes and the quote still counts, 2n+1 are n backslashes and a literal quote; other backslashes are literal."""
    tokens, current, quoted, started, i = [], [], False, False, 0
    while i < len(text):
        c = text[i]
        if c == "\\":
            j = i
            while j < len(text) and text[j] == "\\":
                j += 1
            if j < len(text) and text[j] == '"':
                current.append("\\" * ((j - i) // 2))
                if (j - i) % 2:
                    current.append('"')
                    j += 1
            else:
                current.append("\\" * (j - i))
            i, started = j, True
        elif c == '"':
            if quoted and text[i + 1:i + 2] == '"':
                current.append('"')
                i += 2
            else:
                quoted, i = not quoted, i + 1
            started = True
        elif c in " \t\r\n" and not quoted:
            if started:
                tokens.append("".join(current))
            current, started, i = [], False, i + 1
        else:
            current.append(c)
            started, i = True, i + 1
    if started:
        tokens.append("".join(current))
    return tokens


class Options:
    """What one AdditionalOptions string names: `folders` searched for headers or libraries, `lookups` named like a
    quoted #include (/FI, /FU: found beside the source or along the include folders), and `paths`, every token read as
    a path (the token itself, a switch's value, the part after `name=`), each resolved against the project folder."""

    def __init__(self, project, options):
        self.folders, self.lookups, self.paths = set(), [], set()
        tokens = command_line_tokens(options)
        for index, token in enumerate(tokens):
            candidates = [token]
            if token[:1] in ("/", "-"):
                for switch, kind in SWITCHES:
                    if token[1:1 + len(switch)].lower() == switch.lower():
                        value = token[1 + len(switch):]
                        if not value and index + 1 < len(tokens):
                            value = tokens[index + 1]
                        candidates.append(value)
                        if value and kind == "folder":
                            self.folders.add(resolved(project.parent / value))
                        elif value and kind == "lookup":
                            self.lookups.append(value)
                        break
            candidates += [candidate.split("=", 1)[1] for candidate in list(candidates) if "=" in candidate]
            self.paths |= {resolved(project.parent / candidate) for candidate in candidates
                           if candidate and not candidate.startswith(("%(", "$("))}


def evaluate(tool, project, configuration, platform, *extra):
    out = subprocess.run([tool, str(project), f"-p:Configuration={configuration}", f"-p:Platform={platform}",
                          f"-p:ForceImportAfterCppTargets={LINK_PROBE}", *extra,
                          "-getItem:ClCompile", "-getItem:ProjectReference", "-getItem:Link", "-getItem:Lib",
                          "-getProperty:IncludePath", "-getProperty:ExternalIncludePath", "-getProperty:LibraryPath",
                          "-getProperty:ConfigurationType", "-getProperty:OutDir", "-getProperty:TargetName",
                          "-getProperty:TargetExt", "-nologo"],
                         capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300)
    if out.returncode != 0:
        raise AssertionError(f"MSBuild could not evaluate {project}: {out.stdout[-400:]}{out.stderr[-400:]}")
    data = json.loads(out.stdout)
    items = data["Items"]
    if not items.get("ClCompile"):
        raise AssertionError(f"{project} evaluated no ClCompile items; the check would see nothing")

    def paths(value):
        # A quote is never part of a Windows path; MSBuild and the tools drop the quotes around an entry.
        entries = (entry.replace('"', "").strip() for entry in value.split(";"))
        return {resolved(project.parent / entry) for entry in entries if entry and not entry.startswith(("%(", "$("))}

    def forced_candidates(entry, source, search):
        # /FI and /FU name a file the way `#include "name"` does: an absolute path is itself, a relative one is looked up
        # beside the source and then along the include folders. Every place the tool could find it counts, so a
        # crossing is caught whichever candidate exists.
        if pathlib.PureWindowsPath(entry).is_absolute():
            return {resolved(pathlib.Path(entry))}
        return {resolved(folder / entry) for folder in [source.parent, project.parent, *search]}

    # MSBuild hands IncludePath to cl as INCLUDE, and the toolset's AddExternalIncludDirectoriesToPaths target appends
    # ExternalIncludePath to it (and hands it as EXTERNAL_INCLUDE) at build time, after evaluation: both are header
    # search folders for every source.
    properties = data.get("Properties", {})
    base_folders = paths(properties.get("IncludePath", "")) | paths(properties.get("ExternalIncludePath", ""))
    folders, forced, sources, options = set(base_folders), set(), set(), set()
    for item in items["ClCompile"]:
        source = resolved(project.parent / item["Identity"])
        # AdditionalOptions reach cl unchanged: every path they name is reported, the folders their /I-style switches
        # add join the search path, and their /FI and /FU files are looked up along it like ForcedIncludeFiles.
        item_options = Options(project, item.get("AdditionalOptions", ""))
        options |= item_options.paths
        item_folders = (paths(item.get("AdditionalIncludeDirectories", "")) |
                        paths(item.get("AdditionalUsingDirectories", "")) | item_options.folders | base_folders)
        folders |= item_folders
        sources.add(source)
        entries = f'{item.get("ForcedIncludeFiles", "")};{item.get("ForcedUsingFiles", "")}'.split(";")
        for entry in [e.replace('"', "").strip() for e in entries] + item_options.lookups:
            if entry and not entry.startswith(("%(", "$(")):
                forced |= forced_candidates(entry, source, item_folders)
    references = {resolved(project.parent / item["Identity"]) for item in items.get("ProjectReference", [])}

    # Link and Lib: what the linker or librarian takes in. The probe adds one item of each type to a C++ project; a
    # C++ project without them means the probe was not imported, and the library check would see nothing.
    link_items = items.get("Link", []) + items.get("Lib", [])
    if properties.get("ConfigurationType") and len(link_items) < 2:
        raise AssertionError(f"{project} evaluated no Link and Lib probe items; the library check would see nothing")
    libraries = set()
    library_folders = paths(properties.get("LibraryPath", ""))
    for item in link_items:
        libraries |= paths(item.get("AdditionalDependencies", ""))
        library_folders |= paths(item.get("AdditionalLibraryDirectories", ""))
        link_options = Options(project, item.get("AdditionalOptions", ""))
        options |= link_options.paths
        library_folders |= link_options.folders
    output = None
    if properties.get("OutDir") and properties.get("TargetName"):
        output = resolved(project.parent / properties["OutDir"] /
                          (properties["TargetName"] + properties.get("TargetExt", "")))
    evaluated = Evaluated(folders, forced, sources, references, options, libraries, library_folders, output)
    return evaluated, items["ClCompile"]


def inside(path, folder):
    return path == folder.resolve() or folder.resolve() in path.parents


def rules(name):
    """(the rule's wording, the folders module `name` may not touch, the projects it may not reach)."""
    if name in ENGINE:
        return ("an engine library may not see, compile or reach recto-ui (D13)", list(UI.values()),
                {PROJECTS["recto-ui"].resolve()})
    return ("recto-ui may not see, compile or reach an engine library (D13)", list(ENGINE.values()),
            {PROJECTS[n].resolve() for n in ENGINE})


def violations(name, configuration, evaluated, chains, outputs=frozenset()):
    """Every crossing for module `name`; `chains` maps each reachable project to the reference chain that reaches it, and
    `outputs` holds the library files the forbidden projects build in this configuration (see `forbidden_outputs`)."""
    rule, forbidden_folders, forbidden_projects = rules(name)
    where = f"{name} ({configuration})"
    # A forbidden library is reached by its path, by its output folder, or by its file name looked up along the library
    # folders (`recto-ui.lib`, or `recto-ui`, which /DEFAULTLIB completes with .lib).
    names = {output.name.lower() for output in outputs} | {output.stem.lower() for output in outputs}
    output_folders = [output.parent for output in outputs]

    def crosses(path):
        return any(inside(path, bad) for bad in forbidden_folders)

    def links(path):
        return crosses(path) or path.name.lower() in names or any(inside(path, bad) for bad in output_folders)

    found = [f"{where}: include folder {p} breaks: {rule}" for p in sorted(evaluated.folders) if crosses(p)]
    found += [f"{where}: forced include {p} breaks: {rule}" for p in sorted(evaluated.forced) if crosses(p)]
    found += [f"{where}: tool option names {p} breaks: {rule}" for p in sorted(evaluated.options) if links(p)]
    found += [f"{where}: compiles {p} breaks: {rule}" for p in sorted(evaluated.sources) if crosses(p)]
    found += [f"{where}: links {p} breaks: {rule}" for p in sorted(evaluated.libraries) if links(p)]
    found += [f"{where}: library folder {p} breaks: {rule}" for p in sorted(evaluated.library_folders) if links(p)]
    found += [f"{where}: reaches {' -> '.join(c.stem for c in chains[p])} breaks: {rule}"
              for p in sorted(chains) if p in forbidden_projects]
    return found


def forbidden_outputs(tool, name, configuration, platform, cache):
    """The library files the projects module `name` may not reach build in this configuration."""
    found = set()
    for project in sorted(rules(name)[2]):
        key = (project, configuration, platform)
        if key not in cache:
            cache[key] = evaluate(tool, project, configuration, platform)[0]
        if cache[key].output is None:
            raise AssertionError(f"{project} evaluated no OutDir and TargetName; its library cannot be recognized")
        found.add(cache[key].output)
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
                    outputs = forbidden_outputs(self.tool, name, configuration, platform, cache)
                    self.assertEqual(violations(name, f"{configuration}|{platform}", cache[key], chains, outputs), [])

    def planted(self, body, *properties, definitions=None):
        """A throwaway project with `body` in its ItemGroup, evaluated as Debug|x64. With `definitions`, it also has that
        ItemDefinitionGroup and imports the hook a C++ project imports after its targets (where the link probe goes)."""
        with tempfile.TemporaryDirectory() as folder:
            project = pathlib.Path(folder) / "planted.vcxproj"
            groups = "".join(f"  <PropertyGroup>{p}</PropertyGroup>\n" for p in properties)
            if definitions is not None:
                groups += f"  <ItemDefinitionGroup>{definitions}</ItemDefinitionGroup>\n"
            hook = '  <Import Project="$(ForceImportAfterCppTargets)" />\n' if definitions is not None else ""
            project.write_text('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                               f"{groups}  <ItemGroup>\n{body}\n  </ItemGroup>\n{hook}</Project>\n", encoding="utf-8")
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
        # /I and /external:I folders also join the search path, and -FI is also a forced include.
        searched = [ui.resolve(), (ui / "include").resolve(), (ui / "include" / "recto").resolve()]
        named = [(ui / "include").resolve(), (ui / "pch.h").resolve(), ui.resolve(), (ui / "ui.ifc").resolve()]
        self.assertEqual(violations("recto-core", "Debug|x64", evaluated, chains),
                         [*[f"recto-core (Debug|x64): include folder {p} breaks: {rule}" for p in sorted(searched)],
                          f"recto-core (Debug|x64): forced include {(ui / 'pch.h').resolve()} breaks: {rule}",
                          *[f"recto-core (Debug|x64): tool option names {p} breaks: {rule}" for p in sorted(named)]])

    def test_an_engine_project_that_links_the_toolkit_library_fails(self):
        # No ProjectReference, yet the librarian bundles recto-ui.lib (by name, along its output folder) and the linker
        # takes a toolkit object and /DEFAULTLIB:recto-ui: each one comes from the Lib and Link item definitions.
        ui = REPO / "src" / "ui"
        outputs = forbidden_outputs(self.tool, "recto-core", "Debug", "x64", {})
        (library,) = outputs
        self.assertEqual(library.name.lower(), "recto-ui.lib")
        evaluated, chains = self.planted(
            '    <ClCompile Include="a.cpp" />', "<ConfigurationType>StaticLibrary</ConfigurationType>",
            definitions=(
                "<Lib><AdditionalDependencies>Recto-UI.lib;kernel32.lib;%(AdditionalDependencies)</AdditionalDependencies>"
                f"<AdditionalLibraryDirectories>{library.parent}</AdditionalLibraryDirectories></Lib>"
                f"<Link><AdditionalDependencies>{ui / 'ui.obj'}</AdditionalDependencies>"
                "<AdditionalOptions>/NOLOGO /defaultlib:recto-ui</AdditionalOptions></Link>"))
        folder = next(iter(evaluated.sources)).parent
        rule = "an engine library may not see, compile or reach recto-ui (D13)"
        self.assertEqual(violations("recto-core", "Debug|x64", evaluated, chains, outputs), [
            f"recto-core (Debug|x64): tool option names {folder / 'recto-ui'} breaks: {rule}",
            *[f"recto-core (Debug|x64): links {p} breaks: {rule}"
              for p in sorted([folder / "Recto-UI.lib", (ui / "ui.obj").resolve()])],
            f"recto-core (Debug|x64): library folder {library.parent} breaks: {rule}"])
        # The same project without those definitions links nothing it may not.
        clean, clean_chains = self.planted('    <ClCompile Include="a.cpp" />',
                                           "<ConfigurationType>StaticLibrary</ConfigurationType>", definitions="")
        self.assertEqual(violations("recto-core", "Debug|x64", clean, clean_chains, outputs), [])

    def test_a_cpp_project_without_the_link_probe_is_refused(self):
        # A C++ project whose Link and Lib settings cannot be read fails the check instead of passing it unseen.
        with self.assertRaisesRegex(AssertionError, "no Link and Lib probe items"):
            self.planted('    <ClCompile Include="a.cpp" />', "<ConfigurationType>StaticLibrary</ConfigurationType>")

    def test_quoted_paths_and_an_option_forced_include_found_along_the_search_path_fail(self):
        # /I"dir" is /Idir to cl, and a quoted AdditionalIncludeDirectories entry is the folder itself; /FI in the
        # options is looked up along the include folders (here src, added by the options themselves) like /FI anywhere.
        src, ui = REPO / "src", REPO / "src" / "ui"
        evaluated, chains = self.planted(
            f'    <ClCompile Include="a.cpp"><AdditionalIncludeDirectories>"{ui / "include" / "recto"}";%(AdditionalIncludeDirectories)'
            f'</AdditionalIncludeDirectories><AdditionalOptions>/I"{ui / "include"}" -I "{src}" /FIui/include/recto/ui/ui.h '
            '%(AdditionalOptions)</AdditionalOptions></ClCompile>')
        rule = "an engine library may not see, compile or reach recto-ui (D13)"
        found = violations("recto-core", "Debug|x64", evaluated, chains)
        self.assertIn(f"recto-core (Debug|x64): include folder {(ui / 'include').resolve()} breaks: {rule}", found)
        self.assertIn(f"recto-core (Debug|x64): include folder {(ui / 'include' / 'recto').resolve()} breaks: {rule}", found)
        self.assertIn(f"recto-core (Debug|x64): forced include {(ui / 'include' / 'recto' / 'ui' / 'ui.h').resolve()} "
                      f"breaks: {rule}", found)
        self.assertIn(f"recto-core (Debug|x64): tool option names {(ui / 'include').resolve()} breaks: {rule}", found)

    def test_options_split_like_the_c_runtime(self):
        self.assertEqual(command_line_tokens(r'/I"a b" -FI x.h "c d"e /D"q\"r" \\srv\share  /x"" ""'),
                         ["/Ia b", "-FI", "x.h", "c de", '/Dq"r', r"\\srv\share", "/x", ""])

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
