"""scripts/ci_red_issue.py and .github/workflows/ci-red.yml: one issue per watched workflow, open while main is red,
from GitHub itself (D00 T04 section 13)."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci_red_issue  # noqa: E402  (scripts/ is not a package; the path is set just above)
from fake_gh_ci_red import FakeGh  # noqa: E402

WORKFLOWS = ROOT / ".github" / "workflows"


def run(number, conclusion, created, *, sha=None, attempt=1, title="", workflow="build.yml", status="completed",
        event="push"):
    return {"databaseId": number, "headSha": sha or f"{number:040d}", "status": status, "conclusion": conclusion,
            "url": f"https://github.com/o/r/actions/runs/{number}", "createdAt": created, "event": event,
            "attempt": attempt, "displayTitle": title, "workflow": workflow}


def failed_job(name="Build x64 & ARM64 and run recto-tests"):
    return [{"name": name, "status": "completed", "conclusion": "failure", "url": "https://github.com/o/r/job/1",
             "databaseId": 1}]


def reconcile(fake, workflow="build.yml"):
    said = []
    code = ci_red_issue.main(["reconcile", "--workflow", workflow], runner=fake, out=said.append)
    return code, said


def open_issues(fake):
    return [i for i in fake.state["issues"] if i["state"] == "open"]


class Reconcile(unittest.TestCase):
    def test_a_first_red_opens_one_issue_after_creating_the_label(self):
        fake = FakeGh(runs=[run(11, "failure", "2026-11-16T10:00:00Z")], jobs={"11": failed_job()})
        code, said = reconcile(fake)
        self.assertEqual(code, 0, said)
        writes = fake.issue_calls()
        self.assertEqual([c[:2] for c in writes], [["label", "create"], ["issue", "create"]])
        (issue,) = open_issues(fake)
        self.assertEqual(issue["title"], "main is red: build.yml")
        self.assertEqual(issue["labels"], ["ci-red"])
        self.assertEqual(issue["assignees"], ["rizonesoft"])
        for text in ("Build x64 & ARM64 and run recto-tests", "actions/runs/11", "0" * 38 + "11", "attempt 1"):
            self.assertIn(text, issue["body"])

    def test_a_second_red_comments_once_and_the_same_run_again_adds_nothing(self):
        fake = FakeGh(runs=[run(11, "failure", "2026-11-16T10:00:00Z")], jobs={"11": failed_job(), "12": failed_job()})
        reconcile(fake)
        fake.state["runs"].append(run(12, "failure", "2026-11-16T10:20:00Z"))
        reconcile(fake)
        reconcile(fake)  # the same newest red, reconciled again
        (issue,) = fake.state["issues"]
        self.assertEqual(len(issue["comments"]), 1)
        self.assertIn("actions/runs/12", issue["comments"][0])
        self.assertEqual([c[:2] for c in fake.issue_calls()].count(["issue", "create"]), 1)

    def test_a_green_closes_the_issue_and_a_main_never_red_is_left_alone(self):
        fake = FakeGh(runs=[run(11, "failure", "2026-11-16T10:00:00Z")], jobs={"11": failed_job()})
        reconcile(fake)
        fake.state["runs"].append(run(12, "success", "2026-11-16T10:20:00Z"))
        code, said = reconcile(fake)
        self.assertEqual(code, 0)
        self.assertEqual(open_issues(fake), [])
        self.assertIn("actions/runs/12", fake.state["issues"][0]["comments"][-1])
        self.assertEqual([c[:2] for c in fake.issue_calls()].count(["issue", "close"]), 1)
        never = FakeGh(runs=[run(21, "success", "2026-11-16T10:00:00Z")])
        reconcile(never)
        self.assertEqual(never.issue_calls(), [])

    def test_a_run_cancelled_with_no_verdict_after_it_is_reported(self):
        fake = FakeGh(runs=[run(11, "success", "2026-11-16T10:00:00Z"), run(12, "cancelled", "2026-11-16T10:20:00Z")],
                      jobs={"12": [{"name": "build", "status": "completed", "conclusion": "cancelled", "databaseId": 7}]})
        reconcile(fake)
        (issue,) = open_issues(fake)
        self.assertIn("cancelled, with no verdict after it", issue["body"])
        self.assertIn("actions/runs/12", issue["body"])

    def test_a_timed_out_run_is_red(self):
        fake = FakeGh(runs=[run(11, "cancelled", "2026-11-16T10:00:00Z")],
                      jobs={"11": [{"name": "asan", "status": "completed", "conclusion": "cancelled", "databaseId": 9,
                                    "url": "https://github.com/o/r/job/9"}]},
                      annotations={"9": [{"message": "The job running on runner X has exceeded the maximum execution "
                                                     "time of 45 minutes."}]})
        reconcile(fake)
        (issue,) = open_issues(fake)
        self.assertIn("is red", issue["body"])
        self.assertIn("timed out", issue["body"])

    def test_no_completed_run_changes_nothing(self):
        fake = FakeGh(runs=[run(11, None, "2026-11-16T10:00:00Z", status="in_progress")])
        code, said = reconcile(fake)
        self.assertEqual((code, fake.issue_calls()), (0, []))


class Order(unittest.TestCase):
    """The reconciler reads main's verdict, never the run that woke it, so finishing order cannot matter."""

    def both_orders(self, runs, jobs=None, issue_open=False):
        outcomes = []
        for order in (runs, list(reversed(runs))):
            fake = FakeGh(runs=list(order), jobs=jobs or {})
            if issue_open:
                fake.state["labels"].append("ci-red")
                fake.state["issues"].append({"number": 1, "title": "main is red: build.yml", "body": "earlier",
                                             "labels": ["ci-red"], "assignees": [], "state": "open", "comments": []})
            reconcile(fake)
            outcomes.append(len(open_issues(fake)))
        return outcomes

    def test_an_older_green_finishing_after_a_newer_red_leaves_the_issue_open(self):
        runs = [run(11, "success", "2026-11-16T10:00:00Z"), run(12, "failure", "2026-11-16T10:20:00Z")]
        self.assertEqual(self.both_orders(runs, {"12": failed_job()}, issue_open=True), [1, 1])

    def test_an_older_red_finishing_after_a_newer_green_opens_nothing(self):
        runs = [run(11, "failure", "2026-11-16T10:00:00Z"), run(12, "success", "2026-11-16T10:20:00Z")]
        self.assertEqual(self.both_orders(runs, {"11": failed_job()}), [0, 0])

    def test_a_rerun_attempt_of_an_older_run_does_not_outrank_the_newer_green(self):
        runs = [run(11, "failure", "2026-11-16T10:00:00Z", attempt=2), run(12, "success", "2026-11-16T10:20:00Z")]
        self.assertEqual(self.both_orders(runs, {"11": failed_job()}), [0, 0])

    def test_a_rebuild_of_an_older_commit_never_decides_main(self):
        runs = [run(11, "success", "2026-11-16T10:00:00Z"),
                run(12, "failure", "2026-11-16T10:20:00Z", title="rebuild of " + "a" * 40, event="workflow_dispatch")]
        self.assertEqual(self.both_orders(runs, {"12": failed_job()}), [0, 0])

    def test_the_deliberate_proof_red_counts_here(self):
        runs = [run(11, "success", "2026-11-16T10:00:00Z", workflow="plan.yml"),
                run(12, "failure", "2026-11-16T10:20:00Z", workflow="plan.yml", title="ci-red proof: a deliberate red",
                    event="workflow_dispatch")]
        fake = FakeGh(runs=runs, jobs={"12": failed_job("plan-gates")})
        reconcile(fake, "plan.yml")
        self.assertEqual([i["title"] for i in open_issues(fake)], ["main is red: plan.yml"])


class Refusals(unittest.TestCase):
    def test_a_failing_gh_exits_3_and_writes_nothing(self):
        fake = FakeGh(runs=[run(11, "failure", "2026-11-16T10:00:00Z")], jobs={"11": failed_job()},
                      fail=["issue", "list"])
        code, said = reconcile(fake)
        self.assertEqual(code, 3)
        self.assertIn("unknown, nothing changed", said[-1])
        self.assertEqual(fake.issue_calls(), [])
        fake = FakeGh(fail=["run", "list"])
        self.assertEqual(reconcile(fake)[0], 3)

    def test_a_workflow_that_is_not_watched_exits_2(self):
        fake = FakeGh()
        self.assertEqual(reconcile(fake, "release.yml")[0], 2)
        self.assertEqual(fake.calls, [])

    def test_the_event_s_folder_path_names_the_same_workflow(self):
        fake = FakeGh(runs=[run(11, "failure", "2026-11-16T10:00:00Z")], jobs={"11": failed_job()})
        self.assertEqual(reconcile(fake, ".github/workflows/build.yml")[0], 0)
        self.assertEqual(len(open_issues(fake)), 1)


def workflow_text(name):
    return (WORKFLOWS / name).read_text(encoding="utf-8")


class Wiring(unittest.TestCase):
    """.github/workflows/ci-red.yml, read and its own run: line executed against a fake gh on PATH."""

    def setUp(self):
        self.text = workflow_text("ci-red.yml")

    def test_it_watches_exactly_both_workflows_by_their_names_on_main(self):
        names = [re.search(r"^name:\s*(\S+)", workflow_text(f), re.M).group(1) for f in ("build.yml", "plan.yml")]
        self.assertEqual(names, ["build", "plan-gates"])
        self.assertRegex(self.text, r"workflow_run:\s*\n\s+workflows: \[build, plan-gates\]\s*\n\s+types: \[completed\]"
                                    r"\s*\n\s+branches: \[main\]")

    def test_permissions_and_the_serialising_concurrency_group(self):
        block = re.search(r"^permissions:\n((?:  .*\n)+)", self.text, re.M).group(1)
        self.assertEqual(sorted(line.strip() for line in block.splitlines()),
                         ["actions: read", "checks: read", "contents: read", "issues: write"])
        self.assertIn("group: ci-red-${{ github.event.workflow_run.path }}", self.text)
        self.assertIn("cancel-in-progress: false", self.text)

    def env_and_run(self):
        step = self.text[self.text.index("- name: Reconcile the ci-red issue"):]
        env = dict(re.findall(r"^\s+([A-Z_]+): (\$\{\{ [^}]+ \}\})\s*$", step.split("run:")[0], re.M))
        run_line = re.search(r"^\s+run: (.+)$", step, re.M).group(1)
        return env, run_line

    def test_the_token_and_repository_are_bound(self):
        env, run_line = self.env_and_run()
        self.assertEqual(env, {"GH_TOKEN": "${{ github.token }}", "GH_REPO": "${{ github.repository }}",
                               "WORKFLOW_FILE": "${{ github.event.workflow_run.path }}"})
        self.assertEqual(run_line, 'python3 scripts/ci_red_issue.py reconcile --workflow "$WORKFLOW_FILE"')

    def execute(self, runs, jobs=None, annotations=None, token=True):
        """Run the step's own command, as the runner would, with a fake gh first on PATH."""
        env_map, run_line = self.env_and_run()
        values = {"${{ github.token }}": "token", "${{ github.repository }}": "o/r",
                  "${{ github.event.workflow_run.path }}": ".github/workflows/build.yml"}
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            fake = FakeGh(runs=runs, jobs=jobs or {}, annotations=annotations or {})
            state = tmp / "state.json"
            state.write_text(json.dumps(fake.state), encoding="utf-8")
            script = Path(__file__).resolve().parent / "fake_gh_ci_red.py"
            if os.name == "nt":
                (tmp / "gh.cmd").write_text(f'@"{sys.executable}" "{script}" %*\r\n', encoding="ascii")
            else:
                (tmp / "gh").write_text(f'#!/bin/sh\nexec "{sys.executable}" "{script}" "$@"\n', encoding="ascii")
                (tmp / "gh").chmod(0o755)
            env = {k: v for k, v in os.environ.items() if k not in ("GH_TOKEN", "GITHUB_TOKEN", "GH_REPO")}
            env.update({name: values[expr] for name, expr in env_map.items()})
            if not token:
                env.pop("GH_TOKEN")
            env["PATH"] = str(tmp) + os.pathsep + env.get("PATH", "")
            env["FAKE_GH_STATE"] = str(state)
            argv = shlex.split(run_line.replace('"$WORKFLOW_FILE"', shlex.quote(env["WORKFLOW_FILE"])))
            self.assertEqual(argv[0], "python3")
            done = subprocess.run([sys.executable, *argv[1:]], cwd=ROOT, env=env, capture_output=True, text=True,
                                  timeout=120)
            return done, json.loads(state.read_text(encoding="utf-8"))

    def test_the_step_opens_for_failed_cancelled_and_timed_out_runs_and_not_for_a_skipped_job_green(self):
        cases = {
            "failure": ([run(11, "failure", "2026-11-16T10:00:00Z")], {"11": failed_job()}, {}),
            "cancelled": ([run(11, "success", "2026-11-16T09:00:00Z"), run(12, "cancelled", "2026-11-16T10:00:00Z")],
                          {"12": [{"name": "build", "status": "completed", "conclusion": "cancelled", "databaseId": 3}]},
                          {}),
            "timed out": ([run(11, "cancelled", "2026-11-16T10:00:00Z")],
                          {"11": [{"name": "asan", "status": "completed", "conclusion": "cancelled", "databaseId": 4}]},
                          {"4": [{"message": "has exceeded the maximum execution time of 45 minutes"}]}),
        }
        for name, (runs, jobs, notes) in cases.items():
            with self.subTest(name):
                done, state = self.execute(runs, jobs, notes)
                self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
                self.assertEqual([i["title"] for i in state["issues"] if i["state"] == "open"], ["main is red: build.yml"])
        # A green run whose conditional job was skipped is green: GitHub reports the run itself as success.
        done, state = self.execute([run(11, "success", "2026-11-16T10:00:00Z")],
                                   {"11": [{"name": "test-list-note", "status": "completed", "conclusion": "skipped"}]})
        self.assertEqual((done.returncode, state["issues"]), (0, []))

    def test_without_the_token_the_step_fails(self):
        done, state = self.execute([run(11, "failure", "2026-11-16T10:00:00Z")], {"11": failed_job()}, token=False)
        self.assertEqual(done.returncode, 3, done.stdout + done.stderr)
        self.assertIn("GH_TOKEN", done.stdout)
        self.assertEqual(state["issues"], [])


class NamedAtTheSource(unittest.TestCase):
    """The runs that are not main's verdict are named by the workflows themselves."""

    def test_build_names_a_rebuild_of_an_older_commit(self):
        self.assertIn("run-name: ${{ inputs.sha != '' && inputs.sha != github.sha && "
                      "format('rebuild of {0}', inputs.sha) || '' }}", workflow_text("build.yml"))

    def test_plan_has_the_deliberate_red_input_its_failing_step_and_its_name(self):
        text = workflow_text("plan.yml")
        self.assertIn("run-name: ${{ inputs.fail_on_purpose && 'ci-red proof: a deliberate red' || '' }}", text)
        self.assertRegex(text, r"fail_on_purpose:\s*\n(\s+.*\n)*?\s+type: boolean\s*\n(\s+.*\n)*?\s+default: false")
        step = re.search(r"- name: Fail on purpose \(ci-red proof\)\n\s+if: (.+)\n(?:\s+.*\n)*?\s+run: (.+)", text)
        self.assertEqual(step.group(1).strip(), "inputs.fail_on_purpose")
        self.assertIn("throw", step.group(2))
        first = re.search(r"plan-gates:\n\s+runs-on: .+\n\s+steps:\n\s+- name: (.+)", text).group(1)
        self.assertEqual(first, "Fail on purpose (ci-red proof)")
        self.assertEqual(ci_red_issue.ci_status.PROOF_TITLE, "ci-red proof: a deliberate red")
        self.assertEqual(ci_red_issue.ci_status.REBUILD_PREFIX, "rebuild of ")


if __name__ == "__main__":
    unittest.main()
