#!/usr/bin/env python3
"""RAM-aware parallel Python test runner implementing Recto's local-vs-CI test cadence.

    python scripts/run-tests.py [--quick]         fast local cadence + change-aware promotion (~10-20s)
    python scripts/run-tests.py --full            every suite in tests/scripts and scripts/tests (CI & pre-ship)
    python scripts/run-tests.py --filter <pat>    only modules matching substring <pat>
    python scripts/run-tests.py --list            print selected modules without running

Standard library only. Spawns isolated windowless `unittest` worker processes bounded by both CPU cores and free
physical memory so test runs never starve the desktop or parallel builds.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]

# Heavy fixture/process-spawning suites deferred to CI (`--full`) during routine `--quick` iteration unless files they
# cover are dirty in the working tree. Even when `test_todo_graph` is deferred, `--quick` still runs the 2-second live
# `todo-graph.py validate` and `plan --check` gates.
HEAVY_MODULES: dict[str, tuple[str, ...]] = {
    "tests.scripts.test_todo_graph": ("scripts/todo-graph.py", "scripts/lib/todo_graph.py", "todo/", "AGENTS.md"),
    "tests.scripts.test_build_corpus": ("scripts/build-corpus", "tests/corpus/"),
    "tests.scripts.test_flate_bench": ("scripts/flate",),
    "tests.scripts.test_render_mockups": ("design/", "resources/", "scripts/render"),
    "tests.scripts.test_release_notes": ("scripts/release", "CHANGELOG", "docs/release"),
    "tests.scripts.test_build_ps1": ("build/", "Directory.Build."),
    "tests.scripts.test_lsp": ("scripts/lsp_mcp.py", "scripts/clangd-lsp.py", "scripts/python-lsp.py",
                               "scripts/gen-compile-commands.py", ".lsp.json", ".mcp.json", ".agents/plugins/"),
    "scripts.tests.test_review_dispatch": ("scripts/review-dispatch.py", ".conclave/reviewers.toml"),
    "scripts.tests.test_three_pass": ("scripts/review-dispatch.py", "scripts/review-prompt.py",
                                      "scripts/lib/todo_graph.py", ".conclave/reviewers.toml"),
}


def windowless() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def free_memory_mb() -> int:
    """Return available physical RAM in MiB using stdlib only (GlobalMemoryStatusEx on Windows, /proc/meminfo on Linux)."""
    if os.name == "nt":
        class MEMORYSTATUSEX(ctypes.Structure):
            _fields_ = [
                ("dwLength", ctypes.c_ulong),
                ("dwMemoryLoad", ctypes.c_ulong),
                ("ullTotalPhys", ctypes.c_ulonglong),
                ("ullAvailPhys", ctypes.c_ulonglong),
                ("ullTotalPageFile", ctypes.c_ulonglong),
                ("ullAvailPageFile", ctypes.c_ulonglong),
                ("ullTotalVirtual", ctypes.c_ulonglong),
                ("ullAvailVirtual", ctypes.c_ulonglong),
                ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
            ]

        stat = MEMORYSTATUSEX()
        stat.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(stat)):
            return int(stat.ullAvailPhys // (1024 * 1024))
    meminfo = Path("/proc/meminfo")
    if meminfo.is_file():
        for line in meminfo.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("MemAvailable:"):
                parts = line.split()
                if len(parts) >= 2 and parts[1].isdigit():
                    return int(parts[1]) // 1024
    return 4096


def compute_workers(requested: int = 0, avail_mb: int | None = None, cpus: int | None = None) -> int:
    if requested > 0:
        return requested
    logical = max(1, cpus if cpus is not None else (os.cpu_count() or 4))
    ram_mb = avail_mb if avail_mb is not None else free_memory_mb()
    by_cpu = max(1, min(8, logical // 2 or 1))
    by_ram = max(1, ram_mb // 768)
    return max(1, min(by_cpu, by_ram))


def dirty_paths(root: Path = ROOT) -> list[str]:
    done = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=root,
        capture_output=True,
        text=True,
        creationflags=windowless(),
    )
    if done.returncode != 0:
        return []
    paths: list[str] = []
    for raw in done.stdout.splitlines():
        if len(raw) > 3:
            entry = raw[3:].strip().strip('"').replace("\\", "/")
            if " -> " in entry:
                entry = entry.split(" -> ", 1)[1].strip('"')
            paths.append(entry)
    return paths


def discover_modules(root: Path = ROOT) -> list[str]:
    modules: list[str] = []
    for pkg_prefix, rel_dir in (("tests.scripts", "tests/scripts"), ("scripts.tests", "scripts/tests")):
        folder = root / rel_dir
        if not folder.is_dir():
            continue
        for p in sorted(folder.glob("test_*.py")):
            modules.append(f"{pkg_prefix}.{p.stem}")
    return modules


def select_modules(
    all_modules: list[str],
    *,
    full: bool,
    changed: list[str],
    pattern: str = "",
) -> list[str]:
    if pattern:
        needle = pattern.lower()
        selected = [m for m in all_modules if needle in m.lower()]
    elif full:
        selected = list(all_modules)
    else:
        selected = []
        for mod in all_modules:
            triggers = HEAVY_MODULES.get(mod)
            if triggers is None:
                selected.append(mod)
                continue
            mod_rel = mod.replace(".", "/") + ".py"
            if any(c == mod_rel or any(c.startswith(t) or c == t for t in triggers) for c in changed):
                selected.append(mod)
    # Schedule heavy modules first so parallel workers never leave the slowest suite for last.
    return sorted(selected, key=lambda m: (0 if m in HEAVY_MODULES else 1, m))


def run_one(module: str, root: Path = ROOT) -> tuple[str, int, float, str]:
    t0 = time.perf_counter()
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    done = subprocess.run(
        [sys.executable, "-B", "-m", "unittest", module],
        cwd=root,
        env=env,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        creationflags=windowless(),
    )
    elapsed = time.perf_counter() - t0
    combined = (done.stdout + "\n" + done.stderr).strip()
    return module, done.returncode, elapsed, combined


def run_quick_graph_gates(root: Path = ROOT) -> tuple[bool, float, str]:
    t0 = time.perf_counter()
    for subcmd in (["validate"], ["plan", "--check"]):
        done = subprocess.run(
            [sys.executable, "-B", "scripts/todo-graph.py", *subcmd],
            cwd=root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            creationflags=windowless(),
        )
        if done.returncode != 0:
            return False, time.perf_counter() - t0, (done.stdout + "\n" + done.stderr).strip()
    return True, time.perf_counter() - t0, "todo-graph validate + plan --check"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="RAM-bounded parallel Python test runner for Recto.")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--quick", action="store_true", help="fast local cadence with change-aware promotion (default)")
    mode.add_argument("--full", action="store_true", help="run 100%% of tests/scripts and scripts/tests")
    parser.add_argument("--filter", default="", help="only run modules whose dotted name contains this substring")
    parser.add_argument("--jobs", type=int, default=0, help="parallel worker count (0 = RAM/CPU auto)")
    parser.add_argument("--list", action="store_true", help="list selected modules and exit")
    args = parser.parse_args(argv)

    all_mods = discover_modules(ROOT)
    changed = [] if args.full else dirty_paths(ROOT)
    selected = select_modules(all_mods, full=args.full, changed=changed, pattern=args.filter)

    if args.list:
        for mod in selected:
            print(mod)
        return 0

    if not selected:
        print(f"no test modules matched filter {args.filter!r}", file=sys.stderr)
        return 2

    avail_mb = free_memory_mb()
    workers = min(len(selected), compute_workers(args.jobs, avail_mb=avail_mb))
    cadence = "full" if args.full else ("filtered" if args.filter else "quick")
    print(f"Recto Python tests ({cadence}): {len(selected)}/{len(all_mods)} module(s), "
          f"{workers} worker(s), {avail_mb} MiB RAM free")

    started = time.perf_counter()
    failures: list[tuple[str, str]] = []

    if not args.full and not args.filter and "tests.scripts.test_todo_graph" not in selected:
        ok, dt, detail = run_quick_graph_gates(ROOT)
        status = "PASS" if ok else "FAIL"
        print(f"  [{status}] {dt:5.2f}s  todo-graph (validate + plan --check)")
        if not ok:
            failures.append(("todo-graph", detail))

    with ThreadPoolExecutor(max_workers=workers) as pool:
        future_map = {pool.submit(run_one, mod, ROOT): mod for mod in selected}
        for fut in as_completed(future_map):
            mod, code, dt, out = fut.result()
            status = "PASS" if code == 0 else "FAIL"
            print(f"  [{status}] {dt:5.2f}s  {mod}")
            if code != 0:
                failures.append((mod, out))

    total = time.perf_counter() - started
    if failures:
        print(f"\nFAILED: {len(failures)} suite(s) failed in {total:.2f}s:", file=sys.stderr)
        for mod, out in failures:
            tail = "\n".join(out.splitlines()[-40:])
            print(f"\n--- {mod} ---\n{tail}", file=sys.stderr)
        return 1

    print(f"OK: all {len(selected)} suite(s) passed in {total:.2f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
