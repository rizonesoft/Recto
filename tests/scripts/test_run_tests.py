"""Tests for scripts/run-tests.py: discovery, RAM-bounded worker sizing, and quick-vs-full cadence selection."""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("run_tests", ROOT / "scripts" / "run-tests.py")
assert SPEC is not None and SPEC.loader is not None
RUN_TESTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUN_TESTS)

MUTATE_SPEC = importlib.util.spec_from_file_location("mutate_check", ROOT / "scripts" / "mutate-check.py")
assert MUTATE_SPEC is not None and MUTATE_SPEC.loader is not None
MUTATE_CHECK = importlib.util.module_from_spec(MUTATE_SPEC)
MUTATE_SPEC.loader.exec_module(MUTATE_CHECK)


class RunTestsCadence(unittest.TestCase):
    def test_discovers_both_recto_and_agent_kit_suites(self):
        mods = RUN_TESTS.discover_modules(ROOT)
        self.assertIn("tests.scripts.test_todo_graph", mods)
        self.assertIn("tests.scripts.test_lsp", mods)
        self.assertIn("scripts.tests.test_review_dispatch", mods)
        self.assertIn("scripts.tests.test_unattended", mods)

    def test_ram_bounded_worker_count_scales_down_under_low_memory(self):
        self.assertEqual(RUN_TESTS.compute_workers(0, avail_mb=600, cpus=16), 1)
        self.assertEqual(RUN_TESTS.compute_workers(0, avail_mb=1600, cpus=16), 2)
        self.assertEqual(RUN_TESTS.compute_workers(0, avail_mb=16384, cpus=16), 8)
        self.assertEqual(RUN_TESTS.compute_workers(3, avail_mb=600, cpus=16), 3)

    def test_quick_mode_defers_heavy_suites_until_their_inputs_change(self):
        mods = [
            "tests.scripts.test_dispatch_hook",
            "tests.scripts.test_todo_graph",
            "tests.scripts.test_lsp",
            "scripts.tests.test_review_dispatch",
        ]
        clean = RUN_TESTS.select_modules(mods, full=False, changed=[])
        self.assertEqual(clean, ["tests.scripts.test_dispatch_hook"])

        touched_lsp = RUN_TESTS.select_modules(mods, full=False, changed=["scripts/lsp_mcp.py"])
        self.assertEqual(touched_lsp, ["tests.scripts.test_lsp", "tests.scripts.test_dispatch_hook"])

        full = RUN_TESTS.select_modules(mods, full=True, changed=[])
        self.assertEqual(len(full), 4)
        # Heavy modules are scheduled ahead of fast modules in full runs.
        self.assertEqual(full[-1], "tests.scripts.test_dispatch_hook")

    def test_filtered_execution_succeeds(self):
        code = RUN_TESTS.main(["--filter", "test_verify_agent_tiers", "--jobs", "1"])
        self.assertEqual(code, 0)

    def test_plan_workflow_runs_each_gate_as_its_own_step(self):
        plan_yml = (ROOT / ".github" / "workflows" / "plan.yml").read_text(encoding="utf-8")
        self.assertNotIn("run: |", plan_yml, "plan.yml must not stack multiple native commands in a single pwsh block")

    @staticmethod
    def _pid_alive(pid: int) -> bool:
        if os.name == "nt":
            out = subprocess.run(
                ["tasklist", "/FI", f"PID eq {pid}", "/NH"],
                capture_output=True,
                text=True,
                creationflags=RUN_TESTS.windowless(),
            )
            return str(pid) in out.stdout
        try:
            os.kill(pid, 0)
            return True
        except OSError:
            return False

    def test_run_one_timeout_kills_hung_worker_and_grandchild_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp)
            pkg = ws / "tests" / "scripts"
            pkg.mkdir(parents=True)
            (ws / "tests" / "__init__.py").write_text("", encoding="utf-8")
            (pkg / "__init__.py").write_text("", encoding="utf-8")
            pid_file = ws / "grandchild.pid"
            (pkg / "test_hang.py").write_text(
                "import pathlib, subprocess, sys, time, unittest\n"
                "class HangTest(unittest.TestCase):\n"
                "    def test_hang(self):\n"
                "        child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])\n"
                f"        pathlib.Path({str(pid_file)!r}).write_text(str(child.pid), encoding='utf-8')\n"
                "        time.sleep(60)\n",
                encoding="utf-8",
            )
            mod, code, dt, out = RUN_TESTS.run_one("tests.scripts.test_hang", ws, timeout_s=0.6)
            self.assertEqual(mod, "tests.scripts.test_hang")
            self.assertEqual(code, 124)
            self.assertIn("timed out", out)
            self.assertLess(dt, 15.0)
            self.assertTrue(pid_file.is_file(), f"worker never started: {out}")
            grandchild_pid = int(pid_file.read_text(encoding="utf-8").strip())
            deadline = time.monotonic() + 3.0
            while time.monotonic() < deadline and self._pid_alive(grandchild_pid):
                time.sleep(0.05)
            self.assertFalse(self._pid_alive(grandchild_pid), f"grandchild PID {grandchild_pid} leaked after timeout")

    def test_timeout_kills_orphan_grandchild_holding_pipes_after_parent_exits(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp)
            for label, runner in (("run_tests", "bounded"), ("mutate_check", "mutate")):
                with self.subTest(runner=label):
                    pid_file = ws / f"{label}_orphan.pid"
                    launcher = (
                        "import os, pathlib, subprocess, sys; "
                        "kw = {'creationflags': 0x00000200} if os.name == 'nt' else {'start_new_session': True}; "
                        "child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'], **kw); "
                        f"pathlib.Path({str(pid_file)!r}).write_text(str(child.pid), encoding='utf-8'); "
                        "os._exit(0)"
                    )
                    cmd = [sys.executable, "-c", launcher]
                    t0 = time.perf_counter()
                    if runner == "bounded":
                        code, elapsed, out = RUN_TESTS._run_bounded_process(cmd, ws, timeout_s=0.8)
                        self.assertEqual(code, 124)
                    else:
                        rc, out = MUTATE_CHECK.run_cmd(cmd, ws, timeout_s=0.8)
                        elapsed = time.perf_counter() - t0
                        self.assertIsNone(rc)
                    self.assertIn("timed out", out)
                    self.assertLess(elapsed, 4.0, f"{label} waited on inherited grandchild pipes instead of killing job")
                    self.assertTrue(pid_file.is_file())
                    orphan_pid = int(pid_file.read_text(encoding="utf-8").strip())
                    deadline = time.monotonic() + 2.0
                    while time.monotonic() < deadline and self._pid_alive(orphan_pid):
                        time.sleep(0.05)
                    self.assertFalse(self._pid_alive(orphan_pid), f"{label} leaked orphan PID {orphan_pid}")

    def test_posix_proc_tree_discovery_and_kill_cross_session_and_reparented_descendants(self):
        pg = RUN_TESTS.proc_group
        token = "deadbeefcafebabe"
        with tempfile.TemporaryDirectory() as tmp:
            fake_proc = Path(tmp) / "proc"
            fake_proc.mkdir()

            def write_proc(pid: int, comm: str, ppid: int, pgid: int, env_items: list[str]) -> None:
                entry = fake_proc / str(pid)
                entry.mkdir()
                (entry / "stat").write_bytes(f"{pid} ({comm}) S {ppid} {pgid} {pgid} 0 -1\n".encode("utf-8"))
                (entry / "environ").write_bytes(("\0".join(env_items) + "\0").encode("utf-8"))

            # Worker 700101 -> detached `start_new_session` reviewer 700202 (with `)` in comm) -> child 700203,
            # plus reparented (`ppid=1`) detached survivor 700304 carrying `_RECTO_MANAGED_GROUP` -> child 700305,
            # plus an unrelated process 700909 that must never be touched.
            write_proc(700101, "python3", 1, 700101, [f"_RECTO_MANAGED_GROUP={token}"])
            write_proc(700202, "codex (helper)", 700101, 700202, [f"_RECTO_MANAGED_GROUP={token}"])
            write_proc(700203, "sleep", 700202, 700202, [])
            write_proc(700304, "flood", 1, 700304, [f"_RECTO_MANAGED_GROUP={token}"])
            write_proc(700305, "leaf", 700304, 700304, [])
            write_proc(700909, "unrelated", 1, 700909, ["OTHER=1"])

            pids, pgids = pg._discover_posix_targets(700101, token, proc_root=fake_proc)
            self.assertEqual(pids, {700101, 700202, 700203, 700304, 700305})
            self.assertEqual(pgids, {700101, 700202, 700304})

            killed_pids: list[tuple[int, int]] = []
            killed_pgids: list[tuple[int, int]] = []
            with (
                mock.patch.object(pg.os, "kill", side_effect=lambda p, s: killed_pids.append((p, int(s)))),
                mock.patch.object(pg.os, "killpg", create=True, side_effect=lambda g, s: killed_pgids.append((g, int(s)))),
            ):
                pg._kill_posix_tree(700101, token, proc_root=fake_proc)

            sigkill = int(getattr(pg.signal, "SIGKILL", pg.signal.SIGTERM))
            self.assertEqual({p for p, s in killed_pids if s == sigkill}, {700101, 700202, 700203, 700304, 700305})
            self.assertEqual({g for g, s in killed_pgids if s == sigkill}, {700101, 700202, 700304})
            self.assertNotIn(700909, {p for p, _ in killed_pids})
            self.assertNotIn(700909, {g for g, _ in killed_pgids})

    def test_keyboard_interrupt_cancels_queued_suites_and_terminates_running_worker(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp)
            pkg = ws / "tests" / "scripts"
            pkg.mkdir(parents=True)
            (ws / "tests" / "__init__.py").write_text("", encoding="utf-8")
            (pkg / "__init__.py").write_text("", encoding="utf-8")
            started_marker = ws / "mod1.started"
            pid_file = ws / "mod1_child.pid"
            queued_marker = ws / "queued.ran"

            (pkg / "test_a_first.py").write_text(
                "import pathlib, subprocess, sys, time, unittest\n"
                "class FirstTest(unittest.TestCase):\n"
                "    def test_slow(self):\n"
                "        child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])\n"
                f"        pathlib.Path({str(pid_file)!r}).write_text(str(child.pid), encoding='utf-8')\n"
                f"        pathlib.Path({str(started_marker)!r}).write_text('1', encoding='utf-8')\n"
                "        time.sleep(60)\n",
                encoding="utf-8",
            )
            for idx in range(2, 6):
                (pkg / f"test_b_queued_{idx}.py").write_text(
                    "import pathlib, unittest\n"
                    "class QueuedTest(unittest.TestCase):\n"
                    "    def test_should_not_run(self):\n"
                    f"        pathlib.Path({str(queued_marker)!r}).write_text('{idx}', encoding='utf-8')\n",
                    encoding="utf-8",
                )

            def interrupt_once_started(futures):
                deadline = time.monotonic() + 10.0
                while time.monotonic() < deadline and not started_marker.is_file():
                    time.sleep(0.02)
                raise KeyboardInterrupt()
                yield  # pragma: no cover

            t0 = time.perf_counter()
            with (
                mock.patch.object(RUN_TESTS, "ROOT", ws),
                mock.patch.object(RUN_TESTS, "as_completed", side_effect=interrupt_once_started),
            ):
                rc = RUN_TESTS.main(["--full", "--jobs", "1", "--timeout", "30"])
            elapsed = time.perf_counter() - t0

            self.assertEqual(rc, 130)
            self.assertLess(elapsed, 15.0)
            self.assertTrue(started_marker.is_file())
            self.assertFalse(queued_marker.exists(), "queued test modules must not execute after KeyboardInterrupt")
            grandchild_pid = int(pid_file.read_text(encoding="utf-8").strip())
            deadline = time.monotonic() + 3.0
            while time.monotonic() < deadline and self._pid_alive(grandchild_pid):
                time.sleep(0.05)
            self.assertFalse(self._pid_alive(grandchild_pid), "running worker child tree leaked after Ctrl+C")


if __name__ == "__main__":
    unittest.main()
