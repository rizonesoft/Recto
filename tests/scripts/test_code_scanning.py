"""Tests for scripts/code-scanning.py and .github/workflows/codeql.yml (D06 T02 s11)."""
from __future__ import annotations

import fnmatch
import importlib.util
import json
import os
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


def _analysis(
    *,
    commit_sha: str = "a" * 40,
    ref: str = "refs/heads/main",
    tool_name: str = "CodeQL",
    category: str = "/language:c-cpp",
    error: str = "",
) -> dict:
    return {
        "id": 1,
        "commit_sha": commit_sha,
        "ref": ref,
        "tool": {"name": tool_name},
        "category": category,
        "error": error,
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
    analyses_data = [_analysis()] if analyses is None else analyses
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
        rc = self.mod.check_ordinary(repo="example/fork", runner=_make_runner(calls=calls), out=lines.append)
        self.assertEqual(rc, 0, lines)
        self.assertTrue(any("0 open alerts on main" in ln for ln in lines), lines)
        run_calls = [c for c in calls if c[:2] == ["run", "list"]]
        self.assertEqual(len(run_calls), 1)
        self.assertIn("--repo", run_calls[0])
        self.assertIn("example/fork", run_calls[0])
        alert_calls = [c for c in calls if c[0] == "api" and "/code-scanning/alerts" in c[1]]
        self.assertEqual(len(alert_calls), 1)
        self.assertIn("repos/example/fork/code-scanning/alerts", alert_calls[0][1])
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

    def test_malformed_alert_record_exits_3_never_0(self):
        for bad_record in ({}, {"number": 1}, {"state": "open"}, {"number": True, "state": "open"}, "not-a-dict"):
            lines: list[str] = []
            rc = self.mod.check_ordinary(
                runner=_make_runner(alert_pages={1: (0, [bad_record], "")}),
                out=lines.append,
            )
            self.assertEqual(rc, 3, (bad_record, lines))

    def test_mismatched_or_errored_analysis_exits_1_in_both_modes(self):
        bad_analyses = [
            [_analysis(commit_sha="b" * 40)],
            [_analysis(tool_name="OtherScanner")],
            [_analysis(category="/language:python")],
            [_analysis(error="upload failed")],
        ]
        for analyses in bad_analyses:
            rc_ord = self.mod.check_ordinary(runner=_make_runner(analyses=analyses), out=lambda _: None)
            self.assertEqual(rc_ord, 1, analyses)
            with tempfile.TemporaryDirectory() as tmp:
                rc_sched = self.mod.check_scheduled(
                    runner=_make_runner(analyses=analyses),
                    root=Path(tmp),
                    now=lambda: 1700000010.0,
                    out=lambda _: None,
                )
                self.assertEqual(rc_sched, 1, analyses)

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
        self.assertEqual(self.mod.check_ordinary(runner=_make_runner(analyses=[]), out=lambda _: None), 1)
        self.assertEqual(self.mod.check_ordinary(runner=_make_runner(analyses_rc=404), out=lambda _: None), 3)
        self.assertEqual(self.mod.check_ordinary(runner=_make_runner(analyses_rc=1), out=lambda _: None), 3)

    def test_scheduled_pending_scan_still_reports_existing_open_alerts(self):
        pending_run = [{"databaseId": 102, "status": "in_progress", "conclusion": "",
                        "headSha": "c" * 40, "url": "https://github.com/rizonesoft/Recto/actions/runs/102",
                        "event": "push", "createdAt": "2026-11-27T06:05:00Z"}]
        with tempfile.TemporaryDirectory() as tmp:
            lines: list[str] = []
            rc = self.mod.check_scheduled(
                runner=_make_runner(runs=pending_run, alert_pages={1: (0, [_alert(7)], "")}),
                root=Path(tmp),
                now=lambda: 1700000010.0,
                out=lines.append,
            )
            self.assertEqual(rc, 1, lines)
            self.assertIn("#7 ", "\n".join(lines))

    def test_scheduled_persists_confirmed_receipt_before_intervening_outage(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            last_path = root / "build" / "automations" / "recto-code-scanning" / "last.json"
            last_path.parent.mkdir(parents=True)
            runner_open = _make_runner(alert_pages={1: (0, [_alert(7)], "")})

            # Poll 1 raises alert #7.
            rc1 = self.mod.check_scheduled(runner=runner_open, root=root, now=lambda: 1700000010.0, out=lambda _: None)
            self.assertEqual(rc1, 1)
            # Runner confirms email sent for poll 1.
            last_path.write_text(json.dumps({
                "started": "2023-11-14T22:13:25Z",
                "finished": "2023-11-14T22:13:35Z",
                "exit": 1,
                "alert": "sent",
            }), encoding="utf-8")

            # Poll 2 encounters an API outage (exit 3), and runner overwrites last.json with exit 3.
            rc2 = self.mod.check_scheduled(runner=_make_runner(run_rc=1), root=root, now=lambda: 1700000100.0, out=lambda _: None)
            self.assertEqual(rc2, 3)
            last_path.write_text(json.dumps({
                "started": "2023-11-14T22:14:55Z",
                "finished": "2023-11-14T22:15:05Z",
                "exit": 3,
                "alert": "none",
            }), encoding="utf-8")

            # Poll 3 runs healthy with #7 still open: MUST NOT re-send #7 because poll 2 persisted receipt.
            rc3 = self.mod.check_scheduled(runner=runner_open, root=root, now=lambda: 1700000200.0, out=lambda _: None)
            self.assertEqual(rc3, 0)

    def test_scheduled_deduplicates_failed_workflow_runs_by_database_id(self):
        failed_101 = [{"databaseId": 101, "status": "completed", "conclusion": "failure",
                       "headSha": "b" * 40, "url": "https://github.com/rizonesoft/Recto/actions/runs/101",
                       "event": "push", "createdAt": "2026-11-27T06:00:00Z"}]
        failed_102 = [{"databaseId": 102, "status": "completed", "conclusion": "failure",
                       "headSha": "c" * 40, "url": "https://github.com/rizonesoft/Recto/actions/runs/102",
                       "event": "push", "createdAt": "2026-11-28T06:00:00Z"}]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            last_path = root / "build" / "automations" / "recto-code-scanning" / "last.json"
            last_path.parent.mkdir(parents=True)

            # First poll reports failed run 101.
            rc1 = self.mod.check_scheduled(
                runner=_make_runner(runs=failed_101), root=root, now=lambda: 1700000010.0, out=lambda _: None
            )
            self.assertEqual(rc1, 1)
            last_path.write_text(json.dumps({
                "started": "2023-11-14T22:13:25Z",
                "finished": "2023-11-14T22:13:35Z",
                "exit": 1,
                "alert": "sent",
            }), encoding="utf-8")

            # Second poll on the same failed run 101 returns 0 (one email per failed run).
            rc2 = self.mod.check_scheduled(
                runner=_make_runner(runs=failed_101), root=root, now=lambda: 1700000100.0, out=lambda _: None
            )
            self.assertEqual(rc2, 0)

            # A new failed run 102 returns 1 again.
            rc3 = self.mod.check_scheduled(
                runner=_make_runner(runs=failed_102), root=root, now=lambda: 1700000200.0, out=lambda _: None
            )
            self.assertEqual(rc3, 1)

    def test_scheduled_reports_both_failed_run_and_new_open_alert_together(self):
        failed_77 = [{"databaseId": 77, "status": "completed", "conclusion": "failure",
                      "headSha": "b" * 40, "url": "https://github.com/rizonesoft/Recto/actions/runs/77",
                      "event": "push", "createdAt": "2026-11-27T06:00:00Z"}]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            last_path = root / "build" / "automations" / "recto-code-scanning" / "last.json"
            last_path.parent.mkdir(parents=True)
            lines: list[str] = []
            rc1 = self.mod.check_scheduled(
                runner=_make_runner(runs=failed_77, alert_pages={1: (0, [_alert(7)], "")}),
                root=root,
                now=lambda: 1700000010.0,
                out=lines.append,
            )
            self.assertEqual(rc1, 1)
            joined = "\n".join(lines)
            self.assertIn("ended with failure", joined)
            self.assertIn("#7 ", joined)

            state = json.loads((root / "build" / "code-scanning" / "state.json").read_text(encoding="utf-8"))
            bucket = state["scopes"]["rizonesoft/Recto@main"]
            self.assertEqual(bucket["raised_alerts"], [7])
            self.assertEqual(bucket["raised_failed_run"], 77)

            # Confirm delivery -> next poll acknowledges both alert #7 and failed run 77.
            last_path.write_text(json.dumps({
                "started": "2023-11-14T22:13:25Z",
                "finished": "2023-11-14T22:13:35Z",
                "exit": 1,
                "alert": "sent",
            }), encoding="utf-8")
            rc2 = self.mod.check_scheduled(
                runner=_make_runner(runs=failed_77, alert_pages={1: (0, [_alert(7)], "")}),
                root=root,
                now=lambda: 1700000100.0,
                out=lambda _: None,
            )
            self.assertEqual(rc2, 0)

    def test_scheduled_namespaces_state_by_repo_and_branch_and_skips_unchanged_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            last_path = root / "build" / "automations" / "recto-code-scanning" / "last.json"
            last_path.parent.mkdir(parents=True)

            # Acknowledge alert #7 on rizonesoft/Recto@main.
            runner_7 = _make_runner(alert_pages={1: (0, [_alert(7)], "")})
            self.assertEqual(
                self.mod.check_scheduled(runner=runner_7, root=root, now=lambda: 1700000010.0, out=lambda _: None),
                1,
            )
            last_path.write_text(json.dumps({
                "started": "2023-11-14T22:13:25Z",
                "finished": "2023-11-14T22:13:35Z",
                "exit": 1,
                "alert": "sent",
            }), encoding="utf-8")
            self.assertEqual(
                self.mod.check_scheduled(runner=runner_7, root=root, now=lambda: 1700000100.0, out=lambda _: None),
                0,
            )

            # A different repo with alert #7 MUST NOT inherit rizonesoft/Recto@main's acknowledgment.
            self.assertEqual(
                self.mod.check_scheduled(
                    runner=runner_7, repo="example/fork", root=root, now=lambda: 1700000200.0, out=lambda _: None
                ),
                1,
            )

            # Unchanged clean poll on rizonesoft/Recto@main performs zero atomic replaces.
            writes = []
            def tracking_replace(src, dst):
                writes.append((src, dst))
                return os.replace(src, dst)

            rc_clean = self.mod.check_scheduled(
                runner=runner_7, root=root, now=lambda: 1700000300.0, out=lambda _: None, replace=tracking_replace
            )
            self.assertEqual(rc_clean, 0)
            self.assertEqual(len(writes), 0, f"expected 0 writes on unchanged poll, got {writes}")

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
                self.mod._write_json_atomic(state_path, {"scopes": {}}, replace=broken_replace)
            self.assertEqual(state_path.read_text(encoding="utf-8"), before)

    def test_resolve_security_extended_rules_extracts_ids_from_ql_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            q1 = Path(tmp) / "WeakCrypto.ql"
            q2 = Path(tmp) / "OverrunningWrite.ql"
            q1.write_text("/**\n * @name Weak crypto\n * @id cpp/weak-cryptographic-algorithm\n */\n", encoding="utf-8")
            q2.write_text("/**\n * @name Overrunning write\n * @id cpp/overrunning-write\n */\n", encoding="utf-8")

            def fake_run(cmd):
                self.assertEqual(cmd[:4], ["codeql", "resolve", "queries", "--format=json"])
                self.assertIn("cpp-security-extended.qls", cmd[4])
                return 0, json.dumps([str(q1), str(q2)]), ""

            resolved = self.mod.resolve_security_extended_rules("codeql", run_cmd=fake_run)
            self.assertEqual(resolved, {"cpp/weak-cryptographic-algorithm", "cpp/overrunning-write"})

            # Missing @id tag must fail loudly.
            q_bad = Path(tmp) / "NoId.ql"
            q_bad.write_text("/** no id here */\n", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                self.mod.resolve_security_extended_rules(
                    "codeql", run_cmd=lambda _cmd: (0, json.dumps([str(q_bad)]), "")
                )

    def test_verify_sarif_requires_exact_security_extended_suite_and_first_party_translation_units(self):
        with tempfile.TemporaryDirectory() as tmp:
            sarif_dir = Path(tmp)
            expected_ids = {f"cpp/high-sec-{i}" for i in range(15)} | {f"cpp/med-sec-{i}" for i in range(20)}
            all_rules = [{"id": rid} for rid in sorted(expected_ids)]
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
                    "properties": {
                        "codeqlConfigSummary": {
                            "queries": [{"type": "builtinSuite", "uses": "security-extended"}]
                        }
                    },
                    "tool": {
                        "driver": {"name": "CodeQL"},
                        "extensions": [{"name": "codeql/cpp-queries", "rules": all_rules}],
                    },
                    "invocations": [{"executionSuccessful": True}],
                    "artifacts": artifacts,
                    "results": [],
                }],
            }
            (sarif_dir / "cpp.sarif").write_text(json.dumps(doc), encoding="utf-8")
            code, errs, summary = self.mod.verify_sarif_dir(sarif_dir, expected_rule_ids=expected_ids)
            self.assertEqual((code, errs), (0, []))
            self.assertEqual(summary["extracted_files"], 6)
            self.assertEqual(summary["rules"], len(expected_ids))

            # Filtering out even ONE single high-precision query from security-extended must FAIL
            # and name the exact omitted query ID.
            doc["runs"][0]["tool"]["extensions"][0]["rules"] = [
                r for r in all_rules if r["id"] != "cpp/high-sec-0"
            ]
            (sarif_dir / "cpp.sarif").write_text(json.dumps(doc), encoding="utf-8")
            code_one, errs_one, _ = self.mod.verify_sarif_dir(sarif_dir, expected_rule_ids=expected_ids)
            self.assertEqual(code_one, 1)
            self.assertTrue(any("cpp/high-sec-0" in e for e in errs_one), errs_one)

            # Header-only artifact under src/pdf/ must NOT certify src/pdf/ translation units.
            doc["runs"][0]["tool"]["extensions"][0]["rules"] = all_rules
            doc["runs"][0]["artifacts"] = [{"location": {"uri": "src/pdf/error.h"}}] + artifacts[1:]
            (sarif_dir / "cpp.sarif").write_text(json.dumps(doc), encoding="utf-8")
            code_hdr, errs_hdr, _ = self.mod.verify_sarif_dir(sarif_dir, expected_rule_ids=expected_ids)
            self.assertEqual(code_hdr, 1)
            self.assertTrue(any("src/pdf/" in e for e in errs_hdr), errs_hdr)

            # Dependency TU with matching suffix (vcpkg_installed/vendor/src/pdf/main.cpp) must NOT satisfy src/pdf/.
            doc["runs"][0]["artifacts"] = [
                {"location": {"uri": "vcpkg_installed/vendor/src/pdf/main.cpp"}}
            ] + artifacts[1:]
            (sarif_dir / "cpp.sarif").write_text(json.dumps(doc), encoding="utf-8")
            code_dep, errs_dep, _ = self.mod.verify_sarif_dir(sarif_dir, expected_rule_ids=expected_ids)
            self.assertEqual(code_dep, 1)
            self.assertTrue(any("src/pdf/" in e for e in errs_dep), errs_dep)

            # Missing codeqlConfigSummary must FAIL even if all rule IDs are present.
            doc["runs"][0]["artifacts"] = artifacts
            doc["runs"][0]["properties"] = {}
            (sarif_dir / "cpp.sarif").write_text(json.dumps(doc), encoding="utf-8")
            code_nocfg, errs_nocfg, _ = self.mod.verify_sarif_dir(sarif_dir, expected_rule_ids=expected_ids)
            self.assertEqual(code_nocfg, 1)
            self.assertTrue(any("codeqlConfigSummary" in e for e in errs_nocfg), errs_nocfg)

            # Mismatched codeqlConfigSummary ("default") must also FAIL.
            doc["runs"][0]["properties"] = {
                "codeqlConfigSummary": {"queries": [{"type": "builtinSuite", "uses": "default"}]}
            }
            (sarif_dir / "cpp.sarif").write_text(json.dumps(doc), encoding="utf-8")
            code_cfg, errs_cfg, _ = self.mod.verify_sarif_dir(sarif_dir, expected_rule_ids=expected_ids)
            self.assertEqual(code_cfg, 1)
            self.assertTrue(any("codeqlConfigSummary" in e for e in errs_cfg), errs_cfg)

            # CLI refuses --verify-sarif without --codeql-cli or --expected-rules.
            cli_lines: list[str] = []
            rc_missing_arg = self.mod.main(["--verify-sarif", str(sarif_dir)], out=cli_lines.append)
            self.assertEqual(rc_missing_arg, 1)
            self.assertTrue(any("--codeql-cli" in ln for ln in cli_lines), cli_lines)

    def test_codeql_workflow_triggers_permissions_and_skill_contract(self):
        text = WORKFLOW.read_text(encoding="utf-8")
        # security-events: write must be job-scoped only (not in top-level permissions).
        top_perm = text.split("jobs:", 1)[0]
        self.assertNotIn("security-events: write", top_perm)
        self.assertIn("security-events: write", text.split("jobs:", 1)[1])
        self.assertIn("queries: security-extended", text)
        self.assertIn("-NoSccache", text)
        self.assertIn("--verify-sarif", text)
        self.assertIn("--codeql-cli", text)

        # Extract push paths and verify representative manifest, library, engine, and verifier paths match.
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
            "scripts/code-scanning.py",
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
