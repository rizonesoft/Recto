---
schema_version: 1
id: pdf-to-word
domain: 04-convert
status: active
title: "TODO-03 -- PDF to Word (DOCX)"
track: M5
---

# TODO-03 -- PDF to Word (DOCX)

> **Goal:** PDF to editable Word through MuPDF's `docx` writer, shipped honestly: labelled as best for text documents (SPEC section 2, principle 4), measured on 20 corpus files, with a recorded decision on whether Recto needs its own layout work (M5's done-when and plan open item).

> **Current state:** Measured 2026-10-13. No code exists. Plan section 5 fixes the mechanism (the `docx` document writer built on Artifex's `extract` library) and the expectation: good for flowing text; multi-column pages, tables and floating images come out rough. D04 T02 §1 provides the writer plumbing and D00 T03 §2 the corpus (including a two-column file for this review). Whether the pinned MuPDF build includes the docx writer and `extract` under Recto's config is checked in §1 before any UI promise.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 2 (DOCX row), 5 (PDF to Word row and its quality note), 7 (M5 done-when), 11 (the layout-work open item).
- `design/SPEC.md` sections 2 (honest conversion), 9.5 (Word document card, options per `explorations/B-Convert.png`: pages, save location, replace-existing).

## Outcome

- A text PDF becomes a `.docx` that opens in Word and LibreOffice with editable, correctly ordered text.
- The 20-file quality review is a committed document with a per-file verdict and the layout-work decision, cited by the release notes.
- The card's hint line tells the truth the review measured, in the SPEC's words where they still fit.

**Adjacency:** all=not-applicable (one converter inside the D04 T01 frame; its list, settings, notifications and reverse are the queue, the Settings save location, completion notices and job cancel owned there)

**Adjacency rationale:** This file contributes exactly one card and one writer to the frame D04 T01 provides; every surrounding surface (queue list, save settings, completion notice, cancel) is declared and owned in that file. The one record this file adds, the quality review, is a committed document under `docs/`, not a product surface.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | DOCX writer wiring and the Word card | D04 T02 §1 | [ ] |
| 2 | §2 | Corpus quality review and the layout decision | §1 | [ ] |

## 1. DOCX writer wiring and the Word card

Verify the pinned MuPDF actually carries the `docx` writer under `build/mupdf-config.h` (the `extract` dependency is the usual casualty of trimmed configs); if a config change is needed, it goes through D00 T01 §2's record as a dated correction there, never a silent local patch. Then register the Word card over the §1 writer plumbing of D04 T02 with the SPEC options (pages, destination, replace-existing) and the honest label "Word document (best for text documents)".

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 with `D-Convert.png` (Word card) and `explorations/B-Convert.png` (its options form).
**Job:** The user can turn a text PDF into an editable Word file. Consumer: Word, LibreOffice and other DOCX readers opening the result.
**Treatment:** MuPDF `docx` writer as shipped, options surfaced where the writer exposes them. Cheaper substitute that fails the checkpoint: a home-grown DOCX from stext paragraphs, which repeats `extract` badly and unmeasured.
**Chrome:** consume the Convert frame, options form shape and writer plumbing. Do not invent a bespoke export page.

**Build order:**

1. Confirm writer availability in the pin (a bridge probe converting one corpus page), recording the config outcome. Done when: `recto-cli`-level conversion (through a test) produces a `.docx` that unzips to valid document XML.
2. Register the card and options; convert the text corpus end to end through the queue. Done when: outputs open in LibreOffice headless (re-export to PDF succeeds) for every good text-corpus file.

- [ ] DOCX conversion through the job frame. Done when: a multi-page text file's output preserves page order and paragraph text against the extraction fixtures (D03 T01 §1), allowing whitespace differences stated in the test. Cheaper substitute that fails the checkpoint: shipping the card before verifying the writer exists in the pin.
- [ ] Honest card label and hint from plan section 5's wording. Done when: the form says what the output is good and bad at before the job runs.
- [ ] Commit: `convert: PDF to Word via the MuPDF docx writer`

**Test checkpoint:** `recto-tests --gtest_filter=Convert.Docx` validates corpus DOCX outputs (unzip, XML parse, LibreOffice re-export where installed) and fixture text comparisons quoted; screenshot of the card and form in findings.

## 2. Corpus quality review and the layout decision

M5's done-when for DOCX is a judgment with evidence: convert 20 corpus files spanning the shapes Recto will meet (single-column reports, two-column papers, tables, floating images, mixed), score each output for text fidelity, reading order, table survival and image placement against a stated rubric, and decide with the owner whether MuPDF's output is the v1 answer or layout work is filed. Corpus additions for the review follow D00 T03 §2's provenance rules. The review's wording feeds the card hint and the release notes verbatim where possible.

**Phase:** 5
**Needs:** attended, owner-decision
**Surface:** no

**Build order:**

1. Assemble the 20-file review set (provenance recorded), convert all through the queue, and score against the rubric in `docs/docx-quality-review.md` with the worst three outputs kept as fixtures. Done when: every file has a verdict a reader can reproduce from the kept outputs.
2. Record the owner decision (ship as-is with the honest label, or file layout work as named sections through `add-todo`) in the review document and as a dated correction against plan section 11's open item. Done when: the card hint in §1 matches the decision's wording.

- [ ] `docs/docx-quality-review.md` with 20 scored files. Done when: the scores distinguish text documents from layout-heavy ones with evidence, not vibes. Cheaper substitute: converting three friendly files and generalizing.
- [ ] Owner decision recorded and any layout work filed with owners. Done when: plan section 11's open item is answered in both places identically.
- [ ] Commit: `docs: DOCX quality review over 20 corpus files with the layout decision`

**Test checkpoint:** the review's conversions re-run clean from the recorded commands, and `python scripts/todo-graph.py resolve 'D04 T03 §2'` no longer waits once the decision is recorded and its Needs removed.

## Verification

- M5's DOCX done-when quotable: 20 files, scored, decision recorded, label honest.
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes.
