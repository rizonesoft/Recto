---
schema_version: 1
id: multi-file-search
domain: 03-text
status: active
title: "TODO-05 -- Search Across Files and Local Index"
---

# TODO-05 -- Search Across Files and Local Index

> **Goal:** Match Acrobat Pro's Advanced Search: one query searches every PDF in a folder (recursive) or every open tab, with the same options as single-document Find (`Match case`, `Whole word`, `Regex`, exact accents), and shows the results grouped by file with page and context. A hit opens its file at the exact location. An optional local full-text index makes repeat searches fast; it is built by background jobs, stored under `AppPaths` data, updated incrementally, and rebuilt, never trusted, when it is corrupt. Nothing is followed, executed or OCR-ed silently.

> **Current state:** Measured 2026-10-21. `D03 T01` (frozen) owns single-document search: `SearchEngine` and `SearchOptions` (`§5`, `src/core/search.h/.cpp`, `match_case`, `whole_word`, `use_regex`, `match_diacritics`), `recto-cli find <file.pdf> <query>` (`§5`), the `FindCard` and Reading room `Search` tab (`§6`), and `PageText` extraction (`§1`, `§2`). None of it searches more than one file. `D02 T04 §1` supplies `AppPaths` and Portable mode, `D02 T04 §2` the `SettingsStore`, `D02 T01 §5` the shared `OpenDocumentPipeline`, `D02 T03 §11` and `§12` the `CommandRegistry` and Command Box. `tests/corpus/` has no multi-file folder fixture. No index, no folder walker and no multi-file result model exist.

> 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); sections rewritten against `todo/10-ui/`. The grouped results list, scope picker, `Skipped` expander and the Settings index manager are `recto-ui` elements (`ui::ItemsView`, `ui::ComboBox`, `ui::Expander`, `ui::SettingsGroup`) under `src/app/`; the search, enumeration and index engines are unchanged.

> **Groomed 2026-10-23:** `Current state` re-measured: none of `src/core/search.*`, `FileScope` or `SearchIndex` exists, `src/ui` is not built and `src/app` is the retired WinUI scaffold; the claims above hold. SPEC 9.4 also lists indexed files among the command box `Files` rows; that gap is filed as `D03 T07 §3`, which reads the `§4` index by file name only. Reviewed for human-judged checkpoints: none found.

## Inputs

- `docs/specs/2026-10-22-recto-ui-toolkit-design.md` sections 4.10, 4.11, 5 and 6 (the `recto-ui` toolkit that replaces WinUI 3, the WinUI-to-`recto-ui` mapping and the `todo/10-ui/` sections this file consumes).
- `design/SPEC.md` sections 4 (`RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`), 9.3 (find card, Reading room Search tab, results rows), 9.7 (Settings groups), 9.9 (confirmation dialog chrome), 10 (keyboard).
- `docs/plans/2026-10-13-recto-foundation.md` sections 3.3 (`PageText`), 4 (actor and job contract), 9 (hostile input) and decisions D6 (Portable mode), D16 (diagnostics), D18 (Acrobat Pro parity).
- The accent-insensitive default and offset-safe folding that `D03 T01 §5` already implements and this file reuses unchanged.
- `todo/03-text/TODO-01-selection-and-search.md` (frozen): `§5` is the single-document engine every section here calls and never edits.
- -> XREF: D03 T07 §3 -- the command box `Files` rows for indexed files read the `SearchIndex` of `§4` by file name only.
- -> XREF: D03 T01 §5 -- multi-file search wraps `SearchEngine` and `SearchOptions` unchanged.
- -> XREF: D02 T04 §1 -- the search index lives under `AppPaths` data and moves with Portable mode.
- -> XREF: D04 T07 §8 -- the Action Wizard (D04 T07 §8) enumerates inputs with `FileScope`.
- -> XREF: D08 T02 §14 -- index beside text index.
- -> XREF: D08 T02 §15 -- freshness rule.
- -> XREF: D08 T02 §16 -- open at location.
- -> XREF: D08 T02 §17 -- Meaning mode in Search panel.
- -> XREF: D06 T04 §9 -- the `D-SearchPanels.png` board and `design/SPEC.md` section 9.3.2 that §7, §8 here build against.
- -> XREF: D10 T02 §11 -- the virtualized `ui::ItemsView` with group headers is the grouped results list built in `§7`.

## Outcome

- `Ctrl+Shift+F` opens a scope picker (`Current document`, `Open tabs`, `Folder...`); a folder search walks the folder recursively without following links and returns hits grouped by file, each group listing page numbers and context snippets with the match in bold, while the UI thread never blocks.
- Clicking a hit opens its file in a tab (or focuses the existing tab) at the page and quad of the match, highlighted with `RectoSearchHitCurrentBrush`.
- An optional index, built by cancelable background jobs, lets a repeat folder search skip every file whose indexed text cannot contain the query and skip re-extracting text for unchanged files; files that might match are reopened and every hit is confirmed against the real page text. The index stores searchable text derived from the PDFs (folded character trigrams per page) on this PC, and Settings says so. A changed file is re-indexed alone, and a corrupt index is discarded and rebuilt with a notice.
- Encrypted files are listed as skipped with the reason, scanned files are searched only through OCR text that is already stored, and no search ever runs OCR, follows a link or runs content.
- `recto-cli find --folder <dir> <query>` returns the same grouped hits as JSON or text.
- The grouped results list, scope picker, `Skipped` expander and Settings index manager are `recto-ui` elements (`ui::ItemsView`, `ui::ComboBox`, `ui::Expander`, `ui::SettingsGroup`) under `src/app/`.

**Adjacency:** list=applicable @ D03 T05 §7; document=applicable @ D03 T05 §8; settings=applicable @ D03 T05 §9; reporting=applicable @ D03 T05 §6; notifications=applicable @ D03 T05 §5; permissions=applicable @ D03 T05 §3; audit=not-applicable (search history is never persisted; the index content disclosure is in `§9`); exchange=applicable @ D03 T05 §6; reverse=applicable @ D03 T05 §9

**Adjacency rationale:** The list surface is the grouped results list (`§7`). Document is opening a hit in a tab (`§8`). Settings is the index manager and the scope defaults (`§9`). Reporting and exchange are the CLI text and JSON output (`§6`). Notifications are the index build progress and the corrupt-index rebuild notice (`§5`). Permissions are the encrypted-file skip with its reason (`§3`). Audit does not apply because the query and results are never stored; the index itself does persist folded text fragments of each indexed page, so `§9` tells the user that and lets them delete it. Reverse is cancel (`§3`, `§5`) and deleting the index (`§9`).

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Multi-file search corpus folder in `tests/corpus/search-folder/` | D00 T03 §3 | [ ] |
| 2 | §2 | Safe recursive folder enumeration (`FileScope`) that never follows links | §1 | [ ] |
| 3 | §3 | Multi-file search engine (`MultiSearch`) with encrypted skip and stored-OCR-only text | §2, D03 T01 §2, D03 T01 §5 | [ ] |
| 4 | §4 | Local index store (`SearchIndex`), incremental update and corrupt-index rebuild | §3, D02 T04 §1 | [ ] |
| 5 | §5 | Background index build jobs and indexed query path | §4, D01 T02 §9 | [ ] |
| 6 | §6 | `recto-cli find --folder` and `--index` | §3, §5, D01 T03 §1 | [ ] |
| 7 | §7 | Search panel scope picker and grouped results list | §3, D03 T01 §6, D02 T03 §12, D06 T04 §9, D10 T02 §2, D10 T02 §7, D10 T02 §9, D10 T02 §11, D10 T02 §19 | [ ] |
| 8 | §8 | Open a result at its location from the grouped results list | §7, D02 T01 §5, D06 T04 §9 | [ ] |
| 9 | §9 | Settings index manager (folders, size, rebuild, delete) | §5, §7, D02 T04 §4, D10 T02 §11, D10 T02 §17, D10 T02 §19 | [ ] |

## 1. Multi-file search corpus folder in `tests/corpus/search-folder/`

Every later section needs a small folder of PDFs with known hit counts, an encrypted file, a scanned file with and without stored OCR text, an accented word, a nested folder, and a link that must not be followed. The corpus is generated, not hand-made, so every byte is reproducible and listed in `tests/corpus/PROVENANCE.md`. This section must not change any existing fixture or the checks in `Corpus.Provenance`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Extend `scripts/build-corpus.py` to emit `tests/corpus/search-folder/` with the fixtures below and register each in `PROVENANCE.md`. Done when: `python scripts/build-corpus.py --check` exits 0 and `Corpus.Provenance` lists every new file.
2. Document the folder and its expected hits in `tests/corpus/README.md`. Done when: the README table names `search-folder/` and each file's expected hit count for the query `invoice`.

- [ ] Generate `tests/corpus/search-folder/a-invoices.pdf` (3 pages, `invoice` on pages 1 and 3, twice on page 3), `b-notes.pdf` (1 page, no `invoice`), and `nested/c-cafe.pdf` (1 page containing `café` and `Café`) in `scripts/build-corpus.py`. Done when: the generated files exist and `python scripts/build-corpus.py --check` passes.
- [ ] Generate `encrypted-aes-256.pdf` inside the folder using the existing fixed test passwords documented in `tests/corpus/README.md`. Done when: the file opens only with `recto-test` and its page text contains `invoice`.
- [ ] Generate `scanned-no-ocr.pdf` (raster page only) and `scanned-with-ocr.pdf` (the same raster plus an invisible text layer, `/Tr 3`, containing `invoice`). Done when: text extraction returns empty text for the first file and `invoice` for the second.
- [ ] Generate `links.pdf` with a `/Link` annotation whose `/URI` points to `https://example.invalid/` and a `/Launch` action to `cmd.exe`, plus a `/GoToR` action naming a file outside the folder. Done when: `rg -c "example.invalid" tests/corpus/search-folder/links.pdf` prints `1` and the fixture parses.
- [ ] Generate `broken-truncated.pdf` (a truncated copy of `a-invoices.pdf`) and `not-a-pdf.pdf` (plain text with a `.pdf` name). Done when: opening each returns a typed `PdfError`.
- [ ] Add `tests/corpus/search-folder/readme.txt` (not a PDF) so extension filtering is exercised. Done when: the file is listed in `PROVENANCE.md` and `python scripts/build-corpus.py --check` passes.
- [ ] Commit: `corpus: add search-folder fixtures for multi-file search`

**Test checkpoint:** `python scripts/build-corpus.py --check` exits 0, and `recto-tests.exe --gtest_filter=Corpus.Provenance` passes with the new files listed.

## 2. Safe recursive folder enumeration (`FileScope`) that never follows links

Define what a folder search may touch. `FileScope` enumerates `.pdf` files under a chosen folder, recursively, and never follows a reparse point, junction, symbolic link, `.lnk` shortcut or network redirector it did not start in. It bounds depth, file count and path length so a hostile or enormous tree cannot hang the app. This is Acrobat's "all PDF documents in" scope, kept offline and link-safe (D16).

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `FileScope` in `src/core/file_scope.h/.cpp`: `FileScope::from_folder(path, options)` and `FileScope::from_files(paths)` returning `PdfResult<FileScope>`, with an enumerator yielding `ScopeEntry { path, size, modified_utc, kind }`. Done when: `FileScope.*` tests enumerate `tests/corpus/search-folder/` recursively and return exactly its `.pdf` entries sorted by path.
2. Add the safety limits: default maximum depth 16, maximum files 20000, maximum path length 32767 UTF-16 units, skip reparse points and symbolic links, skip hidden and system files unless an option allows them. Done when: a junction loop created by the test and a 17-level tree enumerate without hanging and report the skipped count.

- [ ] Implement `FileScope::from_folder` with `FindFirstFileExW` (`FindExInfoBasic`, `FIND_FIRST_EX_LARGE_FETCH`) and an iterative stack, not recursion. Done when: `FileScope.RecursiveOrder` passes and a 5000-file temp tree enumerates in under 2 s on the reference machine.
- [ ] Skip every entry whose attributes include `FILE_ATTRIBUTE_REPARSE_POINT` and never open `.lnk` files, and expose `FileScope::open_checked(entry)` as the only way to open a listed file: it opens with `CreateFileW` (`FILE_FLAG_OPEN_REPARSE_POINT`, share mode read only), then on the handle rejects reparse points, checks `GetFinalPathNameByHandleW` stays inside the canonical selected root, and records the file ID (`FILE_ID_INFO`) for §4. Done when: `FileScope.NeverFollowsLinks` creates a junction and a symbolic link pointing outside the scope and proves neither is entered nor listed, and `FileScope.OpenRace` swaps an enumerated subfolder for a junction after enumeration (through a test hook between listing and opening) and `open_checked` refuses the file.
- [ ] Enforce depth, count and path-length limits and return a `FileScopeSummary { skipped_links, skipped_hidden, truncated_at_limit }`. Done when: `FileScope.Limits` shows each limit trip with a typed summary, not a failure of the whole search.
- [ ] Accept the open-tabs scope as `FileScope::from_tabs` over `TabSource { tab_id, display_name, path_or_null, actor_handle }` entries the shell passes, which §3 searches through the owning tab's `DocumentActor`, never through a path. Done when: `FileScope.OpenTabs` lists three tabs including an unsaved one with a null path.
- [ ] Match the `.pdf` extension case-insensitively and list only files with that extension. Done when: `readme.txt` and a file named `x.PDF.bak` are not listed and `x.PDF` is.
- [ ] Add `FileScope.*` tests in `tests/recto-tests/file_scope_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=FileScope.*` passes under ASan.
- [ ] Commit: `search: add FileScope folder enumeration that never follows links`

**Test checkpoint:** `recto-tests.exe --gtest_filter=FileScope.*` passes under ASan, including `FileScope.NeverFollowsLinks`, which fails if a junction target is entered.

## 3. Multi-file search engine (`MultiSearch`) with encrypted skip and stored-OCR-only text

`MultiSearch` runs `D03 T01 §5`'s `SearchEngine` over every file in a `FileScope` on background workers and returns hits grouped by file. Each file is opened as its own `recto::pdf::Document` on a job-owned `DocumentActor`; the viewer's open document is never shared. It must not change `SearchEngine`, run OCR, follow a link, or fall over on a hostile file: one bad file becomes a per-file status and the search carries on.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `MultiSearchOptions` (wraps `SearchOptions` from `D03 T01 §5` unchanged, plus `max_hits_per_file`, `max_total_hits`) and `FileResult { path, status, hits, pages_searched, note }` with `status` in `Searched`, `SkippedEncrypted`, `SkippedUnreadable`, `NoTextLayer`, `Cancelled` in `src/core/multi_search.h`. Done when: the header compiles in `recto-core` and `recto-cli`.
2. Implement `MultiSearch::run(scope, query, options, cancel, on_file_done)` with a bounded worker count (default `min(4, hardware_concurrency - 1)`), per-file isolation and progressive callbacks. Done when: `MultiSearch.*` tests on `tests/corpus/search-folder/` return the expected grouped hits for `invoice`.

- [ ] Open each disk file through `FileScope::open_checked` (never by path again) on its own job `DocumentActor`, search with `SearchEngine`, and close it before the next file on the same worker so at most `worker_count` job documents are open; for an open-tab entry, request immutable `std::shared_ptr<const PageText>` snapshots from the tab's own `DocumentActor` through `get_page_text`, which includes unsaved edits and unlocked encrypted tabs without a second open or a retained password. Done when: `MultiSearch.BoundedOpenDocuments` observes at most 4 job documents on a 50-file scope via the `Runtime` live-object counters, and `MultiSearch.OpenTabSnapshots` finds `invoice` in an unsaved tab and in an unlocked encrypted tab with no password passed to the job.
- [ ] Skip encrypted files that need a password and report `SkippedEncrypted` with the note `Password required. Open the file first to include it.`; include an encrypted file when its tab is already open and unlocked (searched through the tab snapshot). Done when: `MultiSearch.EncryptedSkippedUnlessOpen` skips `encrypted-aes-256.pdf` as a disk file and finds `invoice` in it as an unlocked open tab.
- [ ] Search scanned files through OCR text only when `PageText` already has stored text (an invisible text layer or a text layer saved by `D03 T01 §8`); otherwise report `NoTextLayer` with the note `No text layer. Run OCR to include it.` and never start OCR. Done when: `MultiSearch.NoSilentOcr` finds `invoice` in `scanned-with-ocr.pdf`, reports `NoTextLayer` for `scanned-no-ocr.pdf`, and proves the OCR pipeline was never invoked (a counting stub records zero calls).
- [ ] Keep accent-insensitive search as the default and an exact-accent toggle by passing `SearchOptions::match_diacritics` through unchanged. Done when: `MultiSearch.Accents` finds `café` and `Café` in `nested/c-cafe.pdf` for the query `cafe` by default, finds both for `café` with exact accents on and `Match case` off, finds only `café` for `café` with exact accents and `Match case` on, and finds no unaccented `cafe` word when exact accents are on.
- [ ] Contain per-file failures: a truncated, non-PDF or crashing-parser file yields `SkippedUnreadable` with the `PdfError` code and the search continues. Done when: `MultiSearch.HostileFilesDoNotStopSearch` runs over `broken-truncated.pdf` and `not-a-pdf.pdf` and still returns the hits from the good files.
- [ ] Never act on links or actions: the searcher reads page text only and never resolves `/URI`, `/Launch` or `/GoToR`. Done when: `MultiSearch.NeverFollowsLinks` over `links.pdf` makes zero network, process and file-open calls outside the scope (a test hook counts them).
- [ ] Cancel within 50 ms of `CancelToken` flipping, mid-file and between files, returning partial grouped hits marked `Cancelled`. Done when: `MultiSearch.CancelMidRun` cancels a 200-file scope and the worker threads join within 50 ms.
- [ ] Add `MultiSearch.*` tests in `tests/recto-tests/multi_search_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=MultiSearch.*` passes under ASan.
- [ ] Commit: `search: add MultiSearch across a FileScope with encrypted skip and no silent OCR`

**Test checkpoint:** `recto-tests.exe --gtest_filter=MultiSearch.*` passes under ASan, including `MultiSearch.NoSilentOcr` (fails if OCR runs) and `MultiSearch.EncryptedSkippedUnlessOpen`.

## 4. Local index store (`SearchIndex`), incremental update and corrupt-index rebuild

The optional catalog makes repeat searches fast. It is a derived cache: the PDFs stay the source of truth, so the index file is never trusted. It lives under `AppPaths::data_dir()` (Portable mode respected, including live migration), is written temp-file-then-replace, and records for each file its path, size, modification time and a per-page normalised token list. A corrupt, truncated, wrong-version or hostile index is discarded and rebuilt.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define the on-disk format in `src/core/search_index.h/.cpp`: header (magic `RSIX`, format version, little-endian), a file table (`path`, `size`, `modified_utc`, `page_count`, `status`), and per-page posting lists of folded character trigrams of the page text using the same folding as `SearchEngine` (case and accents folded), so any literal of 3 or more folded characters can be looked up as a substring. Done when: `SearchIndex.RoundTrip` writes and reads a 3-file index byte-identically.
2. Implement load with full validation (every offset and length checked against the file size with checked arithmetic, a total cap of 2 GB, term and posting count caps) returning `PdfResult<SearchIndex>`. Done when: `SearchIndex.RejectsCorrupt` returns a typed error for truncation, flipped bytes, a wrong version and a posting list that claims 4 billion entries, without allocating more than 64 MB.

- [ ] Resolve the index directory as `AppPaths::data_dir() / "search-index"` and re-resolve after `AppPaths::set_portable_mode`. Done when: `SearchIndex.PortableMode` shows the index in `<exe-dir>\data\search-index` in Portable mode and under `%LOCALAPPDATA%\Recto\search-index` otherwise.
- [ ] Write every index file with temp-file-then-replace in the destination folder. Done when: `SearchIndex.AtomicWrite` kills the writer mid-write (fault injection) and the previous index loads unchanged.
- [ ] Implement `SearchIndex::plan_update(scope)` returning `Added`, `Changed` (file ID, size, last write time or NTFS change time differs), `Removed` and `Unchanged` file lists using only `FileScope` metadata, never file contents. The change time is recorded because the file system updates it when a timestamp is restored by hand. Done when: `SearchIndex.IncrementalPlan` touches one file in a `TempDir` copy of the corpus and the plan lists exactly that file as `Changed`, and `SearchIndex.RestoredTimestamp` replaces a file with different bytes of equal length, restores its last write time, and the plan still lists it as `Changed`.
- [ ] Treat a corrupt or unreadable index as absent: delete it, record `index_rebuilt_reason`, and return an empty index so §5 rebuilds it. Done when: `SearchIndex.CorruptIsRebuiltNotTrusted` shows a flipped byte yields an empty index plus a reason, never a wrong hit.
- [ ] Never index encrypted files' text, files that need a password, or pages without stored text; store their status only. Done when: `SearchIndex.NoSecretsIndexed` finds no `invoice` posting for `encrypted-aes-256.pdf` and the status `SkippedEncrypted`.
- [ ] Add `fuzz/fuzz_search_index.cpp` to `recto-fuzz.exe` loading arbitrary bytes through the §4 loader, with seeds in `tests/corpus/seeds/` built by `scripts/build-corpus.py`. Done when: `recto-fuzz.exe fuzz_search_index -runs=100000` reports no crash, leak or oversize allocation under ASan.
- [ ] Add `SearchIndex.*` tests in `tests/recto-tests/search_index_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=SearchIndex.*` passes under ASan.
- [ ] Commit: `search: add SearchIndex store with incremental plan and corrupt-index rebuild`

**Test checkpoint:** `recto-tests.exe --gtest_filter=SearchIndex.*` passes under ASan, including `SearchIndex.CorruptIsRebuiltNotTrusted`, which fails if a corrupt index ever returns a hit.

## 5. Background index build jobs and indexed query path

Index building runs as background jobs on the shared `RenderPool`-sized worker budget at below-viewer priority, so reading, scrolling and rendering never slow down. A query against an indexed folder uses the posting lists to pick candidate files and pages, then confirms every hit by running `SearchEngine` on the real page text, so the index can only narrow work and never invent a result. Regex queries and exact-accent queries fall back to confirming against the page text of candidate files.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `IndexJob` in `src/core/index_job.h/.cpp`: given a `FileScope` and `SearchIndex::plan_update`, extract `PageText` for `Added` and `Changed` files, append postings, drop `Removed` files, checkpoint the index atomically every 50 files, and report progress. Done when: `IndexJob.BuildsCorpus` indexes `tests/corpus/search-folder/` and a second run indexes zero files.
2. Add `MultiSearch::run_indexed(...)` that narrows candidates with the index and confirms hits against real text. Done when: `MultiSearch.IndexedMatchesUnindexed` returns identical grouped hits with and without the index for ten queries including substring (`voice`), regex, short, accented and case-sensitive ones.

- [ ] Run index jobs at lowest worker priority, limited to one worker while a viewer render is active, and cancelable at any page boundary. Done when: `IndexJob.YieldsToViewer` shows tile latency in `ThreadSafety`-style timing stays within 10 percent while a 100-file index builds.
- [ ] Read each file once through `FileScope::open_checked` into memory, extract text from that snapshot, then re-read the file's ID, size, last write time and change time from the same handle after reading and discard the file's postings if any differ (it is re-planned next run). Store a SHA-256 (CNG) of each indexed file and add a `Verify index` operation that recomputes it and invalidates entries whose digest differs. Done when: `IndexJob.FileChangedMidBuild` modifies a file after the read starts and the index never holds postings from it, and `IndexJob.VerifyCatchesSameMetadata` shows `Verify index` invalidates a same-size file whose metadata was forged.
- [ ] Publish progress and the corrupt-index notice through the app `QueueCard`-style job event channel as typed events (`IndexProgress`, `IndexRebuilt`, `IndexSkipped`). Done when: `IndexJob.Events` receives the three event kinds in order from the test harness.
- [ ] Use the index only to narrow the scope for literal queries of 3 or more folded characters (looking up every trigram of the folded query and requiring all to be present on the page); for regex queries, queries under 3 folded characters, and any query the trigram lookup cannot answer completely, scan the whole eligible scope and mark the result `index_assisted = false`. Exact-accent and `Match case` queries use the folded lookup to narrow and then confirm with the real options. Done when: `MultiSearch.IndexFallback` shows `inv.*ce` and the two-character query `in` return every hit with no index assistance, and `MultiSearch.IndexSubstring` finds `voice` inside `invoice` with the index.
- [ ] Validate freshness at search time before any index exclusion: `run_indexed` opens each eligible file with `FileScope::open_checked` (share mode read only, so no writer can change it while the handle is held), reads file ID, size, last write time and change time from that same handle, and the index may exclude the file only when those match its entry and only while that handle is still held, so the check and the exclusion refer to the same bytes (the file is never re-identified by path); every `Added`, `Changed` and unverifiable file (metadata unreadable, index entry missing or marked invalid) goes into the direct scan, or is refreshed synchronously first, so a negative posting is never trusted for a file that changed. A deleted file is dropped with a `Removed` note and a replaced file is searched as its new content. `Verify index` stays an optional deep check and is not what makes a search complete. Done when: `MultiSearch.StaleIndexEntry` deletes an indexed file and the result lists no hit for it and one `Removed` note, `MultiSearch.ExclusionBoundToHandle` replaces a file through a test hook between the identity read and the exclusion and proves the write is refused or the file is scanned, and `MultiSearch.ReplacedFileSearched` indexes a file without `invoice`, replaces it with one containing `invoice` (with and without restoring the timestamp), searches before any rebuild, and finds the hit.
- [ ] Add `IndexJob.*` and `MultiSearch.Indexed*` tests in `tests/recto-tests/index_job_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=IndexJob.*:MultiSearch.Indexed*:MultiSearch.IndexFallback:MultiSearch.StaleIndexEntry:MultiSearch.ReplacedFileSearched:MultiSearch.ExclusionBoundToHandle` passes under ASan.
- [ ] Commit: `search: add background IndexJob and confirmed indexed query path`

**Test checkpoint:** `recto-tests.exe --gtest_filter=IndexJob.*:MultiSearch.Indexed*` passes under ASan, including `MultiSearch.IndexedMatchesUnindexed`, which fails if the index ever adds or drops a hit.

## 6. `recto-cli find --folder` and `--index`

The command line exposes the same engine for scripts. `recto-cli find <query> --folder <dir>` searches a folder; `--open-tabs` is not offered because the CLI has no tabs. It extends `D03 T01 §5`'s `recto-cli find` without changing the single-file form, which stays `recto-cli find <file.pdf> <query>`.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `--folder <dir>` to `recto-cli find` in `src/cli/cmd_find.cpp` with `--no-recurse`, `--case`, `--whole-word`, `--regex`, `--exact-accents`, `--max-hits`, `--json` and the existing exit-code contract of `D01 T03 §1`. Done when: `recto-cli find invoice --folder tests/corpus/search-folder --json` prints grouped hits for `a-invoices.pdf` pages 1 and 3 and `scanned-with-ocr.pdf`.
2. Add `--index` (use and update the local index), `--rebuild-index` and `--index-status`. Done when: a second `--index` run reports `indexed_files: 0` and the same hits.

- [ ] Print text output grouped by file (`path`, then `page N: snippet` lines with the match in `[brackets]`) and JSON with `files[].status`, `files[].note`, `files[].hits[]` (`page`, `quads`, `snippet`). Done when: `Cli.FindFolderJson` parses the output and matches the `search-folder` expectations.
- [ ] Return exit code 0 when at least one hit was found, 1 when none were found, and the `D01 T03 §1` error code for an unreadable folder, listing skipped encrypted and no-text-layer files on stderr or in JSON. Done when: `Cli.FindFolderExitCodes` asserts all three codes.
- [ ] Keep `recto-cli find <file.pdf> <query>` byte-identical in output. Done when: `Cli.FindCommand` from `D03 T01 §5` still passes unchanged.
- [ ] Add `Cli.FindFolder*` tests in `tests/recto-tests/cli_find_folder_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Cli.FindFolder*:Cli.FindCommand` passes under ASan.
- [ ] Commit: `cli: add recto-cli find --folder with optional local index`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.FindFolder*:Cli.FindCommand` passes under ASan, and `recto-cli find invoice --folder tests/corpus/search-folder --json` prints a `files` array whose `a-invoices.pdf` entry lists pages 1 and 3.

## 7. Search panel scope picker and grouped results list

The user searches more than one file from the same Search surface they already know. The `FindCard` (`D03 T01 §6`) gains no new behaviour; instead a `Search in files` mode of the Search panel adds a scope picker and a results list grouped by file. Matches Acrobat Pro's Advanced Search window, in Recto's one shell (D17) and reachable from `Ctrl+K`.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 9.3 (find card rows, `Match case`, `Whole word`, `Regex` toggles, bold match in context rows, `RectoSearchHitBrush`) and section 9.9 (`ui::Dialog` chrome for the folder confirmation), hosted in the Reading room `Search` inspector tab and, in Focus and Classic, in the `SidePanelHost` of `D02 T03 §3`. Design board: `design/SPEC.md` section 9.3.2 with `design/screenshots/D-SearchPanels.png` (drawn by `D06 T04 §9`).
**Job:** A user with a folder of contracts or papers types a query once, picks `Folder...` or `Open tabs`, and sees every file that matches with its pages and context, plus an honest list of files that could not be searched and why. Consumer: `MultiSearch` (§3), `SearchIndex` (§4), `CommandRegistry` (`D02 T03 §11`), `AnnouncementService` (`D03 T02 §5`).
**Treatment:** One `FilesSearchViewModel` over `MultiSearch` and a virtualized `ui::ItemsView` with file group headers (name, folder, hit count), up to 5 context rows per file with `Show all N in this file`, and a `Skipped (N)` `ui::Expander` (`D10 T02 §19`) listing each skipped file with its note. Scope picker is a `ui::ComboBox` (`Current document`, `Open tabs`, `Folder...`) plus a `Choose folder` button using `IFileOpenDialog` in folder mode; `Include subfolders` and `Use index` are `ui::ToggleSwitch`es (`D10 T02 §7`); the three find toggles and `Exact accents` mirror `D03 T01 §6`. Cheaper substitute that fails the checkpoint: a flat list of hits without file grouping, a modal that blocks until the search ends, or hiding skipped files instead of listing them.
**Chrome:** consume `SidePanelHost` (`D02 T03 §3`), `recto_tokens.g.h` tokens including `RectoSearchHitBrush`, Fluent UI System Icons (`RectoIcons`), `ui::Dialog` chrome (`D10 T02 §17`, `D02 T01 §10`), `CommandRegistry` (`D02 T03 §11`).

**Build order:**

1. Add `FilesSearchViewModel` in `src/app/viewmodels/` driving `MultiSearch` off the UI thread, marshalling progressive `FileResult` batches through `ui::Dispatcher::post` and cancelling on a new query or `Esc`. Done when: `Panels.FilesSearchProgressive` shows the first group within 250 ms on the corpus while the search continues.
2. Add the scope picker, grouped results list and `Skipped` expander in `src/app/panels/files_search_panel.h/.cpp` and register the commands. Done when: `Panels.FilesSearchGrouping` drives the panel and asserts group headers, context rows, bold match runs and the skipped list.

- [ ] Register `Search in folder...`, `Search open tabs` and `Search in files` with `Ctrl+Shift+F` in the `CommandRegistry` so each is reachable from `Ctrl+K`. Done when: `Commands.Reachability` finds all three in the Command Box and `Ctrl+Shift+F` opens the panel. Cheaper substitute: a menu item with no Command Box entry.
- [ ] Remember the last scope, `Include subfolders` and `Use index` in the `SettingsStore` and never store the query or results. Done when: restarting restores the toggles and the query box is empty.
- [ ] Show the chosen folder path, a determinate or indeterminate progress bar, `N of M files`, and a `Cancel` button while searching. Done when: cancelling leaves the partial grouped results visible, marked `Search cancelled`.
- [ ] Give the scope picker, folder button, toggles, `Cancel` and every icon-only button a `ui::Element` automation name and tooltip, and announce `N files, M results` through `AnnouncementService`. Done when: `A11y.FilesSearchInventory` finds no unnamed control. Cheaper substitute: visible labels only.
- [ ] Use only `recto_tokens.g.h` tokens and Fluent UI System Icons (`RectoIcons`) in the panel sources. Done when: `rg "#[0-9A-Fa-f]{6}" src/app/panels/files_search_panel.cpp` prints nothing.
- [ ] Add `Panels.FilesSearch*` tests in `tests/recto-tests/files_search_panel_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Panels.FilesSearch*:Commands.Reachability` passes.
- [ ] Commit: `viewer: add Search in files panel with scope picker and grouped results`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.FilesSearch*:Commands.Reachability:A11y.FilesSearchInventory` passes; `Panels.FilesSearchGrouping` fails if hits are not grouped by file.

## 8. Open a result at its location from the grouped results list

A result is only useful if it lands on the match. Activating a hit opens the file through the shared `OpenDocumentPipeline` (or focuses the tab already open on that path), scrolls to the page, centres the matched quad and highlights it with `RectoSearchHitCurrentBrush`, then runs the normal single-document find for the same query so `F3` steps through the rest of that file's hits.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it reuses the hit highlight of `design/SPEC.md` section 9.3 and the password dialog of section 9.9 (`D02 T01 §10`), with no new chrome. Design board: `design/SPEC.md` section 9.3.2 with `design/screenshots/D-SearchPanels.png` (drawn by `D06 T04 §9`).
**Job:** The user clicks `invoice` on page 3 of `a-invoices.pdf` and is looking at that match, not at page 1. Consumer: `OpenDocumentPipeline` (`D02 T01 §5`), `DocumentView` scroll (`D02 T02 §6`), `SearchViewModel` (`D03 T01 §6`).
**Treatment:** The panel calls `OpenDocumentPipeline::open(path, OpenTarget{page, quad, query, options})`; an already-open tab is focused instead of duplicated; the match quad comes from the stored `SearchHit`, re-found by the single-document engine if the file changed since the search. Cheaper substitute that fails the checkpoint: opening the file at page 1 and leaving the user to search again.
**Chrome:** consume `OpenDocumentPipeline`, `DocumentTabSession`, `recto_tokens.g.h` highlight tokens.

**Build order:**

1. Extend the open request with `OpenTarget` in the app layer without changing `D02 T01 §5`'s signature for existing callers (a new overload). Done when: `Pipeline.OpenAtTarget` opens the corpus file and reports page index 2 as the visible page.
2. Wire hit activation, keyboard `Enter` on a result row, and double-click in `FilesSearchPanel`. Done when: `Panels.FilesSearchOpenHit` activates a hit and asserts the tab, page, current-match quad and `F3` stepping.

- [ ] Reuse an existing tab on the same canonical path (case-insensitive, long-path normalised) instead of opening a duplicate. Done when: `Panels.FilesSearchReuseTab` activates two hits in one file and sees one tab.
- [ ] Re-run the single-document search on open and, when the file's size or modification time differs from the search time, show the note `This file changed since the search. Results were refreshed.` Done when: `Panels.FilesSearchStaleHit` modifies a file and sees the note and the refreshed position.
- [ ] Prompt through the standard Password dialog when the activated file is encrypted and skipped, and re-run the search for that file after unlock. Done when: `Panels.FilesSearchEncryptedUnlock` unlocks `encrypted-aes-256.pdf` and the group gains its hits.
- [ ] Open files read-only from the results (no write) and never open linked targets. Done when: the test asserts the tab's dirty flag is false and `links.pdf` opens with no navigation.
- [ ] Add `Pipeline.OpenAtTarget` and `Panels.FilesSearchOpenHit` tests in `tests/recto-tests/files_search_open_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Pipeline.OpenAtTarget:Panels.FilesSearch*Open*:Panels.FilesSearchReuseTab:Panels.FilesSearchStaleHit:Panels.FilesSearchEncryptedUnlock` passes.
- [ ] Commit: `viewer: open Search in files hits at their page and match`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Pipeline.OpenAtTarget:Panels.FilesSearchOpenHit` passes; `Panels.FilesSearchOpenHit` fails if the visible page is not the hit's page.

## 9. Settings index manager (folders, size, rebuild, delete)

The index is optional and visible. A `Search index` card in Settings (Performance and privacy, `D02 T04 §6`) states `The index stores searchable text from your PDFs on this PC, in the folder shown below. Delete it to remove that text.`, lists the indexed folders, each with file count, index size and last update, and offers `Add folder`, `Rebuild`, `Remove` and `Delete all`. Nothing is indexed until the user adds a folder or ticks `Use index` for a folder search and confirms; there is no hidden crawler.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.7 (Settings groups, `Find a setting`) with `design/screenshots/D-Settings.png`; the card follows the grouped-card pattern of `D02 T04 §5` and `§6`, and the confirm dialogs follow section 9.9.
**Job:** The user can see how much disk the index uses and where (including the Portable data folder), index a folder for fast repeat search, rebuild a doubtful index, or delete it entirely. Consumer: `SettingsStore` (`D02 T04 §2`), `IndexJob` (§5), `AppPaths` (`D02 T04 §1`).
**Treatment:** A `SearchIndexSettings` card bound to `SearchIndexManager` (list, add, rebuild, remove, delete) with the resolved index path shown read-only; deleting asks first and shows the freed size afterwards. Cheaper substitute that fails the checkpoint: a toggle with no size or location, or a delete that leaves files behind.
**Chrome:** consume `SettingsPage` frame and `Find a setting` registry (`D02 T04 §4`), `ui::Dialog` chrome (`D10 T02 §17`), `recto_tokens.g.h`, Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Implement `SearchIndexManager` in `src/core/search_index_manager.h/.cpp` (folders list persisted through `SettingsStore`, `rebuild`, `remove_folder`, `delete_all`, `status`). Done when: `SearchIndexManager.*` tests add, rebuild, remove and delete over a `TempDir` and assert files on disk.
2. Add the `Search index` card to `SettingsPage` with `Find a setting` entries and the confirm dialogs. Done when: `Settings.SearchIndexCard` drives add, rebuild and delete and asserts the displayed size changes.

- [ ] Register `Search index` rows in the `Find a setting` registry so typing `index` shows the card, and add `Rebuild search index` and `Delete search index` to the Command Box. Done when: `Settings.FindASetting` and `Commands.Reachability` both find them.
- [ ] Show the privacy statement above the folder list on the card. Done when: `Settings.SearchIndexDisclosure` finds the statement text and the resolved path in the card.
- [ ] Show the resolved index folder from `AppPaths::data_dir()` and update it live when Portable mode toggles. Done when: `Settings.SearchIndexPortable` toggles Portable mode and the shown path and the files move together.
- [ ] Ask before `Delete all` and before `Rebuild` of a folder over 1000 files, naming the folder and size. Done when: the dialogs appear and `Cancel` changes nothing. Cheaper substitute: delete on click.
- [ ] Make index-folder paths removable even when the folder no longer exists, and show `Folder not found` instead of failing. Done when: `SearchIndexManager.MissingFolder` removes a deleted folder's entry and its postings.
- [ ] Give every control an automation name and tooltip and use only `recto_tokens.g.h` tokens. Done when: `A11y.SettingsSearchIndex` finds no unnamed control and `rg "#[0-9A-Fa-f]{6}" src/app/` finds no hex in the new settings sources.
- [ ] Add `SearchIndexManager.*` and `Settings.SearchIndex*` tests in `tests/recto-tests/search_index_manager_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=SearchIndexManager.*:Settings.SearchIndex*:Settings.FindASetting` passes.
- [ ] Commit: `settings: add Search index manager card`

**Test checkpoint:** `recto-tests.exe --gtest_filter=SearchIndexManager.*:Settings.SearchIndex*` passes; `Settings.SearchIndexCard` fails if `Delete all` leaves any file under the index folder.

## Verification

- On a folder of the corpus, `Ctrl+Shift+F` with the scope `Folder...` returns hits grouped by file with pages and context, lists the encrypted file as skipped with its reason, reports the scanned file without stored OCR as `No text layer`, and never starts OCR (`MultiSearch.NoSilentOcr`); clicking a hit lands on the match (`Panels.FilesSearchOpenHit`).
- With `Use index`, a second search of the unchanged folder indexes zero files and returns identical hits (`MultiSearch.IndexedMatchesUnindexed`); touching one file re-indexes only that file (`SearchIndex.IncrementalPlan`); a flipped byte in the index file causes a rebuild, not a wrong hit (`SearchIndex.CorruptIsRebuiltNotTrusted`).
- `recto-cli find invoice --folder tests/corpus/search-folder --json` prints the same grouped hits as the app.
- A junction and a symbolic link in a searched folder are neither entered nor listed (`FileScope.NeverFollowsLinks`), and `links.pdf` triggers no navigation (`MultiSearch.NeverFollowsLinks`).
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
