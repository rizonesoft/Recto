# Audit - D00 T01 §5: Reference benchmark machines recorded

- **Audit section:** `D06 T05 §4` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T01 §5`, stamped earlier (`docs/reviews/d00-t01-s5.json`, 3 rounds under the earlier process)
- **Date:** 2026-10-21

## Checkpoint re-run

At `51d905d`: `python scripts/todo-graph.py validate` -> `valid: 677 sections`; `plan --check` -> `plan current`; `docs/reference-machines.md` holds 0 `TODO` or `TBD` tokens. The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T15:08:44Z
- Effort: medium
- Lines: 27
- Digest: 699042c8f6bd399a

## adversarial: needs-attention

1. **P3** `docs/reference-machines.md:60`: The fingerprint procedure cannot reproduce the complete recorded baseline. Lines 63-72 omit physical disk capacity, Windows SDK and Windows App SDK versions, and MSBuild version, although section 1 records these values. Reproduce by comparing the query’s selected fields with rows 19-22. Someone following this procedure cannot verify all recorded storage and toolchain specifications.

## consistency: approve

## integration: needs-attention

2. **P3** `docs/reference-machines.md:40`: Benchmark ownership references are stale throughout the document. `D01 T03 §1` owns command dispatch and `info`; `bench` belongs to §3. The repeatedly cited `D06 T01 §4` now owns text normalization and regex tests; performance instrumentation and baseline recording belong to §§7-8. Reproduce by comparing lines 5, 30-36, 40 and 56 with `todo/01-engine/TODO-03-cli-and-fuzz.md:112` and `todo/06-quality/TODO-01-corpus-and-suites.md:126,196,224`. Following these references directs implementers to unrelated contracts.

## source-defect: advisory

Live hardware verification was unavailable: the attempted processor, operating-system and video-controller CIM queries returned `Access denied`. The recorded hardware values therefore remain unverified in this audit.

## design: advisory

Not applicable. This section owns no user surface.

## record: needs-attention

3. **P3** `docs/reviews/00-workspace/D00-T01-s5.md:34`: The final review claims that the fingerprint commands reproduce “every value” in section 1. Finding 1 demonstrates that this evidence claim exceeds what those commands output. Reproduce by comparing the commands’ selected fields with the recorded table.

4. **P3** `todo/00-workspace/TODO-01-repo-and-build.md:171`: The stamped contract says `recto-cli bench` “landed in D01 T03 §3”, but that section remains unchecked and `src/cli/main.cpp:8` implements only a scaffold that prints its ABI and returns success. Reproduce by inspecting those two files. The contract incorrectly describes planned functionality as delivered.

The checkpoint claim in `docs/reviews/06-quality/audit-d00-t01-s5.md:9` was independently reproduced: validation reports 677 sections and exits 0, plan checking exits 0, and the reference document contains zero `TODO` or `TBD` tokens. These checks do not detect the findings above.

OVERALL: needs-attention

## Fixes after round 1

- Finding 1 (P3): the fingerprint procedure in `docs/reference-machines.md` now prints disk byte sizes (`Get-Disk` `Size`), the MSBuild version, the installed Windows SDK and the Windows App SDK package version. Running it on 2026-10-21 showed the record had drifted (Visual Studio updated to 18.11.1, and the disk sizes had come from another source); section 1 now holds exactly what the procedure prints: Visual Studio `18.11.1` (`18.11.12315.225`), MSBuild `18.11.1.51303`, MSVC `14.51.36231`, Windows SDK `10.0.26100.0`, Windows App SDK `2.5.1`, disks `1,000,204,886,016` and `512,110,190,592` bytes.
- Finding 2 (P3): the benchmark owners are corrected: `recto-cli bench` is `D01 T03 §3`, the performance harness `D06 T01 §7` and the baseline record `D06 T01 §8`.
- Findings 3 and 4 (P3): the earlier receipt's "every value" claim is superseded by finding 1; a dated audit note in `D00 T01 §5` records that `bench` has not landed (`D01 T03 §3` is open) and names the corrected owners. The stamped checklist and the historical receipt keep their wording.