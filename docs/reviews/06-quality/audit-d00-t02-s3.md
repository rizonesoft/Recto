# Audit - D00 T02 §3: Seeded graph and generated plan

- **Audit section:** `D06 T05 §8` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T02 §3` (`docs/reviews/d00-t02-s3.json`, earlier process)
- **Date:** 2026-10-21

## Checkpoint re-run

At `27f2f05`: `python scripts/todo-graph.py validate` -> `valid: 677 sections`; `plan --check` -> `plan current`; `resolve 'D00 T01 §1'` -> exit 3 (verified). The full suites ran at `221e630` (no file under `scripts/` or `tests/` has changed since: 0 changed): `python -m unittest discover -s tests/scripts` -> `Ran 235 tests`, `OK`; `python -m unittest discover -s scripts/tests` -> `Ran 73 tests`, `OK (skipped=5)`. The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T17:43:50Z
- Effort: medium
- Lines: 26
- Digest: feb392c90ea9f4a2

### adversarial: needs-attention

1. **P1: Release can become ready with safety gates unfinished.** `todo/07-release/TODO-02-store-and-compliance.md:68` does not transitively require `D06 T03 §5` (save-failure injection), `§6` (raw-byte redaction proof), `§12` (release smoke), or `D07 T03 §5` (release-safety gate). Reproduction: load `graph()`, traverse the release section’s `depends`, and check these four references: all are absent. Setting only its ancestors’ `done=True` in memory makes `disposition()` return `0` while those gates remain unfinished. The generated frontier therefore permits release work without required safety evidence.

### consistency: approve

No separate actionable finding.

### integration: needs-attention

Finding **1 (P1)** applies: later safety sections exist but are disconnected from the terminal release dependency chain.

### source-defect: needs-attention

2. **P2: The evaluation corpus is scheduled after the implementation it should inform.** `todo/04-convert/TODO-03-pdf-to-word.md:197` first assembles the 20-document corpus in §6, assigned Phase 5 at line 191 and dependent on §5 at line 56. Foundation plan `docs/plans/2026-10-13-recto-foundation.md:477` explicitly requires assembly **before M5**. Reproduce by resolving `D04 T03 §6` and following its prerequisites: the converter sections precede corpus assembly. Separate early corpus preparation from the later evaluation and tuning gate.

### design: advisory

Not applicable: this section owns no user surface.

### record: advisory

The full checkpoint remains independently unverified. Both unittest suites encountered temporary-directory failures under the read-only sandbox. This is an environment limitation, not evidence of a repository defect. The audit note’s historical passing results cannot substitute for a successful rerun here.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

`validate` -> `valid: 678 sections`; `plan --sync` then `plan --check` current.

- Finding 1 (P1): the release gate `D07 T03 §5` now depends directly on the save-integrity suite `D06 T03 §5` and the raw-byte redaction suite `D06 T03 §6` (it already reached the release smoke `D06 T03 §12`), and gains a rule that fails the gate while any section in phases 0 to 6 other than itself is unstamped, so no v1 feature or quality suite can be left behind the tag; 27 quality sections and many feature sections were not upstream of the gate before, and the rule covers them all.
- Finding 2 (P2): corpus assembly moves from `D04 T03 §6` (Phase 5, after the converter) to a new `D04 T03 §7` (Phase 4: 20 deterministic fixtures, an expected-structure oracle and provenance), which `D04 T03 §2` and `§6` now depend on; `§6` keeps the evaluation, tuning and report with a dated correction, its moved item is struck with a reason, and the foundation plan's section 11 item 3 names both.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T17:51:38Z
- Effort: medium
- Lines: 28
- Digest: e8858f48d3fa9785

### adversarial: approve

No additional actionable finding established.

### consistency: needs-attention

1. **P3: Coverage ownership omits the relocated Word corpus.** `todo/TODO-00-INDEX.md:49` still lists only `D04 T03 §1-§6`. Corpus assembly now belongs to §7, explicitly named by foundation plan line 477. This leaves the stamped coverage sweep incomplete after the round 1 correction. Reproduce by comparing that index row with `python scripts/todo-graph.py resolve "D04 T03 §7"`. Include §7 in the recorded ownership.

### integration: approve

No additional actionable finding established.

### source-defect: approve

No additional actionable finding established.

### design: advisory

Not applicable: this section owns no user surface.

### record: needs-attention

Finding **1 (P3)** also affects the ticked promise that the coverage record names every resolving section.

Checkpoint verification remains incomplete: `validate` reports 678 sections, `plan --check` passes, and resolving `D00 T01 §1` reports exit 3. Both unittest suites encountered temporary-directory errors under the read-only sandbox. These are verification limitations, not established repository defects. The historical passing results at `docs/reviews/06-quality/audit-d00-t02-s3.md:9` do not establish a passing full checkpoint at `c7c20d3`; two tooling files changed after the cited test commit.

HIGHEST: P3
OVERALL: needs-attention

## Fixes after round 2

- Finding 1 (P3): the root index coverage row now names `D04 T03 §1-§7`.
- Record: the full checkpoint re-run at `c7c20d3` (the tip after round 1; this round only edits the index): `python -m unittest discover -s tests/scripts` -> `Ran 238 tests`, `OK` (the three new pre-commit hook tests included); `python -m unittest discover -s scripts/tests` -> `Ran 73 tests`, `OK (skipped=5)`; `validate` -> `valid: 678 sections`; `plan --check` -> `plan current`; `resolve 'D00 T01 §1'` -> exit 3.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T17:59:41Z
- Effort: medium
- Lines: 28
- Digest: c8a4698731afe261

### adversarial: approve

No additional actionable defect established at `39ab786`.

### consistency: approve

No additional actionable finding.

### integration: approve

No additional actionable finding. The distribution section produces artifacts; the later safety gate governs tagging. Its ordering does not establish a publication bypass.

### source-defect: approve

No additional actionable finding.

### design: advisory

Not applicable: this section owns no user surface.

### record: advisory

Full checkpoint verification remains limited by the read-only sandbox. Both unittest suites failed on unavailable writable temporary directories, so I cannot independently confirm their recorded passing results.

`validate` reports 678 sections, `plan --check` passes, and resolving `D00 T01 §1` returns exit 3. The coverage row includes `D04 T03 §7`. Comparing `c7c20d3..39ab786` confirms only the root index and audit record changed, supporting the stated absence of subsequent tooling changes.

HIGHEST: none
OVERALL: approve

## Outcome

- The stamp of `D00 T02 §3` is confirmed with corrections: the graph validates, the plan is current, and both tooling suites pass. The audit tied the release gate to every phase 0 to 6 section and to the save and redaction suites, and moved the Word evaluation corpus before M5 (`D04 T03 §7`). No downstream stamp is suspect.