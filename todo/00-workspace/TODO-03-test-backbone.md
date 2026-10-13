---
schema_version: 1
id: test-backbone
domain: 00-workspace
status: active
title: "TODO-03 -- Test Backbone"
track: M0
---

# TODO-03 -- Test Backbone

> **Goal:** The test machinery every later domain's checkpoint runs on exists and is boring: a GoogleTest runner wired into the build, a licensed and provenanced PDF corpus, sanitizer and warning configurations, so that an engine section can say "run the suite" and mean something falsifiable.

> **Current state:** Measured 2026-10-13. The only tests in the repository are the TODO tooling tests (`tests/scripts/test_todo_graph.py`, Python). No C or C++ code, solution or corpus exists. The foundation plan fixes the choices this file executes: GoogleTest via vcpkg manifest mode for test-only dependencies (D14, section 2), MSVC AddressSanitizer and libFuzzer for security testing (sections 2 and 9), the thread-safety test battery (section 4.11, owned by D01 T03 §3) and a regression corpus under `tests/corpus/` (section 3.2). This file builds the backbone; D06 writes the suites that run on it.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 2 (test stack), 3.2 (`tests/`, `tests/corpus/`, `fuzz/`), 4.11 (the battery this backbone must be able to host), 9 (security testing).
- -> XREF: D06 T01 §1 -- the golden render suite is the first real consumer of the corpus and runner built here; its fixture format requirements flow back into §2.

## Outcome

- `recto-tests` runs locally and in CI with one command, filters by suite name, and fails the build on a red test.
- `tests/corpus/` holds a small, licensed starter corpus with a provenance record per file, and the rules for adding to it are written down.
- An AddressSanitizer configuration builds and runs the suite, ready for the engine battery in D01.

**Adjacency:** list=not-applicable (test discovery is the runner's own listing); document=not-applicable (no user-carried output); settings=applicable @ D00 T03 §3; reporting=applicable @ D00 T03 §1; notifications=not-applicable (CI status is GitHub's surface); permissions=not-applicable (no roles in a test harness); audit=applicable @ D00 T03 §2; exchange=not-applicable (no user data moves); reverse=not-applicable (tests create nothing to undo)

**Adjacency rationale:** Settings (the sanitizer and warning configurations) and reporting (test results a stamp can quote) are the backbone's real surfaces. Audit is the corpus provenance record: an unlicensed or unprovenanced PDF in the corpus is a legal defect in an AGPL repository, so every file carries its source. The rest belong to the product domains.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | GoogleTest runner wired into build and CI | D00 T01 §3 | [ ] |
| 2 | §2 | Corpus layout, provenance and starter set | §1 | [ ] |
| 3 | §3 | Sanitizer and warning configurations | §1 | [ ] |

## 1. GoogleTest runner wired into build and CI

D14 keeps vcpkg for test-only dependencies so no shipping binary inherits a test framework. The runner must support the section workflow's habit: filtered runs while iterating (`--gtest_filter`), full runs at file closeout, machine-readable results a stamp can quote.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Fill `vcpkg.json` (manifest mode, GoogleTest pinned) and wire `recto-tests` to it from the D00 T01 §3 scaffold, with test discovery by suite prefix (`Bridge.*`, `Actor.*`, and so on as domains land). Done when: `recto-tests --gtest_list_tests` enumerates the starter suites.
2. Add starter suites that prove the harness can fail: one passing arithmetic-free smoke test per project boundary (bridge link, core link, services link) plus one deliberately failing test behind a flag, removed after proving the failure path. Done when: a failing test turns the local run and the CI job red with the test named.
3. Wire the full run into `.github/workflows/build.yml` (D00 T01 §4) for x64 and ARM64 with the XML or JSON results uploaded as artifacts. Done when: a stamp can quote `recto-tests` counts from the CI artifact.

- [ ] `vcpkg.json` plus `recto-tests` discovery and filtering. Done when: `--gtest_filter` runs exactly the named suite. Cheaper substitute: a custom assert main that cannot filter or report counts.
- [ ] Failure path proven locally and in CI, then the planted failure removed. Done when: the red run and the following green run are both in CI history.
- [ ] Commit: `test: GoogleTest backbone via vcpkg manifest`

**Test checkpoint:** `recto-tests` exits 0 with the starter suites green on both architectures (quote counts), and exits non-zero naming the test when one is forced to fail.

## 2. Corpus layout, provenance and starter set

Every engine claim in this plan is measured against real PDFs: rendering goldens, the search text set, conversion round-trips, fuzz seeds. The corpus is therefore a legal and a technical artifact: each file needs a source and a license compatible with redistribution in an AGPL repository, recorded where the file lives.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Create `tests/corpus/` layout by purpose (`render/`, `text/`, `convert/`, `broken/`, `seeds/`) and `tests/corpus/README.md` stating the provenance rule: every file is self-generated, public domain, or under a named license recorded beside it. Done when: a contributor adding a file knows exactly what record it needs.
2. Generate the starter set with Recto-owned tooling where possible (scripted PDF generation checked in beside the outputs): multi-page text, rotated pages, image pages, an encrypted file with a known password, an outlined file, a damaged file, a two-column file for later DOCX review. Done when: each purpose folder holds at least the files its first consuming section names.
3. Record provenance per file in `tests/corpus/PROVENANCE.md` (file, generator or source, license, date). Done when: every corpus file has a row and D06 T01 §1 can consume the render set without a license question.

- [ ] Corpus layout and provenance rules in `tests/corpus/README.md`. Done when: the rule would refuse an anonymous PDF dropped in without a record.
- [ ] Starter corpus generated and provenanced. Done when: `recto-cli` (D01 T03 §1) will have a file for every verb on day one. Cheaper substitute: PDFs copied from the web with unknown licensing.
- [ ] Commit: `test: corpus layout with provenance and starter set`

**Test checkpoint:** every file under `tests/corpus/` appears in `PROVENANCE.md` (a count comparison in the commit body), and the starter set opens or refuses exactly as its folder promises once D01 T03 §1 exists (recorded then if the CLI lands later).

## 3. Sanitizer and warning configurations

The engine's thread-safety and hostile-input claims are only as good as the configurations that check them. This section lands the MSVC AddressSanitizer solution configuration and the warnings-as-errors bar once, so later sections inherit them instead of reinventing flags per project.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add solution configurations for ASan (Debug+ASan, x64 first) covering `recto-bridge`, `recto-core`, `recto-tests` and `recto-fuzz`, with the ASan runtime deployment the test runner needs. Done when: the §1 starter suites run green under ASan and a planted heap overflow in a scratch test is reported with a stack.
2. Set the warning bar in `Directory.Build.props`: `/W4` (or the scaffold's chosen level), warnings as errors, static analysis where MSBuild offers it, per-language (`/TC` bridge kept C-clean). Done when: a new warning fails the local build identically to CI.
3. Document how a section runs its ASan pass in `CONTRIBUTING.md`. Done when: D01 T03 §3 can cite the command instead of inventing one.

- [ ] ASan configuration building and running the suites. Done when: the planted-overflow proof is quoted in the commit body and then removed. Cheaper substitute: ASan flags on one project only, leaving the bridge unchecked.
- [ ] Warnings-as-errors bar shared by local and CI builds. Done when: both fail on the same planted warning.
- [ ] Commit: `test: ASan configuration and warnings-as-errors bar`

**Test checkpoint:** `recto-tests` green under ASan with the planted overflow reported before its removal (quote the ASan summary), and a planted warning fails both local and CI builds.

## Verification

- A new engine section can name its suite, filter and corpus files without inventing harness machinery.
- `python scripts/todo-graph.py validate` passes; the corpus provenance count matches the corpus file count.
- CI runs the suites on both architectures (D00 T01 §4) and the ASan pass is one documented command away.
