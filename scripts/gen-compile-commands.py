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


def windowless() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def find_vs_install() -> Path | None:
    env_vc = os.environ.get("VCToolsInstallDir")
    if env_vc:
        vc_path = Path(env_vc).resolve()
        if vc_path.is_dir():
            for anc in (vc_path, *vc_path.parents):
                if (anc / "VC" / "Tools" / "MSVC").is_dir():
                    return anc
    pf86 = os.environ.get("ProgramFiles(x86)") or r"C:\Program Files (x86)"
    vswhere = Path(pf86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if vswhere.is_file():
        done = subprocess.run(
            [str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
            capture_output=True, text=True, timeout=15, creationflags=windowless(),
        )
        if done.returncode == 0 and done.stdout.strip():
            cand = Path(done.stdout.strip().splitlines()[-1])
            if cand.is_dir():
                return cand
    for base in (os.environ.get("ProgramFiles") or r"C:\Program Files", pf86):
        vs_root = Path(base) / "Microsoft Visual Studio"
        if vs_root.is_dir():
            for ver_dir in sorted(vs_root.iterdir(), reverse=True):
                if not ver_dir.is_dir():
                    continue
                for edition in ("Enterprise", "Professional", "Community", "BuildTools"):
                    cand = ver_dir / edition
                    if (cand / "VC" / "Tools" / "MSVC").is_dir():
                        return cand
    return None


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


def target_sdk_version() -> str:
    if PROPS.is_file():
        m = re.search(r"<WindowsTargetPlatformVersion>([^<]+)</WindowsTargetPlatformVersion>",
                      PROPS.read_text(encoding="utf-8"))
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


def parse_vcxproj(proj_name: str, vcxproj: Path, sys_includes: list[str]) -> list[dict]:
    tree = ET.parse(vcxproj)
    root_el = tree.getroot()
    proj_dir = vcxproj.parent

    inc_dirs: list[Path] = [proj_dir]
    if (proj_dir / "include").is_dir():
        inc_dirs.append(proj_dir / "include")

    defines: list[str] = ["UNICODE", "_UNICODE", "_DEBUG", "_WIN64", "WIN32"]
    uses_vcpkg = False

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
                cleaned = entry.replace("$(ProjectDir)", str(proj_dir) + "/").replace("$(SolutionDir)", str(ROOT) + "/")
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
            ROOT / "vcpkg_installed" / "x64-windows" / "include",
            ROOT / "build" / "vcpkg_installed" / "x64-windows" / "include",
        ):
            if vcpkg_inc not in inc_dirs:
                inc_dirs.append(vcpkg_inc)

    # C++/WinRT generated projections when PackageReference Microsoft.Windows.CppWinRT is used
    for pkg in root_el.findall(".//msb:PackageReference", MSBUILD_NS):
        if pkg.attrib.get("Include") == "Microsoft.Windows.CppWinRT":
            gen_dir = ROOT / "build" / "obj" / proj_name / "x64" / "Debug" / "Generated Files"
            if gen_dir not in inc_dirs:
                inc_dirs.append(gen_dir)

    sources: list[Path] = []
    seen_sources: set[Path] = set()
    for item in root_el.findall(".//msb:ClCompile", MSBUILD_NS):
        inc = item.attrib.get("Include")
        if not inc:
            continue
        pattern = str(proj_dir / inc.replace("\\", "/"))
        matched = sorted(Path(p).resolve() for p in glob.glob(pattern)) if any(c in inc for c in "*?") else [
            (proj_dir / inc.replace("\\", "/")).resolve()
        ]
        for src in matched:
            if src.is_file() and src not in seen_sources:
                seen_sources.add(src)
                sources.append(src)

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
    for src in sources:
        entries.append({
            "directory": str(proj_dir),
            "file": str(src),
            "arguments": [*base_args, "/Zs", "-c", str(src)],
        })
    return entries


def build_database(root: Path = ROOT) -> list[dict]:
    sln = root / "Recto.sln"
    if not sln.is_file():
        raise FileNotFoundError(f"missing solution file: {sln}")
    sys_includes = discover_system_includes(target_sdk_version())
    entries: list[dict] = []
    for name, vcxproj in solution_projects(sln):
        entries.extend(parse_vcxproj(name, vcxproj, sys_includes))
    return entries


def write_database(out_path: Path = OUT, root: Path = ROOT) -> list[dict]:
    entries = build_database(root)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    tmp = out_path.with_name(f"{out_path.name}.{os.getpid()}.tmp")
    tmp.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8", newline="\n")
    os.replace(tmp, out_path)
    return entries


def check_database(out_path: Path = OUT, root: Path = ROOT) -> tuple[bool, str]:
    if not out_path.is_file():
        return False, f"{out_path} is missing (run: python scripts/gen-compile-commands.py)"
    try:
        actual = json.loads(out_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        return False, f"{out_path} is unreadable ({exc})"
    expected = build_database(root)
    actual_files = sorted(e.get("file", "") for e in actual if isinstance(e, dict))
    expected_files = sorted(e["file"] for e in expected)
    if actual_files != expected_files:
        return False, f"{out_path} lists {len(actual_files)} files, expected {len(expected_files)} from Recto.sln"
    missing = [f for f in actual_files if not Path(f).is_file()]
    if missing:
        return False, f"{out_path} names missing source file {missing[0]}"
    return True, f"ok: {len(actual_files)} translation units"


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
