# 06 Quality

> **Cross-cutting (suites land with their milestones; audit and operations close at M6)**

The evidence programs the milestones close on: golden renders, text and find tables, conversion round-trips, the performance harness against plan section 8, the hostile-input audit, and the continuous fuzz operation. Test infrastructure itself is 00-workspace's backbone file.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-corpus-and-suites.md) | Golden, Text, Round-Trip and Performance Suites | active |
| [TODO-02](./TODO-02-security-and-fuzzing.md) | Hostile-Input Rules, Fuzz Operations and the Broker Design | active |
| [TODO-03](./TODO-03-user-failure-gates.md) | User-Reported Failure Class Gates | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- Suite-level oracles over the corpus, golden refresh discipline, performance measurement
- Plan section 9 audit, fuzz scheduling and triage, the D13 broker design record
- Gates for the failure classes users of PDF tools hit hardest

## Out of scope

- The runner, corpus provenance and ASan setup themselves (00 T03)
- Per-section test checkpoints, owned by their sections

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
