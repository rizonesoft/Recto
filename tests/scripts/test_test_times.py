"""Tests for scripts/test-times.py and tests/recto-tests/time-budget.json."""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "test-times.py"
LIVE_BUDGET = ROOT / "tests" / "recto-tests" / "time-budget.json"


def _write_gtest_xml(path: Path, suites: list[tuple[str, list[tuple[str, float]]]]) -> None:
    lines = ['<?xml version="1.0" encoding="UTF-8"?>']
    total_tests = sum(len(cases) for _, cases in suites)
    total_time = sum(secs for _, cases in suites for _, secs in cases)
    lines.append(f'<testsuites tests="{total_tests}" failures="0" disabled="0" errors="0" time="{total_time:.3f}" name="AllTests">')
    for suite_name, cases in suites:
        suite_time = sum(secs for _, secs in cases)
        lines.append(f'  <testsuite name="{suite_name}" tests="{len(cases)}" failures="0" disabled="0" errors="0" time="{suite_time:.3f}">')
        for case_name, secs in cases:
            lines.append(f'    <testcase name="{case_name}" status="run" result="completed" time="{secs:.3f}" classname="{suite_name}" />')
        lines.append("  </testsuite>")
    lines.append("</testsuites>")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _write_budget(path: Path, exceptions: list[dict[str, object]], debug: float = 20, release: float = 5, asan: float = 60) -> None:
    path.write_text(
        json.dumps({"debug": debug, "release": release, "asan": asan, "exceptions": exceptions}, indent=2) + "\n",
        encoding="utf-8",
    )


class TestTimesCliTests(unittest.TestCase):
    def _run(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(SCRIPT), *args],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
        )

    def test_live_budget_file_parses_with_expected_defaults(self):
        data = json.loads(LIVE_BUDGET.read_text(encoding="utf-8"))
        self.assertEqual(data["debug"], 20)
        self.assertEqual(data["release"], 5)
        self.assertEqual(data["asan"], 60)
        self.assertIsInstance(data["exceptions"], list)

    def test_report_lists_total_top_15_and_suite_totals(self):
        with tempfile.TemporaryDirectory() as tmp:
            xml = Path(tmp) / "report.xml"
            cases_a = [(f"Fast{i}", 0.1 * (i + 1)) for i in range(12)]
            cases_b = [(f"Slow{i}", 1.0 * (i + 2)) for i in range(6)]
            _write_gtest_xml(xml, [("SuiteA", cases_a), ("SuiteB", cases_b)])

            res = self._run("report", str(xml))
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertIn("Total: 18 tests", res.stdout)
            self.assertIn("SuiteB.Slow5\n", res.stdout)
            self.assertNotIn("SuiteA.Fast0\n", res.stdout)
            self.assertNotIn("SuiteA.Fast1\n", res.stdout)
            self.assertNotIn("SuiteA.Fast2\n", res.stdout)
            self.assertIn("SuiteA\n", res.stdout)
            self.assertIn("SuiteB\n", res.stdout)

    def test_empty_xml_returns_exit_2_for_report_and_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            xml = Path(tmp) / "empty.xml"
            budget = Path(tmp) / "budget.json"
            _write_gtest_xml(xml, [])
            _write_budget(budget, [])

            rep = self._run("report", str(xml))
            self.assertEqual(rep.returncode, 2, rep.stdout + rep.stderr)
            chk = self._run("check", str(xml), "--budget", str(budget), "--mode", "debug")
            self.assertEqual(chk.returncode, 2, chk.stdout + chk.stderr)

    def test_check_within_and_over_budget(self):
        with tempfile.TemporaryDirectory() as tmp:
            xml = Path(tmp) / "suite.xml"
            budget = Path(tmp) / "budget.json"
            _write_gtest_xml(xml, [("PdfDoc", [("Fast", 1.25), ("Pokey", 24.5)])])
            _write_budget(budget, [])

            over = self._run("check", str(xml), "--budget", str(budget), "--mode", "debug")
            self.assertEqual(over.returncode, 1, over.stdout + over.stderr)
            self.assertIn("PdfDoc.Pokey", over.stderr)
            self.assertIn("24.500s", over.stderr)

            _write_gtest_xml(xml, [("PdfDoc", [("Fast", 1.25), ("Pokey", 19.5)])])
            ok = self._run("check", str(xml), "--budget", str(budget), "--mode", "debug")
            self.assertEqual(ok.returncode, 0, ok.stdout + ok.stderr)

    def test_check_exceptions_limit_mode_isolation_and_multi_xml(self):
        with tempfile.TemporaryDirectory() as tmp:
            shard0 = Path(tmp) / "shard0.xml"
            shard1 = Path(tmp) / "shard1.xml"
            budget = Path(tmp) / "budget.json"
            _write_gtest_xml(shard0, [("Runtime", [("SlowStress", 45.0)])])
            _write_gtest_xml(shard1, [("PdfDoc", [("Normal", 0.5)])])
            _write_budget(
                budget,
                [{"test": "Runtime.SlowStress", "mode": "asan", "seconds": 50, "reason": "ASan stress loop"}],
            )

            ok = self._run("check", str(shard0), str(shard1), "--budget", str(budget), "--mode", "asan")
            self.assertEqual(ok.returncode, 0, ok.stdout + ok.stderr)

            wrong_mode = self._run("check", str(shard0), str(shard1), "--budget", str(budget), "--mode", "debug")
            self.assertEqual(wrong_mode.returncode, 1, wrong_mode.stdout + wrong_mode.stderr)
            self.assertIn("Runtime.SlowStress", wrong_mode.stderr)

            _write_gtest_xml(shard0, [("Runtime", [("SlowStress", 55.0)])])
            over_exc = self._run("check", str(shard0), str(shard1), "--budget", str(budget), "--mode", "asan")
            self.assertEqual(over_exc.returncode, 1, over_exc.stdout + over_exc.stderr)
            self.assertIn("Runtime.SlowStress", over_exc.stderr)
            self.assertIn("55.000s", over_exc.stderr)

    def test_check_refuses_missing_reason_and_unknown_exception_test(self):
        with tempfile.TemporaryDirectory() as tmp:
            xml = Path(tmp) / "suite.xml"
            budget = Path(tmp) / "budget.json"
            _write_gtest_xml(xml, [("Corpus", [("Provenance", 0.1)])])

            _write_budget(
                budget,
                [{"test": "Corpus.Provenance", "mode": "debug", "seconds": 30, "reason": "   "}],
            )
            no_reason = self._run("check", str(xml), "--budget", str(budget), "--mode", "debug")
            self.assertEqual(no_reason.returncode, 2, no_reason.stdout + no_reason.stderr)
            self.assertIn("Corpus.Provenance", no_reason.stderr)

            _write_budget(
                budget,
                [{"test": "Corpus.DoesNotExist", "mode": "debug", "seconds": 30, "reason": "Valid reason"}],
            )
            unknown = self._run("check", str(xml), "--budget", str(budget), "--mode", "debug")
            self.assertEqual(unknown.returncode, 2, unknown.stdout + unknown.stderr)
            self.assertIn("Corpus.DoesNotExist", unknown.stderr)


if __name__ == "__main__":
    unittest.main()
