---
schema_version: 1
id: print-production
domain: 04-convert
status: active
title: "TODO-06 -- Print Production: Preflight, PDF/X, Output Preview and Separations"
track: M5
---

# TODO-06 -- Print Production: Preflight, PDF/X, Output Preview and Separations

> **Goal:** Recto matches Acrobat Pro's Print Production tools without weakening anything Recto already guarantees: a preflight engine driven by JSON profiles (built-in `PDF/X-1a`, `PDF/X-3`, `PDF/X-4` and common-check profiles for resolution, fonts, colour spaces, total area coverage, hairlines and transparency), fixups that write a new file with a before and after report, `PDF/X` validation and conversion with output intents and trim and bleed boxes, Output Preview with a separations list, per-plate show and hide, total area coverage under the pointer, overprint simulation and ink aliases, the Preflight dialog and Output Preview panel, and `recto-cli preflight`. Every verdict is honest: a check that could not analyse some content says `Incomplete`, never `Passed`.

> **Current state:** Measured 2026-10-21. No preflight, `PDF/X`, separations or ICC conversion code exists. `src/pdf` holds the engine scaffold only. The pieces this file reuses are specified but not built: the content interpreter and colour spaces (`D01 T02 §3`, `§4`), the `PDF/A` validator and converter with font embedding, output intents and transparency flattening (`D04 T05 §5`, `§6`), `AtomicFileWriter` and `PdfWriter` (`D04 T02 §1`, `§2`), `PdfOptimizer` image downsampling (`D04 T02 §11`), and the job runtime and queue (`D04 T01 §1`, `§5`). Foundation plan D18 (2026-10-21) requires Acrobat Pro parity tool by tool.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` decisions D3, D9, D12, D16, D17, D18, sections 4 (actor, `DisplayList` and job isolation rules) and 9 (hostile input).
- `AGENTS.md` architecture rules: the open viewer document is never modified in place, jobs open their own `recto::pdf::Document`, every write is temp-file-then-replace, colours come only from `design/tokens/RectoColors.xaml`, every command is reachable from `Ctrl+K`.
- Verifier lessons: a verifier that splits content on whitespace or reads only device colour operators reports a compliant file that is not, and page operations that drop `/OutputIntents` and the `PDF/X` identification break a converted file.
- `design/SPEC.md` sections 8.2 (contextual bars and the `more` menu), 8.3 (side panels), 9.5 (queue card), 9.9 (dialog chrome), 11 (accessibility).
- ISO 15930 (`PDF/X`: part 1 `PDF/X-1a:2001`, part 3 `PDF/X-3:2002`, part 7 `PDF/X-4:2010`), ISO 32000-2 (colour spaces, transparency, overprint control, page boundaries, output intents) and ICC.1 (profile format). The ISO texts are paywalled: each rule in a built-in profile records the clause number the implementer read in its `source` field.
- `D04 T05 §5` and `§6` (`ComplianceEngine`: finding model, font embedding, output intent injection, transparency flattening), `D04 T02 §1`, `§2`, `§11`, `D04 T01 §1`, `§4`, `§5`, `D01 T02 §3`, `§4`, `§6`, `§10`, `D06 T03 §2`, `§4`.
- Cross-file cross-references to this file are added by the coordinator, in both directions.
- -> XREF: D01 T02 §4 -- an optional `InkObserver` on this interpreter feeds preflight without changing `DisplayList` output.
- -> XREF: D04 T05 §5 -- preflight reuses the compliance finding model and validator.
- -> XREF: D06 T03 §4 -- the ink hook is proven not to change any consumer's operator trace.
- -> XREF: D05 T01 §2 -- merge and split keep `/OutputIntents` and PDF/X identity.
- -> XREF: D04 T05 §22 -- ICC conversion for contrast.
- -> XREF: D04 T05 §23 -- inventory method.
- -> XREF: D04 T05 §26 -- shared exit codes.
- -> XREF: D07 T01 §12 -- print greyscale engine.
- -> XREF: D04 T07 §16 -- greyscale step.
- -> XREF: D06 T04 §19 -- the `D-Preflight.png` board and `design/SPEC.md` section 9.14 that §13, §14, §19 here build against.

## Outcome

- `recto-cli preflight run tests/corpus/print/pdfx-valid-4.pdf --profile pdfx-4` exits `0` with verdict `Passed`, and the same command on every `tests/corpus/print/` fault fixture reports the planted fault by rule id, page and object, never `Passed` (`§4`, `§5`, `§6`).
- A page whose content could not be fully analysed (form nesting past the cap, an undecodable image) makes every content rule `Incomplete` and the verdict `Incomplete`, so the report never claims compliance for content it did not read (`§5`, `§6`).
- Preflight fixups and `PDF/X` conversion write a new file through `AtomicFileWriter`, re-run the profile on the result, and stamp `PDF/X` identification only on a file that passes; the open document and the original are never modified (`§8`, `§9`, `§10`).
- Output Preview shows each plate on its own, hides and shows plates, simulates overprint, and reads total area coverage (TAC) under the pointer from the rendered ink planes, with a visible `Approximate` list for anything it cannot draw exactly (`§11`, `§12`, `§14`).

**Adjacency:** list=applicable @ D04 T06 §13; document=applicable @ D04 T06 §6; settings=applicable @ D02 T04 §2; reporting=applicable @ D04 T06 §6; notifications=applicable @ D04 T01 §6; permissions=applicable @ D04 T06 §8; audit=applicable @ D04 T06 §6; exchange=applicable @ D04 T06 §2; reverse=applicable @ D04 T06 §8

**Adjacency rationale:** The profile library (list) lives in the Preflight dialog (`§13`), and profiles import and export as JSON (`§2`, exchange). The preflight report exported as JSON, HTML or text is the document a print buyer files and the audit record of what was checked, with which profile version and what was left unanalysed (`§6`). Last-used profile and similar remembered choices go through the typed `SettingsStore` (`D02 T04 §2`) with the dialog as consumer; there is no Settings page group because profile tunables live in the profile. Completion of a preflight or fixup job uses the existing notices (`D04 T01 §6`). Fixups refuse a file whose permissions forbid modification, and a refusal is tested both ways (`§8`). Every fixup writes a new file and leaves the original untouched, which is the reverse of the operation (`§8`).

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Print production fixtures, synthetic ICC profiles and provenance rows | D00 T03 §3, D00 T03 §5 | [ ] |
| 2 | §2 | Preflight profile JSON schema, rule kinds and built-in profiles | D04 T05 §5, §1 | [ ] |
| 3 | §3 | Ink-aware paint events from the one shared content interpreter | §1, D01 T02 §4, D06 T03 §4 | [ ] |
| 4 | §4 | Document-level preflight checks: boxes, intents, PDF/X identity, fonts, security | §1, §2, D04 T05 §5 | [ ] |
| 5 | §5 | Content preflight checks and the coverage ledger: colour graph, TAC, hairlines | §1, §2, §3, D01 T01 §8, D06 T03 §2 | [ ] |
| 6 | §6 | Profile evaluation, verdict and report export (JSON, HTML, text) | §4, §5, D04 T02 §1 | [ ] |
| 7 | §7 | ICC colour transform engine over Windows Colour System | §1, D01 T02 §3 | [ ] |
| 8 | §8 | Fixup framework with before and after report, and structural fixups | §6, D04 T02 §2, D04 T05 §6 | [ ] |
| 9 | §9 | Content fixups: ICC colour conversion, hairlines, flattening and resolution | §7, §8, D04 T02 §11, D03 T01 §9 | [ ] |
| 10 | §10 | PDF/X conversion for X-1a, X-3 and X-4 with output intents | §8, §9 | [ ] |
| 11 | §11 | Output Preview session, ink inventory and ink aliases | §3, §5, D01 T02 §10 | [ ] |
| 12 | §12 | Plate renderer: separations, overprint simulation and total area coverage | §7, §11, D01 T02 §6 | [ ] |
| 13 | §13 | Preflight dialog, report view and profile management | §6, §10, D04 T01 §5, D02 T01 §10, D02 T03 §11, D06 T04 §19 | [ ] |
| 14 | §14 | Output Preview panel with plate toggles and pointer ink readout | §9, §12, D02 T03 §3, D02 T02 §4, D03 T02 §5, D06 T04 §19 | [ ] |
| 15 | §15 | `recto-cli preflight` command family | §6, §10, §12, D01 T03 §1 | [ ] |
| 16 | §16 | Print production honesty gates and fuzz targets | §10, §12, §15, D01 T03 §4 | [ ] |
| 17 | §17 | PDF/E and PDF/VT identification and preflight profiles | §2, §4, §5, §6, §13, §15 | [ ] |
| 18 | §18 | Whole-document greyscale conversion fixup and the `colour.only_gray` check | §3, §5, §7, §8, §9, D03 T03 §2, D03 T03 §6 | [ ] |
| 19 | §19 | Greyscale commands: Preflight dialog, `Ctrl+K` and `recto-cli preflight greyscale` | §13, §15, §18, D02 T03 §11, D06 T04 §19 | [ ] |

## 1. Print production fixtures, synthetic ICC profiles and provenance rows

Every check in this file needs files whose truth is known by construction, not by what Recto reports. This section extends `scripts/build-corpus.py` with a `print` group that builds deterministic fixtures into `tests/corpus/print/` and writes beside each one an `.expect.json` sidecar listing the facts the generator planted (inks used, TAC maxima, hairline widths, image ppi, box values, missing fonts). Sidecars are emitted while the file is built and are never read back from Recto, so a test compares Recto against the generator and cannot agree with itself. The `PDF/X` valid fixtures are built from the key lists in ISO 15930 because no free independent `PDF/X` validator exists; that is a known weakness recorded in `tests/corpus/PROVENANCE.md`, and an externally validated sample from a commercial preflight tool is a future owner-supplied addition, not a blocker. Synthetic ICC profiles come from a documented closed-form ink model so expected colour values can be computed in Python without any ICC engine.

**Phase:** 5
**Needs:** none
**Surface:** no

**Build order:**

1. Add the `print` group to `scripts/build-corpus.py` and generate the colour, line, image, transparency, overprint and structure fixtures with their sidecars. Done when: `python scripts/build-corpus.py --group print` run twice yields byte-identical files (equal SHA-256) and every fixture has a sidecar.
2. Generate the synthetic ICC profiles and the hostile ICC set, and add every file to `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=Corpus.Provenance` passes with the new rows and fails when one fixture byte is changed.

- [ ] Add `tests/corpus/print/cmyk-in-form-xobject.pdf`: a `DeviceCMYK` logo drawn inside a Form XObject nested three deep, content written with comments between operands, CR-only line ends, and tokens split across two `/Contents` streams, plus a variant whose page sets `/DefaultRGB`. Done when: its sidecar lists `DeviceCMYK` as used on page 1 at the nested object ids.
- [ ] Add `tests/corpus/print/spot-and-indexed.pdf` with a `Separation` ink named `PANTONE Test 1`, a two-colorant `DeviceN` with one process and one spot component, an `Indexed` image over `DeviceCMYK`, and an `ICCBased` `N=4` fill. Done when: the sidecar lists the spot names, the `Indexed` base space and the `ICCBased` component count.
- [ ] Add `tests/corpus/print/hairlines.pdf` with strokes of `0 w`, `0.05 w`, `0.25 w` and `0.5 w`, one `0.5 w` stroke under a `0.1 0 0 0.1 0 0 cm` scale, a horizontal and a vertical `0.5 w` line under the anisotropic `10 0 0 0.1 0 0 cm` (thickness `0.05` pt and `5` pt), two `0.5 w` lines under the shear `1 0 3 1 0 0 cm`, one along the user `y` axis (device thickness `0.5 / sqrt(10) = 0.158` pt) and one along the user `x` axis (`0.5` pt), one width set only through an `/ExtGState` `/LW`, and one stroke inside a tiling pattern. Done when: the sidecar lists each stroke's device-space width in points.
- [ ] Add `tests/corpus/print/tac-patches.pdf` with vector patches at known `C M Y K` values (`100 100 100 100`, `85 75 68 90`, `60 40 40 100`, `0 0 0 100`), an axial shading running from `0 0 0 0` to `100 100 100 100`, a `FunctionType 2` shading with `C0 = [0 2 0 0]`, `C1 = [2 0 0 0]`, `N = 1` and `/Range [0 1 0 1 0 1 0 1]` (both ends read `100` percent after clipping, the middle reads `200`), a shading whose function has `/Domain [0 4]`, `N = 1`, `C0 = [0 1 0 0]`, `C1 = [1 0.75 0 0]` and `/Range [0 1 0 1 0 1 0 1]` (TAC `100` percent at both domain ends and `175` percent at input `1`), a sampled `FunctionType 0` shading with `/Order 3`, and a `DeviceCMYK` image whose pixel values are generated from a formula. Done when: the sidecar lists each patch's TAC, each shading's true maximum and the image's maximum TAC with the count of pixels over `300`.
- [ ] Add `tests/corpus/print/transparency-and-overprint.pdf`: `/ca 0.5` fill, `/BM /Multiply`, an `/SMask` image, a page `/Group /S /Transparency`, `K 100` text with `/op true /OPM 1` over `C 100`, the same with overprint off, a white object with overprint on, and a spot fill with overprint on. Done when: the sidecar lists each object's expected value in each plate.
- [ ] Add `tests/corpus/print/image-resolutions.pdf` placing the same image at `72`, `150`, `300` and `1200` effective ppi, one rotated by `30` degrees and one placed through a nested `cm`. Done when: the sidecar lists each placement's effective ppi to two decimals.
- [ ] Add valid `tests/corpus/print/pdfx-valid-1a.pdf`, `pdfx-valid-3.pdf` and `pdfx-valid-4.pdf` (output intent with an embedded ICC profile, `TrimBox`, `BleedBox`, embedded fonts, identification keys, `/Trapped`) and one broken sibling per rule in `§2`'s built-in profiles (missing intent, missing `TrimBox`, `TrimBox` and `ArtBox` both present, `BleedBox` outside `MediaBox`, unembedded font, `DeviceRGB` in `X-1a`, transparency in `X-1a`, encryption, `/JavaScript` action). Done when: the sidecar of each broken file names exactly one planted rule id.
- [ ] Add `tests/corpus/print/deep-forms.pdf` (Form XObject nesting `40`, over the cap of `32` in `D01 T02 §4`) and `tests/corpus/print/undecodable-image.pdf` (a `CMYK` image with a filter chain Recto reports as unsupported). Done when: the sidecars mark both as `expect_incomplete`.
- [ ] Generate `tests/corpus/print/profiles/test-cmyk.icc` and `test-srgb.icc` (ICC v2, device class `prtr` and `mntr`, `A2B0` and `B2A0` as `mft1` tables of nine grid points per channel for the CMYK profile) from the closed-form model documented in `scripts/build-corpus.py`, and emit a Python function that converts a `CMYK` and an `RGB` value without an ICC engine for use as the test oracle. Done when: `PrintFixtures.IccOracle` computes the model for white, `0 0 0 100` and `100 0 0 0` and matches the table lookup in the generated profile within `DeltaE76 <= 2.0`.
- [ ] Generate `tests/corpus/broken/print/` hostile ICC files: truncated header, tag table pointing past the end, tag size overflow, a `mft1` claiming `255` grid points on `4` inputs, an in-bounds `mft1` with a plausible grid but a payload too short for it, an `mft2` with oversized table entry counts, `profile size` field not equal to the file length, and a non-`acsp` signature. Done when: each file is listed in `PROVENANCE.md` with the fault it plants.
- [ ] Add `PrintFixtures.Deterministic` and `PrintFixtures.SidecarsComplete` in `tests/recto-tests/print_fixtures_tests.cpp`. Done when: the tests pass, and deleting a sidecar or editing one fixture byte makes them fail.
- [ ] Commit: `corpus: add print production fixtures, synthetic ICC profiles and sidecars`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PrintFixtures.*:Corpus.Provenance` passes, and changing one byte of `tests/corpus/print/hairlines.pdf` makes `Corpus.Provenance` fail.

## 2. Preflight profile JSON schema, rule kinds and built-in profiles

A profile is a rule set stored as JSON, so print shops can share and review them and Recto never executes anything from a profile. This section defines the schema, the closed list of rule kinds the engine implements, loading with strict validation, and the built-in profiles. It also owns the strict bounded JSON reader and the JSON Schema subset validator that other JSON-driven features reuse (`D04 T07 §1`). It does not evaluate anything (that is `§6`). It matches Acrobat's preflight profiles and keeps Recto's rule that a profile is data: no scripting, no external commands, no network.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/pdf/preflight_profile.h/.cpp` (`PreflightProfile`, `PreflightRule`, `RuleSeverity`, `PdfXLevel`, `load_profile_json`, `builtin_profiles`) and `src/pdf/profiles/preflight-profile.schema.json`. Done when: `PreflightProfile.SchemaRoundTrip` loads and re-serializes each built-in profile with identical content.
2. Author the built-in profile JSON files as Windows resources and the validation of user profiles. Done when: `PreflightProfile.Validation` rejects every malformed profile in a table and accepts the built-ins.

- [ ] Implement `BoundedJson` in `src/pdf/bounded_json.h/.cpp` (strict RFC 8259 parse, duplicate keys rejected, invalid UTF-8 rejected, depth, size and string-length caps, errors carrying a JSON pointer) and a `JsonSchemaSubset` validator in `src/pdf/json_schema_subset.h/.cpp` supporting `type`, `required`, `properties`, `additionalProperties`, `enum`, `minimum`, `maximum`, `minLength`, `maxLength`, `pattern`, `items`, `minItems` and `maxItems`, with no `$ref`. Done when: `BoundedJson.Strictness` rejects duplicate keys, deep nesting and invalid UTF-8, and `JsonSchemaSubset.Keywords` accepts and rejects one document per keyword.
- [ ] Define the profile document in `src/pdf/profiles/preflight-profile.schema.json` (JSON Schema 2020-12): `schema_version`, `id`, `name`, `version`, `description`, optional `base` (a built-in id to inherit from), and `rules[]` of `{id, check, severity (error|warning|info|off), params, source, fixup}`. Done when: the schema file validates every built-in profile and rejects a profile with an unknown top-level key.
- [ ] Implement the `check` kind registry and the built-in kinds in `src/pdf/preflight_profile.cpp`. A kind is registered once through `register_check_kind(id, ParamSchema, evaluator, owner)` (the typed, range-checked `params` schema, the evaluator function and the section that owns it), so a profile may name only registered kinds and a new standard adds kinds by registration without changing the loader. The built-in kinds are: `pdf.version_max`, `file.encryption`, `actions.prohibited`, `optional_content.forbidden`, `attachments.forbidden`, `opi.forbidden`, `boxes.trim_or_art`, `boxes.nesting`, `boxes.bleed_min`, `intent.present`, `intent.profile_matches_content`, `pdfx.identification`, `pdfx.trapped`, `fonts.embedded`, `fonts.type3`, `colour.device_rgb`, `colour.device_gray`, `colour.cal_lab_icc`, `colour.spot`, `colour.indexed_base`, `ink.tac_max`, `ink.spot_count_max`, `lines.hairline_min`, `image.ppi_min`, `image.ppi_max`, `transparency.forbidden`, `overprint.white_object`. Done when: `PreflightProfile.CheckKinds` lists exactly these ids before any standard-specific registration, refuses a profile naming an unregistered kind, and `PreflightProfile.RegisterKind` accepts a test kind with a typed parameter and evaluates it.
- [ ] Bound loading: reject a profile over `1 MiB`, nesting over `8`, more than `512` rules, duplicate rule ids, a `base` that is not a built-in, and any `params` value outside its documented range, returning `PdfError` with the JSON pointer of the fault. Done when: `PreflightProfile.Validation` covers each rejection and none allocates past the bound under ASan.
- [ ] Ship the built-in profiles `pdfx-1a-2001`, `pdfx-3-2002`, `pdfx-4`, `print-ready-cmyk` and `common-checks` in `src/pdf/profiles/*.json` compiled into `src/pdf/preflight_profiles.rc`; every rule carries a non-empty `source` naming the ISO 15930 clause or the common-practice reason. Done when: `PreflightProfile.BuiltinsHaveSources` fails when any built-in rule has an empty `source`.
- [ ] Set the default parameters of the common checks as documented defaults in `common-checks`: colour and grey images `150` ppi minimum, monochrome `600` ppi minimum, maximum `2400` ppi, hairline minimum `0.25` pt, TAC maximum `300` percent, spot ink maximum `4`. Done when: `PreflightProfile.CommonDefaults` asserts these values and the profile file notes they are defaults a shop edits.
- [ ] Load and save user profiles under `AppPaths::data_dir()/preflight-profiles/*.json` with `AtomicFileWriter`, never overwriting a built-in id; implement `import_profile` and `export_profile` for sharing. Done when: `PreflightProfile.UserStore` imports, edits, exports and deletes a profile, refuses an id equal to a built-in, and leaves a failed import with no partial file. Cheaper substitute: writing the profile with `std::ofstream`, which leaves a truncated profile after a crash.
- [ ] Define `PreflightFinding` (`rule_id`, `check`, `severity`, `page_index`, `object_id`, `bbox_pt`, `measured`, `limit`, `message`, `fixup_id`, `is_auto_fixable`) and an adapter to the `ComplianceFinding` of `D04 T05 §5`. Done when: `PreflightProfile.FindingAdapter` converts a finding and back without losing `rule_id`.
- [ ] Add the libFuzzer target `fuzz_preflight_profile` in `fuzz/fuzz_preflight_profile.cpp` feeding `load_profile_json` and `BoundedJson`. Done when: a 60 second smoke run on the built-in profiles as seeds finds no crash under ASan.
- [ ] Commit: `pdf: add preflight profile schema, rule kinds and built-in print profiles`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PreflightProfile.*:BoundedJson.*:JsonSchemaSubset.*` passes under ASan, and emptying one built-in rule's `source` makes `PreflightProfile.BuiltinsHaveSources` fail.

## 3. Ink-aware paint events from the one shared content interpreter

Preflight and Output Preview must know the source colour of every painted object (its colour space, its components, its overprint state), which the `DisplayList` does not keep because it stores display colours. A verifier that finds colour by splitting content on whitespace misses a `CMYK` logo inside a Form XObject and reports it as `RGB`. Recto does not write a second parser: this section adds an optional `InkObserver` to the existing interpreter (`D01 T02 §3`, `§4`), fed from the same operator dispatch that feeds `DisplayListBuilder`. With no observer attached the interpreter behaves exactly as before, runs on the caller's thread, owns its graphics state on the stack and changes no thread-safety guarantee of `D01 T02`; the viewer never attaches one.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `InkPaintEvent`, `InkColour`, `InkObserver` and `InkLimits` in `src/pdf/ink_events.h`, and call the observer from the interpreter's paint, image, shading and text operators in `src/pdf/interpreter.cpp`. Done when: `PdfInk.Events` records, for `tests/corpus/print/cmyk-in-form-xobject.pdf`, a `DeviceCMYK` fill event at the nested object id.
2. Prove there is still one interpreter. Done when: `PdfInk.MatchesOperatorTrace` shows the observer's operator sequence equals the `operator_trace` of `D06 T03 §4` on the split-`/Contents` variants and the print fixtures, and `PdfInk.NullObserverIdentical` shows the `DisplayList` hash with no observer equals the hash before this change.

- [ ] Define `InkColour` in `src/pdf/ink_events.h`: the resolved space class (`Gray`, `Rgb`, `Cmyk`, `CalGray`, `CalRgb`, `Lab`, `IccBased{n}`, `Indexed{base}`, `Separation{name}`, `DeviceN{names}`, `Pattern`), the raw components, and the effective space after `/DefaultGray`, `/DefaultRGB` and `/DefaultCMYK` substitution. Done when: `PdfInk.DefaultSpaces` reports `DeviceRGB` content under `/DefaultRGB` as the `ICCBased` space it maps to.
- [ ] Define `InkPaintEvent` kinds `FillPath`, `StrokePath`, `Text`, `Image`, `Shading`, `FormBegin`, `FormEnd`, `PatternBegin`, `PatternEnd`, each carrying page index, object id of the content owner, device-space bounding box, line width in device space for strokes, fill and stroke `InkColour`, overprint state (`op`, `OP`, `OPM`), alpha, blend mode, soft-mask presence, and the optional-content visibility. Geometry and glyph runs reuse the `RecordedPath` and `RecordedGlyphRun` value types of `D01 T02 §1`. Done when: `PdfInk.EventFields` checks each field on the print fixtures.
- [ ] Carry image samples as `std::shared_ptr<const NativeBitmap>` (components per pixel, bits, the `/Decode` array, the colour space reference) decoded through `D01 T01 §7` and `§8`, bounded by `InkLimits::max_image_bytes` (`256 MiB`), and mark an image whose components the decoder cannot supply as `components_unavailable`. Done when: `PdfInk.ImageComponents` returns `CMYK` samples for the fixture image and `components_unavailable` for `undecodable-image.pdf`.
- [ ] Walk Form XObjects, tiling patterns, Type 3 glyph procedures and the appearance streams of annotations whose `Print` flag is set, with the depth caps of `D01 T02 §3` and `§4`, and return the typed `Truncated` outcome of `D06 T03 §2` with the cap that fired when a cap is hit. Done when: `PdfInk.CapsReportTruncated` returns `Truncated` for `deep-forms.pdf` and success one level under the cap.
- [ ] Track the overprint, alpha, blend and soft-mask state through `q`, `Q` and `gs` exactly as the interpreter's graphics state does, with no second copy of the state machine. Done when: `PdfInk.StateTracking` reads the planted values from `transparency-and-overprint.pdf`.
- [ ] Stop on `CancelToken` every `64` events and honour `InkLimits::max_events` (`50 million`), returning `Cancelled` or `Truncated`. Done when: `PdfInk.CancelAndCap` cancels a walk within `50 ms` and caps an event flood without unbounded allocation under ASan.
- [ ] Add `PdfInk.MatchesOperatorTrace` and `PdfInk.NullObserverIdentical` in `tests/recto-tests/pdf_ink_tests.cpp`, and re-run `PdfInterpreter.*` and `ThreadSafety.Determinism`. Done when: all pass under ASan, and a planted second tokenizer in the observer path makes `PdfInk.MatchesOperatorTrace` fail.
- [ ] Commit: `pdf: add InkObserver paint events to the shared interpreter`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInk.*:PdfInterpreter.*:ThreadSafety.Determinism` passes under ASan, and attaching a no-op observer leaves the `DisplayList` hash unchanged.

## 4. Document-level preflight checks: boxes, intents, PDF/X identity, fonts, security

These checks read the document structure, not page content: page boundaries, output intents, `PDF/X` identification, fonts, encryption, actions, optional content and attachments. They match the structural half of Acrobat's preflight and of the `PDF/X` standards. Each check reads parsed objects and dictionaries through `recto::pdf::Document` and the `ComplianceEngine` readers of `D04 T05 §5`, never text tokens, and each records a coverage entry so the report states what was and was not examined.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `run_document_checks(const Document&, const PreflightProfile&, CoverageLedger&)` in `src/pdf/preflight_document.cpp`. Done when: `PreflightDoc.Faults` reports each planted fault in the `pdfx-broken-*` fixtures by rule id, page and object.
2. Prove no false pass. Done when: `PreflightDoc.ValidFixturesClean` reports zero error findings on `pdfx-valid-1a.pdf`, `pdfx-valid-3.pdf` and `pdfx-valid-4.pdf` under their own profiles.

- [ ] Implement `boxes.trim_or_art`, `boxes.nesting` and `boxes.bleed_min`: for every page, resolve inherited `/MediaBox` and `/CropBox`, require `TrimBox` or `ArtBox` per the profile and never both when the profile says so, require the nesting `TrimBox` inside `BleedBox` inside `MediaBox`, and measure the bleed in points. Done when: `PreflightDoc.Boxes` flags the planted nesting and missing-box faults with the page index.
- [ ] Implement `intent.present` and `intent.profile_matches_content`: read `/Root /OutputIntents`, require `/S /GTS_PDFX`, require either an embedded `/DestOutputProfile` or an `/OutputConditionIdentifier` with `/RegistryName`, validate the ICC header (signature `acsp`, device class, colour space, `N` equal to the stream's `/N`) with the bounded parser of `§7`, and compare the intent's colour space with the colour spaces the content uses. Done when: `PreflightDoc.Intent` flags a missing intent, a profile whose `/N` disagrees with its header, and `CMYK` content under an `RGB` intent.
- [ ] Implement `pdfx.identification` and `pdfx.trapped`: for `X-1a` and `X-3` require the `/Info` keys `/GTS_PDFXVersion` and `/GTS_PDFXConformance` with the profile's version string, for `X-4` require the XMP property `pdfxid:GTS_PDFXVersion`, require `/Trapped` where the profile says so, and flag `/Info` and XMP that disagree. Done when: `PreflightDoc.Identification` flags a version string that differs between `/Info` and XMP.
- [ ] Implement `pdf.version_max` from the header and the catalog `/Version`, `file.encryption` from the trailer `/Encrypt`, `actions.prohibited` over `/OpenAction`, `/AA`, the name tree and every annotation and outline action (`/JavaScript`, `/Launch`, `/Sound`, `/Movie`, `/Rendition`), `optional_content.forbidden` from `/OCProperties`, `attachments.forbidden` from `/EmbeddedFiles` and file-attachment annotations, and `opi.forbidden` from `/OPI` keys. Done when: `PreflightDoc.Security` flags each planted item by object id and flags nothing on the valid fixtures.
- [ ] Implement `fonts.embedded` and `fonts.type3` over every font reachable from page, Form XObject, pattern and annotation appearance resources (reusing the `D04 T05 §5` font audit, exposed from `src/pdf/compliance.h` if it is private): require a non-empty decodable `/FontFile`, `/FontFile2` or `/FontFile3` for each non-Type 3 font that is used with a visible text rendering mode, and list Type 3 fonts. Done when: `PreflightDoc.Fonts` flags `/Helvetica` unembedded with the page index and font object id, and does not flag an unembedded font that is never shown.
- [ ] Record a `CoverageEntry` (`rule_id`, scope, `analysed`, reason) for every check, marking a check `analysed=false` with the reason when a needed object is unreadable (damaged xref, undecodable stream), so a damaged file cannot pass by being unreadable. Done when: `PreflightDoc.CoverageOnDamage` produces `analysed=false` entries on a damaged-xref fixture from `tests/corpus/broken/`.
- [ ] Add `PreflightDoc.*` tests in `tests/recto-tests/preflight_document_tests.cpp` driven by the fixture sidecars. Done when: `recto-tests.exe --gtest_filter=PreflightDoc.*` passes under ASan.
- [ ] Commit: `pdf: add document-level preflight checks for boxes, intents, PDF/X identity, fonts and security`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PreflightDoc.*` passes under ASan, and deleting the `/OutputIntents` check makes `PreflightDoc.Intent` fail.

## 5. Content preflight checks and the coverage ledger: colour graph, TAC, hairlines

These checks read what is painted: colour spaces, images, ink amounts, line widths, transparency and overprint. They walk the parsed operators through `§3`'s observer and the resolved colour-space graph (never content text), including colour inside Form XObjects, patterns, shadings, Type 3 glyphs and printing annotations. The acceptance bar is: a `CMYK` logo three Form XObjects deep, a spot colour and an `Indexed` image over `DeviceCMYK` are all found. A coverage ledger records every page, stream and image the walk did or did not analyse, and a content rule is only `Evaluated` when everything it needs was analysed.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `run_content_checks(Document&, const PreflightProfile&, InkObserver-driven walk, CoverageLedger&, CancelToken)` in `src/pdf/preflight_content.cpp`. Done when: `PreflightContent.Fixtures` matches every sidecar fact for the colour, TAC, hairline, image, transparency and overprint fixtures.
2. Implement the ledger rules. Done when: `PreflightContent.Incomplete` shows `deep-forms.pdf` and `undecodable-image.pdf` make the affected rules `Incomplete` with the missing pages or objects listed, never `Evaluated`.

- [ ] Build the colour-space graph resolver in `src/pdf/preflight_content.cpp`: nodes for every space in a resources dictionary, edges for `Indexed` base, `Separation` and `DeviceN` alternate, `ICCBased` alternate and `Pattern` underlying space, with cycle detection and depth cap `8`. Done when: `PreflightContent.ColourGraph` resolves the `Indexed` image over `DeviceCMYK` of `spot-and-indexed.pdf` to `Cmyk` and survives a cyclic alternate fixture.
- [ ] Evaluate `colour.device_rgb`, `colour.device_gray`, `colour.cal_lab_icc`, `colour.spot`, `colour.indexed_base` and `ink.spot_count_max` from the effective spaces of paint events and images, listing each object and page, and counting distinct spot names including those only reachable through `DeviceN`. Done when: `PreflightContent.ColourRules` finds the nested `CMYK` logo, the `Separation`, the `DeviceN` colorants and the `Indexed` base, in `cmyk-in-form-xobject.pdf` and `spot-and-indexed.pdf`.
- [ ] Compute TAC for `DeviceCMYK`, `ICCBased` with `N=4`, `Indexed` over `CMYK`, and the process components of `DeviceN`: per distinct vector colour, per shading only where the maximum is provable, and per image by scanning samples for the maximum and the share of pixels over the limit (`D01 T02 §3` function evaluator). A shading maximum is provable in exactly these cases, each evaluated on the domain the shading actually uses: `FunctionType 2` whose function `/Domain` is exactly `[0 1]`, with `N > 0` and `C0` and `C1` both inside `/Range` (each component is then monotone over `[0 1]` and never clipped, so the sum is monotone and the maximum is at an end); `FunctionType 3` where every sub-function is itself provable and its `/Encode` pair is `[0 1]` or `[1 0]`; `FunctionType 0` with `/Order 1` whose `/Encode` spans the full sample grid and whose decoded samples lie inside `/Range`, where the maximum is at a sample point. A `FunctionType 2` function with any other `/Domain`, or any `/Encode` that uses part of its domain, is not provable and falls to the sampling rule below. Every other case (clipping possible, `N <= 0`, cubic `/Order 3`, `FunctionType 4`, a radial shading whose function is unprovable) cannot establish a maximum: a `4096`-point sample is taken and a value over the limit makes the rule `Failed`, otherwise the rule is `Incomplete` with the reason in the ledger, never `Passed`. Spot inks do not count unless the profile sets `tac_includes_spots`. Done when: `PreflightContent.Tac` reports the planted patch, provable-shading and image maxima within `0.5` percentage points, flags only those over `ink.tac_max`, returns `Failed` for the clipped `FunctionType 2` fixture at a `150` percent limit and `Incomplete` for it at a `250` percent limit, returns `Incomplete` for the `/Domain [0 4]` fixture at a `250` percent limit and `Failed` at `150`, and returns `Incomplete` for the `/Order 3` shading and a `FunctionType 4` shading with a narrow high-TAC interval between samples.
- [ ] Evaluate `lines.hairline_min` from the transformed stroke geometry, not a scalar scale: for each segment (and for curves at sampled tangents) the device thickness perpendicular to the stroke is `w x abs(det(M)) / length(M d)`, where `M` is the linear part of the `CTM` and `d` the unit user-space direction, so anisotropic scaling and shear are measured per direction; `0 w` is the device minimum, and `/LW`, patterns and stroked text modes are included. Done when: `PreflightContent.Hairlines` flags `0 w`, `0.05 w`, the scaled `0.5 w` stroke, the horizontal `0.5 w` line under `10 0 0 0.1 0 0 cm` (`0.05` pt), the sheared line along the user `y` axis (`0.158` pt), the `/LW`-only stroke and the pattern stroke, and not the `0.25 w`, the plain `0.5 w`, the vertical anisotropic line (`5` pt) or the sheared line along the user `x` axis (`0.5` pt).
- [ ] Evaluate `image.ppi_min` and `image.ppi_max` from each placement's effective ppi (pixel size over placed inches, using the full `CTM` including rotation and shear), and cross-check against `min_rendered_dpi` of `D04 T02 §11`. Done when: `PreflightContent.ImagePpi` matches the sidecar ppi to `0.5` percent and agrees with `PdfOptimizer` on the same file.
- [ ] Evaluate `transparency.forbidden` (any `/ca` or `/CA` below `1`, `/BM` other than `/Normal` and `/Compatible`, any `/SMask` in a graphics state or image, any transparency `/Group` that the profile forbids) and `overprint.white_object`. Done when: `PreflightContent.Transparency` flags each planted construct and `PreflightContent.WhiteOverprint` flags the white overprint object.
- [ ] Evaluate `intent.profile_matches_content` against paint events (completing `§4`'s check with the spaces actually painted). Done when: `PreflightContent.IntentMatch` flags `DeviceRGB` painted under a `CMYK` intent and accepts `ICCBased` `RGB` under an `RGB` intent.
- [ ] Implement the `CoverageLedger` (`pages_total`, `pages_analysed`, per-page `status` of `Analysed`, `Truncated(cap)` or `Failed(reason)`, objects skipped with reason, images with `components_unavailable`) and mark each content rule `Evaluated` or `Incomplete`. Done when: `PreflightContent.Incomplete` passes and a planted early `return` that skips a page without a ledger entry makes `PreflightContent.LedgerAccountsForEveryPage` fail.
- [ ] Add `PreflightContent.*` tests in `tests/recto-tests/preflight_content_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PreflightContent.*` passes under ASan.
- [ ] Commit: `pdf: add content preflight checks over the colour graph with a coverage ledger`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PreflightContent.*` passes under ASan, and replacing the graph walk with a whitespace-split scan of content text makes `PreflightContent.ColourRules` fail on `cmyk-in-form-xobject.pdf`.

## 6. Profile evaluation, verdict and report export (JSON, HTML, text)

This section joins profile, document checks and content checks into one run with a verdict and a report a print buyer can file. It matches Acrobat's preflight results and report export, and keeps Recto's rule that a report never says more than was analysed. The verdict is `Passed`, `PassedWithWarnings`, `Failed` or `Incomplete`; `Incomplete` wins over `Passed` whenever any needed rule is `Incomplete`, and a listing cut for size says how many findings it omitted.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `PreflightEngine::run(path, password, profile, CancelToken, ProgressSink)` in `src/pdf/preflight.h/.cpp` and `PreflightJobFactory` in `src/services/preflight_job.cpp`, running as a job on `D04 T01 §1`'s runner with its own `pdf::Document`. Done when: `PreflightRun.Verdict` returns the expected verdict for every fixture.
2. Implement the report model and exporters in `src/core/preflight_report.cpp`. Done when: `PreflightReport.Exports` writes valid JSON, HTML and text for a failing and a passing run.

- [ ] Implement the verdict function: `Failed` if any evaluated rule has an error finding, else `Incomplete` if any rule the profile requires is `Incomplete`, else `PassedWithWarnings` if any warning exists, else `Passed`. Done when: `PreflightRun.Verdict` covers the four outcomes, and a run on `deep-forms.pdf` under `pdfx-4` is `Incomplete`, never `Passed`.
- [ ] Define `PreflightReport` (profile id and version, tool version, input file name, size and SHA-256, start and end time, verdict, findings grouped by rule with a total count and at most `200` listed instances each with `omitted` set when cut, the coverage ledger, per-page summary, the ink inventory used). Done when: `PreflightReport.NoSilentCut` shows a rule with `500` findings lists `200` and states `omitted = 300`.
- [ ] Open the input as a job-owned `pdf::Document` from the saved file on disk, propagate `Truncated` and `RecoveryNotes` of `D06 T03 §2` into the ledger, and report `NeedsPassword` through the shared job recovery of `D04 T01 §2`. Done when: `PreflightRun.Isolation` preflights a file that is being rendered in a `DocumentActor` with no shared state, and an encrypted fixture yields `NeedsPassword`.
- [ ] Make the report deterministic: findings sorted by page, object and rule, no thread-order dependence. Done when: `PreflightRun.Deterministic` produces byte-identical JSON (timestamps excluded) from a 1-worker and an 8-worker run.
- [ ] Export JSON (`preflight-report.schema.json`, versioned), a self-contained static HTML file (no script, inline style honouring `prefers-color-scheme`, every string escaped), and a plain text summary through `AtomicFileWriter`. Done when: `PreflightReport.Exports` validates the JSON against its schema, loads the HTML in a parser that rejects `<script>`, and a failed write leaves no partial file. Cheaper substitute: concatenating HTML strings without escaping, which lets a hostile font name inject markup.
- [ ] Record in every report the profile version and the list of rules the profile switched `off`, so a report cannot hide that a check was disabled. Done when: `PreflightReport.OffRulesListed` shows disabled rules in all three exports.
- [ ] Add `PreflightRun.*` and `PreflightReport.*` tests in `tests/recto-tests/preflight_run_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PreflightRun.*:PreflightReport.*` passes under ASan.
- [ ] Commit: `core: add preflight engine verdicts and JSON, HTML and text report export`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PreflightRun.*:PreflightReport.*` passes under ASan, and making the verdict ignore `Incomplete` rules makes `PreflightRun.Verdict` fail.

## 7. ICC colour transform engine over Windows Colour System

Colour conversion (`§9`), the `PDF/X` output intent (`§10`) and the Output Preview composite (`§12`) all need profile-to-profile conversion, which the interpreter's display-only `CMYK` to sRGB conversion (`D01 T02 §3`) does not provide. This section wraps Windows Colour System (`mscms.dll`: `OpenColorProfileW` with an in-memory profile, `CreateMultiProfileTransform`, `TranslateBitmapBits`, `TranslateColors`) behind one bounded interface. Hostile ICC bytes never reach Windows: a native validator rejects them first, and that validator is fuzzed.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `IccProfileInfo::read` (bounded header and tag table validation) and `ColourTransform` in `src/pdf/colour_transform.h/.cpp`. Done when: `PdfColour.ProfileValidation` rejects every file in `tests/corpus/broken/print/` before any WCS call and accepts the synthetic profiles.
2. Implement conversion of tuples and image rows and prove it against the Python oracle. Done when: `PdfColour.OracleAgreement` agrees with the generator oracle within `DeltaE76 <= 2.0` for a grid of sRGB and CMYK inputs.

- [ ] Implement `IccProfileInfo::read(span<const uint8_t>)` checking size `<= 16 MiB`, header length `128`, the `acsp` signature, the profile size field against the byte count, the tag table count `<= 100` and every tag offset and size inside the file with overflow-checked arithmetic, extracting device class, colour space, PCS, version and the description, and then validating the payload of every tag Recto passes to WCS: accept only the tag types `mft1`, `mft2`, `mAB `, `mBA `, `curv`, `para`, `XYZ `, `desc`, `mluc` and `text`, and for each lookup table compute input channels, output channels, grid points and entry sizes with checked arithmetic and require the computed byte length to equal the tag size, with at most `4` input channels, `17` grid points per channel and `4 MiB` of table data. A profile with any other tag type in a transform direction, or any failing payload, is rejected, so only validated structure reaches Windows. Done when: `PdfColour.ProfileValidation` accepts the synthetic profiles and rejects each hostile file, including the short-payload `mft1`, with a typed `PdfError` naming the fault.
- [ ] Implement `ColourTransform::create(source, destination, RenderingIntent, black_point_compensation)` over `OpenColorProfileW` with `PROFILE_MEMBUFFER` and `CreateMultiProfileTransform`, caching transforms in a `ProfileCache` keyed by the SHA-256 of the profile bytes. Done when: `PdfColour.CreateAndCache` creates a `CMYK` to sRGB transform once and reuses it, and `black_point_compensation` is reported as unsupported with a typed error if the Windows API cannot honour it, never silently ignored.
- [ ] Implement `convert_tuple` (float components in, float components out, via `TranslateColors`) and `convert_rows` (8-bit and 16-bit samples with a row stride, via `TranslateBitmapBits`, never assuming `stride == width * channels`). Done when: `PdfColour.StrideAndRows` converts an image with padded stride identically to the unpadded copy.
- [ ] Make a transform usable by one thread at a time and create one per worker, never sharing across threads. Done when: `PdfColour.ThreadIsolation` runs `8` threads converting the same input and gets identical output under ASan.
- [ ] Honour the document's default intent (`/ri`) as the starting rendering intent and let the profile or fixup override it. Done when: `PdfColour.RenderingIntent` shows perceptual and relative colorimetric give different results on the synthetic profile and the choice is recorded in the fixup report.
- [ ] Enumerate system profiles with `GetColorDirectoryW` and `EnumColorProfilesW` for the profile pickers; bundle none in v1 (recorded default, changing it needs a licence check for each profile). Done when: `PdfColour.SystemProfiles` lists the profiles in the Windows colour directory and tolerates an empty directory.
- [ ] Add the libFuzzer target `fuzz_icc_profile` in `fuzz/fuzz_icc_profile.cpp` feeding `IccProfileInfo::read`. Done when: a 60 second smoke run seeded with the synthetic and hostile profiles finds no crash under ASan.
- [ ] Add `PdfColour.*` tests in `tests/recto-tests/pdf_colour_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfColour.*` passes under ASan.
- [ ] Commit: `pdf: add bounded ICC colour transform engine over Windows Colour System`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfColour.*` passes under ASan, and skipping the tag-table bound check makes `PdfColour.ProfileValidation` fail on the hostile fixtures.

## 8. Fixup framework with before and after report, and structural fixups

A fixup changes the document to clear a finding. This section builds the framework and the structural fixups (page boxes, output intent, `PDF/X` identification, `/Trapped`, prohibited keys, encryption removal, font embedding). It matches Acrobat's preflight fixups and keeps Recto's rules: the fixup runs as a job on its own `pdf::Document`, writes a full new file through `PdfWriter` and `AtomicFileWriter` (never an incremental update, which would keep the old bytes, and never the open document), re-runs the profile on the result, and reports what changed and what stayed open.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `PreflightFixup`, `FixupPlan`, `FixupContext` and `run_fixups` in `src/pdf/preflight_fixups.h/.cpp` and `FixupJobFactory` in `src/services/fixup_job.cpp`. Done when: `Fixups.PlanAndRun` applies a plan to a fixture and the output re-opens.
2. Implement the structural fixups and the report. Done when: `Fixups.Structural` clears the planted box, intent, identification and security faults and `Fixups.ReportHonest` lists every fixup as `Applied`, `Skipped` or `Failed` with a reason.

- [ ] Define `PreflightFixup` (`id`, `name`, `applies_to_checks`, `run(FixupContext&) -> PdfResult<FixupOutcome>`), `FixupOutcome` (`objects_changed`, `status`, `reason`) and a registry; build `FixupPlan` from a report's `is_auto_fixable` findings, ordered structure first, then content, then fonts, then identification last. Done when: `Fixups.PlanOrder` orders a mixed plan as specified.
- [ ] Write the result as a full rewrite with `PdfWriter` (`D04 T02 §2`) into `AtomicFileWriter` (`D04 T02 §1`) using the `verify` callback to re-open the temp file and re-run the profile before the replace, naming the output `<name>-preflighted.pdf` through `resolve_output_path` (`D04 T01 §4`, `AppendNumber`). Done when: `Fixups.AtomicOutput` shows a failing verify leaves no output file, the input hash is unchanged, and the open document of a `DocumentActor` is untouched. Cheaper substitute: an incremental update on the input, which leaves the pre-fix bytes in the file.
- [ ] Refuse a file whose `/Encrypt` permissions forbid modification (opened with the user password only) with a typed `PdfError` naming the permission flag, and accept it when the owner password is supplied. Done when: `Fixups.Permissions` refuses and accepts the two cases and writes no file on refusal.
- [ ] Implement `fix.set_trim_box` (from `CropBox` or `MediaBox`, optional inset), `fix.set_bleed_box` (trim plus the requested bleed, clamped inside `MediaBox`, and when the media box leaves no room the finding stays open with the reason that bleed content cannot be invented), and `fix.remove_art_box` where the profile forbids both. Done when: `Fixups.Boxes` fixes the planted box faults and leaves the no-room case open with its reason.
- [ ] Implement `fix.add_output_intent` (embedding the chosen ICC profile bytes validated by `§7`, writing `/S /GTS_PDFX`, `/OutputConditionIdentifier`, `/RegistryName`, `/Info` text), `fix.set_trapped`, and `fix.pdfx_identification` writing the `/Info` keys and the synchronized XMP for the level. Done when: `Fixups.Intent` makes `PreflightDoc.Intent` and `PreflightDoc.Identification` clean on the broken fixtures.
- [ ] Implement `fix.strip_prohibited` (actions, `/OPI`, optional content when forbidden, attachments when forbidden) reusing `D04 T05 §6`'s stripper, and `fix.remove_encryption` that requires the password already held by the job. Done when: `Fixups.Prohibited` removes the planted items and `PreflightDoc.Security` is clean afterwards.
- [ ] Implement `fix.embed_fonts` by calling the font embedding and subsetting of `D04 T05 §6` and refusing fonts whose `OS/2 fsType` embedding rights forbid embedding, leaving those findings open with the font name. Done when: `Fixups.EmbedFonts` embeds `/Helvetica` and leaves a restricted-licence fixture font open with the reason.
- [ ] Produce the `FixupReport` (before report, after report, per-fixup outcome, residual findings, output path) and never report `Applied` for a fixup whose targeted finding is still present after the re-run. Done when: `Fixups.ReportHonest` fails when a planted no-op fixup claims `Applied`.
- [ ] Add `Fixups.*` tests in `tests/recto-tests/preflight_fixups_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Fixups.*` passes under ASan.
- [ ] Commit: `pdf: add preflight fixup framework and structural fixups with a before and after report`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Fixups.*` passes under ASan, and making `fix.add_output_intent` write the intent without the ICC stream makes `Fixups.Intent` fail.

## 9. Content fixups: ICC colour conversion, hairlines, flattening and resolution

These fixups rewrite page content: converting colours to the output condition with ICC profiles, widening hairlines, flattening transparency where the target forbids it, and fixing image resolution. They match Acrobat's preflight fixups for colour conversion, hairlines and flattening. They edit content only through the content-stream operator editor that `text_replace` uses (`D03 T01 §9`), never through a second tokenizer, so the interpreter, the observer and the rewriter agree on what an operator is.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `fix.convert_colours` in `src/pdf/preflight_fixups_content.cpp` over operators, images, shadings, patterns and Form XObjects using `§7`. Done when: `FixupsContent.ColourConversion` converts the `RGB` and `Gray` fixtures to the synthetic `CMYK` profile and `PreflightContent.ColourRules` is clean afterwards.
2. Implement hairline, flattening and resolution fixups. Done when: `FixupsContent.Hairlines`, `FixupsContent.Flatten` and `FixupsContent.Resolution` pass.

- [ ] Rewrite colour operators `g G rg RG k K cs CS sc SC scn SCN` and `ri` for every paint event whose effective space differs from the target, converting each distinct colour once with `ColourTransform::convert_tuple` and mapping pure `RGB` black `0 0 0` to `0 0 0 1` `K` only when `preserve_pure_black` is set (default on). Done when: `FixupsContent.PureBlack` keeps text black as `K 100` and `FixupsContent.ColourConversion` matches the Python oracle within `DeltaE76 <= 2.0`.
- [ ] Convert image XObjects (including `Indexed` palettes, `/SMask` untouched, `ICCBased` sources, inline images) with `ColourTransform::convert_rows`, re-encoding with the filter that preserves quality (`FlateDecode` for lossless sources, `DCTDecode` quality `>= 90` for sources that were `DCT`), and keep an image's original when conversion fails with the finding left open. Done when: `FixupsContent.Images` converts an `RGB` and a `Gray` image to `CMYK`, keeps the `SMask`, and leaves `undecodable-image.pdf` untouched with the reason.
- [ ] Convert shadings and tiling patterns by rewriting their colour functions and pattern content, and process each shared Form XObject once by object id. Done when: `FixupsContent.ShadingsAndForms` converts a shading and a three-deep Form XObject exactly once.
- [ ] Keep spot colours unless `convert_spots_to_process` is set, in which case convert through the tint transform and the alternate space; implement `fix.alias_inks` applying an ink alias map (rename a `Separation` to another ink's name, or convert to process). Done when: `FixupsContent.Spots` keeps `PANTONE Test 1` by default, converts it to process with the option, and renames it through an alias.
- [ ] Implement `fix.widen_hairlines`: for each stroke below the profile minimum compute the `w` whose thinnest-direction device thickness reaches the minimum (`w' = minimum x length(M d) / abs(det(M))`), edit `w` operands, `/LW` in graphics states and stroked text widths, leave dash patterns unchanged, and list in the fixup report each stroke whose other directions became thicker. Done when: `FixupsContent.Hairlines` leaves no stroke below `0.25` pt on `hairlines.pdf` including the anisotropic and sheared lines, and does not alter strokes that already met the minimum.
- [ ] Implement `fix.flatten_transparency` by calling the flattener of `D04 T05 §6` at `flatten_dpi` (default `300`), recording in the report how many regions were rasterized and which pages lost live text, and skipping the fixup when the profile allows transparency. Done when: `FixupsContent.Flatten` clears `PreflightContent.Transparency` on the fixture with `SSIM >= 0.98` against the original render and skips under `pdfx-4`.
- [ ] Implement `fix.downsample_images` by calling `PdfOptimizer` image downsampling (`D04 T02 §11`) for images over `image.ppi_max`, and leave images under `image.ppi_min` open with the reason that resolution cannot be invented. Done when: `FixupsContent.Resolution` downsamples the `1200` ppi placement and leaves the `72` ppi placement flagged.
- [ ] Add `FixupsContent.*` tests in `tests/recto-tests/preflight_fixups_content_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=FixupsContent.*` passes under ASan.
- [ ] Commit: `pdf: add preflight content fixups for ICC colour conversion, hairlines, flattening and resolution`

**Test checkpoint:** `recto-tests.exe --gtest_filter=FixupsContent.*` passes under ASan, and skipping the shared-Form-by-object-id rule makes `FixupsContent.ShadingsAndForms` fail.

## 10. PDF/X conversion for X-1a, X-3 and X-4 with output intents

`PDF/X` conversion composes the fixups of `§8` and `§9` into a plan per level, writes the file, re-runs the matching built-in profile, and writes the `PDF/X` identification only when the result passes. This matches Acrobat's `Convert to PDF/X`, and it keeps an honesty rule Acrobat leaves to the user: a file Recto could not make compliant is returned as a partially converted file with its residual findings, never labelled `PDF/X`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `convert_to_pdfx(path, PdfXLevel, PdfXOptions, CancelToken)` in `src/pdf/pdfx.h/.cpp` and its job factory. Done when: `PdfX.ConvertLevels` converts the colour, transparency and box fixtures to each level and the matching profile returns `Passed`.
2. Implement the identification gate. Provisional identification exists only inside the temp file until validation of the final bytes passes. Done when: `PdfX.NoLabelOnFailure` shows an unfixable input (a `72` ppi image under a `150` ppi minimum) yields `PartiallyConverted` with no identification keys in the output.

- [ ] Define `PdfXOptions` (`output_profile` path or a registered condition identifier, `bleed_pt`, `trapped`, `flatten_dpi`, `convert_spots_to_process`, `preserve_pure_black`, `rendering_intent`) and `PdfXResult` (`Converted`, `PartiallyConverted`, the after report, the output path). Done when: `src/pdf/pdfx.h` compiles in `recto-pdf`, `recto-core` and `recto-cli`.
- [ ] Compose the `X-1a` plan: convert all colour to `CMYK`, grey or spot (no `RGB`, `Lab`, `Cal*` or `ICCBased` `RGB`), flatten transparency, embed fonts, set boxes, add the intent, strip prohibited items, write `/Trapped`, and write the `/Info` identification as provisional identification inside the private temp file only. Done when: `PdfX.X1a` output passes `pdfx-1a-2001`.
- [ ] Compose the `X-3` plan: as `X-1a` but keep device-independent colour that the intent covers, still flatten transparency. Done when: `PdfX.X3` output passes `pdfx-3-2002` and keeps an `ICCBased` source space unconverted.
- [ ] Compose the `X-4` plan: keep live transparency and `ICCBased` colour, convert untagged `DeviceRGB` and `DeviceGray` to the intent, write identification in XMP, and raise the PDF version only as far as the profile needs. Done when: `PdfX.X4` output passes `pdfx-4`, keeps the `/ca 0.5` object live, and does not rasterize it.
- [ ] Decide `Converted` or `PartiallyConverted` on the final bytes: the temp file carries provisional identification (`/Info` keys and XMP), and the `AtomicFileWriter` `verify` callback re-opens that exact temp file and runs the target profile including its identification rules; `Passed` publishes it as `Converted`. Otherwise the converter rewrites the temp file without identification, runs the profile again on those bytes to record the residual findings (the identification rule is expected to fail and nothing else may be new), and publishes it as `PartiallyConverted`. Done when: `PdfX.NoLabelOnFailure` passes, `PdfX.ConvertedIsLabelled` shows a `Converted` file carries the identification and passes its profile when re-read from disk, and `PdfX.PartialRevalidated` shows the partial output lacks identification and was revalidated.
- [ ] Verify the conversion preserves the output intent and identification that page operations can drop: converting a file that already has an output intent and identification, then running `D05 T01 §3`'s `extract` on it, keeps `/OutputIntents` and the identification, or the page-ops owner is listed in the report as open. Done when: `PdfX.SurvivesPageOps` extracts pages from a converted file and `PreflightDoc.Intent` is still clean.
- [ ] Add `PdfX.*` tests in `tests/recto-tests/pdfx_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfX.*` passes under ASan.
- [ ] Commit: `pdf: add PDF/X-1a, X-3 and X-4 conversion with output intents and an identification gate`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfX.*` passes under ASan, and writing the identification before the verify step makes `PdfX.NoLabelOnFailure` fail.

## 11. Output Preview session, ink inventory and ink aliases

Output Preview needs the page's inks and a place to render them. The viewer's `DocumentActor` may share only immutable `DisplayList` and `PageText` snapshots, and a `DisplayList` carries no source colour, so an `OutputPreviewSession` owns its own `pdf::Document` opened from the saved file, like a job, on one session thread registered with `Runtime::register_job_thread` so it joins at shutdown step 3 (`D01 T02 §10`). The consequence is recorded as a default: the preview shows the last saved version, the panel says so when the tab has unsaved changes, and it refreshes on save. Adding a second immutable snapshot type to the actor contract would need a dated correction to foundation plan section 4 first. This matches the separations list and Ink Manager of Acrobat's Output Preview.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `OutputPreviewSession` in `src/core/output_preview_session.h/.cpp` (open, close, `inventory()`, request queue with latest-wins coalescing, `CancelToken`, thread registered with `Runtime`). Done when: `OutputPreview.SessionLifecycle` opens, closes and shuts down mid-request with zero leaked threads and `debug_live_documents() == 0`.
2. Implement `InkInventory` and `InkAliasMap`. Done when: `OutputPreview.Inventory` reports the inks of `spot-and-indexed.pdf` per page.

- [ ] Open the document from the tab's saved path with the password the tab already holds in memory (never persisted), on the session thread, never sharing the `DocumentActor`'s instance. Done when: `OutputPreview.Isolation` previews a file that is rendering in a `DocumentActor` with no shared `pdf::Document`.
- [ ] Build `InkInventory` per page and per document from `§3`'s events: the four process plates when `DeviceCMYK`, `ICCBased` `N=4` or process `DeviceN` components are used, one plate per distinct spot name from `Separation` and `DeviceN` (including through `Indexed` and pattern spaces), and the special colorants `/All` and `/None` handled as defined by ISO 32000-2 (`/All` paints every plate, `/None` paints none). Done when: `OutputPreview.Inventory` matches the fixture sidecars and `OutputPreview.SpecialColorants` paints `/All` into every plate and `/None` into none.
- [ ] Implement `InkAliasMap` (spot name to another spot name, or to `Process`) applied to the inventory and to later renders, session-scoped and never written to the document (the document change is `§9`'s `fix.alias_inks`). Done when: `OutputPreview.Aliases` merges two aliased spots into one plate in the inventory.
- [ ] Detect that the saved file changed (size, modified time and a hash of the first and last `64 KiB`) and raise `source_changed` so the panel offers a refresh. Done when: `OutputPreview.SourceChanged` raises after the file is replaced on disk.
- [ ] Coalesce requests so only the latest viewport request renders and stale ones are cancelled within `50 ms`. Done when: `OutputPreview.LatestWins` issues `20` requests and only the last completes.
- [ ] Cap what one request may use: at most `16` plates and a render region of at most `2048 x 2048` pixels, so memory is bounded at `64 MiB` of plate data. Done when: `OutputPreview.Bounds` refuses a larger request with a typed error and reports the inks it could not show when a page has more than `16` plates.
- [ ] Add `OutputPreview.*` tests in `tests/recto-tests/output_preview_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OutputPreview.*` passes under ASan.
- [ ] Commit: `core: add Output Preview session with ink inventory and ink aliases`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OutputPreview.*` passes under ASan, and sharing the actor's `pdf::Document` in the session makes `OutputPreview.Isolation` fail.

## 12. Plate renderer: separations, overprint simulation and total area coverage

This section draws each ink on its own 8-bit plane from the paint events of `§3`, simulates overprint, composites the visible plates to a screen preview through `§7`, and answers the questions the panel asks: which inks are under this point, and what is the total area coverage. It matches Output Preview's separations, overprint preview and TAC readout. Anything it cannot draw exactly goes into a `PreviewLimits` list the panel shows, so an approximation is never presented as exact.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `PlateRenderer::render(session, page, region, scale, plates, options, CancelToken) -> shared_ptr<const PlateSet>` in `src/pdf/plate_renderer.h/.cpp` using the per-worker Direct2D replay of `D01 T02 §6`. Done when: `Plates.Fixtures` renders every plate of `tac-patches.pdf` and `spot-and-indexed.pdf` and each patch matches its sidecar value within one 8-bit level.
2. Implement overprint, composite and sampling. Done when: `Plates.Overprint`, `Plates.Composite` and `Plates.Sampling` pass.

- [ ] Render one 8-bit ink plane per plate (value `0` to `255`) where a knockout object writes its tint into every plate it covers, including `0` where it has no ink, and an object painted with overprint writes only into the plates it has ink in, per the ISO 32000-2 overprint rules for `DeviceCMYK`, `DeviceGray`, `Separation` and `DeviceN` (with `/OPM 1` leaving zero `DeviceCMYK` components untouched). Done when: `Plates.Overprint` shows the `K 100` overprint text leaves the cyan plate intact under it, the same text without overprint knocks the cyan out, the white overprint object changes nothing, and a spot fill with overprint leaves the process plates intact.
- [ ] Draw images per plate from their native components (`§3`'s `NativeBitmap`), clipping and soft-masking as the interpreter specifies, and draw shadings by evaluating the function per plate. Done when: `Plates.Images` matches the generated image's channel values and `Plates.Shading` matches the shading sidecar at five sample points.
- [ ] Handle alpha with source-over per plane and record every unsupported construct in `PreviewLimits` instead of drawing it wrong: blend modes other than `Normal`, `Multiply` and `Screen`, overprint on `ICCBased` and `Lab` spaces (treated as knockout), isolated or knockout transparency groups, and images whose components were unavailable. Done when: `Plates.PreviewLimits` lists `Difference` blend and the `components_unavailable` image for the fixtures that contain them, and lists nothing for the plain fixtures.
- [ ] Composite the visible plates to BGRA: convert the process plates through the output intent (or the chosen simulation profile) to sRGB with `§7`, add spots through their alternate space tint transform (`D01 T02 §3` functions) multiplied over the result, and exclude hidden plates. Done when: `Plates.Composite` matches the Python oracle within `DeltaE76 <= 3.0` for the process patches and shows a hidden plate's ink absent.
- [ ] Implement `PlateSet::sample(x, y)` returning each plate's percent and `tac_percent`, the sum of the four process plates regardless of plate visibility (and plus spots when the option is set), and `tac_mask(limit_percent)` as an overlay plane. Done when: `Plates.Sampling` reads `85 75 68 90` as `318` percent at the patch and `Plates.TacIgnoresVisibility` shows hiding the black plate does not change `tac_percent` or the mask.
- [ ] Produce an `overprint_mask` plane of the area painted by objects with overprint enabled for the Warnings mode of `§14`. Done when: `Plates.OverprintMask` covers exactly the planted overprint objects.
- [ ] Add `Plates.*` tests in `tests/recto-tests/plate_renderer_tests.cpp`, rendering on the session thread. Done when: `recto-tests.exe --gtest_filter=Plates.*` passes under ASan with the render running on the session thread.
- [ ] Commit: `pdf: add plate renderer with overprint simulation, composite and TAC sampling`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Plates.*` passes under ASan, and making overprint objects knock out makes `Plates.Overprint` fail.

## 13. Preflight dialog, report view and profile management

The Preflight dialog lets a user pick a profile, analyse the file, read the results by category with a jump to the offending page, fix what can be fixed into a new copy, convert to `PDF/X`, export the report, and manage profiles. It matches Acrobat's Preflight dialog and keeps Recto's rules: it runs as a queued job, shows the honest verdict including `Incomplete`, never edits the open document, and is reachable from `Ctrl+K`.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface: no SPEC screenshot shows it. It follows `design/SPEC.md` section 9.9 dialog chrome (the two-column form of the Watermark dialog: settings left, results right), section 9.5 queue behaviour and section 4 tokens. Design board: `design/SPEC.md` section 9.14 with `design/screenshots/D-Preflight.png` (drawn by `D06 T04 §19`).
**Job:** A print buyer or designer picks a profile, runs the check on the open file or a chosen file, sees pass, fail or incomplete with the findings grouped by category, jumps to a finding on the page, saves a fixed copy or a `PDF/X` copy, and exports the report. Consumers: the user filing the report, `DocumentView` (jump), `ConversionRunner` (jobs).
**Treatment:** A `ContentDialog` on the shared 9.9 chrome with a profile `ComboBox`, an `Analyze` button, a verdict banner (`RectoSuccessBrush`, `RectoWarningBrush`, `RectoDangerBrush` with icon and text, never colour alone), a grouped `TreeView` of findings with counts and an `omitted` note, a coverage summary, and buttons `Fix to new copy`, `Convert to PDF/X...`, `Save report...`. Cheaper substitute that fails the checkpoint: a text box dump of findings with no jump, or a green banner on `Incomplete`.
**Chrome:** Consume the single-active-dialog host and `ContentDialog` chrome of `D02 T01 §10`, `QueueCard` and the job runner (`D04 T01 §5`), `CommandRegistry` (`D02 T03 §11`), `DocumentView` navigation, and `RectoColors.xaml` status brushes with Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Implement `PreflightViewModel` in `src/services/preflight_view_model.h/.cpp` driving profile choice, the analyse job, result tree, fix and convert jobs, and export. Done when: `PreflightUi.ViewModel` runs analyse, fix and export against fixtures with no UI thread blocking.
2. Build `src/app/views/PreflightDialog.xaml/.cpp` and the profile editor. Done when: `AppTests.PreflightDialog` matches the layout in a display-session capture and every control passes `A11yInventory`.

- [ ] Register `PrintProduction.Preflight`, `PrintProduction.ConvertToPdfX`, `PrintProduction.OutputPreview` and `PrintProduction.PreflightFixups` in `CommandRegistry` (`D02 T03 §11`) with synonyms (`preflight`, `pdf/x`, `print ready`, `separations`, `ink`, `tac`), and add `Preflight` to the `more` menu. Done when: `Panels.CommandRegistryCompleteness` passes and typing `preflight` in `Ctrl+K` runs the dialog.
- [ ] Build the profile picker, `Analyze` and the verdict banner in `PreflightDialog.xaml`, running the job from `§6` and showing progress in the dialog and `QueueCard`. Done when: analysing `pdfx-valid-4.pdf` shows `Passed` and `deep-forms.pdf` shows `Incomplete` with the reason and no success styling. Cheaper substitute: treating a missing result as success.
- [ ] Build the grouped findings `TreeView` (category, rule, instance) with counts, `omitted` notes and a coverage summary, and make selecting an instance call `DocumentView` navigation to the page with the finding's `bbox_pt` highlighted. Done when: `AppTests.PreflightDialog` selects a hairline finding and the view shows its page and box.
- [ ] Wire `Fix to new copy` to the fixup job of `§8` and `§9` with a confirmation naming the output file and a result summary listing applied, skipped and failed fixups, and `Convert to PDF/X...` to `§10` with a level picker, output condition picker (profiles from `§7`), bleed and trapping inputs. Done when: converting a fixture from the dialog produces a new file in the chosen folder and the dialog shows `Converted` or `PartiallyConverted` with residual findings. Cheaper substitute: overwriting the open file.
- [ ] Build profile management: duplicate a built-in, edit severities and parameters from the schema with inline validation, rename, delete a user profile, import and export JSON, remembering the last profile through `SettingsStore` (`D02 T04 §2`). Done when: `AppTests.PreflightProfiles` edits a duplicated profile, runs it, exports it and re-imports it with identical content.
- [ ] Show `Save report...` with a `FileSavePicker` for `.json`, `.html` and `.txt`, writing through `§6`'s exporters, and announce completion through `IAnnouncementSink` (`D03 T02 §5`). Done when: the three exports open correctly and the announcement is recorded.
- [ ] Give every control an automation name and tooltip and make the whole dialog keyboard operable, including the findings tree. Done when: `A11yInventory` (`D03 T02 §3`) passes on `PreflightDialog`.
- [ ] Record `Preflight` in the `design/SPEC.md` section 8.2 `more` menu list and add a `Preflight` row to the section 9.9 dialog table. Done when: both rows exist in `design/SPEC.md`.
- [ ] Commit: `app: add Preflight dialog with grouped results, fixups, PDF/X conversion and profile management`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PreflightUi.*:AppTests.PreflightDialog:AppTests.PreflightProfiles` passes on the display-session host, and making the banner green on `Incomplete` fails `AppTests.PreflightDialog`.

## 14. Output Preview panel with plate toggles and pointer ink readout

The Output Preview panel shows separations on the page: a list of plates with show and hide, a total area coverage readout under the pointer, an overprint preview toggle, warnings for TAC over a limit, and the Ink Manager. It matches Acrobat's Output Preview, and it adds a keyboard way to read inks because a pointer-only readout would exclude keyboard users. The preview is the saved version of the file (`§11`), and the panel says so.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface: no SPEC screenshot shows it. It is a new panel in the `design/SPEC.md` section 8.3 `SidePanelHost` (floating card in Focus, left dock in Classic, right inspector in Reading room) using section 4 tokens, with the Ink Manager as a section 9.9 `ContentDialog`. Design board: `design/SPEC.md` section 9.14 with `design/screenshots/D-Preflight.png` (drawn by `D06 T04 §19`).
**Job:** A prepress operator turns on Output Preview, sees which plates a page uses, hides a plate to inspect another, reads the ink percentages and TAC at any point (by pointer or by keyboard), sees where TAC exceeds a limit and where overprint is set, and aliases or converts spot inks.
**Treatment:** A `SidePanelHost` panel with a mode switch (`Separations`, `Warnings`), a plate list (colour swatch from the plate's ink, name, eye toggle, page-use badge), an `Overprint preview` toggle, a TAC limit number box, a `Readout` text block, an `Ink Manager...` button, a visible `Approximate` expander listing `PreviewLimits`, and a `Showing saved version` banner with `Refresh`. On the page a `OutputPreviewOverlay` paints the composite over the tile layer, and a readout chip follows the pointer. Cheaper substitute that fails the checkpoint: a static composite image with no per-plate toggles, or a readout reachable only by pointer.
**Chrome:** Consume `SidePanelHost` (`D02 T03 §3`), the page overlay slot and pointer events of `DocumentView` (`D02 T02 §4`), `CommandRegistry` (`D02 T03 §11`), `AnnouncementService` (`D03 T02 §5`), the 9.9 dialog chrome for Ink Manager, and `RectoColors.xaml` tokens with Fluent UI System Icons (`RectoIcons`: `eye`, `eye_off`) for the eye toggle.

**Build order:**

1. Implement `OutputPreviewViewModel` in `src/services/output_preview_view_model.h/.cpp` bound to `OutputPreviewSession` and `PlateRenderer`. Done when: `OutputPreviewUi.ViewModel` toggles plates, changes the limit and samples points without blocking the UI thread.
2. Build `OutputPreviewPanel.xaml/.cpp`, `OutputPreviewOverlay` and the Ink Manager dialog. Done when: `AppTests.OutputPreviewPanel` shows the plates of `spot-and-indexed.pdf`, hides one and reads TAC at a patch.

- [ ] Register the panel in `SidePanelHost` with the switcher entry `Output Preview` and a header action `Ink Manager`, shown only when a document is open, and add the panel to `design/SPEC.md` section 8.3. Done when: the panel opens from the switcher, `Ctrl+K` and the `more` menu in all three layout presets.
- [ ] Build the plate list from `InkInventory` with swatch, name, eye toggle (`AutomationProperties.Name` `Show <ink>`/`Hide <ink>`), and a per-plate page-use badge; toggling re-composites without a re-render when the planes are cached. Done when: hiding the cyan plate removes cyan from the overlay in `AppTests.OutputPreviewPanel` and the TAC readout is unchanged.
- [ ] Build the pointer readout chip near the pointer and the `Readout` text block in the panel showing each plate's percent and `TAC`, updating at most every `33 ms`, and add keyboard sampling: when the overlay is focused, arrow keys move a crosshair by `1` point (`Shift` by `10`) and the sampled values are announced politely through `AnnouncementService`. Done when: `AppTests.OutputPreviewReadout` reads `318` percent at the `85 75 68 90` patch by pointer and by keyboard.
- [ ] Build the `Warnings` mode: a TAC-over-limit overlay in `RectoDangerBrush` at `40` percent opacity from `PlateSet::tac_mask`, a numeric limit box (default `300`, range `100` to `400`), and an `Overprinting objects` highlight from `overprint_mask`. Done when: the mask covers exactly the over-limit patches and the overprint objects in the fixtures.
- [ ] Show the `Showing saved version` banner when the tab has unsaved changes or `source_changed` is raised, with `Refresh`, and show `PreviewLimits` in an `Approximate` expander that is open by default when non-empty. Done when: editing the tab marks the banner and a page with a `Difference` blend lists it.
- [ ] Build the Ink Manager `ContentDialog`: a list of spot inks with an alias dropdown (`None`, another ink, `Process (CMYK)`), a `Convert all spots to process` check, `Apply to preview`, and `Save converted copy...` that runs `fix.alias_inks` and `fix.convert_colours` (`§9`) as a job to a new file. Done when: aliasing two spots to one merges their plates in the preview, and the saved copy re-opens with the alias applied. Cheaper substitute: renaming inks in the open document.
- [ ] Respect reduced motion and high contrast (`D02 T04 §7`, `§8`) and give every control an automation name and tooltip. Done when: `A11yInventory` passes on `OutputPreviewPanel` and `OutputPreviewOverlay`.
- [ ] Add Acrobat's `Rich black` and `Black text` colour warnings beside the TAC warning: highlight areas whose black is built from K plus other process inks above a configurable threshold, and black text drawn in more than one ink (`docs/acrobat-parity.md`). Done when: the rich-black and four-colour-text fixtures are highlighted and plain K text is not.
- [ ] Commit: `app: add Output Preview panel with plate toggles, TAC readout, warnings and Ink Manager`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OutputPreviewUi.*:AppTests.OutputPreviewPanel:AppTests.OutputPreviewReadout` passes on the display-session host, and removing the keyboard sampling path fails `AppTests.OutputPreviewReadout`.

## 15. `recto-cli preflight` command family

The CLI makes every print production operation scriptable and gives tests a seam below the UI. It follows the dispatcher and exit-code contract of `D01 T03 §1` (`0` success, `1` invalid arguments, `2` damaged or unreadable file, `3` password) and adds `4` for a `Failed` verdict and `5` for `Incomplete`, so a script can tell a failed file from an unchecked one. It runs every operation through the same job engine as the app.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `recto-cli preflight <run|fix|convert|profiles|inks|plate>` in `src/cli/cmd_preflight.cpp`. Done when: `Cli.PreflightRun` runs a profile on a fixture and returns the documented exit code.
2. Implement the remaining verbs and `--json`. Done when: `Cli.PreflightFix`, `Cli.PreflightConvert`, `Cli.PreflightProfiles`, `Cli.PreflightInks` and `Cli.PreflightPlate` pass.

- [ ] Implement `recto-cli preflight run <in.pdf> [--profile <id|path.json>] [--report <out.json|.html|.txt>] [--password <pw>] [--json]`, exit `0` for `Passed` and `PassedWithWarnings`, `4` for `Failed`, `5` for `Incomplete`; the report requested with `--report` is written for exits `0`, `4` and `5` (the diagnostic is the point of those runs) and not for `1`, `2` and `3`. Done when: `Cli.PreflightRun` returns `0` on `pdfx-valid-4.pdf`, `4` on a broken fixture and `5` on `deep-forms.pdf`, and a report file exists for all three.
- [ ] Implement `recto-cli preflight fix <in.pdf> --profile <id|path> --out <out.pdf> [--password <pw>] [--json]` writing a new copy and the `FixupReport` as JSON, exit `0` when no error finding remains and `4` when residual findings remain (the copy and the report are still written, because the copy is a valid improved file and the report lists what is left). Done when: `Cli.PreflightFix` writes a fixed copy, never the input, and refuses an `--out` equal to the input with exit `1`.
- [ ] Implement `recto-cli preflight convert <in.pdf> --to x1a|x3|x4 --output-profile <icc|condition-id> --out <out.pdf> [--bleed <pt>] [--trapped true|false] [--flatten-dpi <n>] [--json]`, exit `0` for `Converted` and `4` for `PartiallyConverted`, where the partial file (without identification) and the residual report are written on exit `4`. Done when: `Cli.PreflightConvert` converts a fixture to each level and returns `4` with no identification keys for the unfixable fixture.
- [ ] Implement `recto-cli preflight profiles <list|show|import|export|validate>` over `§2`. Done when: `Cli.PreflightProfiles` lists the five built-ins, validates a user profile and rejects a malformed one with exit `1` and the JSON pointer.
- [ ] Implement `recto-cli preflight inks <in.pdf> [--page <n>] [--json]` printing the `InkInventory`, and `recto-cli preflight plate <in.pdf> --page <n> --plate <name> --out <plate.png> [--scale <n>] [--overprint on|off] [--sample <x,y>]` writing one plate as an 8-bit PNG and printing sampled percents and `TAC`. Done when: `Cli.PreflightInks` and `Cli.PreflightPlate` match the fixture sidecars.
- [ ] Map `NeedsPassword` to `3`, damaged files to `2`, and `Cancelled` to a documented exit code, and state the output rule per exit code: on `1`, `2`, `3` and cancel nothing is written; on `4` and `5` only the diagnostic report and, for `fix` and `convert` on exit `4`, the new copy are written; on `0` everything requested is written. Done when: `Cli.PreflightExitCodes` verifies each code against that table, including that no output exists after exits `1`, `2`, `3` and cancel.
- [ ] Add `Cli.Preflight*` tests in `tests/recto-tests/cli_preflight_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Cli.Preflight*` passes under ASan.
- [ ] Commit: `cli: add recto-cli preflight run, fix, convert, profiles, inks and plate`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.Preflight*` passes under ASan, and mapping `Incomplete` to exit `0` fails `Cli.PreflightRun`.

## 16. Print production honesty gates and fuzz targets

This section proves the properties that make the tools trustworthy: a verdict never claims what was not analysed, the checks cannot be fooled by token layout, fixups and conversions never touch their input, and the new parsers survive hostile input. The gates test behaviour owned by earlier sections; a failing gate is fixed in its owner, never by loosening the gate.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `tests/recto-tests/print_honesty_tests.cpp` with the gates below. Done when: `recto-tests.exe --gtest_filter=PrintHonesty.*` runs every gate.
2. Run the new fuzz targets for the acceptance time. Done when: `fuzz_preflight_profile` and `fuzz_icc_profile` run `10` minutes clean from the fixtures as seeds and the findings are triaged under `D06 T02 §4`.

- [ ] Gate `PrintHonesty.NeverPassesUnanalysed`: for every fixture marked `expect_incomplete`, every profile returns `Incomplete` or `Failed`, never `Passed`, and a planted skip of a page without a ledger entry fails the gate. Done when: the gate passes and the planted skip fails it.
- [ ] Gate `PrintHonesty.TokenLayoutIndependent`: the `CMYK` logo fixture rewritten with different whitespace, comment placement, CR-only line ends and split `/Contents` boundaries yields the same findings in all variants. Done when: all variants report identical findings.
- [ ] Gate `PrintHonesty.InputUntouched`: after `preflight fix`, `preflight convert` and an Ink Manager copy, the input file's SHA-256 is unchanged, no temp file remains after success or injected failure, and the output differs from the input path. Done when: the gate passes with fault injection at each `AtomicFileWriter` step (`D06 T03 §5` harness).
- [ ] Gate `PrintHonesty.NoLabelOnFailure`: no output of any conversion carries `PDF/X` identification unless the matching profile returns `Passed` on that output re-read from disk. Done when: the gate re-opens each converted fixture and checks both conditions.
- [ ] Gate `PrintHonesty.TacFromPlanes`: the TAC readout equals the sum of the rendered process planes at the sample, and differs from a vector-colour-only estimate on the shading and image fixtures, proving it reads the planes. Done when: the gate passes on the shading and image patches.
- [ ] Gate `PrintHonesty.NoScriptsAnywhere`: profile loading, report HTML, and the whole print production code path never execute PDF JavaScript, launch actions, external commands or network calls, scanned in the fixtures with `/JavaScript` and `/Launch` actions. Done when: the gate runs the fixtures with a network and process-creation trap installed and observes no call.
- [ ] Add both targets to the scheduled fuzz workflow of `D06 T02 §3` and promote any crash input to `tests/corpus/broken/print/` with a provenance row. Done when: the workflow lists `fuzz_preflight_profile` and `fuzz_icc_profile`.
- [ ] Commit: `test(quality): prove print production verdicts, fixups and parsers are honest and bounded`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PrintHonesty.*` passes under ASan, and making `deep-forms.pdf` return `Passed` fails `PrintHonesty.NeverPassesUnanalysed`.

## 17. PDF/E and PDF/VT identification and partial preflight profiles

Acrobat Pro's Standards tool identifies and validates PDF/E (ISO 24517, engineering documents) and PDF/VT (ISO 16612, variable and transactional printing) besides PDF/A, PDF/UA and PDF/X (foundation plan D18; `docs/acrobat-parity.md`). This section adds both as built-in profiles on the preflight engine of §2 to §6: the identification read from XMP, the document-level rules the engine can check, and for PDF/VT the document part hierarchy (`/DPartRoot`) and its record boundaries. It is honest about coverage: each profile ships a requirement inventory generated from the standard's clause index (every numbered clause that contains a requirement, each marked `implemented`, `not implemented` or `not applicable` with a reason) and an `inventory_complete` flag that stays `false` until a second reader has compared the inventory with the standard. The status matrix is fixed: no identification gives `NotClaimed`; identification with no checks run gives `IdentifiedOnly`; identification with the implemented checks run gives `PartialChecksPassed` or `PartialChecksFailed`, and `PartialChecksPassed` is allowed while entries are `not implemented`; the word `Conformant` is reachable only when `inventory_complete` is `true`, no entry is `not implemented` and every check passes, which no profile satisfies in v1. Supported versions are PDF/E-1, PDF/VT-1 and PDF/VT-2, whose base standards the engine already checks (PDF/X rules of §4 and §5). PDF/VT-3 is ISO 16612-3 on PDF/X-6 (ISO 15930-9), which has no validation owner in this file, so a PDF/VT-3 file is reported `IdentifiedOnly` with the note that its PDF/X-6 base was not assessed. It adds no conversion: Recto validates and reports, and conversion to PDF/E or PDF/VT stays out of scope until a user asks (recorded in `docs/user-demand.md`).

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add PDF/E-1, PDF/VT-1, PDF/VT-2 and PDF/VT-3 identification readers (XMP `pdfe:ISO_PDFEVersion` and `pdfvt:version`) and built-in profiles for PDF/E-1, PDF/VT-1 and PDF/VT-2 under `resources/preflight/` on the §2 schema, each rule carrying its ISO clause in `source` as read by the implementer. Done when: `Preflight.PdfEVtProfiles` loads both profile families and identifies the claimed version of each fixture.
2. Validate the PDF/VT document part hierarchy: `/DPartRoot` present when claimed, `/DParts` arrays well formed and bounded in depth and count, every page reached exactly once, record metadata (`/DPM`) readable. Done when: the malformed-hierarchy fixtures each yield a named finding and no crash under ASan.

- [ ] Generate PDF/E and PDF/VT fixtures (claimed and valid, claimed with a violation, unclaimed) with `scripts/build-corpus.py` into `tests/corpus/print/standards/`, with provenance rows. Done when: `Corpus.Provenance` passes.
- [ ] Read the claimed version from XMP and report "Not claimed" rather than guessing from content. Done when: an unclaimed fixture reports "Not claimed" for both standards.
- [ ] Author `resources/preflight/pdfe-1.json` with the document-level rules the engine can check (encryption forbidden, fonts embedded, output and colour rules, JavaScript forbidden, 3D content in the allowed form). Done when: the violation fixture fails the expected rule and the valid fixture reports `PartialChecksPassed`, not conformant.
- [ ] Author `resources/preflight/pdfvt-1.json` and `pdfvt-2.json` on top of the PDF/X rules of §4 and §5 they build on, and make a claimed PDF/VT-3 report `IdentifiedOnly` with the unassessed PDF/X-6 base named. Done when: each valid fixture reports `PartialChecksPassed`, each violation fixture fails its named rule, and a PDF/VT-3 fixture reports `IdentifiedOnly`, never a pass.
- [ ] Register the standard-specific check kinds with `register_check_kind` (§2) and name each one's evaluator owner: `standard.identification` (version read from XMP, owner §17), `pdfe.three_d_allowed` (3D content only in the allowed form, owner §17), `pdfvt.dpart_integrity` (hierarchy present, well formed, every page reached exactly once, owner §17) and `pdfvt.record_metadata` (`/DPM` readable). Each has a typed `params` schema in the profile. Done when: `Preflight.PdfEVtKinds` loads a profile naming the PDF/VT hierarchy rule and refuses it if the kind is not registered.
- [ ] Write the requirement inventory for each profile in `resources/preflight/inventory/*.json` from the standard's clause index (clause id, title as read, `implemented` true, false or `not_applicable` with reason, rule id) with `inventory_complete: false`, and derive `ConformanceStatus` in the report and dialog from the matrix above. Done when: `Preflight.PdfEVtInventory` asserts the matrix over a table of cases: `PartialChecksPassed` with unimplemented entries is accepted, `Conformant` with an unimplemented entry or `inventory_complete: false` is refused, and no report for these profiles in v1 says conformant.
- [ ] Walk `/DPartRoot` iteratively with depth 64 and node count 1,000,000 caps that return `Truncated` and a finding, never recursion on document depth (D06 T03 §1). Done when: a 100,000-deep fixture reports the cap and the run completes.
- [ ] Show PDF/E and PDF/VT in the Preflight dialog's profile list (§13) and in `recto-cli preflight --profile pdfvt-2` (§15). Done when: both entry points run the profiles.
- [ ] Record "convert to PDF/E or PDF/VT" as not planned in `docs/user-demand.md` with its reason. Done when: the ledger row exists.
- [ ] Commit: `print: identify and partially preflight PDF/E and PDF/VT`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Preflight.PdfEVtProfiles:Preflight.PdfVtHierarchy:Preflight.PdfEVtKinds:Preflight.PdfEVtInventory` passes under ASan, the claimed-with-violation fixtures each fail their named rule, and the PDF/VT-3 fixture reports `IdentifiedOnly`.

## 18. Whole-document greyscale conversion fixup and the `colour.only_gray` check

Foxit PDF Editor converts a whole document to greyscale; Acrobat Pro reaches a similar result only through the Convert Colors preflight fixup, which `§9` matches with a CMYK target. `§9` has no grey target, so this section adds one: `fix.convert_to_grey` in `src/pdf/preflight_fixups_content.cpp` rewrites every colour in the file to `DeviceGray`, on a copy, through the same content-stream operator editor and colour rewrite machinery as `fix.convert_colours`, and a new check kind `colour.only_gray` proves that nothing coloured is left. Three methods are offered: `PdfSpec` (default, the device-space conversion formulas of ISO 32000: `gray = 0.3 R + 0.59 G + 0.11 B` and `gray = 1 - min(1, 0.3 C + 0.59 M + 0.11 Y + K)`), `Rec709` (luminance weights `0.2126`, `0.7152`, `0.0722` applied to linear light) and `IccGray` (through a grey output profile with `§7`). The fixup keeps Recto's rules: a new file through `PdfWriter` and `AtomicFileWriter`, the original and the open document untouched, no second tokenizer, and a report that never says `Applied` for an object it could not convert.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `fix.convert_to_grey` and its three methods for operators, images, shadings, patterns, Form XObjects and annotation appearances. Done when: `FixupsGrey.Methods` converts the `greyscale-mixed.pdf` fixture and matches the Python oracle of each method within `1/255` per sample, under ASan.
2. Register `colour.only_gray` and gate the fixup on it. Done when: `FixupsGrey.OnlyGrayVerified` shows the after report passes `colour.only_gray`, and a planted skipped object makes the fixup report `Failed` for it, under ASan.

- [ ] Define `GreyMethod` (`PdfSpec`, `Rec709`, `IccGray`), `GreyOptions` (`method`, `gray_profile` path, `preserve_pure_black` default `true`, `on_unconverted` of `Fail` or `Warn`) and `GreyReport` (`objects_changed`, `images_converted`, `shadings_converted`, `unconverted` as object id and reason) in `src/pdf/preflight_fixups_content.h`. Done when: the header compiles in `recto-pdf`, `recto-core` and `recto-cli`.
- [ ] Rewrite colour operators `g G rg RG k K cs CS sc SC scn SCN` and `ri` for every paint event whose effective space is not `DeviceGray`, converting each distinct colour once, mapping `RGB` black and `K 100` black to gray `0` when `preserve_pure_black` is set, and handling `Lab`, `CalGray`, `CalRGB`, `ICCBased`, `Indexed`, `Separation` and `DeviceN` through their alternate space and tint transform before the grey conversion. Done when: `FixupsGrey.Operators` matches the oracle for each space on `greyscale-mixed.pdf` and keeps black text at gray `0`.
- [ ] Convert image XObjects and inline images (including `Indexed` palettes, `/Decode` arrays and `ICCBased` sources) to one-component `DeviceGray`, keep `/SMask` untouched, leave stencil masks as they are (the fill colour that paints them is converted), and replace a colour-key `/Mask` array (two integers per source component, evaluated on the original samples) with an explicit one-component mask image derived from the original samples before conversion, so colours that collapse to the same grey keep their transparent or opaque state, keep `DCTDecode` sources as `DCTDecode` at quality `>= 90`, and write other sources as `FlateDecode`. Done when: `FixupsGrey.Images` converts an `RGB`, a `CMYK`, an `Indexed` and an inline image, keeps the soft mask, `FixupsGrey.ColourKeyMask` keeps a transparent colour and an opaque colour of equal grey value distinct, and leaves `undecodable-image.pdf` untouched with its reason in `unconverted`. Cheaper substitute: rasterizing whole pages, which loses live text.
- [ ] Convert shadings, function-based colour, coloured tiling patterns and Form XObjects once per object id by rewriting their colour functions and content, and set the `/CS` of transparency groups (page, Form XObject and soft-mask groups) to `DeviceGray` so blending happens in the grey space. Done when: `FixupsGrey.ShadingsPatternsGroups` converts an axial shading, a coloured pattern, a three-deep Form XObject and a group, each exactly once.
- [ ] Convert the appearance streams of annotations and form widgets (printing or not) and also the stored properties that regenerate them: annotation `/C` and `/IC`, widget `/MK` `/BC` and `/BG`, every `/DA` default appearance string wherever it is stored (on fields and widgets, on `/AcroForm`, and on the annotation dictionary itself for `FreeText` (ISO 32000-1 Table 174, where `/DA` is required) and for `Redact` annotations that carry `/OverlayText` (Table 187)), and the colours in `/DS` and `/RC` rich text, then remove `/DefaultRGB` and `/DefaultCMYK` default colour spaces from resources and leave output intents untouched. Done when: `FixupsGrey.AnnotationsAndDefaults` finds a highlight, a text field, a `FreeText` annotation and a `Redact` annotation with overlay text in grey and no default colour space left, `FixupsGrey.RegeneratedAppearance` edits a converted text field and regenerates its appearance with the form engine (`D03 T03 §6`) and finds it still grey, and `FixupsGrey.RegeneratedFreeText` regenerates the appearance of a converted `FreeText` annotation that has a red `/DA` and no `/DS` or `/RC` with the annotation appearance generator (`D03 T03 §2`) and finds it still grey (the test fails if annotation-level `/DA` is skipped).
- [ ] Implement `IccGray` through `ColourTransform` (`§7`) with the chosen grey profile, accept a profile only when its data colour space is `GRAY` and its device class is `mntr`, `prtr` or `scnr` with a tone curve usable in the PCS-to-device direction, refuse any other class (`link`, `abst`, `nmcl`) and any other colour space with a typed error naming the fault, and report the rendering intent used. Done when: `FixupsGrey.IccGray` converts through the synthetic output-class (`prtr`) grey profile within `DeltaE76 <= 2.0` of the oracle, accepts a `mntr` grey profile, and refuses an `RGB` profile and a `link` profile.
- [ ] Register the check kind `colour.only_gray` with `register_check_kind` (`§2`), owner `§18`, evaluating the effective spaces of every paint event and image through the observer of `§3` and the colour-space graph of `§5`, so colour inside nested Form XObjects, patterns, shadings and appearances is found whatever the token layout. Done when: `PreflightContent.OnlyGray` flags a colour operator three Form XObjects deep and passes the converted output.
- [ ] Re-run `colour.only_gray` on the written temp file in the `AtomicFileWriter` verify callback of `§8`, list any object still coloured in `GreyReport::unconverted`, and never report `Applied` while one remains; with `on_unconverted = Fail` write no output. Done when: `FixupsGrey.OnlyGrayVerified` and `FixupsGrey.NoFalseApplied` pass, the latter with a planted no-op fixup. Cheaper substitute: trusting the rewriter's return value without re-reading the output.
- [ ] Add `greyscale-mixed.pdf` to the `print` group of `scripts/build-corpus.py` (RGB, CMYK, Lab, `ICCBased`, `Separation`, `DeviceN`, `Indexed` image, inline image, axial shading, coloured tiling pattern, shared Form XObject, transparency group, highlight annotation, a colour-key masked RGB image with a transparent and an opaque colour of equal grey value, a text field whose `/DA` sets a red font, a `FreeText` annotation whose own `/DA` sets red text with no `/DS` or `/RC`, a `Redact` annotation with `/OverlayText` and a red `/DA`, and `test-gray-prtr.icc` and `test-gray-mntr.icc` synthetic grey profiles) with a sidecar holding the oracle grey value of each object for each method, and record it in `tests/corpus/PROVENANCE.md`. Done when: `Corpus.Provenance` passes and two runs are byte-identical.
- [ ] Add `FixupsGrey.*` tests in `tests/recto-tests/preflight_fixups_grey_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=FixupsGrey.*:PreflightContent.OnlyGray` passes under ASan.
- [ ] Commit: `pdf: add whole-document greyscale conversion and the colour.only_gray check`

**Test checkpoint:** `recto-tests.exe --gtest_filter=FixupsGrey.*:PreflightContent.OnlyGray` passes under ASan, and skipping the shared-Form-by-object-id rule fails `FixupsGrey.ShadingsPatternsGroups`.

## 19. Greyscale commands: Preflight dialog, `Ctrl+K` and `recto-cli preflight greyscale`

Make the greyscale conversion of `§18` reachable. The Preflight dialog (`§13`) gains `Convert to greyscale...`, which opens a small dialog with the method and the pure-black option and runs the fixup as a job to a new file; `Ctrl+K` runs the same command; and `recto-cli preflight greyscale` runs it from a script, with the exit codes of `§15`. It matches Foxit PDF Editor's greyscale conversion and keeps Recto's rules: a new uniquely named file, the original never modified, and honest results (`Converted` or `PartiallyConverted` with the objects that stayed coloured).

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface: no SPEC screenshot shows it. It follows the Preflight dialog of `§13` (SPEC section 9.9 dialog chrome) for the button and a `ContentDialog` of the same chrome for the options. Design board: `design/SPEC.md` section 9.14 with `design/screenshots/D-Preflight.png` (drawn by `D06 T04 §19`).
**Job:** A person with a colour document that will be photocopied or printed in black and white converts it to a greyscale copy, sees how many images and objects were converted and whether anything stayed coloured, and opens the result.
**Treatment:** A `Convert to greyscale...` button beside `Convert to PDF/X...`, a `ContentDialog` with a `Method` combo (`Standard (PDF formulas)`, `Perceptual (Rec. 709)`, `ICC grey profile...`), a `Keep black text pure black` check, an output name preview, and a result view that lists unconverted objects when the result is partial. Cheaper substitute that fails the checkpoint: a one-click button with no method choice, or a success message when objects stayed coloured.
**Chrome:** consume the Preflight dialog and `PreflightViewModel` (`§13`), the single-active-dialog host (`D02 T01 §10`), `QueueCard` and the job runner (`D04 T01 §5`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens with Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Add the greyscale job factory and `recto-cli preflight greyscale`. Done when: `Cli.PreflightGreyscale` converts a fixture, returns `0` for `Converted` and `4` for `PartiallyConverted`, and never writes the input.
2. Add the dialog action, command and tests. Done when: `AppTests.PreflightGreyscale` converts a fixture from the dialog, shows the partial result for `undecodable-image.pdf`, and `Ctrl+K` "grayscale" opens the options.

- [ ] Implement `GreyscaleJobFactory` in `src/services/greyscale_job.cpp` running `fix.convert_to_grey` (`§18`) as a job on its own `pdf::Document`, naming the output `<name>-grey.pdf` through `resolve_output_path` (`D04 T01 §4`, `AppendNumber`) and writing through `AtomicFileWriter`. Done when: `Greyscale.JobAtomic` shows a failed verify leaves no output and the input hash unchanged. Cheaper substitute: writing the output with `std::ofstream`, which leaves a truncated file on a crash.
- [ ] Implement `recto-cli preflight greyscale <in.pdf> --out <out.pdf> [--method pdf|rec709|icc] [--gray-profile <icc>] [--keep-pure-black true|false] [--on-unconverted fail|warn] [--password <pw>] [--json]` in `src/cli/cmd_preflight.cpp`, exit `0` for `Converted`, `4` for `PartiallyConverted` (the partial copy and the `GreyReport` are written), the codes `1`, `2`, `3` of `D01 T03 §1`, and refuse an `--out` equal to the input with exit `1`. Done when: `Cli.PreflightGreyscale` verifies each code and the output rule.
- [ ] Build the options `ContentDialog` in `src/app/views/GreyscaleDialog.xaml/.cpp` with the method combo, a profile picker enabled only for `ICC grey profile...`, the pure-black check, the output name preview and an automation name and tooltip on every control. Done when: `A11yInventory` (`D03 T02 §3`) passes on `GreyscaleDialog`. Cheaper substitute: unlabelled controls.
- [ ] Add `Convert to greyscale...` to `PreflightDialog.xaml` and show the `GreyReport` (counts, and each unconverted object with its reason as text with an icon, never colour alone). Done when: `AppTests.PreflightGreyscale` shows `PartiallyConverted` and the reasons for the undecodable fixture.
- [ ] Register `PrintProduction.ConvertToGreyscale` in `CommandRegistry` (`D02 T03 §11`) with synonyms `grayscale`, `greyscale`, `black and white`, `desaturate`. Done when: `Panels.CommandRegistryCompleteness` passes and typing `grayscale` in `Ctrl+K` opens the dialog.
- [ ] Add the `Greyscale` row to the `design/SPEC.md` section 9.9 dialog table. Done when: the row exists.
- [ ] Add `Greyscale.*`, `Cli.PreflightGreyscale` and `AppTests.PreflightGreyscale` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=Greyscale.*:Cli.PreflightGreyscale:AppTests.PreflightGreyscale` passes.
- [ ] Commit: `app: add greyscale conversion to the Preflight dialog, Ctrl+K and recto-cli preflight`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Greyscale.*:Cli.PreflightGreyscale:AppTests.PreflightGreyscale` passes on the display-session host, and a success message on a partial result fails `AppTests.PreflightGreyscale`.

## Verification

- `recto-cli preflight run` on every `tests/corpus/print/` fixture returns the exit code its sidecar expects, and the planted faults are named by rule id, page and object.
- A `CMYK` logo three Form XObjects deep, a spot colour and an `Indexed` image over `DeviceCMYK` are all found (`§5`), and a page the walk could not read makes the verdict `Incomplete` (`§5`, `§6`, `§16`).
- `PDF/X` conversion output passes its matching built-in profile when re-read from disk and carries identification only in that case (`§10`, `§16`).
- Output Preview plates, overprint simulation and TAC match the sidecars within one 8-bit level, and the `Approximate` list is non-empty exactly when an unsupported construct is present (`§12`, `§14`).
- Every control in the Preflight dialog and Output Preview panel carries an automation name and tooltip (`D03 T02 §3`), and every command is reachable from `Ctrl+K`.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
