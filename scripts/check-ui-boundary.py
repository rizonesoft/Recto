"""Prove the D13 boundary between the engine libraries and recto-ui from what the compiler and the librarian report.

D10 T01 §1. After a build of one configuration, this reads the tools' own records, so no spelling of an include, an
option or a linker directive can matter:

- Includes: every translation unit of recto-pdf, recto-core, recto-services and recto-ui is compiled with
  /sourceDependencies (`GenerateSourceDependencies` in Directory.Build.targets), so cl writes a JSON report per source
  into the project's IntDir listing every file it actually opened, forced includes, header units and module interfaces
  included. No engine translation unit may have opened a file under src/ui, and no recto-ui one a file under src/pdf,
  src/core or src/services. A compiled source without a report is refused: the proof must cover every source.
- Links: `dumpbin /directives` on each module library lists the linker directives its objects carry
  (`#pragma comment(lib)` and `comment(linker)` as the compiler resolved them); none may name the other side's library.
  `lib /list` lists the members the librarian put in it; none may come from the other side's IntDir, OutDir or source
  folder (a library named in Lib.AdditionalDependencies is merged member by member).

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


class Project:
    """One module project in one configuration: its compiled sources and where its outputs go."""

    def __init__(self, name, sources, int_dir, library):
        self.name, self.sources, self.int_dir, self.library = name, sources, int_dir, library


def evaluate(msbuild, name, configuration, platform):
    project = PROJECTS[name]
    data = json.loads(run([msbuild, project, f"-p:Configuration={configuration}", f"-p:Platform={platform}",
                           "-getItem:ClCompile", "-getProperty:IntDir", "-getProperty:OutDir",
                           "-getProperty:TargetName", "-getProperty:TargetExt", "-nologo"]))
    properties = data["Properties"]
    sources = {normalized(project.parent / item["Identity"]) for item in data["Items"].get("ClCompile", [])}
    if not sources:
        raise EvidenceError(f"{name} evaluated no ClCompile items")
    int_dir = normalized(project.parent / properties["IntDir"])
    library = normalized(project.parent / properties["OutDir"] / (properties["TargetName"] + properties["TargetExt"]))
    return Project(name, sources, int_dir, library)


def report_paths(report):
    """Every file a /sourceDependencies report says the compiler opened or imported, the source itself included."""
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

    for key in ("Source", "Includes", "ImportedModules", "ImportedHeaderUnits"):
        walk(data.get(key, []))
    return normalized(data["Source"]), found


def include_crossings(name, sources, report_folder, forbidden_folders):
    """Crossings in the /sourceDependencies reports under `report_folder` for the compiled `sources` of module `name`.
    Reports for sources no longer compiled are ignored; a compiled source without a report is an EvidenceError."""
    reports = {}
    for path in sorted(pathlib.Path(report_folder).glob("*.json")):
        try:
            report = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, ValueError) as error:
            raise EvidenceError(f"cannot read {path}: {error}") from error
        if isinstance(report, dict) and "Data" in report:
            source, opened = report_paths(report)
            reports[source] = opened
    missing = sorted(str(s) for s in sources if normalized(s) not in reports)
    if missing:
        raise EvidenceError(f"{name}: no /sourceDependencies report for {', '.join(missing)} in {report_folder}; "
                            "rebuild the configuration")
    found = []
    for source in sorted(normalized(s) for s in sources):
        for opened in reports[source]:
            if any(inside(opened, folder) for folder in forbidden_folders):
                found.append(f"{name}: {source} opened {normalized(opened)}")
    return sorted(set(found))


def directive_crossings(name, library, forbidden_stems, dumpbin):
    """Linker directives in `library` that name a forbidden library (`recto-ui`, `recto-ui.lib`, any folder)."""
    found = []
    for line in run([dumpbin, "/nologo", "/directives", library]).splitlines():
        text = line.strip()
        if not text.startswith(("/", "-")):
            continue
        for token in text.split():
            lowered = token.lower().replace('"', "")
            value = lowered.split(":", 1)[1] if lowered[1:].startswith("defaultlib:") else None
            if value is not None:
                stem = pathlib.PureWindowsPath(value).name
                stem = stem[:-4] if stem.endswith(".lib") else stem
                hit = stem in forbidden_stems
            else:
                hit = any(s in lowered for s in forbidden_stems)
            if hit:
                found.append(f"{name}: {normalized(library)} carries the linker directive {token}")
    return sorted(set(found))


def member_crossings(name, library, project_folder, forbidden_folders, lib):
    """Members of static library `library` that came from a forbidden folder (a source folder, IntDir or OutDir)."""
    found = []
    for line in run([lib, "/nologo", "/list", library]).splitlines():
        member = line.strip()
        if not member:
            continue
        path = normalized(pathlib.Path(project_folder) / member)
        if any(inside(path, folder) for folder in forbidden_folders):
            found.append(f"{name}: {normalized(library)} holds the member {member}")
    return sorted(set(found))


def check(configuration, platform, vs=None):
    vs = visual_studio(vs)
    tools = tool_folder(vs)
    msbuild = vs / "MSBuild" / "Current" / "Bin" / "MSBuild.exe"
    projects = {name: evaluate(msbuild, name, configuration, platform) for name in PROJECTS}
    found = []
    for name, project in projects.items():
        folders, names = forbidden(name)
        if not project.library.is_file():
            raise EvidenceError(f"{project.library} does not exist; build {configuration}|{platform} first")
        found += include_crossings(name, project.sources, project.int_dir, folders)
        found += directive_crossings(name, project.library, names, tools / "dumpbin.exe")
        outputs = [p for n in names for p in (projects[n].int_dir, projects[n].library.parent)]
        found += member_crossings(name, project.library, PROJECTS[name].parent, folders + outputs, tools / "lib.exe")
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--configuration", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--vs", help="the Visual Studio installation folder (default: the newest Visual Studio 2026)")
    args = parser.parse_args(argv)
    try:
        found = check(args.configuration, args.platform, args.vs)
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
