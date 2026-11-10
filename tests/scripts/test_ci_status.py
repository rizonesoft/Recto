"""scripts/ci_status.py: the CI state of main read through a fake gh, and never green when gh cannot answer."""
import importlib.util
import io
import json
import os
from pathlib import Path
import re
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

    def __init__(self, runs=None, jobs=None, fail=None, missing=False, annotations=None):
        self.runs, self.jobs, self.fail, self.missing, self.calls = runs or {}, jobs or {}, fail, missing, []
        self.annotations = annotations or {}

    def __call__(self, args, timeout=None):
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
        if args[0] == "api":
            return 0, json.dumps(annotations_for(self.annotations, args[1])), ""
        raise AssertionError(args)


def annotations_for(annotations, path):
    assert path.startswith("repos/{owner}/{repo}/check-runs/") and path.endswith("/annotations"), path
    return [{"message": m} for m in annotations.get(int(path.split("/")[-2]), [])]


TIMED_OUT = "The job has exceeded the maximum execution time of 45m0s"


def cancelled_job(job_id, name="Build x64 & ARM64 and run recto-tests"):
    return {"databaseId": job_id, "name": name, "status": "completed", "conclusion": "cancelled", "url": f"j{job_id}"}


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

    def __init__(self, polls, jobs=None, annotations=None, pushes=((SHA_OLD, SHA_NEW),)):
        self.polls, self.jobs, self.count, self.annotations = polls, jobs or {}, {}, annotations or {}
        self.pushes = pushes  # GitHub's record of the branch's pushes, (before, after); None: it cannot be read

    def __call__(self, args, timeout=None):
        if args[:2] == ["run", "list"]:
            workflow = args[args.index("--workflow") + 1]
            assert args[args.index("--commit") + 1] == SHA_NEW
            answers = self.polls.get(workflow, [[]])
            i = self.count.get(workflow, 0)
            self.count[workflow] = i + 1
            return 0, json.dumps(answers[min(i, len(answers) - 1)]), ""
        if args[:2] == ["run", "view"]:
            return 0, json.dumps({"jobs": self.jobs.get(int(args[2]), [])}), ""
        if args[0] == "api" and "/activity?" in args[1]:
            assert args[1] == "repos/{owner}/{repo}/activity?ref=refs/heads/main&activity_type=push&per_page=100", args
            if self.pushes is None:
                return 1, "", "HTTP 502: Bad Gateway"
            return 0, json.dumps([{"before": b, "after": a, "activity_type": "push"} for b, a in self.pushes]), ""
        if args[0] == "api":
            return 0, json.dumps(annotations_for(self.annotations, args[1])), ""
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


class FailingFor:
    """gh that fails for one workflow (always, or for its first `polls` reads) and answers the others through `gh`."""

    def __init__(self, gh, workflow, polls=None):
        self.gh, self.workflow, self.polls, self.seen = gh, workflow, polls, 0

    def __call__(self, args, timeout=None):
        if args[:2] == ["run", "list"] and args[args.index("--workflow") + 1] == self.workflow:
            self.seen += 1
            if self.polls is None or self.seen <= self.polls:
                return 1, "", "HTTP 502: Bad Gateway"
        return self.gh(args)


class WaitAtOnce(unittest.TestCase):
    """What the completion review of D00 T04 §9 found: a red is reported the poll it is seen, one failed read never
    hides it, a restart keeps the push range, a live watch is never doubled, and a failed state write is not a red."""

    BOTH = ["src/pdf/error.cpp", "scripts/ci_status.py"]
    RED_JOB = {1: [{"name": "Build x64", "status": "completed", "conclusion": "failure", "url": "u"}]}

    setUp, tearDown = Wait.setUp, Wait.tearDown

    def run_wait(self, gh, changed=None, **kw):
        clock, lines = Clock(), []
        kw.setdefault("git", lambda args: "\n".join(changed or self.BOTH) + "\n")
        code = ci.wait(SHA_NEW, runner=gh, root=self.root, poll=60, clock=clock, sleep=clock.sleep, out=lines.append,
                       **{"pid": 1, **kw})
        return code, "\n".join(lines), clock.now

    def state(self):
        return json.loads((self.root / "build" / "ci-watch" / f"{SHA_NEW}.json").read_text(encoding="utf-8"))

    def test_a_red_is_reported_the_poll_it_is_seen_while_another_workflow_still_runs(self):
        gh = WaitGh({"build.yml": [[wrun(1, "build", conclusion="failure")]],
                     "plan.yml": [[wrun(2, "plan-gates", status="in_progress")]]}, jobs=self.RED_JOB)
        code, out, elapsed = self.run_wait(gh)
        self.assertEqual((code, elapsed), (1, 0.0), out)
        self.assertIn("failed job: Build x64 (failure)", out)
        self.assertIn("still running on this commit: plan.yml", out)
        self.assertIn("`gh run view 1 --log-failed`", out)
        state = self.state()
        self.assertEqual((state["exit"], state["workflows"]["plan.yml"]["state"]), (1, "pending"))

    def test_a_red_is_reported_before_any_other_workflow_is_read(self):
        asked = []

        class Recording(WaitGh):
            def __call__(self, args, timeout=None):
                if args[:2] == ["run", "list"]:
                    asked.append(args[args.index("--workflow") + 1])
                return super().__call__(args)
        gh = Recording({"build.yml": [[wrun(1, "build", conclusion="failure")]],
                        "plan.yml": [[wrun(2, "plan-gates", status="in_progress")]]}, jobs=self.RED_JOB)
        code, out, elapsed = self.run_wait(gh)
        self.assertEqual((code, elapsed, asked), (1, 0.0, ["build.yml"]), out)
        self.assertIn("still running on this commit: plan.yml", out)

    def test_a_failed_read_of_one_workflow_never_hides_anothers_red(self):
        gh = FailingFor(WaitGh({"build.yml": [[wrun(1, "build", conclusion="failure")]]}, jobs=self.RED_JOB), "plan.yml")
        code, out, elapsed = self.run_wait(gh)
        self.assertEqual((code, elapsed), (1, 0.0), out)
        self.assertEqual(self.state()["exit"], 1)

    def test_a_short_outage_is_waited_out_and_a_long_one_is_unknown(self):
        short = FailingFor(WaitGh({"build.yml": [[wrun(1, "build")]]}), "build.yml", polls=3)
        code, out, elapsed = self.run_wait(short, changed=["src/pdf/error.cpp"])
        self.assertEqual((code, elapsed), (0, 180.0), out)
        long = FailingFor(WaitGh({}), "build.yml")
        code, out, elapsed = self.run_wait(long, changed=["src/pdf/error.cpp"])
        self.assertEqual((code, elapsed), (3, 600.0), out)
        self.assertIn("gh could not read it for 10 minutes", out)
        self.assertIn("HTTP 502", self.state()["workflows"]["build.yml"]["note"])

    def test_a_green_that_cannot_be_read_again_is_not_the_verdict(self):
        class DownFrom:
            """build.yml answers its first read, then fails for `polls` reads (None: for good)."""

            def __init__(self, gh, polls):
                self.gh, self.polls, self.seen = gh, polls, 0

            def __call__(self, args, timeout=None):
                if args[:2] == ["run", "list"] and args[args.index("--workflow") + 1] == "build.yml":
                    self.seen += 1
                    if self.seen > 1 and (self.polls is None or self.seen <= 1 + self.polls):
                        return 1, "", "HTTP 503: Service Unavailable"
                return self.gh(args)
        # Build is green, plan still runs; the build is started again and its next read fails as plan turns green.
        polls = {"build.yml": [[wrun(1, "build")], [wrun(1, "build", conclusion="failure")]],
                 "plan.yml": [[wrun(2, "plan-gates", status="in_progress")], [wrun(2, "plan-gates")]]}
        code, out, elapsed = self.run_wait(DownFrom(WaitGh(polls, jobs=self.RED_JOB), polls=2))
        self.assertEqual((code, elapsed), (1, 180.0), out)  # read again once GitHub answers: the rerun is red
        self.assertIn("gh could not read it again", out)
        code, out, elapsed = self.run_wait(DownFrom(WaitGh(polls), polls=None))
        self.assertEqual((code, elapsed), (3, 660.0), out)  # never read again: unknown, not the old green
        self.assertEqual(self.state()["workflows"]["build.yml"]["state"], "unknown")

    def test_the_push_range_is_read_from_github_when_nobody_gave_it(self):
        # A code commit and a docs-only commit pushed together, and a session that died before it recorded a watch:
        # the watch started again with the head alone must still expect the build.
        asked = []

        def git(args):
            asked.append(args)
            return "src/core/runtime.cpp\ndocs/experiments/x.md\n" if args[2] == SHA_OLD else "docs/experiments/x.md\n"
        gh = WaitGh({"build.yml": [[], [], [wrun(1, "build", conclusion="failure")]]}, jobs=self.RED_JOB)
        code, out, elapsed = self.run_wait(gh, git=git)
        self.assertEqual((code, elapsed), (1, 120.0), out)
        self.assertEqual(asked, [["diff", "--name-only", SHA_OLD, SHA_NEW]])
        self.assertEqual((self.state()["since"], self.state()["expected"]), (SHA_OLD, ["build.yml"]))

    def test_an_unknown_push_range_proves_no_workflow_unnecessary(self):
        def git(args):
            return "docs/experiments/x.md\n"  # the head commit alone starts nothing
        for pushes in (None, ((SHA_OLD, "c" * 40),)):  # GitHub cannot be read; or it never saw this head pushed
            with self.subTest(pushes=pushes):
                # Nothing is registered yet: the watch waits, and the build that then appears red is reported.
                late = WaitGh({"build.yml": [[], [], [], [wrun(1, "build", conclusion="failure")]]}, jobs=self.RED_JOB,
                              pushes=pushes)
                code, out, elapsed = self.run_wait(late, git=git)
                self.assertEqual((code, elapsed), (1, 180.0), out)
                self.assertIn("the push that brought bbbbbbbbbbbb is not known", out)
                self.assertIn("build.yml: pending the push range is not known: waiting to see whether it starts", out)
                # A push that really starts nothing is green once every workflow had its ten minutes.
                code, out, elapsed = self.run_wait(WaitGh({}, pushes=pushes), git=git)
                self.assertEqual((code, elapsed), (0, 600.0), out)
                self.assertEqual(self.state()["workflows"], {})

    def test_progress_lines_are_flushed_as_they_are_printed(self):
        self.assertIs(ci.wait.__kwdefaults__["out"], ci.say)
        self.assertIs(ci.follow.__kwdefaults__["out"], ci.say)
        with unittest.mock.patch("builtins.print") as printed:
            ci.say("a line")
        printed.assert_called_once_with("a line", flush=True)

    def test_a_restarted_watch_keeps_the_first_ones_push_range(self):
        path = self.root / "build" / "ci-watch" / f"{SHA_NEW}.json"
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps({"sha": SHA_NEW, "since": SHA_OLD, "pid": 4242, "watches": 1, "exit": None}),
                        encoding="utf-8")
        asked = []

        def git(args):
            asked.append(args)
            return "src/core/runtime.cpp\ntodo/x.md\n" if args[2] == SHA_OLD else "todo/x.md\n"
        gh = WaitGh({"build.yml": [[wrun(1, "build")]], "plan.yml": [[wrun(2, "p")]]}, pushes=None)  # GitHub cannot say
        code, out, _ = self.run_wait(gh, git=git, alive=lambda pid: False)
        self.assertEqual(code, 0, out)
        self.assertEqual(asked, [["diff", "--name-only", SHA_OLD, SHA_NEW]])
        state = self.state()
        self.assertEqual((state["since"], state["expected"], state["watches"]), (SHA_OLD, ["build.yml", "plan.yml"], 2))

    def test_a_second_watch_is_never_started_beside_a_live_one(self):
        path = self.root / "build" / "ci-watch" / f"{SHA_NEW}.json"
        path.parent.mkdir(parents=True)
        before = json.dumps({"sha": SHA_NEW, "since": None, "pid": 4242, "watches": 1, "exit": None})
        path.write_text(before, encoding="utf-8")
        code, out, _ = self.run_wait(WaitGh({}), alive=lambda pid: pid == 4242)
        self.assertEqual(code, 6, out)
        self.assertIn("already watched by process 4242", out)
        self.assertEqual(path.read_text(encoding="utf-8"), before)

    def test_a_pid_that_now_belongs_to_another_process_is_not_the_watch(self):
        # The watch died without recording its end and its pid was given to another process, which is alive.
        path = self.root / "build" / "ci-watch" / f"{SHA_NEW}.json"
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps({"sha": SHA_NEW, "since": SHA_OLD, "pid": 4242, "born": 111, "watches": 1, "exit": None}),
                        encoding="utf-8")
        reused = {"alive": lambda pid: pid == 4242, "born": lambda pid: {4242: 999, 1: 5}.get(pid)}
        code, out, _ = self.run_wait(WaitGh({"build.yml": [[wrun(1, "build")]], "plan.yml": [[wrun(2, "p")]]}), **reused)
        self.assertEqual(code, 0, out)  # the replacement watch runs
        self.assertEqual((self.state()["pid"], self.state()["born"], self.state()["watches"]), (1, 5, 2))
        # The same pid with the same creation time is the watch itself, still running: no second one.
        path.write_text(json.dumps({"sha": SHA_NEW, "since": SHA_OLD, "pid": 4242, "born": 999, "watches": 1, "exit": None}),
                        encoding="utf-8")
        code, out, _ = self.run_wait(WaitGh({}), **reused)
        self.assertEqual(code, 6, out)

    def test_a_process_is_known_by_its_pid_and_when_it_was_created(self):
        mine = ci.process_born(os.getpid())
        self.assertIsInstance(mine, int)
        self.assertEqual(ci.process_born(os.getpid()), mine)
        self.assertIsNone(ci.process_born(999_999_9))
        self.assertTrue(ci.watch_alive({"pid": os.getpid(), "born": mine}))
        self.assertFalse(ci.watch_alive({"pid": os.getpid(), "born": mine - 10_000_000}))
        self.assertTrue(ci.watch_alive({"pid": os.getpid()}))  # a file written before the creation time was recorded
        self.assertFalse(ci.watch_alive({"pid": 999_999_9, "born": mine}))
        child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
        try:
            self.assertNotEqual(ci.process_born(child.pid), None)
            self.assertFalse(ci.watch_alive({"pid": child.pid, "born": mine}))
        finally:
            child.kill()
            child.wait(timeout=30)

    def test_two_watches_started_together_never_both_follow_one_commit(self):
        import threading
        lock = self.root / "build" / "ci-watch" / f"{SHA_NEW}.lock"
        # A lock file left by a watch that was killed is no obstacle and nobody deletes it: the claim is the lock the
        # operating system holds, so eight recoveries started together still yield exactly one watch.
        lock.parent.mkdir(parents=True)
        lock.write_text(json.dumps({"pid": 4242, "born": 111}), encoding="utf-8")
        barrier, results = threading.Barrier(8), []

        def start(n):
            barrier.wait()
            results.append((n, *ci.claim(lock, n, n * 10)))
        threads = [threading.Thread(target=start, args=(n,)) for n in range(1, 9)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=30)
        try:
            winners = [n for n, handle, holder in results if handle is not None]
            self.assertEqual(len(winners), 1, results)
            self.assertEqual(json.loads(lock.read_text(encoding="utf-8")), {"pid": winners[0], "born": winners[0] * 10})
            # While it is held, every later claim is refused too, and none of them changed the holder's record.
            handle, holder = ci.claim(lock, 99, 990)
            self.assertIsNone(handle)
            self.assertEqual(holder, {"pid": winners[0], "born": winners[0] * 10})
        finally:
            for _, handle, _ in results:
                if handle is not None:
                    handle.close()
        # Given back, it is there for the next watch.
        handle, holder = ci.claim(lock, 99, 990)
        self.assertIsNotNone(handle)
        handle.close()

    def test_a_claim_ends_with_the_process_that_held_it(self):
        lock = self.root / "build" / "ci-watch" / f"{SHA_NEW}.lock"
        code = ("import importlib.util, sys, time\n"
                f"spec = importlib.util.spec_from_file_location('ci_status', r'{SCRIPT}')\n"
                "ci = importlib.util.module_from_spec(spec); spec.loader.exec_module(ci)\n"
                f"handle, holder = ci.claim(r'{lock}', 7, 70)\n"
                "print('held' if handle else 'refused', flush=True)\n"
                "time.sleep(60)\n")
        child = subprocess.Popen([sys.executable, "-c", code], stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(child.stdout.readline().strip(), "held")
            handle, holder = ci.claim(lock, 99, 990)
            self.assertIsNone(handle)
            self.assertEqual(holder, {"pid": 7, "born": 70})
        finally:
            child.kill()  # the watch is killed without any chance to clean up
            child.wait(timeout=30)
            child.stdout.close()
        handle, holder = ci.claim(lock, 99, 990)
        self.assertIsNotNone(handle, holder)
        handle.close()

    def test_a_claim_that_cannot_be_written_is_unknown_never_a_red_and_is_given_back(self):
        lock = self.root / "build" / "ci-watch" / f"{SHA_NEW}.lock"
        real_open = open

        class FullDisk:
            """The lock file opens and locks, and the volume has no room for its few bytes."""

            def __init__(self, handle):
                self.handle = handle

            def flush(self):
                raise OSError(28, "No space left on device")

            def __getattr__(self, name):
                return getattr(self.handle, name)

        def opening(path, mode="r", *args, **kwargs):
            handle = real_open(path, mode, *args, **kwargs)
            return FullDisk(handle) if str(path).endswith(".lock") else handle
        with unittest.mock.patch.object(ci, "open", opening, create=True):
            with self.assertRaises(ci.Unavailable) as caught:
                ci.claim(lock, 1, 5)
            self.assertIn("No space left on device", str(caught.exception))
            out = io.StringIO()
            with redirect_stdout(out):
                code = ci.main(["--wait", SHA_NEW], runner=WaitGh({}), root=self.root,
                               git=lambda args: SHA_NEW + "\n" if args[0] == "rev-parse" else "x\n")
        self.assertEqual(code, 3, out.getvalue())
        self.assertIn("ci-status unknown: cannot write", out.getvalue())
        handle, holder = ci.claim(lock, 2, 6)  # not left held by the watch that failed
        self.assertIsNotNone(handle, holder)
        handle.close()

    def test_no_file_error_anywhere_leaves_as_exit_1(self):
        for argv, target in ((["--gate"], "gate"), (["--scheduled"], "scheduled"), (["--wait", SHA_NEW], "wait"), ([], "report")):
            with self.subTest(argv=argv):
                out = io.StringIO()
                with unittest.mock.patch.object(ci, target, side_effect=PermissionError(13, "Access is denied")), \
                        redirect_stdout(out):
                    code = ci.main(argv, runner=WaitGh({}), root=REPO, git=lambda args: SHA_NEW + "\n")
                self.assertEqual(code, 3, out.getvalue())
                self.assertIn("ci-status unknown: PermissionError", out.getvalue())
                self.assertIn("nothing here says CI is red", out.getvalue())
                # A defect in the script itself (Python would exit 1 on it) is unknown too, and bad usage stays 2.
                out = io.StringIO()
                with unittest.mock.patch.object(ci, target, side_effect=KeyError("workflows")), redirect_stdout(out):
                    code = ci.main(argv, runner=WaitGh({}), root=REPO, git=lambda args: SHA_NEW + "\n")
                self.assertEqual(code, 3, out.getvalue())
                self.assertIn("this script failed (KeyError", out.getvalue())
        with self.assertRaises(SystemExit) as usage, redirect_stdout(io.StringIO()), unittest.mock.patch("sys.stderr", io.StringIO()):
            ci.main(["--wait", "not a sha"])
        self.assertEqual(usage.exception.code, 2)

    def test_a_watch_holds_its_claim_while_it_runs_and_gives_it_back_when_it_ends(self):
        lock = self.root / "build" / "ci-watch" / f"{SHA_NEW}.lock"
        seen, second, lines, root = [], [], [], self.root

        class Starting(WaitGh):
            """On its first call (the watch is already following), it starts a second watch of the same commit."""

            def __call__(self, args, timeout=None):
                if not second:
                    seen.append(json.loads(lock.read_text(encoding="utf-8")))
                    inner = Clock()  # its own clock, so a watch that is wrongly let through ends at once here
                    second.append(ci.wait(SHA_NEW, runner=WaitGh({}), git=lambda a: "x\n", root=root, out=lines.append,
                                          pid=2, born=lambda pid: 5, clock=inner, sleep=inner.sleep))
                return super().__call__(args)
        code, out, _ = self.run_wait(Starting({"build.yml": [[wrun(1, "build")]], "plan.yml": [[wrun(2, "p")]]}),
                                     born=lambda pid: 5)
        self.assertEqual((code, second), (0, [6]), out)
        self.assertIn("already watched by process 1", "\n".join(lines))
        self.assertEqual(seen, [{"pid": 1, "born": 5}])

        def free():
            handle, _ = ci.claim(lock, 99, 990)
            if handle is not None:
                handle.close()
            return handle is not None
        self.assertTrue(free())
        # It is given back when the watch ends in an error too.
        with unittest.mock.patch.object(ci, "write_state", side_effect=ci.Unavailable("cannot write x")):
            with self.assertRaises(ci.Unavailable):
                self.run_wait(WaitGh({}))
        self.assertTrue(free())

    def test_progress_is_printed_as_it_happens(self):
        gh = WaitGh({"build.yml": [[], [], [wrun(1, "build", status="in_progress")], [wrun(1, "build")]]})
        code, out, _ = self.run_wait(gh, changed=["src/pdf/error.cpp"])
        self.assertEqual(code, 0, out)
        lines = out.splitlines()
        self.assertEqual(lines[0], "ci watch: bbbbbbbbbbbb expects build.yml (1 changed file(s)); reading every 60 s "
                                   "for up to 60 min")
        self.assertEqual(lines[1], "[    0 s] build.yml: pending waiting for GitHub to register the run")
        self.assertEqual(lines[2], "[  120 s] build.yml: pending in_progress https://github.com/o/r/actions/runs/1")
        self.assertEqual(lines[3], "[  180 s] build.yml: green completed https://github.com/o/r/actions/runs/1")
        self.assertEqual(lines[-1], "ci watch: exit 0 for bbbbbbbbbbbb. next: nothing, CI is green for this push")

    def test_a_state_file_that_cannot_be_written_is_unknown_and_never_a_red(self):
        gh = WaitGh({"build.yml": [[wrun(1, "build")]]})
        def git(args):
            return SHA_NEW + "\n" if args[0] == "rev-parse" else "src/pdf/error.cpp\n"
        failures = {"a full disk": unittest.mock.patch.object(Path, "write_text", side_effect=OSError(28, "No space left on device")),
                    "a locked file": unittest.mock.patch.object(ci.os, "replace", side_effect=PermissionError(13, "locked")),
                    "a refused folder": unittest.mock.patch.object(Path, "mkdir", side_effect=PermissionError(13, "denied"))}
        for name, failure in failures.items():
            with self.subTest(name=name):
                out = io.StringIO()
                with failure, redirect_stdout(out):
                    code = ci.main(["--wait", SHA_NEW], runner=gh, root=self.root, git=git)
                self.assertEqual(code, 3, out.getvalue())
                self.assertIn("ci-status unknown: cannot write", out.getvalue())
                self.assertRegex(out.getvalue(), rf"ci-watch.{SHA_NEW}\.(json|lock)")  # the file it could not write
                self.assertIn("run the same command again", out.getvalue())
                self.assertEqual(list((self.root / "build").rglob("*.tmp")), [])

    def test_a_workflow_the_push_was_not_expected_to_start_is_read_before_the_push_is_called_green(self):
        # A watch restarted without its push range expects plan.yml alone; build.yml cannot be read at first, and it
        # has a red run on this commit. The green plan must not end the watch green.
        red_build = WaitGh({"plan.yml": [[wrun(2, "plan-gates")]], "build.yml": [[wrun(1, "build", conclusion="failure")]]},
                           jobs=self.RED_JOB)
        code, out, elapsed = self.run_wait(FailingFor(red_build, "build.yml", polls=2), changed=["todo/x.md"])
        self.assertEqual((code, elapsed), (1, 120.0), out)
        self.assertEqual(self.state()["expected"], ["plan.yml"])
        # Read and found to have no run on this commit, it holds nothing; never readable, it is unknown, not green.
        none = WaitGh({"plan.yml": [[wrun(2, "plan-gates")]]})
        code, out, elapsed = self.run_wait(FailingFor(none, "build.yml", polls=2), changed=["todo/x.md"])
        self.assertEqual((code, elapsed), (0, 120.0), out)
        code, out, elapsed = self.run_wait(FailingFor(none, "build.yml"), changed=["todo/x.md"])
        self.assertEqual((code, elapsed), (3, 600.0), out)

    def test_an_abbreviated_sha_is_watched_under_its_full_id(self):
        asked = []

        def git(args):
            asked.append(args)
            if args[0] == "rev-parse":
                return (SHA_NEW if args[2].startswith("bbbbbbb") else SHA_OLD) + "\n"
            return "src/pdf/error.cpp\n"
        out = io.StringIO()
        with redirect_stdout(out):
            code = ci.main(["--wait", "bbbbbbb", "--since", "aaaaaaa"], runner=WaitGh({"build.yml": [[wrun(1, "build")]]}),
                           root=self.root, git=git)
        self.assertEqual(code, 0, out.getvalue())
        self.assertEqual(asked[:2], [["rev-parse", "--verify", "bbbbbbb^{commit}"], ["rev-parse", "--verify", "aaaaaaa^{commit}"]])
        self.assertEqual(asked[2], ["diff", "--name-only", SHA_OLD, SHA_NEW])
        self.assertEqual((self.state()["sha"], self.state()["since"]), (SHA_NEW, SHA_OLD))
        self.assertEqual(sorted(p.name for p in (self.root / "build" / "ci-watch").iterdir()),
                         [f"{SHA_NEW}.json", f"{SHA_NEW}.lock"])
        with redirect_stdout(io.StringIO()) as refused:
            code = ci.main(["--wait", "ccccccc"], runner=WaitGh({}), root=self.root, git=lambda args: "not a commit\n")
        self.assertEqual(code, 3)
        self.assertIn("ccccccc does not name a commit in this clone", refused.getvalue())

    def test_a_failed_write_keeps_the_old_file_and_a_verdict_already_known(self):
        target = self.root / "build" / "x.json"
        ci.write_state(target, {"kept": True})
        with unittest.mock.patch.object(ci.os, "replace", side_effect=PermissionError(13, "locked")):
            with self.assertRaises(ci.Unavailable):
                ci.write_state(target, {"kept": False})
        self.assertEqual(json.loads(target.read_text(encoding="utf-8")), {"kept": True})
        self.assertEqual(list(target.parent.glob("*.tmp")), [])
        # The watch's first write succeeds and its last one fails: the red it saw is still its answer.
        real, calls = ci.os.replace, []

        def replace(source, destination):
            calls.append(destination)
            if len(calls) > 1:
                raise PermissionError(13, "locked")
            return real(source, destination)
        gh = WaitGh({"build.yml": [[wrun(1, "build", conclusion="failure")]]}, jobs=self.RED_JOB)
        with unittest.mock.patch.object(ci.os, "replace", side_effect=replace):
            code, out, _ = self.run_wait(gh, changed=["src/pdf/error.cpp"])
        self.assertEqual(code, 1, out)
        self.assertIn("cannot write", out)
        self.assertIn("`gh run view 1 --log-failed`", out)
        # The same failure on a push that ended green is unknown, never a green that nobody can read back: the file
        # still says the watch has not ended.
        calls.clear()
        with unittest.mock.patch.object(ci.os, "replace", side_effect=replace):
            code, out, _ = self.run_wait(WaitGh({"build.yml": [[wrun(1, "build")]]}), changed=["src/pdf/error.cpp"])
        self.assertEqual(code, 3, out)
        self.assertNotIn("next: nothing", out)
        self.assertIn("next: the watch's state could not be saved", out)
        self.assertIsNone(self.state()["exit"])

    def test_a_git_that_cannot_run_or_does_not_answer_is_unknown_and_never_a_red(self):
        failures = [subprocess.TimeoutExpired(["git", "ls-remote"], 60), FileNotFoundError("git")]
        for failure in failures:
            with self.subTest(failure=type(failure).__name__):
                with unittest.mock.patch.object(ci.subprocess, "run", side_effect=failure):
                    with self.assertRaises(ci.Unavailable) as caught:
                        ci.run_git(["ls-remote", "origin", "refs/heads/main"])
                    self.assertIn("git ls-remote origin could not run", str(caught.exception))
                    out = io.StringIO()
                    with redirect_stdout(out):
                        code = ci.main(["--gate"], runner=GateGh(), root=self.root)
                self.assertEqual(code, 3, out.getvalue())
                self.assertIn("ci gate: unknown: git rev-parse origin/main could not run", out.getvalue())


class GateGh:
    """gh for --gate: the runs GitHub lists for the head commit now, per workflow."""

    def __init__(self, runs=None, jobs=None, fail=False):
        self.runs, self.jobs, self.fail, self.calls = runs or {}, jobs or {}, fail, []

    def __call__(self, args, timeout=None):
        self.calls.append(args)
        if self.fail:
            return 1, "", "HTTP 502: Bad Gateway"
        if args[:2] == ["run", "list"]:
            return 0, json.dumps(self.runs.get(args[args.index("--workflow") + 1], [])), ""
        if args[:2] == ["run", "view"]:
            return 0, json.dumps({"jobs": self.jobs.get(int(args[2]), [])}), ""
        raise AssertionError(args)


class Gate(unittest.TestCase):
    """--gate: a new row or a stamp waits for the watch of main's head to end green, and for that green to still be
    true on GitHub (D00 T04 §9)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "build" / "ci-watch").mkdir(parents=True)

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, head, remote=None):
        def answer(args):
            if args[0] == "ls-remote":
                self.assertEqual(args, ["ls-remote", "origin", "refs/heads/main"])
                return f"{remote or head}\trefs/heads/main\n"
            return head + "\n"
        return answer

    def gate(self, head, state, gh=None, remote=None, alive=lambda pid: False, born=lambda pid: None):
        if state is not None:
            (self.root / "build" / "ci-watch" / f"{state['sha']}.json").write_text(json.dumps(state), encoding="utf-8")
        lines = []
        gh = gh or GateGh({"build.yml": [wrun(2, "build", sha=head)]})
        code = ci.gate(git=self.git(head, remote), runner=gh, root=self.root, out=lines.append, alive=alive, born=born)
        return code, "\n".join(lines)

    GREEN = {"sha": SHA_NEW, "pid": 2, "exit": 0, "workflows": {"build.yml": {"state": "green", "run": {"url": "u2"}}}}

    def test_a_delayed_red_holds_the_next_row_until_its_fix_is_green(self):
        red = {"sha": SHA_OLD, "pid": 1, "exit": 1, "workflows": {"build.yml": {"state": "red", "run": {"url": "u1", "databaseId": 7},
               "failed_jobs": [{"name": "Build x64", "url": "j"}]}}}
        code, out = self.gate(SHA_OLD, red)
        self.assertEqual(code, 1, out)
        self.assertIn("build.yml red u1 (failed jobs: Build x64)", out)
        self.assertIn("failed job: Build x64", out)
        self.assertIn("next: red comes first, before any new row or stamp: read `gh run view 7 --log-failed`", out)
        # The fix is pushed: no watch of the new head yet holds the gate shut.
        code, out = self.gate(SHA_NEW, red)
        self.assertEqual(code, 6, out)
        self.assertIn(f"next: start `python scripts/ci_status.py --wait {SHA_NEW}`", out)
        code, out = self.gate(SHA_NEW, self.GREEN)
        self.assertEqual(code, 0, out)

    def test_no_watch_a_dead_watch_and_a_live_watch_hold_it_shut_and_say_what_to_do(self):
        code, out = self.gate(SHA_NEW, None)
        self.assertEqual(code, 6, out)
        dead = {"sha": SHA_NEW, "pid": 999_999_9, "exit": None, "workflows": {}}
        code, out = self.gate(SHA_NEW, dead)
        self.assertEqual(code, 6, out)
        self.assertIn("gone before it ended", out)
        self.assertIn(f"next: start `python scripts/ci_status.py --wait {SHA_NEW}`", out)
        code, out = self.gate(SHA_NEW, dead, alive=lambda pid: True)
        self.assertEqual(code, 6, out)
        self.assertIn("still running (process 9999999). next: wait for its notification", out)
        self.assertNotIn("--wait", out)
        # Its pid is alive but belongs to another process now: the watch is gone, and the gate says to start it again.
        reused = {"sha": SHA_NEW, "pid": 4242, "born": 111, "exit": None, "workflows": {}}
        code, out = self.gate(SHA_NEW, reused, alive=lambda pid: True, born=lambda pid: 999)
        self.assertEqual(code, 6, out)
        self.assertIn("gone before it ended", out)
        self.assertIn(f"next: start `python scripts/ci_status.py --wait {SHA_NEW}`", out)

    def test_a_head_this_clone_has_not_fetched_is_not_judged_by_an_old_green(self):
        code, out = self.gate(SHA_NEW, self.GREEN, remote="c" * 40)
        self.assertEqual(code, 6, out)
        self.assertIn("main moved on GitHub to cccccccccccc", out)
        self.assertIn(f"`git pull --ff-only`, then `python scripts/ci_status.py --wait {'c' * 40} --since {SHA_NEW}`", out)

    def test_a_recorded_green_is_refused_once_the_commit_runs_again(self):
        rerun = GateGh({"build.yml": [wrun(2, "build", status="in_progress")]})
        code, out = self.gate(SHA_NEW, self.GREEN, gh=rerun)
        self.assertEqual(code, 6, out)
        self.assertIn("the recorded green of bbbbbbbbbbbb is stale: build.yml is running again", out)
        failed = GateGh({"build.yml": [wrun(2, "build", conclusion="failure")]},
                        jobs={2: [{"name": "Build x64", "status": "completed", "conclusion": "failure", "url": "u"}]})
        code, out = self.gate(SHA_NEW, self.GREEN, gh=failed)
        self.assertEqual(code, 1, out)
        self.assertIn("ci gate: red at bbbbbbbbbbbb after a recorded green: build.yml is red", out)
        self.assertIn("failed jobs: Build x64", out)
        # GitHub not answering is unknown (3), never the old green.
        with self.assertRaises(ci.Unavailable):
            self.gate(SHA_NEW, self.GREEN, gh=GateGh(fail=True))
        out = io.StringIO()
        with redirect_stdout(out):
            code = ci.main(["--gate"], runner=GateGh(fail=True), root=self.root, git=self.git(SHA_NEW))
        self.assertEqual(code, 3, out.getvalue())
        self.assertIn("ci gate: unknown:", out.getvalue())

    def test_a_workflow_the_watch_never_recorded_is_still_read_on_the_head(self):
        workflows = self.root / ".github" / "workflows"
        workflows.mkdir(parents=True)
        for name in ("build.yml", "plan.yml"):
            (workflows / name).write_text("on:\n  push:\n", encoding="utf-8")
        plan_only = {"sha": SHA_NEW, "pid": 2, "exit": 0, "workflows": {"plan.yml": {"state": "green", "run": {"url": "p"}}}}
        green_plan = [wrun(3, "plan-gates")]
        red = GateGh({"plan.yml": green_plan, "build.yml": [wrun(2, "build", conclusion="failure")]},
                     jobs={2: [{"name": "Build x64", "status": "completed", "conclusion": "failure", "url": "u"}]})
        code, out = self.gate(SHA_NEW, plan_only, gh=red)
        self.assertEqual(code, 1, out)
        self.assertIn("after a recorded green: build.yml is red", out)
        running = GateGh({"plan.yml": green_plan, "build.yml": [wrun(2, "build", status="in_progress")]})
        self.assertEqual(self.gate(SHA_NEW, plan_only, gh=running)[0], 6)
        # A workflow this push did not start, and the watch did not expect, holds nothing.
        code, out = self.gate(SHA_NEW, plan_only, gh=GateGh({"plan.yml": green_plan}))
        self.assertEqual(code, 0, out)

    def test_a_red_is_never_hidden_behind_a_workflow_still_running_or_unreadable(self):
        workflows = self.root / ".github" / "workflows"
        workflows.mkdir(parents=True)
        for name in ("build.yml", "plan.yml"):
            (workflows / name).write_text("on:\n  push:\n", encoding="utf-8")
        both = {"sha": SHA_NEW, "pid": 2, "exit": 0, "workflows": {
            "build.yml": {"state": "green", "run": {"url": "b"}}, "plan.yml": {"state": "green", "run": {"url": "p"}}}}
        jobs = {3: [{"name": "validate", "status": "completed", "conclusion": "failure", "url": "u"}]}
        # Both were started again: the build still runs (it is read first), the plan has failed.
        gh = GateGh({"build.yml": [wrun(2, "build", status="in_progress")],
                     "plan.yml": [wrun(3, "plan-gates", conclusion="failure")]}, jobs=jobs)
        code, out = self.gate(SHA_NEW, both, gh=gh)
        self.assertEqual(code, 1, out)
        self.assertIn("plan.yml is red", out)
        self.assertIn("failed jobs: validate", out)
        self.assertIn("build.yml is running again", out)

        class BuildDown(GateGh):
            def __call__(self, args, timeout=None):
                if args[:2] == ["run", "list"] and args[args.index("--workflow") + 1] == "build.yml":
                    return 1, "", "HTTP 502: Bad Gateway"
                return super().__call__(args)
        code, out = self.gate(SHA_NEW, both, gh=BuildDown({"plan.yml": [wrun(3, "plan-gates", conclusion="failure")]}, jobs=jobs))
        self.assertEqual(code, 1, out)
        # With no red, a workflow that cannot be read is unknown, never the recorded green.
        with self.assertRaises(ci.Unavailable):
            self.gate(SHA_NEW, both, gh=BuildDown({"plan.yml": [wrun(3, "plan-gates")]}))

    def test_each_other_end_names_its_own_recovery(self):
        def ended(code, state, watches=1, run_id=7):
            return {"sha": SHA_NEW, "pid": 1, "exit": code, "watches": watches, "workflows": {
                "build.yml": {"state": state, "run": {"url": "u", "databaseId": run_id}, "failed_jobs": []}}}
        again = f"`python scripts/ci_status.py --wait {SHA_NEW}`"
        cases = [(ended(5, "cancelled"), f"ci gate: cancelled at bbbbbbbbbbbb: build.yml cancelled u. next: a cancelled "
                                         f"run is not green: `gh run rerun 7`, then {again}"),
                 (ended(3, "unknown"), f"next: unknown is not green: start a second watch, {again}"),
                 (ended(4, "pending"), f"next: still running at the watch's time limit is not green: start a second "
                                       f"watch, {again}"),
                 (ended(3, "unknown", watches=2), "next: the second watch also ended unknown: record a Critical event"),
                 (ended(4, "pending", watches=2), "next: the second watch also ended still running at the watch's time "
                                                  "limit: record a Critical event")]
        for state, expected in cases:
            with self.subTest(expected=expected):
                code, out = self.gate(SHA_NEW, state)
                self.assertEqual(code, 6, out)
                self.assertIn(expected, out)

    def test_the_session_start_line_names_the_failed_jobs_on_one_line(self):
        red = {"sha": SHA_NEW, "pid": 1, "exit": 1, "workflows": {
            "build.yml": {"state": "red", "run": {"url": "u1", "databaseId": 7},
                          "failed_jobs": [{"name": "Build x64", "url": "j"}, {"name": "Tests", "url": "k"}]}}}
        (self.root / "build" / "ci-watch" / f"{SHA_NEW}.json").write_text(json.dumps(red), encoding="utf-8")
        out = io.StringIO()
        with redirect_stdout(out):
            code = ci.main(["--gate", "--brief"], runner=GateGh(), root=self.root, git=self.git(SHA_NEW))
        self.assertEqual(code, 0)
        lines = out.getvalue().splitlines()
        self.assertEqual(len(lines), 1, lines)
        self.assertIn("ci gate: red at bbbbbbbbbbbb: build.yml red u1 (failed jobs: Build x64, Tests)", lines[0])


class TimedOut(unittest.TestCase):
    """GitHub reports a job stopped at its timeout-minutes as cancelled: that run is red, never skipped or rerun
    (main timed out twice on 2026-11-09 while this report read green from the run before)."""

    def test_the_report_reads_a_timed_out_run_red(self):
        fake = FakeGh({"build.yml": [run(9, conclusion="cancelled"), run(8, sha=SHA_OLD)], "plan.yml": [run(7)]},
                      jobs={9: [cancelled_job(91)]}, annotations={91: [TIMED_OUT, "The operation was canceled."]})
        code, report = status(fake)
        self.assertEqual(code, 1)
        build = report["workflows"][0]
        self.assertEqual(build["verdict"], "red")
        self.assertEqual(build["verdict_run"]["databaseId"], 9)
        self.assertIn("timed out", build["failed_jobs"][0]["conclusion"])

    def test_a_plain_cancel_is_still_passed_over(self):
        fake = FakeGh({"build.yml": [run(9, conclusion="cancelled"), run(8, sha=SHA_OLD)], "plan.yml": [run(7)]},
                      jobs={9: [cancelled_job(91)]}, annotations={91: ["The operation was canceled."]})
        code, report = status(fake)
        self.assertEqual(code, 0)
        self.assertEqual(report["workflows"][0]["verdict_run"]["databaseId"], 8)

    def test_wait_reads_a_timed_out_run_red(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / ".github" / "workflows").mkdir(parents=True)
            (root / ".github" / "workflows" / "build.yml").write_text(
                (REPO / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8"), encoding="utf-8")
            gh = WaitGh({"build.yml": [[wrun(9, "build", conclusion="cancelled")]]}, jobs={9: [cancelled_job(91)]},
                        annotations={91: [TIMED_OUT]})
            clock, lines = Clock(), []
            code = ci.wait(SHA_NEW, runner=gh, git=lambda args: "src/pdf/error.cpp\n", root=root, workflows=["build.yml"],
                           clock=clock, sleep=clock.sleep, out=lines.append, pid=1)
            self.assertEqual(code, 1, lines)
            self.assertIn("timed out", "\n".join(lines))


RED_JOBS = {n: [{"name": f"Build x64 #{n}", "status": "completed", "conclusion": "failure", "url": "j"}] for n in range(1, 10)}


class Scheduled(unittest.TestCase):
    """--scheduled, the recto-ci-watch task: every red run is reported until its email is sent and never after, an
    outage only after an hour (D00 T04 §9)."""

    NOW = 1_794_300_000.0  # 2026-11-10T09:20:00Z

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def scheduled(self, fake, now=None):
        lines = []
        code = ci.scheduled(runner=fake, root=self.root, now=lambda: self.NOW if now is None else now, out=lines.append)
        return code, "\n".join(lines)

    def runner_says(self, alert, at=None, exit_code=1):
        """What .conclave/automations/run.py leaves after the run that raised at `at`: whether its email was sent."""
        def stamp(seconds):
            return ci.time.strftime("%Y-%m-%dT%H:%M:%SZ", ci.time.gmtime(seconds))
        at = self.NOW if at is None else at
        folder = self.root / "build" / "automations" / "recto-ci-watch"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "last.json").write_text(json.dumps({"name": "recto-ci-watch", "started": stamp(at - 2), "finished": stamp(at + 3),
                                                      "exit": exit_code, "alert": alert}), encoding="utf-8")

    def baseline(self):
        """The task has run before, on a green main, so later reds are news."""
        self.assertEqual(self.scheduled(FakeGh({"build.yml": [run(1)], "plan.yml": [run(2)]}))[0], 0)

    def test_a_red_run_is_reported_until_its_email_is_sent_and_never_after(self):
        self.baseline()
        red = FakeGh({"build.yml": [run(5, conclusion="failure"), run(1)], "plan.yml": [run(2)]}, jobs=RED_JOBS)
        code, out = self.scheduled(red)
        self.assertEqual(code, 1, out)
        self.assertIn("red run on main: build.yml at bbbbbbbbbbbb https://github.com/o/r/actions/runs/5; failed jobs: "
                      "Build x64 #5", out)
        # No word from the runner, then an email that could not be sent: the run is raised again each time.
        self.assertEqual(self.scheduled(red)[0], 1)
        self.runner_says("not sent")  # the runner keeps the task's own exit (1) beside it
        self.assertEqual(self.scheduled(red)[0], 1)
        # A sent email from another moment is not this run's.
        self.runner_says("sent", at=self.NOW - 900)
        self.assertEqual(self.scheduled(red)[0], 1)
        self.runner_says("sent")
        code, out = self.scheduled(red)
        self.assertEqual(code, 0, out)
        self.assertNotIn("red run on main", out)
        self.assertEqual(self.scheduled(red)[0], 0)
        newer = FakeGh({"build.yml": [run(7, conclusion="failure"), run(5, conclusion="failure"), run(1)],
                        "plan.yml": [run(2)]}, jobs=RED_JOBS)
        code, out = self.scheduled(newer)
        self.assertEqual(code, 1, out)
        self.assertIn("runs/7", out)
        self.assertNotIn("runs/5;", out)

    def test_two_reds_between_two_polls_are_both_reported_and_a_later_green_hides_neither(self):
        self.baseline()
        gh = FakeGh({"build.yml": [run(9), run(8, conclusion="failure"), run(7, conclusion="failure"), run(1)],
                     "plan.yml": [run(6, conclusion="failure"), run(2)]}, jobs=RED_JOBS)
        code, out = self.scheduled(gh)
        self.assertEqual(code, 1, out)
        for n in (6, 7, 8):
            self.assertIn(f"runs/{n}; failed jobs: Build x64 #{n}", out)
        self.assertIn("main has 3 red run(s) not yet reported by email: build.yml, plan.yml", out)

    def test_one_workflow_that_cannot_be_read_never_drops_anothers_red(self):
        self.baseline()

        class PlanDown(FakeGh):
            def __call__(self, args, timeout=None):
                if args[:2] == ["run", "list"] and args[args.index("--workflow") + 1] == "plan.yml":
                    return 1, "", "HTTP 502: Bad Gateway"
                return super().__call__(args)
        gh = PlanDown({"build.yml": [run(5, conclusion="failure"), run(1)]}, jobs=RED_JOBS)
        code, out = self.scheduled(gh)
        self.assertEqual(code, 1, out)
        self.assertIn("runs/5; failed jobs: Build x64 #5", out)
        self.assertIn("ci-status unknown for 0 minutes: plan.yml: gh run list exited 1: HTTP 502: Bad Gateway", out)
        self.assertEqual(self.scheduled(gh)[0], 1)  # still not acknowledged, so raised again
        self.runner_says("sent")
        self.assertEqual(self.scheduled(gh)[0], 0)

    def test_a_rerun_that_fails_is_a_new_red_though_its_run_id_was_seen(self):
        self.baseline()
        cancelled = dict(run(9, conclusion="cancelled"), attempt=1)
        gh = FakeGh({"build.yml": [cancelled, run(1)], "plan.yml": [run(2)]}, jobs={9: [cancelled_job(91)]},
                    annotations={91: ["The operation was canceled."]})
        self.assertEqual(self.scheduled(gh)[0], 0)
        rerun = dict(run(9, conclusion="failure"), attempt=2)
        gh = FakeGh({"build.yml": [rerun, run(1)], "plan.yml": [run(2)]}, jobs=RED_JOBS)
        code, out = self.scheduled(gh)
        self.assertEqual(code, 1, out)
        self.assertIn("runs/9; failed jobs: Build x64 #9", out)

    def test_the_first_run_reports_the_standing_red_and_not_history(self):
        old = FakeGh({"build.yml": [run(5), run(4, conclusion="failure")], "plan.yml": [run(2)]}, jobs=RED_JOBS)
        self.assertEqual(self.scheduled(old)[0], 0)
        self.tearDown()
        self.setUp()
        standing = FakeGh({"build.yml": [run(5, conclusion="failure"), run(4, conclusion="failure")], "plan.yml": [run(2)]},
                          jobs=RED_JOBS)
        code, out = self.scheduled(standing)
        self.assertEqual(code, 1, out)
        self.assertIn("runs/5;", out)
        self.assertNotIn("runs/4;", out)

    def test_a_newest_run_whose_verdict_cannot_be_read_never_drops_an_older_red(self):
        self.baseline()

        class ViewDown(FakeGh):
            def __call__(self, args, timeout=None):
                if args[:3] == ["run", "view", "9"]:
                    return 1, "", "HTTP 502: Bad Gateway"
                return super().__call__(args)
        runs = {"build.yml": [run(10, conclusion="cancelled"), run(9, conclusion="cancelled"), run(7, conclusion="failure"),
                              run(1)], "plan.yml": [run(2)]}
        code, out = self.scheduled(ViewDown(runs, jobs={**RED_JOBS, 10: [cancelled_job(101)]}, annotations={101: [TIMED_OUT]}))
        self.assertEqual(code, 1, out)
        self.assertIn("runs/7; failed jobs: Build x64 #7", out)
        self.assertIn("runs/10;", out)  # the cancelled run after the unreadable one is still judged: it timed out
        self.assertIn("build.yml run 9: gh run view exited 1: HTTP 502", out)

    def test_a_first_run_that_could_not_read_everything_still_skips_history_on_the_next(self):
        class PlanDown(FakeGh):
            def __call__(self, args, timeout=None):
                if args[:2] == ["run", "list"] and args[args.index("--workflow") + 1] == "plan.yml":
                    return 1, "", "HTTP 502: Bad Gateway"
                return super().__call__(args)
        runs = {"build.yml": [run(5), run(4, conclusion="failure")], "plan.yml": [run(2)]}
        self.assertEqual(self.scheduled(PlanDown(runs, jobs=RED_JOBS))[0], 0)
        code, out = self.scheduled(FakeGh(runs, jobs=RED_JOBS))
        self.assertEqual(code, 0, out)  # run 4 failed before this task ever read main whole: history, not news
    def test_an_email_acknowledges_only_the_runs_it_named(self):
        # Email was down while failures piled up: more are pending than one email's log tail can carry.
        self.baseline()
        memory_path = self.root / "build" / "automations" / "ci-watch-reported.json"
        memory = json.loads(memory_path.read_text(encoding="utf-8"))
        memory["pending"] = [{"key": f"{n}/1", "workflow": "build.yml", "url": f"https://github.com/o/r/actions/runs/{n}",
                              "sha": "b" * 12, "failed_jobs": ["Build x64"]} for n in range(100, 161)]
        memory_path.write_text(json.dumps(memory), encoding="utf-8")
        green = FakeGh({"build.yml": [run(1)], "plan.yml": [run(2)]})
        code, out = self.scheduled(green)
        self.assertEqual(code, 1, out)
        lines = out.splitlines()
        named = [line for line in lines if line.startswith("red run on main:")]
        self.assertEqual(len(named), ci.ALERT_BATCH)
        # Everything the email must carry is inside the 60 lines the runner sends, and says that more will follow.
        self.assertEqual(lines[-(ci.ALERT_BATCH + 1):-1], named)
        sent = int(re.search(r"(?m)^LOG_LINES = (\d+)", (REPO / ".conclave/automations/alert.py").read_text(encoding="utf-8"))[1])
        self.assertLessEqual(ci.ALERT_BATCH + 1, sent)  # the runner's own number of log lines per email
        self.assertIn("main has 61 red run(s) not yet reported by email", lines[-1])
        self.assertIn("21 more follow in the next email", lines[-1])
        self.assertIn("runs/100;", out)
        self.assertNotIn("runs/140;", out)
        # That email is sent: the 40 it named are done, and the 21 it did not name are raised now.
        self.runner_says("sent")
        code, out = self.scheduled(green)
        self.assertEqual(code, 1, out)
        named = [line for line in out.splitlines() if line.startswith("red run on main:")]
        self.assertEqual(len(named), 21)
        self.assertIn("runs/140;", out)
        self.assertNotIn("runs/100;", out)
        self.runner_says("sent")
        self.assertEqual(self.scheduled(green)[0], 0)

    def test_known_reds_are_saved_and_said_before_any_slow_detail_call(self):
        self.baseline()
        memory_path = self.root / "build" / "automations" / "ci-watch-reported.json"
        clock, saved_at_first_detail, lines = Clock(), [], []

        class Stalling(FakeGh):
            """Every job-detail call takes two minutes, the longest one gh call may take."""

            def __call__(self, args, timeout=None):
                if args[:2] == ["run", "view"]:
                    if not saved_at_first_detail:
                        saved_at_first_detail.append((json.loads(memory_path.read_text(encoding="utf-8")), list(lines)))
                    clock.sleep(120)
                return super().__call__(args)
        runs = {"build.yml": [run(9, conclusion="failure"), run(8, conclusion="cancelled"), run(7, conclusion="failure"),
                              run(6, conclusion="failure"), run(1)], "plan.yml": [run(2)]}
        gh = Stalling(runs, jobs=RED_JOBS)
        code = ci.scheduled(runner=gh, root=self.root, now=lambda: self.NOW, out=lines.append, clock=clock)
        out = "\n".join(lines)
        self.assertEqual(code, 1, out)
        # Before the first detail call the three failed runs were already in the memory file and in the output.
        memory, said = saved_at_first_detail[0]
        self.assertEqual([p["key"] for p in memory["pending"]], ["6/1", "7/1", "9/1"])
        self.assertEqual(len([line for line in said if line.startswith("found: red run on main: build.yml")]), 3)
        # Detail calls stop once the budget is spent, so the poll ends well inside the task's five minutes.
        details = [call for call in gh.calls if call[:2] == ["run", "view"]]
        self.assertEqual(len(details), 2, details)
        self.assertLessEqual(clock.now, ci.POLL_BUDGET)
        self.assertIn("left for the next poll", out)
        for n in (6, 7, 9):
            self.assertIn(f"runs/{n}", out.split("latest completed run")[-1])  # in the closing block the email carries
    def test_the_first_run_reports_a_standing_timeout_and_not_an_old_one(self):
        timed_out = {"jobs": {9: [cancelled_job(91)], 4: [cancelled_job(41)]}, "annotations": {91: [TIMED_OUT], 41: [TIMED_OUT]}}
        # Main's newest build ran into its time limit when the task is installed: that is the standing red.
        standing = FakeGh({"build.yml": [run(9, conclusion="cancelled"), run(1)], "plan.yml": [run(2)]}, **timed_out)
        code, out = self.scheduled(standing)
        self.assertEqual(code, 1, out)
        self.assertIn("runs/9", out)
        self.assertEqual(self.scheduled(standing)[0], 1)  # pending until its email is sent
        self.runner_says("sent")
        self.assertEqual(self.scheduled(standing)[0], 0)
        # A timeout that a later green run has put behind it is history on a first run.
        self.tearDown()
        self.setUp()
        old = FakeGh({"build.yml": [run(5), run(4, conclusion="cancelled"), run(1)], "plan.yml": [run(2)]}, **timed_out)
        code, out = self.scheduled(old)
        self.assertEqual(code, 0, out)
        self.assertEqual(self.scheduled(old)[0], 0)

    def test_no_call_of_a_poll_is_given_more_time_than_the_poll_has_left(self):
        self.baseline()
        clock, given, lines = Clock(), [], []

        class Slow(FakeGh):
            """A slow network: each run list takes 60 seconds, a job lookup or an annotation read would take 110."""

            def __call__(self, args, timeout=None):
                cost = 60 if args[:2] == ["run", "list"] else self.detail
                given.append((args[0:2], timeout))
                if cost > timeout:
                    clock.sleep(timeout)
                    return 1, "", "gh could not run: timed out"
                clock.sleep(cost)
                return super().__call__(args)
        runs = {"build.yml": [run(9, conclusion="cancelled"), run(1)], "plan.yml": [run(2)]}
        slow = Slow(runs, jobs={9: [cancelled_job(91)]}, annotations={91: [TIMED_OUT]})
        slow.detail = 110
        code = ci.scheduled(runner=slow, root=self.root, now=lambda: self.NOW, out=lines.append, clock=clock)
        # The lists took 120 seconds and the job lookup 110: the annotation read got the 10 that were left, not 120.
        self.assertEqual([limit for _, limit in given], [120, 120, 120, 10])
        self.assertLessEqual(clock.now, ci.POLL_BUDGET)
        self.assertEqual(code, 0, lines)
        memory = json.loads((self.root / "build" / "automations" / "ci-watch-reported.json").read_text(encoding="utf-8"))
        self.assertNotIn("9/1", memory["passed"] + memory["reported"])  # not judged, so the next poll judges it
        self.assertIn("build.yml run 9", "\n".join(lines))
        # When the job lookup uses up everything that was left, the annotation read is not made at all.
        clock.now, slow.detail = 0.0, 120
        del given[:]
        code = ci.scheduled(runner=slow, root=self.root, now=lambda: self.NOW, out=lines.append, clock=clock)
        self.assertEqual([limit for _, limit in given], [120, 120, 120])
        self.assertEqual((code, clock.now), (0, 240.0))
        self.assertIn("not asked: this poll's 240 seconds are spent", "\n".join(lines))
        code, out = self.scheduled(FakeGh(runs, jobs={9: [cancelled_job(91)]}, annotations={91: [TIMED_OUT]}))
        self.assertEqual(code, 1, out)
        self.assertIn("runs/9", out)

    def test_the_real_gh_runner_takes_the_time_limit_it_is_given(self):
        with unittest.mock.patch.object(ci.shutil, "which", return_value="gh"), \
                unittest.mock.patch.object(ci.subprocess, "run", side_effect=subprocess.TimeoutExpired(["gh"], 7)) as ran:
            with self.assertRaises(ci.Unavailable):
                ci.run_gh(["run", "list"], timeout=7)
        self.assertEqual(ran.call_args.kwargs["timeout"], 7)
        self.assertEqual(ci.run_gh.__defaults__, (ci.GH_SECONDS,))

    def test_a_timed_out_run_is_a_red_run_here_too(self):
        self.baseline()
        gh = FakeGh({"build.yml": [run(9, conclusion="cancelled"), run(1)], "plan.yml": [run(2)]},
                    jobs={9: [cancelled_job(91)]}, annotations={91: [TIMED_OUT]})
        code, out = self.scheduled(gh)
        self.assertEqual(code, 1, out)
        self.assertIn("runs/9", out)

    def test_an_outage_alerts_after_an_hour_until_its_email_is_sent_and_never_after(self):
        down = FakeGh(fail=(1, "", "HTTP 502"))
        start = self.NOW
        self.assertEqual(self.scheduled(down, now=start)[0], 0)
        self.assertEqual(self.scheduled(down, now=start + 1800)[0], 0)
        code, out = self.scheduled(down, now=start + 3700)
        self.assertEqual(code, 1, out)
        self.assertIn("not yet reported by email: GitHub could not be read for over 60 minutes", out)
        # The outage that hides CI can stop the email too: it is raised again until one is sent.
        self.runner_says("not sent", at=start + 3700)
        self.assertEqual(self.scheduled(down, now=start + 4600)[0], 1)
        self.runner_says("sent", at=start + 4600)
        self.assertEqual(self.scheduled(down, now=start + 5500)[0], 0)
        self.assertEqual(self.scheduled(down, now=start + 9000)[0], 0)  # the same outage: one email
        # GitHub answers again, then goes away again: that is a new outage.
        self.assertEqual(self.scheduled(FakeGh({"build.yml": [run(1)], "plan.yml": [run(2)]}), now=start + 9900)[0], 0)
        self.assertEqual(self.scheduled(down, now=start + 10800)[0], 0)
        self.assertEqual(self.scheduled(down, now=start + 10800 + 3700)[0], 1)

    def test_a_run_that_cannot_be_judged_never_drops_a_red_found_beside_it(self):
        self.baseline()

        class ViewDown(FakeGh):
            def __call__(self, args, timeout=None):
                if args[:3] == ["run", "view", "8"]:
                    return 1, "", "HTTP 503: Service Unavailable"
                return super().__call__(args)
        runs = {"build.yml": [run(9, conclusion="failure"), run(8, conclusion="cancelled"), run(7, conclusion="failure"), run(1)],
                "plan.yml": [run(2)]}
        code, out = self.scheduled(ViewDown(runs, jobs=RED_JOBS))
        self.assertEqual(code, 1, out)
        self.assertIn("runs/7; failed jobs: Build x64 #7", out)
        self.assertIn("runs/9; failed jobs: Build x64 #9", out)  # the run after the unreadable one is still judged
        self.assertIn("build.yml run 8: gh run view exited 1: HTTP 503", out)
        # The run that could not be judged is judged on the next poll: here it timed out, so it is a red as well.
        self.runner_says("sent")
        code, out = self.scheduled(FakeGh(runs, jobs={**RED_JOBS, 8: [cancelled_job(81)]}, annotations={81: [TIMED_OUT]}))
        self.assertEqual(code, 1, out)
        self.assertIn("runs/8", out)
        self.assertNotIn("runs/7;", out)
        self.assertNotIn("runs/9;", out)

    def test_memory_that_cannot_be_written_is_unknown_and_never_a_silent_pass(self):
        (self.root / ".github" / "workflows").mkdir(parents=True)
        (self.root / ".github" / "workflows" / "build.yml").write_text("on:\n  push:\n", encoding="utf-8")
        gh = FakeGh({"build.yml": [run(5, conclusion="failure")]}, jobs=RED_JOBS)
        out = io.StringIO()
        with unittest.mock.patch.object(ci.os, "replace", side_effect=PermissionError(13, "locked")), redirect_stdout(out):
            code = ci.main(["--scheduled"], runner=gh, root=self.root)
        self.assertEqual(code, 3, out.getvalue())
        self.assertIn("ci-status unknown: cannot write", out.getvalue())
        self.assertIn("ci-watch-reported.json", out.getvalue())


if __name__ == "__main__":
    unittest.main()
