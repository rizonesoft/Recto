# Fixing a user-reported bug

A bug a user reported is fixed only when a test proves it, and it is closed only when the fix reaches users in a release. This page is the rule every fix for a user-reported issue follows; `scripts/release-notes.py` checks it and writes the closing comments.

## 1. Reproduce first

Get the problem happening on your machine before you change any code. Use the reporter's file if they shared it; otherwise a reproducer made with `recto-cli scrub` (the command arrives with D07 T03 §3) or a synthetic file that shows the same problem (`docs/support/triage.md` explains both). Add the file to `tests/corpus/user-reports/` with a row in `tests/corpus/PROVENANCE.md` naming the issue, where the file came from and that it holds nothing private.

## 2. Write the test, and watch it fail

Write a regression test that fails because of the bug: a gtest case (`Suite.Case`) for app and engine code, or a Python test (`module.Class.test_name`, a plain test method of a `unittest.TestCase` class, decorated if at all only by the repository's own code or `unittest.mock`) for tooling. A new case in an existing test file is fine. A gtest regression test counts when the CI build of the fix commit lists it: every push to `main` records `recto-tests.exe --gtest_list_tests` as a git note on the commit (`refs/notes/gtest-list`), so land the test with the fix, not after it. Run it on the parent commit, before your fix, and keep the failing output: the pull request quotes it. `scripts/release-notes.py` checks that the named test exists at the fix commit and is a test unittest or gtest will actually run (not skipped, disabled or expected to fail); it cannot prove the test exercises the bug, and a test written to defeat the check is a code review matter, which is why the pull request shows it failing on the parent and passing on the fix.

## 3. Fix it, with both trailers

Fix the bug at the layer that owns it, then run the test again and keep the passing output. The fix commit carries both trailers at the end of its message:

```text
pdf: accept an empty cross-reference table

Fixes-User: #123
Regression-Test: PdfXref.EmptyTable
```

Name every test the fix relies on, one `Regression-Test:` line each; each one must exist at the fix commit. A fix that truly cannot have a test (wording in the documentation, for example) has no `Regression-Test:` line and is excused when the release notes are written with `--allow-no-test <sha>=<reason>`; the reason is printed next to the fix.

## 4. Label it `fixed-on-main`, and leave it open

When the fix merges, label the issue `fixed-on-main` and comment with the commit. Do not close it: users on the current release still have the bug.

## 5. Close it when the release ships

When the release containing the fix is published, run:

```powershell
python scripts/release-notes.py <previous tag> <new tag> --issues issues.json
```

with `issues.json` from `gh issue list --state all --limit 1000 --json number,title,labels`. A gtest id must be in its fix commit's CI test list (the script fetches `refs/notes/gtest-list` from GitHub); a Python id must be a unittest test at its fix commit. It lists the release's user fixes by kind label, prints a "Fixed in vX.Y.Z" comment for each issue, and exits 1 if any fix lacks an existing regression test. Post each comment, change the label from `fixed-on-main` to `released`, and close the issue.
