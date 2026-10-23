"""Prove the D13 boundary between the engine libraries and recto-ui from what the compiler and the librarian report.

D10 T01 §1. After a build of one configuration, this reads the tools' own records, so no spelling of an include, an
option or a linker directive can matter:

- Includes: every translation unit of recto-pdf, recto-core, recto-services and recto-ui is compiled with
  /sourceDependencies (`GenerateSourceDependencies` in Directory.Build.targets), so cl writes a JSON report per source
  listing every file it actually used: includes and forced includes, the precompiled header, imported modules and
  header units with their compiled BMIs. No engine unit may have used a file under src/ui or recto-ui's IntDir or
  OutDir, and no recto-ui unit one under src/pdf, src/core, src/services or their IntDirs and OutDirs. Each unit's
  report is read where its evaluated SourceDependenciesFile puts it; a compiled unit (ExcludedFromBuild items are not
  compiled) without generation enabled or without its report is refused: the proof covers every unit.
- Links: `dumpbin /directives` on each module library lists the linker directives its objects carry
  (`#pragma comment(lib)` and `comment(linker)` as the compiler resolved them); none may name the other side's library
  by its evaluated file name, stem or folder. `lib /list` lists the members the librarian put in it; each must be an
  object the project compiled itself (an allow list), so a merged library or an added object is refused wherever it is.
- Symbols: `dumpbin /symbols` of each module library; no symbol a module needs from outside itself (or forces with
  /INCLUDE:) may be one the other side defines. A library only contributes code that resolves an undefined symbol, so
  this holds whatever library names reach the linker.
- The projects are evaluated with every global property the build used (`--property Name=Value`, one per /p:).

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


class Unit:
    """One compiled source: where cl writes its object and its /sourceDependencies report, and whether it writes one."""

    def __init__(self, source, obj, report, generates):
        self.source, self.obj, self.report, self.generates = source, obj, report, generates


class Project:
    """One module project in one configuration: its compiled units and where its outputs go."""

    def __init__(self, name, units, int_dir, library):
        self.name, self.units, self.int_dir, self.library = name, units, int_dir, library

    @property
    def sources(self):
        return {unit.source for unit in self.units}


def output_path(project_folder, value, filename, extension, completes):
    """A cl output switch's evaluated value as one file, the way cl names it: a value ending in a separator is a folder,
    so the file is `<folder>/<filename><extension>`; a file name is used as written, except that a switch that
    `completes` (/Fo) adds the extension to a name that has none. /sourceDependencies does not: `deps` is `deps`."""
    if not value:
        raise EvidenceError(f"no output location evaluated for {filename}")
    if value.endswith(("\\", "/")):
        return normalized(pathlib.Path(project_folder) / value / f"{filename}{extension}")
    path = pathlib.Path(project_folder) / value
    return normalized(path.with_suffix(extension) if completes and not path.suffix else path)


def compiled_units(project_folder, items):
    """The ClCompile items the build compiles (an item excluded from the build is never compiled), with their evaluated
    object file, report location and GenerateSourceDependencies setting."""
    units = []
    for item in items:
        if item.get("ExcludedFromBuild", "").strip().lower() == "true":
            continue
        source = normalized(pathlib.Path(project_folder) / item["Identity"])
        name = pathlib.Path(item["Identity"]).name
        stem = pathlib.Path(item["Identity"]).stem
        obj = output_path(project_folder, item.get("ObjectFileName", ""), stem, ".obj", completes=True)
        report = output_path(project_folder, item.get("SourceDependenciesFile", ""), name, ".json", completes=False)
        units.append(Unit(source, obj, report, item.get("GenerateSourceDependencies", "").strip().lower() == "true"))
    return units


def evaluation_arguments(msbuild, project, configuration, platform, properties):
    """MSBuild's evaluation of `project` with the same global properties the build used (EnableASAN above all: an
    item, a setting or an output path may depend on it)."""
    return [msbuild, project, f"-p:Configuration={configuration}", f"-p:Platform={platform}",
            *[f"-p:{p}" for p in properties], "-getItem:ClCompile", "-getProperty:IntDir", "-getProperty:OutDir",
            "-getProperty:TargetName", "-getProperty:TargetExt", "-nologo"]


def evaluate(msbuild, name, configuration, platform, properties=()):
    project = PROJECTS[name]
    data = json.loads(run(evaluation_arguments(msbuild, project, configuration, platform, properties)))
    props = data["Properties"]
    units = compiled_units(project.parent, data["Items"].get("ClCompile", []))
    if not units:
        raise EvidenceError(f"{name} evaluated no compiled ClCompile items")
    int_dir = normalized(project.parent / props["IntDir"])
    library = normalized(project.parent / props["OutDir"] / (props["TargetName"] + props["TargetExt"]))
    return Project(name, units, int_dir, library)


def report_paths(report):
    """Every file a /sourceDependencies report says the compiler used: the source, its includes, the precompiled
    header it used and the module interfaces and header units it imported (their sources and compiled BMIs)."""
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
    return normalized(data["Source"]), found


def read_report(path):
    """The source a /sourceDependencies report file is about, and every path it lists."""
    try:
        report = json.loads(pathlib.Path(path).read_text(encoding="utf-8-sig"))
    except (OSError, ValueError) as error:
        raise EvidenceError(f"cannot read the /sourceDependencies report {path}: {error}") from error
    if not isinstance(report, dict):
        raise EvidenceError(f"{path} is not a /sourceDependencies report")
    return report_paths(report)


def include_crossings(name, units, forbidden_folders):
    """Crossings in the /sourceDependencies reports of module `name`'s compiled `units`: any file under a forbidden
    folder (the other side's sources, IntDirs and OutDirs, where its precompiled headers and BMIs are) that a unit used.
    Each unit's report is read from exactly where its evaluated SourceDependenciesFile puts it, so an older report left
    elsewhere is never read in its place. A unit that does not generate a report, or whose report is missing or about
    another source, is an EvidenceError: the proof covers every compiled unit."""
    found, missing = [], []
    for unit in sorted(units, key=lambda u: str(u.source)):
        if not unit.generates:
            raise EvidenceError(f"{name}: {unit.source} is compiled without GenerateSourceDependencies, so the compiler "
                                "reports nothing about it")
        if not unit.report.is_file():
            missing.append(str(unit.report))
            continue
        source, opened = read_report(unit.report)
        if source != unit.source:
            raise EvidenceError(f"{name}: {unit.report} reports on {source}, not {unit.source}; two sources share a "
                                "report name")
        found += [f"{name}: {unit.source} used {normalized(p)}" for p in opened
                  if any(inside(p, folder) for folder in forbidden_folders)]
    if missing:
        raise EvidenceError(f"{name}: no /sourceDependencies report at {', '.join(missing)}; rebuild the configuration")
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
    """Linker directives in `library` that name one of `forbidden_libraries` (the other side's evaluated outputs) by
    file name, stem or path, or any file under `forbidden_folders`."""
    names = library_names(forbidden_libraries)
    found = []
    for line in run([dumpbin, "/nologo", "/directives", library]).splitlines():
        text = line.strip()
        if not text.startswith(("/", "-")):
            continue
        for token in text.split():
            spelled = token.replace('"', "")
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


def symbol_table(library, dumpbin):
    """(defined, undefined) external symbols of `library`'s members, from `dumpbin /symbols`, plus the symbols its
    /INCLUDE: directives force the linker to resolve (counted as undefined)."""
    defined, undefined = set(), set()
    for line in run([dumpbin, "/nologo", "/symbols", library]).splitlines():
        left, bar, right = line.partition("|")
        fields = left.split()
        if not bar or "External" not in fields or len(fields) < 3:
            continue
        symbol = right.strip().split(" ", 1)[0]
        # UNDEF with a value is a COMMON symbol, which this member defines.
        (undefined if "UNDEF" in fields and int(fields[1], 16) == 0 else defined).add(symbol)
    for line in run([dumpbin, "/nologo", "/directives", library]).splitlines():
        for token in line.split():
            if token[1:].lower().startswith("include:"):
                undefined.add(token.split(":", 1)[1].strip('"'))
    return defined, undefined


def symbol_crossings(name, table, forbidden_tables):
    """Symbols module `name` needs from outside itself that the other side defines. A library only contributes code
    that resolves an undefined symbol, so this is the link-level proof, whatever names or directives reach the linker:
    a forward declaration of an engine function in recto-ui (or the reverse) shows here and nowhere else."""
    defined, undefined = table
    needed = undefined - defined
    found = []
    for other, (other_defined, _) in sorted(forbidden_tables.items()):
        found += [f"{name}: needs {symbol}, which {other} defines" for symbol in sorted(needed & other_defined)]
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


def check(configuration, platform, vs=None, properties=()):
    vs = visual_studio(vs)
    tools = tool_folder(vs)
    msbuild = vs / "MSBuild" / "Current" / "Bin" / "MSBuild.exe"
    projects = {name: evaluate(msbuild, name, configuration, platform, properties) for name in PROJECTS}
    found = []
    for name, project in projects.items():
        folders, names = forbidden(name)
        if not project.library.is_file():
            raise EvidenceError(f"{project.library} does not exist; build {configuration}|{platform} first")
        others = [projects[n] for n in names]
        outputs = [p for other in others for p in (other.int_dir, other.library.parent)]
        found += include_crossings(name, project.units, folders + outputs)
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
    parser.add_argument("--property", action="append", default=[], metavar="NAME=VALUE",
                        help="a global MSBuild property the build used (EnableASAN=true); repeat for each")
    args = parser.parse_args(argv)
    try:
        found = check(args.configuration, args.platform, args.vs, args.property)
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
