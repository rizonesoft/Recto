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


def pr(number, *, author="app/github-actions", labels=("dependencies", "pins"), mergeable="MERGEABLE",
       checks=(("COMPLETED", "SUCCESS"),), branch="pins/vcpkg-2026.10.16"):
    return {"number": number, "title": f"pin {number}", "author": {"login": author},
            "labels": [{"name": n} for n in labels], "headRefName": branch, "headRefOid": f"{number:040d}",
            "mergeable": mergeable,
            "statusCheckRollup": [{"__typename": "CheckRun", "status": s, "conclusion": c} for s, c in checks]}


class FakeRun:
    """Answers git and gh the way a test asks, and records every call."""

    def __init__(self, prs, *, dirty="", head=SHA, upstream=SHA, fail=None):
        self.prs, self.dirty, self.head, self.upstream, self.fail = prs, dirty, head, upstream, fail or ()
        self.calls = []

    def __call__(self, args, root):
        self.calls.append(args)
        if any(f in " ".join(args) for f in self.fail):
            return subprocess.CompletedProcess(args, 1, "", "refused")
        answers = {("git", "status"): self.dirty, ("git", "rev-parse", "HEAD"): self.head,
                   ("git", "rev-parse", "origin/main"): self.upstream, ("gh", "pr", "list"): json.dumps(self.prs)}
        out = next((v for k, v in answers.items() if tuple(args[: len(k)]) == k), "")
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
        self.assertTrue(after[1][-1] == "validate")

    def test_ineligible_pull_requests_are_skipped(self):
        fake = FakeRun([pr(1, author="someone"), pr(2, labels=("bug",)), pr(3, mergeable="CONFLICTING"),
                        pr(4, checks=()), pr(5, checks=(("COMPLETED", "FAILURE"),)),
                        pr(6, checks=(("IN_PROGRESS", None),))])
        self.assertEqual(self.main(fake), 0)
        self.assertEqual(fake.merges(), [])
        self.assertEqual(sum("skipped" in line for line in self.lines), 6)

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

    def test_the_first_failure_stops_and_leaves_the_rest(self):
        for failing in ("merge 1", "pull", "validate"):
            fake = FakeRun([pr(1), pr(2)], fail=(failing,))
            with self.subTest(failing=failing):
                self.assertEqual(self.main(fake), 1)
                self.assertNotIn("2", fake.merges())
                self.assertIn("left for you", self.lines[-1])

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
