#!/usr/bin/env python3
"""Build and check `build/compile_commands.json` from MSBuild `RectoCompileCommands` manifests.

    python scripts/build-compile-commands.py [--sln Recto.sln] [--manifests-dir build\\compile-commands\\x64\\Debug] [--out build\\compile_commands.json]
    python scripts/build-compile-commands.py check [--db build\\compile_commands.json] [--sln Recto.sln] [--clangd build\\clangd.cmd]

Reads the evaluated `@(ClCompile)` item metadata and `$(IncludePath)` written by
`RectoCompileCommands` (`Directory.Build.targets`) after the actual MSBuild compile/build,
verifies exact translation-unit set equality against the solution's eligible `.vcxproj` units,
and emits an atomic clangd compilation database (`directory`, `file`, `arguments`).
Stdlib Python only.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import glob
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
DEFAULT_SLN = ROOT / "Recto.sln"
DEFAULT_MANIFESTS_DIR = ROOT / "build" / "compile-commands" / "x64" / "Debug"
DEFAULT_OUT = ROOT / "build" / "compile_commands.json"
DEFAULT_CLANGD_CMD = ROOT / "build" / "clangd.cmd"
RECORD_SEP = "\x1f"
MSBUILD_NS = {"msb": "http://schemas.microsoft.com/developer/msbuild/2003"}
PROJECT_RE = re.compile(r'^Project\("\{[0-9A-Fa-f-]+\}"\)\s*=\s*"([^"]+)",\s*"([^"]+\.vcxproj)"', re.M)
_UNESCAPE_RE = re.compile(r"%(25|3[Bb])")
CPP_EXTS = {".c", ".cc", ".cpp", ".cxx"}

_UNESCAPE_MAP = {
    "25": "%",
    "3b": ";",
    "3B": ";",
}


class CompileCommandsError(Exception):
    """Raised when MSBuild manifests or compile commands fail validation."""


def windowless() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def unescape_msbuild_field(raw: str) -> str:
    return _UNESCAPE_RE.sub(lambda m: _UNESCAPE_MAP[m.group(1)], raw)


def _canonical_path(path_text: str | Path) -> Path:
    p = Path(path_text)
    try:
        return p.resolve()
    except OSError:
        return Path(os.path.abspath(str(p)))


def norm_path_key(path_text: str | Path) -> str:
    return os.path.normcase(os.path.normpath(str(_canonical_path(path_text))))


def split_msbuild_values(raw: str) -> list[str]:
    """Split `;`-delimited MSBuild lists while keeping quoted `;` characters intact."""
    out: list[str] = []
    buf: list[str] = []
    in_quotes = False
    i = 0
    n = len(raw)
    while i < n:
        ch = raw[i]
        if ch == "\\" and i + 1 < n and raw[i + 1] == '"':
            buf.append('\\"')
            i += 2
            continue
        if ch == '"':
            in_quotes = not in_quotes
            buf.append(ch)
            i += 1
            continue
        if ch == ";" and not in_quotes:
            tok = "".join(buf).strip()
            if tok and not tok.startswith("%("):
                out.append(tok)
            buf.clear()
            i += 1
            continue
        buf.append(ch)
        i += 1
    tok = "".join(buf).strip()
    if tok and not tok.startswith("%("):
        out.append(tok)
    return out


def split_cli_options(raw: str) -> list[str]:
    """Split Windows compiler option strings while keeping quoted payloads attached to switches."""
    tokens: list[str] = []
    buf: list[str] = []
    in_quotes = False
    i = 0
    n = len(raw)
    while i < n:
        ch = raw[i]
        if ch == "\\" and i + 1 < n and raw[i + 1] == '"':
            buf.append('\\"')
            i += 2
            continue
        if ch == '"':
            in_quotes = not in_quotes
            buf.append(ch)
            i += 1
            continue
        if not in_quotes and ch.isspace():
            if buf:
                tok = "".join(buf)
                if not tok.startswith("%("):
                    tokens.append(tok)
                buf.clear()
            i += 1
            continue
        buf.append(ch)
        i += 1
    if buf:
        tok = "".join(buf)
        if not tok.startswith("%("):
            tokens.append(tok)
    return tokens


def solution_projects(sln_path: Path) -> list[tuple[str, Path]]:
    sln_path = sln_path.resolve()
    text = sln_path.read_text(encoding="utf-8-sig")
    projects: list[tuple[str, Path]] = []
    for name, rel in PROJECT_RE.findall(text):
        p = (sln_path.parent / rel.replace("\\", "/")).resolve()
        if not p.is_file():
            raise CompileCommandsError(f"solution references missing project file: {p}")
        projects.append((name, p))
    if not projects:
        raise CompileCommandsError(f"no .vcxproj projects found in {sln_path}")
    return projects


def _condition_applies(cond: str, *, configuration: str, platform: str) -> bool:
    c = cond.strip()
    if not c:
        return True
    # Match exact Debug|x64 / Configuration / Platform checks without full MSBuild eval.
    cfg_plat = f"{configuration}|{platform}".lower()
    if "|" in c and ("debug|" in c.lower() or "release|" in c.lower()):
        if f"'{cfg_plat}'" not in c.lower().replace(" ", ""):
            return False
    else:
        if "release" in c.lower() and configuration.lower() != "release":
            if "!=" not in c:
                return False
        if "arm64" in c.lower() and platform.lower() != "arm64":
            if "!=" not in c:
                return False
    return True


def eligible_vcxproj_sources(
    vcxproj: Path,
    *,
    configuration: str = "Debug",
    platform: str = "x64",
) -> list[Path]:
    vcxproj = vcxproj.resolve()
    proj_dir = vcxproj.parent
    root_el = ET.parse(vcxproj).getroot()
    sources: list[Path] = []
    seen: set[str] = set()
    for item in root_el.findall(".//msb:ClCompile", MSBUILD_NS) + root_el.findall(".//ClCompile"):
        inc = item.attrib.get("Include")
        if not inc:
            continue
        cond = item.attrib.get("Condition", "")
        if not _condition_applies(cond, configuration=configuration, platform=platform):
            continue
        excluded = False
        for ex_el in item.findall("msb:ExcludedFromBuild", MSBUILD_NS) + item.findall("ExcludedFromBuild"):
            if _condition_applies(ex_el.attrib.get("Condition", ""), configuration=configuration, platform=platform):
                excluded = (ex_el.text or "").strip().lower() == "true"
        if excluded:
            continue
        pattern = str(proj_dir / inc.replace("\\", "/"))
        if any(ch in inc for ch in "*?"):
            matched = sorted(Path(p).resolve() for p in glob.glob(pattern))
        else:
            matched = [(proj_dir / inc.replace("\\", "/")).resolve()]
        for src in matched:
            if src.suffix.lower() not in CPP_EXTS:
                continue
            if not src.is_file():
                continue
            key = norm_path_key(src)
            if key not in seen:
                seen.add(key)
                sources.append(src)
    return sources


def eligible_solution_sources(
    sln_path: Path,
    *,
    configuration: str = "Debug",
    platform: str = "x64",
) -> tuple[list[tuple[str, Path]], dict[str, Path]]:
    projects = solution_projects(sln_path)
    by_key: dict[str, Path] = {}
    for _, vcxproj in projects:
        for src in eligible_vcxproj_sources(vcxproj, configuration=configuration, platform=platform):
            by_key[norm_path_key(src)] = src
    return projects, by_key


def _resolve_project_path(proj_dir: Path, raw_entry: str) -> str:
    cleaned = raw_entry.strip().strip('"')
    if not cleaned:
        return ""
    p = Path(cleaned)
    cand = p if p.is_absolute() else (proj_dir / cleaned.replace("/", os.sep))
    return os.path.normpath(str(_canonical_path(cand)))


def _std_flag(lang_std: str) -> str:
    mapping = {
        "stdcpp23": "/std:c++23preview",
        "stdcpplatest": "/std:c++23preview",
        "stdcpp20": "/std:c++20",
        "stdcpp17": "/std:c++17",
        "stdcpp14": "/std:c++14",
    }
    return mapping.get(lang_std.strip().lower(), "/std:c++23preview")


def _eh_flag(eh: str) -> str | None:
    mapping = {
        "sync": "/EHsc",
        "async": "/EHa",
        "synccthrow": "/EHs",
    }
    return mapping.get(eh.strip().lower())


def _warn_flag(lvl: str) -> str | None:
    mapping = {
        "level4": "/W4",
        "level3": "/W3",
        "level2": "/W2",
        "level1": "/W1",
        "turnoffallwarnings": "/W0",
    }
    return mapping.get(lvl.strip().lower())


def _target_triple(platform_name: str) -> str:
    low = platform_name.strip().lower()
    if low == "arm64":
        return "--target=aarch64-pc-windows-msvc"
    if low in ("win32", "x86"):
        return "--target=i686-pc-windows-msvc"
    return "--target=x86_64-pc-windows-msvc"


def _runtime_lib_flag(rt: str) -> str | None:
    mapping = {
        "multithreadeddebugdll": "/MDd",
        "multithreadeddll": "/MD",
        "multithreadeddebug": "/MTd",
        "multithreaded": "/MT",
    }
    return mapping.get(rt.strip().lower())


def _append_unique_ci(seq: list[str], seen: set[str], value: str) -> None:
    key = value.lower()
    if key not in seen:
        seen.add(key)
        seq.append(value)


def parse_manifest(manifest_path: Path, *, expected_proj_name: str) -> tuple[dict, list[dict]]:
    raw_text = manifest_path.read_text(encoding="utf-8-sig")
    lines = [ln for ln in raw_text.splitlines() if ln.strip()]
    if not lines:
        raise CompileCommandsError(f"manifest for project '{expected_proj_name}' is empty: {manifest_path}")

    project_info: dict | None = None
    units: list[dict] = []
    for line_no, line in enumerate(lines, start=1):
        parts = line.split(RECORD_SEP)
        kind = parts[0]
        if kind == "project":
            if len(parts) < 6:
                raise CompileCommandsError(f"{manifest_path}:{line_no}: malformed project record")
            project_info = {
                "proj_path": parts[1].strip(),
                "proj_dir": _canonical_path(parts[2].strip()),
                "platform": parts[3].strip() or "x64",
                "configuration": parts[4].strip() or "Debug",
                "include_path": unescape_msbuild_field(parts[5]),
                "external_include_path": unescape_msbuild_field(parts[6]) if len(parts) > 6 else "",
            }
        elif kind == "unit":
            if len(parts) < 18:
                raise CompileCommandsError(f"{manifest_path}:{line_no}: malformed unit record")
            units.append({
                "full_path": parts[1].strip(),
                "lang_std": parts[2].strip(),
                "eh": parts[3].strip(),
                "warn_lvl": parts[4].strip(),
                "conformance": parts[5].strip(),
                "rtti": parts[6].strip(),
                "pch_mode": parts[7].strip(),
                "pch_file": parts[8].strip(),
                "treat_wx": parts[9].strip(),
                "dbg_fmt": parts[10].strip(),
                "runtime_lib": parts[11].strip(),
                "std_preproc": parts[12].strip(),
                "add_inc": unescape_msbuild_field(parts[13]),
                "defines": unescape_msbuild_field(parts[14]),
                "undefines": unescape_msbuild_field(parts[15]),
                "forced_inc": unescape_msbuild_field(parts[16]),
                "add_opts": unescape_msbuild_field(parts[17]),
                "disable_warn": unescape_msbuild_field(parts[18]) if len(parts) > 18 else "",
            })
        else:
            raise CompileCommandsError(f"{manifest_path}:{line_no}: unknown record tag {kind!r}")

    if project_info is None:
        raise CompileCommandsError(f"manifest for project '{expected_proj_name}' missing project header: {manifest_path}")
    if not units:
        raise CompileCommandsError(f"manifest for project '{expected_proj_name}' recorded 0 units: {manifest_path}")
    return project_info, units


def _build_entry(project_info: dict, unit: dict) -> dict:
    proj_dir: Path = project_info["proj_dir"]
    src_path = os.path.normpath(str(_canonical_path(unit["full_path"])))
    plat = project_info["platform"]
    cfg = project_info["configuration"]

    args: list[str] = [
        "clang-cl.exe",
        _target_triple(plat),
        "-fms-compatibility-version=19.44",
        _std_flag(unit["lang_std"]),
    ]
    eh = _eh_flag(unit["eh"])
    if eh:
        args.append(eh)
    warn = _warn_flag(unit["warn_lvl"])
    if warn:
        args.append(warn)
    if unit["conformance"].lower() in ("true", ""):
        args.append("/permissive-")
    if unit["rtti"].lower() == "false":
        args.append("/GR-")
    if unit["std_preproc"].lower() == "true":
        args.append("/Zc:preprocessor")
    rt_flag = _runtime_lib_flag(unit["runtime_lib"])
    if rt_flag:
        args.append(rt_flag)
    if unit["dbg_fmt"].lower() == "oldstyle":
        args.append("/Z7")

    args.append("/Zc:__cplusplus")

    # Preprocessor defines (preserve order cleanly; skip unresolved %(...) tokens).
    seen_defs: set[str] = set()
    defines: list[str] = []
    for default_def in ("UNICODE", "_UNICODE"):
        _append_unique_ci(defines, seen_defs, default_def)
    if cfg.lower() == "debug":
        _append_unique_ci(defines, seen_defs, "_DEBUG")
    if plat.lower() == "x64":
        _append_unique_ci(defines, seen_defs, "_WIN64")
        _append_unique_ci(defines, seen_defs, "WIN32")
    for raw_def in split_msbuild_values(unit["defines"]):
        if "$(" in raw_def:
            continue
        _append_unique_ci(defines, seen_defs, raw_def)
    for d in defines:
        args.append(f"-D{d}")

    for raw_undef in split_msbuild_values(unit["undefines"]):
        if "$(" not in raw_undef:
            args.append(f"-U{raw_undef}")

    seen_inc: set[str] = set()
    inc_dirs: list[str] = []
    _append_unique_ci(inc_dirs, seen_inc, os.path.normpath(str(proj_dir)))
    for raw_inc in split_msbuild_values(unit["add_inc"]):
        if "$(" in raw_inc:
            continue
        resolved_inc = _resolve_project_path(proj_dir, raw_inc)
        if resolved_inc:
            _append_unique_ci(inc_dirs, seen_inc, resolved_inc)
    for inc in inc_dirs:
        args.append(f"-I{inc}")

    seen_sys: set[str] = set()
    sys_dirs: list[str] = []
    for raw_sys in (
        split_msbuild_values(project_info["include_path"])
        + split_msbuild_values(project_info["external_include_path"])
    ):
        if "$(" in raw_sys or raw_sys.strip() == "PreventSdkUapPropsAssignment":
            continue
        resolved_sys = _resolve_project_path(proj_dir, raw_sys)
        if resolved_sys and resolved_sys.lower() not in seen_inc:
            _append_unique_ci(sys_dirs, seen_sys, resolved_sys)
    for sys_inc in sys_dirs:
        args.extend(["-imsvc", sys_inc])

    seen_fi: set[str] = set()
    forced_list: list[str] = []
    for raw_fi in split_msbuild_values(unit["forced_inc"]):
        if "$(" in raw_fi:
            continue
        resolved_fi = _resolve_project_path(proj_dir, raw_fi)
        if resolved_fi:
            _append_unique_ci(forced_list, seen_fi, resolved_fi)
    # If PCH uses Use without ForcedIncludeFiles, give clangd /FI<PrecompiledHeaderFile> so PCH declarations exist.
    if unit["pch_mode"].lower() == "use" and unit["pch_file"] and Path(src_path).name.lower() != "pch.cpp":
        resolved_pch = _resolve_project_path(proj_dir, unit["pch_file"])
        if resolved_pch:
            _append_unique_ci(forced_list, seen_fi, resolved_pch)
    for fi in forced_list:
        args.append(f"/FI{fi}")

    # Pass AdditionalOptions verbatim; cl.exe-specific options are stripped in .clangd with comments.
    for opt in split_cli_options(unit["add_opts"]):
        args.append(opt)

    args.extend(["/Zs", "-c", src_path])
    return {
        "directory": os.path.normpath(str(proj_dir)),
        "file": src_path,
        "arguments": args,
    }


def build_entries_from_manifests(
    sln_path: Path = DEFAULT_SLN,
    manifests_dir: Path = DEFAULT_MANIFESTS_DIR,
    *,
    configuration: str = "Debug",
    platform: str = "x64",
) -> list[dict]:
    sln_path = sln_path.resolve()
    manifests_dir = manifests_dir.resolve()
    if not sln_path.is_file():
        raise CompileCommandsError(f"missing solution file: {sln_path}")
    if not manifests_dir.is_dir():
        raise CompileCommandsError(f"missing MSBuild manifests directory: {manifests_dir}")

    projects, expected_by_key = eligible_solution_sources(sln_path, configuration=configuration, platform=platform)
    expected_manifest_names = {f"{name}.txt": name for name, _ in projects}
    present_txts = sorted(manifests_dir.glob("*.txt"))
    present_names = {p.name: p for p in present_txts}

    missing_manifests = [name for fname, name in expected_manifest_names.items() if fname not in present_names]
    if missing_manifests:
        missing_str = ", ".join(missing_manifests)
        raise CompileCommandsError(
            f"missing manifest(s) for solution project(s) [{missing_str}] in {manifests_dir}"
        )

    extra_manifests = [p.name for fname, p in present_names.items() if fname not in expected_manifest_names]
    if extra_manifests:
        raise CompileCommandsError(
            f"unexpected manifest(s) in {manifests_dir} not in {sln_path.name}: {', '.join(extra_manifests)}"
        )

    entries: list[dict] = []
    manifest_sources: dict[str, Path] = {}
    for proj_name, _ in projects:
        manifest_file = present_names[f"{proj_name}.txt"]
        project_info, units = parse_manifest(manifest_file, expected_proj_name=proj_name)
        for u in units:
            src_key = norm_path_key(u["full_path"])
            if src_key in manifest_sources:
                raise CompileCommandsError(
                    f"duplicate translation unit {u['full_path']} across manifests (project {proj_name})"
                )
            manifest_sources[src_key] = _canonical_path(u["full_path"])
            entries.append(_build_entry(project_info, u))

    expected_keys = set(expected_by_key.keys())
    actual_keys = set(manifest_sources.keys())
    if actual_keys != expected_keys:
        missing_units = sorted(str(expected_by_key[k]) for k in (expected_keys - actual_keys))
        extra_units = sorted(str(manifest_sources[k]) for k in (actual_keys - expected_keys))
        details = []
        if missing_units:
            details.append(f"missing units ({len(missing_units)}): {', '.join(missing_units[:5])}")
        if extra_units:
            details.append(f"extra units ({len(extra_units)}): {', '.join(extra_units[:5])}")
        raise CompileCommandsError(
            f"source-set mismatch between MSBuild manifests ({len(actual_keys)}) and {sln_path.name} "
            f"({len(expected_keys)}): {'; '.join(details)}"
        )
    return entries


def serialize_entries(entries: list[dict]) -> bytes:
    return (json.dumps(entries, indent=2) + "\n").encode("utf-8")


def write_database_from_manifests(
    sln_path: Path = DEFAULT_SLN,
    manifests_dir: Path = DEFAULT_MANIFESTS_DIR,
    out_path: Path = DEFAULT_OUT,
) -> list[dict]:
    entries = build_entries_from_manifests(sln_path, manifests_dir)
    out_path = out_path.resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    payload = serialize_entries(entries)
    tmp = out_path.with_name(f"{out_path.name}.{os.getpid()}.tmp")
    tmp.write_bytes(payload)
    os.replace(tmp, out_path)
    return entries


def database_matches_current_manifests(
    out_path: Path = DEFAULT_OUT,
    sln_path: Path = DEFAULT_SLN,
    manifests_dir: Path = DEFAULT_MANIFESTS_DIR,
) -> tuple[bool, int]:
    """Return (True, unit_count) only when `out_path` matches fresh, complete MSBuild manifests byte-for-byte."""
    out_path = out_path.resolve()
    sln_path = sln_path.resolve()
    manifests_dir = manifests_dir.resolve()
    if not out_path.is_file() or not sln_path.is_file() or not manifests_dir.is_dir():
        return False, 0
    try:
        entries = build_entries_from_manifests(sln_path, manifests_dir)
        projects, _ = eligible_solution_sources(sln_path)
    except (CompileCommandsError, OSError, ET.ParseError):
        return False, 0

    # If any project/solution input was edited after the manifests ran, manifests are stale.
    manifest_mtimes = [
        (manifests_dir / f"{proj_name}.txt").stat().st_mtime
        for proj_name, _ in projects
    ]
    if not manifest_mtimes:
        return False, 0
    oldest_manifest = min(manifest_mtimes)
    sln_dir = sln_path.parent
    inputs = [sln_path, sln_dir / "Directory.Build.props", sln_dir / "Directory.Build.targets"]
    inputs.extend(p for _, p in projects)
    if any(p.is_file() and p.stat().st_mtime > oldest_manifest for p in inputs):
        return False, 0

    try:
        actual_bytes = out_path.read_bytes()
    except OSError:
        return False, 0
    return actual_bytes == serialize_entries(entries), len(entries)


def check_clangd_all(
    db_path: Path = DEFAULT_OUT,
    sln_path: Path = DEFAULT_SLN,
    clangd_cmd: Path | str = DEFAULT_CLANGD_CMD,
) -> tuple[bool, str]:
    db_path = db_path.resolve()
    sln_path = sln_path.resolve()
    if not db_path.is_file():
        return False, f"compile database is missing: {db_path}"
    if not sln_path.is_file():
        return False, f"solution file is missing: {sln_path}"

    try:
        data = json.loads(db_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        return False, f"unreadable compile database {db_path}: {exc}"
    if not isinstance(data, list) or not all(isinstance(e, dict) for e in data):
        return False, f"{db_path} must contain a JSON array of compile command objects"

    _, expected_by_key = eligible_solution_sources(sln_path)
    db_keys: dict[str, Path] = {}
    for idx, entry in enumerate(data):
        raw_file = str(entry.get("file", "")).strip()
        if not raw_file:
            return False, f"{db_path}[{idx}] is missing 'file'"
        src_p = Path(raw_file)
        if not src_p.is_file():
            return False, f"{db_path} names missing source file: {raw_file}"
        k = norm_path_key(src_p)
        if k in db_keys:
            return False, f"{db_path} contains duplicate entry for {raw_file}"
        db_keys[k] = src_p

    if set(db_keys.keys()) != set(expected_by_key.keys()):
        missing = sorted(str(expected_by_key[k]) for k in (set(expected_by_key) - set(db_keys)))
        extra = sorted(str(db_keys[k]) for k in (set(db_keys) - set(expected_by_key)))
        return False, (
            f"source-set mismatch: db has {len(db_keys)}, {sln_path.name} has {len(expected_by_key)}; "
            f"missing={missing[:3]} extra={extra[:3]}"
        )

    clangd_path = Path(clangd_cmd)
    if str(clangd_cmd) and clangd_path.is_file():
        clangd_exe = str(clangd_path.resolve())
    else:
        clangd_exe = str(clangd_cmd)

    db_dir = str(db_path.parent)
    failures: list[str] = []

    def check_one(src: Path) -> tuple[str, int, str]:
        if str(HERE) not in sys.path:
            sys.path.insert(0, str(HERE))
        import lib.proc_group as proc_group

        if clangd_exe.lower().endswith((".cmd", ".bat")) and os.name == "nt":
            cmd = ["cmd", "/c", clangd_exe, f"--compile-commands-dir={db_dir}", f"--check={src}"]
        else:
            cmd = [clangd_exe, f"--compile-commands-dir={db_dir}", f"--check={src}"]
        managed = proc_group.spawn_managed_process(cmd, cwd=sln_path.parent, capture_output=True)
        try:
            stdout, stderr = managed.proc.communicate(timeout=180)
            return str(src), int(managed.proc.returncode or 0), ((stdout or "") + "\n" + (stderr or "")).strip()
        except subprocess.TimeoutExpired:
            out, err = managed.drain_after_terminate()
            partial = (out + "\n" + err).strip()
            return str(src), 124, f"timed out after 180s\n{partial}".strip()
        finally:
            managed.terminate_and_close()

    with ThreadPoolExecutor(max_workers=2) as pool:
        futs = [pool.submit(check_one, db_keys[k]) for k in sorted(db_keys)]
        for fut in as_completed(futs):
            src_str, rc, output = fut.result()
            low = output.lower()
            has_unknown = (
                "unknown argument" in low
                or "argument unused during compilation" in low
                or "failed to resolve" in low
            )
            has_diag_err = rc != 0 or any(
                ln.lstrip().startswith("E[") or "] E[" in ln or ": error:" in ln.lower()
                for ln in output.splitlines()
            )
            if has_diag_err or has_unknown:
                snippet = "\n".join(output.splitlines()[:25])
                failures.append(f"{src_str} (exit {rc}):\n{snippet}")

    if failures:
        return False, f"{len(failures)}/{len(db_keys)} translation unit(s) failed clangd --check:\n" + "\n---\n".join(failures[:5])
    return True, f"checked {len(db_keys)}/{len(expected_by_key)} translation units: 0 errors"


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    raw = list(sys.argv[1:] if argv is None else argv)
    if not raw or raw[0].startswith("-"):
        raw = ["build", *raw]
    parser = argparse.ArgumentParser(description="Build or check compile_commands.json from MSBuild manifests.")
    sub = parser.add_subparsers(dest="command", required=True)

    p_build = sub.add_parser("build", help="build compile_commands.json from MSBuild manifests")
    p_build.add_argument("--sln", default=str(DEFAULT_SLN), help="path to Recto.sln")
    p_build.add_argument("--manifests-dir", default=str(DEFAULT_MANIFESTS_DIR), help="MSBuild manifests folder")
    p_build.add_argument("--out", default=str(DEFAULT_OUT), help="output compile_commands.json path")

    p_check = sub.add_parser("check", help="verify source-set equality and run clangd --check on every unit")
    p_check.add_argument("--db", default=str(DEFAULT_OUT), help="compile_commands.json path")
    p_check.add_argument("--sln", default=str(DEFAULT_SLN), help="path to Recto.sln")
    p_check.add_argument("--clangd", default=str(DEFAULT_CLANGD_CMD), help="clangd executable or build/clangd.cmd")
    return parser.parse_args(raw)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    if args.command == "build":
        try:
            entries = write_database_from_manifests(Path(args.sln), Path(args.manifests_dir), Path(args.out))
        except CompileCommandsError as exc:
            print(f"build-compile-commands: {exc}", file=sys.stderr)
            return 2
        print(f"wrote {len(entries)} compile commands from MSBuild manifests to {Path(args.out).resolve()}")
        return 0

    ok, msg = check_clangd_all(Path(args.db), Path(args.sln), args.clangd)
    print(msg, file=sys.stdout if ok else sys.stderr)
    if not ok and "mismatch" in msg:
        return 2
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
