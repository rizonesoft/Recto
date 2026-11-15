#!/usr/bin/env python3
"""Tests for MSBuild `RectoCompileCommands` compile database generation and `build/clangd.cmd`."""
from __future__ import annotations

import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell")


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load {path}")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


PROC_GROUP = _load_module("proc_group", SCRIPTS / "lib" / "proc_group.py")


def _run_managed(
    cmd: list[str],
    *,
    cwd: Path = ROOT,
    env: dict[str, str] | None = None,
    timeout: float = 60.0,
) -> subprocess.CompletedProcess[str]:
    managed = PROC_GROUP.spawn_managed_process(cmd, cwd=cwd, env=env, capture_output=True)
    try:
        stdout, stderr = managed.proc.communicate(timeout=timeout)
        return subprocess.CompletedProcess(cmd, int(managed.proc.returncode or 0), stdout or "", stderr or "")
    except subprocess.TimeoutExpired as exc:
        out, err = managed.drain_after_terminate()
        raise subprocess.TimeoutExpired(cmd, timeout, output=out, stderr=err) from exc
    finally:
        managed.terminate_and_close()


def _pid_alive(pid: int) -> bool:
    if os.name == "nt":
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        STILL_ACTIVE = 259
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not handle:
            return False
        try:
            code = ctypes.c_uint32()
            if kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
                return code.value == STILL_ACTIVE
            return False
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def _vs_install_path() -> str | None:
    if sys.platform != "win32":
        return None
    vswhere = (
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    if not vswhere.is_file():
        return None
    done = _run_managed(
        [
            str(vswhere),
            "-version",
            "[18.0,19.0)",
            "-latest",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        timeout=30,
    )
    out = done.stdout.strip()
    return out or None


def _msbuild_path() -> Path | None:
    vs = _vs_install_path()
    if not vs:
        return None
    cand = Path(vs) / "MSBuild" / "Current" / "Bin" / "MSBuild.exe"
    return cand if cand.is_file() else None


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _write_minimal_vcxproj(
    path: Path,
    *,
    guid: str,
    name: str,
    sources: list[str],
    deps: list[tuple[Path, str]] | None = None,
    config_type: str = "StaticLibrary",
    extra_targets: str = "",
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    compile_items = "\n".join(f'    <ClCompile Include="{src}" />' for src in sources)
    dep_items = ""
    if deps:
        refs = "\n".join(
            f'    <ProjectReference Include="{p}"><Project>{g}</Project></ProjectReference>'
            for p, g in deps
        )
        dep_items = f"  <ItemGroup>\n{refs}\n  </ItemGroup>\n"
    content = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
        '  <ItemGroup Label="ProjectConfigurations">\n'
        '    <ProjectConfiguration Include="Debug|x64">\n'
        '      <Configuration>Debug</Configuration>\n'
        '      <Platform>x64</Platform>\n'
        '    </ProjectConfiguration>\n'
        '    <ProjectConfiguration Include="Release|x64">\n'
        '      <Configuration>Release</Configuration>\n'
        '      <Platform>x64</Platform>\n'
        '    </ProjectConfiguration>\n'
        '  </ItemGroup>\n'
        '  <PropertyGroup Label="Globals">\n'
        f'    <ProjectGuid>{guid}</ProjectGuid>\n'
        '    <Keyword>Win32Proj</Keyword>\n'
        f'    <RootNamespace>{name.replace("-", "_")}</RootNamespace>\n'
        f'    <ProjectName>{name}</ProjectName>\n'
        '  </PropertyGroup>\n'
        '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />\n'
        '  <PropertyGroup Label="Configuration">\n'
        f'    <ConfigurationType>{config_type}</ConfigurationType>\n'
        '    <CharacterSet>Unicode</CharacterSet>\n'
        '  </PropertyGroup>\n'
        '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />\n'
        '  <ItemGroup>\n'
        '    <ClInclude Include="pch.h" />\n'
        '    <ClCompile Include="pch.cpp">\n'
        '      <PrecompiledHeader>Create</PrecompiledHeader>\n'
        '      <ForcedIncludeFiles />\n'
        '    </ClCompile>\n'
        f'{compile_items}\n'
        '  </ItemGroup>\n'
        f'{dep_items}'
        f'{extra_targets}'
        '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />\n'
        '</Project>\n'
    )
    path.write_text(content, encoding="utf-8")


def _write_sln(path: Path, projects: list[tuple[str, str, str]]) -> None:
    lines = ["Microsoft Visual Studio Solution File, Format Version 12.00"]
    for name, rel, guid in projects:
        lines.append(
            f'Project("{{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}}") = "{name}", "{rel}", "{guid}"'
        )
        lines.append("EndProject")
    lines.append("Global")
    lines.append("\tGlobalSection(SolutionConfigurationPlatforms) = preSolution")
    lines.append("\t\tDebug|x64 = Debug|x64")
    lines.append("\t\tRelease|x64 = Release|x64")
    lines.append("\tEndGlobalSection")
    lines.append("\tGlobalSection(ProjectConfigurationPlatforms) = postSolution")
    for _, _, guid in projects:
        for cfg in ("Debug|x64", "Release|x64"):
            lines.append(f"\t\t{guid}.{cfg}.ActiveCfg = {cfg}")
            lines.append(f"\t\t{guid}.{cfg}.Build.0 = {cfg}")
    lines.append("\tEndGlobalSection")
    lines.append("EndGlobal")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _manifest_record(project_dir: Path, vcxproj: Path, include_path: str, units: list[dict]) -> str:
    sep = "\x1f"
    esc = lambda s: s.replace("%", "%25").replace(";", "%3B")
    lines = [
        sep.join(["project", str(vcxproj), str(project_dir), "x64", "Debug", esc(include_path), ""])
    ]
    for u in units:
        lines.append(
            sep.join(
                [
                    "unit",
                    str(u["file"]),
                    u.get("std", "stdcpp23"),
                    u.get("eh", "Sync"),
                    u.get("warn", "Level4"),
                    u.get("conf", "true"),
                    u.get("rtti", "true"),
                    u.get("pch_mode", "Use"),
                    u.get("pch_file", "pch.h"),
                    u.get("wx", "true"),
                    u.get("dbg", "OldStyle"),
                    u.get("rt", "MultiThreadedDebugDLL"),
                    u.get("std_preproc", "true"),
                    esc(u.get("add_inc", "")),
                    esc(u.get("defs", "WIN32_LEAN_AND_MEAN;NOMINMAX")),
                    esc(u.get("undefs", "")),
                    esc(u.get("forced", "")),
                    esc(u.get("add_opts", "/utf-8")),
                    esc(u.get("disable_warn", "")),
                ]
            )
        )
    return "\n".join(lines) + "\n"


def _copy_real_build_configs(ws: Path) -> None:
    for name in ("Directory.Build.props", "Directory.Build.targets"):
        shutil.copy2(ROOT / name, ws / name)
    targets = ws / "Directory.Build.targets"
    text = targets.read_text(encoding="utf-8")
    # Ensure manifests work inside temp workspace root identically to ROOT.
    assert "RectoCompileCommands" in text


MSBUILD = _msbuild_path()
BCC = _load_module("build_compile_commands", SCRIPTS / "build-compile-commands.py")
GEN_CC = _load_module("gen_compile_commands", SCRIPTS / "gen-compile-commands.py")
CLANGD_LSP = _load_module("clangd_lsp", SCRIPTS / "clangd-lsp.py")


class BuildCompileCommandsTests(unittest.TestCase):
    def test_two_project_conversion_spaces_quoted_defines_pch_and_sentinels(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp).resolve() / "Repo With Spaces"
            ws.mkdir(parents=True)
            p1_dir = ws / "src" / "proj one"
            p2_dir = ws / "src" / "proj two"
            p1_dir.mkdir(parents=True)
            p2_dir.mkdir(parents=True)

            (p1_dir / "pch.h").write_text("#pragma once\n", encoding="utf-8")
            (p1_dir / "pch.cpp").write_text('#include "pch.h"\n', encoding="utf-8")
            (p1_dir / "alpha.cpp").write_text('#include "pch.h"\nint a() { return 1; }\n', encoding="utf-8")
            (p2_dir / "pch.h").write_text("#pragma once\n", encoding="utf-8")
            (p2_dir / "beta.cpp").write_text("int b() { return 2; }\n", encoding="utf-8")

            v1 = p1_dir / "recto-alpha.vcxproj"
            v2 = p2_dir / "recto-beta.vcxproj"
            _write_minimal_vcxproj(v1, guid="{11111111-1111-1111-1111-111111111111}", name="recto-alpha", sources=["alpha.cpp"])
            _write_minimal_vcxproj(v2, guid="{22222222-2222-2222-2222-222222222222}", name="recto-beta", sources=["beta.cpp"])
            # Adjust v2 so it does not declare pch.cpp (beta.cpp only).
            v2.write_text(
                v2.read_text(encoding="utf-8").replace('    <ClCompile Include="pch.cpp">\n      <PrecompiledHeader>Create</PrecompiledHeader>\n      <ForcedIncludeFiles />\n    </ClCompile>\n', ""),
                encoding="utf-8",
            )
            sln = ws / "Test.sln"
            _write_sln(
                sln,
                [
                    ("recto-alpha", r"src\proj one\recto-alpha.vcxproj", "{11111111-1111-1111-1111-111111111111}"),
                    ("recto-beta", r"src\proj two\recto-beta.vcxproj", "{22222222-2222-2222-2222-222222222222}"),
                ],
            )

            mdir = ws / "build" / "compile-commands" / "x64" / "Debug"
            mdir.mkdir(parents=True)
            sys_inc = str(ws / "SDK Includes") + ";PreventSdkUapPropsAssignment"
            (mdir / "recto-alpha.txt").write_text(
                _manifest_record(
                    p1_dir,
                    v1,
                    sys_inc,
                    [
                        {"file": p1_dir / "pch.cpp", "pch_mode": "Create", "pch_file": "pch.h"},
                        {
                            "file": p1_dir / "alpha.cpp",
                            "add_inc": "extra inc;../proj two",
                            "defs": 'RECTO_TAG="hello;world";%(PreprocessorDefinitions)',
                            "add_opts": '/utf-8 /DQUOTED="with spaces"',
                        },
                    ],
                ),
                encoding="utf-8",
            )
            (mdir / "recto-beta.txt").write_text(
                _manifest_record(
                    p2_dir,
                    v2,
                    sys_inc,
                    [
                        {"file": p2_dir / "beta.cpp", "forced": "pch.h"},
                    ],
                ),
                encoding="utf-8",
            )

            out_db = ws / "build" / "compile_commands.json"
            entries = BCC.write_database_from_manifests(sln, mdir, out_db)
            self.assertEqual(len(entries), 3)
            self.assertEqual(list(out_db.parent.glob("*.tmp")), [])

            parsed = json.loads(out_db.read_text(encoding="utf-8"))
            by_name = {Path(e["file"]).name: e for e in parsed}
            self.assertEqual(set(by_name.keys()), {"pch.cpp", "alpha.cpp", "beta.cpp"})

            alpha_args = by_name["alpha.cpp"]["arguments"]
            self.assertEqual(alpha_args[0], "clang-cl.exe")
            self.assertIn("-DRECTO_TAG=\"hello;world\"", alpha_args)
            self.assertIn('/DQUOTED="with spaces"', alpha_args)
            self.assertIn(f"-I{p1_dir / 'extra inc'}", alpha_args)
            self.assertIn(f"/FI{p1_dir / 'pch.h'}", alpha_args)
            self.assertFalse(any("PreventSdkUapPropsAssignment" in a for a in alpha_args))
            self.assertFalse(any("%(PreprocessorDefinitions)" in a for a in alpha_args))

            pch_args = by_name["pch.cpp"]["arguments"]
            self.assertFalse(any(a.startswith("/FI") for a in pch_args))

            ok, count = BCC.database_matches_current_manifests(out_db, sln, mdir)
            self.assertTrue(ok)
            self.assertEqual(count, 3)

    def test_refuses_missing_empty_zero_unit_orphan_and_source_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp).resolve()
            p1 = ws / "proj1"
            p2 = ws / "proj2"
            p1.mkdir()
            p2.mkdir()
            (p1 / "pch.h").write_text("", encoding="utf-8")
            (p1 / "pch.cpp").write_text("", encoding="utf-8")
            (p1 / "a.cpp").write_text("", encoding="utf-8")
            (p2 / "pch.h").write_text("", encoding="utf-8")
            (p2 / "pch.cpp").write_text("", encoding="utf-8")
            (p2 / "b.cpp").write_text("", encoding="utf-8")

            v1 = p1 / "p1.vcxproj"
            v2 = p2 / "p2.vcxproj"
            _write_minimal_vcxproj(v1, guid="{1}", name="p1", sources=["a.cpp"])
            _write_minimal_vcxproj(v2, guid="{2}", name="p2", sources=["b.cpp"])
            sln = ws / "S.sln"
            _write_sln(sln, [("p1", "proj1\\p1.vcxproj", "{1}"), ("p2", "proj2\\p2.vcxproj", "{2}")])

            mdir = ws / "manifests"
            mdir.mkdir()
            m1 = _manifest_record(p1, v1, "", [{"file": p1 / "pch.cpp", "pch_mode": "Create"}, {"file": p1 / "a.cpp"}])
            m2 = _manifest_record(p2, v2, "", [{"file": p2 / "pch.cpp", "pch_mode": "Create"}, {"file": p2 / "b.cpp"}])

            # 1. Missing project manifest exits 2 naming p2.
            (mdir / "p1.txt").write_text(m1, encoding="utf-8")
            done = _run_managed(
                [sys.executable, str(SCRIPTS / "build-compile-commands.py"), "--sln", str(sln), "--manifests-dir", str(mdir), "--out", str(ws / "cc.json")],
            )
            self.assertEqual(done.returncode, 2)
            self.assertIn("p2", done.stderr)

            # 2. Empty manifest exits 2.
            (mdir / "p2.txt").write_text("", encoding="utf-8")
            done_empty = _run_managed(
                [sys.executable, str(SCRIPTS / "build-compile-commands.py"), "--sln", str(sln), "--manifests-dir", str(mdir), "--out", str(ws / "cc.json")],
            )
            self.assertEqual(done_empty.returncode, 2)
            self.assertIn("p2", done_empty.stderr)

            # 3. 0-unit manifest (header only) exits 2.
            (mdir / "p2.txt").write_text(_manifest_record(p2, v2, "", []).rstrip() + "\n", encoding="utf-8")
            with self.assertRaises(BCC.CompileCommandsError):
                BCC.build_entries_from_manifests(sln, mdir)

            # 4. Source mismatch (missing b.cpp in p2) exits 2.
            (mdir / "p2.txt").write_text(_manifest_record(p2, v2, "", [{"file": p2 / "pch.cpp", "pch_mode": "Create"}]), encoding="utf-8")
            with self.assertRaises(BCC.CompileCommandsError) as ctx:
                BCC.build_entries_from_manifests(sln, mdir)
            self.assertIn("source-set mismatch", str(ctx.exception))

            # 5. Orphan extra manifest exits 2.
            (mdir / "p2.txt").write_text(m2, encoding="utf-8")
            (mdir / "ghost.txt").write_text(m2, encoding="utf-8")
            with self.assertRaises(BCC.CompileCommandsError) as ctx_extra:
                BCC.build_entries_from_manifests(sln, mdir)
            self.assertIn("ghost.txt", str(ctx_extra.exception))

    @unittest.skipUnless((ROOT / "build" / "compile-commands" / "x64" / "Debug").is_dir(), "needs live Debug|x64 MSBuild manifests")
    def test_live_manifest_copy_with_one_project_removed_exits_2(self):
        live_mdir = ROOT / "build" / "compile-commands" / "x64" / "Debug"
        with tempfile.TemporaryDirectory() as tmp:
            copy_dir = Path(tmp) / "manifests"
            shutil.copytree(live_mdir, copy_dir)
            target = copy_dir / "recto-services.txt"
            self.assertTrue(target.is_file())
            target.unlink()
            done = _run_managed(
                [
                    sys.executable,
                    str(SCRIPTS / "build-compile-commands.py"),
                    "--sln",
                    str(ROOT / "Recto.sln"),
                    "--manifests-dir",
                    str(copy_dir),
                    "--out",
                    str(Path(tmp) / "compile_commands.json"),
                ],
            )
            self.assertEqual(done.returncode, 2, done.stderr)
            self.assertIn("recto-services", done.stderr)

    def test_add_and_remove_unit_in_synthetic_fixture(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp).resolve()
            p1 = ws / "proj1"
            p1.mkdir()
            (p1 / "pch.h").write_text("#pragma once\n", encoding="utf-8")
            (p1 / "pch.cpp").write_text('#include "pch.h"\n', encoding="utf-8")
            (p1 / "a.cpp").write_text("int a() { return 1; }\n", encoding="utf-8")
            v1 = p1 / "p1.vcxproj"
            _write_minimal_vcxproj(v1, guid="{1}", name="p1", sources=["a.cpp"])
            sln = ws / "S.sln"
            _write_sln(sln, [("p1", "proj1\\p1.vcxproj", "{1}")])
            mdir = ws / "manifests"
            mdir.mkdir()
            out_db = ws / "compile_commands.json"

            (mdir / "p1.txt").write_text(_manifest_record(p1, v1, "", [{"file": p1 / "pch.cpp", "pch_mode": "Create"}, {"file": p1 / "a.cpp"}]), encoding="utf-8")
            entries1 = BCC.write_database_from_manifests(sln, mdir, out_db)
            self.assertEqual({Path(e["file"]).name for e in entries1}, {"pch.cpp", "a.cpp"})

            # Add b.cpp
            (p1 / "b.cpp").write_text("int b() { return 2; }\n", encoding="utf-8")
            _write_minimal_vcxproj(v1, guid="{1}", name="p1", sources=["a.cpp", "b.cpp"])
            (mdir / "p1.txt").write_text(
                _manifest_record(p1, v1, "", [{"file": p1 / "pch.cpp", "pch_mode": "Create"}, {"file": p1 / "a.cpp"}, {"file": p1 / "b.cpp"}]),
                encoding="utf-8",
            )
            entries2 = BCC.write_database_from_manifests(sln, mdir, out_db)
            self.assertEqual({Path(e["file"]).name for e in entries2}, {"pch.cpp", "a.cpp", "b.cpp"})

            # Remove b.cpp
            _write_minimal_vcxproj(v1, guid="{1}", name="p1", sources=["a.cpp"])
            (mdir / "p1.txt").write_text(
                _manifest_record(p1, v1, "", [{"file": p1 / "pch.cpp", "pch_mode": "Create"}, {"file": p1 / "a.cpp"}]),
                encoding="utf-8",
            )
            entries3 = BCC.write_database_from_manifests(sln, mdir, out_db)
            self.assertEqual({Path(e["file"]).name for e in entries3}, {"pch.cpp", "a.cpp"})

    @unittest.skipUnless(MSBUILD is not None, "needs Visual Studio 2026 MSBuild")
    def test_real_msbuild_full_no_change_one_source_and_add_remove_unit(self):
        assert MSBUILD is not None
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp).resolve() / "msbuild-ws"
            ws.mkdir(parents=True)
            _copy_real_build_configs(ws)
            pa = ws / "src" / "recto-alpha"
            pb = ws / "src" / "recto-beta"
            pa.mkdir(parents=True)
            pb.mkdir(parents=True)
            for pdir, fn in ((pa, "alpha_fn"), (pb, "beta_fn")):
                (pdir / "pch.h").write_text("#pragma once\n#include <cstdint>\n", encoding="utf-8")
                (pdir / "pch.cpp").write_text('#include "pch.h"\n', encoding="utf-8")
                (pdir / f"{pdir.name.split('-')[1]}.cpp").write_text(f"int32_t {fn}() {{ return 42; }}\n", encoding="utf-8")

            g_a = "{A1111111-1111-1111-1111-111111111111}"
            g_b = "{B2222222-2222-2222-2222-222222222222}"
            va = pa / "recto-alpha.vcxproj"
            vb = pb / "recto-beta.vcxproj"
            _write_minimal_vcxproj(va, guid=g_a, name="recto-alpha", sources=["alpha.cpp"])
            _write_minimal_vcxproj(vb, guid=g_b, name="recto-beta", sources=["beta.cpp"])
            sln = ws / "TwoProj.sln"
            _write_sln(
                sln,
                [
                    ("recto-alpha", r"src\recto-alpha\recto-alpha.vcxproj", g_a),
                    ("recto-beta", r"src\recto-beta\recto-beta.vcxproj", g_b),
                ],
            )
            mdir = ws / "build" / "compile-commands" / "x64" / "Debug"
            out_db = ws / "build" / "compile_commands.json"

            def run_msbuild() -> None:
                if mdir.exists():
                    shutil.rmtree(mdir)
                done = _run_managed(
                    [
                        str(MSBUILD),
                        str(sln),
                        "/nologo",
                        "/m:2",
                        "/p:Configuration=Debug;Platform=x64;EnableASAN=false",
                    ],
                    cwd=ws,
                    timeout=120,
                )
                self.assertEqual(done.returncode, 0, done.stdout + "\n" + done.stderr)

            # 1. Full MSBuild build -> 4 units.
            run_msbuild()
            e1 = BCC.write_database_from_manifests(sln, mdir, out_db)
            self.assertEqual(len(e1), 4)

            # 2. Immediate no-change MSBuild rebuild -> still emits all 4 units across both manifests.
            run_msbuild()
            e2 = BCC.write_database_from_manifests(sln, mdir, out_db)
            self.assertEqual({Path(e["file"]).name for e in e2}, {"pch.cpp", "alpha.cpp", "beta.cpp"})
            self.assertEqual(len(e2), 4)

            # 3. One-source incremental rebuild -> clears manifests dir, rebuilds 1 source, still emits all 4 units.
            alpha_cpp = pa / "alpha.cpp"
            alpha_cpp.write_text("int32_t alpha_fn() { return 43; }\n", encoding="utf-8")
            run_msbuild()
            e3 = BCC.write_database_from_manifests(sln, mdir, out_db)
            self.assertEqual(len(e3), 4)

            # 4. Add gamma.cpp to recto-alpha -> appears in DB on next build.
            (pa / "gamma.cpp").write_text("int32_t gamma_fn() { return 7; }\n", encoding="utf-8")
            _write_minimal_vcxproj(va, guid=g_a, name="recto-alpha", sources=["alpha.cpp", "gamma.cpp"])
            run_msbuild()
            e4 = BCC.write_database_from_manifests(sln, mdir, out_db)
            self.assertEqual(len(e4), 5)
            self.assertIn("gamma.cpp", {Path(e["file"]).name for e in e4})

            # 5. Remove gamma.cpp from recto-alpha -> leaves DB on next build.
            _write_minimal_vcxproj(va, guid=g_a, name="recto-alpha", sources=["alpha.cpp"])
            run_msbuild()
            e5 = BCC.write_database_from_manifests(sln, mdir, out_db)
            self.assertEqual(len(e5), 4)
            self.assertNotIn("gamma.cpp", {Path(e["file"]).name for e in e5})

    def test_gen_compile_commands_and_lsp_preserve_fresh_msbuild_database_byte_for_byte(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp).resolve()
            p1 = ws / "src" / "recto-alpha"
            p1.mkdir(parents=True)
            (ws / "Directory.Build.props").write_text("<Project />\n", encoding="utf-8")
            (ws / "Directory.Build.targets").write_text("<Project />\n", encoding="utf-8")
            (p1 / "pch.h").write_text("#pragma once\n", encoding="utf-8")
            (p1 / "pch.cpp").write_text('#include "pch.h"\n', encoding="utf-8")
            (p1 / "alpha.cpp").write_text("int alpha() { return 1; }\n", encoding="utf-8")

            v1 = p1 / "recto-alpha.vcxproj"
            _write_minimal_vcxproj(v1, guid="{1}", name="recto-alpha", sources=["alpha.cpp"])
            sln = ws / "Recto.sln"
            _write_sln(sln, [("recto-alpha", r"src\recto-alpha\recto-alpha.vcxproj", "{1}")])

            mdir = ws / "build" / "compile-commands" / "x64" / "Debug"
            mdir.mkdir(parents=True)
            manifest = mdir / "recto-alpha.txt"
            manifest.write_text(
                _manifest_record(
                    p1,
                    v1,
                    r"C:\CustomMsbuildInclude",
                    [
                        {"file": p1 / "pch.cpp", "pch_mode": "Create"},
                        {"file": p1 / "alpha.cpp", "defs": "CUSTOM_MSBUILD_DEF=1"},
                    ],
                ),
                encoding="utf-8",
            )
            out_db = ws / "build" / "compile_commands.json"
            BCC.write_database_from_manifests(sln, mdir, out_db)
            before_sha = _sha256(out_db)

            # While MSBuild manifests are fresh, check_database and ensure_database preserve exact MSBuild bytes.
            ok, msg = GEN_CC.check_database(out_db, ws)
            self.assertTrue(ok, msg)
            self.assertIn("MSBuild", msg)
            GEN_CC.ensure_database(out_db, ws)
            CLANGD_LSP.ensure_compile_commands(ws)
            self.assertEqual(_sha256(out_db), before_sha)

            # Touch vcxproj newer than manifest -> MSBuild database is stale -> fallback regenerates without CUSTOM_MSBUILD_DEF.
            future = manifest.stat().st_mtime + 10.0
            os.utime(v1, (future, future))
            if os.name == "nt":
                GEN_CC.ensure_database(out_db, ws)
                self.assertNotEqual(_sha256(out_db), before_sha)

    @unittest.skipUnless(POWERSHELL is not None, "needs PowerShell")
    def test_build_ps1_preserves_db_on_project_release_asan_and_fails_when_generator_fails(self):
        ps_exe = POWERSHELL
        assert ps_exe is not None
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp).resolve()
            ws = base / "repo"
            (ws / "build").mkdir(parents=True)
            (ws / "scripts").mkdir(parents=True)
            for name in ("Recto.sln", "Directory.Build.props", "Directory.Build.targets",
                         "vcpkg.json", "vcpkg-configuration.json"):
                shutil.copy2(ROOT / name, ws / name)
            for name in ("build.ps1", "parallelism.ps1", "asan-runtime.ps1", "msbuild-log.ps1",
                         "sccache.ps1", "sccache-cl.cpp", "tools.lock.json", "bootstrap-tools.ps1", "python.cmd"):
                shutil.copy2(ROOT / "build" / name, ws / "build" / name)
            shutil.copy2(SCRIPTS / "gen-compile-commands.py", ws / "scripts" / "gen-compile-commands.py")
            shutil.copy2(SCRIPTS / "build-compile-commands.py", ws / "scripts" / "build-compile-commands.py")
            (ws / "scripts" / "check-ui-boundary.py").write_text("import sys\nsys.exit(0)\n", encoding="utf-8")

            stub_wrapper = base / "wrapper" / "sccache-cl.exe"
            stub_wrapper.parent.mkdir(parents=True)
            stub_wrapper.write_bytes(b"MZ")

            pf86 = base / "pf86"
            vs_installer = pf86 / "Microsoft Visual Studio" / "Installer"
            vs_installer.mkdir(parents=True)
            vs_root = base / "vs2026"
            msbuild_dir = vs_root / "MSBuild" / "Current" / "Bin"
            msbuild_dir.mkdir(parents=True)
            asan_dir = vs_root / "VC" / "Tools" / "MSVC" / "14.51" / "bin" / "Hostx64" / "x64"
            asan_dir.mkdir(parents=True)
            (asan_dir / "clang_rt.asan_dynamic-x86_64.dll").write_bytes(b"MZ")
            vswhere_ps1 = vs_installer / "vswhere.ps1"
            vswhere_ps1.write_text(f"Write-Output '{vs_root}'\n$global:LASTEXITCODE = 0\n", encoding="utf-8")

            msbuild_ps1 = msbuild_dir / "MSBuild.ps1"
            msbuild_ps1.write_text(
                "foreach ($a in $args) {\n"
                "    if ($a -like '/flp:logfile=*') {\n"
                "        $logPath = ($a -split '=', 2)[1].Split(';')[0]\n"
                "        Set-Content -LiteralPath $logPath -Value \"Build succeeded.`n    0 Warning(s)`n    0 Error(s)`n\" -Encoding UTF8\n"
                "    }\n"
                "}\n"
                "$global:LASTEXITCODE = 0\n",
                encoding="utf-8",
            )

            # Build a self-contained Recto.sln with fresh MSBuild manifests so gen-compile-commands.py preserves MSBuild DB bytes.
            proj_dir = ws / "src" / "pdf"
            proj_dir.mkdir(parents=True)
            (proj_dir / "pch.h").write_text("#pragma once\n", encoding="utf-8")
            (proj_dir / "pch.cpp").write_text('#include "pch.h"\n', encoding="utf-8")
            (proj_dir / "pdf.cpp").write_text("int pdf_ok() { return 1; }\n", encoding="utf-8")
            v_pdf = proj_dir / "recto-pdf.vcxproj"
            _write_minimal_vcxproj(v_pdf, guid="{A720FA8C-20AE-5233-98DC-FC49F525A2F8}", name="recto-pdf", sources=["pdf.cpp"])
            _write_sln(ws / "Recto.sln", [("recto-pdf", r"src\pdf\recto-pdf.vcxproj", "{A720FA8C-20AE-5233-98DC-FC49F525A2F8}")])

            mdir_keep = ws / "build" / "compile-commands" / "x64" / "Debug"
            mdir_keep.mkdir(parents=True)
            sentinel_manifest = mdir_keep / "recto-pdf.txt"
            sentinel_manifest.write_text(
                _manifest_record(
                    proj_dir,
                    v_pdf,
                    r"C:\CustomMsbuildInclude",
                    [
                        {"file": proj_dir / "pch.cpp", "pch_mode": "Create"},
                        {"file": proj_dir / "pdf.cpp", "defs": "RECTO_SENTINEL=1"},
                    ],
                ),
                encoding="utf-8",
            )
            db_path = ws / "build" / "compile_commands.json"
            BCC.write_database_from_manifests(ws / "Recto.sln", mdir_keep, db_path)
            sentinel_sha = _sha256(db_path)

            def run_mock_build(ps_args: str) -> subprocess.CompletedProcess[str]:
                stamp = ws / "build" / "restore.stamp"
                if stamp.exists():
                    stamp.unlink()
                env = os.environ.copy()
                env["ProgramFiles(x86)"] = str(pf86)
                env["RECTO_TOOLS_DIR"] = str(ROOT / ".tools")
                env["RECTO_SCCACHE_WRAPPER_EXE"] = str(stub_wrapper)
                env["RECTO_SCCACHE_NO_DAEMON"] = "1"
                cmd = (
                    f"Set-Alias -Name '{vs_installer / 'vswhere.exe'}' -Value '{vswhere_ps1}' -Scope Global\n"
                    f"Set-Alias -Name '{msbuild_dir / 'MSBuild.exe'}' -Value '{msbuild_ps1}' -Scope Global\n"
                    "function global:Get-Counter { [pscustomobject]@{ CounterSamples = @([pscustomobject]@{ CookedValue = 9500 }) } }\n"
                    "function global:Get-CimInstance { [pscustomobject]@{ FreePhysicalMemory = 9728000 } }\n"
                    f"& '{ws / 'build' / 'build.ps1'}' {ps_args}\n"
                )
                return _run_managed(
                    [ps_exe, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", cmd],
                    cwd=ws,
                    env=env,
                    timeout=120,
                )

            # Skip build-compile-commands overwriting sentinel on -Project, -Configuration Release, and -EnableASAN.
            for args in (
                "-Platform x64 -Configuration Debug -Project recto-pdf -SkipTests",
                "-Platform x64 -Configuration Release -SkipTests",
                "-Platform x64 -Configuration Debug -EnableASAN -SkipTests",
            ):
                res = run_mock_build(args)
                self.assertEqual(res.returncode, 0, res.stdout + "\n" + res.stderr)
                self.assertEqual(_sha256(db_path), sentinel_sha, args)
                self.assertTrue(sentinel_manifest.is_file(), args)

            # Full Debug|x64 build clears manifests dir and fails if scripts/build-compile-commands.py exits non-zero.
            (ws / "scripts" / "build-compile-commands.py").write_text(
                "import sys\n"
                "def database_matches_current_manifests(*_a, **_kw):\n"
                "    return False, 0\n"
                "if __name__ == '__main__':\n"
                "    print('simulated manifest error', file=sys.stderr)\n"
                "    sys.exit(2)\n",
                encoding="utf-8",
            )
            fail_res = run_mock_build("-Platform x64 -Configuration Debug -SkipTests")
            self.assertNotEqual(fail_res.returncode, 0)
            self.assertIn("build-compile-commands.py failed (exit 2)", fail_res.stdout + "\n" + fail_res.stderr)
            self.assertFalse(sentinel_manifest.exists())

    @unittest.skipUnless(MSBUILD is not None and POWERSHELL is not None, "needs Visual Studio 2026 MSBuild and PowerShell")
    def test_real_build_ps1_preserves_msbuild_db_across_project_release_and_asan(self):
        ps_exe = POWERSHELL
        assert ps_exe is not None
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp).resolve() / "real-ps1-ws"
            (ws / "build").mkdir(parents=True)
            (ws / "scripts").mkdir(parents=True)
            for name in ("Directory.Build.props", "Directory.Build.targets", "vcpkg.json", "vcpkg-configuration.json"):
                shutil.copy2(ROOT / name, ws / name)
            for name in ("build.ps1", "parallelism.ps1", "asan-runtime.ps1", "msbuild-log.ps1",
                         "sccache.ps1", "sccache-cl.cpp", "tools.lock.json", "bootstrap-tools.ps1", "python.cmd"):
                shutil.copy2(ROOT / "build" / name, ws / "build" / name)
            shutil.copy2(SCRIPTS / "gen-compile-commands.py", ws / "scripts" / "gen-compile-commands.py")
            shutil.copy2(SCRIPTS / "build-compile-commands.py", ws / "scripts" / "build-compile-commands.py")
            (ws / "scripts" / "check-ui-boundary.py").write_text("import sys\nsys.exit(0)\n", encoding="utf-8")

            sentinel_target = (
                '  <Target Name="InjectLiveSentinel" BeforeTargets="ClCompile">\n'
                '    <ItemGroup>\n'
                '      <ClCompile>\n'
                '        <PreprocessorDefinitions>RECTO_LIVE_MSBUILD_SENTINEL=1;%(PreprocessorDefinitions)</PreprocessorDefinitions>\n'
                '      </ClCompile>\n'
                '    </ItemGroup>\n'
                '  </Target>\n'
            )
            project_specs = [
                ("recto-pdf", r"src\pdf\recto-pdf.vcxproj", "{A720FA8C-20AE-5233-98DC-FC49F525A2F8}", "StaticLibrary", sentinel_target),
                ("recto-core", r"src\core\recto-core.vcxproj", "{87C3D4CC-C445-580B-BF9D-D0957453AC2A}", "StaticLibrary", ""),
                ("recto-services", r"src\services\recto-services.vcxproj", "{42D98EE2-0544-5DF8-A78A-021D32D97FE2}", "StaticLibrary", ""),
                ("recto-ui", r"src\ui\recto-ui.vcxproj", "{D8279F0D-BF97-5DF7-8A4B-8BB563A9DBCC}", "StaticLibrary", ""),
                ("Recto", r"src\app\Recto.vcxproj", "{DBB1443A-A96E-5A3B-B2C5-AE54C6FC110B}", "Application", ""),
                ("recto-cli", r"src\cli\recto-cli.vcxproj", "{CC37EC84-53C5-5F71-B0D4-F8DF09D19B81}", "Application", ""),
                ("recto-tests", r"tests\recto-tests\recto-tests.vcxproj", "{1CE2125F-D61E-5C0B-81D1-4A4C65926D01}", "Application", ""),
                ("recto-fuzz", r"fuzz\recto-fuzz.vcxproj", "{2CD9910F-60B9-5227-9FA5-F9E9EE0D2A24}", "Application", ""),
            ]
            sln_entries: list[tuple[str, str, str]] = []
            for idx, (name, rel, guid, cfg_type, extra) in enumerate(project_specs):
                pdir = (ws / rel.replace("\\", "/")).parent
                pdir.mkdir(parents=True, exist_ok=True)
                (pdir / "pch.h").write_text("#pragma once\n", encoding="utf-8")
                (pdir / "pch.cpp").write_text('#include "pch.h"\n', encoding="utf-8")
                src_name = f"unit_{idx}.cpp"
                body = "int main() { return 0; }\n" if cfg_type == "Application" else f"int fn_{idx}() {{ return {idx}; }}\n"
                (pdir / src_name).write_text(body, encoding="utf-8")
                _write_minimal_vcxproj(
                    ws / rel.replace("\\", "/"),
                    guid=guid,
                    name=name,
                    sources=[src_name],
                    config_type=cfg_type,
                    extra_targets=extra,
                )
                sln_entries.append((name, rel, guid))
            _write_sln(ws / "Recto.sln", sln_entries)

            sub_tmp = Path(tmp).resolve() / "ps1-temp"
            sub_tmp.mkdir(parents=True)
            env = os.environ.copy()
            env["RECTO_TOOLS_DIR"] = str(ROOT / ".tools")
            env["TEMP"] = str(sub_tmp)
            env["TMP"] = str(sub_tmp)

            def run_real_ps1(ps_args: str) -> subprocess.CompletedProcess[str]:
                return _run_managed(
                    [ps_exe, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", f"& '{ws / 'build' / 'build.ps1'}' {ps_args}"],
                    cwd=ws,
                    env=env,
                    timeout=180,
                )

            r_debug = run_real_ps1("-Platform x64 -Configuration Debug -NoSccache -SkipTests")
            self.assertEqual(r_debug.returncode, 0, r_debug.stdout + "\n" + r_debug.stderr)
            db_path = ws / "build" / "compile_commands.json"
            self.assertIn("-DRECTO_LIVE_MSBUILD_SENTINEL=1", db_path.read_text(encoding="utf-8"))
            live_sha = _sha256(db_path)

            for ps_args in (
                "-Platform x64 -Configuration Debug -Project recto-pdf -NoSccache -SkipTests",
                "-Platform x64 -Configuration Release -NoSccache -SkipTests",
                "-Platform x64 -Configuration Debug -EnableASAN -NoSccache -SkipTests",
            ):
                r_mode = run_real_ps1(ps_args)
                self.assertEqual(r_mode.returncode, 0, f"{ps_args} failed:\n{r_mode.stdout}\n{r_mode.stderr}")
                self.assertEqual(_sha256(db_path), live_sha, f"database mutated after {ps_args}")

            CLANGD_LSP.ensure_compile_commands(ws)
            self.assertEqual(_sha256(db_path), live_sha)
            ok, msg = GEN_CC.check_database(db_path, ws)
            self.assertTrue(ok, msg)
            self.assertIn("ok (MSBuild):", msg)

    @unittest.skipUnless(os.name == "nt", "Windows build/clangd.cmd wrapper")
    def test_clangd_cmd_missing_component_version_and_diagnostics(self):
        clangd_cmd = ROOT / "build" / "clangd.cmd"
        self.assertTrue(clangd_cmd.is_file())

        with tempfile.TemporaryDirectory() as empty_vs:
            env = os.environ.copy()
            env["RECTO_VS_INSTALL"] = empty_vs
            missing = _run_managed(
                ["cmd", "/c", str(clangd_cmd), "--version"],
                cwd=ROOT, env=env, timeout=30,
            )
            self.assertEqual(missing.returncode, 2)
            self.assertIn("Microsoft.VisualStudio.Component.VC.Llvm.Clang", missing.stderr + missing.stdout)

        clangd_exe = CLANGD_LSP.find_clangd()
        if not clangd_exe:
            self.skipTest("clangd not installed on this host")

        ver = _run_managed(
            ["cmd", "/c", str(clangd_cmd), "--version"],
            cwd=ROOT, timeout=30,
        )
        self.assertEqual(ver.returncode, 0, ver.stdout + ver.stderr)
        self.assertIn("clangd version", ver.stdout + ver.stderr)

        # Unknown arguments and compile errors on any line must be caught cleanly.
        if (ROOT / "build" / "compile_commands.json").is_file():
            ok_chk = _run_managed(
                ["cmd", "/c", str(clangd_cmd), "--check=src/pdf/error.cpp"],
                cwd=ROOT, timeout=60,
            )
            combined = ok_chk.stdout + "\n" + ok_chk.stderr
            self.assertEqual(ok_chk.returncode, 0, combined)
            self.assertNotIn("unknown argument", combined.lower())
            self.assertNotIn("argument unused during compilation", combined.lower())
            self.assertIn("All checks completed, 0 errors", combined)

        # Planted error beyond line 1 (line 20) still fails --check cleanly even with --check-lines=1-1.
        with tempfile.TemporaryDirectory() as tmp_cpp_dir:
            bad_cpp = Path(tmp_cpp_dir).resolve() / "bad_line20.cpp"
            lines = ["// line"] * 19 + ['int broken = "not_an_int";']
            bad_cpp.write_text("\n".join(lines) + "\n", encoding="utf-8")
            bad_chk = _run_managed(
                ["cmd", "/c", str(clangd_cmd), f"--check={bad_cpp}"],
                cwd=ROOT, timeout=60,
            )
            bad_out = bad_chk.stdout + "\n" + bad_chk.stderr
            self.assertNotEqual(bad_chk.returncode, 0)
            self.assertIn("Line 20", bad_out)

    def test_short_8dot3_or_alias_paths_in_manifest_match_resolved_solution_units(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp).resolve()
            real_ws = base / "Repo Long Directory"
            alias_ws = base / "REPO~1"
            p1 = real_ws / "src" / "proj1"
            p1.mkdir(parents=True)
            (p1 / "pch.h").write_text("#pragma once\n", encoding="utf-8")
            (p1 / "pch.cpp").write_text('#include "pch.h"\n', encoding="utf-8")
            (p1 / "unit.cpp").write_text("int u() { return 1; }\n", encoding="utf-8")

            v1 = p1 / "p1.vcxproj"
            _write_minimal_vcxproj(v1, guid="{1}", name="p1", sources=["unit.cpp"])
            sln = real_ws / "S.sln"
            _write_sln(sln, [("p1", "src\\proj1\\p1.vcxproj", "{1}")])

            if os.name == "nt":
                mk = _run_managed(["cmd", "/c", "mklink", "/J", str(alias_ws), str(real_ws)], cwd=base, timeout=30)
                self.assertEqual(mk.returncode, 0, mk.stdout + mk.stderr)
            else:
                os.symlink(real_ws, alias_ws, target_is_directory=True)

            try:
                alias_p1 = alias_ws / "src" / "proj1"
                alias_v1 = alias_p1 / "p1.vcxproj"
                mdir = real_ws / "manifests"
                mdir.mkdir()
                (mdir / "p1.txt").write_text(
                    _manifest_record(
                        alias_p1,
                        alias_v1,
                        "",
                        [{"file": alias_p1 / "pch.cpp", "pch_mode": "Create"}, {"file": alias_p1 / "unit.cpp"}],
                    ),
                    encoding="utf-8",
                )
                out_db = real_ws / "build" / "compile_commands.json"
                entries = BCC.write_database_from_manifests(sln, mdir, out_db)
                self.assertEqual(len(entries), 2)
                for e in entries:
                    self.assertTrue(Path(e["file"]).is_relative_to(real_ws), e)
                    self.assertTrue(Path(e["directory"]).is_relative_to(real_ws), e)
            finally:
                if os.name == "nt" and alias_ws.exists():
                    alias_ws.rmdir()
                elif alias_ws.is_symlink():
                    alias_ws.unlink()

    def test_run_managed_timeout_reaps_descendant_processes_and_releases_temp_locks(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp).resolve()
            pid_file = ws / "grandchild.pid"
            lock_file = ws / "locked.txt"
            grandchild = ws / "grandchild.py"
            parent = ws / "parent.py"

            grandchild.write_text(
                "import os, pathlib, sys, time\n"
                "lock_p = pathlib.Path(sys.argv[1])\n"
                "pid_p = pathlib.Path(sys.argv[2])\n"
                "with lock_p.open('w', encoding='utf-8') as fh:\n"
                "    pid_p.write_text(str(os.getpid()), encoding='utf-8')\n"
                "    fh.write('holding lock\\n')\n"
                "    fh.flush()\n"
                "    time.sleep(30)\n",
                encoding="utf-8",
            )
            parent.write_text(
                "import subprocess, sys, time\n"
                "p = subprocess.Popen([sys.executable, '-B', sys.argv[1], sys.argv[2], sys.argv[3]])\n"
                "time.sleep(30)\n",
                encoding="utf-8",
            )

            if os.name == "nt":
                cmd = ["cmd", "/c", sys.executable, "-B", str(parent), str(grandchild), str(lock_file), str(pid_file)]
            else:
                cmd = ["sh", "-c", f'"{sys.executable}" -B "{parent}" "{grandchild}" "{lock_file}" "{pid_file}"']

            with self.assertRaises(subprocess.TimeoutExpired):
                _run_managed(cmd, cwd=ws, timeout=2.0)

            self.assertTrue(pid_file.is_file(), "grandchild did not start before timeout")
            gpid = int(pid_file.read_text(encoding="utf-8").strip())
            self.assertFalse(_pid_alive(gpid), f"descendant pid {gpid} survived _run_managed timeout")
            lock_file.unlink()

    @unittest.skipUnless(MSBUILD is not None, "needs Visual Studio 2026 MSBuild")
    def test_out_of_tree_vcxproj_importing_repo_targets_does_not_pollute_repo_manifests(self):
        assert MSBUILD is not None
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp).resolve()
            repo_ws = base / "repo"
            repo_ws.mkdir()
            _copy_real_build_configs(repo_ws)

            probe_ws = base / "outside-probe"
            probe_ws.mkdir()
            (probe_ws / "pch.h").write_text("#pragma once\n", encoding="utf-8")
            (probe_ws / "pch.cpp").write_text('#include "pch.h"\n', encoding="utf-8")
            (probe_ws / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
            probe_proj = probe_ws / "recto-out-of-tree-probe.vcxproj"
            probe_proj.write_text(
                f"""<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <ProjectGuid>{{12345678-1234-1234-1234-1234567890AB}}</ProjectGuid>
    <Keyword>Win32Proj</Keyword>
    <RootNamespace>RectoOutOfTreeProbe</RootNamespace>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <Import Project="{repo_ws / 'Directory.Build.props'}" />
  <PropertyGroup Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset>v145</PlatformToolset>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <PropertyGroup>
    <OutDir>$(ProjectDir)out\\</OutDir>
    <IntDir>$(ProjectDir)obj\\</IntDir>
  </PropertyGroup>
  <ItemGroup>
    <ClCompile Include="pch.cpp"><PrecompiledHeader>Create</PrecompiledHeader><ForcedIncludeFiles /></ClCompile>
    <ClCompile Include="main.cpp" />
  </ItemGroup>
  <Import Project="{repo_ws / 'Directory.Build.targets'}" />
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
""",
                encoding="utf-8",
            )

            r_default = _run_managed(
                [str(MSBUILD), str(probe_proj), "-nologo", "-t:ClCompile",
                 "-p:Configuration=Debug", "-p:Platform=x64", "/nr:false"],
                cwd=probe_ws,
                timeout=120,
            )
            self.assertEqual(r_default.returncode, 0, r_default.stdout + r_default.stderr)
            self.assertFalse(
                (repo_ws / "build" / "compile-commands").exists(),
                "out-of-tree probe project leaked manifest into repo build/compile-commands",
            )

            explicit_out = probe_ws / "cc_out"
            r_override = _run_managed(
                [str(MSBUILD), str(probe_proj), "-nologo", "-t:RectoCompileCommands",
                 "-p:Configuration=Debug", "-p:Platform=x64",
                 f"-p:RectoCompileCommandsDir={explicit_out}", "/nr:false"],
                cwd=probe_ws,
                timeout=120,
            )
            self.assertEqual(r_override.returncode, 0, r_override.stdout + r_override.stderr)
            self.assertTrue((explicit_out / "recto-out-of-tree-probe.txt").is_file())

    def test_live_solution_has_8_projects_and_at_least_74_units(self):
        projects, units = BCC.eligible_solution_sources(ROOT / "Recto.sln")
        self.assertEqual(len(projects), 8)
        self.assertGreaterEqual(len(units), 74)


if __name__ == "__main__":
    unittest.main()
