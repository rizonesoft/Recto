---
schema_version: 1
id: security-and-fuzzing
domain: 06-quality
status: active
title: "TODO-02 -- Hostile-Input Rules, Fuzz Operations and the Broker Design"
track: M6
---

# TODO-02 -- Hostile-Input Rules, Fuzz Operations and the Broker Design

> **Goal:** Plan section 9 enforced as a system, not as intentions: the hostile-input rules audited across every surface that touches untrusted files, the fuzz operation running continuously from M1's targets with a triage loop that files crashes as owned work, and the post-v1 sandboxed broker designed on paper against the bridge boundary that was built for it.

> **Current state:** Measured 2026-10-13. No code exists. The rules are plan section 9 and D12 to D13: every PDF and Office file is hostile; MuPDF JavaScript disabled at build; Office macros disabled during conversion; web and file links ask before opening; PDF launch actions never execute; v1 parses in-process with the AppContainer broker planned after v1 behind the existing bridge boundary. D01 T03 §2 builds the targets and CI smoke this file operates; the per-surface rules land in D02 T02 §4 (links), D04 T04 (macros) and D00 T01 §2 (build config), and §1 here audits them as one list before ship.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D12, D13), 3.1 (the boundary the broker moves), 9 (the whole security posture).
- -> XREF: D01 T01 §2 -- the bridge error and logging boundary the §1 audit reads first; audit findings against the bridge land as sections in that file's domain.
- -> XREF: D01 T03 §2 -- the fuzz targets and seed corpus this file's operation runs, grows and triages.

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

One pass, one record (`docs/security-audit.md`), rule by rule from plan section 9 and D12: JavaScript absent from the linked MuPDF (object-level proof on the built libraries, not a config reread), macros inert on both Office paths (the D04 T04 marker tests re-run), links ask and launch actions inert (D02 T02 §4's crafted files re-driven), the bridge containing every parse error as a status over the broken corpus plus the fuzz seed corpus, and no path where document content reaches a shell, a script engine or an auto-opened handler. Findings become sections through `add-todo` in their owning domains; this section stamps on the audit's completeness, with critical findings filed and named, not on their absence.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write the audit checklist from plan section 9 verbatim (one row per rule: the rule, its owning section, its proof command or search) and execute every row against the current build. Done when: each row carries quoted evidence or a filed owner.
2. Commission one adversarial read of the bridge and job boundaries by a reviewer who did not write them, over the same checklist. Done when: its findings are in the audit record with dispositions.

- [ ] `docs/security-audit.md` with per-rule quoted evidence. Done when: the JavaScript row's evidence is a symbol-level check on the built `libmupdf`, not the config file's word. Cheaper substitute: rereading the plan and asserting compliance.
- [ ] All findings filed with owners through `add-todo`. Done when: the audit record and `query stats` agree on what was filed.
- [ ] Commit: `security: hostile-input rules audit against plan section 9`

**Test checkpoint:** every checklist proof command re-runs green or names its filed owner (the audit record is the quote), and `recto-cli` over `tests/corpus/broken/` plus seeds exits cleanly per file.

## 2. Continuous fuzz operation and crash triage

The operating loop over D01 T03 §2's targets: scheduled longer runs beyond the CI smoke (nightly local or scheduled workflow with an explicit budget), corpus minimization and merging back into `tests/corpus/seeds/` under provenance rules (fuzzer-derived files are synthetic and recorded as such), and the triage rule: every unique crash reproduces through `recto-cli`, minimizes, joins the appropriate corpus folder, and files as a section in the owning domain with the input attached by path. A crash inside MuPDF itself is triaged against the pin (fixed upstream, worked around at the bridge only with a `WORKAROUND:` marker and an upstream report, or the pin moves), recorded the same way.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Stand up the scheduled run (the workflow or local schedule this section chooses and records) with artifacts: findings, coverage summary, corpus deltas. Done when: two consecutive scheduled runs complete and their summaries are in the record.
2. Write the triage runbook into `fuzz/README.md` (reproduce, minimize, classify Recto versus MuPDF, file, regression-test) and prove it once end to end on a planted crash input kept outside the seeds. Done when: the planted crash completes the loop as a corpus file plus a filed section that a fix then closes.

- [ ] Scheduled fuzz runs with recorded summaries. Done when: the schedule survives a red run (a found crash does not silently end the operation). Cheaper substitute: the M1 one-hour run treated as a lifetime proof.
- [ ] Triage runbook proven on a planted crash. Done when: the regression test naming the minimized input exists and fails without its fix.
- [ ] Commit: `fuzz: scheduled operation with a proven crash triage loop`

**Test checkpoint:** the planted crash reproduces through `recto-cli` from its minimized corpus path, its artifacts quoted (repro command, minimized path, filed ref, regression test), and two scheduled summaries on record.

## 3. Post-v1 broker process design record

Plan D13 and section 9's last bullet, designed before it is needed: move `recto-bridge` (and with it MuPDF) into a broker process in an AppContainer with no file or network access, the app passing file bytes or handles in and receiving display lists and tiles over a shared-memory channel. The record (`docs/specs/YYYY-MM-DD-broker-process-design.md` per repo convention for specs) decides: the IPC surface (which bridge calls cross, how display lists and pixmaps transfer without copies where it matters), actor and job placement, capability and handle brokering for file access, crash containment and restart semantics, and the performance cost model against plan section 8. No code in v1; the design's job is to keep D01's boundary honest by proving the move is contained as claimed.

**Phase:** 7
**Needs:** owner-decision
**Surface:** no

**Build order:**

1. Inventory the true bridge surface as built (every `rc_*` entry, its ownership transfers) and classify each call as broker-side, app-side or split. Done when: no call requires a document object on both sides, or the exceptions are named as design costs.
2. Write the design record with the IPC and shared-memory layout, AppContainer capability set, restart semantics and the migration sequence from the in-process bridge. Done when: the owner approves it as the post-v1 direction (recorded decision with reversal cost) or rejects it with the alternative recorded.

- [ ] Bridge surface inventory against the as-built headers. Done when: generated from the headers by script or table-checked against them, not from memory. Cheaper substitute: designing against the plan's imagined bridge instead of the shipped one.
- [ ] Approved design record in `docs/specs/`. Done when: D13's "contained change" claim is either substantiated by the inventory or corrected in the plan.
- [ ] Commit: `docs: broker process design for post-v1 sandboxing (D13)`

**Test checkpoint:** the inventory regenerates from `src/bridge` headers with an empty diff against the record's table, and the owner decision is recorded (removing this section's `owner-decision` Need at stamp time).

## Verification

- Plan section 9 is auditable end to end from `docs/security-audit.md` alone.
- The fuzz operation has run on schedule with its triage loop proven; the seed corpus state is committed.
- `python scripts/todo-graph.py validate` passes.
