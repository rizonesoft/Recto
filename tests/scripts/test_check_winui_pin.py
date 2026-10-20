"""scripts/check-winui-pin.py: the pin follows the latest stable WinUI 3 release, proposed on a branch, never on main."""
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[2] / "scripts"
spec = importlib.util.spec_from_file_location("check_winui_pin", SCRIPTS / "check-winui-pin.py")
wp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wp)

PROPS = """<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <!-- keep this comment -->
    <PackageReference Include="Microsoft.WindowsAppSDK" Version="2.4.0" />
    <PackageReference Include="Microsoft.Windows.CppWinRT" Version="3.0.260818.1" />
  </ItemGroup>
</Project>
"""


def release(tag, prerelease=False):
    return {"tag_name": tag, "prerelease": prerelease, "published_at": "2026-09-16T22:53:27Z",
            "html_url": "https://example.invalid/" + tag}


RELEASES = [release("winui3/release/2.5.4-experimental", True), release("winui3/release/1.8.12"),
            release("winui3/release/2.5.1"), release("winui3/release/2.4.0"), release("winui2/release/2.8.7"),
            release("winui3/release/2.6.0-experimental")]


class Selection(unittest.TestCase):
    def test_the_newest_stable_winui3_release_wins(self):
        self.assertEqual(wp.latest_stable(RELEASES)[0], "2.5.1")

    def test_experimental_never_counts_even_unmarked(self):
        sneaky = [release("winui3/release/9.9.9-experimental"), release("winui3/release/2.5.1")]
        self.assertEqual(wp.latest_stable(sneaky)[0], "2.5.1")
        labelled = [dict(release("winui3/release/2.6.0"), name="WinUI 3 in WinAppSDK 2.6.0-experimental"),
                    release("winui3/release/2.5.1")]
        self.assertEqual(wp.latest_stable(labelled)[0], "2.5.1")

    def test_versions_compare_numerically(self):
        self.assertEqual(wp.latest_stable([release("winui3/release/2.9.0"), release("winui3/release/2.10.0")])[0], "2.10.0")
        self.assertTrue(wp.newer("2.10.0", "2.9.9"))
        self.assertFalse(wp.newer("2.5.1", "2.5.1"))

    def test_the_writer_rewrites_the_reference_the_reader_reads(self):
        reordered = PROPS.replace('<PackageReference Include="Microsoft.WindowsAppSDK" Version="2.4.0" />',
                                  "<PackageReference Version='2.4.0' Include='Microsoft.WindowsAppSDK' />")
        self.assertEqual(wp.pinned_text(reordered), "2.4.0")
        self.assertIn("<PackageReference Version='2.5.1' Include='Microsoft.WindowsAppSDK' />",
                      wp.repinned(reordered, "2.4.0", "2.5.1"))
        commented = PROPS.replace("<!-- keep this comment -->",
                                  '<!-- <PackageReference Include="Microsoft.WindowsAppSDK" Version="2.4.0" /> -->')
        out = wp.repinned(commented, "2.4.0", "2.5.1")
        self.assertIn('<!-- <PackageReference Include="Microsoft.WindowsAppSDK" Version="2.4.0" /> -->', out)
        self.assertEqual(wp.pinned_text(out), "2.5.1")

    def test_only_the_version_changes(self):
        out = wp.repinned(PROPS, "2.4.0", "2.5.1")
        self.assertEqual(out, PROPS.replace('Version="2.4.0"', 'Version="2.5.1"'))
        with self.assertRaises(ValueError):
            wp.repinned(PROPS, "1.0.0", "2.5.1")


class Repo(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        run = lambda *a: subprocess.run(["git", *a], cwd=self.root, check=True, capture_output=True)
        run("init", "-q", "-b", "main")
        run("config", "user.email", "test@example.invalid")
        run("config", "user.name", "Test")
        (self.root / "src" / "app").mkdir(parents=True)
        (self.root / wp.PIN_FILE).write_text(PROPS, encoding="utf-8", newline="")
        (self.root / "build.cmd").write_text("@echo off\n", encoding="utf-8")
        run("add", ".")
        run("commit", "-q", "-m", "init")
        self.mails = []

    def tearDown(self):
        self.tmp.cleanup()

    @staticmethod
    def built(cmd, **kwargs):
        return subprocess.CompletedProcess(cmd, 0, stdout="", stderr="")

    def main(self, *argv, releases=RELEASES, nuget=("2.4.0", "2.5.1"), build=None, mail=None, feed=None):
        def fetch(url):
            if "github" in url:
                return releases
            return feed if feed is not None else {"versions": list(nuget)}

        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = wp.main(list(argv), fetch=fetch, root=self.root, build=build or self.built,
                           mail=mail or (lambda s, b, d=False: self.mails.append((s, b))))
        return code, out.getvalue() + err.getvalue()

    def commit_pin(self, version):
        (self.root / wp.PIN_FILE).write_text(PROPS.replace('Version="2.4.0"', f'Version="{version}"'),
                                             encoding="utf-8", newline="")
        subprocess.run(["git", "commit", "-q", "-am", f"pin {version}"], cwd=self.root, check=True, capture_output=True)

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.root, capture_output=True, text=True).stdout.strip()

    def test_check_fails_on_an_older_pin_and_passes_on_the_latest(self):
        self.assertEqual(self.main("check")[0], 1)
        self.assertEqual(self.main("check", releases=[release("winui3/release/2.4.0")])[0], 0)

    def test_update_proposes_on_a_branch_and_leaves_main_and_the_checkout_alone(self):
        before = (self.root / wp.PIN_FILE).read_text(encoding="utf-8")
        code, out = self.main("update")
        self.assertEqual(code, 0, out)
        self.assertEqual((self.root / wp.PIN_FILE).read_text(encoding="utf-8"), before)
        self.assertIn('Version="2.4.0"', self.git("show", f"main:{wp.PIN_FILE}"))
        self.assertIn('Version="2.5.1"', self.git("show", f"winui/2.5.1:{wp.PIN_FILE}"))
        self.assertIn("keep this comment", self.git("show", f"winui/2.5.1:{wp.PIN_FILE}"))
        self.assertEqual(self.git("worktree", "list").count("\n"), 0)  # the temporary worktree is gone
        self.assertEqual(len(self.mails), 1)
        self.assertIn("git merge winui/2.5.1", self.mails[0][1])
        self.assertEqual(self.main("update")[0], 0)  # proposed before: nothing is redone or re-sent
        self.assertEqual(len(self.mails), 1)

    def test_a_failed_check_proposes_nothing(self):
        code, out = self.main("update", nuget=("2.4.0",))
        self.assertEqual(code, 1)
        self.assertIn("NuGet has no", out)
        self.assertEqual(self.git("branch", "--list", "winui/*"), "")
        self.assertEqual(self.mails, [])

    def test_a_failed_build_proposes_nothing_and_a_passing_one_proposes(self):
        calls = []

        def build(cmd, **kwargs):
            calls.append((cmd, Path(kwargs["cwd"]).name))
            return subprocess.CompletedProcess(cmd, calls_exit[0], stdout="error C2065", stderr="")

        calls_exit = [1]
        code, out = self.main("update", build=build)
        self.assertEqual(code, 1)
        self.assertIn("build.cmd failed", out)
        self.assertEqual(calls, [(["cmd", "/c", "build.cmd"], "tree")])  # built in the worktree, never the checkout
        self.assertEqual((self.git("branch", "--list", "winui/*"), self.mails), ("", []))
        calls_exit[0] = 0
        self.assertEqual(self.main("update", build=build)[0], 0)
        self.assertIn('Version="2.5.1"', self.git("show", f"winui/2.5.1:{wp.PIN_FILE}"))

    def test_unreadable_releases_exit_2(self):
        def broken(url):
            raise wp.Unreadable("offline")

        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(wp.main(["check"], fetch=broken, root=self.root), 2)

    def test_dry_run_changes_and_sends_nothing(self):
        code, out = self.main("update", "--dry-run")
        self.assertEqual(code, 1)
        self.assertIn("would create winui/2.5.1", out)
        self.assertEqual((self.git("branch", "--list", "winui/*"), self.mails), ("", []))

    def test_main_without_build_cmd_proposes_nothing(self):
        subprocess.run(["git", "rm", "-q", "build.cmd"], cwd=self.root, check=True, capture_output=True)
        subprocess.run(["git", "commit", "-q", "-m", "no build"], cwd=self.root, check=True, capture_output=True)
        code, out = self.main("update")
        self.assertEqual(code, 1)
        self.assertIn("main has no build.cmd", out)
        self.assertEqual((self.git("branch", "--list", "winui/*"), self.mails), ("", []))

    def test_main_not_the_checkout_is_the_baseline(self):
        # The checkout already reads 2.5.1 but main still pins 2.4.0: main is what needs the proposal.
        (self.root / wp.PIN_FILE).write_text(PROPS.replace('Version="2.4.0"', 'Version="2.5.1"'), encoding="utf-8")
        code, out = self.main("update")
        self.assertEqual(code, 0, out)
        self.assertIn('Version="2.5.1"', self.git("show", f"winui/2.5.1:{wp.PIN_FILE}"))
        self.assertEqual(len(self.mails), 1)

    def test_main_ahead_of_the_feed_fails_even_when_the_checkout_lags(self):
        self.commit_pin("2.6.0")  # main pins 2.6.0 ...
        (self.root / wp.PIN_FILE).write_text(PROPS, encoding="utf-8", newline="")  # ... the checkout still reads 2.4.0
        code, out = self.main("update", nuget=("2.4.0", "2.5.1", "2.6.0"))
        self.assertEqual(code, 1, out)
        self.assertIn("2.6.0 is ahead of the newest stable WinUI 3 release 2.5.1", out)
        self.assertIn('Version="2.6.0"', self.git("show", f"main:{wp.PIN_FILE}"))
        self.assertEqual((self.git("branch", "--list", "winui/*"), self.mails), ("", []))

    def test_a_prerelease_on_main_fails_cleanly_when_the_checkout_is_stable(self):
        self.commit_pin("2.5.4-experimental")
        (self.root / wp.PIN_FILE).write_text(PROPS, encoding="utf-8", newline="")
        code, out = self.main("update")
        self.assertEqual(code, 1)
        self.assertIn("is not an exact stable version", out)
        self.assertEqual(self.mails, [])

    def test_update_refuses_a_main_that_turned_prerelease_after_the_check(self):
        # main() validated main, then main moved to a prerelease before the worktree was made: update decides again.
        self.commit_pin("2.5.4-experimental")
        err = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(err):
            code = wp.update("2.5.1", release("winui3/release/2.5.1"), {"2.5.1"}, self.root, build=self.built,
                             mail=lambda s, b, d=False: self.mails.append((s, b)))
        self.assertEqual(code, 1)
        self.assertIn("not an exact stable version", err.getvalue())
        self.assertEqual((self.git("branch", "--list", "winui/*"), self.mails), ("", []))

    def test_a_current_pin_missing_from_nuget_fails(self):
        code, out = self.main("check", releases=[release("winui3/release/2.4.0")], nuget=("2.5.1",))
        self.assertEqual(code, 1)
        self.assertIn("2.4.0 on main is not published on NuGet", out)

    def test_an_unfinished_branch_is_discarded_and_redone(self):
        subprocess.run(["git", "branch", "winui/2.5.1"], cwd=self.root, check=True, capture_output=True)
        code, out = self.main("update")
        self.assertEqual(code, 0, out)
        self.assertIn("discarded the unfinished winui/2.5.1", out)
        self.assertIn('Version="2.5.1"', self.git("show", f"winui/2.5.1:{wp.PIN_FILE}"))
        self.assertEqual(self.git("rev-parse", "refs/winui-pin/2.5.1"), self.git("rev-parse", "winui/2.5.1"))
        self.assertEqual(len(self.mails), 1)

    def test_a_finished_proposal_is_never_resent(self):
        self.assertEqual(self.main("update")[0], 0)
        subprocess.run(["git", "branch", "-D", "winui/2.5.1"], cwd=self.root, check=True, capture_output=True)
        code, out = self.main("update")  # the operator deleted the branch; the marker still says it was proposed
        self.assertEqual(code, 0)
        self.assertIn("proposed and emailed before", out)
        self.assertEqual(len(self.mails), 1)

    def test_a_failed_cleanup_is_a_failure_not_a_proposal(self):
        real = wp.git

        def git(*args, **kwargs):
            if args[:2] == ("branch", "-D"):
                return subprocess.CompletedProcess(args, 1, stdout="", stderr="error: branch is locked")
            return real(*args, **kwargs)

        with mock.patch.object(wp, "git", git):
            code, out = self.main("update", nuget=("2.4.0",))  # the check fails, so the branch must go
            self.assertEqual(code, 1)
            self.assertIn("cleanup failed", out)
            self.assertIn("could not delete winui/2.5.1", out)
            code, out = self.main("update")  # the leftover branch is not a proposal: every run fails until repaired
            self.assertEqual(code, 1)
            self.assertIn("an unfinished proposal is in the way", out)
        self.assertEqual(self.mails, [])

    def test_an_unsent_proposal_is_dropped_so_the_next_run_retries(self):
        def down(subject, body, dry_run=False):
            raise OSError("SMTP2GO unreachable")

        code, out = self.main("update", mail=down)
        self.assertEqual(code, 1)
        self.assertIn("could not email the proposal", out)
        self.assertEqual(self.git("branch", "--list", "winui/*"), "")
        self.assertEqual(self.main("update")[0], 0)  # the next run proposes and sends it
        self.assertEqual(len(self.mails), 1)

    def test_a_pin_ahead_of_the_newest_stable_release_fails(self):
        self.commit_pin("99.0.0")
        code, out = self.main("check")
        self.assertEqual(code, 1)
        self.assertIn("ahead of the newest stable WinUI 3 release 2.5.1", out)

    def test_a_prerelease_pin_fails_cleanly(self):
        self.commit_pin("2.5.4-experimental")
        code, out = self.main("check")
        self.assertEqual(code, 1)
        self.assertIn("is not an exact stable version", out)

    def test_odd_feed_shapes_are_unreadable_not_crashes(self):
        self.assertEqual(self.main("check", releases=[None, 7, release("winui3/release/2.4.0")])[0], 0)
        for feed in ([], {"versions": None}, {"versions": ["2.5.1", 3]}):
            with self.subTest(feed=feed):
                self.assertEqual(self.main("update", feed=feed)[0], 2)


class RepositoryPin(unittest.TestCase):
    def test_the_repository_pins_one_windows_app_sdk_version(self):
        self.assertRegex(wp.pinned(), r"^\d+\.\d+\.\d+$")


if __name__ == "__main__":
    unittest.main()
