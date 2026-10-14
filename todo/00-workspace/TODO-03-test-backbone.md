---
schema_version: 1
id: test-backbone
domain: 00-workspace
status: active
title: "TODO-03 -- Test Backbone: GoogleTest, Corpus and ASan"
track: M0
---

# TODO-03 -- Test Backbone: GoogleTest, Corpus and ASan

> **Goal:** Stand up the test runner, the redistributable test corpus with per-file license provenance, and the AddressSanitizer build job so every M1+ section can write a real failing-then-passing test in `recto-tests` and run engine code under ASan in CI.

> **Current state:** Measured 2026-10-14. `tests/recto-tests` exists from `D00 T01 §3` with three scaffold ABI tests; `tests/corpus/` does not exist yet, and no ASan CI job runs yet.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 2 (GoogleTest via vcpkg manifest, MSVC ASan), 3.2 (`tests/recto-tests/`, `tests/corpus/`), 4.9 (thread-safety battery runs under ASan), 7 (M0 scope), 9 (fuzz crashes become regression files in `tests/corpus/broken/`), 10 (third-party license tracking).
- -> XREF: D01 T03 §2 -- the fuzz targets use `tests/corpus/seeds/` and promote crashes into `tests/corpus/broken/` under the provenance rule established in §2 here.
- -> XREF: D06 T01 §1 -- the golden render, text expectation and conversion round-trip suites in D06 T01 consume the corpus folders and helper APIs created here.

## Outcome

- `recto-tests` has shared fixture helpers (corpus path resolution, temporary directory RAII with clean teardown, pixel buffer comparison, and `PdfResult` assertion macros) ready for M1 to M5 tests.
- `tests/corpus/` holds a small, redistributable set of real PDFs (1-page text, multi-page two-column with outline and links, rotated pages, image-bearing page, scanned image-only page for OCR, AES-256 encrypted PDF with a known password, and intentionally malformed files in `broken/`) with `tests/corpus/PROVENANCE.md` recording the license and origin of every file.
- An AddressSanitizer build configuration (`/fsanitize=address`) compiles `recto-pdf`, `recto-core`, `recto-services` and `recto-tests` and runs as a dedicated CI job on every push.

**Adjacency:** all=not-applicable (test runner infrastructure, fixtures and sanitizer configuration; no user-facing surfaces or product data)

**Adjacency rationale:** The corpus files are test inputs stored in git, not user documents. Provenance is tracked in `tests/corpus/PROVENANCE.md` and audited by a test in §2 so no unlicensed PDF can slip into the repository.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | GoogleTest harness and test fixture utilities | D00 T01 §3 | [ ] |
| 2 | §2 | Redistributable test corpus with provenance check | §1 | [ ] |
| 3 | §3 | AddressSanitizer build and CI job | §1, D00 T01 §4 | [ ] |

## 1. GoogleTest harness and test fixture utilities

Flesh out `tests/recto-tests` beyond the §3 smoke test: add `tests/recto-tests/test_support.h` and `.cpp` providing corpus path lookup (`recto::test::corpus_path("render/simple.pdf")` resolving from the binary location or `RECTO_CORPUS_DIR`), a `TempDir` RAII helper that creates a unique folder under the system temp path and removes it on scope exit, and a deterministic byte-and-pixel comparison helper that reports the first differing coordinate.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `test_support.h` and `test_support.cpp` in `tests/recto-tests/` with `corpus_path`, `TempDir` (using `std::filesystem`, long-path safe `\\?\` handling where needed), and `expect_rgba_equal(actual, expected, width, height, stride)`. Done when: unit tests in `tests/recto-tests/support_tests.cpp` verify that `TempDir` cleans up its directory on destruction and that `expect_rgba_equal` catches a planted single-channel difference at `(x, y)`.
2. Verify `--gtest_filter` and XML output (`--gtest_output=xml:...`) work cleanly from `build/build.ps1`. Done when: `recto-tests.exe --gtest_filter=Support.*` runs only the support suite and exits 0.

- [ ] `test_support.h/.cpp` and `support_tests.cpp` compiled into `recto-tests`. Done when: both positive and planted-mismatch assertions pass in `Support.*`. Cheaper substitute: hard-coding relative paths like `..\..\tests\corpus` that break depending on the working directory.
- [ ] Commit: `test: add corpus path, TempDir and pixel comparison helpers to recto-tests`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Support.*` exits 0 and runs at least 3 tests covering path resolution, temp directory cleanup and pixel diff detection.

## 2. Redistributable test corpus with provenance check

Create `tests/corpus/` with sub-folders `render/`, `text/`, `convert/`, `broken/`, and `seeds/`, populated with small, clean PDFs generated deterministically by a checked-in script (`scripts/build-corpus.py` or a small generator) or taken from CC0 / public-domain sources, plus `tests/corpus/README.md` and `tests/corpus/PROVENANCE.md` listing every file, its SHA-256, its license, and what feature it exercises (classic XRef, compressed XRef stream, incremental update, multi-page text, outline, internal/external links, 90/180/270 rotation, embedded JPEG/PNG, scanned raster page for OCR, AES-128 and AES-256 encryption with password `"recto-test"`, and truncated/corrupt headers in `broken/`). Add a `Corpus.Provenance` test in `recto-tests` that walks `tests/corpus/` and fails if any file on disk is missing from `PROVENANCE.md` or has a mismatched SHA-256.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author the deterministic corpus generator and populate `tests/corpus/{render,text,convert,broken,seeds}/` along with `tests/corpus/README.md` and `tests/corpus/PROVENANCE.md`. Keep total corpus size under 5 MB. Done when: every category above has at least one representative file and `PROVENANCE.md` lists the SHA-256 and license of each.
2. Add `Corpus.Provenance` in `tests/recto-tests/corpus_tests.cpp` verifying that the set of files under `tests/corpus/` (excluding `README.md` and `PROVENANCE.md`) matches the table in `PROVENANCE.md` 1-for-1 by path and SHA-256. Done when: dropping an unlisted dummy file into `tests/corpus/render/` fails `Corpus.Provenance` with the unlisted path named, and removing it restores green.

- [ ] `tests/corpus/` populated (`render/`, `text/`, `convert/`, `broken/`, `seeds/`) with `README.md` and `PROVENANCE.md`. Done when: total size is under 5 MB and every file has a license and SHA-256 entry. Cheaper substitute: committing random PDFs scraped from the web without license provenance, which violates `GPL-3.0-or-later` distribution rules.
- [ ] `Corpus.Provenance` test enforces 1-for-1 coverage and SHA-256 match. Done when: planted-unlisted-file failure is demonstrated in review notes and the clean tree passes.
- [ ] Commit: `test: seed redistributable PDF test corpus with automated provenance check`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Corpus.Provenance` exits 0 and reports the verified file count.

## 3. AddressSanitizer build and CI job

Wire `/fsanitize=address` through `Directory.Build.props` when `/p:EnableASAN=true` is passed on `x64` (`recto-pdf`, `recto-core`, `recto-services`, `recto-cli`, `recto-tests`), ensuring the ASan runtime DLL (`clang_rt.asan_dynamic-x86_64.dll`) is on `PATH` or copied beside `recto-tests.exe`, and add an `asan` job to `.github/workflows/build.yml` that builds and runs `recto-tests` under ASan on every push.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Configure `EnableASAN` in `Directory.Build.props` / `Directory.Build.targets` (disabling incremental linking `/INCREMENTAL:NO` and debug edit-and-continue when ASan is active, per MSVC requirements) and add `-EnableASAN` support to `build/build.ps1`. Done when: `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN` compiles all engine and test targets and runs `recto-tests.exe` cleanly.
2. Prove ASan is actually instrumented in `recto-tests.exe` (for example by checking `dumpbin /dependents recto-tests.exe` for `clang_rt.asan_dynamic-x86_64.dll` and temporarily running a death test or planted out-of-bounds read during local verification), then add the `asan` job to `.github/workflows/build.yml`. Done when: CI runs the `asan` job green on `main`.

- [ ] `/p:EnableASAN=true` builds `recto-pdf`, `recto-core`, `recto-services`, `recto-cli` and `recto-tests` cleanly and runs `recto-tests` under ASan. Done when: `dumpbin /dependents` confirms the ASan runtime is linked and `recto-tests` exits 0. Cheaper substitute: setting the MSBuild property without passing `/fsanitize=address` to `ClCompile`, which passes tests without sanitizing anything.
- [ ] `.github/workflows/build.yml` runs the ASan job on every push. Done when: `gh run view` shows the `asan` job green.
- [ ] Commit: `ci: enable AddressSanitizer build and test job for engine and services`

**Test checkpoint:** `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN` exits 0, `dumpbin /dependents` on `recto-tests.exe` lists `clang_rt.asan_dynamic-x86_64.dll`, and the CI `asan` job is green.

## Verification

- `recto-tests` runs both normal and ASan configurations green locally and in CI.
- `Corpus.Provenance` verifies every file under `tests/corpus/`.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
