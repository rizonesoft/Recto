# Audit - D01 T01 §8: Bi-level and image stream filters (`CCITTFaxDecode`, `JBIG2Decode`, WIC `DCT` and `JPX`)

- **Audit section:** `D06 T05 §22` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §8` (`docs/reviews/d01-t01-s8.json`, earlier process)
- **Date:** 2026-10-22

## Checkpoint re-run

At `ab5ea35`: `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*` -> exit 0, `0 Warning(s)`, `Staged for recto-tests: clang_rt.asan_dynamic-x86_64.dll`, `[  PASSED  ] 17 tests.` (17 tests from 1 suite). The checkpoint holds.
