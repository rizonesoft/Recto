---
schema_version: 1
id: listening-to-users
domain: 07-release
status: active
title: "TODO-03 -- Listening to Users: Intake, Triage, Diagnostics, Releases and Demand"
---

# TODO-03 -- Listening to Users: Intake, Triage, Diagnostics, Releases and Demand

> **Goal:** Recto hears its users and answers them in the open. A report arrives through one GitHub form with the version, install type, OS and steps it needs, prefilled from the app's own diagnostics; a user with a private file can share a structure-preserving reproducer after inspecting it; every user-reported bug gets a regression test before it closes; an issue closes when the release containing its fix ships; security reports have a private channel and an advisory; and what users ask for is recorded in a public demand ledger that is reviewed before every release.

> **Current state:** Measured 2026-10-21. `.github/` holds `pull_request_template.md` and CI workflows only: there are no issue forms, no `SECURITY.md`, no triage or release-closing policy, no reproducer tooling, no demand ledger and no in-app path from a problem to a report. `D07 T02 §4` plans a local crash bundle and `D07 T02 §6` a release runbook; neither covers non-crash reports or closing issues on release.

## Inputs

- Foundation plan D16 (local diagnostics and zero network telemetry) and section 9 (hostile input); AGENTS.md "Tests prove claims" and "Fix at the owning layer".
- -> XREF: D07 T02 §4 -- the opt-in local crash bundle that §2 here reuses for its diagnostics text, and that links to §1's issue form.
- -> XREF: D07 T02 §6 -- the GitHub release runbook that §4 and §5 here extend with closing-on-release and the release gate.
- -> XREF: D02 T03 §11 -- `CommandRegistry`, through which §2's `Report a problem` and §7's `What's new` are reachable from `Ctrl+K`.
- -> XREF: D06 T03 §12 -- the clean-machine release smoke whose report §5's gate requires.
- -> XREF: D99 T01 §2 -- the operator enables GitHub private vulnerability reporting that §1's `SECURITY.md` points to.
- -> XREF: D08 T01 §9 -- diagnostics must never carry an assistant key (D08 T01 §9 scans for it).
- -> XREF: D04 T07 §2 -- records the Action Wizard's watched-folder request (D04 T07 §2).
- -> XREF: D07 T02 §10 -- update notice policy.
- -> XREF: D06 T04 §25 -- the `D-Lifecycle.png` board and `design/SPEC.md` section 9.19 that §2, §7 here build against.

## Outcome

- A new GitHub issue is either a bug form (version, install type, Windows build, GPU, steps, expected and actual, file or reproducer) or a feature form that starts with a duplicate search; `SECURITY.md` routes vulnerabilities to private reporting.
- `Help -> Report a problem` (and `Ctrl+K`) copies diagnostics with no document content and opens the bug form prefilled; `recto-cli scrub` turns a private PDF into a structure-preserving reproducer, reports what it kept, and renders preview pages the user inspects before sharing.
- A user-reported bug closes only with a regression test that fails before the fix, and only when the release containing the fix is published, with "Fixed in vX.Y.Z".
- No release is tagged while a closed-on-main data-loss, crash or security fix is unreleased past seven days, and each security fix ships with an advisory.
- `docs/scope.md` says what Recto does and does not do, and `docs/user-demand.md` maps every recurring request to a TODO section or a recorded decision, reviewed before each release.

**Adjacency:** list=applicable @ D07 T03 §6; document=applicable @ D07 T03 §3; settings=applicable @ D07 T03 §7; reporting=applicable @ D07 T03 §6; notifications=applicable @ D07 T03 §7; permissions=applicable @ D07 T03 §1; audit=applicable @ D07 T03 §4; exchange=applicable @ D07 T03 §2; reverse=not-applicable (an issue or report is reversed on GitHub by closing or editing it; Recto keeps no local record to undo)

**Adjacency rationale:** The demand ledger (§6) is the list and the release-review report. The reproducer a user carries to the tracker is §3's scrubbed PDF. The only setting is §7's opt-in update notice, which is also the only notification. Private security reporting (§1) is the permission boundary: who may see a vulnerability before its fix. The changelog mapping issues to releases (§4) is the audit trail. Diagnostics copied to an issue (§2) is the exchange.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Issue forms, triage policy and `SECURITY.md` | -- | [ ] |
| 2 | §4 | User-bug regression rule, closing on release and changelog mapping | §1 | [ ] |
| 3 | §6 | Public scope page and user demand ledger | §1 | [ ] |
| 4 | §3 | Structure-preserving reproducer scrubber (`recto-cli scrub`) | D04 T02 §2 | [ ] |
| 5 | §2 | In-app `Report a problem` with privacy-safe diagnostics | §1, D07 T02 §1, D07 T02 §4, D02 T03 §11, D06 T04 §25 | [ ] |
| 6 | §5 | Release gate for unreleased fixes, security advisories and CI integrity | §4, D07 T02 §6, D07 T02 §7, D06 T03 §12 | [ ] |
| 7 | §7 | What's new after update and opt-in update notice | §2, D02 T04 §6, D06 T04 §25 | [ ] |

## 1. Issue forms, triage policy and `SECURITY.md`

> **Started:** 2026-10-21T09:40:00Z

Common user-handling failures are no issue template, reports relayed to GitHub where reporters cannot read replies, a bot that splits reports and closes real requests as support questions, and private vulnerability reporting left off while researchers wait. This section writes Recto's intake files and the triage rules people follow. Turning on GitHub's private vulnerability reporting is an operator setting owned by `D99 T01 §2`.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Author `.github/ISSUE_TEMPLATE/bug_report.yml`, `feature_request.yml` and `config.yml`, plus `SECURITY.md` at the repository root. Done when: `python -m unittest tests.scripts.test_issue_forms` parses each form and finds every required field.
2. Author `docs/support/triage.md` with labels, response targets and the closing rules. Done when: the labels it names exist in `.github/labels.yml` and the test checks the file lists every label.

- [ ] Author `.github/ISSUE_TEMPLATE/bug_report.yml` with required fields: Recto version (from `Help -> About`), install type (`Installer`, `Portable`, `MSIX`), Windows build, GPU and driver, steps, expected, actual, and a file or `recto-cli scrub` reproducer, plus an optional diagnostics paste from §2. Install type is an `input` naming the three values, not a `dropdown`, because GitHub's URL query prefill only fills text fields and §2 prefills `version`, `install-type` and `windows-build` (default taken 2026-10-21; switching to a `dropdown` costs §2 that prefill). Done when: the form renders on GitHub and every required field has `validations: required: true`. Cheaper substitute: a Markdown template whose placeholders reporters leave unfilled.
- [ ] Author `.github/ISSUE_TEMPLATE/feature_request.yml` that opens with a link to search existing issues and `docs/user-demand.md`, then asks for the task the user is trying to do and the app they use today. Done when: the form renders and the search link targets this repository's open issues.
- [ ] Author `.github/ISSUE_TEMPLATE/config.yml` disabling blank issues and linking `SECURITY.md` for vulnerabilities. Done when: GitHub's new-issue page shows only the two forms and the security link.
- [x] Author `SECURITY.md` naming supported versions, the private reporting route (GitHub "Report a vulnerability"), a first-response target of 3 working days, and the promise of an advisory with every security release. Done when: the file exists and `test_issue_forms` checks those four items.
- [x] Author `.github/labels.yml` with area labels (`engine`, `viewer`, `text`, `convert`, `organize`, `print`, `install`), kind labels (`bug`, `feature`, `regression`, `data-loss`, `crash`, `security`) and state labels (`needs-repro`, `fixed-on-main`, `released`). Done when: the file parses and every label has a description.
- [x] Author `docs/support/triage.md`: a person, not a bot, closes an issue; a duplicate close links the canonical issue and states its status; a feature request is never closed as "support"; `needs-repro` offers `recto-cli scrub` and a synthetic-file guide; one owner per issue, and a superseded pull request is closed with a link. Done when: the file covers each rule and `test_issue_forms` checks the headings.
- [x] Add `tests/scripts/test_issue_forms.py` validating the YAML forms, required fields, labels and the security file. Done when: removing a required field makes the test fail.
- [ ] Commit: `repo: add issue forms, labels, triage policy and SECURITY.md`

**Test checkpoint:** `python -m unittest tests.scripts.test_issue_forms` passes, and deleting the `validations` block from the version field makes it fail.

## 2. In-app `Report a problem` with privacy-safe diagnostics

Reporters often cannot say which version, install type or GPU they had, and maintainers spend their replies asking. Recto offers `Help -> Report a problem`: it builds a diagnostics block with no document content, copies it, and opens the bug form with the version, install type and Windows build prefilled through GitHub's form URL parameters. Nothing is sent by Recto itself; the user's browser opens the form.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface: a small `ContentDialog` on the `design/SPEC.md` section 9.9 dialog chrome; no screenshot exists. Design board: `design/SPEC.md` section 9.19 with `design/screenshots/D-Lifecycle.png` (drawn by `D06 T04 §25`).
**Job:** A user who hit a problem can copy accurate diagnostics and reach a prefilled GitHub form in two clicks. Consumer: the `bug_report.yml` form (§1) and the maintainer reading it.
**Treatment:** One dialog listing exactly what will be copied, with `Copy diagnostics` and `Open bug form` buttons and a link to `recto-cli scrub` help. Cheaper substitute that fails the checkpoint: a raw log file the user must find in `%LOCALAPPDATA%`.
**Chrome:** Shared `RectoDialogStyles.xaml` (`D02 T01 §10`), stock `ContentDialog`, `RectoColors.xaml` tokens, Fluent UI System Icons (`RectoIcons`: `question_circle`).

**Build order:**

1. Add `src/services/diagnostics_report.h/.cpp` building the diagnostics text from the version provenance (`D07 T02 §1`), install type, Windows build, GPU adapter and driver, active settings that affect rendering, and the last 50 diagnostic ring entries with document text and paths stripped. Done when: `Diagnostics.ReportHasNoDocumentContent` passes on a session that opened a fixture with known text.
2. Add `ReportProblemDialog` in `src/app/dialogs/` and the `help.reportProblem` command. Done when: the dialog opens from `Help` and from `Ctrl+K`, copies the text, and launches the prefilled form URL.

- [ ] Author `src/services/diagnostics_report.h/.cpp` composing version, commit, build flavour, install type, Windows build, GPU and driver, DPI and display count, renderer mode (hardware or WARP) and the redacted ring tail. Done when: the output contains each field and no file path or page text.
- [ ] Strip document text, file names, folder paths, form values and passwords from ring entries with an allow-list of fields, never a block-list. Done when: `Diagnostics.ReportHasNoDocumentContent` finds none of the fixture's strings.
- [ ] Build `src/app/dialogs/ReportProblemDialog.xaml/.h/.cpp` showing the exact text that will be copied, `Copy diagnostics`, `Open bug form` and a `How to share a file safely` link. Done when: the dialog renders in Light, Dark and HighContrast with token brushes only. Cheaper substitute: copying silently without showing the user what leaves their machine.
- [ ] Build the GitHub form URL with `template=bug_report.yml` and prefilled `version`, `install-type` and `windows-build` query fields, opened through the shared ask-before-open link path. Done when: the opened URL prefills those three fields on GitHub.
- [ ] Register `help.reportProblem` in `CommandRegistry` with synonyms "bug", "report", "feedback" and add it to the Classic `Help` menu. Done when: each synonym finds the command in `Ctrl+K`.
- [ ] Offer `Report a problem` from the `D07 T02 §4` crash bundle prompt and the `D06 T03 §3` page notice. Done when: both surfaces open this dialog.
- [ ] Commit: `app: add Report a problem with privacy-safe diagnostics and a prefilled bug form`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Diagnostics.ReportHasNoDocumentContent:Shell.ReportProblemDialog` passes on the display-session host.

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

A common failure is closing issues when a fix merges while users on the release keep hitting them, duplicates piling up, and fixes shipping without tests. Recto's rule: a user-reported bug closes with a regression test that fails before the fix, the fix commit carries `Fixes-User: #N` and `Regression-Test: <test id>` trailers, the pull request quotes the test failing on the parent commit and passing on the fix, the issue is labelled `fixed-on-main`, and it closes only when the release that contains the commit is published, with a "Fixed in vX.Y.Z" comment.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Author `scripts/release-notes.py` listing every `Fixes-User:` trailer between two tags, grouped by label, and printing the closing comments. Done when: `python -m unittest tests.scripts.test_release_notes` passes on a fixture repository.
2. Document the rule in `docs/support/user-bugs.md`, link it from `AGENTS.md` and the pull request template. Done when: the template asks for the trailer and the regression test name.

- [ ] Author `scripts/release-notes.py <from-tag> <to-tag> [--json]` reading commit trailers with `git log --format=%(trailers:key=Fixes-User)`. Done when: a fixture repository with three trailers yields three entries.
- [ ] Print, per issue, the closing comment "Fixed in vX.Y.Z (commit, release link)". Done when: the output matches the fixture's expected text.
- [ ] Refuse a `Fixes-User:` commit without a `Regression-Test:` trailer, or whose named test (a gtest `Suite.Case` or a Python `module.Class.test_name`) does not exist at that commit, with `--allow-no-test <reason>` recorded in the output for documentation-only fixes; whether the file is new or existing does not matter. Done when: a fixture adding a real case to an existing test file passes and a fixture naming a missing test is reported as a violation.
- [ ] Author `docs/support/user-bugs.md`: reproduce first (scrubbed or synthetic file into `tests/corpus/user-reports/` with a provenance row), test fails before the fix, trailer on the fix, label `fixed-on-main`, close on release. Done when: the file states each step.
- [ ] Add both trailers and a "Regression test: failing on parent, passing on fix" evidence block to `.github/pull_request_template.md`, and a pointer from `AGENTS.md` "Working rules". Done when: both files reference `docs/support/user-bugs.md`.
- [ ] Add `tests/scripts/test_release_notes.py` building a temporary git repository with tagged commits. Done when: the test passes and removing the trailer parsing makes it fail.
- [ ] Commit: `repo: map user-reported fixes to releases and require a regression test per fix`

**Test checkpoint:** `python -m unittest tests.scripts.test_release_notes` passes, and a fixture `Fixes-User:` commit whose `Regression-Test:` names a test that does not exist is reported as a violation.

## 5. Release gate for unreleased fixes, security advisories and CI integrity

Another is security and data-loss fixes sitting on main while the current release stays vulnerable with no advisory, and CI red or cancelled on main so regressions ship unseen. `scripts/release-gate.py` blocks tagging when the release would leave a `fixed-on-main` crash, data-loss or security fix unreleased for more than seven days without a patch release, when a security fix has no advisory draft, when the last main CI run was cancelled or red, or when the `D06 T03 §12` smoke report is missing.

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
- [ ] Add `tests/scripts/test_release_gate.py` with JSON fixtures for each rule. Done when: each rule has a passing and a failing fixture.
- [ ] Commit: `release: gate tags on unreleased critical fixes, advisories, green CI and the smoke report`

**Test checkpoint:** `python -m unittest tests.scripts.test_release_gate` passes, and each rule's failing fixture makes `python scripts/release-gate.py --fixture <file>` exit 1.

## 6. Public scope page and user demand ledger

Users file bugs for features that exist but are hidden, ask scope questions nobody answers, and cannot tell shipped from planned. Recto publishes `docs/scope.md` (what it does, what it never does and why: no PDF JavaScript, no network by default, Windows only, how XFA is handled) and `docs/user-demand.md`, a ledger that maps each recurring request to a TODO section, a recorded decision, or an explicit "not planned" with its reason. The ledger is seeded from Recto's own GitHub issues, `docs/acrobat-parity.md` and the competitor parity rows, and reviewed before every release.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Author `docs/scope.md` and `docs/user-demand.md`, seeding the ledger from Recto's own GitHub issues, `docs/acrobat-parity.md` and the competitor parity rows. Done when: every ledger row names a TODO reference, a decision, or "not planned" with a reason.
2. Add `scripts/check-user-demand.py` validating that each TODO reference in the ledger resolves through `scripts/todo-graph.py resolve`. Done when: `python -m unittest tests.scripts.test_user_demand` passes and a bogus reference fails it.

- [ ] Author `docs/scope.md`: supported Windows versions and architectures, what Recto never does (run PDF JavaScript, contact the network without consent, collect telemetry), XFA and dynamic-form handling, and where to ask. Done when: each topic has a heading and the file is linked from `README.md`.
- [ ] Author `docs/user-demand.md` with columns Request, Evidence (Recto issue numbers and parity rows), Status (`planned`, `decided`, `not planned`), Owner (TODO reference or decision) and Last reviewed. Done when: every open Recto issue labelled as a request, and every `docs/acrobat-parity.md` row without a TODO owner, has a row.
- [ ] Record the requests no section owns yet as `decided` or `not planned` with the decision and its cost of reversing, never as silence (tab tear-off and split view, mobile apps, AI features). Done when: no row has an empty Owner.
- [ ] Author `scripts/check-user-demand.py` resolving every TODO reference and failing on an unknown one or a row older than the last release's review date. Done when: the script exits 0 on the seeded ledger.
- [ ] Add `tests/scripts/test_user_demand.py` with a valid and an invalid ledger fixture. Done when: the invalid fixture fails.
- [ ] Add the ledger review to the release runbook checklist in `docs/release-and-store-runbook.md` once `D07 T02 §6` creates it, and to `docs/support/triage.md` now. Done when: `docs/support/triage.md` names the review.
- [ ] Commit: `docs: publish Recto's scope and a user demand ledger seeded from Recto issues and parity rows`

**Test checkpoint:** `python -m unittest tests.scripts.test_user_demand` passes and `python scripts/check-user-demand.py` exits 0 on the committed ledger.

## 7. What's new after update and opt-in update notice

Users stay on old builds and report bugs already fixed, and cannot find features that have shipped. Recto shows a `What's new` page once after each version change, built from §4's release notes embedded at build time (no network), and offers an opt-in update notice that checks GitHub Releases at most once a day only when the user turns it on. Because the notice is outbound network access, the owner decided on 2026-10-21 (foundation plan, owner decisions after the decision table): it ships in v1, off by default, and checks at most once a day while on.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.8 (About) for placement; the `What's new` page is a genuinely new surface on the same chrome, with no screenshot. Design board: `design/SPEC.md` section 9.19 with `design/screenshots/D-Lifecycle.png` (drawn by `D06 T04 §25`).
**Job:** A user who just upgraded sees what changed and which of their reported issues are fixed; a user who opted in learns a newer version exists. Consumer: the user, and the settings store that records the last seen version and the opt-in.
**Treatment:** A `What's new` page in the About area listing release-note entries, and a dismissible `InfoBar` titled `Update available` (naming the new version) linking to the release page. Cheaper substitute that fails the checkpoint: silent auto-install, or a check that runs without consent.
**Chrome:** Stock `InfoBar` and the About page layout from `D07 T02 §2`; the `Check for updates` toggle and `Check now` in the `Language and updates` group (SPEC 9.7), shared with the `Language` row of `D07 T02 §14` and placed by `kSettingsGroupOrder` (`D02 T04 §6`); `RectoColors.xaml` tokens only.

**Build order:**

1. Embed the release notes for the current version at build time (`build/generated/whats_new.json` from `scripts/release-notes.py`) and show the `What's new` page once when `SettingsStore` records a different last-seen version. Done when: `Shell.WhatsNewOncePerVersion` passes.
2. Add `Check for updates` (off by default, owner decision 2026-10-21) that queries the GitHub Releases API at most once a day and shows an `InfoBar` linking to the release; it never downloads or installs. Done when: `Shell.UpdateNoticeOptIn` passes and no request is made while the setting is off.

- [ ] Confirm the owner decision recorded 2026-10-21 in the foundation plan (v1, off by default, at most once a day) and use it as the `SettingsStore` default. Done when: the decision line exists in the plan and a fresh settings file reads `updates.check_enabled = false`.
- [ ] Generate `build/generated/whats_new.json` from `scripts/release-notes.py` during the build and embed it as a resource. Done when: the built app contains the current version's entries.
- [ ] Build the `What's new` page and show it once per version change, reachable later from `Help` and `Ctrl+K` ("what's new", "changelog"). Done when: the page appears once after a version change and never on a normal start. Cheaper substitute: a link to a web changelog the user must find.
- [ ] Add the `Check for updates` setting to the `Language and updates` group with the decided default, reading it through `SettingsStore`, and a manual `Check now` command. Done when: the setting is read at startup and listed in the settings search.
- [ ] Query `https://api.github.com/repos/rizonesoft/Recto/releases/latest` at most once per 24 hours only while the setting is on, compare versions, and show a dismissible `Update available` `InfoBar` linking to the release page; never download or install. Done when: a test with the setting off records zero network requests and with it on records one, and with a newer release fixture `Shell.UpdateNoticeOptIn` finds the `Update available` `InfoBar` naming that version.
- [ ] Show a clear message on a failed check (cause and when it will retry) instead of a bare status code. Done when: a 403 fixture shows the rate-limit message.
- [ ] Commit: `app: show What's new per version and an opt-in update notice`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.WhatsNewOncePerVersion:Shell.UpdateNoticeOptIn` passes on the display-session host, with zero network requests recorded while the setting is off.

## Verification

- A new GitHub issue can only be a bug form, a feature form or a security report, and `python -m unittest tests.scripts.test_issue_forms tests.scripts.test_release_notes tests.scripts.test_release_gate tests.scripts.test_user_demand` passes.
- `Help -> Report a problem` copies diagnostics with no document content, and `recto-cli scrub` produces a reproducer with the same structural trace, a kept-channel report and preview pages.
- The release runbook runs `scripts/release-gate.py`, publishes "Fixed in vX.Y.Z" comments from `scripts/release-notes.py`, and reviews `docs/user-demand.md`.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
