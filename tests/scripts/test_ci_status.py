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


if __name__ == "__main__":
    unittest.main()
