"""scripts/merge_prs.py: dependency pull requests merge only when safe, one at a time, stopping at the first failure."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("merge_prs", ROOT / "scripts" / "merge_prs.py")
merge_prs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(merge_prs)
SECTION_IN_PROGRESS = merge_prs.section_in_progress  # the real one; the merge tests patch it
SHA = "a" * 40
GREEN_RUNS = [(name, "completed", "success") for name in sorted(merge_prs.REQUIRED_CHECKS)]


def pr(number, *, author="app/github-actions", labels=("dependencies", "pins"), mergeable="MERGEABLE",
       checks=None, branch="pins/vcpkg-2026.10.16", base="main"):
    """A pull request as gh pr view shows it, with its head commit's check runs under "_runs" (for the fake)."""
    return {"number": number, "title": f"pin {number}", "author": {"login": author}, "state": "OPEN",
            "labels": [{"name": n} for n in labels], "baseRefName": base, "headRefName": branch,
            "headRefOid": f"{number:040d}", "mergeable": mergeable,
            "_runs": GREEN_RUNS if checks is None else list(checks), "_statuses": []}


class FakeRun:
    """Answers git and gh the way a test asks, and records every call. after_merge(number, prs) may change later
    answers."""

    def __init__(self, prs, *, dirty="", head=SHA, upstream=SHA, fail=(), after_merge=None):
        self.prs = {p["number"]: p for p in prs}
        self.dirty, self.head, self.upstream, self.fail, self.after_merge = dirty, head, upstream, fail, after_merge
        self.calls = []

    def __call__(self, args, root):
        self.calls.append(args)
        if any(f in " ".join(args) for f in self.fail):
            return subprocess.CompletedProcess(args, 1, "", "refused")
        out = ""
        if args[:2] == ["git", "status"]:
            out = self.dirty
        elif args[:3] == ["git", "rev-parse", "HEAD"]:
            out = self.head
        elif args[:3] == ["git", "rev-parse", "origin/main"]:
            out = self.upstream
        elif args[:3] == ["gh", "pr", "list"]:
            base = args[args.index("--base") + 1]
            out = json.dumps([{"number": n} for n, p in self.prs.items() if p["baseRefName"] == base])
        elif args[:3] == ["gh", "pr", "view"]:
            out = json.dumps({k: v for k, v in self.prs[int(args[3])].items() if not k.startswith("_")})
        elif args[:2] == ["gh", "api"]:
            path = next(a for a in args if "/commits/" in a)
            sha = path.split("/commits/")[1].split("/")[0]
            p = next(p for p in self.prs.values() if p["headRefOid"] == sha)
            rows = p["_statuses"] if path.split("?")[0].endswith("/status") else p["_runs"]
            out = "".join(json.dumps(list(r)) + "\n" for r in rows)
        elif args[:3] == ["gh", "pr", "merge"] and self.after_merge:
            self.after_merge(int(args[3]), self.prs)
        return subprocess.CompletedProcess(args, 0, out, "")

    def merges(self):
        return [c[3] for c in self.calls if c[:3] == ["gh", "pr", "merge"]]


class MergePrs(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.lines = []
        patcher = mock.patch.object(merge_prs, "section_in_progress", return_value=None)
        self.in_progress = patcher.start()
        self.addCleanup(patcher.stop)
        no_wait = mock.patch.object(merge_prs, "SETTLE_WAIT", 0)
        no_wait.start()
        self.addCleanup(no_wait.stop)

    def tearDown(self):
        self.tmp.cleanup()

    def main(self, fake, *argv):
        return merge_prs.main(list(argv), run=fake, root=self.root, out=self.lines.append)

    def test_eligible_pull_requests_merge_one_at_a_time_with_their_checked_commit(self):
        fake = FakeRun([pr(2), pr(1, author="app/dependabot", labels=("dependencies",))])
        self.assertEqual(self.main(fake), 0)
        self.assertEqual(fake.merges(), ["1", "2"])
        merge = next(c for c in fake.calls if c[:3] == ["gh", "pr", "merge"])
        self.assertEqual(merge[4:], ["--rebase", "--match-head-commit", f"{1:040d}"])
        after = fake.calls[fake.calls.index(merge) + 1 :]
        self.assertEqual(after[0][:3], ["git", "pull", "--ff-only"])
        self.assertEqual(after[1][-1], "validate")

    def test_ineligible_pull_requests_are_skipped(self):
        failing = GREEN_RUNS[:-1] + [(GREEN_RUNS[-1][0], "completed", "failure")]
        running = GREEN_RUNS + [("extra", "in_progress", None)]
        all_skipped = [(name, "completed", "skipped") for name, _, _ in GREEN_RUNS]
        fake = FakeRun([pr(1, author="someone"), pr(2, labels=("bug",)), pr(3, mergeable="CONFLICTING"),
                        pr(4, checks=()), pr(5, checks=failing), pr(6, checks=running),
                        pr(7, checks=GREEN_RUNS[1:]),  # a required job never ran
                        pr(8, checks=all_skipped), pr(9, mergeable="UNKNOWN")])
        self.assertEqual(self.main(fake), 0)
        self.assertEqual(fake.merges(), [])
        self.assertEqual(sum("skipped" in line for line in self.lines), 9)

    def test_a_failing_or_pending_commit_status_blocks_the_merge(self):
        blocked = pr(1)
        blocked["_statuses"] = [("ci/legacy", "failure")]
        pending = pr(2)
        pending["_statuses"] = [("ci/legacy", "pending")]
        passing = pr(3)
        passing["_statuses"] = [("ci/legacy", "success")]
        fake = FakeRun([blocked, pending, passing])
        self.assertEqual(self.main(fake), 0)
        self.assertEqual(fake.merges(), ["3"])

    def test_only_pull_requests_into_main_are_considered(self):
        fake = FakeRun([pr(1, base="release/1.0")])
        self.assertEqual(self.main(fake), 0)
        self.assertEqual(fake.merges(), [])
        listing = next(c for c in fake.calls if c[:3] == ["gh", "pr", "list"])
        self.assertEqual(listing[listing.index("--base") + 1], "main")
        self.assertEqual(merge_prs.why_not(pr(2, base="release/1.0"), GREEN_RUNS), "not an open pull request into main")

    def test_each_pull_request_is_judged_as_it_is_right_before_its_merge(self):
        def rerun_failed(number, prs):  # while #1 merges, #2's checks are rerun and fail on the same head
            if number == 1:
                prs[2]["_runs"] = GREEN_RUNS[:-1] + [(GREEN_RUNS[-1][0], "completed", "failure")]

        fake = FakeRun([pr(1), pr(2)], after_merge=rerun_failed)
        self.assertEqual(self.main(fake), 0)
        self.assertEqual(fake.merges(), ["1"])

    def test_excluded_branches_are_left_for_their_section(self):
        fake = FakeRun([pr(1, branch="pins/section-work-1")])
        with mock.patch.object(merge_prs, "EXCLUDED_BRANCHES", ("pins/section-work",)):
            self.assertEqual(self.main(fake), 0)
        self.assertEqual(fake.merges(), [])
        self.assertIn("section work", self.lines[0])

    def test_nothing_runs_unless_safe(self):
        for fake, reason in ((FakeRun([pr(1)], dirty=" M x"), "working tree"),
                             (FakeRun([pr(1)], head="b" * 40), "is not origin/main")):
            self.lines.clear()
            with self.subTest(reason=reason):
                self.assertEqual(self.main(fake), 0)
                self.assertEqual(fake.merges(), [])
                self.assertIn(reason, self.lines[0])
        self.in_progress.return_value = "D06 T05 §11"
        fake = FakeRun([pr(1)])
        self.assertEqual(self.main(fake), 0)
        self.assertEqual(fake.merges(), [])

    def test_the_scheduled_task_stands_aside_while_a_campaign_runs(self):
        (self.root / "build").mkdir()
        (self.root / "build" / "claude-campaign-guard.json").write_text("{}", encoding="utf-8")
        fake = FakeRun([pr(1)])
        self.assertEqual(self.main(fake, "--scheduled"), 0)
        self.assertEqual(fake.calls, [])
        self.assertIn("campaign", self.lines[0])
        self.assertEqual(self.main(fake), 0)  # the loop itself runs it at a section boundary
        self.assertEqual(fake.merges(), ["1"])

    def test_the_scheduled_task_holds_the_guard_lock_for_its_whole_window(self):
        seen = []

        def probe(number, prs):  # during the merge, a campaign's attempt to take the lock must fail
            try:
                with merge_prs.campaign_guard._Lock(str(self.root), wait=0.1):
                    seen.append("free")
            except merge_prs.campaign_guard.GuardError:
                seen.append("held")

        self.assertEqual(self.main(FakeRun([pr(1)], after_merge=probe), "--scheduled"), 0)
        self.assertEqual(seen, ["held"])

    def test_the_first_failure_stops_and_says_what_state_it_left(self):
        fake = FakeRun([pr(1), pr(2)], fail=("merge 1",))
        self.assertEqual(self.main(fake), 1)
        self.assertNotIn("2", fake.merges())
        self.assertIn("still open", self.lines[-1])
        for failing in ("pull", "validate"):
            self.lines.clear()
            fake = FakeRun([pr(1), pr(2)], fail=(failing,))
            with self.subTest(failing=failing):
                self.assertEqual(self.main(fake), 1)
                self.assertEqual(fake.merges(), ["1"])
                self.assertIn("merged on GitHub, but the local follow-up failed", self.lines[-1])

    def test_a_dry_run_merges_nothing(self):
        fake = FakeRun([pr(1)])
        self.assertEqual(self.main(fake, "--dry-run"), 0)
        self.assertEqual(fake.merges(), [])
        self.assertIn("would merge", self.lines[0])

    def test_section_in_progress_means_started_and_ready(self):
        blocked = mock.Mock(ref="D01 T02 §1", done=False, body="> **Started:** x")
        ready = mock.Mock(ref="D06 T05 §11", done=False, body="> **Started:** x")
        untouched = mock.Mock(ref="D06 T05 §12", done=False, body="no start yet")
        stamped = mock.Mock(ref="D06 T05 §10", done=True, body="> **Started:** x")
        rows = [stamped, blocked, untouched, ready]
        disposition = {id(blocked): 4, id(ready): 0, id(untouched): 0, id(stamped): 3}
        with mock.patch.object(merge_prs.todo_graph, "graph", return_value={}), \
                mock.patch.object(merge_prs.todo_graph, "ordered", return_value=rows), \
                mock.patch.object(merge_prs.todo_graph, "disposition", side_effect=lambda r, _: disposition[id(r)]):
            self.assertEqual(SECTION_IN_PROGRESS(self.root), "D06 T05 §11")
            rows.remove(ready)
            self.assertIsNone(SECTION_IN_PROGRESS(self.root))


if __name__ == "__main__":
    unittest.main()
