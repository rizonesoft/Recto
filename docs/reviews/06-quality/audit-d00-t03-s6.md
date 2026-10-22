# Audit - D00 T03 §6: MSVC AddressSanitizer build configuration and CI asan job

- **Audit section:** `D06 T05 §14` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T03 §6` (`docs/reviews/d00-t03-s6.json`, earlier process)
- **Date:** 2026-10-22

## Checkpoint re-run

At `a10db7c`: `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN` -> exit 0, `[  PASSED  ] 102 tests.`, `0 Warning(s)`; `dumpbin /dependents build\bin\x64\Release\recto-tests\recto-tests.exe` lists `clang_rt.asan_dynamic-x86_64.dll`. CI: build run `39697676260` on `09afd65` (main) has `Build x64 Release with AddressSanitizer and run recto-tests: success`. Re-run after the round 1 fix with the same results. The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T00:24:52Z
- Effort: medium
- Lines: 20
- Digest: 4c50a5d72d378f12

### adversarial: approve

### consistency: approve

### integration: approve

### source-defect: needs-attention

1. **P3** `build/build.ps1:123`: Debug staging requires `clang_rt.asan_dbg_dynamic-x86_64.dll`, although current MSVC uses `clang_rt.asan_dynamic-x86_64.dll` for both Debug and Release. This adds an unnecessary dependency and can reject a usable toolchain if the legacy DLL is absent. Reproduce by comparing this branch with the installed `Microsoft.Cpp.EnableASAN.props` and `AddAsanBinariesToCopy` target, which stage the unified DLL. Both DLLs exist locally, so this is not a demonstrated local build failure. [Microsoft runtime documentation](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-runtime?view=msvc-170).

### design: advisory

Not applicable.

### record: advisory

`docs/reviews/06-quality/audit-d00-t03-s6.md:9` still records an unfinished checkpoint. The subsequently produced build log reports zero warnings/errors, and its XML reports 102 tests with zero failures/errors. I independently verified the executable’s ASan DLL import and ran `Support.AsanActiveWhenEnabled` with `RECTO_EXPECT_ASAN=1`, successfully. I did not rerun the build or full suite because they write files. The cited CI result remains unverified because GitHub access failed through the environment’s proxy.

HIGHEST: P3
OVERALL: needs-attention

## Fixes after round 1

`python -m unittest tests.scripts.test_build_ps1` -> `OK` (fails against the script before the fix); `powershell -File build/build.ps1 -Platform x64 -Configuration Debug -EnableASAN` -> exit 0, `[  PASSED  ] 102 tests.`; the Release ASan checkpoint re-run -> exit 0, 102 tests, `0 Warning(s)`.

- Finding 1 (P3): confirmed against Microsoft Learn (ASan runtime and build reference: since VS 2022 17.7 Preview 3, `/MD` and `/MDd` both use `clang_rt.asan_dynamic-{arch}.dll`) and the binary itself: a Debug ASan `recto-tests.exe` imports `clang_rt.asan_dynamic-x86_64.dll`, so the script staged the wrong DLL for Debug and worked only because `PATH` held the right one. `build/build.ps1` now stages exactly the `clang_rt.asan*` DLLs `dumpbin /dependents` reports for each built executable, and refuses an executable that imports none, so the toolset, not the script, decides the name. `tests/scripts/test_build_ps1.py` guards it and runs in `plan-gates`.
- Record advisory: the checkpoint is recorded above.