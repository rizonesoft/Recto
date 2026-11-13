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
import signal
import subprocess
import sys
import threading
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


def _kill_process_tree(proc: subprocess.Popen) -> None:
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/F", "/T", "/PID", str(proc.pid)],
            capture_output=True,
            creationflags=windowless(),
        )
    else:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except (ProcessLookupError, OSError):
            pass
    try:
        proc.kill()
    except OSError:
        pass


def _run_bounded_process(
    cmd: list[str],
    cwd: Path,
    *,
    env: dict[str, str] | None = None,
    timeout_s: float = 300.0,
    cancel_event: threading.Event | None = None,
    active_procs: set[subprocess.Popen] | None = None,
    procs_lock: threading.Lock | None = None,
) -> tuple[int, float, str]:
    t0 = time.perf_counter()
    if cancel_event is not None and cancel_event.is_set():
        return 130, 0.0, "cancelled"

    spawn_kwargs: dict = (
        {"creationflags": windowless() | subprocess.CREATE_NEW_PROCESS_GROUP}
        if os.name == "nt"
        else {"start_new_session": True}
    )
    proc = subprocess.Popen(
        cmd,
        cwd=cwd,
        env=env,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        **spawn_kwargs,
    )
    if active_procs is not None and procs_lock is not None:
        with procs_lock:
            if cancel_event is not None and cancel_event.is_set():
                _kill_process_tree(proc)
                proc.communicate()
                return 130, time.perf_counter() - t0, "cancelled"
            active_procs.add(proc)
    try:
        out, err = proc.communicate(timeout=timeout_s)
    except subprocess.TimeoutExpired:
        _kill_process_tree(proc)
        out, err = proc.communicate()
        combined = ((out or "") + "\n" + (err or "")).strip()
        return 124, time.perf_counter() - t0, f"timed out after {timeout_s:g}s\n{combined}".strip()
    except BaseException:
        _kill_process_tree(proc)
        proc.communicate()
        raise
    finally:
        if active_procs is not None and procs_lock is not None:
            with procs_lock:
                active_procs.discard(proc)

    if cancel_event is not None and cancel_event.is_set():
        return 130, time.perf_counter() - t0, "cancelled"
    combined = ((out or "") + "\n" + (err or "")).strip()
    return proc.returncode, time.perf_counter() - t0, combined


def run_one(
    module: str,
    root: Path = ROOT,
    *,
    timeout_s: float = 300.0,
    cancel_event: threading.Event | None = None,
    active_procs: set[subprocess.Popen] | None = None,
    procs_lock: threading.Lock | None = None,
) -> tuple[str, int, float, str]:
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    code, elapsed, combined = _run_bounded_process(
        [sys.executable, "-B", "-m", "unittest", module],
        root,
        env=env,
        timeout_s=timeout_s,
        cancel_event=cancel_event,
        active_procs=active_procs,
        procs_lock=procs_lock,
    )
    return module, code, elapsed, combined


def run_quick_graph_gates(
    root: Path = ROOT,
    *,
    timeout_s: float = 300.0,
    cancel_event: threading.Event | None = None,
    active_procs: set[subprocess.Popen] | None = None,
    procs_lock: threading.Lock | None = None,
) -> tuple[bool, float, str]:
    t0 = time.perf_counter()
    for subcmd in (["validate"], ["plan", "--check"]):
        code, _, detail = _run_bounded_process(
            [sys.executable, "-B", "scripts/todo-graph.py", *subcmd],
            root,
            timeout_s=timeout_s,
            cancel_event=cancel_event,
            active_procs=active_procs,
            procs_lock=procs_lock,
        )
        if code != 0:
            return False, time.perf_counter() - t0, detail
    return True, time.perf_counter() - t0, "todo-graph validate + plan --check"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="RAM-bounded parallel Python test runner for Recto.")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--quick", action="store_true", help="fast local cadence with change-aware promotion (default)")
    mode.add_argument("--full", action="store_true", help="run 100%% of tests/scripts and scripts/tests")
    parser.add_argument("-k", "--filter", default="", help="only run modules whose dotted name contains this substring")
    parser.add_argument("-j", "--jobs", type=int, default=0, help="parallel worker count (0 = RAM/CPU auto)")
    parser.add_argument("--timeout", type=float, default=300.0, help="per-suite timeout in seconds (default: 300)")
    parser.add_argument("--list", action="store_true", help="list selected modules and exit")
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        print("--timeout must be greater than 0", file=sys.stderr)
        return 2

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
    cancel_event = threading.Event()
    active_procs: set[subprocess.Popen] = set()
    procs_lock = threading.Lock()
    pool = ThreadPoolExecutor(max_workers=workers)
    completed_cleanly = False
    try:
        if not args.full and not args.filter and "tests.scripts.test_todo_graph" not in selected:
            ok, dt, detail = run_quick_graph_gates(
                ROOT,
                timeout_s=args.timeout,
                cancel_event=cancel_event,
                active_procs=active_procs,
                procs_lock=procs_lock,
            )
            status = "PASS" if ok else "FAIL"
            print(f"  [{status}] {dt:5.2f}s  todo-graph (validate + plan --check)")
            if not ok:
                failures.append(("todo-graph", detail))

        future_map = {
            pool.submit(
                run_one,
                mod,
                ROOT,
                timeout_s=args.timeout,
                cancel_event=cancel_event,
                active_procs=active_procs,
                procs_lock=procs_lock,
            ): mod
            for mod in selected
        }
        for fut in as_completed(future_map):
            if cancel_event.is_set():
                break
            mod, code, dt, out = fut.result()
            status = "PASS" if code == 0 else ("TIME" if code == 124 else "FAIL")
            print(f"  [{status}] {dt:5.2f}s  {mod}")
            if code != 0:
                failures.append((mod, out))
        completed_cleanly = True
    except KeyboardInterrupt:
        print("\nInterrupted: cancelled queued suites and terminated running test workers.", file=sys.stderr)
        return 130
    finally:
        if completed_cleanly:
            pool.shutdown(wait=True, cancel_futures=False)
        else:
            cancel_event.set()
            with procs_lock:
                running = list(active_procs)
            for proc in running:
                _kill_process_tree(proc)
            pool.shutdown(wait=True, cancel_futures=True)

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
