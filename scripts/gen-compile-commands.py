#!/usr/bin/env python3
"""Generate `build/compile_commands.json` from `Recto.sln` and its `.vcxproj` projects.

    python scripts/gen-compile-commands.py           write build/compile_commands.json atomically
    python scripts/gen-compile-commands.py --check   verify build/compile_commands.json is current and valid

Self-contained stdlib Python: parses every `.vcxproj` in `Recto.sln` dynamically, expands wildcard `ClCompile` globs
(such as `ui_*_tests.cpp`), resolves portable MSVC toolset and Windows SDK `-imsvc` system header directories on
Windows without hardcoding machine paths, and writes `clang-cl` C++23 commands so `clangd` understands every module,
vcpkg header and C++/WinRT projection out of the box.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SLN = ROOT / "Recto.sln"
PROPS = ROOT / "Directory.Build.props"
OUT = ROOT / "build" / "compile_commands.json"
MSBUILD_NS = {"msb": "http://schemas.microsoft.com/developer/msbuild/2003"}
PROJECT_RE = re.compile(r'^Project\("\{[0-9A-Fa-f-]+\}"\)\s*=\s*"([^"]+)",\s*"([^"]+\.vcxproj)"', re.M)


VC_COMPONENTS = (
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
    "Microsoft.VisualStudio.Component.VC.Tools.ARM64",
)


def windowless() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def _has_msvc_headers(vs_dir: Path) -> bool:
    msvc_root = vs_dir / "VC" / "Tools" / "MSVC"
    if not msvc_root.is_dir():
        return False
    try:
        return any(d.is_dir() and (d / "include").is_dir() for d in msvc_root.iterdir())
    except OSError:
        return False


def _query_vswhere(vswhere: Path, *extra_args: str) -> list[Path]:
    try:
        done = subprocess.run(
            [str(vswhere), "-products", "*", "-sort", *extra_args, "-property", "installationPath"],
            capture_output=True, text=True, timeout=15, creationflags=windowless(),
        )
    except (OSError, subprocess.SubprocessError):
        return []
    if done.returncode != 0 or not done.stdout.strip():
        return []
    out: list[Path] = []
    for raw_line in done.stdout.splitlines():
        line = raw_line.strip()
        if line:
            cand = Path(line)
            if cand.is_dir() and cand not in out:
                out.append(cand)
    return out


def iter_vs_installs(*, require_cpp: bool = True) -> list[Path]:
    """Return installed Visual Studio roots newest-first, optionally requiring usable C++ MSVC headers."""
    candidates: list[Path] = []

    def add(cand: Path) -> None:
        if not cand.is_dir():
            return
        if require_cpp and not _has_msvc_headers(cand):
            return
        if cand not in candidates:
            candidates.append(cand)

    env_vc = os.environ.get("VCToolsInstallDir")
    if env_vc:
        vc_path = Path(env_vc).resolve()
        if vc_path.is_dir():
            for anc in (vc_path, *vc_path.parents):
                if (anc / "VC" / "Tools" / "MSVC").is_dir():
                    add(anc)
                    break

    pf86 = os.environ.get("ProgramFiles(x86)") or r"C:\Program Files (x86)"
    vswhere = Path(pf86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if vswhere.is_file():
        if require_cpp:
            for cand in _query_vswhere(vswhere, "-version", "[18.0,19.0)", "-requiresAny", "-requires", *VC_COMPONENTS):
                add(cand)
            for cand in _query_vswhere(vswhere, "-requiresAny", "-requires", *VC_COMPONENTS):
                add(cand)
        else:
            for cand in _query_vswhere(vswhere):
                add(cand)

    for base in (os.environ.get("ProgramFiles") or r"C:\Program Files", pf86):
        vs_root = Path(base) / "Microsoft Visual Studio"
        if vs_root.is_dir():
            for ver_dir in sorted(vs_root.iterdir(), reverse=True):
                if not ver_dir.is_dir():
                    continue
                for edition in ("Enterprise", "Professional", "Community", "BuildTools"):
                    add(ver_dir / edition)
    return candidates


def find_vs_install() -> Path | None:
    installs = iter_vs_installs(require_cpp=True)
    return installs[0] if installs else None


def discover_system_includes(sdk_version: str) -> list[str]:
    if os.name != "nt":
        return []
    includes: list[str] = []
    vs = find_vs_install()
    if vs is not None:
        msvc_root = vs / "VC" / "Tools" / "MSVC"
        if msvc_root.is_dir():
            versions = sorted((d for d in msvc_root.iterdir() if d.is_dir()), key=lambda p: p.name, reverse=True)
            if versions and (versions[0] / "include").is_dir():
                includes.append(str(versions[0] / "include"))
    pf86 = os.environ.get("ProgramFiles(x86)") or r"C:\Program Files (x86)"
    kits_inc = Path(pf86) / "Windows Kits" / "10" / "Include"
    if kits_inc.is_dir():
        chosen = kits_inc / sdk_version
        if not chosen.is_dir():
            versions = sorted((d for d in kits_inc.iterdir() if d.is_dir() and d.name.startswith("10.")),
                              key=lambda p: p.name, reverse=True)
            chosen = versions[0] if versions else chosen
        if chosen.is_dir():
            for sub in ("ucrt", "shared", "um", "winrt", "cppwinrt"):
                if (chosen / sub).is_dir():
                    includes.append(str(chosen / sub))
    return includes


def target_sdk_version(root: Path = ROOT) -> str:
    props = root / "Directory.Build.props"
    if props.is_file():
        m = re.search(r"<WindowsTargetPlatformVersion>([^<]+)</WindowsTargetPlatformVersion>",
                      props.read_text(encoding="utf-8"))
        if m:
            return m.group(1).strip()
    return "10.0.26100.0"


def solution_projects(sln_path: Path) -> list[tuple[str, Path]]:
    text = sln_path.read_text(encoding="utf-8-sig")
    projects = []
    for name, rel in PROJECT_RE.findall(text):
        p = (sln_path.parent / rel.replace("\\", "/")).resolve()
        if p.is_file():
            projects.append((name, p))
    return projects


def split_msbuild_list(raw: str) -> list[str]:
    out = []
    for item in raw.split(";"):
        tok = item.strip()
        if not tok or tok.startswith("%("):
            continue
        out.append(tok)
    return out


def parse_vcxproj(proj_name: str, vcxproj: Path, sys_includes: list[str], root: Path = ROOT) -> list[dict]:
    root = root.resolve()
    tree = ET.parse(vcxproj)
    root_el = tree.getroot()
    proj_dir = vcxproj.resolve().parent

    inc_dirs: list[Path] = [proj_dir]
    if (proj_dir / "include").is_dir():
        inc_dirs.append(proj_dir / "include")

    defines: list[str] = ["UNICODE", "_UNICODE", "_DEBUG", "_WIN64", "WIN32"]
    uses_vcpkg = False

    targets_file = root / "Directory.Build.targets"
    if targets_file.is_file():
        try:
            targets_root = ET.parse(targets_file).getroot()
            for group in targets_root.findall(".//msb:ItemDefinitionGroup", MSBUILD_NS) + targets_root.findall(".//ItemDefinitionGroup"):
                cond = group.attrib.get("Condition", "")
                if "EnableASAN" in cond:
                    continue
                if "Release" in cond and "Debug" not in cond:
                    continue
                if "ARM64" in cond and "x64" not in cond:
                    continue
                for def_el in group.findall(".//msb:ClCompile/msb:PreprocessorDefinitions", MSBUILD_NS) + group.findall(".//ClCompile/PreprocessorDefinitions"):
                    for d in split_msbuild_list(def_el.text or ""):
                        if "$(" not in d and d != "NDEBUG" and d not in defines:
                            defines.append(d)
        except ET.ParseError:
            pass

    for prop in root_el.findall(".//msb:VcpkgEnableManifest", MSBUILD_NS):
        if (prop.text or "").strip().lower() == "true":
            uses_vcpkg = True

    for group in root_el.findall(".//msb:ItemDefinitionGroup", MSBUILD_NS):
        cond = group.attrib.get("Condition", "")
        if "Release" in cond and "Debug" not in cond:
            continue
        if "ARM64" in cond and "x64" not in cond:
            continue
        for inc_el in group.findall(".//msb:ClCompile/msb:AdditionalIncludeDirectories", MSBUILD_NS):
            for entry in split_msbuild_list(inc_el.text or ""):
                cleaned = entry.replace("$(ProjectDir)", str(proj_dir) + "/").replace("$(SolutionDir)", str(root) + "/")
                if "$(" in cleaned:
                    continue
                cand = (proj_dir / cleaned.replace("\\", "/")).resolve()
                if cand not in inc_dirs:
                    inc_dirs.append(cand)
        for def_el in group.findall(".//msb:ClCompile/msb:PreprocessorDefinitions", MSBUILD_NS):
            for d in split_msbuild_list(def_el.text or ""):
                if "$(" not in d and d != "NDEBUG" and d not in defines:
                    defines.append(d)

    if uses_vcpkg:
        for vcpkg_inc in (
            root / "vcpkg_installed" / "x64-windows" / "include",
            root / "build" / "vcpkg_installed" / "x64-windows" / "include",
        ):
            if vcpkg_inc not in inc_dirs:
                inc_dirs.append(vcpkg_inc)

    # C++/WinRT generated projections when PackageReference Microsoft.Windows.CppWinRT is used
    for pkg in root_el.findall(".//msb:PackageReference", MSBUILD_NS):
        if pkg.attrib.get("Include") == "Microsoft.Windows.CppWinRT":
            gen_dir = root / "build" / "obj" / proj_name / "x64" / "Debug" / "Generated Files"
            if gen_dir not in inc_dirs:
                inc_dirs.append(gen_dir)

    sources: list[tuple[Path, bool]] = []
    seen_sources: set[Path] = set()
    pch_header = (proj_dir / "pch.h").resolve()
    has_pch = pch_header.is_file()
    for item in root_el.findall(".//msb:ClCompile", MSBUILD_NS):
        inc = item.attrib.get("Include")
        if not inc:
            continue
        pch_el = item.find("msb:PrecompiledHeader", MSBUILD_NS)
        forced_el = item.find("msb:ForcedIncludeFiles", MSBUILD_NS)
        pch_mode = (pch_el.text or "").strip().lower() if pch_el is not None else ""
        forced_explicit_empty = forced_el is not None and not split_msbuild_list(forced_el.text or "")
        is_pch_creator = pch_mode in ("create", "notusing") or forced_explicit_empty
        pattern = str(proj_dir / inc.replace("\\", "/"))
        matched = sorted(Path(p).resolve() for p in glob.glob(pattern)) if any(c in inc for c in "*?") else [
            (proj_dir / inc.replace("\\", "/")).resolve()
        ]
        for src in matched:
            if src.is_file() and src not in seen_sources:
                seen_sources.add(src)
                use_forced_pch = has_pch and not is_pch_creator and src.name.lower() != "pch.cpp"
                sources.append((src, use_forced_pch))

    base_args = [
        "clang-cl.exe",
        "--target=x86_64-pc-windows-msvc",
        "-fms-compatibility-version=19.44",
        "/std:c++23preview",
        "/EHsc",
        "/W4",
        "/permissive-",
        "/Zc:__cplusplus",
        "/Zc:preprocessor",
        "/utf-8",
    ]
    for d in defines:
        base_args.append(f"-D{d}")
    for inc_path in inc_dirs:
        base_args.append(f"-I{inc_path}")
    for sys_inc in sys_includes:
        base_args.extend(["-imsvc", sys_inc])

    entries = []
    for src, use_forced_pch in sources:
        unit_args = list(base_args)
        if use_forced_pch:
            unit_args.append(f"/FI{pch_header}")
        entries.append({
            "directory": str(proj_dir),
            "file": str(src),
            "arguments": [*unit_args, "/Zs", "-c", str(src)],
        })
    return entries


def build_database(root: Path = ROOT) -> list[dict]:
    root = root.resolve()
    sln = root / "Recto.sln"
    if not sln.is_file():
        raise FileNotFoundError(f"missing solution file: {sln}")
    sys_includes = discover_system_includes(target_sdk_version(root))
    entries: list[dict] = []
    for name, vcxproj in solution_projects(sln):
        entries.extend(parse_vcxproj(name, vcxproj, sys_includes, root))
    return entries


def _write_entries(out_path: Path, entries: list[dict]) -> list[dict]:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    tmp = out_path.with_name(f"{out_path.name}.{os.getpid()}.tmp")
    tmp.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8", newline="\n")
    os.replace(tmp, out_path)
    return entries


def write_database(out_path: Path = OUT, root: Path = ROOT) -> list[dict]:
    return _write_entries(out_path, build_database(root))


def _normalize_entry(entry: dict) -> tuple[str, str, tuple[str, ...]]:
    directory = os.path.normcase(os.path.normpath(str(entry.get("directory", ""))))
    file_path = os.path.normcase(os.path.normpath(str(entry.get("file", ""))))
    raw_args = entry.get("arguments")
    if isinstance(raw_args, list):
        args = tuple(str(a) for a in raw_args)
    else:
        args = (str(entry.get("command", "")),)
    return (file_path, directory, args)


def _check_against_expected(out_path: Path, expected: list[dict]) -> tuple[bool, str]:
    if not out_path.is_file():
        return False, f"{out_path} is missing (run: python scripts/gen-compile-commands.py)"
    try:
        actual = json.loads(out_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        return False, f"{out_path} is unreadable ({exc})"
    if not isinstance(actual, list) or not all(isinstance(e, dict) for e in actual):
        return False, f"{out_path} must be a JSON array of compile command objects"
    actual_files = sorted(str(e.get("file", "")) for e in actual)
    expected_files = sorted(str(e["file"]) for e in expected)
    if actual_files != expected_files:
        return False, f"{out_path} lists {len(actual_files)} files, expected {len(expected_files)} from Recto.sln"
    missing = [f for f in actual_files if not Path(f).is_file()]
    if missing:
        return False, f"{out_path} names missing source file {missing[0]}"
    actual_norm = sorted(_normalize_entry(e) for e in actual)
    expected_norm = sorted(_normalize_entry(e) for e in expected)
    if actual_norm != expected_norm:
        mismatch = next(
            (a[0] for a, b in zip(actual_norm, expected_norm) if a != b),
            actual_files[0] if actual_files else str(out_path),
        )
        return False, f"{out_path} has stale directory or compiler arguments for {mismatch} (run: python scripts/gen-compile-commands.py)"
    return True, f"ok: {len(actual_files)} translation units"


def check_database(out_path: Path = OUT, root: Path = ROOT) -> tuple[bool, str]:
    if not out_path.is_file():
        return False, f"{out_path} is missing (run: python scripts/gen-compile-commands.py)"
    return _check_against_expected(out_path, build_database(root))


def ensure_database(out_path: Path = OUT, root: Path = ROOT) -> Path:
    """Regenerate `out_path` when missing, older than project files, or out of sync with `root` (e.g. moved checkout)."""
    root = root.resolve()
    sln = root / "Recto.sln"
    if not sln.is_file():
        return out_path
    expected = build_database(root)
    ok, _ = _check_against_expected(out_path, expected)
    if ok and out_path.is_file():
        db_mtime = out_path.stat().st_mtime
        inputs = [sln, root / "Directory.Build.props", root / "Directory.Build.targets"]
        inputs.extend(p for _, p in solution_projects(sln))
        if not any(p.is_file() and p.stat().st_mtime > db_mtime for p in inputs):
            return out_path
    _write_entries(out_path, expected)
    return out_path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate or verify build/compile_commands.json for clangd.")
    parser.add_argument("--check", action="store_true", help="verify build/compile_commands.json matches Recto.sln")
    parser.add_argument("--out", default=str(OUT), help="output compile_commands.json path")
    args = parser.parse_args(argv)
    out_path = Path(args.out).resolve()
    if args.check:
        ok, msg = check_database(out_path, ROOT)
        print(msg, file=sys.stdout if ok else sys.stderr)
        return 0 if ok else 1
    entries = write_database(out_path, ROOT)
    print(f"wrote {len(entries)} compile commands to {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
