"""scripts/ci_status.py: the CI state of main read through a fake gh, and never green when gh cannot answer."""
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest
import unittest.mock
from contextlib import redirect_stdout

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts" / "ci_status.py"
spec = importlib.util.spec_from_file_location("ci_status", SCRIPT)
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)

SHA_OLD, SHA_NEW = "a" * 40, "b" * 40


def run(n, status="completed", conclusion="success", sha=SHA_NEW):
    return {"databaseId": n, "headSha": sha, "status": status, "conclusion": conclusion if status == "completed" else "",
            "url": f"https://github.com/o/r/actions/runs/{n}", "createdAt": f"2026-10-23T00:00:{n:02d}Z", "event": "push"}


class FakeGh:
    """An in-process gh: runs per workflow, jobs per run id, or a failure for every call."""

    def __init__(self, runs=None, jobs=None, fail=None, missing=False):
        self.runs, self.jobs, self.fail, self.missing, self.calls = runs or {}, jobs or {}, fail, missing, []

    def __call__(self, args):
        self.calls.append(args)
        if self.missing:
            raise FileNotFoundError("gh")
        if self.fail:
            return self.fail
        if args[:2] == ["run", "list"]:
            assert args[args.index("--json") + 1] == ci.FIELDS and args[args.index("--limit") + 1] == "10"
            assert args[args.index("--branch") + 1] == "main"
            return 0, json.dumps(self.runs.get(args[args.index("--workflow") + 1], [])), ""
        if args[:2] == ["run", "view"]:
            return 0, json.dumps({"jobs": self.jobs.get(int(args[2]), [])}), ""
        raise AssertionError(args)


def status(fake, *argv):
    out = io.StringIO()
    with redirect_stdout(out):
        code = ci.main(["--json", *argv], runner=fake)
    return code, json.loads(out.getvalue())


class CiStatus(unittest.TestCase):
    def test_green(self):
        code, report = status(FakeGh({"build.yml": [run(2), run(1, sha=SHA_OLD)], "plan.yml": [run(3)]}))
        self.assertEqual(code, 0)
        self.assertEqual([(w["workflow"], w["verdict"], w["in_progress"]) for w in report["workflows"]],
                         [("build.yml", "green", 0), ("plan.yml", "green", 0)])
        self.assertEqual(report["workflows"][0]["verdict_run"]["headSha"], SHA_NEW)

    def test_red_names_the_workflow_commit_url_and_failed_jobs(self):
        fake = FakeGh({"build.yml": [run(5, conclusion="failure"), run(4)], "plan.yml": [run(3)]},
                      jobs={5: [{"name": "x64 Release", "status": "completed", "conclusion": "failure", "url": "u1"},
                                {"name": "ARM64 Debug", "status": "completed", "conclusion": "success", "url": "u2"},
                                {"name": "asan", "status": "completed", "conclusion": "timed_out", "url": "u3"}]})
        code, report = status(fake)
        self.assertEqual(code, 1)
        build = report["workflows"][0]
        self.assertEqual((build["verdict"], build["verdict_run"]["headSha"], build["verdict_run"]["url"]),
                         ("red", SHA_NEW, "https://github.com/o/r/actions/runs/5"))
        self.assertEqual([j["name"] for j in build["failed_jobs"]], ["x64 Release", "asan"])
        self.assertIn(["run", "view", "5", "--json", "jobs"], fake.calls)
        out = io.StringIO()
        with redirect_stdout(out):
            ci.main([], runner=fake)
        text = out.getvalue()
        for expected in ("build.yml: red at bbbbbbbbbbbb (https://github.com/o/r/actions/runs/5)",
                         "failed job: x64 Release (failure) u1", "failed job: asan (timed_out) u3"):
            self.assertIn(expected, text)

    def test_in_progress_runs_are_reported_and_do_not_decide(self):
        fake = FakeGh({"build.yml": [run(9, status="in_progress"), run(8, status="queued"), run(7, conclusion="failure")],
                       "plan.yml": [run(6, status="in_progress"), run(5)]}, jobs={7: []})
        code, report = status(fake)
        self.assertEqual(code, 1, "a newer run still in progress does not make an older red run green")
        self.assertEqual([(w["verdict"], w["in_progress"]) for w in report["workflows"]], [("red", 2), ("green", 1)])

    def test_a_cancelled_run_carries_no_verdict(self):
        code, report = status(FakeGh({"build.yml": [run(3, conclusion="cancelled"), run(2)],
                                      "plan.yml": [run(4, conclusion="cancelled"), run(1, conclusion="failure")]}))
        self.assertEqual(code, 1)
        build, plan = report["workflows"]
        self.assertEqual((build["latest_completed_state"], build["verdict"]), ("cancelled", "green"))
        self.assertEqual((plan["latest_completed_state"], plan["verdict"]), ("cancelled", "red"))
        code, report = status(FakeGh({"build.yml": [run(3, conclusion="cancelled")], "plan.yml": [run(1)]}))
        self.assertEqual((code, report["workflows"][0]["verdict"]), (0, "none"))

    def test_no_runs(self):
        code, report = status(FakeGh({}))
        self.assertEqual(code, 0)
        self.assertEqual([(w["verdict"], w["latest_completed"], w["in_progress"]) for w in report["workflows"]],
                         [("none", None, 0), ("none", None, 0)])

    def test_gh_missing_unauthenticated_or_failing_is_never_green(self):
        cases = {"missing": FakeGh(missing=True),
                 "unauthenticated": FakeGh(fail=(4, "", "To get started with GitHub CLI, please run:  gh auth login")),
                 "not json": FakeGh(fail=(0, "<html>", ""))}
        for name, fake in cases.items():
            with self.subTest(name):
                code, report = status(fake)
                self.assertEqual(code, 3)
                self.assertEqual(report["exit"], 3)
                self.assertNotIn("workflows", report)
        self.assertIn("gh auth login", status(cases["unauthenticated"])[1]["error"])

    def test_bad_usage(self):
        for argv in (["--workflow", "nightly.yml"], ["--workflow", "../build.yml"], ["--branch", " "], ["--nope"]):
            with self.subTest(argv=argv), self.assertRaises(SystemExit) as ctx, redirect_stdout(io.StringIO()):
                with unittest.mock.patch("sys.stderr", io.StringIO()):
                    ci.main(argv, runner=FakeGh())
            self.assertEqual(ctx.exception.code, 2)

    def test_the_watched_workflows_exist(self):
        for workflow in ci.WORKFLOWS:
            self.assertTrue((REPO / ".github" / "workflows" / workflow).is_file(), workflow)


FAKE_GH = textwrap.dedent('''\
    import json, os, sys
    args = sys.argv[1:]
    if os.environ.get("FAKE_GH") == "unauth":
        sys.stderr.write("To get started with GitHub CLI, please run:  gh auth login\\n")
        sys.exit(4)
    if args[:2] == ["run", "list"]:
        print(json.dumps([{"databaseId": 1, "headSha": "c" * 40, "status": "completed", "conclusion": "success",
                           "url": "https://example.invalid/1", "createdAt": "2026-10-23T00:00:00Z", "event": "push"}]))
''')


class RealProcess(unittest.TestCase):
    """The default runner against a fake gh executable on PATH, and with no gh at all."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.bin = Path(self._tmp.name) / "bin"
        self.bin.mkdir()
        fake = Path(self._tmp.name) / "fake_gh.py"
        fake.write_text(FAKE_GH, encoding="utf-8")
        if os.name == "nt":
            (self.bin / "gh.cmd").write_text(f'@"{sys.executable}" "{fake}" %*\r\n', encoding="utf-8")
        else:
            launcher = self.bin / "gh"
            launcher.write_text(f'#!/bin/sh\nexec "{sys.executable}" "{fake}" "$@"\n', encoding="utf-8")
            launcher.chmod(0o755)

    def tearDown(self):
        self._tmp.cleanup()

    def script(self, path, **env):
        system = {"SYSTEMROOT": os.environ["SYSTEMROOT"]} if os.name == "nt" else {}
        return subprocess.run([sys.executable, str(SCRIPT), "--json"], capture_output=True, text=True, timeout=120,
                              env={"PATH": path, **system, **env}, cwd=self._tmp.name)

    def test_green_through_a_fake_gh(self):
        result = self.script(str(self.bin))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual([w["verdict"] for w in json.loads(result.stdout)["workflows"]], ["green", "green"])

    def test_unauthenticated_gh_exits_3(self):
        result = self.script(str(self.bin), FAKE_GH="unauth")
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertIn("gh auth login", json.loads(result.stdout)["error"])

    def test_no_gh_on_path_exits_3(self):
        empty = Path(self._tmp.name) / "empty"
        empty.mkdir()
        result = self.script(str(empty))
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout)["error"], "gh is not installed or not on PATH")


def wrun(n, workflow, status="completed", conclusion="success", sha=SHA_NEW):
    entry = run(n, status=status, conclusion=conclusion, sha=sha)
    entry.update({"workflowName": workflow, "attempt": 1})
    return entry


class WaitGh:
    """gh for --wait: per workflow, a list of answers, one per poll (the last repeats); jobs per run id."""

    def __init__(self, polls, jobs=None):
        self.polls, self.jobs, self.count = polls, jobs or {}, {}

    def __call__(self, args):
        if args[:2] == ["run", "list"]:
            workflow = args[args.index("--workflow") + 1]
            assert args[args.index("--commit") + 1] == SHA_NEW
            answers = self.polls.get(workflow, [[]])
            i = self.count.get(workflow, 0)
            self.count[workflow] = i + 1
            return 0, json.dumps(answers[min(i, len(answers) - 1)]), ""
        if args[:2] == ["run", "view"]:
            return 0, json.dumps({"jobs": self.jobs.get(int(args[2]), [])}), ""
        raise AssertionError(args)


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class Wait(unittest.TestCase):
    """--wait follows one push's runs to their end, reading which workflows the push starts from their own filters
    (D00 T04 §9): a red anywhere wins, an expected run never seen is unknown, a cancel is not green."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / ".github" / "workflows").mkdir(parents=True)
        for name in ("build.yml", "plan.yml"):
            (self.root / ".github" / "workflows" / name).write_text(
                (REPO / ".github" / "workflows" / name).read_text(encoding="utf-8"), encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def wait(self, changed, gh, timeout=3600):
        clock = Clock()
        lines = []
        git = lambda args: "\n".join(changed) + "\n"
        code = ci.wait(SHA_NEW, runner=gh, git=git, root=self.root, timeout=timeout, poll=60, clock=clock,
                       sleep=clock.sleep, out=lines.append, pid=1)
        return code, "\n".join(lines), json.loads((self.root / "build" / "ci-watch" / f"{SHA_NEW}.json").read_text(encoding="utf-8"))

    def test_a_stamp_expects_plan_alone(self):
        code, out, state = self.wait(["todo/00-workspace/TODO-04-build-performance.md", "docs/reviews/x.json"],
                                     WaitGh({"plan.yml": [[wrun(1, "plan-gates")]]}))
        self.assertEqual(code, 0, out)
        self.assertEqual(state["expected"], ["plan.yml"])
        self.assertEqual(state["exit"], 0)

    def test_code_alone_expects_build_alone(self):
        code, out, state = self.wait(["src/pdf/error.cpp"], WaitGh({"build.yml": [[wrun(1, "build")]]}))
        self.assertEqual(code, 0, out)
        self.assertEqual(state["expected"], ["build.yml"])

    def test_a_run_that_appears_late_is_followed(self):
        gh = WaitGh({"build.yml": [[], [], [], [wrun(1, "build", status="in_progress")], [wrun(1, "build")]]})
        code, out, _ = self.wait(["src/pdf/error.cpp"], gh)
        self.assertEqual(code, 0, out)
        self.assertEqual(gh.count["build.yml"], 5)

    def test_an_expected_run_never_seen_is_unknown(self):
        code, out, state = self.wait(["src/pdf/error.cpp"], WaitGh({}))
        self.assertEqual(code, 3, out)
        self.assertEqual(state["workflows"]["build.yml"]["state"], "unknown")
        self.assertIn("not seen in 10 minutes", out)

    def test_a_red_beside_a_green_is_red_and_named(self):
        gh = WaitGh({"build.yml": [[wrun(1, "build", conclusion="failure")]], "plan.yml": [[wrun(2, "plan-gates")]]},
                    jobs={1: [{"name": "Build x64", "status": "completed", "conclusion": "failure", "url": "u"}]})
        code, out, _ = self.wait(["src/pdf/error.cpp", "scripts/ci_status.py"], gh)
        self.assertEqual(code, 1, out)
        self.assertIn("failed job: Build x64 (failure)", out)

    def test_a_cancel_never_hides_a_red(self):
        gh = WaitGh({"build.yml": [[wrun(1, "build", conclusion="cancelled")]],
                     "plan.yml": [[wrun(2, "plan-gates", conclusion="failure")]]})
        code, out, _ = self.wait(["src/pdf/error.cpp", "scripts/ci_status.py"], gh)
        self.assertEqual(code, 1, out)

    def test_a_cancel_alone_is_not_green(self):
        code, out, _ = self.wait(["src/pdf/error.cpp"], WaitGh({"build.yml": [[wrun(1, "build", conclusion="cancelled")]]}))
        self.assertEqual(code, 5, out)

    def test_a_rerun_turning_green_is_green(self):
        gh = WaitGh({"build.yml": [[wrun(1, "build", status="in_progress")], [wrun(1, "build")]]})
        code, out, _ = self.wait(["src/pdf/error.cpp"], gh)
        self.assertEqual(code, 0, out)

    def test_a_run_past_the_timeout(self):
        code, out, _ = self.wait(["src/pdf/error.cpp"], WaitGh({"build.yml": [[wrun(1, "build", status="in_progress")]]}),
                                 timeout=300)
        self.assertEqual(code, 4, out)

    def test_the_push_range_decides(self):
        # A stamp whose push also brought code: the range, not the last commit, says build.yml runs.
        calls = []
        def git(args):
            calls.append(args)
            return "src/core/runtime.cpp\ntodo/x.md\n" if args[2] == "a" * 40 else "todo/x.md\n"
        clock = Clock()
        code = ci.wait(SHA_NEW, since="a" * 40, runner=WaitGh({"build.yml": [[wrun(1, "build")]], "plan.yml": [[wrun(2, "p")]]}),
                       git=git, root=self.root, clock=clock, sleep=clock.sleep, out=lambda line: None, pid=1)
        self.assertEqual(code, 0)
        state = json.loads((self.root / "build" / "ci-watch" / f"{SHA_NEW}.json").read_text(encoding="utf-8"))
        self.assertEqual(state["expected"], ["build.yml", "plan.yml"])


class Gate(unittest.TestCase):
    """--gate: a new row or a stamp waits for the watch of origin/main's head to end green (D00 T04 §9)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "build" / "ci-watch").mkdir(parents=True)

    def tearDown(self):
        self.tmp.cleanup()

    def gate(self, head, state):
        if state is not None:
            (self.root / "build" / "ci-watch" / f"{state['sha']}.json").write_text(json.dumps(state), encoding="utf-8")
        lines = []
        return ci.gate(git=lambda args: head + "\n", root=self.root, out=lines.append), "\n".join(lines)

    def test_a_delayed_red_holds_the_next_row_until_its_fix_is_green(self):
        red = {"sha": SHA_OLD, "pid": 1, "exit": 1, "workflows": {"build.yml": {"state": "red", "run": {"url": "u1"},
               "failed_jobs": [{"name": "Build x64", "url": "j"}]}}}
        code, out = self.gate(SHA_OLD, red)
        self.assertEqual(code, 1, out)
        self.assertIn("build.yml red u1", out)
        self.assertIn("failed job: Build x64", out)
        # The fix is pushed: no watch of the new head yet holds the gate shut.
        code, out = self.gate(SHA_NEW, red)
        self.assertEqual(code, 6, out)
        green = {"sha": SHA_NEW, "pid": 2, "exit": 0, "workflows": {"build.yml": {"state": "green", "run": {"url": "u2"}}}}
        code, out = self.gate(SHA_NEW, green)
        self.assertEqual(code, 0, out)

    def test_no_watch_and_a_dead_watch_hold_it_shut(self):
        code, out = self.gate(SHA_NEW, None)
        self.assertEqual(code, 6, out)
        dead = {"sha": SHA_NEW, "pid": 999_999_9, "exit": None, "workflows": {}}
        code, out = self.gate(SHA_NEW, dead)
        self.assertEqual(code, 6, out)
        self.assertIn("gone before it ended", out)

class Scheduled(unittest.TestCase):
    """--scheduled, the recto-ci-watch task: each red run is reported once, an outage only after an hour (D00 T04 §9)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def scheduled(self, fake, now=1000.0):
        lines = []
        code = ci.scheduled(runner=fake, root=self.root, now=lambda: now, out=lines.append)
        return code, "\n".join(lines)

    def test_a_red_run_is_reported_once_then_a_new_one_again(self):
        red = FakeGh({"build.yml": [run(5, conclusion="failure")], "plan.yml": [run(6)]},
                     jobs={5: [{"name": "Build x64", "status": "completed", "conclusion": "failure", "url": "j"}]})
        code, out = self.scheduled(red)
        self.assertEqual(code, 1, out)
        self.assertIn("main is red: build.yml", out)
        code, out = self.scheduled(red)
        self.assertEqual(code, 0, out)  # the same red run: no second email
        newer = FakeGh({"build.yml": [run(7, conclusion="failure"), run(5, conclusion="failure")], "plan.yml": [run(6)]})
        code, out = self.scheduled(newer)
        self.assertEqual(code, 1, out)
        green = FakeGh({"build.yml": [run(8), run(7, conclusion="failure")], "plan.yml": [run(6)]})
        code, out = self.scheduled(green)
        self.assertEqual(code, 0, out)

    def test_an_outage_alerts_once_after_an_hour(self):
        down = FakeGh(fail=(1, "", "HTTP 502"))
        self.assertEqual(self.scheduled(down, now=1000.0)[0], 0)
        self.assertEqual(self.scheduled(down, now=1000.0 + 1800)[0], 0)
        code, out = self.scheduled(down, now=1000.0 + 3700)
        self.assertEqual(code, 1, out)
        self.assertEqual(self.scheduled(down, now=1000.0 + 7400)[0], 0)

if __name__ == "__main__":
    unittest.main()
