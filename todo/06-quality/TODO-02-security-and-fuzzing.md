---
schema_version: 1
id: security-and-fuzzing
domain: 06-quality
status: active
title: "TODO-02 -- Hostile-Input Rules, Fuzz Operations and the Broker Design"
track: M6
---

# TODO-02 -- Hostile-Input Rules, Fuzz Operations and the Broker Design

> **Goal:** Plan section 9 enforced as a system, not as intentions: the hostile-input rules audited across every surface that touches untrusted files, the fuzz operation running continuously from M1's targets with a triage loop that files crashes as owned work, and the post-v1 sandboxed broker designed on paper against the `recto-pdf` boundary that was built for it.

> **Current state:** Measured 2026-10-14. No code exists. The rules are plan section 9 and D12 to D13: every PDF and Office file is hostile; `recto-pdf` contains no JavaScript interpreter ( `/JS` and `/JavaScript` dictionaries are ignored); Office macros are ignored by the built-in OpenXML parser and forced off on external converters; web and file links ask before opening; PDF launch actions never execute; v1 parses in-process with the AppContainer broker planned after v1 behind the `recto-pdf` boundary. D01 T03 §2 builds the targets and CI smoke this file operates; the per-surface rules land in D02 T02 §4 (links), D04 T04 (macros and XXE/ZIP-bomb defenses) and D01 T01 (PDF parser bounds), and §1 here audits them as one list before ship.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D12, D13), 3.1 (the boundary the broker moves), 9 (the whole security posture).
- -> XREF: D01 T01 §2 -- the `recto-pdf` parser, stream-filter, and error boundary the §1 audit reads first; audit findings against `recto-pdf` land as sections in that file's domain.
- -> XREF: D01 T03 §2 -- the fuzz targets and seed corpus this file's operation runs, grows and triages.
- -> XREF: D04 T04 §2 -- the macro-suppression (`msoAutomationSecurityForceDisable` and `vbaProject.bin` ignore) and safe-subprocess (`CreateProcessW`) proofs re-run by the §1 audit here.

## Outcome

- Every plan section 9 rule has a named test or a searched proof, gathered in one audit record a reviewer can re-run.
- Fuzz crashes become minimized corpus files plus owned sections, never chat messages; the seed corpus only grows.
- The broker design record exists before v1 ships, so the post-v1 move is an execution of a decided design, not a redesign under pressure.

**Adjacency:** all=not-applicable (an assurance program over other files surfaces; its records are the audit document, the fuzz corpus and the design record, none of them product surfaces)

**Adjacency rationale:** Security here is a property audited across owning sections, in the same shape as D03 T02's accessibility audit. The permissions key deserves one sentence: Recto has no user-role permission system; its refusal boundaries (links, macros, launch actions) are owned and exercised at D02 T02 §4 and D04 T04, and section 1 here verifies them in place rather than re-owning them.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Hostile-input rules audit | D02 T02 §4, D04 T04 §2 | [ ] |
| 2 | §2 | Continuous fuzz operation and crash triage | D01 T03 §2 | [ ] |
| 3 | §3 | Post-v1 broker process design record | §1 | [ ] |

## 1. Hostile-input rules audit

One pass, one record (`docs/security-audit.md`), rule by rule from plan section 9 and D12: no JavaScript engine linked anywhere in `recto-pdf` or dependencies (symbol-level check on built libraries and test that `/JS` actions are ignored), macros inert across built-in OpenXML (`vbaProject.bin` ignored), Office COM, and LibreOffice paths (the D04 T04 marker tests re-run), links ask and launch actions inert (D02 T02 §4's crafted files re-driven), `recto-pdf` containing every parse error as a `Result<T>` status over the broken corpus plus the fuzz seed corpus with decompression-ratio and recursion bounds enforced, and no path where document content reaches a shell, a script engine or an auto-opened handler. Findings become sections through `add-todo` in their owning domains; this section stamps on the audit's completeness, with critical findings filed and named, not on their absence.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write the audit checklist from plan section 9 verbatim (one row per rule: the rule, its owning section, its proof command or search) and execute every row against the current build. Done when: each row carries quoted evidence or a filed owner.
2. Commission one adversarial read of the `recto-pdf` parser, OpenXML reader, and job boundaries by a reviewer who did not write them, over the same checklist. Done when: its findings are in the audit record with dispositions.

- [ ] `docs/security-audit.md` with per-rule quoted evidence. Done when: the JavaScript and decompression-bomb rows carry symbol-level and test-level evidence on the built `recto-pdf.lib`. Cheaper substitute: rereading the plan and asserting compliance.
- [ ] All findings filed with owners through `add-todo`. Done when: the audit record and `query stats` agree on what was filed.
- [ ] Commit: `security: hostile-input rules audit against plan section 9`

**Test checkpoint:** every checklist proof command re-runs green or names its filed owner (the audit record is the quote), and `recto-cli` over `tests/corpus/broken/` plus seeds exits cleanly per file.

## 2. Continuous fuzz operation and crash triage

The operating loop over D01 T03 §2's targets: scheduled longer runs beyond the CI smoke (nightly local or scheduled workflow with an explicit budget), corpus minimization and merging back into `tests/corpus/seeds/` under provenance rules (fuzzer-derived files are synthetic and recorded as such), and the triage rule: every unique crash reproduces through `recto-cli`, minimizes, joins the appropriate corpus folder, and files as a section in the owning domain with the input attached by path.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Stand up the scheduled run (the workflow or local schedule this section chooses and records) with artifacts: findings, coverage summary, corpus deltas. Done when: two consecutive scheduled runs complete and their summaries are in the record.
2. Write the triage runbook into `fuzz/README.md` (reproduce, minimize, classify owning `src/pdf` or `src/core` module, file, regression-test) and prove it once end to end on a planted crash input kept outside the seeds. Done when: the planted crash completes the loop as a corpus file plus a filed section that a fix then closes.

- [ ] Scheduled fuzz runs with recorded summaries. Done when: the schedule survives a red run (a found crash does not silently end the operation). Cheaper substitute: the M1 one-hour run treated as a lifetime proof.
- [ ] Triage runbook proven on a planted crash. Done when: the regression test naming the minimized input exists and fails without its fix.
- [ ] Commit: `fuzz: scheduled operation with a proven crash triage loop`

**Test checkpoint:** the planted crash reproduces through `recto-cli` from its minimized corpus path, its artifacts quoted (repro command, minimized path, filed ref, regression test), and two scheduled summaries on record.

## 3. Post-v1 broker process design record

Plan D13 and section 9's last bullet, designed before it is needed: move `recto-pdf` into a broker process in an AppContainer with no file or network access, the app passing file bytes or handles in and receiving `DisplayList`s, `PageText`, and `Bitmap` tiles over a shared-memory channel. The record (`docs/specs/YYYY-MM-DD-broker-process-design.md` per repo convention for specs) decides: the IPC surface (which `recto::pdf::*` calls cross, how display lists and bitmaps transfer without copies where it matters), actor and job placement, capability and handle brokering for file access, crash containment and restart semantics, and the performance cost model against plan section 8. No code in v1; the design's job is to keep D01's boundary honest by proving the move is contained as claimed.

**Phase:** 7
**Needs:** owner-decision
**Surface:** no

**Build order:**

1. Inventory the true `recto-pdf` public surface as built (every `recto::pdf::*` entry, its ownership transfers) and classify each call as broker-side, app-side or split. Done when: no call requires a mutable `Document` object on both sides, or the exceptions are named as design costs.
2. Write the design record with the IPC and shared-memory layout, AppContainer capability set, restart semantics and the migration sequence from the in-process engine. Done when: the owner approves it as the post-v1 direction (recorded decision with reversal cost) or rejects it with the alternative recorded.

- [ ] `recto-pdf` surface inventory against the as-built headers. Done when: generated from `src/pdf/recto_pdf.h` by script or table-checked against it, not from memory. Cheaper substitute: designing against an imagined boundary instead of the shipped one.
- [ ] Approved design record in `docs/specs/`. Done when: D13's "contained change" claim is either substantiated by the inventory or corrected in the plan.
- [ ] Commit: `docs: broker process design for post-v1 sandboxing (D13)`

**Test checkpoint:** the inventory regenerates from `src/pdf` headers with an empty diff against the record's table, and the owner decision is recorded (removing this section's `owner-decision` Need at stamp time).

## Verification

- Plan section 9 is auditable end to end from `docs/security-audit.md` alone.
- The fuzz operation has run on schedule with its triage loop proven; the seed corpus state is committed.
- `python scripts/todo-graph.py validate` passes.
