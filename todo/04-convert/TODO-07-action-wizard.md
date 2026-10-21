---
schema_version: 1
id: action-wizard
domain: 04-convert
status: active
title: "TODO-07 -- Action Wizard: Saved Multi-Step Actions over Files and Folders"
track: M5
---

# TODO-07 -- Action Wizard: Saved Multi-Step Actions over Files and Folders

> **Goal:** Recto matches Acrobat Pro's Action Wizard: an action is a saved, named, ordered list of steps (OCR, optimize, watermark, header and Bates, flatten, sanitize, redact by pattern, encrypt, metadata, PDF/A, preflight, convert and export, save with a naming template) stored as versioned JSON, run over selected files or a whole folder as jobs in the existing queue, with a per-file result and a summary report. It keeps Recto's guarantees: inputs are never overwritten, outputs are new uniquely named files, no step runs PDF JavaScript, nothing leaves the machine, and a redaction step that could not see a page fails the file instead of passing it.

> **Current state:** Measured 2026-10-21. No action, step or batch code exists. Every operation an action can call is specified but not built: OCR and optimization (`D04 T02 §7`, `§11`), watermark, header and Bates, encryption and redaction (`D05 T01 §5`, `§6`, `§7`, `§10`), annotation and form flattening (`D03 T03 §2`, `§6`), editable metadata (`D02 T03 §10`), `PDF/A` (`D04 T05 §6`), preflight and `PDF/X` (`D04 T06 §6`, `§10`), converters (`D04 T01 §3`), and safe folder enumeration (`D03 T05 §2`). The job runtime and queue are `D04 T01 §1`, `§2`, `§5`, `§7`, and `D04 T01` is frozen: this file depends on its sections and does not edit them.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` decisions D9 (no PDF JavaScript), D16 (offline, no telemetry), D17, D18, and section 4 (jobs open their own `pdf::Document`).
- `AGENTS.md` architecture rules: every file write is temp-file-then-replace, organize and authoring operations work on a copy, the open viewer document is never modified in place, macros and scripts never run, every command is reachable from `Ctrl+K`.
- `docs/research/2026-10-20-pdfcraft-issue-lessons.md`: pdfcraft #799 (a batch result silently replaced an earlier one), #1795 (folder jobs overwrote same-name outputs and existing documents), #1805 (the output collision check raced with publication), #1804 (a staged file was not synced before success was reported). Watched folders (pdfcraft #3613) are not in this file; they are recorded as a demand-ledger row, not as unowned work.
- `design/SPEC.md` sections 9.5 (Convert page and queue card), 9.9 (dialog chrome and completion notices), 11 (accessibility).
- `D04 T01 §1`, `§2`, `§3`, `§4`, `§5`, `§6`, `§7` (runner, per-file results, converter registry, `resolve_output_path`, queue, notices, password recovery), `D04 T02 §1` (`AtomicFileWriter`), `D03 T05 §2` (`FileScope`), `D04 T06 §2` (`BoundedJson`, profile schema), `D06 T03 §5` (save fault injection).
- Cross-file cross-references to this file are added by the coordinator, in both directions.
- -> XREF: D04 T01 §2 -- every action step runs as a job in this runtime.
- -> XREF: D03 T05 §2 -- recursive input sets come from `FileScope`.
- -> XREF: D05 T02 §9 -- redact-by-pattern steps share the Find and Redact presets.
- -> XREF: D07 T03 §6 -- watched-folder automation is a ledger row, not a step type.

## Outcome

- An action authored in the editor saves as `<data_dir>\actions\<id>.action.json`, loads back identically, and a hostile or newer action file is refused or preserved with a reason, never partly run (`§1`, `§2`, `§3`).
- `recto-cli action run sample.action.json in1.pdf in2.pdf --out out\` and the Run panel produce, for each input, a new uniquely named output in the chosen folder; no input and no existing file is ever replaced, even when the output folder is the input folder or two workers publish the same name at once (`§6`, `§9`, `§14`).
- Each file ends `Succeeded`, `SucceededWithWarnings`, `Failed(step, reason)`, `Skipped(reason)` or `Cancelled`, the run ends with a summary report exportable as JSON, CSV or HTML, and a redaction step that finds a page it cannot search fails that file with no output (`§5`, `§9`, `§10`).
- A pause point stops a file for the user only when the action contains one, and never holds a worker while it waits (`§9`, `§13`).

**Adjacency:** list=applicable @ D04 T07 §12; document=applicable @ D04 T07 §10; settings=applicable @ D02 T04 §5; reporting=applicable @ D04 T07 §10; notifications=applicable @ D04 T01 §6; permissions=applicable @ D04 T07 §9; audit=applicable @ D04 T07 §10; exchange=applicable @ D04 T07 §2; reverse=applicable @ D04 T07 §9

**Adjacency rationale:** The action list is the Actions view (`§12`). The run report is both the document a user files and the audit record of what ran on which file with which action revision (`§10`). The suggested output folder is seeded from `Where to save converted files` (`D02 T04 §5` owns the value). Notices reuse the focus-aware completion notice (`D04 T01 §6`) once per run, not per file. Refusals (read-only output folder, output equal to an input, encrypted input without a password, an action newer than the app) are exercised both ways in the runner (`§9`). Import and export of action JSON is the exchange surface (`§2`). The reverse of a run is stop and cancel; outputs are new files and inputs are never modified, so there is nothing to revert (`§9`). Watched-folder triggers are out of scope for v1 and recorded in the demand ledger.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Action model, JSON schema and versioned migration | D04 T06 §2, D02 T04 §1 | [ ] |
| 2 | §2 | Action store with built-in actions, import and export under hostile-file limits | §1, D04 T02 §1 | [ ] |
| 3 | §3 | Step contract, step registry and whole-action validation | §1, D04 T01 §1 | [ ] |
| 4 | §4 | Document steps: OCR, optimize, watermark, header and Bates, flatten, sanitize | §3, D04 T02 §7, D04 T02 §11, D05 T01 §6, D03 T03 §6, D02 T03 §7 | [ ] |
| 5 | §5 | Protection and standards steps: redact by pattern, encrypt, metadata, PDF/A, preflight | §3, D03 T01 §5, D05 T01 §7, D05 T01 §10, D02 T03 §10, D04 T05 §6, D04 T06 §10 | [ ] |
| 6 | §6 | Output naming template and collision-free exclusive publish | §3, D04 T02 §1, D04 T01 §4 | [ ] |
| 7 | §7 | Convert and export steps through the converter registry | §6, D04 T01 §3 | [ ] |
| 8 | §8 | Input set builder over FileScope with recursion and output planning | §1, D03 T05 §2 | [ ] |
| 9 | §9 | Action runner: windowed per-file jobs, pause points, stop and password recovery | §6, §8, D04 T01 §2, D04 T01 §7 | [ ] |
| 10 | §10 | Run report with per-file results and summary (JSON, CSV, HTML) | §9, D04 T02 §1 | [ ] |
| 11 | §11 | `recto-cli action` command family | §2, §10, D01 T03 §1 | [ ] |
| 12 | §12 | Actions list and step editor on the Convert page | §2, §3, D04 T01 §3, D02 T01 §10, D02 T03 §11 | [ ] |
| 13 | §13 | Run panel, progress, pause prompt and report view | §10, §12, D04 T01 §5, D04 T01 §6, D03 T02 §5 | [ ] |
| 14 | §14 | Action Wizard gates: no script, no overwrite, hostile actions, end-to-end actions | §7, §11, §13, D06 T03 §5 | [ ] |

## 1. Action model, JSON schema and versioned migration

An action is data: a name, an ordered list of typed steps with parameters, and output settings. This section defines that data, its JSON schema, and how versions are read. It matches the saved actions of Acrobat's Action Wizard and keeps Recto's rule that nothing in an action can execute: there is no script step, no command step and no URL step. The same typed parameter schema drives validation (`§3`) and the editor form (`§12`), so the two cannot disagree.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/actions/action_model.h/.cpp` (`ActionDefinition`, `ActionStepSpec`, `ActionOutputSpec`, `ParamValue`) and `src/services/actions/action.schema.json`. Done when: `ActionModel.SchemaRoundTrip` serializes and re-parses each fixture action with identical content.
2. Implement load with migration and refusal of newer versions. Done when: `ActionModel.Versions` migrates an older fixture, refuses a newer one and preserves an unknown step type.

- [ ] Define the document in `src/services/actions/action.schema.json`: `schema` (`"recto.action"`), `schema_version` (integer), `id` (lowercase `a-z0-9-`, `2` to `60` characters), `name` (`1` to `80` characters), `description`, `revision` (integer, incremented on each save), `steps[]` of `{id, type, type_version, params, pause_before, prompt}`, and `output` of `{folder, name_template, if_exists, keep_structure}`. Done when: the schema rejects an unknown top-level key and an `if_exists` other than `append_number` or `skip`.
- [ ] Forbid replacing: `if_exists` has exactly the values `append_number` and `skip`, and there is no `replace` value anywhere in the schema, model or UI. Done when: `ActionModel.NoReplaceValue` fails if the enum gains a replace-like value (pdfcraft #799, #1795).
- [ ] Define `ParamSchema` field kinds used by every step (`bool`, `int` with range, `number` with range, `enum`, `string` with maximum length, `string_list` with maximum count, `file_path`, `folder_path`, `colour_hex`, `secret`) in `src/services/actions/param_schema.h`. Done when: `ActionModel.ParamKinds` validates one value of each kind and rejects one out-of-range value of each.
- [ ] Reject a literal value for any `secret` parameter: a secret is stored only as `{"source": "prompt"}` or `{"source": "env", "name": "<VAR>"}`, never as text. Done when: `ActionModel.NoStoredSecrets` rejects an action file that contains a password string and accepts the `prompt` form (credentials never enter tracked files, arguments or logs).
- [ ] Parse through the `BoundedJson` reader and schema subset validator of `D04 T06 §2`, with the hostile-file limits in `§2`, returning `PdfResult<ActionDefinition>` whose error carries the JSON pointer of the fault. Done when: `ActionModel.Pointers` reports `/steps/2/params/opacity` for an out-of-range opacity.
- [ ] Implement `migrate_action(json, from_version)` as a chain `migrate_v1_to_v2` and so on, run in memory on load and applied on disk only on the next save; refuse a `schema_version` newer than the app with a message naming the version and leave the file untouched. Done when: `ActionModel.Versions` loads a version `0` fixture as version `1`, refuses version `99`, and the file bytes are unchanged after both.
- [ ] Preserve a step whose `type` or `type_version` the app does not know as an `UnknownStep` carrying its original JSON, round-trip it unchanged on save and export, and mark the action `not_runnable` with the step named. Done when: `ActionModel.UnknownStepPreserved` saves and re-reads an action holding a future step type byte for byte in that step.
- [ ] Add `ActionModel.*` tests in `tests/recto-tests/action_model_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionModel.*` passes under ASan.
- [ ] Commit: `services: add the versioned action model, JSON schema and migration`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionModel.*` passes under ASan, and adding `replace` to the `if_exists` enum makes `ActionModel.NoReplaceValue` fail.

## 2. Action store with built-in actions, import and export under hostile-file limits

The store lists, saves, duplicates, renames, deletes, imports and exports actions, and ships a few built-in ones so a new user has something to run. It matches Acrobat's action list and its import and export of `.sequ` action files, as JSON. Every write is temp-file-then-replace, an import is treated as hostile input, and importing never runs anything.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `ActionStore` in `src/services/actions/action_store.h/.cpp` over `AppPaths::data_dir()/actions/`. Done when: `ActionStore.Crud` creates, lists, renames, duplicates and deletes actions and leaves no partial file after an injected write failure.
2. Implement import and export with the limits, and the built-in actions. Done when: `ActionStore.HostileImport` refuses every hostile file and `ActionStore.Builtins` lists the built-ins read-only.

- [ ] Save with `AtomicFileWriter` (`D04 T02 §1`) as `<id>.action.json`, increment `revision`, and keep list order stable by name. Done when: `ActionStore.Crud` shows a failed save leaves the previous revision byte-identical. Cheaper substitute: writing the action with `std::ofstream`, which truncates the saved action on a crash.
- [ ] Import: refuse a file over `1 MiB`, JSON nesting over `16`, more than `64` steps, any string over `4096` characters, more than `256` patterns or a pattern over `512` characters, invalid UTF-8, duplicate keys and any `$ref`; on an id clash import as a new id with a `(2)` name suffix, never replacing the stored action. Done when: `ActionStore.HostileImport` covers each refusal with a typed error and no file written.
- [ ] On import, show nothing executes: the import returns the validated action and its `validate_action` messages (`§3`) for the caller to display, and stores it only on explicit save. Done when: `ActionStore.ImportDoesNotRun` imports an action with a `redact_patterns` step and observes no job submitted and no file read other than the action file.
- [ ] Export one action or all to a `.action.json` or a `.zip` of them through `AtomicFileWriter`, with secrets already absent by `§1`. Done when: `ActionStore.Export` re-imports its own export with identical content.
- [ ] Ship read-only built-in actions as resources `src/services/actions/builtin/*.action.json`: `scan-to-searchable` (OCR, optimize, save), `prepare-for-sharing` (flatten annotations, sanitize, optimize, save), `print-ready` (preflight with fix, `PDF/X-4`, save), and `archive-pdfa` (OCR, `PDF/A-2u`, save); built-ins duplicate to edit and cannot be deleted or overwritten. Done when: `ActionStore.Builtins` lists the four, and delete and save-over return a typed refusal.
- [ ] Handle an unreadable or invalid stored file by listing it as `Damaged` with the reason, never skipping it silently and never deleting it. Done when: `ActionStore.DamagedListed` shows a truncated stored file listed as damaged with its path.
- [ ] Add `ActionStore.*` tests in `tests/recto-tests/action_store_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionStore.*` passes under ASan.
- [ ] Commit: `services: add the action store with built-in actions and bounded import and export`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionStore.*` passes under ASan, and removing the duplicate-key check makes `ActionStore.HostileImport` fail.

## 3. Step contract, step registry and whole-action validation

Every step type implements one contract and registers with one registry, like converter cards in `D04 T01 §3`: adding a step is one `register_step` call, and the editor, validator, runner and CLI all read the registry. The validator checks parameters and step order before anything runs, so an action that could never succeed fails at save or at start, not on file 87 of 200.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `IActionStep`, `StepContext`, `StepResult` and `ActionStepRegistry` in `src/services/actions/action_step.h/.cpp`. Done when: `ActionSteps.Registry` registers a test step and rejects a duplicate id.
2. Implement `validate_action` with parameter and order rules. Done when: `ActionSteps.Validation` and `ActionSteps.OrderRules` pass.

- [ ] Define `IActionStep` (`id`, `type_version`, `title`, `icon_glyph`, `param_schema()`, `summary(params)`, `kind` of `PdfToPdf`, `TerminalSave` or `TerminalExport`, `prepare_run(const InputSet&, params) -> PdfResult<StepRunState>` for run-wide state, and `run(StepContext&) -> PdfResult<StepResult>`). `StepContext` carries the input path, the output scratch path, a `CancelToken`, a progress sink, resolved secrets, the shared `StepRunState` and the run's `FileScope` summary. Done when: `src/services/actions/action_step.h` compiles in `recto-services` and `recto-cli`.
- [ ] Enforce by signature that a step receives paths and opens its own `recto::pdf::Document` on the job thread, never a `DocumentActor*` or a viewer document, and writes only into `StepContext::output_path` through `AtomicFileWriter`. Done when: `ActionSteps.Isolation` runs a step on a file open in a `DocumentActor` with no shared instance, and a step that writes anywhere else fails `ActionSteps.WritesOnlyScratch`.
- [ ] Implement `validate_action(const ActionDefinition&) -> ValidationReport` returning errors and warnings with step id and JSON pointer: parameter kinds and ranges from each `ParamSchema`, exactly one terminal step and it last, and the order rules below. Done when: `ActionSteps.Validation` rejects an action with two terminal steps, no terminal step, and a step after the terminal step.
- [ ] Implement the order rules as a table with severity and message: `encrypt` must be the last `PdfToPdf` step (error); `optimize` with `linearize` must be the last `PdfToPdf` step (error, any later rewrite drops linearization); `encrypt` after `pdfa` or `pdfx_convert` (error, those standards forbid encryption); any content or metadata step (`ocr`, `optimize`, `watermark`, `header_footer_bates`, `flatten`, `sanitize`, `redact_patterns`, `metadata`) after `pdfa` or `pdfx_convert` (error, the later step can break the standard the earlier one established); `metadata` before `sanitize` (warning, sanitize removes it); `redact_patterns` before any `ocr` step (warning, scanned pages have no text to match). Done when: `ActionSteps.OrderRules` triggers each rule with a fixture action and none on the built-ins.
- [ ] Validate every `file_path` and `folder_path` parameter shape (no wildcard, no device name, length) at save and check existence at run start, reporting a missing stamp image as an error naming the step. Done when: `ActionSteps.Paths` reports a missing image path at run start and not at save.
- [ ] Refuse to run an action marked `not_runnable` (`§1`) with the unknown step named. Done when: `ActionSteps.NotRunnable` returns the typed refusal.
- [ ] Add `ActionSteps.*` tests in `tests/recto-tests/action_steps_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionSteps.*` passes under ASan.
- [ ] Commit: `services: add the action step contract, registry and whole-action validation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionSteps.*` passes under ASan, and dropping the encrypt-must-be-last rule makes `ActionSteps.OrderRules` fail.

## 4. Document steps: OCR, optimize, watermark, header and Bates, flatten, sanitize

These six steps wrap engines that already exist, each as a thin adapter: parameters in, one engine call, one scratch file out. They match the corresponding Acrobat actions and add nothing the engine does not already guarantee. Bates numbering across a batch is the one step that needs run-wide state: starting numbers depend on the page counts of the files before it, so they are planned from the input order before any file runs.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `ocr`, `optimize`, `watermark`, `flatten` and `sanitize` in `src/services/actions/steps/document_steps.cpp`. Done when: `ActionDocSteps.Each` runs every step on a corpus file and the output opens.
2. Implement `header_footer_bates` with the Bates plan. Done when: `ActionDocSteps.BatesAcrossFiles` numbers three files continuously in input order under two workers.

- [ ] Implement `ocr` over `create_searchable_pdf` (`D04 T02 §7`) with parameters `language`, `skip_pages_with_existing_text` (default on), `ocr_vector_outlined_pages`, `deskew`, `pages`. Done when: `ActionDocSteps.Ocr` makes `tests/corpus/text/scanned.pdf` searchable and leaves digital pages untouched.
- [ ] Implement `optimize` over `PdfOptimizer` (`D04 T02 §11`) with `preset` of `web`, `balanced` or `print` and `linearize`. Done when: `ActionDocSteps.Optimize` shrinks an image-heavy fixture and the output is linearized only when asked.
- [ ] Implement `watermark` over `apply_watermark` (`D05 T01 §5`) with `text` or `image_path`, `opacity`, `rotation`, `layer`, `anchor`, `tile`, `pages`, `colour_hex`. Done when: `ActionDocSteps.Watermark` stamps text and an image and renders the mark at the requested opacity.
- [ ] Implement `header_footer_bates` over `apply_headers_footers_and_bates` (`D05 T01 §6`) with six zone texts using the tokens of that section, font size, `pages`, `shrink_to_fit`, and `bates` of `prefix`, `start`, `digits`, `suffix`. Done when: `ActionDocSteps.HeaderFooter` writes the six zones with tokens expanded.
- [ ] Plan Bates numbers in `prepare_run`: read the page count of each input in run order (a cheap xref read) and give each file its first number, so concurrent files get deterministic continuous ranges; when a file later fails, record its reserved range as a `BatesGap` warning in the report rather than renumbering. Done when: `ActionDocSteps.BatesAcrossFiles` gives files of `3`, `5` and `2` pages the ranges `1-3`, `4-8` and `9-10` and reports the gap when the second file fails.
- [ ] Implement `flatten` with `annotations` (`D03 T03 §2`), `forms` (`D03 T03 §6`) and `layers` (visible layers, `D02 T03 §7`) options, each independent. Done when: `ActionDocSteps.Flatten` flattens a form and a highlight and leaves unselected categories live.
- [ ] Implement `sanitize` over `Redactor::sanitize_document` (`D05 T01 §10`) with the seven `SanitizeOptions` booleans. Done when: `ActionDocSteps.Sanitize` removes metadata, scripts and attachments from a fixture and reports each category removed.
- [ ] Register all six in `ActionStepRegistry` with `ParamSchema`, summaries and Segoe Fluent glyph names. Done when: `ActionSteps.Registry` lists them and `validate_action` accepts the built-ins.
- [ ] Add `ActionDocSteps.*` tests in `tests/recto-tests/action_doc_steps_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionDocSteps.*` passes under ASan.
- [ ] Commit: `services: add OCR, optimize, watermark, header and Bates, flatten and sanitize action steps`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionDocSteps.*` passes under ASan, and numbering each file from `1` makes `ActionDocSteps.BatesAcrossFiles` fail.

## 5. Protection and standards steps: redact by pattern, encrypt, metadata, PDF/A, preflight

These steps change what a file reveals or claims, so they are the ones where an unattended batch can do harm: a redaction that skips a scanned page, an encryption with a lost password, a `PDF/A` label on a file that does not pass. Each is built to fail the file rather than succeed falsely. They match Acrobat's redaction, security, metadata, `PDF/A` and preflight actions.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `redact_patterns` and its verification in `src/services/actions/steps/redact_step.cpp`. Done when: `ActionProtect.RedactPatterns` redacts pattern matches on text pages, fails a file with a page that has no searchable text, and passes the raw-byte check of `D06 T03 §6`.
2. Implement `encrypt`, `metadata`, `pdfa`, `preflight` and `pdfx_convert`. Done when: `ActionProtect.Encrypt`, `ActionProtect.Metadata`, `ActionProtect.PdfA` and `ActionProtect.Preflight` pass.

- [ ] Extract the `Find & Redact` regex presets (`email`, `phone`, `national-id`, `card`, `date`, `ipv4`) of `D05 T02 §9` into `src/core/redaction_presets.h` if they are not already a shared table, and use that table from both the viewer and this step. Done when: `ActionProtect.PresetsShared` shows the viewer and the step resolve the same preset ids to the same expressions.
- [ ] Implement `redact_patterns` with parameters `patterns[]` (`kind` of `literal`, `regex` or `preset`, `value`, `case_sensitive`, `whole_word`), `fill_colour_hex`, `overlay_text`, `pages`, and `on_unsearchable_pages` of `fail` (default), `ocr_then_redact` or `skip_with_warning`. It finds hits with `SearchEngine` (`D03 T01 §5`), builds `RedactionMark` rectangles from the hit quads and calls `Redactor::apply_redactions` (`D05 T01 §10`). Done when: `ActionProtect.RedactPatterns` redacts every email and card number on a text fixture.
- [ ] Validate every regular expression at save and at run start against the search engine's bounded matcher and refuse one that does not compile or exceeds its step budget; run the match with the same budget. Done when: `ActionProtect.BadRegex` refuses an invalid expression and a catastrophic-backtracking expression without hanging.
- [ ] Fail the file when a page has no searchable text and `on_unsearchable_pages` is `fail`, naming the pages, and write no output; with `ocr_then_redact` run `create_searchable_pdf` on those pages first; with `skip_with_warning` record the pages in the result warnings. Done when: `ActionProtect.UnsearchablePages` fails a scanned fixture by default, redacts it after OCR with the second mode, and warns with the third.
- [ ] Verify after applying: re-run the same patterns on the redacted scratch file and fail the file if any match remains, in addition to the raw-byte oracles of `D06 T03 §6`. Done when: `ActionProtect.VerifyAfterRedact` fails when a planted engine bug leaves one match and passes otherwise. Cheaper substitute: trusting the redactor's return value without searching the result.
- [ ] Implement `encrypt` over `encrypt_pdf` (`D05 T01 §7`) with `user_password` and `owner_password` as `secret` parameters resolved at run start (prompt or environment variable), and the permission flags; refuse an action whose encrypt step has neither password. Done when: `ActionProtect.Encrypt` encrypts a fixture, the output requires the password, and the password appears in no log, report or action file.
- [ ] Implement `metadata` over `metadata_edit` (`D02 T03 §10`) with `set` for `title`, `author`, `subject`, `keywords`, supporting the tokens `{name}`, `{date}` and `{n}`, `clear` for chosen fields, and `remove_custom_xmp`. Done when: `ActionProtect.Metadata` sets and clears fields and keeps `/Info` and XMP synchronized.
- [ ] Implement `pdfa` over `ComplianceEngine::convert_to_pdfa` (`D04 T05 §6`) with `level`, `colour_profile`, `auto_tag`, then run `validate_pdfa` on the output and fail the file with the findings unless `on_partial` is `warn`. Done when: `ActionProtect.PdfA` converts a fixture to `PDF/A-2u` with zero findings and fails a fixture the converter cannot fix.
- [ ] Implement `preflight` over `PreflightEngine` and the fixups (`D04 T06 §6`, `§8`) with `profile`, `fix` and `on_fail` of `fail_file` (default) or `warn`, treating `Incomplete` as `Failed` unless `on_incomplete` is `warn`; implement `pdfx_convert` over `convert_to_pdfx` (`D04 T06 §10`) with `level`, `output_profile`, `bleed_pt` and `on_partial`. Done when: `ActionProtect.Preflight` fails a fixture with an error finding, treats `deep-forms.pdf` as failed, and `ActionProtect.PdfX` writes no identification on a partial conversion.
- [ ] Register all six steps with `ParamSchema` and summaries, and add `ActionProtect.*` tests in `tests/recto-tests/action_protect_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionProtect.*` passes under ASan.
- [ ] Commit: `services: add redact by pattern, encrypt, metadata, PDF/A, preflight and PDF/X action steps`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionProtect.*` passes under ASan, and making `on_unsearchable_pages` default to `skip_with_warning` makes `ActionProtect.UnsearchablePages` fail.

## 6. Output naming template and collision-free exclusive publish

Where an output goes and what it is called decides whether a batch destroys work. pdfcraft's batch and folder jobs replaced earlier results and existing documents, and their collision check raced with publication (#799, #1795, #1805). This section builds the naming template and the one publish path every action output uses: a name is claimed by creating the file exclusively, never by checking first and writing later, so two workers publishing the same name cannot overwrite each other, and an input can never be the target.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `NameTemplate` in `src/services/actions/name_template.h/.cpp`. Done when: `ActionNaming.Template` expands every token and sanitizes unsafe names.
2. Implement `OutputPublisher` in `src/services/actions/output_publisher.h/.cpp`. Done when: `ActionNaming.ExclusivePublish` publishes the same name from `8` threads and gets `8` distinct files, none replacing another.

- [ ] Implement tokens `{name}` (input base name), `{ext}`, `{action}`, `{date}` (`yyyy-MM-dd`), `{time}` (`HH-mm-ss`), `{n}` (position in the run, zero-padded to the run's width) and `{parent}` (the input's folder name), reject unknown tokens at save, and default the template to `{name}-{action}`. Done when: `ActionNaming.Template` expands each token and rejects `{bogus}`.
- [ ] Sanitize the expanded name for Windows: replace `\ / : * ? " < >` and `|` and control characters, strip trailing dots and spaces, avoid reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1` to `COM9`, `LPT1` to `LPT9`) including with an extension, and cap the file name at `200` UTF-16 units keeping the extension. Done when: `ActionNaming.Sanitize` handles non-Latin names, a trailing dot and `con.pdf` without error.
- [ ] Publish with `AtomicFileWriter` (`D04 T02 §1`) as a new file: the temp file is written and flushed in the destination folder and published with `MoveFileExW` without replace-existing, and on `ERROR_ALREADY_EXISTS` the publisher tries the next `name (2).pdf`, `name (3).pdf` up to `9999`, so a name is claimed atomically at publication and never by a prior existence check. Done when: `ActionNaming.ExclusivePublish` runs `8` threads publishing the same base name and finds `8` distinct files with the expected contents. Cheaper substitute: `exists()` followed by write, which races (pdfcraft #1805).
- [ ] Refuse any published path that is the same file as an input of the run, comparing volume serial and file index from `GetFileInformationByHandle` (so hard links, junction aliases and case differences are caught), and rename instead of replace when the output folder is the input folder. Done when: `ActionNaming.NeverInput` publishes beside the input without touching it, including through a hard link to the input.
- [ ] Apply `if_exists` of `skip` by treating an existing target name as `Skipped(exists)` for that file with no write, and `append_number` as above. Done when: `ActionNaming.SkipPolicy` skips an existing output and reports it.
- [ ] With `keep_structure`, create the mirrored subfolders under the output folder (depth at most `32`, each created with `CreateDirectoryW`), and without it flatten and rely on the exclusive publish for same-name inputs from different folders. Done when: `ActionNaming.Structure` mirrors `a\x.pdf` and `b\x.pdf` in one case and produces `x.pdf` and `x (2).pdf` in the other.
- [ ] Refuse an output folder that is read-only or cannot be created with a typed error before any file runs. Done when: `ActionNaming.OutputFolderErrors` refuses a read-only folder at run start with no work done.
- [ ] Register the `save` terminal step using the publisher with parameters taken from the action's `output`. Done when: `ActionNaming.SaveStep` runs `save` as the last step of a test action and publishes the file.
- [ ] Add `ActionNaming.*` tests in `tests/recto-tests/action_naming_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionNaming.*` passes under ASan.
- [ ] Commit: `services: add action naming template and race-free exclusive output publishing`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionNaming.*` passes under ASan, and replacing the exclusive create with an existence check makes `ActionNaming.ExclusivePublish` fail.

## 7. Convert and export steps through the converter registry

An action can end in any conversion Recto offers: Word, Excel, HTML, Markdown, images, text and the rest. Rather than a copy of each converter's options, the `convert` step is a thin terminal that names a converter card from the registry of `D04 T01 §3` and carries that card's option values, so new converters become available to actions automatically and an action never duplicates a converter's logic. It matches the export steps of Acrobat's Action Wizard.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the `convert` terminal step in `src/services/actions/steps/convert_step.cpp` over `ConverterRegistry`. Done when: `ActionConvert.Registry` lists every registered converter card as a selectable `converter_id`.
2. Implement option validation and output publishing. Done when: `ActionConvert.RunsConverter` runs a text export and a Word export from a test action.

- [ ] Define the `convert` parameters as `converter_id` plus `options`, where `options` is validated against the card's option schema (`default_options_factory` of `D04 T01 §3`) and an unknown `converter_id` makes the action `not_runnable` with the id named. Done when: `ActionConvert.UnknownConverter` marks the action not runnable and `ActionConvert.OptionsValidated` rejects an out-of-range DPI.
- [ ] Use the card's `accept_predicate` against the run's input kinds before starting, failing the run with the card's disable reason when no input is acceptable, and skipping an unacceptable file with `Skipped(reason)`. Done when: `ActionConvert.InputKinds` skips a `.docx` given to a PDF-only converter with the card's reason.
- [ ] Run the card's `job_factory` for the file with the previous step's scratch file as the input and publish every output it produces (an image export makes many files) through `OutputPublisher` (`§6`), applying the same naming template with a per-output suffix. Done when: `ActionConvert.MultiOutput` publishes a `9`-image export as nine uniquely named files.
- [ ] Reject any converter option that would write outside the planned output path (a converter-specific `Save to` field) by overriding the common options of `D04 T01 §4` with the action's output settings. Done when: `ActionConvert.OutputOverride` shows a card option naming another folder is ignored and reported.
- [ ] Add `ActionConvert.*` tests in `tests/recto-tests/action_convert_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionConvert.*` passes under ASan.
- [ ] Commit: `services: add the convert action step over the converter registry`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionConvert.*` passes under ASan, and letting a converter option choose its own output folder makes `ActionConvert.OutputOverride` fail.

## 8. Input set builder over FileScope with recursion and output planning

A run starts from files the user picked, a folder, or both. This section turns that into an ordered, de-duplicated list of inputs with their planned relative output paths, using the link-safe enumerator of `D03 T05 §2` instead of a second enumerator. It matches the Action Wizard's file and folder selection and its include-subfolders option.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `InputSet::build(selection, options) -> PdfResult<InputSet>` in `src/services/actions/input_set.h/.cpp` over `FileScope::from_folder` and `FileScope::from_files`. Done when: `ActionInput.Build` returns the expected ordered set for a mixed selection.
2. Add the safety summary and counts. Done when: `ActionInput.Limits` reports a tree over the limits as a summary and not a failure.

- [ ] Accept selections of files, folders and the current open document's saved path; for an open document with unsaved changes, use the saved file and record `used_saved_version` in the run so the UI can say so. Done when: `ActionInput.OpenDocument` selects the saved path of a dirty tab and sets the flag.
- [ ] Enumerate folders with `FileScope::from_folder` with `recursive` as the user's option (non-recursive lists only the top level), never following links, with the limits of `D03 T05 §2` (depth `16`, files `20000`), and carry its `FileScopeSummary` (skipped links, hidden files, truncation) into the input set. Done when: `ActionInput.Recursion` lists nested files only with the option on and shows a planted junction as skipped.
- [ ] Filter by extension case-insensitively to `.pdf` for PDF-to-PDF actions, or to the accepted kinds of the terminal converter (`§7`), and list the rejected files with their reason instead of dropping them silently. Done when: `ActionInput.KindFilter` lists `notes.txt` as `Skipped(unsupported type)`.
- [ ] De-duplicate by file identity (volume serial and file index) so a file chosen twice, or through two paths, runs once, and keep the stable order: explicit files in the order given, then folder files sorted by relative path with a natural-order comparison. Done when: `ActionInput.Dedup` runs a file given twice once and `ActionInput.Order` is identical across runs.
- [ ] Compute each input's planned relative output folder for `keep_structure` and its `{n}` index, and reserve no filesystem state. Done when: `ActionInput.Plan` returns the planned relative paths without creating any file or folder.
- [ ] Cap the run at `5000` inputs by default and report `truncated_at_limit` as a visible condition that blocks starting until the user narrows the selection, never trimming silently. Done when: `ActionInput.RunCap` blocks a `5001` file selection with the reason.
- [ ] Add `ActionInput.*` tests in `tests/recto-tests/action_input_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionInput.*` passes under ASan.
- [ ] Commit: `services: add the action input set builder over FileScope with output planning`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionInput.*` passes under ASan, and following a junction in the enumeration makes `ActionInput.Recursion` fail.

## 9. Action runner: windowed per-file jobs, pause points, stop and password recovery

The runner executes an action over an input set as jobs in the existing queue. It does not edit `D04 T01`: it submits one `ConversionJob` per file (or per segment between pause points) to the runner of `D04 T01 §1`, feeds the queue in a small window so a 500-file run shows a handful of rows, and records each file's outcome. A pause is a boundary between jobs, so a file waiting for the user holds no worker; the intermediate stays in scratch until the user continues or stops.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `ActionRunner` in `src/services/actions/action_runner.h/.cpp`: validate, plan, `prepare_run` every step, then feed per-file jobs in a window of `4`. Done when: `ActionRun.TwoAtOnce` runs `10` files with at most `2` executing and at most `4` queue rows.
2. Implement pause, stop, password recovery and failure isolation. Done when: `ActionRun.Pause`, `ActionRun.Stop`, `ActionRun.Passwords` and `ActionRun.FailureIsolation` pass.

- [ ] Define `ActionRunSpec` (`action`, `InputSet`, `output folder`, resolved secrets, `on_pause` for non-interactive runs), `ActionRunState` (`Preparing`, `Running`, `AwaitingInput`, `Stopping`, `Finished`) and `FileOutcome` (`Succeeded`, `SucceededWithWarnings`, `Failed{step, reason}`, `Skipped{reason}`, `Cancelled`, `NeedsPassword`). Done when: `src/services/actions/action_runner.h` compiles across `recto-services`, `src/app` and `recto-cli`.
- [ ] Run `validate_action`, check the output folder is writable, check every referenced path, resolve secrets, and call each step's `prepare_run` before submitting any job, failing the whole run with the reason and no file touched when any of these fail. Done when: `ActionRun.PreflightOfRun` refuses a run with a missing stamp image and a read-only output folder with zero jobs submitted.
- [ ] Run each file's steps as one `ConversionJob` using scratch files `AppPaths::temp_dir()/actions/<run-id>/<index>/step-<n>.pdf` written by `AtomicFileWriter`, passing each step's output to the next, publishing only through `OutputPublisher`, and cleaning the scratch folder through `JobTempScope` (`D04 T01 §2`) on every exit. Done when: `ActionRun.ScratchCleanup` leaves no scratch file after success, failure and cancel, and the input hash is unchanged.
- [ ] Feed the queue through a window: submit at most `4` file jobs at a time, submit the next as one finishes, and keep a job in `NeedsPassword` out of the window count so a parked file cannot stall the run. Done when: `ActionRun.WindowedFeed` shows a `NeedsPassword` file does not block the remaining files.
- [ ] Handle a pause: when a step has `pause_before`, end the file's job after the previous step with its scratch file kept, set the file `AwaitingInput` with the step's `prompt`, hold no worker, and on `continue` submit a job for the remaining steps; `skip file` ends it `Skipped(skipped at pause)` and `stop run` ends the run. A run with no `pause_before` never enters `AwaitingInput`, and the UI offers no way to add a pause at run time. Done when: `ActionRun.Pause` pauses one file while the others run, resumes it, and `ActionRun.NoPauseNoPrompt` shows a pause-free action never prompts.
- [ ] Implement `stop`: trip every `CancelToken`, let in-flight steps stop at their next cooperative check, delete in-flight scratch and unpublished temp files, keep outputs already published, and mark unstarted files `Cancelled`. Done when: `ActionRun.Stop` stops a `20`-file run mid-way with published outputs intact and no `.tmp` left.
- [ ] Recover passwords through the existing flow: an encrypted input yields `NeedsPassword` (`D04 T01 §2`), the run keeps the file parked, and `resubmit_with_password` (`D04 T01 §7`) resumes it; offer a run-level list of passwords to try, held in memory only. Done when: `ActionRun.Passwords` unlocks an `AES-256` fixture from the list and from a single prompt and persists no password.
- [ ] Isolate failures: a failing file records `Failed{step, reason}` with the engine's message and never aborts other files, never leaves a partial output, and a step that returns `Truncated` or a recovery note adds a warning to the file's outcome. Done when: `ActionRun.FailureIsolation` runs a batch with one corrupt and one encrypted file and completes the rest.
- [ ] Never run PDF JavaScript, launch actions, external commands or network calls from any step; open documents only through `recto::pdf::Document`. Done when: `ActionRun.NoActiveContent` runs an action over a fixture with `/OpenAction` JavaScript and `/Launch` and observes neither executed, with a process-creation and network trap installed.
- [ ] Add `ActionRun.*` tests in `tests/recto-tests/action_run_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionRun.*` passes under ASan.
- [ ] Commit: `services: add the action runner with windowed file jobs, pause points, stop and password recovery`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionRun.*` passes under ASan, and holding a worker while a file awaits input makes `ActionRun.Pause` fail.

## 10. Run report with per-file results and summary (JSON, CSV, HTML)

A batch is only trustworthy if its result can be read afterwards: which files succeeded, which were skipped and why, what each step did, and which action revision produced them. This section builds the run report from the runner's outcomes and exports it. It matches Acrobat's action completion summary and adds an exportable record. The report never says more than happened: a file that was not run is `Cancelled` or `Skipped`, never absent.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `ActionRunReport` and its builder in `src/services/actions/run_report.h/.cpp` from `FileOutcome` records. Done when: `ActionReport.Counts` shows the summary counts equal the per-file outcomes for a mixed run.
2. Implement the exporters. Done when: `ActionReport.Exports` writes valid JSON, CSV and HTML and a failed write leaves no partial file.

- [ ] Define `ActionRunReport` (action id, name, revision and SHA-256 of the action JSON, Recto version, start and end time, the input summary with `FileScopeSummary` and `used_saved_version`, summary counts for each outcome, and a `FileReport` per input). Done when: `src/services/actions/run_report.h` compiles in `recto-services` and `recto-cli`.
- [ ] Define `FileReport` (input path, size and modified time, outcome, the steps run with status, duration and warnings, the output paths with size and SHA-256, the failing step and reason, and `BatesGap` ranges). Done when: `ActionReport.FileFields` checks every field on a run with one success, one warning, one failure, one skip and one cancel.
- [ ] Order files by input order, make the report deterministic apart from timestamps, and list every input of the set exactly once. Done when: `ActionReport.EveryInputOnce` finds each input of a stopped run in the report as `Cancelled` and none twice.
- [ ] Export JSON against a versioned `run-report.schema.json`, CSV (one row per file with a header, RFC 4180 quoting, and a guard that prefixes any cell starting with `=`, `+`, `-` or `@` with a single quote), and a self-contained static HTML file with every value escaped and no script, through `AtomicFileWriter`. Done when: `ActionReport.Exports` validates the JSON, parses the CSV back with hostile file names, and the HTML parser finds no `<script>`. Cheaper substitute: concatenating unescaped HTML or CSV, which lets a file name inject markup or a spreadsheet formula.
- [ ] Expose the list of `Failed` and `NeedsPassword` inputs as an `InputSet` so the UI and CLI can re-run just those with the same action. Done when: `ActionReport.RetrySet` returns exactly the failed inputs.
- [ ] Add `ActionReport.*` tests in `tests/recto-tests/action_report_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionReport.*` passes under ASan.
- [ ] Commit: `services: add the action run report with JSON, CSV and HTML export`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionReport.*` passes under ASan, and dropping an input from a stopped run's report makes `ActionReport.EveryInputOnce` fail.

## 11. `recto-cli action` command family

The CLI runs an action without the app, for scripts and scheduled tasks. It follows the dispatcher and exit-code contract of `D01 T03 §1` and uses the same runner, naming and report code as the Run panel. Credentials never travel in arguments: passwords come from environment variables named on the command line.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `recto-cli action <run|validate|list|show|import|export>` in `src/cli/cmd_action.cpp`. Done when: `Cli.ActionValidate` and `Cli.ActionList` pass.
2. Implement `run` with report, pause policy and exit codes. Done when: `Cli.ActionRun` runs a built-in action over a folder and returns the documented exit code.

- [ ] Implement `recto-cli action run <action.json|id> <files-or-folders...> --out <dir> [--recursive] [--keep-structure] [--if-exists append-number|skip] [--report <path.json|.csv|.html>] [--password env:NAME]... [--secret <step-id>.<param>=env:NAME]... [--on-pause stop|continue] [--json]` in `src/cli/cmd_action.cpp`. Done when: `Cli.ActionRun` runs the `scan-to-searchable` built-in over a mixed folder and writes the outputs and the report.
- [ ] Refuse a missing `--out`, require `--on-pause continue` for an action with pause points (the default `stop` refuses to start, names the step and exits `1`), and never accept a literal password on the command line. Done when: `Cli.ActionArguments` covers each refusal and shows `--password secret123` is rejected.
- [ ] Map exit codes: `0` all files `Succeeded` or `SucceededWithWarnings`, `1` invalid arguments or an invalid action, `2` the action file unreadable, `4` at least one file `Failed`, `Skipped` for an error or `NeedsPassword`, `5` the run was stopped; print a one-line summary and the per-file table, or the report as JSON with `--json`. Done when: `Cli.ActionExitCodes` verifies each code.
- [ ] Implement `validate <action.json> [--json]` printing errors and warnings with JSON pointers, `list` and `show <id>`, `import <file> [--as <id>]` and `export <id> --out <file>` over `ActionStore`. Done when: `Cli.ActionValidate`, `Cli.ActionList` and `Cli.ActionImportExport` pass.
- [ ] Run through `ActionRunner` and `ConversionRunner` like the app, with Ctrl+C cancelling through the same stop path. Done when: `Cli.ActionInterrupt` interrupts a run and finds no `.tmp` file and the published outputs intact.
- [ ] Add `Cli.Action*` tests in `tests/recto-tests/cli_action_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Cli.Action*` passes under ASan.
- [ ] Commit: `cli: add recto-cli action run, validate, list, show, import and export`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.Action*` passes under ASan, and accepting a literal `--password` value makes `Cli.ActionArguments` fail.

## 12. Actions list and step editor on the Convert page

Users build and manage actions in a view beside the converters. This section adds an `Actions` view to the Convert page: a list of built-in and saved actions, and an editor with an ordered step list and a parameter form per step. It matches Acrobat's Action Wizard list and Create New Action dialog, and keeps Recto's one shell (D17): it is a view inside the Convert page, not a new mode and not a second shell.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface: no SPEC screenshot shows it. It follows `design/SPEC.md` section 9.5 (the Convert page layout and cards) for the host, section 9.9 for dialogs, section 4 tokens, and the two-column editor pattern of the Watermark dialog.
**Job:** A user opens Actions, picks a built-in or saved action, duplicates or creates one, adds steps from a grouped menu, reorders them, sets each step's parameters, marks a pause point if they want one, sees validation messages immediately, and saves, exports or imports it.
**Treatment:** A `SelectorBar` at the top of `ConvertPage` (`Convert`, `Actions`); on the left an action list with search and `New`, `Duplicate`, `Delete`, `Import`, `Export`; on the right the editor with a name box, a description, an ordered `ListView` of step cards (glyph, title, one-line summary, pause marker, remove) reordered by drag and by `Move up` and `Move down` buttons, an `Add step` menu grouped as `Document`, `Protect and standards`, `Convert and save`, a parameter form generated from the step's `ParamSchema`, a validation `InfoBar` list, and `Save` and `Run...`. Built-in actions open read-only with a `Duplicate to edit` banner. Cheaper substitute that fails the checkpoint: a raw JSON text box, or reordering by drag only.
**Chrome:** Consume `ConvertPage` and its registry (`D04 T01 §3`), the 9.9 dialog host (`D02 T01 §10`), `CommandRegistry` (`D02 T03 §11`), and `RectoColors.xaml` tokens with Segoe Fluent Icons.

**Build order:**

1. Implement `ActionsViewModel` and `ActionEditorViewModel` in `src/services/actions_view_model.h/.cpp` over `ActionStore`, `ActionStepRegistry` and `validate_action`. Done when: `ActionsUi.ViewModel` creates, edits, reorders, validates and saves an action with no UI thread blocking.
2. Build `src/app/views/ActionsView.xaml/.cpp`, `ActionEditor.xaml/.cpp` and the parameter form generator. Done when: `AppTests.ActionsEditor` builds a copy of `scan-to-searchable` from empty by adding steps and setting parameters.

- [ ] Add the `Convert | Actions` `SelectorBar` to `ConvertPage` and host `ActionsView` in its content area, and record the Actions view in `design/SPEC.md` section 9.5. Done when: switching between `Convert` and `Actions` keeps the Step 1 files, and the SPEC row exists.
- [ ] Build the action list with search, built-in and saved groups, damaged actions shown with their reason, and the buttons `New`, `Duplicate`, `Delete` (with a `ContentDialog` confirmation), `Import` and `Export` through file pickers calling `ActionStore`. Done when: `AppTests.ActionsEditor` lists, duplicates and deletes a saved action and shows a damaged file with its reason. Cheaper substitute: deleting without a confirmation.
- [ ] Generate the parameter form from each `ParamSchema` (toggles, number boxes with range, combo boxes, text boxes with maximum length, pickers for files and folders, a pattern list editor, a colour picker that stores a hex value, and a `secret` row that offers `Ask when the action runs` and `Read from environment variable` and no text box), with inline validation messages. Done when: `AppTests.ActionsEditor` shows the secret row has no text input and an out-of-range number shows its message.
- [ ] Build the ordered step list with drag reorder and `Move up` and `Move down` buttons that work from the keyboard, a remove button, a pause toggle labelled `Pause before this step and ask me` with a `prompt` text box, and the `Add step` menu from `ActionStepRegistry`. Done when: reordering by keyboard changes the saved order and the pause marker shows on the card.
- [ ] Show `validate_action` results live as `InfoBar`s with the step highlighted, disable `Run...` while errors exist, and explain each order rule in plain words. Done when: placing `encrypt` before `optimize` shows the order error on the encrypt step and disables `Run...`.
- [ ] Register `Actions.Open`, `Actions.New`, `Actions.Run` and one command per saved action in `CommandRegistry` (`D02 T03 §11`) so `Ctrl+K` runs a saved action, and add `Actions` to the `more` menu. Done when: `Panels.CommandRegistryCompleteness` passes and typing an action name in `Ctrl+K` opens its Run panel.
- [ ] Give every control an automation name and tooltip, and make the editor fully keyboard operable. Done when: `A11yInventory` (`D03 T02 §3`) passes on `ActionsView` and `ActionEditor`.
- [ ] Commit: `app: add the Actions view with action list and step editor on the Convert page`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionsUi.*:AppTests.ActionsEditor` passes on the display-session host, and giving a `secret` row a text box fails `AppTests.ActionsEditor`.

## 13. Run panel, progress, pause prompt and report view

Running an action is a short flow: choose the inputs, choose the output folder, check the name preview, start; then watch progress, answer a pause prompt only if the action has one, stop if needed, and read the result. This section builds that flow in the Actions view. It matches Acrobat's run panel and completion summary, and reuses the queue card, notices and password dialog instead of duplicating them.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface: no SPEC screenshot shows it. It follows `design/SPEC.md` section 9.5 (Step 1 file chips and drop zone, the queue card states) and section 9.9 (dialogs and the completion notice), with section 4 tokens.
**Job:** A user picks files or a folder (with a recursive option), picks where the results go, sees what the first outputs will be called, starts the run, watches per-file progress, answers a pause prompt when the action has one, stops if needed, and finishes on a summary they can open, filter, retry from and export.
**Treatment:** A run panel with an `Inputs` block (the Step 1 chip bar and drop zone, a folder picker, `Include subfolders`, `Keep folder structure`), an `Output` block (folder picker seeded from `Where to save converted files`, `If a file exists` with `Add number` and `Skip` only, a name preview for the first three inputs, and prompts for any `secret` parameters) and a `Start` button. After start: a progress view with an overall bar, per-file rows (name, current step, state) and `Stop`; a pause prompt `ContentDialog` with the step's `prompt`, `Continue`, `Skip this file` and `Stop run`; and a report view grouped `Succeeded`, `With warnings`, `Failed`, `Skipped`, with `Open file`, `Show in folder`, `Retry failed`, `Save report...` and a details expander per failure. Cheaper substitute that fails the checkpoint: a single spinner with no per-file state, an `If a file exists` option that includes `Replace`, or a final toast with no list.
**Chrome:** Consume the Step 1 chip bar and drop zone of `D04 T01 §3`, `QueueCard` (`D04 T01 §5`), the completion notice (`D04 T01 §6`), `PasswordDialog` (`D02 T01 §10`), `AnnouncementService` (`D03 T02 §5`), and `RectoColors.xaml` status brushes with icons.

**Build order:**

1. Implement `ActionRunViewModel` in `src/services/action_run_view_model.h/.cpp` over `InputSet`, `NameTemplate`, `ActionRunner` and `ActionRunReport`. Done when: `ActionRunUi.ViewModel` plans, starts, pauses, stops and reports a run with no UI thread blocking.
2. Build `ActionRunPanel.xaml/.cpp`, the progress view, the pause prompt and the report view. Done when: `AppTests.ActionRunPanel` runs a three-file action to a report in a display-session capture.

- [ ] Build the `Inputs` block reusing the Step 1 chip bar and drop zone of `D04 T01 §3`, add `Choose folder...`, `Include subfolders` and `Keep folder structure`, show the counts and the `FileScopeSummary` (skipped links, truncation) before start, and block start with the reason when the input set exceeds the cap. Done when: choosing a folder with a junction shows the skipped link count and a `5001`-file folder blocks start.
- [ ] Build the `Output` block with the folder picker seeded from `D02 T04 §5`, an `If a file exists` selector with exactly `Add number` and `Skip`, and a live name preview for the first three inputs from `NameTemplate`, flagging a planned name that would equal an input as `will be renamed`. Done when: `AppTests.ActionRunPanel` shows no `Replace` option and previews the planned names.
- [ ] Prompt for each `secret` parameter and for run-level passwords with the shared `PasswordDialog` chrome before start, hold them in memory only, and never write them to the report or settings. Done when: `ActionRunUi.Secrets` finds no secret in settings, report or log after a run.
- [ ] Build the progress view bound to `ActionRunViewModel` with per-file rows, an overall bar, `Stop` with a confirmation naming how many outputs are already published, and `Enter password` on parked files. Done when: stopping a running action in `AppTests.ActionRunPanel` leaves the published outputs listed and the rest `Cancelled`.
- [ ] Build the pause prompt as a `ContentDialog` on the 9.9 chrome showing the file name, the step and its `prompt`, with `Continue`, `Skip this file` and `Stop run`, one prompt at a time in file order, and never show a prompt for an action without a pause point. Done when: `AppTests.ActionRunPanel` shows a prompt for a paused action and none for `scan-to-searchable`.
- [ ] Build the report view from `ActionRunReport` with the four groups, per-failure reasons, `Open file` and `Show in folder` (the shared `JobOutputActions` of `D04 T01 §6`), `Retry failed` (a new run over the failed inputs), `Save report...`, a completion notice once per run, and an `IAnnouncementSink` summary (`D03 T02 §5`). Done when: `ActionRunUi.Report` retries only the failed inputs and the announcement is recorded.
- [ ] Give every control an automation name and tooltip and keep the run flow keyboard operable. Done when: `A11yInventory` (`D03 T02 §3`) passes on `ActionRunPanel` and the report view.
- [ ] Commit: `app: add the action Run panel with progress, pause prompt and report view`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionRunUi.*:AppTests.ActionRunPanel` passes on the display-session host, and adding a `Replace` choice to `If a file exists` fails `AppTests.ActionRunPanel`.

## 14. Action Wizard gates: no script, no overwrite, hostile actions, end-to-end actions

This section proves the properties that make a batch tool safe to leave running: no input or earlier output is ever overwritten, hostile action files and inputs are survived, no active content runs, and the built-in actions work from the CLI and the app on real corpus files. The gates test behaviour owned by earlier sections; a failing gate is fixed in its owner, never by loosening the gate.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Author `tests/recto-tests/action_gates_tests.cpp` with the gates below. Done when: `recto-tests.exe --gtest_filter=ActionGates.*` runs every gate.
2. Run the end-to-end actions. Done when: `ActionGates.BuiltinsEndToEnd` runs all four built-in actions over a corpus folder through the CLI and through the runner.

- [ ] Gate `ActionGates.NeverOverwrites`: run the same action twice over the same inputs into the same folder, into the input folder, and with two inputs sharing a base name from different folders, and assert every earlier file is byte-identical afterwards and every new output has a distinct name (pdfcraft #799, #1795). Done when: the gate passes and a planted replace in the publisher fails it.
- [ ] Gate `ActionGates.PublishRace`: publish the same name from `8` concurrent runs with fault injection at each `AtomicFileWriter` step using the `D06 T03 §5` harness, and assert no output is lost, no temp file remains and the input hash is unchanged (pdfcraft #1804, #1805). Done when: the gate passes under ASan.
- [ ] Gate `ActionGates.HostileActions`: import and run a table of hostile action files (oversized, deep, duplicate keys, a literal password, an unknown step type, a path-traversal output template such as `..\..\x`, a template that expands to a reserved device name) and assert each is refused or sanitized with the reason and nothing outside the output folder is written. Done when: every row passes.
- [ ] Gate `ActionGates.NoActiveContent`: run every step type over fixtures carrying `/JavaScript`, `/Launch`, embedded Office macros in attachments and HTML script, with process-creation and network traps, and assert none ran. Done when: the gate observes zero traps tripped.
- [ ] Gate `ActionGates.RedactionFailsClosed`: run `redact_patterns` over a scanned page, a page with a pattern split across lines, and a page with a pattern inside an annotation, and assert each either redacts completely (raw-byte oracle of `D06 T03 §6`) or fails the file with no output. Done when: the gate passes and removing the post-apply verification fails it.
- [ ] Gate `ActionGates.BuiltinsEndToEnd`: run `scan-to-searchable`, `prepare-for-sharing`, `print-ready` and `archive-pdfa` over `tests/corpus/` through `recto-cli action run` and through `ActionRunner`, and assert identical outputs, the expected report counts, and a passing `PDF/A` or `PDF/X` validation where the action claims one. Done when: the gate passes and the CLI and runner outputs are byte-identical apart from timestamps.
- [ ] Gate `ActionGates.PauseHoldsNoWorker`: run a paused action beside `4` other conversions and assert the others finish while the paused file waits. Done when: the gate passes.
- [ ] Record watched folders (pdfcraft #3613) as a demand-ledger row in `docs/user-demand.md` when that file exists (created by `D07 T03 §6`), stating that they are out of scope for v1 and why. Done when: the row exists with the issue number and the reason.
- [ ] Commit: `test(quality): prove action runs never overwrite, never run active content and fail closed`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionGates.*` passes under ASan, and replacing the exclusive publish with a replace makes `ActionGates.NeverOverwrites` fail.

## Verification

- `recto-cli action run` over a corpus folder with each built-in action finishes with exit `0`, every output is a new uniquely named file, and every input hash is unchanged (`§6`, `§9`, `§14`).
- Two runs into the same folder, a run into the input folder and two same-named inputs from different folders never replace a file (`§6`, `§14`).
- A redaction step fails closed on a page it cannot search, and an `Incomplete` or `Failed` preflight fails the file (`§5`, `§14`).
- A pause appears only for an action with a pause point and holds no worker while waiting (`§9`, `§13`, `§14`).
- The run report lists every input exactly once and exports as JSON, CSV and HTML without injection (`§10`).
- Every control in the Actions view and Run panel carries an automation name and tooltip (`D03 T02 §3`), and every action is reachable from `Ctrl+K`.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
