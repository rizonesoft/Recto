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
        """Regression: alert.send takes the From line; the WinUI notifier once called it with three arguments."""
        calls = []
        fake = types.ModuleType("alert")
        fake.key = lambda: "k"
        fake.sender = lambda folder: f"{folder.strip(chr(92))} automations <x@example.invalid>"
        fake.send = lambda api_key, subject, body, from_: calls.append((api_key, subject, body, from_))
        with mock.patch.dict(sys.modules, {"alert": fake}):
            load("check-winui-pin").notify("subject", "body")
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


class ConclaveKitTests(GitRepo):
    def setUp(self):
        super().setUp()
        self.mod = load("check-conclave-kits")
        self.sdk = self.root / "sdk"
        self.sdk.mkdir()
        subprocess.run(["git", "init", "-q", "-b", "main"], cwd=self.sdk, check=True)
        skill = self.sdk / "kits" / "todo" / "templates" / ".claude" / "skills" / "x" / "SKILL.md"
        skill.parent.mkdir(parents=True)
        skill.write_text("kit text\n", encoding="utf-8")
        (self.sdk / "kits" / "todo" / "templates" / "AGENTS.md").write_text("project file\n", encoding="utf-8")
        subprocess.run(["git", "add", "-A"], cwd=self.sdk, check=True)
        subprocess.run(["git", "commit", "-q", "-m", "kit"], cwd=self.sdk, check=True)
        self.repo = Path(self._tmp.name) / "consumer"
        (self.repo / ".claude" / "skills" / "x").mkdir(parents=True)

    def test_identical_kit_file_is_in_step_and_project_files_are_ignored(self):
        (self.repo / ".claude" / "skills" / "x" / "SKILL.md").write_bytes(b"kit text\r\n")
        self.assertEqual(self.mod.drift(self.sdk, self.repo), [])

    def test_changed_kit_file_is_drift_unless_allowed_with_a_reason(self):
        (self.repo / ".claude" / "skills" / "x" / "SKILL.md").write_text("local edit\n", encoding="utf-8")
        self.assertEqual(self.mod.drift(self.sdk, self.repo), [".claude/skills/x/SKILL.md differs from the SDK (kit todo)"])
        (self.repo / ".conclave").mkdir()
        (self.repo / ".conclave" / "kit-drift-allow.txt").write_text(".claude/skills/x/SKILL.md | kept on purpose\n",
                                                                       encoding="utf-8")
        self.assertEqual(self.mod.drift(self.sdk, self.repo), [])

    def test_allow_entry_without_a_reason_is_refused(self):
        (self.repo / ".conclave").mkdir()
        (self.repo / ".conclave" / "kit-drift-allow.txt").write_text(".claude/skills/x/SKILL.md\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            self.mod.allowed(self.repo)

    def test_uncommitted_sdk_change_is_not_drift(self):
        (self.repo / ".claude" / "skills" / "x" / "SKILL.md").write_text("kit text\n", encoding="utf-8")
        (self.sdk / "kits" / "todo" / "templates" / ".claude" / "skills" / "x" / "SKILL.md").write_text("wip\n")
        self.assertEqual(self.mod.drift(self.sdk, self.repo), [])


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
