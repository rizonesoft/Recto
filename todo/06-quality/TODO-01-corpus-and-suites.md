---
schema_version: 1
id: corpus-and-suites
domain: 06-quality
status: active
title: "TODO-01 -- Golden, Text, Round-Trip and Performance Suites"
track: M1
---

# TODO-01 -- Golden, Text, Round-Trip and Performance Suites

> **Goal:** The evidence suites the milestones close on: golden render comparisons for the engine and viewer, exact text and find tables for the text corpus, conversion round-trips for every M4 and M5 converter, and the performance harness that measures foundation plan section 8's targets on the named reference machines.

> **Current state:** Measured 2026-10-14. No suites exist; D00 T03 will provide the runner, corpus folders (`render/`, `text/`, `convert/`, `broken/`, `seeds/`) with provenance, and the ASan configuration. Individual sections already name their own fixtures (single-threaded reference renders, extraction strings, hit tables, OCR scanned pages); this file turns those into maintained suites with refresh rules, so a later optimization or `recto-pdf` rendering change is judged against recorded expectations, not memory.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 4.11, 7 (each milestone's done-when wording), 8 (the five performance targets and the reference-machine rule).
- `tests/corpus/README.md` and `PROVENANCE.md` as built by D00 T03 §2 (the refresh rules extend, never replace, its provenance rule).
- -> XREF: D00 T03 §2 -- the corpus this file's suites consume and grow; fixture format requirements flow back into its folder rules.
- -> XREF: D01 T03 §1 -- `recto-cli` is the harness every suite drives; its output contract is what goldens compare.

## Outcome

- An engine rendering change produces a named, reviewable golden diff, or silence that means identical output.
- Find, extraction, and OCR correctness is a table that either matches or refuses, per corpus file.
- Performance targets are numbers measured on named machines by a repeatable harness, quoted identically by every section that claims them.

**Adjacency:** all=not-applicable (test suites over other files behavior; their lists are the corpus folders and their reports are test output owned by the runner in D00 T03 §1)

**Adjacency rationale:** The suites' surfaces are the runner's output and the golden files themselves. The one judgment this file adds over the backbone is the refresh discipline (when a golden may change and who approves), which is process recorded here, not a product surface.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Golden render comparison suite | D01 T03 §1, D00 T03 §2 | [ ] |
| 2 | §2 | Text and find verification tables | D03 T01 §3 | [ ] |
| 3 | §3 | Conversion round-trip suite | D04 T02 §4, D05 T01 §3 | [ ] |
| 4 | §4 | Performance targets harness | D02 T02 §3, D00 T01 §5 | [ ] |

## 1. Golden render comparison suite

Deterministic rendering made contractual: every `tests/corpus/render/` file renders at stated zooms and rotations through `recto-cli`, compared pixel-exactly against committed goldens (the plan's battery already proves worker-count independence; this suite pins the pixels themselves). Goldens refresh only through a named command that rewrites them, and a refresh is its own commit whose body states why pixels legitimately changed (stated `recto-pdf` rendering fix), reviewed like code.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write the suite in `recto-tests` (`Goldens.*`): render through the same code path `recto-cli render` uses, compare against `tests/corpus/render/goldens/`, and on mismatch write the actual and a diff image to the test output folder. Done when: a one-pixel planted change in a golden fails the suite naming the page and zoom.
2. Add the refresh command (`recto-tests` flag or a `tools/refresh-goldens` entry in the test project) and the refresh rule to `tests/corpus/README.md`. Done when: refreshing is one command whose diff is reviewable file by file.

- [ ] Pixel-exact golden suite over the render corpus. Done when: the suite is green on x64 and ARM64 in CI (any platform pixel divergence is a finding, not a tolerance). Cheaper substitute: perceptual tolerance that lets slow drift pass unnoticed.
- [ ] Refresh command and rule. Done when: a no-op refresh produces an empty git diff.
- [ ] Commit: `test: golden render comparisons with a disciplined refresh`

**Test checkpoint:** `recto-tests --gtest_filter=Goldens.*` green on both architectures, planted-pixel failure demonstrated, no-op refresh diff empty (quoted).

## 2. Text and find verification tables

M3's done-when made durable: per text-corpus file, the expected extracted text (D03 T01 §1's fixtures), plus find tables (query, options, expected total, expected per-page counts, first-hit context) exercised through the engine and, at services level, through the find walk order. Tables live as data files beside the corpus so adding a case is adding rows, and the unscanned-versus-scanned (pre- and post-OCR) distinction is a first-class expectation.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `tests/corpus/text/expectations/` data (one file per corpus document: extraction text, find table) and the `TextTables.*` suite consuming them. Done when: every text-corpus file has expectations and the suite is green.
2. Cover the recorded normalization rules with adversarial rows (ligatures, soft hyphens, whole-word at punctuation, case folding beyond ASCII). Done when: each rule D03 T01 §3 recorded has a row that fails if the rule is inverted (spot-proven by mutation in the section's evidence).

- [ ] Expectation tables for the whole text corpus. Done when: adding a corpus file without expectations fails the suite. Cheaper substitute: assertions buried per-test in C++ that nobody grows.
- [ ] Normalization adversarial rows. Done when: the ligature and whole-word rows distinguish the recorded rules from their opposites.
- [ ] Commit: `test: text extraction and find verification tables`

**Test checkpoint:** `recto-tests --gtest_filter=TextTables.*` green under ASan with per-file coverage counts quoted.

## 3. Conversion round-trip suite

Plan section 7's M4 rule (each conversion round-trips the corpus with no errors) and M5's verification halves, as one suite over `tests/corpus/convert/`: every converter (images, images-to-PDF, text, searchable-PDF OCR, merge, split, reorder set, encrypt and decrypt, native `DocxWriter` DOCX validity, built-in OpenXML/RTF `Office -> PDF`, plus optional Office COM and LibreOffice paths where the host provides them) runs its applicable files, and outputs verify by the strongest cheap oracle per kind (render equality, info equality, byte fixture, unzip-and-parse, permission readback). Optional external-converter legs (Office COM, LibreOffice) skip with a named line when absent, never silently, while the built-in OpenXML/RTF leg always runs on every host.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `RoundTrips.*` driving the services-level converters (not the UI) over the convert corpus with per-converter oracles. Done when: the suite enumerates converter-by-file applicability from data, so a new corpus file joins every applicable leg automatically.
2. Wire the optional external-converter legs with explicit skip lines naming the missing external converter while always running the built-in OpenXML/RTF engine, and run the full matrix on hosts that have Office or LibreOffice (results recorded in D04 T04 §4's comparison). Done when: CI output states which legs ran and which skipped, per run.

- [ ] Round-trip matrix over the convert corpus. Done when: a converter regression (planted wrong page order in split) fails exactly its leg. Cheaper substitute: asserting only that outputs exist and are non-empty.
- [ ] Explicit external-converter skips with built-in OpenXML always active. Done when: a no-Office CI run prints the skip lines for external converters while passing the built-in OpenXML leg.
- [ ] Commit: `test: conversion round-trip suite over the convert corpus`

**Test checkpoint:** `recto-tests --gtest_filter=RoundTrips.*` green under ASan with the applicability matrix and skip lines quoted.

## 4. Performance targets harness

Plan section 8 as measurements, not aspirations: a harness (CLI-driven for engine timings, app-driven for presentation timings) measuring first-page-visible on a generated 100-page text PDF, scroll frame times over a scripted trace, zoom preview and sharp-tile latency, memory with a 500-page document open, and cold start to empty window, each reported against its target on the D00 T01 §5 reference machines. The harness records dated results under `docs/perf/`; CI runs a smoke subset with loose bounds (shared runners are not reference machines, stated plainly) while the binding numbers come from the named machines.

**Phase:** 2
**Needs:** windows-host, display-session, attended
**Surface:** no

**Build order:**

1. Build the measurement harness and the generated benchmark documents (100-page text, 500-page mixed) under corpus provenance rules. Done when: one command produces the five numbers with machine, build and date stamped.
2. Record the M2 baseline on both reference machines in `docs/perf/`, and state per target whether it is met, missed with an owning section filed, or pending a later milestone's surface. Done when: M2's done-when ("targets in section 8 met on the reference machine") is quotable target by target.

- [ ] Harness producing the five target numbers. Done when: repeat runs on one machine agree within noise the harness itself reports (median and spread). Cheaper substitute: stopwatch anecdotes in review findings, unrepeatable by the next reader.
- [ ] Dated reference-machine baselines with misses filed to owners. Done when: no target is silently unmeasured.
- [ ] Commit: `test: performance harness for plan section 8 targets`

**Test checkpoint:** the `tests/perf/` harness output for both machines quoted in findings against each target, with repeatability spreads and owners for any miss.

## Verification

- Each milestone done-when that names a suite here (M1 goldens' determinism adjacency, M3 search completeness, M4 round-trips, M2 targets) quotes this file's suites.
- Golden refreshes in history each carry their stated reason; a refresh without one is a review defect.
- `python scripts/todo-graph.py validate` passes.
