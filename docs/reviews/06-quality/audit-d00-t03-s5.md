# Audit - D00 T03 §5: PROVENANCE.md and the Corpus.Provenance SHA-256 integrity suite

- **Audit section:** `D06 T05 §13` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T03 §5` (`docs/reviews/d00-t03-s5.json`, earlier process, 3 rounds)
- **Date:** 2026-10-22

## Checkpoint re-run

At `8d88cac` (before the round 1 fix, which changes no C++): `.\build.cmd` -> exit 0 (x64 and ARM64, Debug and Release; `[  PASSED  ] 102 tests.` for x64 Debug and Release), run because the test binary predated `31bf898` (the vcpkg baseline merge). Then `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Corpus.*` -> `Corpus.Provenance verified 39 corpus files`, `[  PASSED  ] 4 tests.`, exit 0, both before and after `PROVENANCE.md` was regenerated. The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T23:48:58Z
- Effort: medium
- Lines: 26
- Digest: e113e870ba345571

### adversarial: approve

No actionable defect found in the current file-set and SHA-256 checks.

### consistency: approve

No actionable violation found in the section-owned implementation.

### integration: needs-attention

1. **P2: The provenance generator rejects the repository’s prescribed user-report fixtures.** `scripts/build-corpus.py:3898` rejects every file outside `expected_corpus_files()`, even when correctly registered in `PROVENANCE.md`. Line 3872 also requires the manifest to equal the generator-only manifest. This conflicts with `docs/support/user-bugs.md:7`, which requires adding reproductions under `tests/corpus/user-reports/` and recording their actual origin. **Reproduce:** add a synthetic CC0 reproduction there with a correct manifest row, then run `python scripts/build-corpus.py --check`. It reports an unexpected file and outdated provenance. Regeneration removes the added manifest row. This is a static finding; no fixture was written.

### source-defect: approve

No actionable misreading of the hashing or enumeration APIs found.

### design: advisory

Not applicable; this section owns no user surface.

### record: advisory

2. **P3: The audit checkpoint remains unverified.** `docs/reviews/06-quality/audit-d00-t03-s5.md:9` contains a pending-build statement, not completed checkpoint output. **Reproduce:** read that file and inspect the available executable, dated October 21. My read-only run passed `Corpus.Provenance` and `Corpus.ProvenanceParserRejectsMalformedRows`; the generator’s `--check` verified 39 fixtures. These do not establish the complete `Corpus.*` checkpoint or the four-configuration rebuild. I did not run the temporary-file mutation tests or rebuild under the no-file-modification constraint.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 1

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 54 tests`, `OK` (`test_user_report_reproductions_are_kept_and_verified` fails against the tree before the fix); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`; `Corpus.*` -> `[  PASSED  ] 4 tests.`

- Finding 1 (P2): `tests/corpus/user-reports/` is now a maintainer-curated area beside the generated fixtures. `user_report_rows()` reads its rows from `PROVENANCE.md` in one strict form (a plain file name, its SHA-256, `CC0-1.0`, an Origin naming the issue as `#N`, a Purpose); `--check` verifies each listed file against its SHA-256 and admits it in the directory walk, while an unlisted or altered file still refuses; regenerating carries the rows forward and refuses to write if a listed reproduction is missing or changed, instead of dropping its row. `PROVENANCE.md`'s intro, `tests/corpus/README.md` and `docs/support/user-bugs.md` (with the exact row format) say so.
- Finding 2 (P3): the checkpoint re-run is now recorded above (full build, then `Corpus.*`).

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T00:08:34Z
- Effort: medium
- Lines: 28
- Digest: e0af8a6524f980ee

### adversarial: needs-attention

1. **P2: Regeneration still silently drops user-report provenance rows with whitespace variations.** `scripts/build-corpus.py:3790` skips rows unless they begin exactly `| user-reports/`. The C++ parser accepts leading whitespace and extra cell padding (`tests/recto-tests/corpus_tests.cpp:169`). Regeneration then overwrites the manifest without those rows at `scripts/build-corpus.py:3850`. **Reproduce:** prepend one space to a valid user-report row and regenerate. The reproduction remains on disk but loses its recorded hash, origin and purpose. Verified without writes by mocking manifest reads and capturing `write_atomic`: generation returned successfully with the row absent. Strict formatting should cause rejection before writing, not silent omission.

### consistency: needs-attention

2. **P3: The corpus instructions contradict the new maintainer-curated workflow.** `tests/corpus/README.md:5` still says every fixture is generated; line 37 requires `scripts/build-corpus.py` as every file’s origin. Line 16 and `docs/support/user-bugs.md:7` instead require user-report fixtures to retain their actual origin. **Reproduce:** follow the provenance rule at line 37 for a user-report reproduction. Its generator-only origin fails the new requirement for an issue reference.

### integration: needs-attention

3. **P2: The generator accepts a blank purpose and emits a manifest that neither validator accepts.** `scripts/build-corpus.py:3775` permits whitespace-only purpose text; line 3795 strips it to an empty string without rejecting it. Serialization produces `|  |`, which fails the Python reader’s own regex and the C++ empty-purpose check at `tests/recto-tests/corpus_tests.cpp:254`. **Reproduce:** replace a valid user-report row’s purpose with spaces, then regenerate. Verified in memory: initial parsing succeeds, serialization succeeds, and parsing the generated manifest raises `malformed user-report row`.

### source-defect: approve

No additional finding.

### design: advisory

Not applicable; no user surface.

### record: advisory

The complete checkpoint was not independently rerun because two `Corpus.*` tests create temporary files, contrary to this audit’s no-write constraint. The read-only subset passed both tests and verified 39 files; `python -B scripts/build-corpus.py --check` also passed.

Existing logs corroborate four completed builds, but ARM64 Debug records **4 warnings**, so they do not establish the checklist’s warning-free claim. The recorded full checkpoint remains only partially corroborated in this round.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 2

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 54 tests`, `OK` (the four new variant subtests fail against `09afd65`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P2): every table row whose path cell names `user-reports/`, however it is spaced, is now a user-report row and must match the one strict form; a leading space or padded cell refuses in `--check` and in regeneration instead of being skipped and dropped.
- Finding 3 (P2): the origin and purpose cells may not be blank or padded (`USER_REPORT_ROW`), and the origin must name the issue as `#N`, so a row that parses always serializes back to a row that parses (here and in `Corpus.Provenance`'s empty-cell check).
- Finding 2 (P3): `tests/corpus/README.md` now says only files outside `user-reports/` are generated, and its provenance rule names both kinds of origin.
- Record advisory: the full build's 4 warnings are MSBuild infrastructure, C# `CS1668` from `ROSLYNCODETASKFACTORYCSHARPCOMPILER` while building `recto-tests.vcxproj` for ARM64 Debug (ATL/MFC and .NET ARM64 library folders on `LIB` that are not installed), not compiler warnings from `corpus_tests.cpp`, whose own claim holds. They do break `D00 T01 §3`'s zero-warnings checkpoint, so the cleanup is filed as `D06 T05 §27`.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T00:21:13Z
- Effort: medium
- Lines: 28
- Digest: 805644a26d188747

### adversarial: approve

No new actionable finding.

### consistency: approve

No new actionable finding.

### integration: approve

No new actionable finding.

### source-defect: approve

No actionable finding.

### design: advisory

Not applicable; this section owns no user surface.

### record: advisory

Verification remains partial under the no-file-modification constraint. The read-only C++ subset passed both tests and verified 39 files; `python -B scripts/build-corpus.py --check` passed. In-memory probes confirmed rejection of the reported spacing and blank-cell variants.

The full `Corpus.*` checkpoint and rebuild were not rerun because they write files. Existing logs corroborate four successful builds, including the four ARM64 Debug CS1668 warnings already assigned to `D06 T05 §27`.

HIGHEST: none
OVERALL: approve
