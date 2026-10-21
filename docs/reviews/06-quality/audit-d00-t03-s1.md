# Audit - D00 T03 §1: test_support corpus path resolver and TempDir RAII helper

- **Audit section:** `D06 T05 §9` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T03 §1` (`docs/reviews/d00-t03-s1.json`, earlier process)
- **Date:** 2026-10-21

## Checkpoint re-run

At `2ee5ad7`, with the Release x64 `recto-tests.exe` built by `.\build.cmd` at `59902fc` (0 files under `src/` or `tests/recto-tests/` changed since): `recto-tests.exe --gtest_filter=Support.CorpusPath*:Support.TempDir*` -> `[  PASSED  ] 3 tests.` (Support.CorpusPathResolution, Support.TempDirCleanup, Support.TempDirLongPathAndReadOnly). The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T18:02:32Z
- Effort: medium
- Lines: 28
- Digest: 10724b825bbcf696

## adversarial: needs-attention

1. **P2: Relative corpus overrides break working-directory independence.** `tests/recto-tests/test_support.cpp:193` accepts `RECTO_CORPUS_DIR` unchanged, and line 203 returns a relative path. Subsequent file access resolves it against the current directory, contradicting the section contract. **Reproduced:** set `RECTO_CORPUS_DIR=tests/corpus`, then run the same Release executable with `--gtest_filter=PdfFilters.FlateDecode` from the repository root and from `build/`. The first exits 0; the second exits 1 with `read_file_bytes: CreateFileW failed`. The override needs a defined, stable base or explicit rejection of relative values.

## consistency: approve

No additional findings.

## integration: needs-attention

Finding 1 affects existing consumers, including `tests/recto-tests/pdf_filters_tests.cpp:66`. The observed failure occurs in a downstream engine test, not merely a hypothetical helper call. Its reproduction and severity are recorded above.

## source-defect: needs-attention

2. **P2: `TempDir::file_path()` does not deliver an extended-length path.** `tests/recto-tests/test_support.cpp:409` returns the ordinary normalized path without `\\?\`. The checkpoint conceals this gap: `support_tests.cpp:255` uses helpers that add the prefix themselves, and line 266 explicitly converts the path before calling Win32. **Reproduction:** on Windows with `LongPathsEnabled` disabled, create the checkpoint’s deep file through `write_file_bytes()`, then call `GetFileAttributesW(temp.file_path(deep_rel).c_str())` directly. The unprefixed path exceeds `MAX_PATH`; converting it with `to_long_path()` permits access. This is a source-derived reproduction, not executed here. The manifest alone does not remove this dependency: Microsoft requires both manifest and registry opt-in for unprefixed long paths. [Microsoft documentation](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)

## design: advisory

Not applicable: this section owns no user surface.

## record: needs-attention

3. **P3: The ticked XML-output pass-through promise is incomplete.** `todo/00-workspace/TODO-03-test-backbone.md:67` promises pass-through parameters for both filtering and XML output. `build/build.ps1:26` exposes only `TestFilter`; line 143 hard-codes the XML destination. **Reproduction by source inspection:** inspect the parameter block and `$xmlOut` assignment. There is no caller-selectable output parameter, despite the item being ticked.

**Verification limitation:** I reran the exact checkpoint. It exited 1 with all three tests failing at `CreateDirectoryW` under this read-only sandbox. Consequently, I could not independently confirm the pass recorded at `docs/reviews/06-quality/audit-d00-t03-s1.md:9`; this environmental failure does not establish a code regression. The claimed absence of source changes since `59902fc` was verified. Fresh x64/ARM64 warning-clean builds were not run. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 1

Gates: `.\build.cmd` -> Debug x64 and ARM64 `0 Warning(s)`, `0 Error(s)`, `[  PASSED  ] 98 tests.`; Release x64 and ARM64 rebuilt with the fix -> `0 Warning(s)`, `0 Error(s)`, `[  PASSED  ] 98 tests.`. The strengthened tests fail on the old `test_support.cpp` (`[  FAILED  ] Support.CorpusPathResolution`, `[  FAILED  ] Support.TempDirLongPathAndReadOnly`) and pass with the fix.

- Finding 1 (P2): `corpus_path()` anchors a relative `RECTO_CORPUS_DIR` at `repo_root()`, so the result never depends on the current directory; `Support.CorpusPathResolution` sets `tests\corpus` while the current directory is a temp folder and expects the repository corpus path.
- Finding 2 (P2): `TempDir::file_path()` returns the `\\?\` form once a path reaches `MAX_PATH - 12` (where plain `CreateDirectoryW` stops), so Win32 accepts it without `LongPathsEnabled`; shorter paths are unchanged, so existing callers are untouched. `Support.TempDirLongPathAndReadOnly` asserts the prefix on the deep path, its absence on a short one, and calls `GetFileAttributesW` on `file_path()` directly.
- Finding 3 (P3): `build/build.ps1` takes `-TestOutputDir <folder>` for the gtest XML reports (default `build\`, relative values taken from the repository root, Windows PowerShell 5.1 compatible); `-TestFilter "Support.*" -TestOutputDir <scratch>\xml-out` built, ran 10 tests and wrote `test-results-x64-Release.xml` there.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T18:10:16Z
- Effort: medium
- Lines: 30
- Digest: 5a9d1b2ecf4b0283

## adversarial: needs-attention

1. **P2**: `tests/recto-tests/test_support.cpp:200` does not anchor every relative override. With the repository on `R:`, setting `RECTO_CORPUS_DIR=C:corpus` makes `repo_root() / base` discard the repository path because the drive names differ. The returned `C:corpus\a.pdf` remains drive-relative, so subsequent reads depend on C’s current directory. **Reproduce:** set that override, switch between two existing C-drive directories, and resolve/read `corpus_path("a.pdf")`. This violates the working-directory independence promise. This is a source-derived reproduction, supported by [Microsoft’s STL implementation](https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/filesystem) and [Windows path semantics](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file).

2. **P3**: `tests/recto-tests/test_support.cpp:409` bypasses long-path conversion when `relative` is empty. **Reproduce:** use a sufficiently deep `TEMP` location so the generated directory exceeds 260 characters, construct `TempDir`, then call `file_path("")`. It returns the unprefixed path despite the corrected nonempty branch returning an extended-length path. Direct Win32 use can consequently fail without long-path opt-in.

## consistency: approve

No additional findings.

## integration: needs-attention

3. **P2**: `tests/recto-tests/support_tests.cpp:263` assumes `"short.bin"` produces a short absolute path. A valid long `TEMP` location makes `file_path("short.bin")` correctly return the extended-length prefix, which this assertion rejects. **Reproduce:** point `TEMP` at an existing writable nested directory approximately 230 characters long, then run `--gtest_filter=Support.TempDirLongPathAndReadOnly`. The generated directory name pushes the complete path beyond the implementation’s 248-character threshold. The checkpoint therefore fails on supported long-temp-path configurations even when the helper behaves correctly. This reproduction is source-derived, not executed here.

## source-defect: needs-attention

Finding 1 applies: Windows drive-relative paths require explicit handling; joining them to a different-drive repository root does not make them absolute.

## design: advisory

Not applicable; this section owns no user surface.

## record: advisory

I reran the exact checkpoint at `5134358`. It exited **1**, with all three tests throwing `CreateDirectoryW failed in TempDir` under the read-only sandbox. This does not establish a code regression, but prevents independent confirmation of the recorded pass.

The claimed absence of changes under `src/` and `tests/recto-tests/` between `59902fc` and `2ee5ad7` checks out. Fresh builds, the 98-test pass, and old-code regression failures were not independently verified. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 2

Gates: `.\build.cmd` -> x64 and ARM64, Debug and Release, 4 x `0 Warning(s)`, 4 x `0 Error(s)`, 2 x `[  PASSED  ] 98 tests.`; against `5134358`'s `test_support.cpp` the new assertion fails ("Expected: (void)recto::test::corpus_path("a.pdf") throws an exception of type std::runtime_error"); with the fix Release x64 passes 98 tests.

- Finding 1 (P2): after anchoring, `corpus_path()` requires an absolute base and throws for a drive-relative override on another drive (`C:corpus` with the repository on `R:`), which no join can anchor; `Support.CorpusPathResolution` sets such a value on a drive other than the repository's and expects the throw.
- Finding 2 (P3): `TempDir::file_path("")` follows the same long-path rule as a non-empty path.
- Finding 3 (P2): the test's plain-path assertions are conditioned on the unprefixed length, so a deep `TEMP` no longer fails a correct helper; the root path gets the same check.