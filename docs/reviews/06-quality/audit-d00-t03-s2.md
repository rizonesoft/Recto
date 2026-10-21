# Audit - D00 T03 §2: RGBA pixel comparison and SSIM helpers

- **Audit section:** `D06 T05 §10` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T03 §2` (`docs/reviews/d00-t03-s2.json`, earlier process)
- **Date:** 2026-10-21

## Checkpoint re-run

At `5db9242`, Release x64 `recto-tests.exe` built from this tip's sources (`.\build.cmd` during the D06 T05 §9 audit, 0 warnings in all four configurations): `recto-tests.exe --gtest_filter=Support.*` -> `[  PASSED  ] 10 tests.` (Support.CorpusPathResolution, Support.TempDirCleanup, Support.TempDirLongPathAndReadOnly, Support.PixelComparison, Support.PixelStridePadding, Support.SsimCalculation, Support.Sha256Hex, Support.ResultMacros, Support.AsanActiveWhenEnabled, Support.AsanInstrumentation). The checkpoint's "all 8 support tests" is now 10: later sections added tests to the suite; every one passes.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T18:21:09Z
- Effort: medium
- Lines: 32
- Digest: 15a0765af5dafb91

### adversarial: needs-attention

1. **P2** `tests/recto-tests/test_support.cpp:693`: Partial SSIM blocks receive the same weight as full 64-pixel blocks, making the `0.99` gate disproportionately sensitive to image edges. Reproduce with two opaque-black 9×9 buffers: change one actual pixel’s RGB to `(1,1,1)`. At `(8,8)`, the calculated score is `0.966677774075`; at `(0,0)`, it is `0.999924928772`. The identical one-level error fails or passes solely because the corner block contains one pixel. These values were verified by evaluating the implementation’s equations, not by running an added C++ test.

2. **P3** `tests/recto-tests/test_support.cpp:583`: The diagnostic attaches the image-wide maximum delta to the first mismatching pixel. Reproduce with two black pixels, changing their actual red channels to `1` and `255`. The message names `(0,0)` and its one-level difference but reports `delta = 255`. Label this value as the global maximum or report the first pixel’s delta separately.

### consistency: approve

No additional findings.

### integration: advisory

Finding 1 affects the promised flattening and conversion threshold checks. No current rendering-helper consumer was found beyond the support tests; this audit does not establish a shipped rendering regression.

### source-defect: approve

No additional findings.

### design: advisory

Not applicable: this section owns no user surface.

### record: needs-attention

3. **P3** `todo/00-workspace/TODO-03-test-backbone.md:97`: The ticked `Done when:` still names LICENSE digest `0c2b2577…`, while the current file and assertion use `3972dc97…`. Reproduce with `Get-FileHash LICENSE -Algorithm SHA256` and compare `support_tests.cpp:510`. Commit `97d8485` legitimately updated the assertion after the LICENSE restoration, but the contract lacks the corresponding correction.

4. **P3** `docs/reviews/00-workspace/D00-T03-s2.md:27`: The evidence overstates coverage: it says byte-count overflow checks and streaming file chunking are tested. `Support.PixelComparison` tests zero width and a short buffer; `Support.PixelStridePadding` tests an undersized stride. Neither exercises multiplication overflow. `Support.Sha256Hex` hashes LICENSE and a nonexistent file, without a multi-chunk file case. Inspect those tests to reproduce the evidence mismatch.

Checkpoint verification limitation: `Support.*` exited **1**, with seven passes and three `CreateDirectoryW failed in TempDir` failures under this read-only sandbox. All five section-owned tests passed. Consequently, the audit header’s ten-pass result was not independently reproduced here; these sandbox failures are not evidence of a product regression. `PdfError.ResultValueAndError` also passed. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 1

Gates: `.\build.cmd` -> 4 x `0 Warning(s)`, 4 x `0 Error(s)`, 2 x `[  PASSED  ] 102 tests.`. Against the old `test_support.cpp` the changed tests fail (`Support.PixelComparison`, `Support.SsimWeighsEdgeBlocksByArea`, `Support.RgbaDiffReportsFirstAndMaxDelta`); Release x64 with the fix passes 102.

- Finding 1 (P2): `compute_ssim` weighs every block by its pixel area; one changed pixel in a 9x9 image now scores within 0.002 at the corner and at the origin, both at least 0.99 (`Support.SsimWeighsEdgeBlocksByArea`; the old corner score was 0.9667).
- Finding 2 (P3): `PixelDiffResult` gains `first_delta`, and the message reads `delta = <first pixel's>, max_delta = <global>` (`Support.RgbaDiffReportsFirstAndMaxDelta`; `Support.PixelComparison`'s exact message updated).
- Finding 3 (P3): a dated audit note in `D00 T03 §2` records that its LICENSE digest is stale since `97d8485`; the stamped text keeps its wording.
- Finding 4 (P3): the paths the earlier review claimed as tested now are: `Support.RgbaSizeOverflowRefused` (a stride whose buffer size cannot be represented throws, in both `compare_rgba` and `compute_ssim`) and `Support.Sha256FileStreamsAcrossChunks` (two full 64 KiB chunks plus a partial one hash as the whole buffer). Both already behaved correctly, so they pass on the old code as well.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T18:31:58Z
- Effort: medium
- Lines: 28
- Digest: 7ec95ba729af5199

### adversarial: needs-attention

1. **P3** `tests/recto-tests/test_support.cpp:633`: The new comment incorrectly promises that a changed pixel scores the same wherever it sits. Reproduce with black 9×9 images, changing one pixel’s RGB to `(6,6,6)`: the current equations yield approximately `0.989543` at `(8,8)` and `0.991538` at `(0,0)`, straddling the `0.99` threshold. Area weighting does not remove dependence on block statistics. Correct the comment and the equivalent claim at `support_tests.cpp:486`. These values were verified by evaluating the equations, not an added C++ test.

### consistency: approve

No additional findings.

### integration: approve

No additional findings.

### source-defect: approve

No additional findings.

### design: advisory

Not applicable: no user surface.

### record: needs-attention

2. **P3** `tests/recto-tests/support_tests.cpp:521`: The new overflow regression test does not distinguish overflow rejection from ordinary short-buffer rejection. Removing the overflow guard at `test_support.cpp:465` leaves both assertions passing: unsigned arithmetic wraps the overflowing buffer’s required size to zero, but the other buffer still requires 12 bytes and is empty, causing the same `std::invalid_argument`. Consequently, `docs/reviews/06-quality/audit-d00-t03-s2.md:59` overstates the regression protection. Assert the overflow-specific diagnostic or otherwise distinguish the rejection paths. This conclusion follows from static inspection and verified unsigned arithmetic; no files were mutated.

Checkpoint limitation: the existing Release x64 binary ran 14 `Support.*` tests, with 10 passes and four `CreateDirectoryW failed in TempDir` failures under the read-only sandbox, including the new streaming-hash test. The recorded clean checkpoint and four-configuration build were therefore not independently reproduced. These environmental failures are not product findings. `PdfError.ResultValueAndError` passed. No files were modified.

HIGHEST: P3
OVERALL: needs-attention

## Fixes after round 2

Gates: `.\build.cmd` -> 4 x `0 Warning(s)`, 4 x `0 Error(s)`, 2 x `[  PASSED  ] 102 tests.`.

- Finding 1 (P3): the `compute_ssim` comment and the test comment no longer claim location independence: area weighting stops an edge block from outweighing its area, while scores still depend on each block's statistics.
- Finding 2 (P3): `Support.RgbaSizeOverflowRefused` now asserts the overflow refusal's own message. With the guard replaced by a no-op it fails ("Which is: "RGBA buffer is smaller than required for dimensions and stride""), so it can no longer pass on the short-buffer path.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T18:36:16Z
- Effort: medium
- Lines: 28
- Digest: 3bf27c78a4752141

### adversarial: approve

No new findings.

### consistency: approve

No new findings.

### integration: approve

No new findings.

### source-defect: approve

No new findings.

### design: advisory

Not applicable: this section owns no user surface.

### record: advisory

The recorded clean checkpoint was not independently reproduced. The existing Release x64 binary ran 14 `Support.*` tests: 10 passed and four failed with `CreateDirectoryW failed in TempDir` under the read-only sandbox. Exit code: `1`. These environmental failures do not establish a product defect.

No rebuild or mutation test was performed. Binary provenance and the reported four-configuration build remain unverified. No files were modified.

HIGHEST: none
OVERALL: approve

## Outcome

- The stamp of `D00 T03 §2` is confirmed with corrections: every `Support.*` test passes (14 now) and all four configurations build with 0 warnings. The audit area-weighted SSIM, split the first-pixel and global deltas in mismatch messages, and gave the overflow refusal and multi-chunk hashing real tests (the overflow one mutation-checked). No downstream stamp is suspect.