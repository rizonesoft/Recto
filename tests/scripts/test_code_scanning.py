"""Tests for scripts/code-scanning.py and .github/workflows/codeql.yml (D06 T02 s11)."""
from __future__ import annotations

import fnmatch
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "code-scanning.py"
WORKFLOW = ROOT / ".github" / "workflows" / "codeql.yml"


def _load():
    spec = importlib.util.spec_from_file_location("code_scanning", SCRIPT)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _alert(number: int, *, state: str = "open", ref: str = "refs/heads/main", rule: str = "cpp/tainted-allocation-size",
           severity: str = "high", path: str = "src/pdf/parser.cpp", line: int = 42) -> dict:
    return {
        "number": number,
        "state": state,
        "html_url": f"https://github.com/rizonesoft/Recto/security/code-scanning/{number}",
        "rule": {"id": rule, "security_severity_level": severity},
        "most_recent_instance": {
            "ref": ref,
            "location": {"path": path, "start_line": line},
        },
    }


def _make_runner(
    *,
    runs: list[dict] | None = None,
    analyses: list[dict] | None = None,
    alert_pages: dict[int, tuple[int, object, str]] | None = None,
    run_rc: int = 0,
    analyses_rc: int = 0,
    calls: list[list[str]] | None = None,
):
    runs_data = [{"databaseId": 100, "status": "completed", "conclusion": "success",
                  "headSha": "a" * 40, "url": "https://github.com/rizonesoft/Recto/actions/runs/100",
                  "event": "push", "createdAt": "2026-11-27T05:00:00Z"}] if runs is None else runs
    analyses_data = [{"id": 1, "commit_sha": "a" * 40, "ref": "refs/heads/main"}] if analyses is None else analyses
    pages = {1: (0, [], "")} if alert_pages is None else alert_pages

    def runner(args: list[str]) -> tuple[int, str, str]:
        if calls is not None:
            calls.append(list(args))
        if args[:2] == ["run", "list"]:
            if run_rc != 0:
                return run_rc, "", "network error"
            return 0, json.dumps(runs_data), ""
        if args[0] == "api" and "/code-scanning/analyses" in args[1]:
            if analyses_rc != 0:
                return analyses_rc, "", "HTTP 404: no analysis found" if analyses_rc == 404 else "api error"
            return 0, json.dumps(analyses_data), ""
        if args[0] == "api" and "/code-scanning/alerts" in args[1]:
            m = re.search(r"[?&]page=(\d+)", args[1])
            page = int(m.group(1)) if m else 1
            rc, payload, err = pages.get(page, (0, [], ""))
            if rc != 0:
                return rc, "", err
            return 0, json.dumps(payload), ""
        return 1, "", f"unexpected gh call: {args}"

    return runner


class CodeScanningTests(unittest.TestCase):
    def setUp(self):
        self.mod = _load()

    def test_clean_green_scan_with_zero_alerts_exits_0_and_checks_request_args(self):
        calls: list[list[str]] = []
        lines: list[str] = []
        rc = self.mod.check_ordinary(runner=_make_runner(calls=calls), out=lines.append)
        self.assertEqual(rc, 0, lines)
        self.assertTrue(any("0 open alerts on main" in ln for ln in lines), lines)
        alert_calls = [c for c in calls if c[0] == "api" and "/code-scanning/alerts" in c[1]]
        self.assertEqual(len(alert_calls), 1)
        self.assertIn("state=open", alert_calls[0][1])
        self.assertIn("ref=refs/heads/main", alert_calls[0][1])
        self.assertIn("per_page=100", alert_calls[0][1])
        self.assertIn("page=1", alert_calls[0][1])

    def test_one_open_alert_prints_rule_severity_location_and_url_and_exits_1(self):
        lines: list[str] = []
        rc = self.mod.check_ordinary(
            runner=_make_runner(alert_pages={1: (0, [_alert(7, path="src/pdf/lexer.cpp", line=99)], "")}),
            out=lines.append,
        )
        self.assertEqual(rc, 1)
        joined = "\n".join(lines)
        self.assertIn("#7", joined)
        self.assertIn("high", joined)
        self.assertIn("cpp/tainted-allocation-size", joined)
        self.assertIn("src/pdf/lexer.cpp:99", joined)
        self.assertIn("security/code-scanning/7", joined)

    def test_dismissed_and_non_main_alerts_are_not_counted(self):
        lines: list[str] = []
        rc = self.mod.check_ordinary(
            runner=_make_runner(alert_pages={1: (0, [
                _alert(1, state="dismissed"),
                _alert(2, state="open", ref="refs/heads/other"),
            ], "")}),
            out=lines.append,
        )
        self.assertEqual(rc, 0, lines)

    def test_pagination_reads_two_pages_and_later_page_failure_exits_3(self):
        page1 = [_alert(i) for i in range(1, 101)]
        page2 = [_alert(101, rule="cpp/overflow", path="src/pdf/xref.cpp", line=12)]
        lines: list[str] = []
        rc = self.mod.check_ordinary(
            runner=_make_runner(alert_pages={1: (0, page1, ""), 2: (0, page2, "")}),
            out=lines.append,
        )
        self.assertEqual(rc, 1)
        joined = "\n".join(lines)
        self.assertIn("#1 ", joined)
        self.assertIn("#101 ", joined)
        self.assertIn("src/pdf/xref.cpp:12", joined)

        # Later-page failure after page 1 succeeded must exit 3 (never 0 or partial 1).
        err_lines: list[str] = []
        rc_err = self.mod.check_ordinary(
            runner=_make_runner(alert_pages={1: (0, page1, ""), 2: (1, None, "502 bad gateway")}),
            out=err_lines.append,
        )
        self.assertEqual(rc_err, 3, err_lines)

    def test_failed_scan_with_older_green_analysis_and_zero_alerts_exits_1(self):
        lines: list[str] = []
        failed_run = [{"databaseId": 101, "status": "completed", "conclusion": "failure",
                       "headSha": "b" * 40, "url": "https://github.com/rizonesoft/Recto/actions/runs/101",
                       "event": "push", "createdAt": "2026-11-27T06:00:00Z"}]
        rc = self.mod.check_ordinary(
            runner=_make_runner(runs=failed_run, alert_pages={1: (0, [], "")}),
            out=lines.append,
        )
        self.assertEqual(rc, 1, lines)
        self.assertTrue(any("ended with failure" in ln for ln in lines), lines)

    def test_pending_scan_exits_2_and_unreadable_api_exits_3(self):
        pending_run = [{"databaseId": 102, "status": "in_progress", "conclusion": "",
                        "headSha": "c" * 40, "url": "https://github.com/rizonesoft/Recto/actions/runs/102",
                        "event": "push", "createdAt": "2026-11-27T06:05:00Z"}]
        self.assertEqual(self.mod.check_ordinary(runner=_make_runner(runs=pending_run), out=lambda _: None), 2)
        self.assertEqual(self.mod.check_ordinary(runner=_make_runner(run_rc=1), out=lambda _: None), 3)
        self.assertEqual(self.mod.check_ordinary(runner=_make_runner(analyses_rc=404), out=lambda _: None), 1)
        self.assertEqual(self.mod.check_ordinary(runner=_make_runner(analyses_rc=1), out=lambda _: None), 3)

    def test_scheduled_delivery_confirmation_retry_foreign_receipt_batching_and_atomic_state(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            last_path = root / "build" / "automations" / "recto-code-scanning" / "last.json"
            last_path.parent.mkdir(parents=True)
            state_path = root / "build" / "code-scanning" / "state.json"

            # 18 open alerts with batch_limit=15: first run raises 1..15 and leaves 16..18 queued.
            alerts = [_alert(i) for i in range(1, 19)]
            runner = _make_runner(alert_pages={1: (0, alerts, "")})
            out1: list[str] = []
            rc1 = self.mod.check_scheduled(runner=runner, root=root, now=lambda: 1700000010.0, out=out1.append, batch_limit=15)
            self.assertEqual(rc1, 1)
            self.assertIn("showing 15 of 18", "\n".join(out1))
            self.assertIn("3 more open alert(s)", "\n".join(out1))

            # Failed delivery (alert != "sent"): next poll MUST retry 1..15, NOT acknowledge them.
            last_path.write_text(json.dumps({
                "started": "2023-11-14T22:13:20Z",
                "finished": "2023-11-14T22:13:40Z",
                "exit": 1,
                "alert": "failed",
            }), encoding="utf-8")
            out_retry: list[str] = []
            rc_retry = self.mod.check_scheduled(runner=runner, root=root, now=lambda: 1700000100.0, out=out_retry.append, batch_limit=15)
            self.assertEqual(rc_retry, 1)
            self.assertIn("#1 ", "\n".join(out_retry))

            # Foreign-run receipt (timestamps do not cover raised_at=1700000100.0): NOT acknowledged.
            last_path.write_text(json.dumps({
                "started": "2023-11-14T22:00:00Z",
                "finished": "2023-11-14T22:00:10Z",
                "exit": 1,
                "alert": "sent",
            }), encoding="utf-8")
            out_foreign: list[str] = []
            rc_foreign = self.mod.check_scheduled(runner=runner, root=root, now=lambda: 1700000200.0, out=out_foreign.append, batch_limit=15)
            self.assertEqual(rc_foreign, 1)
            self.assertIn("#1 ", "\n".join(out_foreign))

            # Confirmed delivery matching raised_at=1700000200.0: 1..15 acknowledged, 16..18 raised next.
            last_path.write_text(json.dumps({
                "started": "2023-11-14T22:16:35Z",
                "finished": "2023-11-14T22:16:45Z",
                "exit": 1,
                "alert": "sent",
            }), encoding="utf-8")
            out2: list[str] = []
            rc2 = self.mod.check_scheduled(runner=runner, root=root, now=lambda: 1700000300.0, out=out2.append, batch_limit=15)
            self.assertEqual(rc2, 1)
            joined2 = "\n".join(out2)
            self.assertNotIn("#1 ", joined2)
            self.assertIn("#16 ", joined2)
            self.assertIn("#18 ", joined2)

            # Confirm 16..18 delivered -> next poll exits 0.
            last_path.write_text(json.dumps({
                "started": "2023-11-14T22:18:15Z",
                "finished": "2023-11-14T22:18:25Z",
                "exit": 1,
                "alert": "sent",
            }), encoding="utf-8")
            rc3 = self.mod.check_scheduled(runner=runner, root=root, now=lambda: 1700000400.0, out=lambda _: None, batch_limit=15)
            self.assertEqual(rc3, 0)

            # Failed atomic replacement leaves prior state readable and uncorrupted.
            before = state_path.read_text(encoding="utf-8")
            def broken_replace(_src, _dst):
                raise OSError("disk full")
            with self.assertRaises(OSError):
                self.mod._write_json_atomic(state_path, {"acknowledged": []}, replace=broken_replace)
            self.assertEqual(state_path.read_text(encoding="utf-8"), before)

    def test_verify_sarif_requires_codeql_security_extended_and_all_first_party_roots(self):
        with tempfile.TemporaryDirectory() as tmp:
            sarif_dir = Path(tmp)
            rules = [{"id": f"cpp/rule-{i}", "properties": {"tags": ["security"]}} for i in range(12)]
            artifacts = [
                {"location": {"uri": "src/pdf/document.cpp"}},
                {"location": {"uri": "src/core/runtime.cpp"}},
                {"location": {"uri": "src/services/services.cpp"}},
                {"location": {"uri": "src/ui/ui.cpp"}},
                {"location": {"uri": "src/app/main.cpp"}},
                {"location": {"uri": "src/cli/main.cpp"}},
            ]
            doc = {
                "version": "2.1.0",
                "runs": [{
                    "tool": {"driver": {"name": "CodeQL", "rules": rules}},
                    "invocations": [{"executionSuccessful": True}],
                    "artifacts": artifacts,
                    "results": [],
                }],
            }
            (sarif_dir / "cpp.sarif").write_text(json.dumps(doc), encoding="utf-8")
            code, errs, summary = self.mod.verify_sarif_dir(sarif_dir)
            self.assertEqual((code, errs), (0, []))
            self.assertEqual(summary["extracted_files"], 6)

            # Missing src/pdf/ must fail.
            doc["runs"][0]["artifacts"] = artifacts[1:]
            (sarif_dir / "cpp.sarif").write_text(json.dumps(doc), encoding="utf-8")
            code_no_pdf, errs_no_pdf, _ = self.mod.verify_sarif_dir(sarif_dir)
            self.assertEqual(code_no_pdf, 1)
            self.assertTrue(any("src/pdf/" in e for e in errs_no_pdf), errs_no_pdf)

            # Missing security-extended rules must fail.
            doc["runs"][0]["artifacts"] = artifacts
            doc["runs"][0]["tool"]["driver"]["rules"] = [{"id": "cpp/only-one", "properties": {"tags": ["correctness"]}}]
            (sarif_dir / "cpp.sarif").write_text(json.dumps(doc), encoding="utf-8")
            code_no_sec, errs_no_sec, _ = self.mod.verify_sarif_dir(sarif_dir)
            self.assertEqual(code_no_sec, 1)
            self.assertTrue(any("security-extended" in e for e in errs_no_sec), errs_no_sec)

    def test_codeql_workflow_triggers_permissions_and_skill_contract(self):
        text = WORKFLOW.read_text(encoding="utf-8")
        # security-events: write must be job-scoped only (not in top-level permissions).
        top_perm = text.split("jobs:", 1)[0]
        self.assertNotIn("security-events: write", top_perm)
        self.assertIn("security-events: write", text.split("jobs:", 1)[1])
        self.assertIn("queries: security-extended", text)
        self.assertIn("-NoSccache", text)
        self.assertIn("--verify-sarif", text)

        # Extract push paths and verify representative manifest, library, and engine paths match.
        paths_block = re.search(r"paths:\n((?:\s+-\s+'[^']+'\n)+)", text)
        self.assertIsNotNone(paths_block)
        patterns = re.findall(r"-\s+'([^']+)'", paths_block.group(1))
        for sample in (
            "src/pdf/parser.cpp",
            "libs/vellum/src/button.cpp",
            "vcpkg.json",
            "vcpkg-configuration.json",
            "CMakeLists.txt",
            "build/build.ps1",
            ".github/workflows/codeql.yml",
        ):
            matched = any(
                fnmatch.fnmatch(sample, pat) or (pat.endswith("/**") and sample.startswith(pat[:-3] + "/"))
                for pat in patterns
            )
            self.assertTrue(matched, f"{sample} did not match codeql.yml push paths {patterns}")

        # Both writer process-phase skills must specify ordinary code-scanning.py and exits 0, 1, 2 and 3.
        for skill_path in (
            ROOT / ".agents" / "skills" / "process-phase" / "SKILL.md",
            ROOT / ".claude" / "skills" / "process-phase" / "SKILL.md",
        ):
            skill_text = skill_path.read_text(encoding="utf-8")
            self.assertIn("build\\python.cmd scripts/code-scanning.py", skill_text, str(skill_path))
            self.assertNotIn("code-scanning.py --scheduled", skill_text, str(skill_path))
            for code_label in ("Exit 0:", "Exit 1:", "Exit 2:", "Exit 3:"):
                self.assertIn(code_label, skill_text, f"{skill_path} missing {code_label}")


if __name__ == "__main__":
    unittest.main()
