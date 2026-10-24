---
schema_version: 1
id: listening-to-users
domain: 07-release
status: active
title: "TODO-03 -- Listening to Users: Intake, Triage, Diagnostics, Releases and Demand"
---

# TODO-03 -- Listening to Users: Intake, Triage, Diagnostics, Releases and Demand

> **Goal:** Recto hears its users and answers them in the open. A report arrives through one GitHub form with the version, install type, OS and steps it needs, prefilled from the app's own diagnostics; a user with a private file can share a structure-preserving reproducer after inspecting it; every user-reported bug gets a regression test before it closes; an issue closes when the release containing its fix ships; security reports have a private channel and an advisory; and what users ask for is recorded in a public demand ledger that is reviewed before every release.

> **Current state:** Measured 2026-10-21. `.github/` holds `pull_request_template.md` and CI workflows only: there are no issue forms, no `SECURITY.md`, no triage or release-closing policy, no reproducer tooling, no demand ledger and no in-app path from a problem to a report. `D07 T02 §4` plans a local crash bundle and `D07 T02 §6` a release runbook; neither covers non-crash reports or closing issues on release. 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); UI references rewritten against `todo/10-ui/`.

> **Groomed 2026-10-23:** re-measured. `§1`, `§4`, `§6`, `§8` and `§9` are stamped: `.github/ISSUE_TEMPLATE/` holds `bug_report.yml`, `feature_request.yml` and `config.yml`, `.github/labels.yml`, `SECURITY.md`, `docs/scope.md`, `docs/user-demand.md` and `docs/support/` exist. `scripts/release-gate.py`, `recto-cli scrub`, the in-app `Report a problem` dialog and the `What's new` page do not exist yet (`§2`, `§3`, `§5`, `§7`). `D06 T03 §12` runs by script, `D99 T01 §11` (operator) runs after `§5` passes and is not a gate, and the operator's one-time repository settings this file relies on (private vulnerability reporting, labels) are `D99 T01 §2`. Version 1 ships unsigned, so the release notes that `§4` maps carry the SmartScreen sentence of `D07 T02 §6`.

## Inputs

- Foundation plan D16 (local diagnostics and zero network telemetry) and section 9 (hostile input); AGENTS.md "Tests prove claims" and "Fix at the owning layer".
- -> XREF: D07 T02 §4 -- the opt-in local crash bundle that §2 here reuses for its diagnostics text, and that links to §1's issue form.
- -> XREF: D07 T02 §6 -- the GitHub release runbook that §4 and §5 here extend with closing-on-release and the release gate.
- -> XREF: D02 T03 §11 -- `CommandRegistry`, through which §2's `Report a problem` and §7's `What's new` are reachable from `Ctrl+K`.
- -> XREF: D06 T03 §12 -- the installed-build release smoke (run by script on the build host) whose report §5's gate requires.
- -> XREF: D99 T01 §11 -- the operator's v1 release checklist runs after this gate passes and is not itself a gate.
- -> XREF: D99 T01 §2 -- the operator enables GitHub private vulnerability reporting that §1's `SECURITY.md` points to.
- -> XREF: D08 T01 §9 -- diagnostics must never carry an assistant key (D08 T01 §9 scans for it).
- -> XREF: D04 T07 §2 -- records the Action Wizard's watched-folder request (D04 T07 §2).
- -> XREF: D07 T02 §10 -- update notice policy.
- -> XREF: D06 T04 §25 -- the `D-Lifecycle.png` board and `design/SPEC.md` section 9.19 that §2, §7 here build against.
- -> XREF: D10 T02 §17 -- the `ui::Dialog` that `ReportProblemDialog` (§2) builds on.
- -> XREF: D10 T02 §20 -- the `ui::InfoBar` that the `Update available` notice (§7) builds on.
- -> XREF: D07 T02 §18 -- the version derivation, `docs/versioning.md` and `scripts/check-release-version.py` that §5's gate runs and §10 and §11 read tags through.
- -> XREF: D07 T02 §19 -- the `recto::core::SemVer` comparison §7's update notice and `What's new` use.
- -> XREF: D07 T02 §21 -- the `recto-cli --version --json` key set that §11 snapshots as a contract.
- -> XREF: D07 T01 §26 -- the writer version field that §11's settings, recents and session snapshots include.

## Outcome

- A new GitHub issue is either a bug form (version, install type, Windows build, GPU, steps, expected and actual, file or reproducer) or a feature form that starts with a duplicate search; `SECURITY.md` routes vulnerabilities to private reporting.
- `Help -> Report a problem` (and `Ctrl+K`) copies diagnostics with no document content and opens the bug form prefilled; `recto-cli scrub` turns a private PDF into a structure-preserving reproducer, reports what it kept, and renders preview pages the user inspects before sharing.
- A user-reported bug closes only with a regression test that fails before the fix, and only when the release containing the fix is published, with "Fixed in vX.Y.Z".
- No release is tagged while a closed-on-main data-loss, crash or security fix is unreleased past seven days, and each security fix ships with an advisory.
- Each release's `CHANGELOG.md` block (Keep a Changelog 1.1.0) and GitHub Release body are generated from the sections stamped since the last tag and the `Fixes-User:` trailers, with no new commit rule, and `scripts/next-version.py` suggests the next SemVer from checked contract snapshots and what shipped (`§10`, `§11`).
- `docs/scope.md` says what Recto does and does not do, and `docs/user-demand.md` maps every recurring request to a TODO section or a recorded decision, reviewed before each release.

**Adjacency:** list=applicable @ D07 T03 §6; document=applicable @ D07 T03 §3; settings=applicable @ D07 T03 §7; reporting=applicable @ D07 T03 §6; notifications=applicable @ D07 T03 §7; permissions=applicable @ D07 T03 §1; audit=applicable @ D07 T03 §4; exchange=applicable @ D07 T03 §2; reverse=not-applicable (an issue or report is reversed on GitHub by closing or editing it; Recto keeps no local record to undo)

**Adjacency rationale:** The demand ledger (§6) is the list and the release-review report. The reproducer a user carries to the tracker is §3's scrubbed PDF. The only setting is §7's opt-in update notice, which is also the only notification. Private security reporting (§1) is the permission boundary: who may see a vulnerability before its fix. The changelog mapping issues to releases (§4) is the audit trail. Diagnostics copied to an issue (§2) is the exchange.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Issue forms, triage policy and `SECURITY.md` | -- | [x] |
| 2 | §9 | Gtest regression evidence from the fix commit's own CI test list | §1 | [x] |
| 3 | §4 | User-bug regression rule, closing on release and changelog mapping | §1, §9 | [x] |
| 4 | §6 | Public scope page and user demand ledger | §1 | [x] |
| 5 | §3 | Structure-preserving reproducer scrubber (`recto-cli scrub`) | D04 T02 §2 | [ ] |
| 6 | §2 | In-app `Report a problem` with privacy-safe diagnostics | §1, D07 T02 §1, D07 T02 §4, D02 T03 §11, D06 T04 §25, D10 T02 §17 | [ ] |
| 7 | §5 | Release gate for unreleased fixes, security advisories and CI integrity | §4, §10, §11, D07 T02 §6, D07 T02 §18, D07 T02 §7, D06 T03 §12, D06 T03 §5, D06 T03 §6 | [ ] |
| 8 | §7 | What's new after update and opt-in update notice | §2, §10, D07 T02 §19, D02 T04 §6, D06 T04 §25, D10 T02 §20 | [ ] |
| 9 | §8 | Strict issue-form YAML reader and top-level schema types | §1 | [x] |
| 10 | §10 | Changelog and release body from stamped sections and `Fixes-User:` trailers | §4, D07 T02 §18 | [ ] |
| 11 | §11 | Contract snapshots and `scripts/next-version.py` SemVer bump suggestion | §10, D07 T02 §18, D07 T02 §21, D04 T08 §2, D04 T08 §5, D07 T02 §8, D02 T04 §2, D07 T01 §22, D07 T01 §26, D02 T01 §13, D02 T03 §11 | [ ] |
| 12 | §12 | Offline decoder diagnostic reference: steps, failure classes and next actions | D01 T02 §25 | [ ] |

## 1. Issue forms, triage policy and `SECURITY.md`

> **Started:** 2026-10-21T09:40:00Z

Common user-handling failures are no issue template, reports relayed to GitHub where reporters cannot read replies, a bot that splits reports and closes real requests as support questions, and private vulnerability reporting left off while researchers wait. This section writes Recto's intake files and the triage rules people follow. Turning on GitHub's private vulnerability reporting is an operator setting owned by `D99 T01 §2`.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Author `.github/ISSUE_TEMPLATE/bug_report.yml`, `feature_request.yml` and `config.yml`, plus `SECURITY.md` at the repository root. Done when: `python -m unittest tests.scripts.test_issue_forms` parses each form and finds every required field.
2. Author `docs/support/triage.md` with labels, response targets and the closing rules. Done when: the labels it names exist in `.github/labels.yml` and the test checks the file lists every label.

- [x] Author `.github/ISSUE_TEMPLATE/bug_report.yml` with required fields: Recto version (from `Help -> About`), install type (`Installer`, `Portable`, `MSIX`), Windows build, GPU and driver, steps, expected, actual, and a file or `recto-cli scrub` reproducer, plus an optional diagnostics paste from §2. Install type is an `input` naming the three values, not a `dropdown`, because GitHub's URL query prefill only fills text fields and §2 prefills `version`, `install-type` and `windows-build` (default taken 2026-10-21; switching to a `dropdown` costs §2 that prefill). Done when: the form renders on GitHub and every required field has `validations: required: true`. Cheaper substitute: a Markdown template whose placeholders reporters leave unfilled.
- [x] Author `.github/ISSUE_TEMPLATE/feature_request.yml` that opens with a link to search existing issues and `docs/user-demand.md`, then asks for the task the user is trying to do and the app they use today. Done when: the form renders and the search link targets this repository's open issues.
- [x] Author `.github/ISSUE_TEMPLATE/config.yml` disabling blank issues, with no contact link of its own for vulnerabilities. Done when: GitHub's new-issue page shows only the two forms and the security link. **Corrected 2026-10-21:** said "and linking `SECURITY.md` for vulnerabilities"; GitHub's chooser already adds a "Report a security vulnerability" entry from `SECURITY.md`, so a `contact_links` entry showed it twice (seen on the live chooser at `dd0b87d`).
- [x] Author `SECURITY.md` naming supported versions, the private reporting route (GitHub "Report a vulnerability"), a first-response target of 3 working days, and the promise of an advisory with every security release. Done when: the file exists and `test_issue_forms` checks those four items.
- [x] Author `.github/labels.yml` with area labels (`engine`, `viewer`, `text`, `convert`, `organize`, `print`, `install`), kind labels (`bug`, `feature`, `regression`, `data-loss`, `crash`, `security`) and state labels (`needs-repro`, `fixed-on-main`, `released`). Done when: the file parses and every label has a description.
- [x] Author `docs/support/triage.md`: a person, not a bot, closes an issue; a duplicate close links the canonical issue and states its status; a feature request is never closed as "support"; `needs-repro` offers `recto-cli scrub` and a synthetic-file guide; one owner per issue, and a superseded pull request is closed with a link. Done when: the file covers each rule and `test_issue_forms` checks the headings.
- [x] Add `tests/scripts/test_issue_forms.py` validating the YAML forms, required fields, labels and the security file. Done when: removing a required field makes the test fail.
- [x] Commit: `repo: add issue forms, labels, triage policy and SECURITY.md`

**Test checkpoint:** `python -m unittest tests.scripts.test_issue_forms` passes, and deleting the `validations` block from the version field makes it fail.

> **Verified:** 2026-10-21 | §1 | 8449427b06e5045650c9dd9b4964e866a42b7a1b
> **Review:** `docs/reviews/d07-t03-s1.json`

## 2. In-app `Report a problem` with privacy-safe diagnostics

Reporters often cannot say which version, install type or GPU they had, and maintainers spend their replies asking. Recto offers `Help -> Report a problem`: it builds a diagnostics block with no document content, copies it, and opens the bug form with the version, install type and Windows build prefilled through GitHub's form URL parameters. Nothing is sent by Recto itself; the user's browser opens the form.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface: a small `ui::Dialog` (`D10 T02 §17`) on the `design/SPEC.md` section 9.9 dialog chrome; no screenshot exists. Design board: `design/SPEC.md` section 9.19 with `design/screenshots/D-Lifecycle.png` (drawn by `D06 T04 §25`).
**Job:** A user who hit a problem can copy accurate diagnostics and reach a prefilled GitHub form in two clicks. Consumer: the `bug_report.yml` form (§1) and the maintainer reading it.
**Treatment:** One dialog listing exactly what will be copied, with `Copy diagnostics` and `Open bug form` buttons and a link to `recto-cli scrub` help. Cheaper substitute that fails the checkpoint: a raw log file the user must find in `%LOCALAPPDATA%`.
**Chrome:** The shared dialog chrome (`D02 T01 §10`) on `ui::Dialog` (`D10 T02 §17`), `Recto*Brush` tokens (`D10 T01 §8`), Fluent UI System Icons (`RectoIcons`: `question_circle`).

**Build order:**

1. Add `src/services/diagnostics_report.h/.cpp` building the diagnostics text from the version provenance (`D07 T02 §1`), install type, Windows build, GPU adapter and driver, active settings that affect rendering, and the last 50 diagnostic ring entries with document text and paths stripped. Done when: `Diagnostics.ReportHasNoDocumentContent` passes on a session that opened a fixture with known text.
2. Add `ReportProblemDialog` in `src/app/dialogs/` and the `help.reportProblem` command. Done when: the dialog opens from `Help` and from `Ctrl+K`, copies the text, and launches the prefilled form URL.

- [ ] Author `src/services/diagnostics_report.h/.cpp` composing version, commit, build flavour, install type, Windows build, GPU and driver, DPI and display count, renderer mode (hardware or WARP) and the redacted ring tail. Done when: the output contains each field and no file path or page text.
- [ ] Strip document text, file names, folder paths, form values and passwords from ring entries with an allow-list of fields, never a block-list. Done when: `Diagnostics.ReportHasNoDocumentContent` finds none of the fixture's strings.
- [ ] Build `src/app/dialogs/ReportProblemDialog.h/.cpp` showing the exact text that will be copied, `Copy diagnostics`, `Open bug form` and a `How to share a file safely` link. Done when: the dialog renders in Light, Dark and HighContrast with token brushes only. Cheaper substitute: copying silently without showing the user what leaves their machine.
- [ ] Build the GitHub form URL with `template=bug_report.yml` and prefilled `version`, `install-type` and `windows-build` query fields, opened through the shared ask-before-open link path. Done when: the opened URL prefills those three fields on GitHub.
- [ ] Register `help.reportProblem` in `CommandRegistry` with synonyms "bug", "report", "feedback" and add it to the Classic `Help` menu. Done when: each synonym finds the command in `Ctrl+K`.
- [ ] Offer `Report a problem` from the `D07 T02 §4` crash bundle prompt and the `D06 T03 §3` page notice. Done when: both surfaces open this dialog.
- [ ] Include each ring entry's code, diagnostic and refusal reason names with its observed value, bound and one-based page (`D01 T02 §23`, `§24`), numbers only. **Corrected 2026-10-23:** (`D01 T02 §24`: the page was not named). Done when: `Diagnostics.ReportCarriesRefusalContext` records a `ResourceLimit` refusal with a path in its message and finds the reason and numbers in the report and the path nowhere, and records a `ConversionFailed` `TextRecall` refusal on zero-based page `2` and a page-less `PackageInvalid` refusal and finds `page 3` for the first and no page for the second. **Corrected 2026-10-23:** (`D01 T02 §24` plan review: the page was promised but not proved). **Corrected 2026-10-23:** (`D01 T02 §22` completion review).
- [ ] Include each ring entry's recovery context (`recovered`, `first`, `tile`, `component` from `D01 T02 §26`) as numbers, omitting absent fields as `DiagnosticEntry::format()` does. Done when: `Diagnostics.ReportCarriesRefusalContext` records a `RecoveredJpxCodeBlocks` entry with count 2, first 1, tile 0 and component 0 and a `RecoveredDamagedRows` entry with count 2 and first 1, and finds exactly those numbers in the report. **Corrected 2026-10-24:** (`D01 T02 §26` completion review gap 5: filed here).
- [ ] Commit: `app: add Report a problem with privacy-safe diagnostics and a prefilled bug form`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Diagnostics.ReportHasNoDocumentContent:Diagnostics.ReportCarriesRefusalContext:Shell.ReportProblemDialog` passes on the display-session host.

## 3. Structure-preserving reproducer scrubber (`recto-cli scrub`)

Maintainers often ask for files that users cannot share (identity documents, legal and company files), and reports die in `needs info`. `recto-cli scrub` writes a copy that keeps the object graph, filters, content-stream operator structure and page geometry that trigger a bug, while replacing text bytes, image samples, metadata and attachments with neutral placeholders of the same shape. Channels a user cannot check by looking at the pages are removed by default: unreachable objects are dropped, font programs are stripped, and unknown or private streams, JavaScript, XFA data and attachment data are zeroed to the same length; each can be kept only by an explicit flag that the report names. What stays visible (a name drawn as vector outlines, a signature drawn as paths) is the user's to check: the tool renders the scrubbed pages for inspection and never calls its output safe. It always writes a new file; the original is never touched.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/core/scrub.h/.cpp` walking every object through `GraftMap`-style copying into a new `PdfWriter` document, replacing string bytes with same-length placeholder bytes, image samples with a flat mid-grey of the same dimensions, bit depth and filter, and `/Info`, XMP, attachments and annotation contents with placeholders. Done when: `Scrub.KeepsStructure` passes on the corpus.
2. Add `recto-cli scrub <in.pdf> <out.pdf> [--keep-fonts] [--keep-unknown] [--blank-paths] [--json]` printing what was replaced and what was kept, and writing `<out>.preview/page-N.png` for every page. Done when: the command writes the copy through `AtomicFileWriter`, refuses when the output path equals the input, and the preview pages exist.

- [ ] Author `src/core/scrub.h/.cpp` copying every indirect object, keeping object numbers, stream filters, dictionary keys and array lengths. Done when: `Scrub.KeepsStructure` compares a structural trace per page (operator names, operand counts and operand types) and finds it identical in every mode, compares every non-string operand as well when `--blank-paths` is off, and with `--blank-paths` excludes only the operands of path-construction operators (`m`, `l`, `c`, `v`, `y`, `h`, `re`); string operands are always excluded because the scrubber replaces them by design.
- [ ] Replace text-showing string operands with same-length bytes that map through the same font to visible placeholder glyphs where the font has them. Done when: extraction of the scrubbed copy contains none of the original words.
- [ ] Replace image samples with a constant mid-grey of identical width, height, bit depth, colour space and filter chain, re-encoded with the original filter. Done when: decoded samples are constant and dimensions match.
- [ ] Replace `/Info`, XMP, attachments, annotation `/Contents`, form values, outline titles and named-destination names with placeholders of the same length class. Done when: a raw object scan finds none of the original strings.
- [ ] Write only objects reachable from the trailer, strip embedded font programs unless `--keep-fonts` is given (a subset font reveals which characters the document used), and zero to the same length every stream the scrubber does not understand (private or unknown dictionaries, JavaScript, XFA packets, embedded-file data) unless `--keep-unknown` is given; report every kept channel in the JSON and console output with path-drawing operator counts per page. Done when: a fixture with an unreachable private stream, an unknown stream holding a planted secret and an embedded font yields a copy where a raw scan finds none of them by default, and `--keep-fonts` and `--keep-unknown` each appear in the report when used.
- [ ] Offer `--blank-paths`, which replaces path coordinates inside each content stream with a degenerate rectangle at the path's bounding box while keeping operator counts, for files whose private content is drawn as outlines (a signature or an outlined name). Done when: a fixture name drawn as outlines is unreadable in the preview with `--blank-paths` and its operator counts are unchanged.
- [ ] Render every scrubbed page to `<out>.preview/page-N.png` with Recto's renderer so the user inspects exactly what will be shared. Done when: the preview folder holds one image per page.
- [ ] Refuse an encrypted input without its password, and write the copy unencrypted with a notice. Done when: an encrypted fixture without a password returns `NeedsPassword`.
- [ ] Commit: `cli: add recto-cli scrub to share structure-preserving reproducers safely`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Scrub.*:Cli.ScrubCommand` passes under ASan: the scrubbed corpus keeps object counts and structural traces, a raw scan finds no original text, metadata or image samples, and the outlined-name fixture is reported as kept path content without `--blank-paths`, and the planted secret in an unknown stream is absent by default.

## 4. User-bug regression rule, closing on release and changelog mapping

> **Started:** 2026-10-21T10:05:00Z

A common failure is closing issues when a fix merges while users on the release keep hitting them, duplicates piling up, and fixes shipping without tests. Recto's rule: a user-reported bug closes with a regression test that fails before the fix, the fix commit carries `Fixes-User: #N` and `Regression-Test: <test id>` trailers, the pull request quotes the test failing on the parent commit and passing on the fix, the issue is labelled `fixed-on-main`, and it closes only when the release that contains the commit is published, with a "Fixed in vX.Y.Z" comment.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Author `scripts/release-notes.py` listing every `Fixes-User:` trailer between two tags, grouped by label, and printing the closing comments. Done when: `python -m unittest tests.scripts.test_release_notes` passes on a fixture repository.
2. Document the rule in `docs/support/user-bugs.md`, link it from `AGENTS.md` and the pull request template. Done when: the template asks for the trailer and the regression test name.

- [x] Author `scripts/release-notes.py <from-tag> <to-tag> [--json] [--issues <file>]` reading commit trailers with `git log --format=%(trailers)`, and grouping issues by their kind label from `--issues` (the JSON of `gh issue list --state all --json number,title,labels`), `unlabelled` without it. Done when: a fixture repository with three trailers yields three entries. **Corrected 2026-10-21:** named no source for the labels it groups by; labels live on GitHub issues, not in commits, so they are read from a `gh` export and the script stays offline and testable.
- [x] Print, per issue, the closing comment "Fixed in vX.Y.Z (commit, release link)". Done when: the output matches the fixture's expected text.
- [x] Refuse a `Fixes-User:` commit without a `Regression-Test:` trailer, or whose named test (a gtest `Suite.Case` or a Python `module.Class.test_name`) does not exist at that commit, with `--allow-no-test <sha>=<reason>` (one per excused commit) recorded in the output for documentation-only fixes; whether the file is new or existing does not matter. Done when: a fixture adding a real case to an existing test file passes and a fixture naming a missing test is reported as a violation. **Corrected 2026-10-21:** said `--allow-no-test <reason>`, a single flag that would excuse every commit in the range at once. **Corrected 2026-10-21:** "exists at that commit" was checked by scanning source text, which review showed comments, macros, `#if (0)` and look-alike classes defeat; a Python id must now be a plain test method of a `unittest.TestCase` class in a worktree of the fix commit, built the way unittest's loader builds it (`cls(method)`, never run), and a gtest id must be declared in source at the fix commit and appear in `--gtest-list` (the release build's `--gtest_list_tests` output), without which it is reported unverified. **Corrected 2026-10-21:** the check guards against mistakes (a missing, skipped, disabled, expected-to-fail or outside test, a wrong id), not a committer deliberately defeating unittest from the test's own code, which no check that loads that code can rule out; that boundary is code review and the pull request's failing-on-parent, passing-on-fix evidence (`docs/support/user-bugs.md`). **Corrected 2026-10-21:** a gtest id counts as declared only when a plain `TEST`, `TEST_F`, `TEST_P`, `TYPED_TEST` or `TYPED_TEST_P` names it in compiled code at the start of a declaration (after the start of the file, `;`, `{` or `}`) and outside any parentheses (depth tracked per `#if` alternative); review round 18 showed that macro arguments and wrapper macros need a preprocessor to judge, so regression tests are declared plainly (`docs/support/user-bugs.md`). **Corrected 2026-10-21:** the review reached its 22-round cap with the same class still open (a macro-defined `#if` condition), so gtest evidence moves to the fix commit's own CI test list in -> XREF: D07 T03 §9, which replaces the source scan; this item stamps with it.
- [x] Author `docs/support/user-bugs.md`: reproduce first (scrubbed or synthetic file into `tests/corpus/user-reports/` with a provenance row), test fails before the fix, trailer on the fix, label `fixed-on-main`, close on release. Done when: the file states each step.
- [x] Add both trailers and a "Regression test: failing on parent, passing on fix" evidence block to `.github/pull_request_template.md`, and a pointer from `AGENTS.md` "Working rules". Done when: both files reference `docs/support/user-bugs.md`.
- [x] Add `tests/scripts/test_release_notes.py` building a temporary git repository with tagged commits. Done when: the test passes and removing the trailer parsing makes it fail.
- [x] Commit: `repo: map user-reported fixes to releases and require a regression test per fix`

**Test checkpoint:** `python -m unittest tests.scripts.test_release_notes` passes, and a fixture `Fixes-User:` commit whose `Regression-Test:` names a test that does not exist is reported as a violation.

> **Verified:** 2026-10-21 | §4 | 4a3506a5c3d57f0a0483214e7c46858478073f07
> **Review:** `docs/reviews/d07-t03-s4.json`

## 5. Release gate for unreleased fixes, security advisories and CI integrity

Another is security and data-loss fixes sitting on main while the current release stays vulnerable with no advisory, and CI red or cancelled on main so regressions ship unseen. `scripts/release-gate.py` blocks tagging when the release would leave a `fixed-on-main` crash, data-loss or security fix unreleased for more than seven days without a patch release, when a security fix has no advisory draft, when the last main CI run was cancelled or red, or when the `D06 T03 §12` smoke report is missing.

> **Groomed 2026-10-22:** `D06 T03 §12` now produces its report by script on the build host (no `attended`), so this gate still depends on it and no gate depends on a person. The clean-machine run, the FIPS host proof and the other operator proofs are the checklist `D99 T01 §11` (Phase 99, outside the phases 0 to 6 rule below) (owner decision 2026-10-22 (foundation plan section 1)).

> **Groomed 2026-10-23:** the gate also enforces the versioning scheme (owner decision 2026-10-23, `docs/versioning.md`): the candidate's tag must pass `scripts/check-release-version.py` (`D07 T02 §18`, which runs `scripts/next-version.py --check` from `§11`), and `CHANGELOG.md` must hold the generated block for the version (`§10`).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `scripts/release-gate.py` reading issues through `gh issue list --json` and runs through `gh run list --json`, plus the smoke report path. Done when: `python -m unittest tests.scripts.test_release_gate` passes on recorded JSON fixtures.
2. Add the gate as a required step in `docs/release-and-store-runbook.md` (`D07 T02 §6`) and as a manual workflow `.github/workflows/release-gate.yml`. Done when: the runbook names the command and the workflow runs it.

- [ ] Fail when an issue labelled `fixed-on-main` and `crash`, `data-loss` or `security` has been fixed for more than seven days and its fix commit is not an ancestor of the candidate commit (`git merge-base --is-ancestor`), so the patch release that ships an overdue fix always passes. Done when: an eight-day-old fix outside the candidate fails the gate and the same fix inside the candidate passes.
- [ ] Fail when a release includes a `security` fix and no draft GitHub security advisory names the affected versions. Done when: the fixture without an advisory fails.
- [ ] Require, for the candidate commit SHA itself, a `success` conclusion for every job and matrix combination named in `.github/release-required-jobs.txt` (workflow, job name and matrix values: build for x64 and ARM64 in Debug and Release, tests, the standard-user leg, plan gates), read with `gh run view <id> --json jobs`; a job that is missing, `skipped`, `cancelled` or `failure`, or a run for another SHA, fails the gate, because a workflow can report success while a conditionally skipped job did not run. Done when: fixtures with the ARM64 job skipped, a cancelled candidate run and a green run for a different SHA each fail the gate, and the full-matrix fixture passes.
- [ ] Add a standard-user (non-administrator) test leg to `.github/workflows/build.yml` so privilege-dependent tests cannot pass only as admin. Done when: the workflow defines the leg and the gate requires its result.
- [ ] Fail when `build/release-smoke/<version>.json` from `D06 T03 §12` is missing, not green, names a commit other than the candidate, or lists an artifact digest that differs from the bundle's `SHA256SUMS.txt` (`D07 T02 §7`). Done when: the missing-report, wrong-commit and rebuilt-artifact fixtures each fail.
- [ ] Fail when any section in phases 0 to 6 other than this gate is unstamped, read from `python scripts/todo-graph.py query plan`, so no v1 feature or quality suite (save-integrity `D06 T03 §5` and raw-byte redaction `D06 T03 §6` among them, which this section also depends on directly) is left behind the tag. Done when: a fixture with one open phase 4 section fails the gate and the all-verified fixture passes.
- [ ] Fail when `python scripts/check-release-version.py` (`D07 T02 §18`) exits non-zero for the candidate (a tag that disagrees with `VERSION`, a lightweight tag, a dirty tree, a disallowed pre-release label, or a version below `scripts/next-version.py`'s suggestion, `§11`), or when `python scripts/changelog.py <previous tag> <candidate tag> --check CHANGELOG.md --version <version>` (`§10`) exits `1`. Done when: fixtures with a mismatched tag, an understated bump and a stale changelog block each fail the gate, and the consistent fixture passes.
- [ ] Add `tests/scripts/test_release_gate.py` with JSON fixtures for each rule. Done when: each rule has a passing and a failing fixture.
- [ ] Commit: `release: gate tags on unreleased critical fixes, advisories, green CI and the smoke report`

**Test checkpoint:** `python -m unittest tests.scripts.test_release_gate` passes, and each rule's failing fixture makes `python scripts/release-gate.py --fixture <file>` exit 1.

## 6. Public scope page and user demand ledger

> **Started:** 2026-10-21T11:40:00Z

Users file bugs for features that exist but are hidden, ask scope questions nobody answers, and cannot tell shipped from planned. Recto publishes `docs/scope.md` (what it does, what it never does and why: no PDF JavaScript, no network by default, Windows only, how XFA is handled) and `docs/user-demand.md`, a ledger that maps each recurring request to a TODO section, a recorded decision, or an explicit "not planned" with its reason. The ledger is seeded from Recto's own GitHub issues, `docs/acrobat-parity.md` and the competitor parity rows, and reviewed before every release.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Author `docs/scope.md` and `docs/user-demand.md`, seeding the ledger from Recto's own GitHub issues, `docs/acrobat-parity.md` and the competitor parity rows. Done when: every ledger row names a TODO reference, a decision, or "not planned" with a reason.
2. Add `scripts/check-user-demand.py` validating that each TODO reference in the ledger resolves through `scripts/todo-graph.py resolve`. Done when: `python -m unittest tests.scripts.test_user_demand` passes and a bogus reference fails it.

- [x] Author `docs/scope.md`: supported Windows versions and architectures, what Recto never does (run PDF JavaScript, contact the network without consent, collect telemetry), XFA and dynamic-form handling, and where to ask. Done when: each topic has a heading and the file is linked from `README.md`.
- [x] Author `docs/user-demand.md` with columns Request, Evidence (Recto issue numbers and parity rows), Status (`planned`, `decided`, `not planned`), Owner (TODO reference or decision) and Last reviewed. Done when: every open Recto issue labelled as a request, and every `docs/acrobat-parity.md` row without a TODO owner, has a row. **Corrected 2026-10-21:** a sixth column, Cost of reversing, holds each decision's reversal cost, which the next item requires; the two open issues are pin-automation alerts, not requests, so the seed comes from the parity ledger.
- [x] Record the requests no section owns yet as `decided` or `not planned` with the decision and its cost of reversing, never as silence (tab tear-off and split view, mobile apps, AI features). Done when: no row has an empty Owner.
- [x] Author `scripts/check-user-demand.py` resolving every TODO reference and failing on an unknown one or a row older than the last release's review date. Done when: the script exits 0 on the seeded ledger.
- [x] Add `tests/scripts/test_user_demand.py` with a valid and an invalid ledger fixture. Done when: the invalid fixture fails.
- [x] Add the ledger review to the release runbook checklist in `docs/release-and-store-runbook.md` once `D07 T02 §6` creates it, and to `docs/support/triage.md` now. Done when: `docs/support/triage.md` names the review.
- [x] Commit: `docs: publish Recto's scope and a user demand ledger seeded from Recto issues and parity rows`

**Test checkpoint:** `python -m unittest tests.scripts.test_user_demand` passes and `python scripts/check-user-demand.py` exits 0 on the committed ledger.

> **Verified:** 2026-10-21 | §6 | 30f5cb144c0a3bf65e6fcc83402d7e419cff65fc
> **Review:** `docs/reviews/d07-t03-s6.json`

## 7. What's new after update and opt-in update notice

Users stay on old builds and report bugs already fixed, and cannot find features that have shipped. Recto shows a `What's new` page once after each version change, built from §4's release notes embedded at build time (no network), and offers an opt-in update notice that checks GitHub Releases at most once a day only when the user turns it on. Because the notice is outbound network access, the owner decided on 2026-10-21 (foundation plan, owner decisions after the decision table): it ships in v1, off by default, and checks at most once a day while on.

> **Groomed 2026-10-23:** versions are compared as SemVer 2.0.0 through `recto::core::SemVer` (`D07 T02 §19`), never as strings, and the comparison follows the build's channel (owner decision 2026-10-23): a `Release` install is offered only newer releases and never a pre-release; a `Preview` install is offered the newest newer release or pre-release; a `Dev` build is offered only a release newer than itself. A tag that does not parse, a draft and an equal version are ignored. `What's new` now embeds the changelog entries of `§10` (which include `§4`'s release notes) and appears once per newer `Release` or `Preview` version, never for a `Dev` build, whose version changes with every commit.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.8 (About) for placement; the `What's new` page is a genuinely new surface on the same chrome, with no screenshot. Design board: `design/SPEC.md` section 9.19 with `design/screenshots/D-Lifecycle.png` (drawn by `D06 T04 §25`).
**Job:** A user who just upgraded sees what changed and which of their reported issues are fixed; a user who opted in learns a newer version exists. Consumer: the user, and the settings store that records the last seen version and the opt-in.
**Treatment:** A `What's new` page in the About area listing release-note entries, and a dismissible `ui::InfoBar` titled `Update available` (naming the new version) linking to the release page. Cheaper substitute that fails the checkpoint: silent auto-install, or a check that runs without consent.
**Chrome:** `ui::InfoBar` (`D10 T02 §20`) and the About page layout from `D07 T02 §2`; the `Check for updates` toggle and `Check now` in the `Language and updates` group (SPEC 9.7), shared with the `Language` row of `D07 T02 §14` and placed by `kSettingsGroupOrder` (`D02 T04 §6`); `Recto*Brush` tokens only (`D10 T01 §8`).

**Build order:**

1. Embed the changelog entries for the current version at build time (`build/generated/whats_new.json` from `scripts/changelog.py --json`, `§10`) and show the `What's new` page once when the running `Release` or `Preview` version is newer by SemVer precedence than the last-seen version `SettingsStore` records. Done when: `Shell.WhatsNewOncePerVersion` passes.
2. Add `Check for updates` (off by default, owner decision 2026-10-21) that queries the GitHub Releases API at most once a day and shows a `ui::InfoBar` linking to the release; it never downloads or installs. Done when: `Shell.UpdateNoticeOptIn` passes and no request is made while the setting is off.

- [ ] Confirm the owner decision recorded 2026-10-21 in the foundation plan (v1, off by default, at most once a day) and use it as the `SettingsStore` default. Done when: the decision line exists in the plan and a fresh settings file reads `updates.check_enabled = false`.
- [ ] Generate `build/generated/whats_new.json` from `scripts/changelog.py --json` (`§10`) during the build and embed it as a resource. Done when: the built app contains the current version's entries.
- [ ] Build the `What's new` page and show it once per newer `Release` or `Preview` version (compared with `recto::core::SemVer`, `D07 T02 §19`; a `Dev` build never opens it by itself), reachable later from `Help` and `Ctrl+K` ("what's new", "changelog"). Done when: the page appears once after an upgrade from `1.0.0` to `1.1.0`, not after a downgrade to `1.0.0`, not for a `Dev` build, and never on a normal start. Cheaper substitute: a link to a web changelog the user must find.
- [ ] Add the `Check for updates` setting to the `Language and updates` group with the decided default, reading it through `SettingsStore`, and a manual `Check now` command. Done when: the setting is read at startup and listed in the settings search.
- [ ] Query `https://api.github.com/repos/rizonesoft/Recto/releases/latest` (`Release` and `Dev` builds) or `releases?per_page=20` (`Preview` builds) at most once per 24 hours only while the setting is on, compare versions, and show a dismissible `Update available` `ui::InfoBar` linking to the release page; never download or install. Done when: a test with the setting off records zero network requests and with it on records one, and with a newer release fixture `Shell.UpdateNoticeOptIn` finds the `Update available` `ui::InfoBar` naming that version.
- [ ] Compare in `UpdateCheck::pick_offer(running, releases)` in `src/services/update_check.h/.cpp`: parse each `tag_name` with `SemVer::from_tag` (`D07 T02 §19`), skip drafts, unparseable tags and, for a `Release` install, every pre-release (by the API's `prerelease` flag and by the parsed version), and offer the highest remaining version that is strictly newer by precedence; bound the response at 1 MiB and parse it with the bounded JSON reader. Done when: `Shell.UpdateOfferRules` checks a `Release` `1.0.0` install against `1.1.0-rc.1` (no offer) and `1.0.1` (offer), a `Preview` `1.1.0-rc.1` install against `1.1.0-rc.2` (offer), a `Dev` `1.1.0-dev.4` build against `1.1.0` (offer) and `1.1.0-rc.1` (no offer), `1.0.0+other` against `1.0.0` (no offer), a `vfoo` tag (skipped) and a 2 MiB body (refused). Cheaper substitute: comparing `tag_name` strings, which ranks `v1.10.0` below `v1.9.0`.
- [ ] Show a clear message on a failed check (cause and when it will retry) instead of a bare status code. Done when: a 403 fixture shows the rate-limit message.
- [ ] Commit: `app: show What's new per version and an opt-in update notice`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.WhatsNewOncePerVersion:Shell.UpdateNoticeOptIn:Shell.UpdateOfferRules` passes on the display-session host, with zero network requests recorded while the setting is off.

## 8. Strict issue-form YAML reader and top-level schema types

> **Started:** 2026-10-21T13:30:00Z

§1's `tests/scripts/test_issue_forms.py` reads the issue forms with a stdlib YAML subset reader, because Recto's tooling is stdlib Python and CI has no PyYAML. Review round 4 of §1 (`docs/reviews/07-release/D07-T03-s1.md`, stopped at the three-round cap) found five shapes that the reader or the schema check accepts while YAML or GitHub would not; none occurs in today's forms, but each lets a broken form pass the stdlib gate. This section closes them so the reader accepts only what it can load exactly as YAML 1.1 does.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Tighten `_Loader` in `tests/scripts/test_issue_forms.py` so each of the four reader findings is refused. Done when: each reproduction from round 4 raises `YamlSubsetError`.
2. Type-check the remaining top-level keys in `form_faults`. Done when: `projects: true` added to the bug form makes `form_faults` return a fault.

- [x] Refuse a sequence item whose dash is followed by more than one space (`  -   type: input`) in `_Loader._sequence`, instead of re-indenting it to two columns. Done when: that line raises `YamlSubsetError`, matching PyYAML's `ParserError`.
- [x] Refuse characters outside YAML's printable set (C0 controls other than line feed and carriage return, C1 controls, `U+FFFE`, `U+FFFF`, and surrogates) in `_Loader.__init__`. Done when: a `\x00` inside `label: Recto version` raises `YamlSubsetError`, matching PyYAML's `ReaderError`.
- [x] Refuse `>` folded block scalars in `_Loader._value` (the forms use only `|`), naming `|` in the message. Done when: `a: >\n  one\n    two\n  three\n` raises `YamlSubsetError` and the forms still load.
- [x] Validate trailing whitespace-only lines in `_Loader._block_scalar` before they are dropped, refusing any that hold spaces past the block's indentation. Done when: `a: |\n  text\n     \n` raises `YamlSubsetError`.
- [x] Type-check top-level `assignees` and `projects` (a list of non-empty strings or a comma-separated string) and `type` (a non-empty string) in `form_faults`, per GitHub's top-level form syntax. Done when: `projects: true`, `assignees: 3` and `type: [bug]` each make `form_faults` return a fault.
- [x] Add each reproduction above to `test_refuses_what_it_does_not_understand` or `test_schema_type_errors_are_caught`. Done when: the suite passes with and without PyYAML, and removing any one new check makes its case fail.
- [x] Commit: `tests: refuse the remaining YAML shapes and type-check top-level form keys`

**Test checkpoint:** `python -m unittest tests.scripts.test_issue_forms` passes with PyYAML installed and with it blocked (`sys.modules['yaml'] = None`), and deleting the dash-spacing refusal makes `test_refuses_what_it_does_not_understand` fail.

> **Verified:** 2026-10-21 | §8 | 7060e141e01280ad0d7ecffd66f7d32f9caf5783
> **Review:** `docs/reviews/d07-t03-s8.json`

## 9. Gtest regression evidence from the fix commit's own CI test list

> **Started:** 2026-10-21T11:10:00Z

`D07 T03 §4` checks that a `Regression-Test:` gtest id existed at its fix commit by scanning C++ source, and its review ran to the 22-round cap (`docs/reviews/07-release/D07-T03-s4.md`, `## Cap reached`) because a text scan cannot decide what the compiler builds: a macro-defined `#if` condition, a `TEST` passed to a macro, or a declaration whose start depends on the `#if` alternative all need a preprocessor. The compiler's own answer is the test binary's `--gtest_list_tests`. CI already builds and tests every push to `main` (`.github/workflows/build.yml`); this section has it record that list against the commit as a git note (`refs/notes/gtest-list`), which is permanent and keyed by commit, and makes `scripts/release-notes.py` read the fix commit's note instead of scanning source. Default taken 2026-10-21: git notes rather than workflow artifacts, because artifacts expire after at most 90 days and a release can be further from its fixes; the cost of changing is a different reader in `scripts/release-notes.py` and a different publish step.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Publish the list from CI. Done when: a push to `main` adds a note under `refs/notes/gtest-list` holding that commit's `recto-tests.exe --gtest_list_tests` output.
2. Read it in `scripts/release-notes.py`. Done when: a gtest id is verified only by the fix commit's note, and the source scanner is gone.

- [x] Add a step to the x64 Release test job in `.github/workflows/build.yml` that runs `recto-tests.exe --gtest_list_tests`, adds the output as a git note on `github.sha` under `refs/notes/gtest-list`, and pushes `refs/notes/gtest-list` (with `contents: write` for that job only, retrying on a concurrent-push rejection by fetching and merging the notes ref). Done when: after a push, `git fetch origin refs/notes/gtest-list:refs/notes/gtest-list` and `git notes --ref=gtest-list show <sha>` print the list.
- [x] Run the step only for `main`, never on pull requests or forks, so no untrusted build writes notes: on a push to `main`, or a manual run on `main` naming a full 40-character commit SHA on `main` (backfill for a commit a multi-commit push skipped), recording the list against the commit the build job actually checked out. Done when: the workflow condition names `refs/heads/main` with `push` or `workflow_dispatch`, the build job refuses a `sha` that is not 40 hex characters, and `test-list-note` reads the commit from `needs.build.outputs.sha`. **Corrected 2026-10-21:** said "only on pushes to `main`"; review round 1 showed a multi-commit push leaves earlier fix commits without a note, so a checked manual backfill was added.
- [x] In `scripts/release-notes.py`, fetch `refs/notes/gtest-list` once per run and verify a gtest id by reading the fix commit's note with `read_gtest_list`; a commit with no note reports the id as unverified (`the fix commit has no CI test list`). Done when: a fixture repository with a note on the fix commit passes, one whose note lacks the id fails, and one with no note fails.
- [x] Delete the C++ source scanner (`_spliced`, `cpp_lines`, `cpp_code`, `_conditional_lines`, `cpp_sources`, `gtest_declared`, `_in_number`, `_separator_follows`) and the `--gtest-list` option, and their fixtures in `tests/scripts/test_release_notes.py`. Done when: `git grep -n "cpp_lines\|gtest_declared\|--gtest-list" -- scripts tests` prints nothing.
- [x] Update `docs/support/user-bugs.md`: a gtest regression test counts when CI's list for the fix commit names it; drop the plain-declaration convention. Done when: the file names `refs/notes/gtest-list` and no longer names the convention.
- [x] Add fixtures to `tests/scripts/test_release_notes.py` that write notes with `git notes --ref=gtest-list add`. Done when: removing the note lookup makes the suite fail.
- [x] Commit: `release: verify gtest regression tests against the fix commit's CI test list`

**Test checkpoint:** `python -m unittest tests.scripts.test_release_notes` passes; a fixture fix commit whose note lacks its `Regression-Test:` id, and one with no note, are each reported as violations; and a real push to `main` leaves a readable note (`git notes --ref=gtest-list show <sha>`).

> **Verified:** 2026-10-21 | §9 | c3f23b0d40af9891ead03b0b326146232af65435
> **Review:** `docs/reviews/d07-t03-s9.json`

## 10. Changelog and release body from stamped sections and `Fixes-User:` trailers

Recto already records what shipped: every finished TODO section has a stamp receipt (`docs/reviews/<ref>.json`, with its `implementation_sha`, `title` and `date`), each section declares `Surface: yes` or `no`, and every user-reported fix carries a `Fixes-User:` trailer (`§4`). This section turns those records into `CHANGELOG.md` in Keep a Changelog 1.1.0 format and the GitHub Release body, with no new commit rule, so the unattended campaign keeps working exactly as it does (owner decision 2026-10-23). Sections stamped between two tags become entries: `Surface: yes` under `Added`, `Surface: no` under `Changed` (with build, test and tooling domains grouped as internal), a section's `> **Breaking:**` note under `Changed` or `Removed`, `Fixes-User:` trailers under `Fixed`, and fixes whose issue carries the `security` label under `Security`. Everything is read from git at the two refs, never from the working tree or the clock, so one range always produces the same bytes.

**Phase:** 6
**Needs:** none
**Surface:** no

**Build order:**

1. Write `scripts/changelog.py <from> <to> [--version <v>] [--issues <file>] [--json] [--update <file>] [--release-body <file>] [--check <file>]` reusing `scripts/release-notes.py` for the trailer and issue logic. Done when: `python -m unittest tests.scripts.test_changelog` passes on a fixture repository.
2. Seed `CHANGELOG.md` and add the `--check` mode the release gate uses (`§5`). Done when: `Changelog.SeedIsValid` parses the committed `CHANGELOG.md` as Keep a Changelog 1.1.0.

- [ ] Collect the sections shipped in `<from>..<to>`: list `docs/reviews/*.json` at `<to>` with `git ls-tree` (skipping `*.revoked-*.json`), keep each receipt whose `implementation_sha` is an ancestor of `<to>` and not of `<from>` (`git merge-base --is-ancestor`; `root` as `<from>` means the first release), and read each section's `Surface`, domain and any `> **Breaking:**` line from the TODO file at `<to>` (`git show <to>:<path>`, parsed with `scripts/lib/todo_graph.py`). Done when: `Changelog.CollectsStampsInRange` builds a fixture with a receipt before `<from>`, two inside the range and one revoked, and gets exactly the two. Cheaper substitute: grepping commit subjects, which misses sections stamped in a later commit and lists reverted work.
- [ ] Classify each section: `Surface: yes` to `Added` with the section title; `Surface: no` to `Changed`, prefixed with its domain name, except domains `00-workspace`, `06-quality` and `99-manual`, which go to one nested `Internal (build, tests and tooling)` item under `Changed`; a `> **Breaking:** <text>` line adds a `Changed` entry starting `**Breaking:**`, or a `Removed` entry when the text starts with `Removed`. Done when: `Changelog.Classifies` checks one fixture section per rule.
- [ ] Add the fixes from `release-notes.py`'s `Fixes-User:` reader (loaded with `importlib`, never copied): an issue labelled `security` in the `--issues` export goes to `Security`, every other one to `Fixed` as "<issue title> (#N)", and with no `--issues` file every fix goes to `Fixed` as "#N". Done when: `Changelog.FixesAndSecurity` places a `security` fixture under `Security` and two others under `Fixed`.
- [ ] Render Keep a Changelog 1.1.0: the `# Changelog` heading and its two standard sentences (linking `https://keepachangelog.com/en/1.1.0/` and `https://semver.org/spec/v2.0.0.html`), `## [Unreleased]`, then `## [<version>] - <YYYY-MM-DD>` per release with the date taken from the annotated tag's tagger date (the commit date of `<to>` when it is untagged), the groups in the order `Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, `Security`, empty groups omitted, and compare links at the end (`[1.1.0]: https://github.com/rizonesoft/Recto/compare/v1.0.0...v1.1.0`). Done when: `Changelog.KeepAChangelogFormat` matches a committed expected file byte for byte. Source: Keep a Changelog 1.1.0.
- [ ] Implement `--update <file>`: insert or replace exactly the block of `--version` and its compare link, keep every other byte, write through temp-file-then-replace, and produce identical bytes on a second run. Done when: `Changelog.UpdateIsIdempotent` runs it twice on a changelog with two earlier releases and a hand-edited `Unreleased` block, and finds both untouched and the second run a no-op.
- [ ] Implement `--release-body <file>` (the version block, the unsigned-release SmartScreen sentence of `D07 T02 §6`, the line "Verify downloads against SHA256SUMS.txt", and the "Fixed in v<version>" list of `§4`) and `--json` (the entries as data, which `§7` embeds as `whats_new.json`). Done when: `Changelog.ReleaseBodyAndJson` finds the sentence and each entry in both outputs.
- [ ] Implement `--check <file> --version <v>`: exit `1` when the file's block for that version differs from a fresh render, `0` when equal. Done when: `Changelog.CheckDetectsDrift` changes one word in a fixture block and gets exit `1`.
- [ ] Seed `CHANGELOG.md` at the repository root with the heading, the two sentences and an empty `## [Unreleased]`, and link it from `README.md` and `docs/versioning.md` (`D07 T02 §18`). Done when: `Changelog.SeedIsValid` parses the committed file (heading, both sentences, an `Unreleased` block, no release block) and fails on a copy missing the `Unreleased` heading.
- [ ] Commit: `release: generate CHANGELOG.md and the release body from stamps and Fixes-User trailers`

**Test checkpoint:** `python -m unittest tests.scripts.test_changelog` passes and `python scripts/changelog.py root HEAD --json` on the repository is quoted, listing the stamped sections and fixes so far.

## 11. Contract snapshots and `scripts/next-version.py` SemVer bump suggestion

SemVer only means something if "breaking" is checked, not remembered. Recto's public contracts (owner decision 2026-10-23, `docs/versioning.md`) are the `recto-cli` flags and JSON output, the `recto-mcp` tool schemas, the settings, recents and session file schemas, the Group Policy keys, and the features reachable from `Ctrl+K`. This section commits a canonical snapshot of each under `docs/contracts/`, keeps them current with tests so a contract change always shows up in a diff, and writes `scripts/next-version.py`, which compares the snapshots at the last release tag with `HEAD` and combines that with what shipped (`§10`) to suggest the next version: a removed or changed contract item or a `> **Breaking:**` note means major, any new contract item or `Surface: yes` section means minor, anything else patch.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `scripts/export-contracts.py` and the `Contracts.SnapshotsCurrent` test, and commit the first snapshots. Done when: `python scripts/export-contracts.py --check` and `recto-tests --gtest_filter=Contracts.*` pass on the built tree.
2. Write `scripts/lib/contract_diff.py` and `scripts/next-version.py`, and wire `--check` into `scripts/check-release-version.py` (`D07 T02 §18`). Done when: `python -m unittest tests.scripts.test_next_version` passes.

- [ ] Write `scripts/export-contracts.py [--check]` producing, in canonical form (sorted keys, two-space indent, LF, volatile fields such as `registry_hash` and `recto_version` removed), `docs/contracts/cli-verbs.json` from `recto-cli verbs --json` (`D04 T08 §2`), `docs/contracts/cli-version.json` from the key set of `recto-cli --version --json` (`D07 T02 §21`), `docs/contracts/mcp-tools.json` from the `tools/list` reply of `recto-mcp` driven through stdin (`D04 T08 §5`), and `docs/contracts/policies.json` from `policy/Recto.admx` (`D07 T02 §8`); `--check` exits `1` when a committed file differs. Done when: `tests/scripts/test_next_version.py` finds `--check` failing after a planted edit to each file. Cheaper substitute: a reviewer's memory of what the CLI accepted last release.
- [ ] Add `Contracts.SnapshotsCurrent` in `tests/recto-tests/contract_tests.cpp`, which renders `docs/contracts/settings-schema.json` (every `SettingsStore` key with type, enum values and default, `D02 T04 §2`), `recents-schema.json` and `session-schema.json` (field tables of `RecentFilesStore` and `SessionStore` with their `schema` numbers, `D07 T01 §22`, `D02 T01 §13`) and `commands.json` (every `CommandRegistry` command id, `D02 T03 §11`) in process and compares them with the committed files, rewriting them only when `RECTO_UPDATE_CONTRACTS=1`. Done when: the test passes, and fails naming the key when a settings key is added in a scratch build without updating the snapshot.
- [ ] Write `scripts/lib/contract_diff.py` classifying the difference between two snapshot sets: a removed verb, flag, tool, argument, key, field, enum value, policy or command id, a changed type, an optional argument made required, a new required argument, or a changed exit-code meaning is `major`; an added item, optional argument or enum value is `minor`; a change to descriptions or help text only is `patch`; an item newly marked `"deprecated": true` is a `Deprecated` note. Done when: `NextVersion.ContractDiffRules` has one passing fixture pair per rule.
- [ ] Write `scripts/next-version.py [--from <tag>] [--to HEAD] [--json] [--check]`: `--from` defaults to the highest `Release` tag; the level is the highest of the contract diff (`git show <tag>:docs/contracts/...` against `<to>`), a `> **Breaking:**` note in a section shipped in the range (major), a `Surface: yes` section shipped in the range (minor), and any shipped section or `Fixes-User:` fix (patch); the output is the suggested version and one reason line per finding; with nothing shipped it prints "no changes" and exits `0`. Done when: `NextVersion.Suggests` gets `2.0.0` for a removed verb after `v1.4.2`, `1.5.0` for an added optional flag, `1.4.3` for a fix only, and `2.0.0` for a `Breaking:` note.
- [ ] Implement `--check`: exit `1` when `VERSION`'s core is below the suggestion (for example `1.4.3` when a contract break suggests `2.0.0`), `0` otherwise, and call it from `scripts/check-release-version.py` (`D07 T02 §18`) so a release tag can never understate a break. Done when: `NextVersion.CheckRefusesUnderstatedBump` fails a fixture tagged `v1.4.3` over a removed verb and passes one tagged `v2.0.0`.
- [ ] Feed the contract findings into the changelog (`§10`): breaking items under `Removed` or as `**Breaking:**` entries under `Changed`, deprecations under `Deprecated`. Done when: `Changelog.ContractFindings` lists a removed fixture verb under `Removed`.
- [ ] Document the `> **Breaking:** <what breaks and how to migrate>` section note in `docs/versioning.md` and among the optional stamp companions of `todo/README.md`, written by hand before stamping a section that breaks a contract. Done when: both files describe the note and `NextVersion.DocNamesBreakingNote` finds it in each.
- [ ] Run `python scripts/export-contracts.py --check` in `.github/workflows/build.yml` after the x64 build. Done when: `tests/scripts/test_next_version.py` finds the step in the workflow.
- [ ] Commit: `release: snapshot public contracts and suggest the next SemVer from what shipped`

**Test checkpoint:** `python -m unittest tests.scripts.test_next_version` passes, `python scripts/export-contracts.py --check` exits `0`, `recto-tests --gtest_filter=Contracts.*` passes, and `python scripts/next-version.py --json` on the repository is quoted.

## 12. Offline decoder diagnostic reference: steps, failure classes and next actions

The completion review of `D01 T02 §25` (`docs/reviews/01-engine/D01-T02-s25.md`, excellence E2) found that the diagnostic ring and ETW now name a decoder step and a Windows HRESULT, but nothing tells a supporter or a user what each means or what to do. Acrobat publishes recovery guidance for its errors; Recto ships an offline reference that covers every decoder step and every WIC failure class, with a safe next action for each, and that Report a problem (`§2`) links to.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `docs/support/decoder-diagnostics.md` from the closed catalogues in `src/pdf/error.h`. Done when: every `DecoderStep` and every failure class has an entry.
2. Keep it complete with a check. Done when: the check fails when a step or class has no entry.

- [ ] Write `docs/support/decoder-diagnostics.md`: for each `DecoderStep` what Recto was doing, and for each WIC failure class (resource, cancel, capability, data, system) the HRESULTs it covers, what it means for the file (the file may be fine for resource, cancel and capability) and a safe next step (close other documents, install the Windows codec or the HEIF/JPEG 2000 extension, use the original file, report it), with no step that could change or upload a document. Done when: every enumerator of `DecoderStep` and every class has a heading.
- [ ] Cover CCITT damaged-row outcomes in `docs/support/decoder-diagnostics.md`: rows substituted under Table 11 (what was replaced and why), the allowance used up, no EOL to resynchronize at, recovery off without `/EndOfLine`, each with a safe next step (another scan or the original file). Done when: each of the CCITT refusal reasons `D01 T02 §26` appends has a heading. **Corrected 2026-10-23:** (`D06 T05 §34` completion review: filed here).
- [ ] Add `tests/scripts/test_decoder_diagnostics_doc.py`, which reads the `DecoderStep` enumerators from `src/pdf/error.h` and the class list from `src/pdf/filters.cpp`'s `WicFailureClass` and fails on any without a heading in the document. Done when: it passes and fails with a planted enumerator.
- [ ] Link the document from Report a problem's `How to share a file safely` area (`§2`) and from `docs/support/triage.md`. Done when: both links resolve.
- [ ] Commit: `docs: offline decoder diagnostic reference with next actions`

**Test checkpoint:** `python -m unittest tests.scripts.test_decoder_diagnostics_doc` passes and fails with a planted enumerator.

## Verification

- A new GitHub issue can only be a bug form, a feature form or a security report, and `python -m unittest tests.scripts.test_issue_forms tests.scripts.test_release_notes tests.scripts.test_release_gate tests.scripts.test_user_demand` passes.
- `Help -> Report a problem` copies diagnostics with no document content, and `recto-cli scrub` produces a reproducer with the same structural trace, a kept-channel report and preview pages.
- The release runbook runs `scripts/release-gate.py`, publishes "Fixed in vX.Y.Z" comments from `scripts/release-notes.py`, and reviews `docs/user-demand.md`.
- `python -m unittest tests.scripts.test_changelog tests.scripts.test_next_version` passes, `python scripts/export-contracts.py --check` exits `0`, and the release gate refuses a tag that understates a contract break.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
