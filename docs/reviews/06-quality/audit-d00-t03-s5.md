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