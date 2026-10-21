---
schema_version: 1
id: action-wizard
domain: 04-convert
status: active
title: "TODO-07 -- Action Wizard: Saved Multi-Step Actions over Files and Folders"
track: M5
---

# TODO-07 -- Action Wizard: Saved Multi-Step Actions over Files and Folders

> **Goal:** Recto matches Acrobat Pro's Action Wizard: an action is a saved, named, ordered list of steps (OCR, optimize, watermark, header and Bates, flatten, sanitize, redact by pattern, encrypt, metadata, PDF/A, preflight, convert and export, save with a naming template) stored as versioned JSON, run over selected files or a whole folder as jobs in the existing queue, with a per-file result and a summary report. It keeps Recto's guarantees: inputs are never overwritten, outputs are new uniquely named files, no step runs PDF JavaScript, Recto makes no network call and every path an action names is a local path (network locations are refused in v1), and a redaction step that could not see a page fails the file instead of passing it, unless the action explicitly opts in to skipping unsearchable pages, in which case the file ends `SucceededWithWarnings` with the skipped pages listed, never plain `Succeeded`.

> **Current state:** Measured 2026-10-21. No action, step or batch code exists. Every operation an action can call is specified but not built: OCR and optimization (`D04 T02 §7`, `§11`), watermark, header and Bates, encryption and redaction (`D05 T01 §5`, `§6`, `§7`, `§10`), annotation and form flattening (`D03 T03 §2`, `§6`), editable metadata (`D02 T03 §10`), `PDF/A` (`D04 T05 §6`), preflight and `PDF/X` (`D04 T06 §6`, `§10`), converters (`D04 T01 §3`), and safe folder enumeration (`D03 T05 §2`). The job runtime and queue are `D04 T01 §1`, `§2`, `§5`, `§7`, and `D04 T01` is frozen: this file depends on its sections and does not edit them.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` decisions D9 (no PDF JavaScript), D16 (offline, no telemetry), D17, D18, and section 4 (jobs open their own `pdf::Document`).
- `AGENTS.md` architecture rules: every file write is temp-file-then-replace, organize and authoring operations work on a copy, the open viewer document is never modified in place, macros and scripts never run, every command is reachable from `Ctrl+K`.
- Known batch failure classes this file must prevent: a batch result silently replacing an earlier one, folder jobs overwriting same-name outputs and existing documents, an output collision check that races with publication, and a staged file not synced before success is reported. Watched folders are not in this file; they are recorded as a demand-ledger row, not as unowned work.
- `design/SPEC.md` sections 9.5 (Convert page and queue card), 9.9 (dialog chrome and completion notices), 11 (accessibility).
- `D04 T01 §1`, `§2`, `§3`, `§4`, `§5`, `§6`, `§7` (runner, per-file results, converter registry, `resolve_output_path`, queue, notices, password recovery), `D04 T02 §1` (`AtomicFileWriter`), `D03 T05 §2` (`FileScope`), `D04 T06 §2` (`BoundedJson`, profile schema), `D06 T03 §5` (save fault injection).
- Cross-file cross-references to this file are added by the coordinator, in both directions.
- -> XREF: D04 T01 §2 -- every action step runs as a job in this runtime.
- -> XREF: D03 T05 §2 -- recursive input sets come from `FileScope`.
- -> XREF: D05 T02 §9 -- redact-by-pattern steps share the Find and Redact presets.
- -> XREF: D07 T03 §6 -- watched-folder automation is a ledger row, not a step type.
- -> XREF: D06 T02 §8 -- encrypt step typed error.
- -> XREF: D03 T03 §40 -- combine form data step.
- -> XREF: D05 T01 §25 -- redact-by-pattern packs.
- -> XREF: D04 T06 §18 -- greyscale step.
- -> XREF: D04 T05 §24 -- pdfua step.
- -> XREF: D05 T01 §4 -- crop step.
- -> XREF: D05 T01 §13 -- remove marks step.
- -> XREF: D05 T01 §20 -- delete blank pages step.
- -> XREF: D05 T01 §2 -- insert pdf step.
- -> XREF: D07 T01 §2 -- batch print step.
- -> XREF: D04 T08 §6 -- CheckedOpen policy.
- -> XREF: D06 T04 §20 -- the `D-Actions.png` board and `design/SPEC.md` section 9.15 that §12, §13, §23 here build against.

## Outcome

- An action authored in the editor saves as `<data_dir>\actions\<id>.action.json`, loads back identically, and a hostile or newer action file is refused or preserved with a reason, never partly run (`§1`, `§2`, `§3`).
- `recto-cli action run sample.action.json in1.pdf in2.pdf --out out\` and the Run panel produce, for each input, a new uniquely named output in the chosen folder; no input and no existing file is ever replaced, even when the output folder is the input folder or two workers publish the same name at once (`§6`, `§9`, `§14`).
- Each file ends `Succeeded`, `SucceededWithWarnings`, `Failed(step, reason)`, `Skipped(reason)` or `Cancelled`, the run ends with a summary report exportable as JSON, CSV or HTML, and a redaction step that finds a page it cannot search fails that file with no output unless the action explicitly opts in to skipping such pages, which ends the file `SucceededWithWarnings` with the pages listed (`§5`, `§9`, `§10`).
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
| 12 | §12 | Actions list and step editor on the Convert page | §2, §3, D04 T01 §3, D02 T01 §10, D02 T03 §11, D06 T04 §20 | [ ] |
| 13 | §13 | Run panel, progress, pause prompt and report view | §10, §12, §20, D04 T01 §5, D04 T01 §6, D03 T02 §5, D06 T04 §20 | [ ] |
| 14 | §14 | Action Wizard gates: no script, no overwrite, hostile actions, end-to-end actions | §7, §11, §13, D06 T03 §5 | [ ] |
| 15 | §15 | `pdfua` step and the `scan-to-accessible` built-in action (OCR then tagged PDF/UA) | §2, §3, §4, §5, D04 T05 §7, D04 T05 §20, D04 T05 §24 | [ ] |
| 16 | §16 | `greyscale` step over the whole-document greyscale fixup | §3, §5, D04 T06 §18 | [ ] |
| 17 | §17 | Page box and background steps: `crop` and `background` | §3, §6, D05 T01 §4, D05 T01 §13 | [ ] |
| 18 | §18 | Page cleanup steps: `remove_marks` and `delete_blank_pages` | §3, D05 T01 §3, D05 T01 §13, D05 T01 §20 | [ ] |
| 19 | §19 | `insert_pdf` step: insert a PDF into many PDFs at a position | §3, §6, §17, D05 T01 §2 | [ ] |
| 20 | §20 | Aggregate terminal steps: one output for a whole run, with all-or-nothing publishing | §3, §6, §9, §10 | [ ] |
| 21 | §21 | `combine_form_data` step: form values from many PDFs into one CSV or workbook | §20, D03 T03 §40, D03 T03 §41 | [ ] |
| 22 | §22 | `print` terminal step: batch print to a named printer | §3, §9, D07 T01 §2, D07 T01 §9 | [ ] |
| 23 | §23 | Print step safeguards: run confirmation, page budget and CLI refusal | §11, §13, §22, D02 T01 §10, D06 T04 §20 | [ ] |
| 24 | §24 | Gates for the new steps: no overwrite, fail closed, honest reports | §14, §15, §16, §17, §18, §19, §21 | [ ] |

## 1. Action model, JSON schema and versioned migration

An action is data: a name, an ordered list of typed steps with parameters, and output settings. This section defines that data, its JSON schema, and how versions are read. It matches the saved actions of Acrobat's Action Wizard and keeps Recto's rule that nothing in an action can execute: there is no script step, no command step and no URL step. The same typed parameter schema drives validation (`§3`) and the editor form (`§12`), so the two cannot disagree.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/actions/action_model.h/.cpp` (`ActionDefinition`, `ActionStepSpec`, `ActionOutputSpec`, `ParamValue`) and `src/services/actions/action.schema.json`. Done when: `ActionModel.SchemaRoundTrip` serializes and re-parses each fixture action with identical content.
2. Implement load with migration and refusal of newer versions. Done when: `ActionModel.Versions` migrates an older fixture, refuses a newer one and preserves an unknown step type.

- [ ] Define the document in `src/services/actions/action.schema.json`: `schema` (`"recto.action"`), `schema_version` (integer), `id` (lowercase `a-z0-9-`, `2` to `60` characters), `name` (`1` to `80` characters), `description`, `revision` (integer, incremented on each save), `steps[]` of `{id, type, type_version, params, pause_before, prompt}`, and `output` of `{folder, name_template, if_exists, keep_structure}`. Done when: the schema rejects an unknown top-level key and an `if_exists` other than `append_number` or `skip`.
- [ ] Forbid replacing: `if_exists` has exactly the values `append_number` and `skip`, and there is no `replace` value anywhere in the schema, model or UI. Done when: `ActionModel.NoReplaceValue` fails if the enum gains a replace-like value.
- [ ] Define `ParamSchema` field kinds used by every step in `src/services/actions/param_schema.h`: scalars (`bool`, `int` with range, `number` with range, `enum`, `string` with maximum length, `file_path`, `folder_path`, `colour_hex`, `secret`) and structured kinds `object{fields}`, `list{item schema, maximum count}`, `union{tag field, variants}` and `provided{provider id}` (a schema supplied at run time by a registry, used by `convert` for the option schema of a converter card). Redaction patterns are `list{union{kind: literal|regex|preset}}`, metadata `set` is an `object`, and Bates is an `object`. Done when: `ActionModel.ParamKinds` validates one value of each kind, rejects one out-of-range or malformed value of each, and `ActionModel.NestedParams` validates a `patterns[]` list with a wrong tagged variant and reports the nested JSON pointer.
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
- [ ] Ship read-only built-in actions as resources `src/services/actions/builtin/*.action.json`: `scan-to-searchable` (OCR, optimize, save), `prepare-for-sharing` (flatten annotations, sanitize, optimize, save), `print-ready` (preflight with fix, `PDF/X-4`, save), and `archive-pdfa` (OCR, `PDF/A-2u`, save); built-ins duplicate to edit and cannot be deleted or overwritten. Done when: `ActionStore.Builtins` lists exactly the ids in the `builtin_action_ids` table (the four above to start; later sections add an id with their action file), and delete and save-over return a typed refusal.
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

- [ ] Define `IActionStep` (`id`, `type_version`, `title`, `icon_glyph`, `param_schema()`, `summary(params)`, `kind` of `PdfToPdf`, `TerminalSave` or `TerminalExport` (later sections add `TerminalAggregate` and `TerminalPrint`; every terminal kind counts as the one terminal step), `prepare_run(const InputSet&, params) -> PdfResult<StepRunState>` for run-wide state, and `run(StepContext&) -> PdfResult<StepResult>`). `StepContext` carries the input path, the output scratch path, the already opened and checked handle of every `file_path` and `folder_path` parameter (`OpenedParam`, produced by the checked opener of `§6`; a step reads parameter files only from that handle and never opens a parameter by path), a `CancelToken`, a progress sink, resolved secrets, the shared `StepRunState` and the run's `FileScope` summary. Done when: `src/services/actions/action_step.h` compiles in `recto-services` and `recto-cli`.
- [ ] Enforce by signature that a step receives paths and opens its own `recto::pdf::Document` on the job thread, never a `DocumentActor*` or a viewer document, and writes only into `StepContext::output_path` through `AtomicFileWriter`. Done when: `ActionSteps.Isolation` runs a step on a file open in a `DocumentActor` with no shared instance, and a step that writes anywhere else fails `ActionSteps.WritesOnlyScratch`.
- [ ] Implement `validate_action(const ActionDefinition&) -> ValidationReport` returning errors and warnings with step id and JSON pointer: parameter kinds and ranges from each `ParamSchema`, exactly one terminal step and it last, and the order rules below. Done when: `ActionSteps.Validation` rejects an action with two terminal steps, no terminal step, and a step after the terminal step.
- [ ] Implement the order rules as a table of registered rules (`register_order_rule(id, severity, message, predicate)`, so later sections add rules for their own steps without editing the validator), with these as the initial entries, each with severity and message: `encrypt` must be the last `PdfToPdf` step (error); `optimize` with `linearize` must be the last `PdfToPdf` step (error, any later rewrite drops linearization); `encrypt` after `pdfa` or `pdfx_convert` (error, those standards forbid encryption); any content or metadata step (`ocr`, `optimize`, `watermark`, `header_footer_bates`, `flatten`, `sanitize`, `redact_patterns`, `metadata`) after `pdfa` or `pdfx_convert` (error, the later step can break the standard the earlier one established); `metadata` before `sanitize` (warning, sanitize removes it); `redact_patterns` before any `ocr` step (warning, scanned pages have no text to match). Done when: `ActionSteps.OrderRules` triggers each rule with a fixture action and none on the built-ins.
- [ ] Validate every `file_path` and `folder_path` parameter shape (no wildcard, no device name, length) at save, and at run start run the checked opener of `§6` once as an early diagnostic, reporting a missing or refused stamp image as an error naming the step; the run-start result is advisory, and the authoritative check is the one made when the step opens the parameter. Done when: `ActionSteps.Paths` reports a missing image path at run start and not at save.
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
- [ ] Register all six in `ActionStepRegistry` with `ParamSchema`, summaries and Fluent UI System Icons names from the SPEC section 6 table. Done when: `ActionSteps.Registry` lists them and `validate_action` accepts the built-ins.
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
- [ ] Require complete search coverage before redaction can succeed: every page in scope must come back from `SearchEngine` as fully searched, and a `Truncated` outcome or a recovery note on any page makes that page unsearchable, never a warning. `on_unsearchable_pages` applies only to pages with no text layer or an unreadable one: `fail` (default) fails the file naming the pages and writes no output; `ocr_then_redact` runs `create_searchable_pdf` on those pages first and then requires full coverage again; `skip_with_warning` is the single explicit exception: the named pages are left unredacted and unsearched, listed in the file's `SkippedPages` and in the report, the file ends `SucceededWithWarnings` and never `Succeeded`, and it is unavailable for `Truncated` pages. The post-apply verification and the redaction gates take the scope as every page except the recorded `SkippedPages`, and also check that each skipped page is byte-identical in content to its input. Done when: `ActionProtect.UnsearchablePages` fails a scanned fixture by default, redacts it after OCR with the second mode, and with the third ends `SucceededWithWarnings` listing the page; `ActionProtect.TruncatedSearchFails` fails a file whose second page holds the sensitive text beyond an extraction cap after a searchable public first page, in every mode.
- [ ] Verify after applying: re-run the same patterns on the redacted scratch file with the same full-coverage requirement and fail the file if any match remains or coverage is incomplete, and also check with an extractor that does not share the search path (the operator trace and `PageText` of `D06 T03 §6`), in addition to its raw-byte oracles. Done when: `ActionProtect.VerifyAfterRedact` fails when a planted engine bug leaves one match, fails when the verification search is truncated, and passes otherwise. Cheaper substitute: trusting the redactor's return value, or repeating the same truncated search.
- [ ] Implement `encrypt` over `encrypt_pdf` (`D05 T01 §7`) with `user_password` and `owner_password` as `secret` parameters resolved at run start (prompt or environment variable), and the permission flags; refuse an action whose encrypt step has neither password. Done when: `ActionProtect.Encrypt` encrypts a fixture, the output requires the password, and the password appears in no log, report or action file.
- [ ] Implement `metadata` over `metadata_edit` (`D02 T03 §10`) with `set` for `title`, `author`, `subject`, `keywords`, supporting the tokens `{name}`, `{date}` and `{n}`, `clear` for chosen fields, and `remove_custom_xmp`. Done when: `ActionProtect.Metadata` sets and clears fields and keeps `/Info` and XMP synchronized.
- [ ] Implement `pdfa` over `ComplianceEngine::convert_to_pdfa` (`D04 T05 §6`) with `level`, `colour_profile`, `auto_tag`, then run `validate_pdfa` on the output and fail the file with the findings unless `on_partial` is `warn`. Done when: `ActionProtect.PdfA` converts a fixture to `PDF/A-2u` with zero findings and fails a fixture the converter cannot fix.
- [ ] Implement `preflight` over `PreflightEngine` and the fixups (`D04 T06 §6`, `§8`) with `profile`, `fix` and `on_fail` of `fail_file` (default) or `warn`, treating `Incomplete` as `Failed` unless `on_incomplete` is `warn`; implement `pdfx_convert` over `convert_to_pdfx` (`D04 T06 §10`) with `level`, `output_profile`, `bleed_pt` and `on_partial`. Done when: `ActionProtect.Preflight` fails a fixture with an error finding, treats `deep-forms.pdf` as failed, and `ActionProtect.PdfX` writes no identification on a partial conversion.
- [ ] Register all six steps with `ParamSchema` and summaries, and add `ActionProtect.*` tests in `tests/recto-tests/action_protect_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionProtect.*` passes under ASan.
- [ ] Commit: `services: add redact by pattern, encrypt, metadata, PDF/A, preflight and PDF/X action steps`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionProtect.*` passes under ASan, and making `on_unsearchable_pages` default to `skip_with_warning` makes `ActionProtect.UnsearchablePages` fail.

## 6. Output naming template and collision-free exclusive publish

Where an output goes and what it is called decides whether a batch destroys work. Batch and folder jobs that replace earlier results or existing documents, or whose collision check races with publication, destroy work. This section builds the naming template and the one publish path every action output uses: a name is claimed by creating the file exclusively, never by checking first and writing later, so two workers publishing the same name cannot overwrite each other, and an input can never be the target.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `NameTemplate` in `src/services/actions/name_template.h/.cpp`. Done when: `ActionNaming.Template` expands every token and sanitizes unsafe names.
2. Implement `OutputPublisher` in `src/services/actions/output_publisher.h/.cpp`. Done when: `ActionNaming.ExclusivePublish` publishes the same name from `8` threads and gets `8` distinct files, none replacing another.

- [ ] Implement tokens `{name}` (input base name), `{ext}`, `{action}`, `{date}` (`yyyy-MM-dd`), `{time}` (`HH-mm-ss`), `{n}` (position in the run, zero-padded to the run's width) and `{parent}` (the input's folder name), reject unknown tokens at save, and default the template to `{name}-{action}`. Done when: `ActionNaming.Template` expands each token and rejects `{bogus}`.
- [ ] Sanitize the expanded name for Windows: replace `\ / : * ? " < >` and `|` and control characters, strip trailing dots and spaces, avoid reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1` to `COM9`, `LPT1` to `LPT9`) including with an extension, and cap the file name at `200` UTF-16 units keeping the extension. Done when: `ActionNaming.Sanitize` handles non-Latin names, a trailing dot and `con.pdf` without error.
- [ ] Publish through held directory handles, not paths: `OutputPublisher` opens the resolved output root, then each mirrored component relative to its already-open parent (`NtCreateFile` with `RootDirectory` and `FILE_OPEN_REPARSE_POINT`, refusing any symbolic-link or junction reparse tag), and keeps those directory handles open without `FILE_SHARE_DELETE` for the whole creation, publication and rollback of that input's outputs, so an ancestor cannot be renamed or replaced while they are held. The temp file is created relative to the final directory handle, written and flushed (`FlushFileBuffers`), and published by a handle-relative rename that does not replace an existing file (`FileRenameInformation` with `RootDirectory` and no replace flag); on `ERROR_ALREADY_EXISTS` the next `name (2).pdf` up to `9999` is tried, so a name is claimed atomically at publication and never by a prior existence check. `AtomicFileWriter` (`D04 T02 §1`) is path-based, so this handle-relative writer reimplements its temp-flush-publish contract for action outputs and must pass the same `AtomicFile.*` behaviours; scratch files and reports keep using `AtomicFileWriter`. Done when: `ActionNaming.ExclusivePublish` runs `8` threads publishing the same base name and finds `8` distinct files with the expected contents, and `ActionNaming.HandleRelative` shows an attempt to rename or replace the output folder or an ancestor while a publish is in flight fails with a sharing violation. Cheaper substitute: `exists()` followed by a path-based write, which races, or re-resolving the destination path at publication.
- [ ] Refuse any published path that is the same file as an input of the run, comparing volume serial and file index from `GetFileInformationByHandle` (so hard links, junction aliases and case differences are caught), and rename instead of replace when the output folder is the input folder. Done when: `ActionNaming.NeverInput` publishes beside the input without touching it, including through a hard link to the input.
- [ ] Apply `if_exists` of `skip` by treating an existing target name as `Skipped(exists)` for that file with no write, and `append_number` as above. Done when: `ActionNaming.SkipPolicy` skips an existing output and reports it.
- [ ] With `keep_structure`, create the mirrored subfolders (depth at most `32`) relative to the held parent handles of the item above, and without it flatten and rely on the exclusive publish for same-name inputs from different folders. The output root is opened once through `CheckedOpen` (a root that is a symbolic link or junction is refused, so the root handle is already its real local directory) and held for the run; every mirrored component is opened relative to it with `FILE_OPEN_REPARSE_POINT` and refused if its reparse tag is a symbolic link or mount point; every create, publish and rollback delete is handle-relative to a held directory whose identity (volume serial and file index) was recorded and re-checked on the handle before publication. Because nothing is created by path, nothing is written outside the root; a directory swap attempted during the run fails on the held handle. Done when: `ActionNaming.Structure` mirrors `a\x.pdf` and `b\x.pdf` in one case and produces `x.pdf` and `x (2).pdf` in the other, and `ActionNaming.Containment` makes the output root itself a junction (refused) and `<output>\a` a junction to another directory before the run (refused) and attempts the swap after the identity check with fault injection (the swap fails) and finds nothing written outside the root in both.
- [ ] Refuse an output folder that is read-only or cannot be created with a typed error before any file runs. Done when: `ActionNaming.OutputFolderErrors` refuses a read-only folder at run start with no work done.
- [ ] Accept only local targets for inputs, output folders and every `file_path` or `folder_path` parameter through one checked opener, `CheckedOpen` in `src/services/actions/checked_open.h/.cpp`, whose result is the opened handle that is then used: it opens the volume root, then each component relative to its retained parent (`NtCreateFile` with `RootDirectory` and `FILE_OPEN_REPARSE_POINT`), refuses a symbolic link or mount point at any component including the leaf (no exception for output roots: a junction or symlink output folder is refused, and the user chooses the real folder), keeps every parent handle open without `FILE_SHARE_DELETE` until the caller has finished with the object so a parent cannot be swapped between check and use, and requires the opened object's `GetFinalPathNameByHandleW` to be a local volume path (not `\\?\UNC\`) on a volume `GetDriveTypeW` does not report as `DRIVE_REMOTE`. Steps read parameter files (for example a watermark image, bounded to `64 MiB` and read into memory) from that handle and pass bytes or the handle to the engine, never a path. The same opener runs at import (advisory), at save (advisory) and at the point of use (authoritative), with a reason naming the path. Network locations are unsupported in v1 so that an imported action cannot move documents over SMB without the user seeing it; supporting them later needs an explicit per-run disclosure and confirmation, recorded as the cost of reversing this default. Done when: `ActionNaming.LocalOnly` refuses a UNC output folder, a remote mapped drive, a UNC stamp image path, a local-looking stamp path whose ancestor is a symbolic link to `\\localhost\c$`, and a junction output folder, with a network trap proving no redirection was followed; and `ActionNaming.CheckedOpenRace` replaces `C:\assets` with such a symbolic link after the check and before the read (and again after run-start validation and before the watermark step), with fault injection, and finds the replacement fails on the retained handle and the stamp is read only from the checked object.
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
- [ ] Run the card's `job_factory` for the file with the previous step's scratch file as the input, write every output it produces (an image export makes many files) into a per-file staging folder inside the run's scratch area and validate each, and only then publish the whole set through `OutputPublisher` (`§6`) with the same naming template and a per-output suffix. Publication of several files cannot be one atomic operation, so it is a transaction with rollback: the publisher records each file it published, and when a later one fails (or the run is cancelled mid-set) it deletes the files it published for that input after confirming each is still the file it created, and any it cannot delete is reported as a retained output with its path, never hidden. A file whose set was rolled back is `Failed` with no outputs; a file with retained outputs is `Failed` and lists them. Done when: `ActionConvert.MultiOutput` publishes a `9`-image export as nine uniquely named files, and `ActionConvert.PublishRollback` injects a failure while publishing image five and finds images one to four removed and the file `Failed`, and with a locked image three finds that path listed as retained.
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
- [ ] Implement `stop`: trip every `CancelToken`, let in-flight steps stop at their next cooperative check, delete in-flight scratch and unpublished temp files, keep the outputs of every input whose output set was completed before the stop, roll back the output set that was still being published for any input (the files already published for that one input are deleted by the rollback of `§7`, and any that cannot be deleted are listed as retained outputs), and mark unstarted files `Cancelled`. Done when: `ActionRun.Stop` stops a `20`-file run mid-way with completed outputs intact and no `.tmp` left, and `ActionRun.StopMidSet` cancels after image four of a nine-image export and finds those four removed and the file `Cancelled`, or listed as retained when a deletion is blocked.
- [ ] Recover passwords through the existing flow: an encrypted input yields `NeedsPassword` (`D04 T01 §2`), the run keeps the file parked, and `resubmit_with_password` (`D04 T01 §7`) resumes it; offer a run-level list of passwords to try, held in memory only. Done when: `ActionRun.Passwords` unlocks an `AES-256` fixture from the list and from a single prompt and persists no password.
- [ ] Isolate failures: a failing file records `Failed{step, reason}` with the engine's message and never aborts other files; it leaves no partial output, with one stated exception: when the rollback of `§7` cannot delete a file it published, that file is listed as a retained output in the file's report entry and the file is still `Failed`. A step that returns `Truncated` or a recovery note adds a warning to the file's outcome unless the step declares truncation fatal in its contract (`truncation_is_fatal`, set by `redact_patterns`, `sanitize`, `preflight` and `pdfx_convert`), in which case the file fails. Done when: `ActionRun.FailureIsolation` runs a batch with one corrupt and one encrypted file and completes the rest, `ActionRun.TruncationFatal` fails a file when a fatal-truncation step reports `Truncated`, and `ActionRun.RetainedOutputs` shows a blocked deletion is reported and not hidden.
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
- [ ] Define `FileReport` (input path, size and modified time, outcome, the steps run with status, duration and warnings, the output paths with size and SHA-256, the failing step and reason, `BatesGap` ranges, and `retained_outputs` with the reason each could not be removed). Done when: `ActionReport.FileFields` checks every field on a run with one success, one warning, one failure, one skip and one cancel.
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

**Fidelity:** Genuinely new surface: no SPEC screenshot shows it. It follows `design/SPEC.md` section 9.5 (the Convert page layout and cards) for the host, section 9.9 for dialogs, section 4 tokens, and the two-column editor pattern of the Watermark dialog. Design board: `design/SPEC.md` section 9.15 with `design/screenshots/D-Actions.png` (drawn by `D06 T04 §20`).
**Job:** A user opens Actions, picks a built-in or saved action, duplicates or creates one, adds steps from a grouped menu, reorders them, sets each step's parameters, marks a pause point if they want one, sees validation messages immediately, and saves, exports or imports it.
**Treatment:** A `SelectorBar` at the top of `ConvertPage` (`Convert`, `Actions`); on the left an action list with search and `New`, `Duplicate`, `Delete`, `Import`, `Export`; on the right the editor with a name box, a description, an ordered `ListView` of step cards (glyph, title, one-line summary, pause marker, remove) reordered by drag and by `Move up` and `Move down` buttons, an `Add step` menu grouped as `Document`, `Protect and standards`, `Convert and save`, a parameter form generated from the step's `ParamSchema`, a validation `InfoBar` list, and `Save` and `Run...`. Built-in actions open read-only with a `Duplicate to edit` banner. Cheaper substitute that fails the checkpoint: a raw JSON text box, or reordering by drag only.
**Chrome:** Consume `ConvertPage` and its registry (`D04 T01 §3`), the 9.9 dialog host (`D02 T01 §10`), `CommandRegistry` (`D02 T03 §11`), and `RectoColors.xaml` tokens with Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Implement `ActionsViewModel` and `ActionEditorViewModel` in `src/services/actions_view_model.h/.cpp` over `ActionStore`, `ActionStepRegistry` and `validate_action`. Done when: `ActionsUi.ViewModel` creates, edits, reorders, validates and saves an action with no UI thread blocking.
2. Build `src/app/views/ActionsView.xaml/.cpp`, `ActionEditor.xaml/.cpp` and the parameter form generator. Done when: `AppTests.ActionsEditor` builds a copy of `scan-to-searchable` from empty by adding steps and setting parameters.

- [ ] Add the `Convert | Actions` `SelectorBar` to `ConvertPage` and host `ActionsView` in its content area, and record the Actions view in `design/SPEC.md` section 9.5. Done when: switching between `Convert` and `Actions` keeps the Step 1 files, and the SPEC row exists.
- [ ] Build the action list with search, built-in and saved groups, damaged actions shown with their reason, and the buttons `New`, `Duplicate`, `Delete` (with a `ContentDialog` confirmation), `Import` and `Export` through file pickers calling `ActionStore`. Done when: `AppTests.ActionsEditor` lists, duplicates and deletes a saved action and shows a damaged file with its reason. Cheaper substitute: deleting without a confirmation.
- [ ] Generate the parameter form from each `ParamSchema` (toggles, number boxes with range, combo boxes, text boxes with maximum length, pickers for files and folders, nested editors generated from `object`, `list` and `union` kinds (add, remove, reorder rows; a tag selector that swaps the variant fields), a colour picker that stores a hex value, and a `secret` row that offers `Ask when the action runs` and `Read from environment variable` and no text box), with inline validation messages. Done when: `AppTests.ActionsEditor` edits a redaction pattern's `kind`, `value`, `case_sensitive` and `whole_word` and saves it identically to the schema form, shows the secret row has no text input and an out-of-range number shows its message.
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

**Fidelity:** Genuinely new surface: no SPEC screenshot shows it. It follows `design/SPEC.md` section 9.5 (Step 1 file chips and drop zone, the queue card states) and section 9.9 (dialogs and the completion notice), with section 4 tokens. Design board: `design/SPEC.md` section 9.15 with `design/screenshots/D-Actions.png` (drawn by `D06 T04 §20`).
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
- [ ] Expose `ActionRunViewModel::aggregate_outputs()` and show each aggregate output (`§20`) in the report view with `Open file`, `Show in folder`, a `Partial` label with the excluded files and their reasons when it is partial, and a run with no aggregate output when the run was stopped. Done when: `ActionRunUi.AggregateOutputs` returns the published path of a test run and `AppTests.ActionRunPanel` shows a combined-form-data output and its partial label. Cheaper substitute: listing the aggregate output only in the saved report.
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
2. Run the end-to-end actions. Done when: `ActionGates.BuiltinsEndToEnd` runs every built-in action (`builtin_action_ids`, `§2`) over a corpus folder through the CLI and through the runner.

- [ ] Gate `ActionGates.NeverOverwrites`: run the same action twice over the same inputs into the same folder, into the input folder, and with two inputs sharing a base name from different folders, and assert every earlier file is byte-identical afterwards and every new output has a distinct name. Done when: the gate passes and a planted replace in the publisher fails it.
- [ ] Gate `ActionGates.PublishRace`: publish the same name from `8` concurrent runs with fault injection at each `AtomicFileWriter` step using the `D06 T03 §5` harness, and assert no output is lost, no temp file remains and the input hash is unchanged. Done when: the gate passes under ASan.
- [ ] Gate `ActionGates.HostileActions`: import and run a table of hostile action files (oversized, deep, duplicate keys, a literal password, an unknown step type, a path-traversal output template such as `..\..\x`, a template that expands to a reserved device name) and assert each is refused or sanitized with the reason and nothing outside the output folder is written. Done when: every row passes.
- [ ] Gate `ActionGates.LocalOnly`: run an imported action naming a UNC output folder, a UNC stamp image, a junction output folder, and a stamp image under a local symbolic link that points at an SMB share, with a network trap installed, and again with the link swapped in between validation and use, and assert each is refused before any access to the remote target. Done when: the gate passes and removing the reparse-point check, the final-path check or the read-from-handle rule fails it.
- [ ] Gate `ActionGates.NoActiveContent`: run every step type over fixtures carrying `/JavaScript`, `/Launch`, embedded Office macros in attachments and HTML script, with process-creation and network traps, and assert none ran. Done when: the gate observes zero traps tripped.
- [ ] Gate `ActionGates.RedactionFailsClosed`: with the default `on_unsearchable_pages`, run `redact_patterns` over a scanned page, a page with a pattern split across lines, a page with a pattern inside an annotation, and a searchable page followed by a page whose text lies beyond an extraction cap, and assert each either redacts completely (raw-byte oracle of `D06 T03 §6`) or fails the file with no output. Done when: the gate passes and removing the post-apply verification fails it.
- [ ] Gate `ActionGates.RedactionExplicitSkip`: with `skip_with_warning`, run the scanned page and assert the file ends `SucceededWithWarnings`, the page is listed in `SkippedPages`, every other page passes the raw-byte oracle, the skipped page is unchanged, and a `Truncated` page still fails the file. Done when: the gate passes and reporting plain `Succeeded` for the file fails it.
- [ ] Gate `ActionGates.BuiltinsEndToEnd`: run every built-in action in `builtin_action_ids` (`scan-to-searchable`, `prepare-for-sharing`, `print-ready`, `archive-pdfa` and the ones later sections add) over `tests/corpus/` through `recto-cli action run` and through `ActionRunner`, and assert identical outputs, the expected report counts, and a passing `PDF/A` or `PDF/X` validation where the action claims one. Done when: the gate passes and the CLI and runner outputs are byte-identical apart from timestamps.
- [ ] Gate `ActionGates.PauseHoldsNoWorker`: run a paused action beside `4` other conversions and assert the others finish while the paused file waits. Done when: the gate passes.
- [ ] Record watched folders as a demand-ledger row in `docs/user-demand.md` when that file exists (created by `D07 T03 §6`), stating that they are out of scope for v1 and why. Done when: the row exists with the issue number and the reason.
- [ ] Commit: `test(quality): prove action runs never overwrite, never run active content and fail closed`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionGates.*` passes under ASan, and replacing the exclusive publish with a replace makes `ActionGates.NeverOverwrites` fail.

## 15. `pdfua` step and the `scan-to-accessible` built-in action (OCR then tagged PDF/UA)

UPDF can run OCR and produce a tagged accessible PDF in one pass; Acrobat Pro needs the Accessibility tool run by hand. This section adds the `pdfua` step, a thin adapter over `convert_to_pdfua` and the PDF/UA validators (`D04 T05 §7`, `§23`, `§24`), and a fifth built-in action `scan-to-accessible` that chains `ocr`, `pdfua` and `save`, so a folder of scans becomes tagged PDF/UA files in one run. The step keeps the honesty rules of `D04 T05`: a file ends `Failed` when the validator reports findings (unless the action says `warn`), a truncated structure walk is fatal, `PartialChecksPassed` for PDF/UA-2 is reported as partial checks and never as conformance, and Alt text the tagger invented as a placeholder (`"Figure on page N"`) is counted and makes the file `SucceededWithWarnings`, because a placeholder does not give a screen-reader user a description.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the `pdfua` step in `src/services/actions/steps/pdfua_step.cpp` and register its `ParamSchema`, summary and order rules. Done when: `ActionAccess.PdfUaStep` tags an untagged corpus file and the output passes `validate_pdfua`, under ASan.
2. Add the `scan-to-accessible` built-in and its store registration. Done when: `ActionAccess.ScanToAccessible` runs OCR, tagging and save over `tests/corpus/text/scanned.pdf` and the output has searchable text and passes `validate_pdfua` with the placeholder warning reported.

- [ ] Define the `pdfua` parameters in its `ParamSchema`: `part` (`ua1` default, `ua2`), `language` (BCP 47 tag or `auto`, where `auto` takes the document `/Lang`, else the language detected by OCR, else `GetUserDefaultLocaleName`), `title` (default `{name}`, used only when the document has no title), `tag_equations` (default `false`, uses `tag_formulas` of `D04 T05 §20`), `on_findings` (`fail` default or `warn`) and `on_placeholder_alt` (`warn` default or `fail`). Done when: `ActionAccess.ParamSchema` validates one value of each kind and rejects an unknown `part`.
- [ ] Implement `run` over `convert_to_pdfua(doc, part, options)` (`D04 T05 §7`, `§24`) on the step's own `pdf::Document`, then run `validate_pdfua` or `validate_pdfua2` on the scratch output; fail the file with the findings unless `on_findings` is `warn`, and treat `Truncated` or an `Incomplete` check as failure (`truncation_is_fatal`). Done when: `ActionAccess.PdfUaStep` fails a fixture the tagger cannot fix and passes one it can.
- [ ] Add `placeholder_alt_count` from the tagger's report to the file's warnings as `PlaceholderAltText{count}` so the file ends `SucceededWithWarnings` (or `Failed` when `on_placeholder_alt` is `fail`). Done when: `ActionAccess.PlaceholderAlt` finishes a three-figure fixture `SucceededWithWarnings` with the count `3` in the report.
- [ ] Report PDF/UA-2 results as `PartialChecksPassed` or `PartialChecksFailed` in the file's step detail, never as conformance. Done when: `ActionAccess.Ua2PartialStatus` finds the wording in the report and no `conformant` string.
- [ ] Register order rules with `register_order_rule` (`§3`): `pdfua` before any `ocr` is a warning (scanned pages have no text to tag); a content or metadata step after `pdfua` is a warning (new content is untagged unless the step marks it as an artifact); `pdfua` after `pdfa` or `pdfx_convert` is an error (the rewrite can break the standard established earlier); and add `pdfua` to the set of steps that `encrypt` must follow. Done when: `ActionAccess.OrderRules` triggers each rule with a fixture action.
- [ ] Add `src/services/actions/builtin/scan-to-accessible.action.json` (`ocr` with skip of pages that already have text, `pdfua` with `part ua1` and `language auto`, `save` with the template `{name}-accessible`) and register its id in the `builtin_action_ids` table of `§2`. Done when: `ActionStore.Builtins` lists it read-only and `validate_action` accepts it with no warning.
- [ ] Add `ActionAccess.*` tests in `tests/recto-tests/action_access_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionAccess.*` passes under ASan.
- [ ] Commit: `services: add the pdfua action step and the scan-to-accessible built-in`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionAccess.*` passes under ASan, and counting placeholder Alt text as a pass fails `ActionAccess.PlaceholderAlt`.

## 16. `greyscale` step over the whole-document greyscale fixup

Foxit PDF Editor converts a document to greyscale and a batch often needs it (a folder of coloured forms headed for a black-and-white copier). This section adds the `greyscale` step as a thin adapter over `fix.convert_to_grey` (`D04 T06 §18`): parameters in, one engine call, one scratch file out. The step never reports success for a file that kept colour: with the default `on_unconverted = fail` the file ends `Failed` and lists the objects that stayed coloured, and `warn` ends `SucceededWithWarnings` with the same list.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the `greyscale` step in `src/services/actions/steps/greyscale_step.cpp`. Done when: `ActionGrey.Step` converts the `greyscale-mixed.pdf` fixture and the output passes the `colour.only_gray` check, under ASan.
2. Register its schema, order rules and outcomes. Done when: `ActionGrey.OrderRules` and `ActionGrey.Outcomes` pass.

- [ ] Define the `greyscale` parameters: `method` (`pdf` default, `rec709`, `icc`), `gray_profile` (`file_path`, required for `icc`, read as bytes from its `OpenedParam` handle and handed to `IccProfileInfo::read`, never opened by path), `preserve_pure_black` (default `true`) and `on_unconverted` (`fail` default or `warn`). Done when: `ActionGrey.ParamSchema` rejects `icc` without a profile and accepts the others.
- [ ] Implement `run` over `fix.convert_to_grey` (`D04 T06 §18`) with `truncation_is_fatal` set, publishing nothing itself (the scratch output flows to the next step). Done when: `ActionGrey.Step` matches the oracle values of `D04 T06 §18` for the three methods.
- [ ] Map the `GreyReport` to the file outcome: no unconverted objects gives `Succeeded`, unconverted objects give `Failed` listing each object and reason (or `SucceededWithWarnings` with `warn`). Done when: `ActionGrey.Outcomes` covers the three cases with `undecodable-image.pdf`.
- [ ] Register order rules (`§3`): `greyscale` after `pdfa` or `pdfx_convert` is an error; a `watermark`, `header_footer_bates` or `background` step with a non-grey colour after `greyscale` is a warning (it adds colour back); `greyscale` before `pdfx_convert` to a CMYK output intent is a warning. Done when: `ActionGrey.OrderRules` triggers each rule.
- [ ] Add `ActionGrey.*` tests in `tests/recto-tests/action_grey_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionGrey.*` passes under ASan.
- [ ] Commit: `services: add the greyscale action step`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionGrey.*` passes under ASan, and ignoring `unconverted` objects fails `ActionGrey.Outcomes`.

## 17. Page box and background steps: `crop` and `background`

UPDF, PDFelement and Foxit PDF Editor all batch-apply crops and page backgrounds; Acrobat Pro does it through the Action Wizard's page steps. This section adds two thin adapters over `crop_and_resize_pages` (`D05 T01 §4`) and `add_background` (`D05 T01 §13`), and the `ParamStager` that lets any step hand a parameter file to an engine that wants a path without opening that path itself. The stager copies the bytes of an already opened and checked parameter handle (`CheckedOpen`, `§6`) into a run-private scratch file once per run, so a path swapped for a link after validation is never followed (`§6`).

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `ParamStager` in `src/services/actions/param_stager.h/.cpp` and the `crop` step. Done when: `ActionPageBoxes.Stager` stages a file from a handle and `ActionPageBoxes.Crop` crops and rescales corpus pages, under ASan.
2. Implement the `background` step. Done when: `ActionPageBoxes.Background` adds a colour, an image and a PDF-page background and leaves the original content streams byte-identical.

- [ ] Implement `ParamStager::stage(const OpenedParam&, run_scratch) -> PdfResult<std::filesystem::path>` copying at most `64 MiB` into `AppPaths::temp_dir()/actions/<run-id>/params/<n>.bin` with `AtomicFileWriter`, once per parameter per run, removed by `JobTempScope` on every exit. Done when: `ActionPageBoxes.Stager` stages once for `50` files, refuses a larger parameter file, and leaves no scratch file after success, failure and cancel.
- [ ] Define the `crop` parameters: `box` (`crop` default, `media`, `trim`, `bleed`, `art`), `margins` (`top`, `right`, `bottom`, `left` in points), `auto_margins` with `padding_pt`, `resize` (`none`, `a4`, `letter`, `legal`, `custom` with width and height) and `pages` (`all`, `even`, `odd` or a range). Done when: `ActionPageBoxes.CropParams` rejects negative margins and a custom size without dimensions.
- [ ] Implement `crop` over `crop_and_resize_pages` (`D05 T01 §4`) with `CropResizeOptions` built from the parameters, failing the file with a typed reason when the result box would be empty or outside the media box. Done when: `ActionPageBoxes.Crop` crops, auto-detects margins and rescales A4 to Letter, and fails a margin larger than the page.
- [ ] Define the `background` parameters: `source` (`colour`, `image`, `pdf_page`), `colour_hex`, `image_path` and `pdf_path` (`file_path`, staged through `ParamStager`), `pdf_page`, `fit` (`fill`, `fit`, `centre`, `tile`), `scale_percent`, `opacity`, `rotation_deg`, `target_box` and `pages`. Done when: `ActionPageBoxes.BackgroundParams` rejects a source without its matching field.
- [ ] Implement `background` over `add_background` (`D05 T01 §13`) with the staged path, add the `occluded_pages` result to the file's warnings, and register order rules (`§3`): a content or metadata step after `pdfa` or `pdfx_convert` stays an error for both steps (they are added to that rule's set), and `background` after `pdfua` is a warning (the mark is an artifact but the tree should be re-validated). Done when: `ActionPageBoxes.Background` adds the three sources, reports an occluded page as a warning, and keeps original streams byte-identical.
- [ ] Add `ActionPageBoxes.*` tests in `tests/recto-tests/action_page_boxes_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionPageBoxes.*` passes under ASan.
- [ ] Commit: `services: add the crop and background action steps with a checked parameter stager`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionPageBoxes.*` passes under ASan, and opening the image parameter by its path instead of the staged handle fails `ActionPageBoxes.Stager`.

## 18. Page cleanup steps: `remove_marks` and `delete_blank_pages`

Foxit PDF Editor, PDFelement and UPDF remove watermarks, backgrounds, headers and footers from many files at once and delete blank pages from scans; Acrobat Pro does both through page tools. This section adds two steps. `remove_marks` is an adapter over `list_marks` and `remove_mark` (`D05 T01 §13`), which find the marks Recto added and the watermark artifacts other producers wrote. `delete_blank_pages` calls `detect_blank_pages` and `plan_blank_page_deletion` (`D05 T01 §20`) and the page edit list of `D05 T01 §3`. Deleting pages unattended is the risky one, so it carries a guard: if more than a set share of the pages would go, the file fails instead of losing most of a document because of a detector misfire, and the deleted page numbers are always listed in the report.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `remove_marks` in `src/services/actions/steps/page_cleanup_steps.cpp`. Done when: `ActionCleanup.RemoveMarks` removes a background, a watermark and a header or footer from fixtures and the page text and pixels equal the unmarked source.
2. Implement `delete_blank_pages` with its guard. Done when: `ActionCleanup.BlankPages` deletes blank scans, keeps a page with an annotation, and fails a file where the share exceeds the guard, under ASan.

- [ ] Define the `remove_marks` parameters: `kinds` (a list of `background`, `watermark`, `header_footer`), `names` (optional list of mark names), `pages`, and `on_none_found` (`continue` default, which adds a warning `NoMarksFound` and passes the file on, or `skip_file`). Done when: `ActionCleanup.ParamSchema` rejects an empty `kinds` list.
- [ ] Implement `remove_marks` over `list_marks` and `remove_mark` (`D05 T01 §13`), removing only marks whose kind and name match, counting removed marks per kind in the file's step detail, and failing the file with a typed reason when a matching mark is not removable. Done when: `ActionCleanup.RemoveMarks` removes each kind, `ActionCleanup.NoMarksFound` passes the file on with the warning, and the raw output holds no removed mark bytes.
- [ ] Require that headers, footers and Bates stamps written by `apply_headers_footers_and_bates` (`D05 T01 §6`) are managed marks that `list_marks` returns with kind `header_footer`; until that engine change exists the step refuses `header_footer` in `kinds` with the reason, and `ActionCleanup.HeaderFooterRemovable` stays red. Done when: `ActionCleanup.HeaderFooterRemovable` stamps a header and a Bates number, removes both and finds the original text and pixels.
- [ ] Define the `delete_blank_pages` parameters: `sensitivity` (`low`, `medium` default, `high`, as `BlankSensitivity` of `D05 T01 §20`), `ignore_margin_percent` (default `5`), `include_nearly_blank` (default `false`), `annotations_count_as_content` (default `true`), `hidden_text_counts_as_content` (default `false`), `max_deleted_percent` (default `50`, range `1` to `100`) and `on_all_blank` (`skip_file` default or `fail`). Done when: `ActionCleanup.BlankParams` validates ranges.
- [ ] Implement `delete_blank_pages` over `detect_blank_pages`, `plan_blank_page_deletion` and `apply_page_edits` (`D05 T01 §20`, `§3`): detect on the job's own document with `BlankDetectOptions` built from the parameters, apply this precedence: first, when every page is blank, `on_all_blank` decides (`skip_file` ends `Skipped(all pages blank)`, `fail` ends `Failed(all pages blank)`; deleting all pages is invalid) and the share guard is not evaluated; otherwise fail the file when the share of deleted pages exceeds `max_deleted_percent`; otherwise delete. List the deleted 1-based page numbers in the file's warnings. Done when: `ActionCleanup.BlankPages` covers deletion, the annotation keep and the guard, and `ActionCleanup.AllBlank` runs an all-blank document under `skip_file`, under `fail`, and with the default `50` percent guard (which must not change the all-blank outcome). Cheaper substitute: deleting every page the detector flags with no guard or list.
- [ ] Register order rules (`§3`): both steps after `pdfa` or `pdfx_convert` are errors, and `delete_blank_pages` after `redact_patterns` is a warning (page numbers in the redaction report no longer match). Done when: `ActionCleanup.OrderRules` triggers each rule.
- [ ] Add `ActionCleanup.*` tests in `tests/recto-tests/action_cleanup_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionCleanup.*` passes under ASan.
- [ ] Commit: `services: add the remove_marks and delete_blank_pages action steps`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionCleanup.*` passes under ASan, and removing the `max_deleted_percent` guard fails `ActionCleanup.BlankPages`.

## 19. `insert_pdf` step: insert a PDF into many PDFs at a position

UPDF and PDFelement insert a cover sheet, a terms page or an index into every file of a batch; Acrobat Pro does it as an Action Wizard page step. The `insert_pdf` step is a thin adapter over `merge_pdfs` (`D05 T01 §2`): it splits the target into the pages before and after the insertion point and merges them around the chosen pages of the source, keeping the target's own bookmarks and not wrapping them under a new top-level entry. The source file is read once through `CheckedOpen` and staged by the `ParamStager` (`§17`), so a path swapped for a link after validation is never followed. A position beyond the end of a short file is a stated policy, not a silent guess.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the `insert_pdf` step in `src/services/actions/steps/insert_pdf_step.cpp`. Done when: `ActionInsert.Positions` inserts at the start, the end, after page `N` and before page `N` and the page order matches the expected sequence, under ASan.
2. Implement the short-file policy and bookmark rule. Done when: `ActionInsert.ShortFilePolicy` and `ActionInsert.Bookmarks` pass.

- [ ] Define the `insert_pdf` parameters: `source` (`file_path`, a PDF), `source_password` (`secret`), `source_pages` (range, default all), `position` (`start`, `end`, `after_page`, `before_page`), `page` (1-based, required for the last two), `if_page_out_of_range` (`fail` default, `append`, `skip_file`) and `keep_source_bookmarks` (`bool`, default `false`). Done when: `ActionInsert.ParamSchema` rejects `after_page` without `page` and accepts `keep_source_bookmarks` of either value.
- [ ] Build the three-part `MergeSourceSpec` list (target pages before, source pages, target pages after) and call `merge_pdfs` with the outline mode that keeps only the target's bookmarks, adding the source's bookmarks as one top-level entry named after the source file only when `keep_source_bookmarks` is `true`; this needs the outline option on `merge_pdfs` that `D05 T01 §2` does not define yet, and `ActionInsert.Bookmarks` stays red until that engine change exists. Done when: `ActionInsert.Bookmarks` runs both values of `keep_source_bookmarks`, finds the target's outline unchanged with `false` and one extra entry for the source with `true`, and finds every target bookmark destination after the insertion point remapped to its new page number.
- [ ] Apply `if_page_out_of_range` when `page` exceeds the target's page count: `fail` fails the file naming the page count, `append` inserts at the end and adds a warning, `skip_file` ends `Skipped(page out of range)`. Done when: `ActionInsert.ShortFilePolicy` covers the three policies on a `3`-page target and `after_page 5`.
- [ ] Pass a password-protected target through the existing `NeedsPassword` flow and an encrypted source through `source_password`, failing the run at start when the source cannot be opened (the source is checked once in `prepare_run`). Done when: `ActionInsert.EncryptedSource` unlocks an `AES-256` source and fails the run early for a wrong secret with zero jobs submitted.
- [ ] Register order rules (`§3`): the step after `pdfa` or `pdfx_convert` is an error, after `pdfua` is a warning (inserted pages are untagged unless the source is tagged), and after `redact_patterns` is a warning. Done when: `ActionInsert.OrderRules` triggers each rule.
- [ ] Add `ActionInsert.*` tests in `tests/recto-tests/action_insert_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionInsert.*` passes under ASan.
- [ ] Commit: `services: add the insert_pdf action step`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionInsert.*` passes under ASan, and inserting after the wrong page fails `ActionInsert.Positions`.

## 20. Aggregate terminal steps: one output for a whole run, with all-or-nothing publishing

Some batch jobs produce one result for many files, such as a single table of form values from a folder of filled forms (UPDF, PDFelement and Foxit PDF Editor all offer it; Acrobat Pro has it as a form-data export). The runner of `§9` is per file, so this section adds a second terminal kind, `TerminalAggregate`: each file's job calls the step's `contribute` and stores a bounded contribution blob in the run scratch folder, and once every file has reached a final state the runner calls `finalize` once to build and publish the single output through `OutputPublisher` (`§6`). Publication is all or nothing: a stopped run publishes nothing, and a run with failed files publishes a partial output only when the action allows it, always marked partial in the report with the excluded files and their reasons.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define the aggregate contract in `src/services/actions/action_step.h` and teach `validate_action` and the runner about it. Done when: `ActionAggregate.Contract` runs a test aggregate step over `10` files, publishes one output once, and publishes nothing when the run is stopped, under ASan.
2. Add the report model and failure policy. Done when: `ActionAggregate.PartialPolicy` and `ActionAggregate.Report` pass.

- [ ] Add `StepKind::TerminalAggregate` and `IAggregateStep` (`contribute(StepContext&) -> PdfResult<ContributionBlob>`, `finalize(AggregateContext&) -> PdfResult<AggregateResult>`, `default_extension()`) to `src/services/actions/action_step.h`; every terminal kind counts as the one terminal step of `validate_action`, and an action with an aggregate terminal may not contain `save` or `convert`. Done when: `ActionAggregate.Validation` rejects an aggregate action that also has `save`.
- [ ] Store each contribution as `AppPaths::temp_dir()/actions/<run-id>/aggregate/<index>.part` through `AtomicFileWriter`, at most `16 MiB` per contribution and `1 GiB` per run, failing the file with a typed reason when its contribution is too large and keeping contributions out of memory. Done when: `ActionAggregate.Bounds` fails an oversized contribution and a run past the total cap without unbounded allocation under ASan.
- [ ] Add the run state `Finalizing` to `ActionRunState` (`§9`): after every file is final, submit one `finalize` job on the runner of `D04 T01 §1`, with its own `CancelToken`, and move to `Finished` when it ends. `stop` during `Finalizing` cancels it and publishes nothing. Done when: `ActionAggregate.StopPublishesNothing` stops a run before and during finalization and finds no output and no leftover scratch.
- [ ] Publish the aggregate output through `OutputPublisher` (`§6`) with the template `output.aggregate_name_template` (default `{action}-{date}`, tokens `{action}`, `{date}`, `{time}`, `{count}`), so it is exclusive, never replaces a file and honours `if_exists`; add the template to the schema of `§1`. Done when: `ActionAggregate.ExclusivePublish` runs two runs into one folder and gets two distinct files.
- [ ] Add `require_all_files` (default `false`) to the action's `output`: when `true` and any file is `Failed`, `Skipped` or `Cancelled`, nothing is published and the run reports why; when `false` a partial output is published and flagged. Done when: `ActionAggregate.PartialPolicy` covers both values with one corrupt file in the set.
- [ ] Extend `ActionRunReport` and `FileReport` (`§10`) with `aggregate_outputs[]` (`path`, size, SHA-256, `partial`, `included_files`, `excluded_files` with reasons) and each file's `contributed` flag, in the JSON, CSV and HTML exports with the same escaping and formula guard. Done when: `ActionAggregate.Report` finds every input in `included_files` or `excluded_files` exactly once.
- [ ] Add `ActionAggregate.*` tests in `tests/recto-tests/action_aggregate_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionAggregate.*` passes under ASan.
- [ ] Commit: `services: add aggregate terminal steps with all-or-nothing publishing`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionAggregate.*` passes under ASan, and publishing before every file is final fails `ActionAggregate.StopPublishesNothing`.

## 21. `combine_form_data` step: form values from many PDFs into one CSV or workbook

UPDF, PDFelement and Foxit PDF Editor collect the values of many filled forms into one spreadsheet, and Acrobat Pro does it as Merge Data Files into Spreadsheet. The `combine_form_data` step is an aggregate terminal step (`§20`) that reuses the combine engine of `D03 T03 §40` rather than reading forms a second time: each file's `contribute` calls `combine_form_data` on that one file's current scratch PDF (the AcroForm reader and XFA converter of `D03 T03 §6` and `§7`, nothing in the file runs) and stores the single row, and `finalize` merges the rows in input order into one table and writes it as CSV, JSON or, through the `XlsxWriter` path of `D03 T03 §41`, an `.xlsx` workbook. Values stay text by default so a postcode `01234` keeps its zero, CSV cells that start with a formula character are neutralised, and a file without form fields is skipped with a reason instead of adding an empty row.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the step's `contribute` and `finalize` in `src/services/actions/steps/combine_form_data_step.cpp` over `combine_form_data` (`D03 T03 §40`). Done when: `ActionFormData.Combine` combines `5` filled corpus forms with different field sets into one CSV whose header is the union of field names, and the CSV equals the output of `recto-cli forms combine` on the same files, under ASan.
2. Implement the `.xlsx` and JSON outputs, value rules and injection guard. Done when: `ActionFormData.Xlsx` and `ActionFormData.CsvGuard` pass.

- [ ] Define the parameters: `format` (`csv` default, `json`, `xlsx`), `column_match` (`name` default or `position`, as in `D03 T03 §40`), `source_column` (default `true`), `utf8_bom` (default `true` for CSV so Excel opens non-ASCII text correctly) and `raw` (default `false`, which keeps the formula-character neutralising of `D03 T03 §40`). Done when: `ActionFormData.ParamSchema` validates ranges and enums.
- [ ] Implement `contribute` by calling `combine_form_data` with the file's scratch PDF and the run's `CombineOptions`, storing its one row and column list as the contribution blob (at most `1 MiB`), and mapping its `skipped` reasons to `Skipped(no form fields)`, `NeedsPassword` (through the existing flow of `D04 T01 §7`) or `Failed` for a corrupt file. Done when: `ActionFormData.FieldKinds` covers each field type on a fixture and `ActionFormData.NoFields` skips a flat PDF.
- [ ] Implement `finalize`: merge the contributions in input order into one `CombinedFormData` (columns as the union in first-seen order or by position, missing values empty), refuse a table over the caps of `D03 T03 §40` (`2000` columns, `500000` cells) with a typed error, and write CSV (with the BOM option), JSON or `.xlsx` into the temporary aggregate file. Done when: `ActionFormData.Combine` finds one row per included file in input order and `ActionFormData.Caps` refuses an oversized run.
- [ ] Neutralise CSV formula injection with the writer of `D03 T03 §40` and test it again at the step boundary. Done when: `ActionFormData.CsvGuard` round-trips a hostile field value and finds it inert.
- [ ] Write `.xlsx` through the `XlsxWriter` path of `D03 T03 §41` with every value as a shared string, so leading zeros survive and no cell is ever a formula. Done when: `ActionFormData.Xlsx` reads the workbook back, finds `01234` as text and finds no `<f>` element.
- [ ] Register the step with its schema and summary. Done when: `ActionSteps.Registry` lists `combine_form_data` and `validate_action` accepts a two-step example, and rejects a step after the aggregate terminal.
- [ ] Add `ActionFormData.*` tests in `tests/recto-tests/action_form_data_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionFormData.*` passes under ASan.
- [ ] Commit: `services: add the combine_form_data aggregate action step`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionFormData.*` passes under ASan, and writing form values as numbers fails the leading-zero check in `ActionFormData.Xlsx`.

## 22. `print` terminal step: batch print to a named printer

UPDF and PDFelement print batches from a saved action, and Acrobat Pro can end an Action Wizard sequence by printing. The `print` step ends an action by sending each file's last scratch PDF to a named printer through the print pipeline of `D07 T01 §2`, so a folder of invoices prints without opening each one. It produces no output file and it cannot be undone, so it is built to be explicit: the printer must be named in the action (there is no silent default printer), the printer must exist when the run starts, only the settings the action sets are sent in the print ticket, documents that forbid printing fail instead of printing, and `stop` cancels what has not yet reached the spooler while reporting what has. The print pipeline lives in `src/app/printing/`, so the step reaches it through an `IBatchPrinter` interface; the app supplies an adapter and the console CLI does not (`§23`).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `IBatchPrinter` and the `print` step with a test double. Done when: `ActionPrint.StepWithDouble` prints three files to the double with the expected ticket settings and publishes no file, under ASan.
2. Implement the app adapter over `PrintService`. Done when: `ActionPrint.AdapterToPdfPrinter` prints a fixture to `Microsoft Print to PDF` through the adapter and the output matches the source pages at `SSIM >= 0.98`.

- [ ] Define `IBatchPrinter` (`list_printers()`, `has_printer(name)`, `print(PrintRequest, CancelToken) -> PdfResult<PrintReceipt>`), `PrintRequest` (`pdf_path`, `printer_name`, and `std::optional` overrides `copies`, `page_range_spec`, `orientation`, `scale`, `duplex`, `colour`, `paper`, `include_annotations`, where an unset optional means the setting is absent from the print ticket and an explicit value equal to the driver default is still sent) and `PrintReceipt` (`pages_sent`, `handed_to_spooler`) in `src/services/actions/batch_printer.h`, plus a recording double for tests. Done when: the header compiles in `recto-services` and `recto-tests`, and `ActionPrint.OptionalSemantics` shows an unset `duplex` leaves the ticket without a duplex entry while an explicit `one-sided` adds one, and a `paper` override reaches the adapter.
- [ ] Add `StepKind::TerminalPrint` to `src/services/actions/action_step.h` as a terminal kind (it counts as the one terminal step, and an action may not combine it with `save`, `convert` or an aggregate step), and add `print` to the Run panel's "no output file" handling so the report shows `printed to <printer>, <n> pages` instead of an output path. Done when: `ActionPrint.Validation` rejects `print` plus `save`.
- [ ] Make `print` a two-phase terminal step: phase one runs every file's earlier steps to its final scratch PDF and computes an authoritative `PrintPlan` (printer, files, and for each final scratch PDF its page count, SHA-256, the pages selected by `page_range` and the copies); the runner then enters the new state `AwaitingConfirmation` (`§9` gains this state) and submits nothing to the spooler until `confirm_print(plan_id)` is called; phase two prints the files, re-checking each file's final page count and SHA-256 against the plan immediately before it is handed over and failing that file on a mismatch. A run without confirmation (the CLI) never reaches phase two. Done when: `ActionPrint.TwoPhase` runs `insert_pdf -> print` on a one-page input that grows to many pages and finds the plan counts the final pages, no spool call happens before `confirm_print`, and a scratch file altered after confirmation fails with no print.
- [ ] Define the `print` parameters: `printer` (required exact name, offered by a `provided{printers}` list in the editor), `copies` (`1` to `99`), `page_range` (range, default all), `orientation` (`auto` default, `portrait`, `landscape`), `scale` (`fit` default, `actual`), `duplex`, `colour` and `paper` (each defaulting to `driver default`, which sends nothing in the ticket), `include_annotations` (default `true`) and `max_pages_per_run` (default `500`). Done when: `ActionPrint.ParamSchema` rejects a missing printer and `copies` of `0`.
- [ ] In `prepare_run`, check `has_printer(name)` and fail the whole run before any file is submitted when the printer is not installed, naming the printer. Done when: `ActionPrint.PrinterMissing` refuses a run with zero jobs submitted.
- [ ] Implement `run` over `IBatchPrinter::print`, sending only the ticket settings the action changed (`D07 T01 §2`), failing the file with the driver's error chain when the spooler or driver fails and continuing with the next file, and failing a document whose permissions forbid printing with the stated reason and no job sent. Done when: `ActionPrint.TicketSendsOnlyChanged` finds no override for untouched settings and `ActionPrint.PrintForbidden` fails a restricted fixture with no job.
- [ ] Implement `stop`: trip the `CancelToken` of in-flight prints, mark unstarted files `Cancelled`, and list files already handed to the spooler as `printed (handed to spooler)` in the report, because Recto does not recall spooler jobs. Done when: `ActionPrint.StopReportsSpooled` stops a `10`-file run after `4` files and finds `4` handed over and `6` cancelled.
- [ ] Add the adapter `src/app/printing/batch_printer_adapter.cpp` over `PrintService` (`D07 T01 §2`) and the headless print worker that `/pt <file> <printer>` already uses (`D07 T01 §9`), so no dialog is shown; the adapter needs a printer name and the changed ticket settings on `PrintJobOptions`, a small extension of `D07 T01 §2` that is listed as a cross-reference request. It opens the document on a print worker, never on the UI thread. Done when: `ActionPrint.AdapterToPdfPrinter` passes on the display-session host.
- [ ] Register the order rules (`§3`): `print` must be the last step, `encrypt` before `print` is a warning (the printed copy needs the password), and any content step after `print` is impossible by the terminal rule. Done when: `ActionPrint.OrderRules` triggers each rule.
- [ ] Add `ActionPrint.*` tests in `tests/recto-tests/action_print_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ActionPrint.*` passes under ASan.
- [ ] Commit: `services: add the print terminal action step over an IBatchPrinter seam`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionPrint.*` passes under ASan, and falling back to the default printer when the named one is missing fails `ActionPrint.PrinterMissing`.

## 23. Print step safeguards: run confirmation, page budget and CLI refusal

Printing cannot be recalled, so the places that start an action with a `print` step must make the cost visible before anything is sent. This section adds the confirmation to the Run panel (`§13`), the page budget, and the refusal in `recto-cli action run`. It matches the care other batch tools take with irreversible steps and keeps Recto's rule that a command never acts on something the user has not seen: no print job leaves the app without a confirmation that names the printer and the number of pages.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface: no SPEC screenshot shows it. It follows `design/SPEC.md` section 9.9 (`ContentDialog` chrome and the completion notice) and the Run panel of `§13`. Design board: `design/SPEC.md` section 9.15 with `design/screenshots/D-Actions.png` (drawn by `D06 T04 §20`).
**Job:** A person who starts an action that ends in printing sees the printer, the number of files and the total number of pages including copies, must confirm, and can stop the run; a script is told plainly that printing is not available from the command line.
**Treatment:** A `ContentDialog` `"Print 240 pages (60 files x 1 copy x 4 pages) on <printer>? This cannot be undone."` with `Print` and `Cancel`, `Cancel` as the default button, shown after the Run panel's input check and before the first job, plus a Start-disabled reason when the page budget is exceeded. Cheaper substitute that fails the checkpoint: starting the print as soon as `Start` is pressed, or a confirmation that omits the page count.
**Chrome:** consume the Run panel and `ActionRunViewModel` (`§13`), the 9.9 dialog host (`D02 T01 §10`), `AnnouncementService` (`D03 T02 §5`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens with Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Expose the runner's authoritative `PrintPlan` (`§22`) and its budget in `ActionRunViewModel`, and refuse printing in the CLI. Done when: `ActionPrintUi.Plan` returns the page total including copies computed from the final scratch PDFs and `Cli.ActionPrintRefused` exits `1` with the reason.
2. Build the confirmation dialog and wire all start paths. Done when: `AppTests.ActionPrintConfirm` shows the dialog with the right numbers from the Run panel and from a `Ctrl+K` saved-action command, and cancelling submits no job.

- [ ] Expose the `PrintPlan` that the runner produced in the `AwaitingConfirmation` state (`§22`) in `src/services/action_run_view_model.cpp` as `printer`, `files`, `pages_total` (page counts of the final scratch PDFs after every earlier step, with `page_range` and `copies` applied) and whether the total exceeds `max_pages_per_run`; never estimate from the original inputs. Done when: `ActionPrintUi.Plan` matches a known set of page counts, ranges and copies, including `insert_pdf -> print` and `delete_blank_pages -> print` runs where the final count differs from the input count.
- [ ] When the authoritative plan exceeds `max_pages_per_run`, do not offer `Print`: show `"This run would print N pages; the action allows M. Raise the limit in the action."` with only `Close`, end the run `Cancelled` with its scratch removed and nothing sent. Done when: `AppTests.ActionPrintConfirm` shows the over-budget dialog, and after raising the limit and running again `Print` is offered.
- [ ] Build the confirmation `ContentDialog` in `src/app/views/PrintRunConfirm.xaml/.cpp` with `Cancel` as the default, an automation name and tooltip on every control, keyboard operation, and a polite announcement of the outcome. Done when: `A11yInventory` (`D03 T02 §3`) passes on the dialog and cancelling ends the run `Cancelled`, removes the scratch files and leaves zero spool submissions. Cheaper substitute: a toast with an undo, which cannot recall a spooled job.
- [ ] Route every start path through the same confirmation: the Run panel `Start`, the `Retry failed` action of the report view, and the per-action `Ctrl+K` command of `§12`. Done when: `AppTests.ActionPrintConfirm` triggers each path and sees the dialog each time.
- [ ] Refuse an action with a `print` step in `recto-cli action run` with exit `1` and the message `"Printing is available from the Recto app only"`, and make `recto-cli action validate` report it as an error for the CLI context. Done when: `Cli.ActionPrintRefused` verifies both and that no job is submitted. The cost of changing this default (moving the print pipeline into `recto-services`) is recorded in `D07 T01 §2`'s request.
- [ ] Add `ActionPrintUi.*`, `Cli.ActionPrintRefused` and `AppTests.ActionPrintConfirm` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=ActionPrintUi.*:Cli.ActionPrintRefused:AppTests.ActionPrintConfirm` passes on the display-session host.
- [ ] Commit: `app: confirm print runs with printer and page count, budget the pages and refuse in the CLI`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionPrintUi.*:Cli.ActionPrintRefused:AppTests.ActionPrintConfirm` passes on the display-session host, and making `Print` the default button fails `AppTests.ActionPrintConfirm`.

## 24. Gates for the new steps: no overwrite, fail closed, honest reports

Prove the properties of the steps added in `§15` to `§21` the same way `§14` proves the first set: nothing is overwritten, hostile parameters are refused, an aggregate publishes all or nothing, and no step reports a better outcome than the file earned. The gates test behaviour owned by earlier sections; a failing gate is fixed in its owner, never by loosening the gate. The print step has its own gates in `§22` and `§23`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `tests/recto-tests/action_gates2_tests.cpp` with the gates below. Done when: `recto-tests.exe --gtest_filter=ActionGates2.*` runs every gate.
2. Extend the end-to-end built-in gate of `§14` to every built-in action. Done when: `ActionGates.BuiltinsEndToEnd` runs `scan-to-accessible` through the CLI and the runner with identical outputs.

- [ ] Gate `ActionGates2.NewStepsNeverOverwrite`: run an action using `crop`, `background`, `insert_pdf`, `remove_marks` and `greyscale` twice into the same folder and into the input folder, and assert every earlier file is byte-identical and every new output has a distinct name. Done when: the gate passes and a planted replace in the publisher fails it.
- [ ] Gate `ActionGates2.ParamFilesLocalOnly`: run `background`, `insert_pdf` and `greyscale` with an image, source and profile path that is a UNC path, a junction, or a local symbolic link swapped to an SMB share between validation and use, with a network trap installed, and assert each is refused before any access to the remote target and that only staged bytes reach the engines. Done when: the gate passes and reading a parameter by path instead of the staged handle fails it.
- [ ] Gate `ActionGates2.AggregateAllOrNothing`: stop a `combine_form_data` run before finalization, during finalization and after a failed file with `require_all_files` on, and assert no output file and no scratch remain; with it off assert the output is flagged partial and lists the excluded file. Done when: the gate passes.
- [ ] Gate `ActionGates2.FormValuesInert`: combine forms whose field values are `=cmd|' /C calc'!A0`, `+1+1`, `@SUM(1)`, a very long string and markup, as CSV and as `.xlsx`, and assert the CSV cells are prefixed and the workbook holds only shared strings. Done when: the gate passes.
- [ ] Gate `ActionGates2.GreyscaleLeavesNoColour`: run `greyscale` over every colour fixture of `tests/corpus/print/` and assert the `colour.only_gray` check passes on each `Succeeded` output and that every file with a remaining coloured object is `Failed` or `SucceededWithWarnings`, never plain `Succeeded`. Done when: the gate passes and ignoring `unconverted` fails it.
- [ ] Gate `ActionGates2.BlankPageGuard`: run `delete_blank_pages` over a scanned book fixture where `70` percent of pages look blank to the detector and assert the file fails with the guard reason and the input is unchanged. Done when: the gate passes.
- [ ] Gate `ActionGates2.AccessibleBuiltin`: run `scan-to-accessible` over the scanned corpus and assert every `Succeeded` output passes `validate_pdfua`, has searchable text, and is `SucceededWithWarnings` when it holds placeholder Alt text. Done when: the gate passes and counting placeholder Alt text as a pass fails it.
- [ ] Change the two mentions of "all four built-in actions" in `§14` (its build order step 2 and the `ActionGates.BuiltinsEndToEnd` gate) to "every built-in action" so the gate covers `scan-to-accessible` and later built-ins. Done when: `ActionGates.BuiltinsEndToEnd` iterates `builtin_action_ids` (`§2`).
- [ ] Commit: `test(quality): prove the new action steps never overwrite, fail closed and report honestly`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ActionGates2.*:ActionGates.BuiltinsEndToEnd` passes under ASan, and replacing the exclusive publish with a replace makes `ActionGates2.NewStepsNeverOverwrite` fail.

## Verification

- `recto-cli action run` over a corpus folder with each built-in action finishes with exit `0`, every output is a new uniquely named file, and every input hash is unchanged (`§6`, `§9`, `§14`).
- Two runs into the same folder, a run into the input folder and two same-named inputs from different folders never replace a file (`§6`, `§14`).
- A redaction step fails closed on a page it cannot search (the explicit skip option ends `SucceededWithWarnings` with the pages listed), and an `Incomplete` or `Failed` preflight fails the file (`§5`, `§14`).
- A pause appears only for an action with a pause point and holds no worker while waiting (`§9`, `§13`, `§14`).
- The run report lists every input exactly once and exports as JSON, CSV and HTML without injection (`§10`).
- Every control in the Actions view and Run panel carries an automation name and tooltip (`D03 T02 §3`), and every action is reachable from `Ctrl+K`.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
