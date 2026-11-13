"""Tests for scripts/run-tests.py: discovery, RAM-bounded worker sizing, and quick-vs-full cadence selection."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("run_tests", ROOT / "scripts" / "run-tests.py")
assert SPEC is not None and SPEC.loader is not None
RUN_TESTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUN_TESTS)


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


if __name__ == "__main__":
    unittest.main()
