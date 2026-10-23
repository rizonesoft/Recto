"""Prove the D13 boundary between the engine libraries and recto-ui from what the compiler and the librarian report.

D10 T01 §1. After a build of one configuration, this reads the tools' own records, so no spelling of an include, an
option or a linker directive can matter:

- Includes: every translation unit of recto-pdf, recto-core, recto-services and recto-ui is compiled with
  /sourceDependencies (`GenerateSourceDependencies` in Directory.Build.targets), so cl writes a JSON report per source
  listing every file it actually used: includes and forced includes, the precompiled header, imported modules and
  header units with their compiled BMIs. No engine unit may have used a file under src/ui or recto-ui's IntDir or
  OutDir, and no recto-ui unit one under src/pdf, src/core, src/services or their IntDirs and OutDirs; nor a
  precompiled header the other side created, wherever it was written, nor a module the other side provides, by its
  name, wherever its BMI was written. Each unit's report is read where the build's recorded SourceDependenciesFile put
  it; a compiled unit (ExcludedFromBuild items are not compiled) without generation enabled or without its report is
  refused: the proof covers every unit.
- Links: `dumpbin /directives` on each module library lists the linker directives its objects carry
  (`#pragma comment(lib)` and `comment(linker)` as the compiler resolved them); none may name the other side's library
  by its recorded file name, stem or folder. `lib /list` lists the members the librarian put in it; each must be an
  object the project compiled itself (an allow list), so a merged library or an added object is refused wherever it is.
- Symbols: `dumpbin /symbols` of each module library; no symbol a module needs from outside itself (or makes the
  linker resolve with a /INCLUDE:, /EXPORT: or /ALTERNATENAME: directive) may be one the other side owns (its
  recto:: namespace, or an extern "C" Recto* name) and defines, however
  it stores it. A library only contributes code that resolves an undefined symbol, so this holds whatever library
  names reach the linker; std::, CRT and SDK names never count, and shared inline or template code is defined by each
  module that uses it, so it is never needed from the other side.
- What the build compiled comes from the build itself: the RectoBoundaryManifest target in Directory.Build.targets
  runs after ClCompile in each module project and records every ClCompile item with the metadata it was compiled with
  (target-time changes included) and the IntDir and library it wrote, in build/boundary/<platform>/<configuration>/.
  build/build.ps1 empties that folder before each configuration, so a manifest is always from the build being checked;
  a missing manifest is refused. Nothing is re-evaluated, so neither a global property nor a target that changes
  ClCompile metadata before compiling can make the check read a different build from the one that ran.

The text scanners (Ui.Project.IncludeBoundary, Ui.Project.NoWinRtUiTypes, tests/scripts/test_ui_module_boundary.py)
stay as fast early warnings; this check is the proof. build/build.ps1 runs it after each configuration it builds.

Usage: python scripts/check-ui-boundary.py --platform x64 --configuration Debug [--vs <Visual Studio folder>]
Exit 0: no crossing. Exit 1: crossings, each printed. Exit 2: the evidence is missing or unreadable.
"""
import argparse
import json
import os
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
ENGINE = {"recto-pdf": REPO / "src" / "pdf", "recto-core": REPO / "src" / "core", "recto-services": REPO / "src" / "services"}
UI = {"recto-ui": REPO / "src" / "ui"}
FOLDERS = {**ENGINE, **UI}
PROJECTS = {name: folder / f"{name}.vcxproj" for name, folder in FOLDERS.items()}


class EvidenceError(Exception):
    """The tools' records needed for the proof are missing or unreadable."""


def normalized(path):
    """`path` as the file system names it (long names for 8.3 ones, links followed) and in lower case: cl writes its
    reports in lower case, and Windows paths ignore case."""
    return pathlib.Path(os.path.normcase(os.path.realpath(str(path))))


def inside(path, folder):
    path, folder = normalized(path), normalized(folder)
    return path == folder or folder in path.parents


def forbidden(name):
    """(source folders, project names) module `name` may not touch."""
    if name in ENGINE:
        return list(UI.values()), list(UI)
    return list(ENGINE.values()), list(ENGINE)


def visual_studio(vs=None):
    if vs:
        return pathlib.Path(vs)
    vswhere = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not vswhere.is_file():
        raise EvidenceError("vswhere.exe was not found; pass --vs")
    found = subprocess.run([str(vswhere), "-version", "[18.0,19.0)", "-latest", "-property", "installationPath"],
                           capture_output=True, text=True).stdout.strip()
    if not found:
        raise EvidenceError("no Visual Studio 2026 was found; pass --vs")
    return pathlib.Path(found.splitlines()[0])


def tool_folder(vs):
    """The host x64 bin folder of the MSVC tools the v145 toolset builds with."""
    build = vs / "VC" / "Auxiliary" / "Build"
    for name in ("Microsoft.VCToolsVersion.v145.default.txt", "Microsoft.VCToolsVersion.default.txt"):
        if (build / name).is_file():
            version = (build / name).read_text(encoding="utf-8").strip()
            folder = vs / "VC" / "Tools" / "MSVC" / version / "bin" / "Hostx64" / "x64"
            if (folder / "dumpbin.exe").is_file():
                return folder
    raise EvidenceError(f"no MSVC tools with dumpbin.exe under {vs}")


def run(arguments):
    out = subprocess.run([str(a) for a in arguments], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if out.returncode != 0:
        raise EvidenceError(f"{pathlib.Path(arguments[0]).name} failed ({out.returncode}): {out.stdout[-400:]}{out.stderr[-400:]}")
    return out.stdout


def command_line_tokens(text):
    """`text` split into arguments the way the Microsoft C runtime splits a command line, which is how cl, link and lib
    read their options and their directives: white space outside quotes separates; a quote opens or closes quoting anywhere in an argument
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


def directive_tokens(line):
    """The linker directives on one line of `dumpbin /directives`. dumpbin prints each directive on its own line and
    without the quotes it was written with (`/DEFAULTLIB:ui kit.lib` for "ui kit.lib"), while a quoted spelling may
    also appear; so the line is split as LINK splits a command line and every piece that does not start a switch is
    joined back to the switch before it."""
    tokens = []
    for token in command_line_tokens(line.strip()):
        if tokens and token[:1] not in ("/", "-"):
            tokens[-1] += " " + token
        else:
            tokens.append(token)
    return tokens


class Unit:
    """One compiled source as the build compiled it: where cl wrote its object and its /sourceDependencies report,
    whether it wrote one, and the precompiled header it created, if any."""

    def __init__(self, source, obj, report, generates, pch=None, options=""):
        self.source, self.obj, self.report, self.generates, self.pch = source, obj, report, generates, pch
        self.options = options


class Project:
    """One module project in one configuration as the build saw it: its compiled units and where its outputs went."""

    def __init__(self, name, units, int_dir, library):
        self.name, self.units, self.int_dir, self.library = name, units, int_dir, library

    @property
    def sources(self):
        return {unit.source for unit in self.units}

    @property
    def produced(self):
        """Files this project's compilation produced that another unit could consume: its precompiled headers."""
        return {unit.pch for unit in self.units if unit.pch is not None}


def output_path(project_folder, value, filename, extension, completes):
    """A cl output switch's value as one file, the way cl names it. /sourceDependencies treats a value naming an
    existing folder as that folder (with or without a trailing separator) and any other value as the file itself;
    /Fo treats a value ending in a separator as a folder and completes a bare file name with its extension
    (`completes`). In a folder, the file is `<folder>/<filename><extension>`."""
    if not value:
        raise EvidenceError(f"no output location recorded for {filename}")
    path = pathlib.Path(project_folder) / value
    if value.endswith(("\\", "/")) or (not completes and path.is_dir()):
        return normalized(path / f"{filename}{extension}")
    return normalized(path.with_suffix(extension) if completes and not path.suffix else path)


# One line per fact, written by the RectoBoundaryManifest target in Directory.Build.targets after ClCompile ran, so
# every value is what the build compiled with, target-time changes included:
#   project|<project file>|<IntDir>|<library file>
#   unit|<source>|<ExcludedFromBuild>|<GenerateSourceDependencies>|<SourceDependenciesFile>|<ObjectFileName>|
#        <PrecompiledHeader>|<PrecompiledHeaderOutputFile>|<AdditionalOptions, the rest of the line>
MANIFESTS = REPO / "build" / "boundary"


def compiled_units(project_folder, items):
    """The units the build compiled from its recorded ClCompile items (dicts with the manifest's fields): an item
    excluded from the build is never compiled."""
    units = []
    for item in items:
        if item.get("ExcludedFromBuild", "").strip().lower() == "true":
            continue
        source = normalized(pathlib.Path(project_folder) / item["Identity"])
        name = pathlib.Path(item["Identity"]).name
        stem = pathlib.Path(item["Identity"]).stem
        obj = output_path(project_folder, item.get("ObjectFileName", ""), stem, ".obj", completes=True)
        report = output_path(project_folder, item.get("SourceDependenciesFile", ""), name, ".json", completes=False)
        pch = None
        if item.get("PrecompiledHeader", "").strip().lower() == "create" and item.get("PrecompiledHeaderOutputFile"):
            pch = normalized(pathlib.Path(project_folder) / item["PrecompiledHeaderOutputFile"])
        units.append(Unit(source, obj, report, item.get("GenerateSourceDependencies", "").strip().lower() == "true",
                          pch, item.get("AdditionalOptions", "")))
    return units


UNIT_FIELDS = ("Identity", "ExcludedFromBuild", "GenerateSourceDependencies", "SourceDependenciesFile",
               "ObjectFileName", "PrecompiledHeader", "PrecompiledHeaderOutputFile", "AdditionalOptions")


def read_manifest(name, path):
    """Project `name` as the build recorded it in its manifest at `path`. A missing manifest means the build did not
    run the recording target for this project (or did not build it), so there is nothing to prove from: refused."""
    if not path.is_file():
        raise EvidenceError(f"{name}: no build manifest at {path}; build the configuration with build/build.ps1 "
                            "(the RectoBoundaryManifest target in Directory.Build.targets writes it)")
    project = None
    items = []
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        fields = line.split("|")
        if fields[0] == "project" and len(fields) == 4:
            project = fields[1:]
        elif fields[0] == "unit" and len(fields) >= 1 + len(UNIT_FIELDS):
            # AdditionalOptions is last and may itself hold a "|".
            values = fields[1:len(UNIT_FIELDS)] + ["|".join(fields[len(UNIT_FIELDS):])]
            items.append(dict(zip(UNIT_FIELDS, values)))
        elif line.strip():
            raise EvidenceError(f"{path}: unreadable manifest line {line!r}")
    if project is None:
        raise EvidenceError(f"{path}: the manifest names no project")
    folder = pathlib.Path(project[0]).parent
    units = compiled_units(folder, items)
    if not units:
        raise EvidenceError(f"{name}: the build compiled no ClCompile items")
    return Project(name, units, normalized(folder / project[1]), normalized(folder / project[2]))


def report_paths(report):
    """What a /sourceDependencies report says about its unit: the source, every file the compiler used (includes and
    forced includes, the precompiled header, imported modules and header units with their compiled BMIs), the module
    the unit provides, and the names of the modules it imports."""
    data = report.get("Data")
    if not isinstance(data, dict) or "Source" not in data:
        raise EvidenceError("a /sourceDependencies report has no Data.Source")
    found = []

    def walk(value):
        if isinstance(value, str):
            if value:
                found.append(value)
        elif isinstance(value, list):
            for entry in value:
                walk(entry)
        elif isinstance(value, dict):
            for entry in value.values():
                walk(entry)

    for key, value in data.items():
        if key != "ProvidedModule":
            walk(value)
    imported = {entry.get("Name", "") for entry in data.get("ImportedModules", []) if isinstance(entry, dict)}
    return normalized(data["Source"]), found, data.get("ProvidedModule", "") or "", imported - {""}


def read_report(path):
    """The report file at `path`, as report_paths reads it."""
    try:
        report = json.loads(pathlib.Path(path).read_text(encoding="utf-8-sig"))
    except (OSError, ValueError) as error:
        raise EvidenceError(f"cannot read the /sourceDependencies report {path}: {error}") from error
    if not isinstance(report, dict):
        raise EvidenceError(f"{path} is not a /sourceDependencies report")
    return report_paths(report)


def redirects_reports(options):
    """Why cl might not have written a unit's report where its SourceDependenciesFile says, or None. A
    /sourceDependencies switch in the unit's AdditionalOptions comes after the one MSBuild generates and wins (with `-`
    it sends the report to standard output, leaving an older report in place); a response file (`@file`) may hold one;
    and the CL and _CL_ environment variables add switches to every compile, read with the same argument rules (so a
    quoted spelling such as `/source"Dependencies"-` and a response file are both caught). Each is refused rather than
    guessed at."""
    sources = [("its AdditionalOptions", options)]
    sources += [(f"the {variable} environment variable", os.environ.get(variable, "")) for variable in ("CL", "_CL_")]
    for where, text in sources:
        for token in command_line_tokens(text):
            if token[:1] in ("/", "-") and token[1:].lower().startswith("sourcedependencies"):
                return f"{where} holds {token}"
            if token.startswith("@"):
                return f"{where} names the response file {token}"
    return None


def unit_reports(name, units):
    """Each compiled unit's report, read from exactly where the build wrote it, so an older report elsewhere is never
    read in its place. A unit that does not generate a report, or whose report is missing or about another source, is
    an EvidenceError: the proof covers every compiled unit."""
    reports, missing = {}, []
    for unit in sorted(units, key=lambda u: str(u.source)):
        if not unit.generates:
            raise EvidenceError(f"{name}: {unit.source} is compiled without GenerateSourceDependencies, so the compiler "
                                "reports nothing about it")
        redirected = redirects_reports(unit.options)
        if redirected:
            raise EvidenceError(f"{name}: the report for {unit.source} may not be where the build recorded it: "
                                f"{redirected}")
        if not unit.report.is_file():
            missing.append(str(unit.report))
            continue
        report = read_report(unit.report)
        if report[0] != unit.source:
            raise EvidenceError(f"{name}: {unit.report} reports on {report[0]}, not {unit.source}; two sources share a "
                                "report name")
        reports[unit.source] = report
    if missing:
        raise EvidenceError(f"{name}: no /sourceDependencies report at {', '.join(missing)}; rebuild the configuration")
    return reports


def provided_modules(name, units):
    """The C++ modules module `name`'s units provide, by name, from their reports."""
    return {report[2] for report in unit_reports(name, units).values() if report[2]}


def include_crossings(name, units, forbidden_folders, forbidden_files=(), forbidden_modules=()):
    """Crossings in the /sourceDependencies reports of module `name`'s compiled `units`. A unit may not use a file
    under a forbidden folder (the other side's sources, IntDirs and OutDirs), a file the other side's compilation
    produced wherever it lies (`forbidden_files`: its precompiled headers), or a module the other side provides by name
    (`forbidden_modules`), whatever folder its BMI was written to."""
    files = {normalized(f) for f in forbidden_files}
    found = []
    for source, (_, opened, _, imported) in unit_reports(name, units).items():
        found += [f"{name}: {source} used {normalized(p)}" for p in opened
                  if normalized(p) in files or any(inside(p, folder) for folder in forbidden_folders)]
        found += [f"{name}: {source} imports module {module}, which the other side provides"
                  for module in sorted(imported & set(forbidden_modules))]
    return sorted(set(found))


def win32_name(name):
    """A file name as Win32 resolves it: case ignored, and trailing dots and spaces dropped (`recto-ui.lib.` opens
    `recto-ui.lib`), which is how LINK finds a /DEFAULTLIB library."""
    return name.rstrip(". ").lower()


def short_name(path):
    """The 8.3 short name of an existing file, which LINK also accepts, or the long name when there is none."""
    if sys.platform != "win32" or not pathlib.Path(path).exists():
        return pathlib.Path(path).name
    import ctypes
    buffer = ctypes.create_unicode_buffer(1024)
    length = ctypes.windll.kernel32.GetShortPathNameW(str(path), buffer, len(buffer))
    return pathlib.PureWindowsPath(buffer.value).name if 0 < length < len(buffer) else pathlib.Path(path).name


def library_names(libraries):
    """Every name a linker directive could use for each of `libraries`: the file name, its short name and the stem
    (LINK adds .lib to a name with no extension), compared as Win32 compares them."""
    names = set()
    for library in libraries:
        library = pathlib.Path(library)
        names |= {win32_name(library.name), win32_name(short_name(library)), win32_name(library.stem)}
    return names


def directive_crossings(name, library, forbidden_libraries, forbidden_folders, dumpbin):
    """Linker directives in `library` that name one of `forbidden_libraries` (the other side's recorded outputs) by
    file name, stem or path, or any file under `forbidden_folders`."""
    names = library_names(forbidden_libraries)
    found = []
    for line in run([dumpbin, "/nologo", "/directives", library]).splitlines():
        text = line.strip()
        if not text.startswith(("/", "-")):
            continue
        for token in directive_tokens(text):
            spelled = token
            _, _, value = spelled.partition(":")
            candidates = [spelled, value] if value else [spelled]
            hit = False
            for candidate in candidates:
                file = win32_name(pathlib.PureWindowsPath(candidate).name)
                hit = hit or file in names or (pathlib.PureWindowsPath(candidate).is_absolute() and
                                               any(inside(candidate, folder) for folder in forbidden_folders))
            if hit:
                found.append(f"{name}: {normalized(library)} carries the linker directive {token}")
    return sorted(set(found))


# The namespace each module owns, as MSVC decorates a qualified name: recto::core::f is ?f@core@recto@@..., and any
# name nested inside (recto::core::detail::g, a member of a recto::core class) carries "@core@recto@@" too.
MODULE_NAMESPACES = {"recto-pdf": "pdf", "recto-core": "core", "recto-services": "services", "recto-ui": "ui"}
# extern "C" names a module defines carry this prefix (recto-pdf's RectoTlgEventWriteTransfer); such a name is owned
# by whichever module defines it.
EXTERN_C_PREFIX = "Recto"
class SymbolTable:
    """A library's external symbols: everything it defines and everything it leaves undefined (plus the symbols its
    /INCLUDE:, /EXPORT: and /ALTERNATENAME: directives make the linker resolve)."""

    def __init__(self, defined, undefined):
        self.defined, self.undefined = defined, undefined


def symbol_table(library, dumpbin):
    """`library`'s SymbolTable from `dumpbin /symbols` and `dumpbin /directives`."""
    defined, undefined = set(), set()
    for line in run([dumpbin, "/nologo", "/symbols", library]).splitlines():
        left, bar, right = line.partition("|")
        fields = left.split()
        if not bar or len(fields) < 3 or "External" not in fields:
            continue
        name = right.strip().split(" ", 1)[0]
        # UNDEF with a value is a COMMON symbol, which this member defines.
        (undefined if fields[2] == "UNDEF" and int(fields[1], 16) == 0 else defined).add(name)
    for line in run([dumpbin, "/nologo", "/directives", library]).splitlines():
        for token in directive_tokens(line):
            undefined |= directive_symbols(token)
    return SymbolTable(defined, undefined)


def directive_symbols(token):
    """Symbols a linker directive makes the linker resolve: /INCLUDE:symbol; /EXPORT:entry[=internal][,...] (the
    internal name, or the entry itself; a forwarder `other.dll.name` needs nothing here); /ALTERNATENAME:a=b (b)."""
    switch, _, value = token.partition(":")
    switch, value = switch.lstrip("/-").lower(), value.strip('"')
    if not value:
        return set()
    if switch == "include":
        return {value}
    if switch == "export":
        entry, _, internal = value.split(",", 1)[0].partition("=")
        target = internal or entry
        return set() if "." in target and not target.startswith("?") else {target}
    if switch == "alternatename":
        _, _, target = value.partition("=")
        return {target} if target else set()
    return set()


def owned_by(symbol, module):
    """True when `symbol` belongs to `module`: a C++ name in its namespace, or an extern "C" name with Recto's prefix
    (owned by the module that defines it). std::, the CRT, the Windows SDK and every other name belong to no module."""
    if symbol.startswith("?"):
        return f"@{MODULE_NAMESPACES[module]}@recto@@" in symbol
    return symbol.lstrip("_").startswith(EXTERN_C_PREFIX)


def symbol_crossings(name, table, forbidden_tables):
    """Symbols module `name` needs from outside itself that another module owns and defines. A library only
    contributes code that resolves an undefined symbol, so this is the link-level proof whatever names or directives
    reach the linker: a forward declaration of an engine function in recto-ui (or the reverse) shows here and nowhere
    else. Ownership decides, not how the definition is stored: an explicit instantiation or selectany data the other
    side owns sits in a shareable COMDAT section, yet a module that needs it and does not define it itself depends on
    the other side's code. Shared code is never a crossing, for two reasons: names no module owns (std::, the CRT, the
    Windows SDK, the global namespace) never count, and a module that instantiates a template or emits an inline
    function itself defines it, so it does not need it from anyone."""
    needed = table.undefined - table.defined
    found = []
    for other, other_table in sorted(forbidden_tables.items()):
        found += [f"{name}: needs {symbol}, which {other} defines" for symbol in sorted(needed & other_table.defined)
                  if owned_by(symbol, other)]
    return found


def member_crossings(name, library, project_folder, own_objects, lib):
    """Members of static library `library` that are not objects module `name` compiled itself. An allow list, so a
    member the librarian took from anywhere else (another library merged in, an object added as a Lib item, an object
    written to an unexpected folder) is refused whatever its path."""
    own = {normalized(o) for o in own_objects}
    found = []
    for line in run([lib, "/nologo", "/list", library]).splitlines():
        member = line.strip()
        if member and normalized(pathlib.Path(project_folder) / member) not in own:
            found.append(f"{name}: {normalized(library)} holds the member {member}, which {name} did not compile")
    return sorted(set(found))


def check(configuration, platform, vs=None, manifests=None):
    vs = visual_studio(vs)
    tools = tool_folder(vs)
    folder = pathlib.Path(manifests) if manifests else MANIFESTS / platform / configuration
    projects = {name: read_manifest(name, folder / f"{name}.txt") for name in PROJECTS}
    modules = {name: provided_modules(name, project.units) for name, project in projects.items()}
    found = []
    for name, project in projects.items():
        folders, names = forbidden(name)
        if not project.library.is_file():
            raise EvidenceError(f"{project.library} does not exist; build {configuration}|{platform} first")
        others = [projects[n] for n in names]
        outputs = [p for other in others for p in (other.int_dir, other.library.parent)]
        produced = {f for other in others for f in other.produced}
        found += include_crossings(name, project.units, folders + outputs, produced,
                                   {m for n in names for m in modules[n]})
        found += directive_crossings(name, project.library, [other.library for other in others], folders + outputs,
                                     tools / "dumpbin.exe")
        found += member_crossings(name, project.library, PROJECTS[name].parent, [unit.obj for unit in project.units],
                                  tools / "lib.exe")
    tables = {name: symbol_table(project.library, tools / "dumpbin.exe") for name, project in projects.items()}
    for name in projects:
        found += symbol_crossings(name, tables[name], {n: tables[n] for n in forbidden(name)[1]})
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--configuration", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--vs", help="the Visual Studio installation folder (default: the newest Visual Studio 2026)")
    parser.add_argument("--manifests", help="the folder of build manifests (default: build/boundary/<platform>/"
                        "<configuration>, where the build writes them)")
    args = parser.parse_args(argv)
    try:
        found = check(args.configuration, args.platform, args.vs, args.manifests)
    except EvidenceError as error:
        print(f"check-ui-boundary: {error}", file=sys.stderr)
        return 2
    where = f"{args.configuration}|{args.platform}"
    for line in found:
        print(f"check-ui-boundary ({where}): {line} breaks D13")
    if not found:
        print(f"check-ui-boundary ({where}): the compiler and librarian report no engine/recto-ui crossing")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
