---
schema_version: 1
id: jobs-and-queue
domain: 04-convert
status: active
title: "TODO-01 -- Conversion Jobs and the Queue"
track: M4
frozen: true
---

# TODO-01 -- Conversion Jobs and the Queue

> **Goal:** Every conversion in Recto runs the same way: an isolated job on its own thread with its own `recto::pdf::Document` instance, progress and cancellation through its `CancelToken`, at most two at once, visible in the SPEC queue card with honest states, and completion announced by toast or Windows notification. The individual converters (`T02` to `T05` and `D05`) plug into this frame and write nothing else.

> **Current state:** Measured 2026-10-14. No code exists. The isolation contract is foundation plan 4.9 (a conversion never shares the viewer's document; progress via `CancelToken` posted with `DispatcherQueue::TryEnqueue`), the writer pattern is plan section 5, and the surfaces are SPEC 9.5 (three-step Convert page, queue card states running, waiting, done, failed with `"Enter password"`) and 9.9 (finished-conversion notification and toast), boards `D-Convert.png` and `explorations/A-Convert.png`, `A-Dialogs.png`, `B-Dialogs.png`. Shutdown joins jobs at their registered point (`D01 T02 §10`).

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 3.3 (`ConversionJob`, `CancelToken`), 4.5, 4.9, 4.10, 5 (writer pattern and the atomic saving rule), 7 (M4).
- `design/SPEC.md` sections 9.5, 9.9, 11 (announcements twin via `D03 T02 §6`'s sink).
- -> XREF: D02 T01 §10 -- the shared `PasswordDialog` (`D02 T01 §10`) is reused for queue-card password retry in §2 and §7 here.
- -> XREF: D02 T02 §8 -- the Floating Page Toolbar's `Convert` accent button navigates into §3's Convert page.
- -> XREF: D02 T04 §5 -- the `Where to save converted files` setting in `D02 T04 §5` provides the default output directory for §4 here.
- -> XREF: D03 T02 §6 -- conversion job completion and failure invoke the polite UIA announcement sink (`IAnnouncementSink`) built in `D03 T02 §6`.
- -> XREF: D04 T02 §1 -- the first real converters register against §1's contract and §4's Step 3 options host.
- -> XREF: D04 T03 §5 -- the native `DocxWriter` and `RtfWriter` register the Word document card and Step 3 options against §3 and §4's converter-card registry.
- -> XREF: D04 T04 §7 -- the 100% native C++20 `Office & Legacy Office -> PDF` converters register the `PDF from Office, HTML, Markdown & Images` card against §3 and §4 here.
- -> XREF: D04 T05 §2 -- the Excel/CSV/XML, HTML5/Markdown/EPUB/LaTeX, and PDF/A/PDF-UA/ZUGFeRD converters register their cards and Step 3 options against §3 and §4's converter-card registry.
- -> XREF: D05 T02 §5 -- Organize saves and `D05 T02 §11`'s Protected/Unlocked PDF cards run as jobs on this frame.

## Outcome

- A job of any kind runs isolated on a 2-worker pool with its own `pdf::Document` (`§1`), reports progress, cancels cleanly leaving no output or temp file, and fails with the engine's message and a typed recovery action (`NeedsPassword` or `RetryableError` in `§2`).
- The three-step Convert page hosts Step 1 input file chips (`§3`), Step 2 converter cards with honest input-type enablement and disable reasons (`§3`), and Step 3 dynamic options with the SPEC Principle 4 capability hint line (`§4`).
- The app-wide floating `QueueCard` and its expanded list show true states (`Running`, `Waiting`, `Done`, `Failed`) at all times and survive switching between views (`§5`).
- Finished jobs raise the focus-aware SPEC Windows notification or in-app toast with working `Open file` and `Show in folder` actions (`§6`), and failed jobs offer inline `Enter password` retry or per-file batch breakdown (`§7`).

**Adjacency:** list=applicable @ D04 T01 §5; document=applicable @ D04 T01 §6; settings=applicable @ D02 T04 §5; reporting=not-applicable (job history is session-scoped by design, stated in §5); notifications=applicable @ D04 T01 §6; permissions=not-applicable (no roles; per-file password refusal is a job state, §2); audit=not-applicable (the job log lines are diagnostics, not an audit trail); exchange=not-applicable (the converters own their formats, T02 to T05); reverse=applicable @ D04 T01 §2

**Adjacency rationale:** The queue is the list (`§5`), its completion notice is both the document the user acts on and the notification surface (`§6`), and cancel is the reverse of starting a job (`§2`, `§5`). Save-location defaults live in Settings (`D02 T04 §5` owns the value, the Convert page reads it in `§4`). Session-scoped history is a deliberate boundary: Recto keeps no conversion log across restarts in v1, stated here instead of discovered.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Isolated `ConversionJob` 2-worker runtime, dedicated `pdf::Document` and `CancelToken` | D01 T02 §10 | [ ] |
| 2 | §2 | Job result & recovery state machine (`Completed`..`NeedsPassword`) and temp cleanup | §1 | [ ] |
| 3 | §3 | Convert Step 1 (`Files` drop zone & chips) and Step 2 (`What to make` card registry) | §2, D02 T01 §4 | [ ] |
| 4 | §4 | Convert Step 3 dynamic options host, capability hint line, and primary action button | §3, D02 T04 §5 | [ ] |
| 5 | §5 | App-wide floating `QueueCard` (progress ring, job states, expanded list, per-job cancel) | §4 | [ ] |
| 6 | §6 | Focus-aware completion notices (Windows toast vs in-app toast, `Open file` & folder) | §5, D03 T02 §6 | [ ] |
| 7 | §7 | Queue error recovery (`Enter password` modal retry, transient retry, batch breakdown) | §6, D02 T01 §10 | [ ] |

## 1. Isolated `ConversionJob` 2-worker runtime, dedicated `pdf::Document` and `CancelToken`

Implement `src/services/conversion.h` and `src/services/conversion.cpp` (`ConversionRunner` and `ConversionJob`): enforce foundation plan section 4.9's isolation contract where every conversion job runs on a bounded 2-concurrent-worker pool with FIFO queuing, opens its own dedicated `recto::pdf::Document` instance (never sharing the viewer's `DocumentActor`), polls its `CancelToken` for cooperative cancellation, throttles progress updates (`0.0..1.0` + step description) to the UI thread via `DispatcherQueue::TryEnqueue`, and joins cleanly at step 2 of `Runtime::shutdown()` (`D01 T02 §10`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `ConversionJobSpec`, `ConversionProgress`, `IConversionJobFactory`, and `ConversionRunner` (`max_concurrent_jobs = 2` worker threads, FIFO pending queue, active job table, and `Runtime` shutdown registration) in `src/services/conversion.h/.cpp`. Done when: submitting 4 jobs starts 2 immediately, holds 2 in `Waiting`, and starts job 3 the moment job 1 completes.
2. Enforce the isolated `pdf::Document` opening contract and throttled (`<= 30 Hz`) `CancelToken` progress callback marshalling. Done when: `Jobs.IsolationAndConcurrency` runs two concurrent conversion jobs on the same PDF file that is actively rendering tiles in a `DocumentActor` without contention or shared state under ASan.

- [ ] Define `JobId`, `JobState` (`Waiting`, `Running`, `Completed`, `Cancelled`, `Failed`, `NeedsPassword`), `ConversionProgress` (`completed_units`, `total_units`, `fraction`, `status_text`), and `ConversionJobSpec` in `src/services/conversion.h`. Done when: `src/services/conversion.h` compiles across `recto-services`, `src/app`, and `tests/recto-tests`.
- [ ] Implement `ConversionRunner` in `src/services/conversion.cpp` with a 2-thread worker pool and FIFO wait queue so at most two conversion jobs execute simultaneously (`foundation plan section 4.9`). Done when: `Jobs.TwoAtOnceBound` submits 4 barrier-synchronized jobs and asserts `active_count == 2` and `waiting_count == 2`. Cheaper substitute: spawning an unbounded `std::thread` per job, which reintroduces CPU/disk thrashing.
- [ ] Enforce by API signature that `ConversionJob` receives input file paths (`std::vector<std::filesystem::path>`) and opens its own private `recto::pdf::Document` instance on the job worker thread, never accepting a `DocumentActor*` or viewer `std::shared_ptr<pdf::Document>`. Done when: `Jobs.ViewerIsolation` proves viewing and scrolling a PDF while converting the same file on disk shares zero `pdf::Document` instances.
- [ ] Implement `ProgressReporter` tied to the job's `CancelToken`, throttling UI progress callbacks to at most one emission per `33 ms` (plus guaranteed `0%` and `100%` transitions) via `DispatcherQueue::TryEnqueue` (or synchronous test callback). Done when: `Jobs.ProgressThrottling` verifies monotonic `0.0 -> 1.0` progress reports without flooding the UI queue.
- [ ] Register `ConversionRunner::cancel_and_join_all()` with `Runtime::shutdown()` (`D01 T02 §10` step 2) so closing the application cancels all running/waiting jobs and joins both worker threads before `RenderPool` and `DocumentActor`s tear down. Done when: `Jobs.ShutdownJoin` shuts down `Runtime` mid-conversion with zero leaked threads or dangling pointers under ASan.
- [ ] Register a built-in diagnostic test job (`PageCountCopyTestJob`) for unit and UI harness verification before `D04 T02` lands. Done when: `PageCountCopyTestJob` runs end-to-end through `ConversionRunner`.
- [ ] Add `Jobs.TwoAtOnceBound`, `Jobs.ViewerIsolation`, `Jobs.ProgressThrottling`, and `Jobs.ShutdownJoin` in `tests/recto-tests/conversion_jobs_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Jobs.*` passes under ASan.
- [ ] Commit: `feat(convert): implement isolated 2-worker ConversionRunner and CancelToken progress marshalling`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Jobs.TwoAtOnceBound:Jobs.ViewerIsolation:Jobs.ProgressThrottling:Jobs.ShutdownJoin` passes under ASan.
**Freeze check:** `ConversionJob` workers open their own dedicated `recto::pdf::Document` instances and never share a `DocumentActor` or `pdf::Document` with the viewer; `ConversionRunner::cancel_and_join_all()` joins both worker threads at step 2 of `Runtime::shutdown()` before `RenderPool` and `DocumentActor`s tear down.

## 2. Job result & recovery state machine (`Completed`..`NeedsPassword`) and temp cleanup

Implement the terminal result and recovery state machine in `src/services/conversion.cpp`: classify job outcomes into `Completed` (with output file paths), `Cancelled`, `Failed` (with `PdfError` message and per-file batch breakdown), and `NeedsPassword` (preserving the job spec and identifying the encrypted input file for in-place password resubmission), and enforce that any cancelled or failed job deletes all temporary staging files and leaves zero partial output files on disk.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `ConversionResult` (`state`, `output_paths`, `error_message`, `failed_file_path`, `recovery_kind`, `per_file_results`) and `ConversionRunner::resubmit_with_password(JobId, std::string password)` / `retry_job(JobId)` in `src/services/conversion.cpp`. Done when: an encrypted corpus PDF (`AES-256`) transitions the job to `NeedsPassword` and calling `resubmit_with_password` re-queues the same `JobId` entry in place and completes cleanly.
2. Implement the job-level temporary-file staging tracker (`JobTempScope`) so tripping `CancelToken` or returning an error mid-job automatically unlinks any uncommitted temp files and leaves pre-existing destination files byte-for-byte untouched. Done when: `Jobs.CancelAndAtomicCleanup` passes under ASan.

- [ ] Define `JobRecoveryKind` (`None`, `NeedsPassword`, `RetryableError`), `PerFileJobOutcome` (`input_path`, `output_path`, `succeeded`, `error_message`), and `ConversionResult` in `src/services/conversion.h`. Done when: `ConversionResult` captures both single-file and multi-file batch outcomes.
- [ ] Map `PdfErrorCode::PasswordRequired` and `InvalidPassword` from `pdf::Document::open` (`D01 T01 §9`) to `JobState::NeedsPassword` with `recovery_kind = JobRecoveryKind::NeedsPassword` and `failed_file_path` set to the encrypted file. Done when: submitting an encrypted PDF without a password yields `JobState::NeedsPassword` rather than a generic failure.
- [ ] Implement `ConversionRunner::resubmit_with_password(JobId id, std::string password)` storing the supplied password in the job's per-file credential map (in memory only, zero disk persistence) and transitioning the existing `JobId` back to `Waiting` -> `Running`. Done when: `Jobs.PasswordResubmission` transitions `NeedsPassword -> Running -> Completed` on the same `JobId` without creating a duplicate queue row.
- [ ] Implement `ConversionRunner::retry_job(JobId id)` for `JobRecoveryKind::RetryableError` (such as a transient file-sharing lock on the output path) re-queuing the existing `JobId` in place. Done when: `Jobs.TransientRetryRecovery` transitions `Failed -> Running -> Completed` on the same `JobId`.
- [ ] Implement `JobTempScope` tracking all sibling temporary paths created during a job and purging any uncommitted files on `CancelToken` abort, exception, or error return. Done when: cancelling a job at 50% progress leaves zero `.tmp` or partial output files in the destination directory (`Jobs.CancelCleanliness`). Cheaper substitute: leaving partial 0-byte or half-written output files on disk when the user clicks Cancel.
- [ ] Record per-file success/failure outcomes in multi-file batch jobs so if file 2 of 3 is corrupt, files 1 and 3 still complete (or report clearly) and the batch summary lists the exact failure reason for file 2. Done when: `Jobs.BatchPartialFailure` verifies per-file status reporting.
- [ ] Commit: `convert: isolated ConversionJob runtime with progress and cancel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Jobs.*` passes under ASan: isolation while viewing, two-at-once bound, cancel cleanliness, and password recovery resubmission.
**Freeze check:** Password resubmission, transient retry, and `JobTempScope` cleanup execute strictly within the isolated `ConversionRunner` worker pool (`<= 2` workers) without touching any viewer `DocumentActor`.

## 3. Convert Step 1 (`Files` drop zone & chips) and Step 2 (`What to make` card registry)

Build the first two steps of the SPEC 9.5 Convert page (`src/app/views/ConvertPage.xaml/.cpp` and `src/services/convert_page_view_model.cpp`): Step 1 (`1. Files`) with drag-and-drop zone, `Choose files...` picker (`FileOpenPicker`), `Use current open PDF` quick-fill when navigating from the viewer, and removable file chips displaying filename, format badge (`PDF`, `DOCX`, `DOC`, `XLSX`, `XLS`, `PPTX`, `PPT`, `RTF`, `HTML`, `MD`, `CSV`, `PNG`, `JPG`, `TIFF`, `SVG`, `XPS`), file size, and remove (`X`) button; and Step 2 (`2. What to make`) with the converter-card registry and responsive card grid (`Word document`, `Excel & Data`, `HTML, Markdown, EPUB & LaTeX`, `Images, TIFF, SVG & XPS`, `Plain text`, `Searchable PDF (OCR)`, `Extract Tables & Images`, `Optimize & Compress PDF`, `PDF/A & E-Invoice`, `PDF from Office, HTML, Markdown & Images`, `Protected & Signed PDF`, `Unlocked PDF`), dynamically computing card enablement from the actual Step 1 input file types and showing an explicit human-readable disable reason when a card does not apply.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (Step 1 `Files` chip bar and drop zone; Step 2 `What to make` option-card grid with accent border, tint glow, check badge, and disabled-reason subtitles) with `design/screenshots/D-Convert.png`.
**Job:** The user can drop or pick one or more files (or bring over the currently open PDF), see them as removable chips, and pick what output format to create from a self-explaining grid of cards. Consumer: Step 3 options host (`§4`) and converter registrations across `D04 T02`-`T05` and `D05 T02 §11`.
**Treatment:** Registry-driven `ConverterCardDescriptor` model where each converter registers its ID, title, glyph, description, and `can_accept_inputs(span<InputFileInfo>) -> {enabled, disable_reason}`. Cheaper substitute that fails the checkpoint: hard-coding `if/else` chains inside `ConvertPage.xaml.cpp` or enabling every card regardless of input file type and failing only after the user clicks Convert.
**Chrome:** consume the mode-switcher `Convert` navigation slot (`D02 T01 §4`), the floating page toolbar `Convert` button (`D02 T02 §8`), and `RectoColors.xaml` card selection tokens (`RectoAccentBrush` border, `RectoAccentSubtleBrush` fill glow, check badge).

**Build order:**

1. Implement `ConverterRegistry` and `ConvertPageViewModel` (`src/services/convert_page_view_model.h/.cpp`) managing Step 1 `InputFileChip` items (file extension/magic-byte classification, size formatting, add/remove/clear) and Step 2 `ConverterCardState` evaluation (auto-selecting the first compatible card when inputs change, and surfacing explicit disable reasons such as `"Add Word, Excel, PowerPoint, HTML or Markdown files to use this"` when only PDFs are loaded). Done when: `ConvertUi.RegistryAndEnablement` verifies card enablement and disable reasons across PDF-only, Office-only, image-only, and mixed input sets.
2. Build Step 1 and Step 2 of `ConvertPage.xaml/.cpp` matching `D-Convert.png`. Done when: dropping files or clicking `Convert` from the floating page toolbar populates Step 1 chips and updates Step 2 cards live.

- [ ] Define `InputFileKind` (`Pdf`, `Word`, `Excel`, `PowerPoint`, `Rtf`, `Html`, `Markdown`, `Csv`, `Xml`, `RasterImage`, `MultipageTiff`, `Svg`, `Xps`, `LegacyOfficeBinary`) and `ConverterCardDescriptor` (`id`, `title`, `subtitle`, `icon_glyph`, `honest_hint`, `accept_predicate`, `default_options_factory`, `job_factory`) in `src/services/convert_page_view_model.h`. Done when: adding a new converter in `D04 T02`-`T05` is a single `ConverterRegistry::register_card` call. Cheaper substitute that fails the checkpoint: hard-coded converter branches inside XAML code-behind.
- [ ] Implement Step 1 file ingestion in `ConvertPageViewModel`: `add_files`, `remove_file`, `clear_files`, and automatic pre-population with the active viewer tab's PDF when the user switches to `Convert` from an open document (`D02 T02 §8`). Done when: clicking `Convert` on the viewer's floating page toolbar opens `ConvertPage` with the current PDF already in Step 1.
- [ ] Build the Step 1 `Files` XAML section (`ConvertPage.xaml`) with drag-and-drop (`DragOver` / `Drop` accepting files from Explorer), `Choose files...` button, and a wrapping `ItemsRepeater` of removable file chips (icon, filename, formatted byte size, `Remove <filename>` button with `>= 32 epx` target). Done when: keyboard and mouse users can add and remove individual file chips cleanly.
- [ ] Compute Step 2 card enablement and explicit disable reasons whenever Step 1 chips change (e.g., `PDF from Office, HTML, Markdown & Images` shows `"Add Word, Excel, PowerPoint, HTML or Markdown files to use this"` when only `.pdf` chips are present; PDF-output cards show `"Add a PDF file to use this converter"` when only `.docx` chips are present). Done when: `ConvertUi.DisableReasons` verifies non-empty disable reasons for every incompatible input combination.
- [ ] Render the Step 2 `What to make` card grid using `RectoColors.xaml` tokens: selected card displays `2 epx` `RectoAccentBrush` border, `RectoAccentSubtleBrush` background tint, and top-right check badge; disabled cards display reduced opacity and the explicit disable reason line. Done when: Step 2 visual layout matches `D-Convert.png`.
- [ ] Support full keyboard navigation (`Arrow` keys / `Tab` + `Space`/`Enter`) across the Step 2 converter cards with accessible names announcing `<Title>, <Subtitle>, <Selected | Disabled: reason>`. Done when: `A11yInventory` (`D03 T02 §3`) passes on `ConvertPage`.
- [ ] Add `ConvertUi.RegistryAndEnablement` and `ConvertUi.DisableReasons` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=ConvertUi.Registry*:ConvertUi.Disable*` passes.
- [ ] Commit: `feat(convert): build ConvertPage Step 1 file chips and Step 2 ConverterRegistry card grid`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertUi.RegistryAndEnablement:ConvertUi.DisableReasons` passes, and Step 1 / Step 2 match `D-Convert.png`.
**Freeze check:** Step 1 file chip inspection reads only file metadata and magic bytes; when pre-populated from the active viewer tab, it copies only the `std::filesystem::path` and never shares the viewer's `DocumentActor` or `pdf::Document`.

## 4. Convert Step 3 dynamic options host, capability hint line, and primary action button

Build Step 3 (`3. Options`) of `ConvertPage` (`src/app/views/ConvertPage.xaml/.cpp`, `SPEC 9.5`, `D-Convert.png`, and `explorations/B-Convert.png`): a dynamic options host that renders the selected converter card's typed option schema (shared controls for `Pages` `All` / custom range, `Save to` `Same folder as original` / `Ask every time` / chosen folder seeded from `D02 T04 §5`, and `If file exists` `Add number (file (2))` / `Replace` / `Skip`, plus converter-specific controls), displays the **SPEC Principle 4 honest capability hint line** (`"Works best on text documents..."`), and drives the dynamic primary action button (`"Convert to Word"`, `"Save 9 images"`, etc.) to enqueue a `ConversionJob` into `§1`'s runner.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 2 (Principle 4: honest conversion hint line), 9.5 (Step 3 Options panel and primary action button), and 9.7 (`Where to save converted files` setting integration), with `design/screenshots/D-Convert.png` and `design/screenshots/explorations/B-Convert.png`.
**Job:** After choosing input files and a target card, the user can configure page ranges, output folder, collision behavior, and format-specific options, read an honest one-line note about what the conversion does best, and start the job with one click. Consumer: `ConversionRunner` (`§1`) and all converter option schemas in `D04 T02`-`T05`.
**Treatment:** Declarative/template-driven Step 3 options host combining common output settings (`Pages`, `Save to`, `If file exists`) with converter-specific option rows, validating page ranges inline before enabling the primary action button. Cheaper substitute that fails the checkpoint: omitting `Save to` / `If file exists` collision handling, ignoring the Settings default output folder (`D02 T04 §5`), or hiding the honest capability hint line.
**Chrome:** consume `RectoColors.xaml` form control tokens and `D02 T04 §5`'s `Where to save converted files` preference.

**Build order:**

1. Implement the shared Step 3 option model (`CommonConvertOptions`: page-range mode `AllPages` vs `CustomRange` validated via `PageRangeParser`, output directory mode initialized from `D02 T04 §5`, collision policy `AppendNumber` / `Overwrite` / `SkipExisting`, and dynamic primary button label formatter) in `src/services/convert_page_view_model.cpp`. Done when: invalid page ranges (`"99-105"` on a 10-page PDF or `"abc"`) show an inline validation message and disable the primary action button.
2. Build the Step 3 XAML options host, honest hint banner, and primary action button in `ConvertPage.xaml/.cpp`, wiring button activation to `ConversionRunner::submit`. Done when: `ConvertUi.Step3OptionsAndSubmit` runs the diagnostic test job end-to-end from `ConvertPage` into `ConversionRunner`.

- [ ] Implement `CommonConvertOptions` (`page_range_mode`, `custom_page_range`, `save_location_mode`, `custom_output_folder`, `collision_policy`) in `src/services/convert_page_view_model.h/.cpp`, seeding `save_location_mode` from Settings `Where to save converted files` (`D02 T04 §5`). Done when: changing the default save location in Settings updates the initial Step 3 `Save to` selection.
- [ ] Implement `resolve_output_path(input_path, target_ext, CommonConvertOptions)` handling `SameFolderAsInput`, `CustomFolder`, and `AppendNumber` (`report (2).docx`, `report (3).docx` when `report.docx` already exists on disk). Done when: `ConvertUi.CollisionResolution` generates conflict-free numbered filenames without overwriting existing files unless `Overwrite` is explicitly chosen.
- [ ] Validate custom page ranges (`"1-5, 8, 11-13"`) live against the input PDF's page count, displaying an inline warning and disabling the primary action button when the range syntax is invalid or exceeds the document page count. Done when: `ConvertUi.PageRangeValidation` rejects out-of-range and malformed strings.
- [ ] Build the Step 3 `Options` XAML container (`ConvertPage.xaml`) hosting the selected card's converter-specific option controls above the shared `Pages`, `Save to` (with `Browse...` `FolderPicker`), and `If file exists` controls. Done when: switching between cards in Step 2 swaps the converter-specific Step 3 rows smoothly without losing Step 1 files.
- [ ] Render the **SPEC Principle 4 honest capability hint line** (`ConverterCardDescriptor::honest_hint`, e.g., `"PDF to Word works best on text documents. Scanned pages use built-in Neural + Windows OCR."`) in `RectoTextSecondaryBrush` directly beside/below the Step 3 action area. Done when: every registered converter card displays its non-empty honest hint line in Step 3. Cheaper substitute that fails the checkpoint: omitting the capability hint text specified in `SPEC 9.5`.
- [ ] Compute the primary action button label dynamically from the selected card and input/page count (e.g., `"Convert to Word"`, `"Convert 3 files to Word"`, `"Save 9 images"` matching `D-Dialogs.png`) and enqueue the job into `ConversionRunner` (`§1`) on click. Done when: clicking the primary button submits the job and activates `QueueCard` (`§5`).
- [ ] Add `ConvertUi.Step3OptionsAndSubmit`, `ConvertUi.CollisionResolution`, and `ConvertUi.PageRangeValidation` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=ConvertUi.Step3*:ConvertUi.Collision*:ConvertUi.PageRange*` passes.
- [ ] Commit: `feat(convert): implement ConvertPage Step 3 dynamic options host and honest capability hint`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertUi.*` passes, verifying Step 3 options binding, page-range validation, numbered collision resolution, and job submission.
**Freeze check:** Step 3 job submission passes only immutable option structs and filesystem paths to `ConversionRunner::submit`; the UI thread never parses or converts documents.

## 5. App-wide floating `QueueCard` (progress ring, job states, expanded list, per-job cancel)

Build the app-wide floating `QueueCard` (`src/app/controls/QueueCard.xaml/.cpp` and `src/services/queue_view_model.cpp`, `SPEC 9.5`, `D-Convert.png`, and `explorations/A-Convert.png`) hosted in `AppShell` (`D02 T01 §4`) so it remains visible bottom-right across view switches (`Convert -> Document -> Organize -> Home`) while any job is active or has unread results: compact summary pill with animated progress ring, expand/collapse toggle into the full job list, all four SPEC row states (`Running` with progress bar & `Cancel`, `Waiting` in FIFO order with `Cancel`, `Done` with `Open file` & `Show in folder`, and `Failed` / `NeedsPassword` with error message & recovery button), and `Clear completed` dismissal.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (`QueueCard` compact pill and expanded list states: `Running`, `Waiting`, `Done`, `Failed`) with `design/screenshots/D-Convert.png` and `design/screenshots/explorations/A-Convert.png`.
**Job:** The user can monitor active and queued conversions from anywhere in the app, switch back to reading a PDF while a batch converts in the background, cancel any running or waiting job, or open completed outputs directly from the queue card. Consumer: `AppShell` overlay slot (`D02 T01 §4`), `ConversionRunner` (`§1`), and Organize save jobs (`D05 T02 §5`).
**Treatment:** Shell-level `QueueCard` bound to `QueueViewModel` observing `ConversionRunner`, surviving mode navigation without interruption. Cheaper substitute that fails the checkpoint: a progress bar inside `ConvertPage` that disappears or cancels the job when the user switches back to the document viewer.
**Chrome:** consume `RectoColors.xaml` card elevation, `RectoAccentBrush` progress ring/bar, `RectoSuccessBrush` done badge, `RectoDangerBrush` failed badge, and `AppShell`'s bottom-right overlay slot.

**Build order:**

1. Implement `QueueViewModel` (`src/services/queue_view_model.h/.cpp`) observing `ConversionRunner` job lifecycle events, maintaining `ObservableVector<QueueItemViewModel>`, aggregate progress fraction, `"Converting k of N..."` summary text, per-job `cancel(JobId)`, `dismiss(JobId)`, and `clear_completed()`. Done when: `Jobs.QueueViewModel` verifies all four item states (`Running`, `Waiting`, `Done`, `Failed`) and aggregate progress calculations.
2. Build `QueueCard.xaml/.cpp` in `src/app/controls/` and host it in `AppShell.xaml` (`D02 T01 §4`) so it persists across page switches. Done when: screenshots of the collapsed and expanded queue card match `D-Convert.png` and `explorations/A-Convert.png`, and switching from `ConvertPage` to `DocumentView` mid-job keeps the `QueueCard` updating smoothly.

- [ ] Implement `QueueItemViewModel` (`job_id`, `title`, `input_filename`, `output_filename`, `state`, `progress_fraction`, `status_text`, `error_message`, `recovery_kind`) and `QueueViewModel` in `src/services/queue_view_model.h/.cpp`. Done when: `QueueViewModel` reflects `ConversionRunner` state transitions on the UI thread via `DispatcherQueue`.
- [ ] Build the compact `QueueCard` header pill (bottom-right `24 epx` inset): animated `ProgressRing` while jobs run (respecting `D02 T04 §8` `ReducedMotion`), summary label (`"Converting 1 of 2..."` or `"2 conversions complete"`), expand/collapse chevron button, and close (`X`) button when all jobs are terminal. Done when: the compact header pill matches `D-Convert.png`.
- [ ] Build the expanded `QueueCard` job list (`explorations/A-Convert.png`) rendering distinct templates for the four SPEC states: `Running` (filename, target format subtitle, determinate `ProgressBar`, percentage, and `Cancel` button), `Waiting` (filename, `"Waiting in queue..."` subtitle, and `Cancel` button), `Done` (green check icon in `RectoSuccessBrush`, output filename, `Open` button, and `Show in folder` button), and `Failed` / `NeedsPassword` (warning/error icon in `RectoDangerBrush` / `RectoWarningBrush`, engine error reason, and recovery action button `Enter password...` or `Retry`). Done when: all four states are demonstrable on demand in `Jobs.QueueStatesUi`. Cheaper substitute that fails the checkpoint: showing only a single spinner with no per-job list or per-job cancel button.
- [ ] Host `QueueCard` in `AppShell`'s top-level overlay grid (`D02 T01 §4`) above mode pages so navigating between `Convert`, `Document`, `Organize`, and `Home` never unloads `QueueCard` or interrupts running jobs. Done when: `Jobs.CrossPageSurvival` starts a job on `ConvertPage`, navigates to `DocumentView`, verifies progress updates continue, and finishes cleanly.
- [ ] Wire per-job `Cancel` to `ConversionRunner::cancel(JobId)` (`§2`), immediately flipping the row to cancelled/removed and confirming zero partial output files remain. Done when: clicking `Cancel` on a running or waiting row aborts the job within `< 50 ms`.
- [ ] Document and enforce session-scoped queue history (clearing completed items on app exit with zero persistent conversion history log on disk). Done when: restarting the app starts with a clean empty queue.
- [ ] Commit: `convert: three-step Convert page and the job queue card`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Jobs.QueueViewModel:Jobs.QueueStatesUi:Jobs.CrossPageSurvival` passes, and display-session screenshots match `D-Convert.png` and `explorations/A-Convert.png`.
**Freeze check:** `QueueCard` and `QueueViewModel` receive progress and state updates exclusively via `DispatcherQueue::TryEnqueue` and cancel jobs through cooperative `CancelToken` signalling without blocking the UI thread.

## 6. Focus-aware completion notices (Windows toast vs in-app toast, `Open file` & folder)

Implement SPEC 9.9's focus-aware completion notification system (`src/app/notifications/JobNotificationService.h/.cpp`): when a job completes while the Recto window is **unfocused** or minimized, raise a native Windows App SDK `AppNotification` (`"<output> is ready"`, with `Open file` and `Show in folder` buttons matching `explorations/A-Dialogs.png`); when the Recto window is **focused**, show the non-modal in-app completion toast (`explorations/B-Dialogs.png`) with the same `Open file` and `Show in folder` actions; and emit the polite UIA accessibility announcement through `D03 T02 §6`'s `IAnnouncementSink`.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.9 (finished-conversion Windows notification and in-app toast) and 11 (polite UIA completion announcement via `D03 T02 §6`) with `design/screenshots/explorations/A-Dialogs.png` and `design/screenshots/explorations/B-Dialogs.png`.
**Job:** The user learns the moment their converted file is ready whether they are working in another app or reading inside Recto, and can open the output file or reveal it in File Explorer with one click. Consumer: `AppShell` tab opener (`Open file`), Windows Explorer (`SHOpenFolderAndSelectItems` for `Show in folder`), and `IAnnouncementSink` (`D03 T02 §6`).
**Treatment:** Focus-state router (`Window::Activated` tracking `WindowActivationState::Deactivated` vs active) dispatching either `Microsoft::Windows::AppNotifications::AppNotificationManager` XML payload (`action=open&path=...`, `action=reveal&path=...`) or the in-app `InAppCompletionToast` control, sharing one `JobOutputActions` handler with `QueueCard` (`§5`). Cheaper substitute that fails the checkpoint: showing only a static label on `ConvertPage`, or firing a notification whose `Open file` / `Show in folder` buttons do nothing.
**Chrome:** consume `InAppCompletionToast` styled with `RectoColors.xaml` tokens (`explorations/B-Dialogs.png`) and share `JobOutputActions` across `QueueCard`, `InAppCompletionToast`, and `AppNotificationManager`.

**Build order:**

1. Implement `JobOutputActions` (`open_output_file(path)` opening `.pdf` outputs in a new Recto tab via `D02 T01 §9` and non-PDF outputs via `ShellExecuteExW` / `Launcher::LaunchFileAsync`, plus `show_in_folder(path)` invoking `SHOpenFolderAndSelectItems` so Explorer highlights the exact file, with an honest error toast if the file was moved or deleted) and `JobNotificationService` in `src/app/notifications/JobNotificationService.cpp`. Done when: `Jobs.OutputActions` verifies `.pdf` tab opening, Explorer folder selection, and missing-file handling.
2. Build `InAppCompletionToast` (`explorations/B-Dialogs.png`) and the Windows `AppNotification` XML builder + activation handler (`explorations/A-Dialogs.png`), keyed on window focus state, and wire `IAnnouncementSink::announce_job_completion` (`D03 T02 §6`). Done when: both notification forms fire in `Jobs.Notices` and match `A-Dialogs.png` and `B-Dialogs.png`.

- [ ] Implement `JobOutputActions::open_output_file` and `show_in_folder` in `src/app/notifications/JobOutputActions.cpp`, checking `std::filesystem::exists(path)` first and displaying an honest non-crashing message (`"The file '<name>' was moved or deleted."`) if the output file was removed after completion. Done when: `QueueCard`, `InAppCompletionToast`, and Windows toast activation all call the exact same `JobOutputActions` methods.
- [ ] Track window focus state (`is_window_focused()`) via `MainWindow::Activated` (`WindowActivationState::CodeActivated` / `PointerActivated` vs `Deactivated`) with an injectable test override (`set_simulated_focus_state`). Done when: `JobNotificationService` routes focused completions to `InAppCompletionToast` and unfocused completions to Windows `AppNotificationManager`.
- [ ] Build `InAppCompletionToast` (`src/app/controls/InAppCompletionToast.xaml/.cpp`) matching `explorations/B-Dialogs.png` (`"<output> is ready"`, `Open file` button, `Show in folder` button, dismiss `X`, auto-dismiss after `8 s` unless hovered or keyboard-focused). Done when: the focused toast screenshot matches `explorations/B-Dialogs.png`.
- [ ] Build the Windows App SDK `AppNotification` payload (`explorations/A-Dialogs.png`) with title `"<output> is ready"`, body `"Converted from <input>"`, and action buttons `action=open&path=<uri>` (`Open file`) and `action=reveal&path=<uri>` (`Show in folder`), plus `AppNotificationManager::NotificationInvoked` handling when the app is running or re-launched. Done when: invoking the notification payload opens the output or reveals it in Explorer (`Jobs.WindowsNotificationPayload`). Cheaper substitute that fails the checkpoint: notifications that only inform without working `Open file` and `Show in folder` buttons.
- [ ] Invoke `IAnnouncementSink::announce_job_completion` and `announce_job_failure` (`D03 T02 §6`) whenever a job reaches a terminal state so screen-reader users hear `"Conversion complete: <output> is ready"` politely. Done when: the `D03 T02 §6` announcement listener records the completion event.
- [ ] Add `Jobs.OutputActions`, `Jobs.Notices`, and `Jobs.WindowsNotificationPayload` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=Jobs.OutputActions:Jobs.Notices:Jobs.WindowsNotificationPayload` passes.
- [ ] Commit: `feat(convert): implement focus-aware Windows notification, in-app toast, and IAnnouncementSink`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Jobs.OutputActions:Jobs.Notices:Jobs.WindowsNotificationPayload` passes, captures of both notice forms match `explorations/A-Dialogs.png` and `B-Dialogs.png`, and `D03 T02 §6`'s announcement listener records the completion.
**Freeze check:** Completion notifications are dispatched after the worker thread's `AtomicFileWriter` commit has completed and closed the output handle; opening a `.pdf` output spawns a new `DocumentActor` via `TabModel` without sharing the conversion worker's `pdf::Document`.

## 7. Queue error recovery (`Enter password` modal retry, transient retry, batch breakdown)

Wire the interactive error recovery flows in the expanded `QueueCard` (`§5`): `NeedsPassword` recovery (clicking `Enter password` on a queue item opens the shared `PasswordDialog` from `D02 T01 §10` for that file, and submitting the password calls `ConversionRunner::resubmit_with_password(JobId, password)` from `§2`, replacing the failed row's state in place `NeedsPassword -> Running -> Done` without duplicating the queue entry), transient `Retry` recovery (clicking `Retry` on a `RetryableError` item calls `ConversionRunner::retry_job(JobId)` in place), and batch failure breakdown (multi-file jobs that encounter per-file errors display an expandable per-file breakdown `2 of 3 succeeded; 1 failed` with `Open folder` for the succeeded files and `Retry failed` / `Dismiss` for the failed entry).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.5 (`QueueCard` failed state with `"Password required"` and `"Enter password"` button) and 9.9 (`PasswordDialog` from `D02 T01 §10`) with `design/screenshots/explorations/A-Convert.png`.
**Job:** When a queued conversion stops because a PDF is password-protected or an output file was temporarily locked, the user can fix the problem right from the queue card and finish the job without starting over. Consumer: `QueueCard` (`§5`), `PasswordDialog` (`D02 T01 §10`), and `ConversionRunner` (`§2`).
**Treatment:** In-place `JobId` state replacement on password or transient retry recovery, reusing `D02 T01 §10`'s `PasswordDialog` with wrong-password retry handling. Cheaper substitute that fails the checkpoint: forcing the user to delete the failed job, go back to Step 1, and re-create the entire job from scratch, or appending a second duplicate row in the queue on retry.
**Chrome:** consume `QueueCard` (`§5`) and `PasswordDialog` (`D02 T01 §10`). Do not invent a second password modal for conversions.

**Build order:**

1. Wire the `Enter password` action in `QueueItemViewModel` (`§5`) to open `PasswordDialog` (`D02 T01 §10`) for `failed_file_path` and invoke `ConversionRunner::resubmit_with_password(job_id, password)`. Done when: entering a wrong password keeps the job in `NeedsPassword` with a clear retry message, and entering the right password transitions the same queue row to `Running -> Done`.
2. Wire `Retry` on transient I/O errors (e.g., output file locked in another app) and per-file batch failure breakdown rows in `QueueCard`. Done when: `Jobs.PasswordQueueRecoveryUi` and `Jobs.BatchErrorBreakdownUi` pass end-to-end.

- [ ] Wire the `Enter password` button on `JobState::NeedsPassword` rows in `QueueCard` (`explorations/A-Convert.png`) to open `D02 T01 §10`'s `PasswordDialog` naming the encrypted file (`"Enter the password to convert '<filename>'"`). Done when: clicking `Enter password` opens `PasswordDialog`.
- [ ] Call `ConversionRunner::resubmit_with_password(job_id, password)` (`§2`) when `PasswordDialog` confirms, updating the existing `QueueItemViewModel` in place (`NeedsPassword -> Waiting/Running -> Done`) rather than appending a new queue item. Done when: `Jobs.PasswordQueueRecoveryUi` verifies `QueueViewModel::items().size()` remains `1` across failure, password entry, and completion. Cheaper substitute that fails the checkpoint: creating a duplicate queue row on retry.
- [ ] Re-prompt with an inline error in `PasswordDialog` (`"Incorrect password. Try again."`) if the resubmitted password is rejected by `pdf::Document::open` (`PdfErrorCode::InvalidPassword`). Done when: entering an invalid password followed by the valid password succeeds cleanly.
- [ ] Wire `Retry` on `JobRecoveryKind::RetryableError` rows in `QueueCard` to invoke `ConversionRunner::retry_job(job_id)` (`§2`), resubmitting the job in place. Done when: `Jobs.TransientRetryUi` verifies the recovery callback and in-place resubmission.
- [ ] Render per-file batch outcome details when a multi-file job partially fails (`"2 of 3 files converted"`), providing `Show in folder` for the completed outputs and `Retry failed file` / `Enter password` for the specific failed file. Done when: `Jobs.BatchErrorBreakdownUi` converts a batch of 2 valid PDFs + 1 encrypted PDF, unlocks the 1 encrypted file from the queue, and completes all 3.
- [ ] Add `Dismiss` (`X`) on failed/cancelled rows and `Clear all` when no jobs are running. Done when: dismissing a failed row removes it cleanly from `QueueCard`.
- [ ] Commit: `convert: completion notifications, toasts and queue error actions`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Jobs.PasswordQueueRecoveryUi:Jobs.TransientRetryUi:Jobs.BatchErrorBreakdownUi` passes, proving in-place password recovery and batch breakdown in `QueueCard`.
**Freeze check:** Queue recovery resubmissions (`resubmit_with_password`, `retry_job`, and per-file batch retry) re-enter the `ConversionRunner` 2-worker FIFO pool and open fresh isolated `pdf::Document` instances on worker threads.

## Verification

- Every SPEC 9.5 and job-related 9.9 element accounted for on rendered surfaces (`D-Convert.png`, `explorations/A-Convert.png`, `B-Convert.png`, `A-Dialogs.png`, `B-Dialogs.png`).
- Job isolation re-proven with real converters once `D04 T02` lands.
- `recto-tests` (`Jobs.*`, `ConvertUi.*`) full suite green; `python scripts/todo-graph.py validate` passes.
