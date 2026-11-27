---
schema_version: 1
id: impossible-sdk-platform
domain: 00-workspace
status: active
title: "TODO-06 -- Impossible SDK as Recto's Platform: Move onto It, Surface by Surface"
frozen: true
---

# TODO-06 -- Impossible SDK as Recto's Platform: Move onto It, Surface by Surface

> **Goal:** Recto calls the operating system only through Impossible SDK, so the same source runs on Windows now and on Impossible OS later with no Impossible OS backend written for Recto (foundation plan D27, owner decision 2026-11-27; the approved design record is `docs/specs/2026-11-27-sdk-as-platform-design.md` in the Impossible SDK repository). `recto-core`, `recto-pdf`, Vellum, `src/app` and `src/cli` draw, read input, open files and run threads through the SDK's `base`, `window`, `draw`, `text` and `gpu` parts instead of Win32, Direct2D, DirectWrite, Direct3D and DirectComposition; Recto's page tiles run on the SDK `gpu` tier with a CPU path; the release is built with the SDK toolchain (clang, libc++ through the SDK `cxx` part) and passes the SDK's link rule, source check and import check in CI. Vellum keeps its own element tree, layout, controls and look; recto-pdf and the viewer keep their own design. Every move lands only within Recto's own pixel tolerance and timing targets, and none of it blocks M0 to M6 on Windows.

> **Current state:** Measured 2026-11-27 at `274f248`. Recto is C++23 on MSVC (`v145`) with modules, exceptions and RTTI, x64 and ARM64 (D14, D15). `src/pdf` (`font.h`, `font.cpp`, `rasterizer.cpp`, `pch.h`) includes DirectWrite, Direct2D and WIC headers; `src/core` (`atomic_write.cpp`, `document_actor.cpp`, `pch.h`) calls Win32 for files and threads; the toolkit is still `src/ui` (`libs/vellum` does not exist until `D10 T04 §1`) on Win32 and DirectComposition; `src/app/pch.h` includes `<windows.h>` and `src/app/main.cpp` calls `CoInitializeEx` and `MessageBoxW` from `wWinMain`; `src/cli` and `src/services` are scaffolds with no Windows calls yet, and `src/services/presentation.*` (the page tile Direct3D 11 presenter) is planned in `D02 T02 §1`. There is no `sdk.json` and no `app.json`. `fuzz/fuzz_smoke.cpp` discards its input and calls only `recto::pdf::abi_version()`; the real `fuzz_open`, `fuzz_filters` and `fuzz_render` targets are `D01 T03 §4`. Caches today are `kMaxActorCachedPages` (32 pages, `src/core/document_actor.h`) and the 512 MB tile cache of `D02 T02 §3`, each with a constant of its own. CI (`.github/workflows/build.yml`) builds x64 and ARM64 with MSVC and runs ASan on x64; nothing runs UBSan or fuzzes inputs. The Impossible SDK repository plans its side of this (the C++ runtime part `cxx`, the opaque `im_window`, the boundary rules with their planted cases, the public `draw` and `text` parts, the `gpu` tier, arm64 Windows and the cache budget call) in its own `todo/` tree; those SDK sections are not duplicated here.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` decision D27 (the dated decision this file executes), D4, D9, D14, D15 and D22 to D26, and section 8 (performance targets).
- Impossible SDK repository `docs/specs/2026-11-27-sdk-as-platform-design.md`: the approved design record this file carries out for Recto, including the boundary rules, "How is a move judged?" and "In what order?".
- Impossible SDK repository guide `docs/guide/own-program.md` (`sdk.json`, `app.json`, `tools/app_build.py`) and `toolchain.json` (clang from the pinned llvm-mingw).
- Impossible OS repository `todo/11-apps/TODO-05-pdf-viewer.md` (20 sections, written for a private seam): reconciled by the Impossible OS side as one transfer item; this file is the Recto side of that transfer and carries the obligations the design record assigns to Recto (platform seam, engine cache budget, fuzz runs).
- `.github/workflows/build.yml`, `build/build.ps1`, `CMakeLists.txt`, `CMakePresets.json`, `fuzz/`, `src/pdf`, `src/core`, `src/ui`, `src/app`, `src/cli`, `src/services`.
- Pin rule (owner, 2026-11-23, restated by the 2026-11-27 record): each SDK feature Recto needs that the SDK lacks is built in the SDK first, by the Recto session that needs it, as a section in the SDK repository; only then does Recto move its `sdk.json` pin. This file never duplicates an SDK section: a Recto section that waits on one names it by feature and SDK repository, never by a cross-reference line, because the SDK tree is outside this graph.
- -> XREF: D00 T04 §8 -- the CMake and Ninja build and the MSBuild parity path that §2 keeps for Recto's own tools while the shipping build moves to the SDK toolchain.
- -> XREF: D01 T02 §8 -- the 32-page `DisplayList` cache whose budget §4 takes from the SDK.
- -> XREF: D01 T03 §4 -- the real libFuzzer entry points that §13 runs under ASan and UBSan for 10 seconds each.
- -> XREF: D10 T04 §1 -- the move to `libs/vellum` that §7 and §8 build on.
- -> XREF: D02 T02 §3 -- the tile texture cache and Direct3D 11 upload that §9 moves onto the SDK `gpu` tier and whose budget §4 takes from the SDK.

## Outcome

- `sdk.json` and `app.json` sit at the repository root, the pin is a full SDK commit, and a check refuses a pin that is not a commit of the Impossible SDK repository or an `app.json` that disagrees with the repository's version or tier.
- No file of `recto-pdf`, `recto-core`, Vellum, `src/app`, `src/cli` or `src/services` includes a Windows, COM or DirectX header or links a system import library; the SDK source check, link rule and import check pass in Recto's CI, and `Recto.exe` and `recto-cli.exe` ship from the SDK toolchain.
- Each surface moved with its Windows tests green before and after, its pictures within Recto's tolerance and its timings no worse than Recto's targets, recorded separately by one harness on one machine.
- Recto's caches take their budget from the SDK, and an oversized document under a bounded budget fails with a typed error and never exhausts the process, in CI.
- Every real fuzz entry point runs 10 seconds under ASan and under UBSan in Windows CI.
- Recto's present ARM64 build stays until the SDK's arm64 target is proved, then moves.

**Adjacency:** all=not-applicable (platform and build migration of existing surfaces; no new record, setting, notice, permission, report or user-visible surface, and every moved surface keeps its owner's own adjacency declaration)

**Adjacency rationale:** This file changes what Recto is built on, not what it does. Settings, lists, documents and undo of every moved surface stay with the section that owns the surface, and the pictures and timings that prove nothing changed are §3's harness. The cache budget (§4) reads its limit from the SDK rather than adding a setting.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `sdk.json`, `app.json` and the SDK pin rule | -- | [ ] |
| 2 | §2 | CI ratchet: SDK toolchain build, source check, link rule and import check | §1, D00 T04 §8 | [ ] |
| 3 | §3 | Move judge: pixels and timings compared separately on one machine | §1 | [ ] |
| 4 | §4 | Cache budget from the SDK, oversized documents fail with an error | §1, D01 T02 §8 | [ ] |
| 5 | §5 | `recto-pdf` onto the SDK: files, memory, fonts, drawing and crypto calls | §2, §3, D01 T02 §6 | [ ] |
| 6 | §6 | `recto-core` onto the SDK: atomic write, threads and time | §2, §3, §5 | [ ] |
| 7 | §7 | Vellum window, input, clipboard and presentation onto the SDK `window` part | §2, §3, D10 T04 §1 | [ ] |
| 8 | §8 | Vellum drawing, text and images onto the SDK `draw` and `text` parts | §7 | [ ] |
| 9 | §9 | Page tiles and `src/services` presenter onto the SDK `gpu` tier, CPU path kept | §8, D02 T02 §3 | [ ] |
| 10 | §10 | `src/app` onto the SDK entry point, no `windows.h`, `CoInitializeEx` or `MessageBoxW` | §6, §7 | [ ] |
| 11 | §11 | `src/cli` onto the SDK entry point and file calls | §5, §6 | [ ] |
| 12 | §12 | Release built and shipped from the SDK toolchain, MSVC kept for tools only | §4, §9, §10, §11 | [ ] |
| 13 | §13 | ASan and UBSan 10-second runs over each real fuzz entry point | D01 T03 §4 | [ ] |
| 14 | §14 | ARM64 kept until the SDK arm64 target is proved, then moved | §12 | [ ] |

## 1. `sdk.json`, `app.json` and the SDK pin rule

The Impossible SDK builds a program from the commit in its `sdk.json` and reads its identity from `app.json` (`tools/new_app.py`, `tools/app_build.py`, `docs/guide/own-program.md`). This section adds the two files and a check, and writes the rule that gives SDK features their owner: a missing SDK feature is built in the SDK by the session that needs it, then the pin moves. It adds no Windows call and moves no code.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Freeze check:** Verify that no file under `src/pdf`, `src/core` or `src/ui` changes, so the actor and tile contract of foundation plan section 4 is untouched.

**Build order:**

1. Add `sdk.json` (`{"sdk": "ImpossibleSDK", "commit": "<40 hex>"}`) and `app.json` (`name`, `label`, `version`, `tier`, `opens`, `needs`) at the repository root in the shape `tools/new_app.py` writes. Done when: the SDK's build tool at the pinned commit (`tools/app_build.py`) reads both files without error.
2. Record how the second executable `recto-cli.exe` is declared, because `app.json` describes one program. Done when: either the SDK supports two programs in one repository and `app.json` lists both, or the gap is filed as an SDK section by this session under the pin rule and named in `docs/sdk-platform.md`.
3. Add `scripts/check-sdk-platform.py` (stdlib) that checks the pin is a full 40-hex commit, `app.json` parses, its `version` equals the version `build/build.ps1` derives (`VERSION` and tags) and its `tier` is an integer. Done when: it exits 0 on the repository and nonzero on each planted fault in `tests/scripts/test_check_sdk_platform.py`.

- [ ] Create `sdk.json` at the repository root with the Impossible SDK commit that holds the `cxx` part and the opaque `im_window`. Done when: the file is valid JSON with a 40-hex `commit` and the SDK repository contains that commit. Cheaper substitute: a branch name or `main`, which does not pin.
- [ ] Create `app.json` at the repository root with `name` `Recto`, `label` `Recto`, the repository version, the tier the SDK reports for the program (Desktop now, the `gpu` tier after §9) and `opens` `["pdf"]`. Done when: the SDK validator accepts it. Cheaper substitute: copying a sample's `app.json` unchanged.
- [ ] Declare what Recto needs from Impossible OS that imports cannot show in `app.json` `needs` as `{"id": "recto-<thing>", "what": "<one sentence>"}` rows. Done when: each row has an id of 4 to 120 characters and Recto never waits on one (the SDK rule). Cheaper substitute: an empty `needs` while the OS plan (`TODO-05`) still lists obligations.
- [ ] Write `docs/sdk-platform.md` with the pin rule (an SDK feature Recto needs is built in the SDK first by this session, then `sdk.json` moves), the list of SDK features Recto waits on and the SDK repository section that owns each by name. Done when: every waiting section of this file appears in the list. Cheaper substitute: prose in the commit message.
- [ ] Add `scripts/check-sdk-platform.py` and `tests/scripts/test_check_sdk_platform.py` (branch pin, short hash, version mismatch, unparsable `app.json`). Done when: `build\python.cmd -m unittest tests.scripts.test_check_sdk_platform` passes and fails when the check is made to accept a branch name.
- [ ] Run `scripts/check-sdk-platform.py` in the `plan.yml` workflow. Done when: a planted short hash fails the job.
- [ ] Commit: `workspace: add sdk.json, app.json and the SDK pin check`

**Test checkpoint:** `build\python.cmd -m unittest tests.scripts.test_check_sdk_platform` passes, and `build\python.cmd scripts/check-sdk-platform.py` exits 0 on the repository and 1 with a short-hash pin.

## 2. CI ratchet: SDK toolchain build, source check, link rule and import check

The SDK's boundary is three things that close the class together: no Windows name on the link line, no Windows type in a public header, and no Windows header in the source. Recto cannot pass them all at once, so a new job runs them now with an exemption list of the files and targets that still break them, and the list only shrinks; each move section deletes its entries, and §12 makes the empty list the shipping gate. This section does not change a line of product code.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Freeze check:** Verify that the job only reads sources and builds a copy, so `src/pdf`, `src/core` and the thread-safety battery are untouched.

**Build order:**

1. Add `scripts/sdk-boundary.py` that runs the SDK's source check (no Windows, COM or DirectX header in the app or its dependencies) over `src/`, `libs/` and `fuzz/` and subtracts `scripts/sdk-boundary-exemptions.json`. Done when: it names each violating file and exits nonzero for a violation not in the list, and for a list entry that no longer violates (so the list cannot go stale).
2. Add a `sdk-build` job to `.github/workflows/build.yml` that builds the SDK toolchain at the `sdk.json` commit, builds Recto's moved targets with it under the link rule and runs the import check (`tools/check_imports.py`) on the result. Done when: the job runs on every code push, reports the targets not yet built as skipped by name, and fails on a built target that breaks a rule.
3. Pin Recto's own dependencies to build from source in this job (the SDK refuses prebuilt objects). Done when: the vcpkg dependencies Recto links are listed with how each is built from source, or filed as an SDK or Recto section if one cannot be.

- [ ] Write `scripts/sdk-boundary.py` and `scripts/sdk-boundary-exemptions.json` (one entry per file with its owning section of this file). Done when: the first run lists every file that includes a Windows, COM or DirectX header today, each owned by one of §5 to §11.
- [ ] Add `tests/scripts/test_sdk_boundary.py` with a planted `#include <windows.h>`, a planted `<d2d1.h>`, and a stale exemption. Done when: each plant produces its own diagnostic and the test fails if the check ignores the list's stale-entry rule.
- [ ] Add the `sdk-build` job to `.github/workflows/build.yml` and pin the SDK checkout to `sdk.json`. Done when: a push shows the job green with the exemption list non-empty and red when a new violation is added.
- [ ] Run the SDK's import check and header check on every moved target's output in that job. Done when: a planted forbidden import in a scratch target fails it.
- [ ] List each vcpkg dependency Recto links with its build-from-source status in `docs/sdk-platform.md`. Done when: no dependency is linked as a prebuilt archive in the `sdk-build` job.
- [ ] Commit: `ci: add the SDK boundary ratchet job and exemption list`

**Test checkpoint:** `build\python.cmd -m unittest tests.scripts.test_sdk_boundary` passes, and the `sdk-build` job is red on a pushed commit that adds `#include <windows.h>` to `src/core` and green after it is removed.

## 3. Move judge: pixels and timings compared separately on one machine

A move lands only when its pictures match within Recto's own tolerance and its timings are no worse than Recto's own targets, and pixels and timings are never mixed in one verdict (design record, "How is a move judged?"). Recto's workload is its conformance set plus opening a 500-page file and rendering page 250. This section builds the harness that every later move section runs before and after.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Freeze check:** Verify that the harness drives `recto-cli` and `recto-tests` through their public interfaces only, so no actor, queue or tile contract changes.

**Build order:**

1. Add `scripts/judge-move.py` (stdlib) with `capture --label <name>` and `compare <before> <after>`: `capture` renders the conformance set (`tests/corpus/render/`, `text/`, `color/`, `images/`, `fonts/`) and a generated 500-page PDF (`scripts/build-corpus.py`) with `recto-cli render`, then times `open` and `render --page 250`. Done when: a capture writes `build/judge/<label>/pixels/` and `timings.json` on the machine it ran on and records the machine.
2. `compare` reports pixels and timings in two separate verdicts: pixels against the tolerance the golden suite already uses (`expect_rgba_equal`, `compute_ssim` in `tests/recto-tests/test_support`, `D06 T01`), timings against the foundation plan section 8 targets and the before capture. Done when: it exits nonzero if either verdict fails and prints which one.
3. Refuse a comparison across machines. Done when: `compare` exits nonzero with a clear message when the two captures name different machines.

- [ ] Write the 500-page generator mode in `scripts/build-corpus.py` (deterministic, no copyright content) and its provenance row in `tests/corpus/PROVENANCE.md`. Done when: `Corpus.Provenance` passes and the file opens with 500 pages in `recto-cli info`.
- [ ] Write `scripts/judge-move.py capture`. Done when: two captures of the same build on one machine compare equal for pixels and within the timing noise of `scripts/test-times.py`.
- [ ] Write `scripts/judge-move.py compare` with separate pixel and timing verdicts and the machine check. Done when: `tests/scripts/test_judge_move.py` covers a changed pixel (pixel verdict fails, timing verdict passes), a slower timing (the reverse) and mixed machines.
- [ ] Record the first capture, taken before any move, as `docs/sdk-platform.md` baseline with the machine and commit. Done when: the baseline names the host from `docs/reference-machines.md` and its commit.
- [ ] Commit: `quality: add the move judge harness for the SDK moves`

**Test checkpoint:** `build\python.cmd -m unittest tests.scripts.test_judge_move` passes, and `build\python.cmd scripts/judge-move.py compare build/judge/baseline build/judge/baseline` exits 0 with both verdicts green.

## 4. Cache budget from the SDK, oversized documents fail with an error

Recto's caches (the 32-page `DisplayList` cache of `D01 T02 §8`, the tile cache of `D02 T02 §3`, decoded-image and font caches) each carry a constant of their own. Under the design record they take their budget from the SDK, so Impossible OS and Windows both bound them from one place, and an oversized document under a bounded budget fails with a typed error and never exhausts the process. This section needs the SDK's cache budget call, which is an SDK section built first under the pin rule.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Freeze check:** Verify that cache eviction and `DocumentActor` ownership are unchanged: only where the budget number comes from and what happens past it change, and `ErrorCode::ResourceLimit` stays the typed refusal (`D01 T02 §18`).

**Build order:**

1. Add `src/core/cache_budget.h/.cpp`: one `CacheBudget` that reads the budget from the SDK's cache budget call and hands each cache its share. Done when: no cache constant in `src/core` or `src/pdf` is read directly (`kMaxActorCachedPages` becomes a default the budget can lower, never raise).
2. Route an allocation past the budget to `ErrorCode::ResourceLimit` with the cache named, never to `std::bad_alloc` or an abort. Done when: opening a document whose first page alone exceeds the budget returns `PdfError` with `ResourceLimit` and the process keeps running.
3. Add `Runtime.CacheBudgetOversizedDocument` that sets a small budget through the SDK's test hook and opens a generated oversized document. Done when: it runs in CI and fails if the code throws, aborts or grows past the budget.

- [ ] Wait on the SDK cache budget section (planned in the Impossible SDK repository: a bounded process budget per program that caches read and that tests can set), then move `sdk.json` to the commit that holds it. Done when: `scripts/check-sdk-platform.py` passes with the new pin.
- [ ] Add `CacheBudget` and route the `DocumentActor` page cache and the tile cache through it (`src/core/cache_budget.cpp`, `src/core/document_actor.cpp`). Done when: `cached_bytes` never exceeds the share and a test lowers the budget mid-run and sees eviction, not growth.
- [ ] Return `ResourceLimit` naming the cache for an oversized document on open and on render. Done when: the CLI exits with its resource-limit code and a message naming the cache, and a hostile 100000-page tree under a tight budget fails the same way.
- [ ] Add `Runtime.CacheBudgetOversizedDocument` to `tests/recto-tests/` and run it under ASan in CI. Done when: it passes, and fails when the budget check is removed from the open path.
- [ ] Add the same test to the Impossible OS import checklist in `docs/sdk-platform.md`. Done when: the file names the test so the OS import can run it again.
- [ ] Commit: `core: take cache budgets from the SDK and fail oversized documents with an error`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Runtime.CacheBudgetOversizedDocument` passes under `-EnableASAN` and fails when the budget check is removed.

## 5. `recto-pdf` onto the SDK: files, memory, fonts, drawing and crypto calls

`recto-pdf` calls Windows for file reads, DirectWrite font loading (`D01 T02 §2`), the Direct2D and WIC tile rasterizer (`D01 T02 §6`) and CNG and CryptoAPI (`bcrypt.dll`, `crypt32.dll`). One surface moves at a time, in this order: files and memory (SDK `base`), the rasterizer (SDK `draw`), fonts and glyph drawing (SDK `text`), then crypto (an SDK part or tier entry; built in the SDK first if the SDK lacks it, per the pin rule). The engine contract does not change: typed `PdfResult`, no exceptions across the actor boundary, bounded recursion and allocation.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Freeze check:** Verify that `DisplayList` stays immutable and reference-counted, `PdfResult<T>` stays the only failure channel, and each `DocumentActor` still owns its `Document`; the ASan `ThreadSafety.*` battery runs before and after each surface.

**Build order:**

1. For each surface in turn, run `scripts/judge-move.py capture --label before-<surface>`, move the surface, capture again and compare. Done when: both verdicts are green before the surface's exemption entries are deleted.
2. Move files and memory first, then the rasterizer to SDK `draw` and the font and glyph work to SDK `text`, each after the SDK has the feature at the quality Direct2D and DirectWrite gave (paths, arcs, gradients, clips, layers, shaping, fallback). Done when: `src/pdf` includes no `d2d1`, `dwrite`, `wincodec` or `windows.h` header and its `sdk-boundary` exemptions are empty.
3. Move crypto last. Done when: the `Crypto.*` and `Signature.*` suites pass and no `bcrypt` or `crypt32` header remains in `src/pdf`.

- [ ] Wait on the SDK `draw` and `text` parts at the quality needed (SDK repository: paths, arcs and gradients, clips and layers, fixed-point text, the text engine with shaping, fallback and right-to-left), then move the pin. Done when: `scripts/check-sdk-platform.py` passes with the new pin.
- [ ] Move file and memory calls in `src/pdf` to the SDK `base` part. Done when: `PdfFile.*` tests pass, the judge shows both verdicts green and the file-call exemptions are deleted from `scripts/sdk-boundary-exemptions.json`.
- [ ] Move `src/pdf/rasterizer.cpp` from Direct2D and WIC to the SDK `draw` part. Done when: `PdfRasterizer.*` and `Render.*` pass, the pixel verdict is green within the golden tolerance and the timing verdict is no worse than the before capture.
- [ ] Move `src/pdf/font.h`, `src/pdf/font.cpp` and glyph drawing from DirectWrite to the SDK `text` part. Done when: `PdfFont.*` and `PdfCharstring.*` pass, the pixel and timing verdicts are green on the conformance set.
- [ ] Move crypto to the SDK feature that replaces `bcrypt.dll` and `crypt32.dll`, or file the missing feature as an SDK section and keep the exemption until it lands. Done when: `Crypto.*` and signature tests pass, or the section names the open SDK section and the exemption stays with that name.
- [ ] Run `ThreadSafety.*` under `-EnableASAN` after each surface. Done when: every run is clean.
- [ ] Commit: `pdf: move recto-pdf onto the SDK parts`

**Test checkpoint:** `.\build.cmd -Platform x64 -Configuration Debug -TestFilter 'PdfRasterizer.*:PdfFont.*:Crypto.*:ThreadSafety.*'` passes, `build\python.cmd scripts/judge-move.py compare` is green for pixels and for timings, and `build\python.cmd scripts/sdk-boundary.py` lists no `src/pdf` file.

## 6. `recto-core` onto the SDK: atomic write, threads and time

`recto-core` owns the `DocumentActor`, `RenderPool` and atomic writes (`src/core/atomic_write.cpp`, `src/core/document_actor.cpp`), which call Win32 for files, replace-on-write, threads, events and time. Each moves to the SDK `base` part (files, threads, time); atomic temp-file-then-replace needs an SDK primitive, built in the SDK first if absent. The thread model is unchanged: one actor per document, immutable snapshots across threads, cooperative `CancelToken`.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Freeze check:** Verify that one `DocumentActor` thread per open document, the MPSC queue, `RenderPool` workers and the section 4.8 ordered shutdown behave exactly as before, proven by `ThreadSafety.*` under ASan before and after.

**Build order:**

1. Replace direct file and replace-on-write calls in `src/core/atomic_write.cpp` with the SDK file calls and the SDK's atomic replace. Done when: the atomic-write tests pass, including the read-back after write and the crash-between-temp-and-replace case.
2. Replace thread, event and time calls in `src/core` with the SDK `base` threads and time. Done when: `src/core` includes no `windows.h` and `Runtime.*` and `ThreadSafety.*` pass.
3. Capture before and compare after with the judge. Done when: pixels and timings are both green and the `src/core` exemptions are deleted.

- [ ] Wait on the SDK atomic replace and thread primitives (SDK repository `base` part), then move the pin. Done when: the pin check passes.
- [ ] Move `src/core/atomic_write.cpp` onto the SDK file calls. Done when: `AtomicWrite.*` passes and a planted failure between temp write and replace leaves the original untouched.
- [ ] Move `DocumentActor`, `RenderPool` and `Runtime` threads, events and time onto the SDK `base` part. Done when: `Runtime.*`, `DocumentActor.*` and `ThreadSafety.*` pass under `-EnableASAN`.
- [ ] Run `scripts/judge-move.py capture` before and compare after. Done when: the timing verdict is no worse than Recto's targets (open a 500-page file, render page 250) and the pixel verdict is green.
- [ ] Delete the `src/core` entries from `scripts/sdk-boundary-exemptions.json`. Done when: `scripts/sdk-boundary.py` lists no `src/core` file.
- [ ] Commit: `core: move recto-core onto the SDK base part`

**Test checkpoint:** `.\build.cmd -Platform x64 -Configuration Debug -TestFilter 'AtomicWrite.*:Runtime.*:DocumentActor.*:ThreadSafety.*' -EnableASAN` passes, and the judge compares green.

## 7. Vellum window, input, clipboard and presentation onto the SDK `window` part

Vellum (`src/ui` until `D10 T04 §1` moves it to `libs/vellum`) keeps its element tree, layout, controls and styling. Underneath, the window, input, DPI, clipboard, drag and drop, and presenting pixels move from Win32 and DirectComposition to the SDK `window` part (`D10 T01 §3`, `§4`, `§5`, `§12`, `§17`, `§19`). The SDK's `im_window` is opaque and carries no Windows handle, so Vellum never holds an `HWND`. A feature the SDK `window` part lacks (title-bar tabs and caption buttons, Snap Layouts, OLE drag and drop, Mica Alt) is built in the SDK first, or Vellum keeps it behind the SDK until it exists and the exemption names the open SDK section.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** no

**Freeze check:** Verify that the UI thread still never parses or renders PDFs and never blocks on an engine thread, and that engine callbacks still marshal through the `Dispatcher` (`D10 T01 §3`).

**Build order:**

1. Move one control surface at a time (window and DPI, then pointer and keyboard, then clipboard, then drag and drop, then non-client regions), capturing and comparing with the judge between each. Done when: each step's goldens (`D10 T01 §20`) and `Ui.*` tests pass at 100, 150 and 200 percent.
2. Route presenting through the SDK `window` present call. Done when: no DirectComposition tree remains in Vellum outside the page canvas that §9 owns.
3. Delete the Vellum entries from the exemption list as each file passes. Done when: `scripts/sdk-boundary.py` lists no `src/ui` or `libs/vellum` file for window, input or clipboard.

- [ ] Wait on the SDK `window` part's opaque `im_window` and the missing features listed in `docs/sdk-platform.md` (title-bar caption area, drag and drop, clipboard formats, per-monitor DPI), then move the pin. Done when: the pin check passes.
- [ ] Move the top-level window, DPI and minimum size. Done when: `Ui.Window*` tests pass and the 100, 150 and 200 percent goldens match within tolerance.
- [ ] Move pointer, keyboard, focus and accelerators. Done when: `Ui.Input*` and `Ui.Focus*` tests pass.
- [ ] Move clipboard and drag and drop. Done when: the copy, paste and drop tests pass, or the open SDK section is named in the exemption entry.
- [ ] Move presentation of the UI surface to the SDK present call. Done when: the judge's timing verdict shows frame time no worse than the target in foundation plan section 8 and no blank frame at reading scroll speed.
- [ ] Delete the matching exemption entries. Done when: `scripts/sdk-boundary.py` passes for the moved files.
- [ ] Commit: `ui: move the Vellum window and input onto the SDK window part`

**Test checkpoint:** `.\build.cmd -Platform x64 -Configuration Debug -TestFilter 'Ui.*:Vellum.*'` passes on the display-session host and `build\python.cmd scripts/judge-move.py compare` is green for pixels and timings.

## 8. Vellum drawing, text and images onto the SDK `draw` and `text` parts

Direct2D drawing primitives (`D10 T01 §9`), DirectWrite text (`D10 T01 §10`), and icon, SVG and bitmap images (`D10 T01 §11`) move to the SDK `draw` and `text` parts, at the quality Direct2D and DirectWrite gave, because a toolkit outside the SDK calls them as public parts. Controls stay on the CPU path. Vellum's token roles, `RectoIcons` font and `vellum::Icon` stay as they are.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** no

**Freeze check:** Verify that the frozen engine contract is untouched: this section changes only the toolkit's drawing calls, and no Vellum header exposes an engine type.

**Build order:**

1. Move drawing primitives (rounded rectangles on the radius tokens, hairlines, shadows, opacity). Done when: the control gallery goldens (`D10 T02 §23`) match within tolerance.
2. Move text (type ramp, trimming, highlighted ranges, fallback, right-to-left). Done when: the text goldens match and `PageText` hit testing in the viewer is unchanged.
3. Move icon, SVG and bitmap elements. Done when: the `RectoIcons` glyphs match their goldens at 16, 20 and 24 epx.

- [ ] Wait on the SDK `draw` and `text` parts as public, importable parts with a stable interface (SDK repository), then move the pin. Done when: the pin check passes.
- [ ] Move Vellum drawing primitives to the SDK `draw` part. Done when: `Vellum.Draw*` tests pass and the pixel verdict is green.
- [ ] Move Vellum text to the SDK `text` part. Done when: `Vellum.Text*` tests pass and the pixel verdict is green on the type ramp and fallback fixtures.
- [ ] Move icon, SVG and bitmap elements. Done when: `Vellum.Icon*` and `Vellum.Image*` tests pass.
- [ ] Compare timings with the judge. Done when: the timing verdict is no worse than the targets and any surface that is slower on the CPU is listed in `docs/sdk-platform.md` as work for the SDK `gpu` tier.
- [ ] Delete the matching exemption entries. Done when: no `d2d1` or `dwrite` header remains in Vellum and `scripts/sdk-boundary.py` passes.
- [ ] Commit: `ui: move Vellum drawing and text onto the SDK draw and text parts`

**Test checkpoint:** `.\build.cmd -Platform x64 -Configuration Debug -TestFilter 'Vellum.*:UiGallery.*'` passes and the judge compares green.

## 9. Page tiles and `src/services` presenter onto the SDK `gpu` tier, CPU path kept

Page tiles use Direct3D 11, DXGI and DirectComposition today (`D02 T02 §1` and `§3`: `ui::SwapChainHost`, `src/services/presentation.*`, the tile texture cache). They move to the SDK `gpu` tier, whose exact method list is set by two workloads, Recto's page tiles being one. Programs never touch `d3d11.dll`; the SDK `gpu` part loads it at run time, and every GPU feature has a CPU path, so a system without the GPU still shows the page. Controls stay on the CPU.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** no

**Freeze check:** Verify that tiles still cross to the presenter only as immutable `DisplayList` rasterizations over the `TileScheduler` and `RenderPool`, that the presentation thread stays the sole caller of the device context, and that device loss is recovered as `D02 T02 §1` requires.

**Build order:**

1. List the Direct3D 11 and DXGI calls the presenter and tile cache make and send that list to the SDK `gpu` tier section as Recto's workload. Done when: `docs/sdk-platform.md` holds the list and the SDK section names it.
2. Move `src/services/presentation.*` and the tile upload to the SDK `gpu` part, with the CPU path chosen at start-up. Done when: the same executable draws the page on the GPU and, with the GPU forced off, on the CPU with the same pixels within tolerance.
3. Judge scroll and zoom. Done when: the p95 frame time at cached tiles is at most 16.6 ms and zoom shows a stretched preview in the same frame, on the same machine as the before capture.

- [ ] Wait on the SDK `gpu` tier (SDK repository: device and swap chain creation, buffers, textures, compiled shaders, draw calls, device-loss status), then move the pin. Done when: the pin check passes and `app.json` `tier` names the `gpu` tier.
- [ ] Move the page tile presenter in `src/services/presentation.*` onto `gpu`. Done when: `View.Presentation*` tests pass on the display-session host.
- [ ] Move the tile texture cache and its budget (§4) onto `gpu` textures. Done when: `View.TileCache*` tests pass and the cache never exceeds its SDK budget.
- [ ] Keep the CPU path: when `gpu` reports no device or loss, tiles present through the SDK `window` pixel path. Done when: the three start-up failure cases (library not loadable, export missing, no adapter) and a device removal during present and during resize each leave the page drawing, in `View.GpuFallback*` tests.
- [ ] Compare timings and pixels with the judge. Done when: both verdicts are green and the 500-page workload (open, render page 250) is no slower than before.
- [ ] Delete the matching exemption entries. Done when: no `d3d11`, `dxgi` or `dcomp` header remains outside the SDK.
- [ ] Commit: `viewer: move page tiles onto the SDK gpu tier with a CPU path`

**Test checkpoint:** `.\build.cmd -Platform x64 -Configuration Debug -TestFilter 'View.*'` passes on the display-session host, including the GPU fallback cases, and the judge compares green.

## 10. `src/app` onto the SDK entry point, no `windows.h`, `CoInitializeEx` or `MessageBoxW`

`src/app/pch.h` includes `<windows.h>`, and `src/app/main.cpp` calls `CoInitializeEx` and `MessageBoxW` from `wWinMain`. The SDK's entry point runs static constructors before `im_main` and destructors after it, so Recto's entry becomes `im_main`. COM initialisation moves under whatever SDK call needs it (clipboard, drag and drop, shell dialogs) and the startup-failure dialog becomes an SDK message call. This is the last surface of the executable, after the toolkit and engine have moved.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** no

**Freeze check:** Verify that `Recto.exe` still creates the `Runtime` once, shuts it down in the section 4.8 order and never starts an engine thread before the entry point runs.

**Build order:**

1. Replace `wWinMain` with `im_main` and the SDK's command line call. Done when: `Recto.exe --self-test` behaves as before and the `Ui.App.*` tests pass.
2. Remove `<windows.h>` from `src/app/pch.h` and `MessageBoxW` and `CoInitializeEx` from `src/app/main.cpp`. Done when: `scripts/sdk-boundary.py` lists no `src/app` file.
3. Keep the startup failure path: the failure is shown through the SDK message call and still never exits silently. Done when: a planted startup failure shows the same text.

- [ ] Wait on the SDK message-box call, command line call and the COM-free clipboard and shell dialogs (SDK repository), then move the pin. Done when: the pin check passes.
- [ ] Move `src/app/main.cpp` to `im_main`. Done when: `Ui.App.*` passes, including the self-test and a planted startup failure.
- [ ] Remove `<windows.h>` from `src/app/pch.h` and the app's other Windows includes. Done when: `scripts/sdk-boundary.py` passes for `src/app`.
- [ ] Judge launch. Done when: cold launch to interactive Home is under 400 ms on the reference host (foundation plan section 8) and no worse than the before capture.
- [ ] Delete the `src/app` exemption entries. Done when: the exemption list holds no `src/app` file.
- [ ] Commit: `app: move Recto.exe onto the SDK entry point`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ui.App.*` passes, `build\python.cmd scripts/sdk-boundary.py` lists no `src/app` file, and the judge's launch timing is green.

## 11. `src/cli` onto the SDK entry point and file calls

`recto-cli` (`src/cli/main.cpp`) is the command-line harness and the fuzz and stress driver. It moves to `im_main` and the SDK file, console and signal calls (Ctrl+C cancellation and exit code 130 are `D01 T03 §12`), keeping every exit code and the `verbs --json` agreement.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Freeze check:** Verify that `recto-cli` still drives `recto-core` through its public headers and the same `RenderPool` tile path as the viewer.

**Build order:**

1. Move `src/cli/main.cpp` to `im_main`, console output and the SDK signal or console-control call. Done when: `Cli.*` tests pass and Ctrl+C still exits 130.
2. Remove Windows includes from `src/cli/pch.h`. Done when: `scripts/sdk-boundary.py` lists no `src/cli` file.
3. Run the judge on `recto-cli render` over the conformance set. Done when: both verdicts are green.

- [ ] Wait on the SDK console control and standard stream calls if missing (SDK repository), then move the pin. Done when: the pin check passes.
- [ ] Move `recto-cli` entry, arguments and output. Done when: `Cli.*` passes and the `verbs --json` agreement test (`D01 T03 §11`) passes.
- [ ] Move Ctrl+C cancellation and unwritable-output handling. Done when: the cancellation and unwritable-path tests pass with the same exit codes.
- [ ] Remove the `src/cli` exemption entries. Done when: `scripts/sdk-boundary.py` lists no `src/cli` file.
- [ ] Commit: `cli: move recto-cli onto the SDK entry point`

**Test checkpoint:** `.\build.cmd -Platform x64 -Configuration Debug -TestFilter 'Cli.*'` passes and `build\python.cmd scripts/sdk-boundary.py` lists no `src/cli` file.

## 12. Release built and shipped from the SDK toolchain, MSVC kept for tools only

When every surface has moved, the exemption list is empty and the release is built with the SDK toolchain (`toolchain.json`: clang, libc++ through the SDK `cxx` part, static, exceptions and RTTI on), passes the link rule, the source check and the import check, and is the build that ships (`Recto.exe` and `recto-cli.exe`). Recto may keep an MSVC build for its own tools and tests, but that build does not ship. Impossible OS compares the hash this release records for each executable with the one it builds from the same pinned commit (SDK builds are reproducible).

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Freeze check:** Verify that the shipped executables pass the full `ThreadSafety.*` battery under the SDK toolchain build as they did under MSVC.

**Build order:**

1. Make `scripts/sdk-boundary-exemptions.json` empty and the `sdk-build` job required on every push. Done when: the file is `[]` and a planted `<windows.h>` turns main red.
2. Build the release in CI with the SDK toolchain and record the SHA-256 of each executable in the release notes (`scripts/release-notes.py`). Done when: two builds of one commit give the same hashes.
3. Mark the MSVC build non-shipping in `build/build.ps1`, `installer/` and the Inno Setup and MSIX inputs. Done when: packaging refuses an MSVC-built executable.

- [ ] Empty the exemption list and make the `sdk-build` job a required check. Done when: `scripts/sdk-boundary.py` exits 0 with an empty list.
- [ ] Run the whole test suite and the `ThreadSafety.*` battery against the SDK toolchain build. Done when: `.\build.cmd` is green on the SDK toolchain build for x64.
- [ ] Record executable hashes in `scripts/release-notes.py` output. Done when: a rebuild of the same commit gives the same SHA-256 per executable.
- [ ] Make packaging refuse non-SDK executables. Done when: a planted MSVC-built `Recto.exe` fails the packaging script.
- [ ] Update `AGENTS.md` (What is here, Architecture rules) and `design/SPEC.md` section 4.1 wording to say Vellum draws through the SDK, in the same commit. Done when: no statement says Vellum draws through Direct2D or DirectComposition as the shipping path.
- [ ] Commit: `release: ship Recto from the SDK toolchain`

**Test checkpoint:** `build\python.cmd scripts/sdk-boundary.py` exits 0 with an empty exemption list, the `sdk-build` job is green on `main`, and `.\build.cmd` passes every test on the SDK toolchain build.

## 13. ASan and UBSan 10-second runs over each real fuzz entry point

`fuzz/fuzz_smoke.cpp` today discards its input, so no fuzz run exercises the engine. `D01 T03 §4` builds the real libFuzzer entry points (`fuzz_open`, `fuzz_filters`, `fuzz_render`); this section runs each for 10 seconds under AddressSanitizer and under UndefinedBehaviorSanitizer in Windows CI, on every push that changes code. It stands alone and starts as soon as the entry points exist: it does not wait for any SDK work.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Freeze check:** Verify that the fuzz targets drive `recto-pdf` and `recto-core` through their public entry points only and add no new threading contract.

**Build order:**

1. Add a `fuzz-sanitizers` job to `.github/workflows/build.yml` that builds each real entry point with `-fsanitize=fuzzer,address` and again with `-fsanitize=fuzzer,undefined` (clang from the Visual Studio LLVM component; MSVC has no UBSan). Done when: the job lists each target it ran and its sanitizer.
2. Run each for `-max_total_time=10` over `tests/corpus/seeds/` and `broken/`. Done when: a planted crash and a planted undefined-behaviour input each fail the job.
3. Delete `fuzz/fuzz_smoke.cpp` or make it a real entry point. Done when: no fuzz target in `fuzz/` discards its input.

- [ ] Add the `fuzz-sanitizers` job and a `scripts/run-fuzz-smoke.py` driver. Done when: the job runs every `fuzz_*` target in `fuzz/` for 10 seconds per sanitizer.
- [ ] Add a UBSan build configuration (clang, `-fsanitize=undefined`) beside the existing ASan one. Done when: `build.cmd` can build one fuzz target under it and a signed-overflow plant is reported.
- [ ] Plant a crash and an undefined-behaviour input in a scratch target (not shipped). Done when: each fails its job and the diagnostics differ.
- [ ] Retire the discarding `fuzz/fuzz_smoke.cpp` once `D01 T03 §4` lands. Done when: `fuzz/CMakeLists.txt` and `recto-fuzz.vcxproj` build only real entry points.
- [ ] Commit: `ci: run each fuzz entry point under ASan and UBSan for 10 seconds`

**Test checkpoint:** the `fuzz-sanitizers` job is green on `main` with one ASan and one UBSan run per real entry point, and red on the planted crash and the planted undefined-behaviour input.

## 14. ARM64 kept until the SDK arm64 target is proved, then moved

Recto ships ARM64 today (D15) and the SDK is x64 only. Recto keeps its present ARM64 MSVC build until the SDK's arm64 Windows target is proved by a sample on Windows arm64 and its checks know the arm64 machine type; only then does Recto move its ARM64 release onto the SDK toolchain. Dropping the MSVC ARM64 build waits for that proof and for §12.

**Phase:** 7
**Needs:** windows-host, arm64-host
**Surface:** no

**Freeze check:** Verify that the ARM64 release passes the same `ThreadSafety.*` battery on the SDK toolchain as the x64 release.

**Build order:**

1. Until the SDK's arm64 section is proved, leave the `-Platform ARM64` MSVC job in `.github/workflows/build.yml` untouched. Done when: `docs/sdk-platform.md` states the condition for moving and names the SDK repository section that proves it.
2. When it is proved, move `sdk.json`, build ARM64 with the SDK toolchain and run the judge on the ARM64 host. Done when: both verdicts are green on the ARM64 host.
3. Retire the MSVC ARM64 shipping build. Done when: ARM64 packaging refuses a non-SDK executable.

- [ ] Wait on the SDK arm64 Windows target (SDK repository: the toolchain target, the checks' arm64 machine type and a proved sample), then move the pin. Done when: the pin check passes and the SDK states the target proved.
- [ ] Build ARM64 with the SDK toolchain in CI beside the MSVC ARM64 job. Done when: the new job is green on `main`.
- [ ] Run the judge and `ThreadSafety.*` on the ARM64 host (`D99 T01 §4`). Done when: both verdicts and the battery are green on the ARM64 machine.
- [ ] Retire the MSVC ARM64 shipping build and make ARM64 packaging refuse non-SDK executables. Done when: a planted MSVC ARM64 executable fails packaging.
- [ ] Commit: `release: move the ARM64 build onto the SDK toolchain`

**Test checkpoint:** the SDK ARM64 job is green on `main`, `build\python.cmd scripts/judge-move.py compare` is green on the ARM64 host, and ARM64 packaging refuses a planted MSVC executable.

## Verification

- Every `src/` and `libs/` file passes `build\python.cmd scripts/sdk-boundary.py` with an empty exemption list, and the SDK's link rule and import check pass on `Recto.exe` and `recto-cli.exe` in CI.
- The judge's pixel and timing verdicts are green for every moved surface, and the 500-page workload (open, render page 250) is no slower than the §3 baseline.
- `recto-tests.exe --gtest_filter=Runtime.CacheBudgetOversizedDocument` passes under ASan, and the `fuzz-sanitizers` job ran every real entry point under ASan and UBSan.
- `build\python.cmd scripts/todo-graph.py validate` and `build\python.cmd scripts/todo-graph.py plan --check` pass.
