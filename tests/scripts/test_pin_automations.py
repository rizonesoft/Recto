"""Tests for the pin automations: the shared notifier and proposer, and every check script with fake sources.

Git-backed cases build a throwaway repository with an isolated git configuration, so the operator's global settings
(signing, hooks, default branch) never apply and nothing outside the temp folder changes.
"""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

REPO = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), REPO / "scripts" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def completed(returncode, stdout=""):
    """A stand-in for subprocess.run's result."""
    return types.SimpleNamespace(returncode=returncode, stdout=stdout, stderr="")


def returning(value):
    """A fake fetch that answers every URL with the same value."""
    def fetch(url):
        return value
    return fetch


class Mailbox:
    def __init__(self):
        self.sent = []

    def __call__(self, root, subject, body, dry_run=False):
        self.sent.append((subject, body))


class GitRepo(unittest.TestCase):
    """A temp repository on branch main with an isolated git configuration."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name) / "repo"
        self.root.mkdir()
        empty = Path(self._tmp.name) / "gitconfig"
        empty.write_text("", encoding="utf-8")
        env = {"GIT_CONFIG_GLOBAL": str(empty), "GIT_CONFIG_NOSYSTEM": "1", "GIT_AUTHOR_NAME": "t",
               "GIT_AUTHOR_EMAIL": "t@example.invalid", "GIT_COMMITTER_NAME": "t",
               "GIT_COMMITTER_EMAIL": "t@example.invalid"}
        self._env = mock.patch.dict(os.environ, env)
        self._env.start()
        self.git("init", "-q", "-b", "main")

    def tearDown(self):
        self._env.stop()
        self._tmp.cleanup()

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.root, capture_output=True, text=True, check=True).stdout.strip()

    def write(self, rel, text):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="")

    def commit(self):
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "fixture")

    def ref_exists(self, ref):
        return subprocess.run(["git", "rev-parse", "--verify", "--quiet", ref], cwd=self.root).returncode == 0


class NotifierTests(unittest.TestCase):
    def test_notify_passes_the_automations_sender(self):
        """Regression: alert.send takes the From line; a pin notifier once called it with three arguments."""
        calls = []
        fake = types.ModuleType("alert")
        fake.key = lambda: "k"
        fake.sender = lambda folder: f"{folder.strip(chr(92))} automations <x@example.invalid>"
        fake.send = lambda api_key, subject, body, from_: calls.append((api_key, subject, body, from_))
        sys.path.insert(0, str(REPO / "scripts" / "lib"))
        import pin_proposal
        pin_proposal.notify(REPO, "subject", "body", alert_module=fake)
        self.assertEqual(calls, [("k", "subject", "body", "Recto automations <x@example.invalid>")])


class ActionsTests(GitRepo):
    WORKFLOW = "on: push\njobs:\n  b:\n    steps:\n      - uses: actions/checkout@v4\n      - uses: ./local\n      - uses: actions/cache@v6 # keep\n"

    def setUp(self):
        super().setUp()
        self.mod = load("check-actions-pins")
        self.write(".github/workflows/ci.yml", self.WORKFLOW)
        self.commit()
        self.fetch = lambda url: {"tag_name": {"actions/checkout": "v7.0.1", "actions/cache": "v6.1.0"}[
            url.split("/repos/")[1].split("/releases")[0]]}

    def test_check_reports_only_the_stale_action(self):
        self.assertEqual(self.mod.main(["check"], fetch=self.fetch, root=self.root), 1)
        found = self.mod.faults(self.mod.scan(self.root), self.fetch)
        self.assertEqual([(f[2], f[3], f[5]) for f in found], [("actions/checkout", "v4", 7)])

    def test_rewrite_keeps_formatting_and_untouched_lines(self):
        new = self.mod.rewrite_text(self.WORKFLOW, {"actions/checkout": 7})
        self.assertEqual(new, self.WORKFLOW.replace("checkout@v4", "checkout@v7"))

    def test_update_proposes_once_on_a_branch_and_leaves_main_alone(self):
        mail = Mailbox()
        self.assertEqual(self.mod.main(["update"], fetch=self.fetch, root=self.root, mail=mail), 0)
        branches = self.git("branch", "--list", "pins/actions-*")
        self.assertTrue(branches)
        branch = branches.strip("* ").strip()
        self.assertIn("actions/checkout@v7", self.git("show", f"{branch}:.github/workflows/ci.yml"))
        self.assertIn("actions/checkout@v4", self.git("show", "main:.github/workflows/ci.yml"))
        self.assertEqual(len(mail.sent), 1)
        self.assertEqual(self.mod.main(["update"], fetch=self.fetch, root=self.root, mail=mail), 0)
        self.assertEqual(len(mail.sent), 1, "a finished proposal is not emailed again")

    def test_failed_email_removes_the_branch_for_a_retry(self):
        def broken(*_):
            raise OSError("smtp down")
        self.assertEqual(self.mod.main(["update"], fetch=self.fetch, root=self.root, mail=broken), 1)
        self.assertEqual(self.git("branch", "--list", "pins/actions-*"), "")


class VcpkgTests(GitRepo):
    OLD, NEW = "a" * 40, "b" * 40

    def setUp(self):
        super().setUp()
        self.mod = load("check-vcpkg-baseline")
        self.write("vcpkg-configuration.json", json.dumps({"default-registry": {"kind": "git", "baseline": self.OLD}}, indent=2))
        self.write("build.cmd", "@echo off\n")
        self.commit()

    def fetch(self, status):
        def fake(url):
            if url.endswith("/releases/latest"):
                return {"tag_name": "2026.10.16"}
            if "/commits/" in url:
                return {"sha": self.NEW}
            return {"status": status}
        return fake

    def test_release_ahead_of_the_baseline_is_stale(self):
        self.assertEqual(self.mod.main(["check"], fetch=self.fetch("ahead"), root=self.root), 1)

    def test_baseline_past_the_release_is_current(self):
        self.assertEqual(self.mod.main(["check"], fetch=self.fetch("behind"), root=self.root), 0)

    def test_update_requires_the_build_and_rewrites_only_the_baseline(self):
        mail = Mailbox()
        self.assertEqual(self.mod.main(["update"], fetch=self.fetch("ahead"), root=self.root,
                                       build=lambda *a, **k: completed(0), mail=mail), 0)
        text = self.git("show", "pins/vcpkg-2026.10.16:vcpkg-configuration.json")
        self.assertIn(self.NEW, text)
        self.assertNotIn(self.OLD, text)
        self.assertEqual(len(mail.sent), 1)

    def test_failed_build_proposes_nothing(self):
        self.assertEqual(self.mod.main(["update"], fetch=self.fetch("ahead"), root=self.root,
                                       build=lambda *a, **k: completed(1, "boom"), mail=Mailbox()), 1)
        self.assertEqual(self.git("branch", "--list", "pins/*"), "")


class NoticeTests(GitRepo):
    def setUp(self):
        super().setUp()
        props = ("<Project><PropertyGroup><WindowsTargetPlatformVersion>10.0.26100.0</WindowsTargetPlatformVersion>"
                 "</PropertyGroup></Project>")
        self.write("Directory.Build.props", props)
        self.write("src/app/Recto.vcxproj", props)
        self.commit()
        self.sdk = load("check-windows-sdk-line")
        self.feed = lambda url: {"versions": ["10.0.26100.9169", "10.0.28000.2705", "10.0.29000.1-preview"]}

    def test_new_sdk_line_is_announced_once_and_the_task_succeeds(self):
        mail = Mailbox()
        self.assertEqual(self.sdk.main(["check"], fetch=self.feed, root=self.root), 1)
        self.assertEqual(self.sdk.main(["notify"], fetch=self.feed, root=self.root, mail=mail), 0)
        self.assertEqual(self.sdk.main(["notify"], fetch=self.feed, root=self.root, mail=mail), 0)
        self.assertEqual([s for s, _ in mail.sent], ["Recto: Windows SDK 10.0.28000 is available"])
        self.assertTrue(self.ref_exists("refs/recto-notice/windows-sdk-10.0.28000"))

    def test_disagreeing_target_versions_fail(self):
        self.write("src/app/Recto.vcxproj", "<Project><PropertyGroup><WindowsTargetPlatformVersion>10.0.22621.0"
                                            "</WindowsTargetPlatformVersion></PropertyGroup></Project>")
        self.assertEqual(self.sdk.main(["notify"], fetch=self.feed, root=self.root, mail=Mailbox()), 1)

    def test_unsent_notice_is_retried(self):
        def broken(*_):
            raise OSError("smtp down")
        self.assertEqual(self.sdk.main(["notify"], fetch=self.feed, root=self.root, mail=broken), 1)
        self.assertFalse(self.ref_exists("refs/recto-notice/windows-sdk-10.0.28000"))

    def test_python_pins_behind_the_newest_minor_are_reported(self):
        py = load("check-python-pin")
        self.write(".github/workflows/plan.yml", "      - uses: actions/setup-python@v7\n        with:\n          python-version: '3.12'\n")
        releases = [{"name": "Python 3.15.0"}, {"name": "Python 3.14.8"}, {"name": "Python 3.14.4"}]
        fetch = returning(releases)
        self.assertEqual(py.main(["check"], fetch=fetch, root=self.root, local=(3, 15, 0)), 1)
        found = py.findings(*py.latest_releases(releases), py.ci_pins(self.root), (3, 14, 4))
        self.assertEqual(len(found), 2)
        self.write(".github/workflows/plan.yml", "          python-version: '3.15'\n")
        self.assertEqual(py.main(["check"], fetch=fetch, root=self.root, local=(3, 15, 0)), 0)

    def test_visual_studio_behind_its_servicing_release_is_reported(self):
        vs = load("check-visual-studio")
        self.write("Directory.Build.props", "<Project><PropertyGroup><PlatformToolset>v145</PlatformToolset></PropertyGroup></Project>")
        fake_vswhere = self.root / "vswhere.exe"
        fake_vswhere.write_text("", encoding="utf-8")
        rows = [{"displayName": "Visual Studio Professional 2026", "isPrerelease": False,
                 "catalog": {"productDisplayVersion": "18.11.0"}}]
        def run(*args, **kwargs):
            return completed(0, json.dumps(rows))
        channel = returning({"info": {"productDisplayVersion": "18.11.1"}})
        with mock.patch.object(vs, "VSWHERE", fake_vswhere):
            mail = Mailbox()
            self.assertEqual(vs.main(["check"], fetch=channel, root=self.root, run=run), 1)
            self.assertEqual(vs.main(["notify"], fetch=channel, root=self.root, run=run, mail=mail), 0)
            self.assertEqual(len(mail.sent), 1)
            current = returning({"info": {"productDisplayVersion": "18.11.0"}})
            self.assertEqual(vs.main(["check"], fetch=current, root=self.root, run=run), 0)


class FakeGh:
    """Records gh calls and answers the list queries the pin modes make."""

    def __init__(self, prs="[]", issues="[]"):
        self.calls, self.prs, self.issues = [], prs, issues

    def __call__(self, *args, cwd):
        self.calls.append(args)
        if args[:2] == ("pr", "list"):
            return self.prs
        if args[:2] == ("issue", "list"):
            return self.issues
        if args[:2] in (("pr", "create"), ("issue", "create")):
            return "https://github.com/example/repo/1\n"
        return ""

    def made(self, *prefix):
        return [c for c in self.calls if c[:len(prefix)] == prefix]


class GitHubModeTests(GitRepo):
    """The pins workflow's modes: a PR with CI dispatched on its branch, and deduplicated issues."""

    OLD, NEW = "a" * 40, "b" * 40

    def setUp(self):
        super().setUp()
        self.write("vcpkg-configuration.json", json.dumps({"default-registry": {"kind": "git", "baseline": self.OLD}}))
        self.commit()
        origin = Path(self._tmp.name) / "origin.git"
        subprocess.run(["git", "init", "-q", "--bare", "-b", "main", str(origin)], check=True)
        self.git("remote", "add", "origin", str(origin))
        self.git("push", "-q", "origin", "main")
        self.origin = origin
        self.vcpkg = load("check-vcpkg-baseline")

    def fetch(self, url):
        if url.endswith("/releases/latest"):
            return {"tag_name": "2026.10.16"}
        if "/commits/" in url:
            return {"sha": self.NEW}
        return {"status": "ahead"}

    def test_pr_mode_pushes_the_branch_opens_a_pr_and_dispatches_ci(self):
        gh = FakeGh()
        self.assertEqual(self.vcpkg.main(["pr"], fetch=self.fetch, root=self.root, run_gh=gh), 0)
        remote = subprocess.run(["git", f"--git-dir={self.origin}", "show",
                                 "pins/vcpkg-2026.10.16:vcpkg-configuration.json"],
                                capture_output=True, text=True, check=True).stdout
        self.assertIn(self.NEW, remote)
        self.assertEqual(len(gh.made("pr", "create")), 1)
        self.assertEqual([c[2] for c in gh.made("workflow", "run")], ["build.yml", "plan.yml"])
        self.assertEqual(self.git("rev-parse", "--abbrev-ref", "HEAD"), "main")
        self.assertIn(self.OLD, (self.root / "vcpkg-configuration.json").read_text(encoding="utf-8"))

    def test_a_version_with_any_existing_pr_is_never_proposed_again(self):
        gh = FakeGh(prs='[{"number": 7, "state": "CLOSED"}]')
        self.assertEqual(self.vcpkg.main(["pr"], fetch=self.fetch, root=self.root, run_gh=gh), 0)
        self.assertEqual(gh.made("pr", "create"), [])
        listed = subprocess.run(["git", f"--git-dir={self.origin}", "branch", "--list", "pins/*"],
                                capture_output=True, text=True, check=True)
        self.assertEqual(listed.stdout.strip(), "")

    def test_issue_mode_files_once_supersedes_older_and_resolves(self):
        props = "<Project><PropertyGroup><WindowsTargetPlatformVersion>10.0.26100.0</WindowsTargetPlatformVersion></PropertyGroup></Project>"
        self.write("Directory.Build.props", props)
        self.write("src/app/Recto.vcxproj", props)
        sdk = load("check-windows-sdk-line")
        feed = returning({"versions": ["10.0.26100.9169", "10.0.28000.2705"]})
        older = '[{"number": 3, "title": "Windows SDK 10.0.27000 is available", "state": "OPEN"}]'
        gh = FakeGh(issues=older)
        self.assertEqual(sdk.main(["issue"], fetch=feed, root=self.root, run_gh=gh), 0)
        self.assertEqual([c[2] for c in gh.made("issue", "close")], ["3"])
        self.assertEqual(len(gh.made("issue", "create")), 1)
        same = '[{"number": 4, "title": "Windows SDK 10.0.28000 is available", "state": "OPEN"}]'
        gh = FakeGh(issues=same)
        self.assertEqual(sdk.main(["issue"], fetch=feed, root=self.root, run_gh=gh), 0)
        self.assertEqual(gh.made("issue", "create"), [])
        gh = FakeGh(issues='[{"number": 4}]')
        current = returning({"versions": ["10.0.26100.9169"]})
        self.assertEqual(sdk.main(["issue"], fetch=current, root=self.root, run_gh=gh), 0)
        self.assertEqual([c[2] for c in gh.made("issue", "close")], ["4"])

    def test_python_issue_scope_ci_ignores_the_local_interpreter(self):
        py = load("check-python-pin")
        self.write(".github/workflows/plan.yml", "          python-version: '3.15'\n")
        releases = returning([{"name": "Python 3.15.0"}])
        gh = FakeGh()
        self.assertEqual(py.main(["issue", "--scope", "ci"], fetch=releases, root=self.root, local=(3, 9, 0), run_gh=gh), 0)
        self.assertEqual(gh.made("issue", "create"), [])


class SdkPinsNotifyTests(GitRepo):
    def test_newer_release_is_announced_and_the_task_succeeds(self):
        mod = load("check-sdk-pins")
        self.write("x", "x")
        self.commit()
        faults = [("Microsoft.Windows.CppWinRT", "3.0.1", "3.0.2", "latest stable is 3.0.2")]
        mail = Mailbox()
        with mock.patch.object(mod, "read_project", return_value=({}, (10, 0, 26100))), \
                mock.patch.object(mod, "check", return_value=faults), mock.patch.object(mod, "ROOT", self.root):
            self.assertEqual(mod.main([]), 1)
            self.assertEqual(mod.main(["notify"], mail=mail), 0)
            self.assertEqual(mod.main(["notify"], mail=mail), 0)
        self.assertEqual(len(mail.sent), 1)

    def test_unstable_pin_still_fails_in_notify_mode(self):
        mod = load("check-sdk-pins")
        faults = [("Microsoft.Windows.CppWinRT", "3.0.1-preview", None, "is not an exact stable version")]
        with mock.patch.object(mod, "read_project", return_value=({}, (10, 0, 26100))), \
                mock.patch.object(mod, "check", return_value=faults):
            self.assertEqual(mod.main(["notify"], mail=Mailbox()), 1)


if __name__ == "__main__":
    unittest.main()
