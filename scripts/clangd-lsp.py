#!/usr/bin/env python3
"""Portable C++23 `clangd` launcher for Claude Code (`.lsp.json`) and Recto's MCP bridge (`scripts/lsp_mcp.py`).

    python scripts/clangd-lsp.py [--check] [extra clangd args...]

Ensures `build/compile_commands.json` exists and is fresher than `Recto.sln` and every `.vcxproj`, locates `clangd`
portably (`RECTO_CLANGD`, `PATH`, Visual Studio LLVM via `vswhere.exe`, or `ProgramFiles/LLVM`), and runs `clangd`
with low-memory flags (`--pch-storage=disk`, `-j=2`, `--limit-results=100`, `--header-insertion=never`).
"""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def windowless() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def load_gen():
    spec = importlib.util.spec_from_file_location("gen_compile_commands", HERE / "gen-compile-commands.py")
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def ensure_compile_commands(root: Path = ROOT) -> Path:
    db = root / "build" / "compile_commands.json"
    return load_gen().ensure_database(db, root)


def find_clangd() -> str | None:
    override = os.environ.get("RECTO_CLANGD")
    if override and Path(override).is_file():
        return str(Path(override))
    on_path = shutil.which("clangd")
    if on_path:
        return on_path
    if os.name == "nt":
        gen = load_gen()
        arch_order = ("ARM64", "x64") if platform.machine().lower() in ("arm64", "aarch64") else ("x64", "ARM64")
        seen_vs: list[Path] = []
        for vs in (*gen.iter_vs_installs(require_cpp=True), *gen.iter_vs_installs(require_cpp=False)):
            if vs in seen_vs:
                continue
            seen_vs.append(vs)
            for arch in arch_order:
                cand = vs / "VC" / "Tools" / "Llvm" / arch / "bin" / "clangd.exe"
                if cand.is_file():
                    return str(cand)
            cand_plain = vs / "VC" / "Tools" / "Llvm" / "bin" / "clangd.exe"
            if cand_plain.is_file():
                return str(cand_plain)
        for base in (os.environ.get("ProgramFiles") or r"C:\Program Files",
                     os.environ.get("ProgramFiles(x86)") or r"C:\Program Files (x86)"):
            cand = Path(base) / "LLVM" / "bin" / "clangd.exe"
            if cand.is_file():
                return str(cand)
    return None


def bind_kill_on_close_job(proc: subprocess.Popen):
    """Attach child process to a Win32 Job Object with KILL_ON_JOB_CLOSE so no orphan survives wrapper exit."""
    if os.name != "nt":
        return None
    try:
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

        class IO_COUNTERS(ctypes.Structure):
            _fields_ = [("r", ctypes.c_uint64), ("w", ctypes.c_uint64), ("o", ctypes.c_uint64),
                        ("rb", ctypes.c_uint64), ("wb", ctypes.c_uint64), ("ob", ctypes.c_uint64)]

        class JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
            _fields_ = [
                ("PerProcessUserTimeLimit", ctypes.c_int64),
                ("PerJobUserTimeLimit", ctypes.c_int64),
                ("LimitFlags", wintypes.DWORD),
                ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t),
                ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t),
                ("PriorityClass", wintypes.DWORD),
                ("SchedulingClass", wintypes.DWORD),
            ]

        class JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
            _fields_ = [
                ("BasicLimitInformation", JOBOBJECT_BASIC_LIMIT_INFORMATION),
                ("IoInfo", IO_COUNTERS),
                ("ProcessMemoryLimit", ctypes.c_size_t),
                ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t),
                ("PeakJobMemoryUsed", ctypes.c_size_t),
            ]

        job = kernel32.CreateJobObjectW(None, None)
        if not job:
            return None
        info = JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
        info.BasicLimitInformation.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not kernel32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
            kernel32.CloseHandle(job)
            return None
        handle = getattr(proc, "_handle", None)
        if handle is None:
            kernel32.CloseHandle(job)
            return None
        kernel32.AssignProcessToJobObject(job, int(handle))
        return job
    except Exception:
        return None


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    clangd = find_clangd()
    if args == ["--check"]:
        db = ROOT / "build" / "compile_commands.json"
        ok_db, db_msg = load_gen().check_database(db, ROOT)
        if not ok_db:
            print(f"clangd-lsp: {db_msg}", file=sys.stderr)
            return 1
        if not clangd:
            print("clangd-lsp: clangd not found on PATH or Visual Studio LLVM", file=sys.stderr)
            return 1
        print(f"clangd: {clangd}\ncompile_commands: {db} ({db_msg})")
        return 0
    if not clangd:
        print("clangd-lsp: clangd not found; install Visual Studio C++ Clang tools or LLVM", file=sys.stderr)
        return 127
    db = ensure_compile_commands(ROOT)
    cmd = [
        clangd,
        f"--compile-commands-dir={db.parent}",
        "--pch-storage=disk",
        "--background-index=0",
        "--limit-results=100",
        "-j=2",
        "--header-insertion=never",
        "--log=error",
        *args,
    ]
    if str(HERE) not in sys.path:
        sys.path.insert(0, str(HERE))
    import lib.proc_group as proc_group

    one_shot = any(a.startswith("--check") or a in ("--version", "--help") for a in args)
    if one_shot:
        with proc_group.termination_signal_guard():
            managed = proc_group.spawn_managed_process(cmd, cwd=ROOT, capture_output=False, inherit_stdin=True)
            try:
                return managed.proc.wait()
            except KeyboardInterrupt:
                managed.terminate_and_close()
                try:
                    return managed.proc.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    return 130
            finally:
                managed.terminate_and_close()
    return proc_group.run_stdio_lsp_with_idle_timeout(cmd, cwd=ROOT)


if __name__ == "__main__":
    sys.exit(main())
