## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T00:29:41Z
- Effort: medium
- Lines: 55
- Digest: 563e32b5ae9358f3

Review basis: the supplied diff. Local reads failed before PowerShell started, so I could not inspect additional repository documents or the parity ledger. No files were modified; no Git commands or tests ran. References below use the candidate’s domain/file/section identifiers.

## source-defect: needs-attention

1. **D04 T05 §10: per-page slide sizes cannot be represented as specified.** `p:sldSz` is a presentation-wide property, not a per-slide property. Reproduce with an A4 portrait page followed by an A3 landscape page: writing either size changes the canvas for both slides. Specify a common slide canvas with an explicit fit policy, or separate presentations. The existing single-page size tests miss this. [Microsoft PresentationML documentation](https://learn.microsoft.com/en-us/office/open-xml/presentation/working-with-presentations).

2. **D04 T06 §17: PDF/VT-3 is assigned the wrong standards foundation.** PDF/VT-3 uses ISO 16612-3 and PDF/X-6. The proposed implementation builds all three VT profiles on checks supporting only PDF/X-1a, X-3 and X-4. Reproduce with a legitimate PDF/VT-3 file: there is no PDF/X-6 validation owner to establish its base conformance. Add that owner or explicitly restrict the supported VT versions. [ISO 16612-3:2020](https://www.iso.org/standard/75218.html).

3. **D04 T06 §5 and §9: the hairline formula is wrong for nonuniform scaling.** `width × sqrt(abs(det(CTM)))` measures an area-derived scale, not the stroke’s perpendicular thickness. Reproduce with `0.5 w`, CTM `[10 0 0 0.1 0 0]`, and a horizontal line: the formula reports `0.5 pt`, while the stroke is `0.05 pt` thick. The check passes a hairline and the fixup leaves it unfixed. Require direction-aware transformed stroke geometry, including shear, and add anisotropic fixtures. [Adobe PDF reference, graphics transformations and line width](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf).

4. **D04 T02 §14–§15: clipboard acquisition is incorrectly forced onto the UI thread.** `GetClipboardData` can trigger delayed rendering in another application. The 512 MiB size cap does not bound that wait. Reproduce with a clipboard owner that delays its `WM_RENDERFORMAT` response: invoking the command stalls the shell before the conversion job starts. Specify a dedicated clipboard acquisition thread and a cancellation/lifetime policy; Win32 does not require the application’s UI thread to acquire the clipboard. [Microsoft clipboard operations](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-operations).

## consistency: needs-attention

5. **D04 T06 §10: PDF/X identification has a circular acceptance condition.** The matching profile requires identification, but identification may be written only after that profile passes. Conversely, the X-1a/X-3 composition bullets require writing identification before verification. Reproduce with an otherwise compliant, unlabelled file: one reading can never reach `Converted`; the other violates the stated gate. Define provisional identification inside the private temp file, complete validation of those final bytes, and removal/revalidation for partial outputs.

6. **D04 T06 §15: nonzero exits both require and forbid output.** `convert` must publish a `PartiallyConverted` file and exit `4`; the final bullet requires writing nothing on any nonzero exit. Likewise, a failed `run --report` needs its report despite exit `4` or `5`. Reproduce either command against the documented failing fixtures. Specify separately which diagnostic reports and partial document outputs survive each exit code.

7. **D04 T02 §15: the refusal UI cannot execute under its own enablement rule.** The command is disabled when no route handles the clipboard, yet tests require invoking it on empty or unsupported content to display an InfoBar. Reproduce with an empty clipboard and press Ctrl+V or invoke the command through Ctrl+K: ordinary disabled-command dispatch cannot reach the required refusal. Define an invocable refusal path and test the real entry points.

8. **D04 T07 §5, §9 and §14: truncation is not consistently fail-closed for redaction.** The runner downgrades a step’s `Truncated` result to a warning; redaction checks only pages with no searchable text and then repeats the same search. Reproduce with a page containing searchable public text followed by sensitive content beyond an extraction cap: both searches can miss the same content and publish a warning-only result. Require complete search coverage before redaction can succeed, and define how the explicit `skip_with_warning` option relates to the unconditional fail-closed claims.

## integration: needs-attention

9. **D04 T02 §14–§16: the clipboard result contract cannot represent the new outputs.** `ClipboardPdfResult` has one `pdf_path`; `pdfs_to_open` is defined for existing PDFs. Section 16 produces one newly generated PDF per Office file, but supplies no generated-output collection, per-output ownership, or partial-failure contract for §15. Reproduce with two DOCX files and an image: the consumer cannot determine all unsaved tabs and their temp-file lifetimes from the declared result. Define a typed output collection and exercise mixed-route dispatch.

10. **D04 T05 §10–§13: automatic OCR is exposed without an implementation step.** The options, CLI and UI expose OCR, and validation mentions OCR characters, but no writer step invokes OCR or specifies how its results enter the editable text pipeline. Reproduce with an image-only PDF and the default UI setting enabled: exporting only the picture can satisfy the nonempty-output checks. Assign OCR orchestration and require an editable-text assertion on a scanned fixture.

11. **D04 T07 §1, §3, §5 and §12: `ParamSchema` cannot describe the required parameter structures.** Its closed field kinds lack nested objects, arrays of objects and tagged unions. Yet redaction requires `patterns[]` objects, metadata requires `set`, Bates requires a structured value, and conversion carries arbitrary typed `options`. Reproduce by building the redaction editor from the declared schema: it cannot express a pattern’s `kind`, `value`, `case_sensitive` and `whole_word` together. Extend the schema contract and test validation/editor agreement for nested parameters.

12. **D04 T06 §17 versus §2: new standards checks have no extensible rule contract.** Section 2 requires exactly its enumerated check kinds and rejects all others. None represents PDF/E identification, allowed 3D content, PDF/VT identification or DPart integrity. Section 17 adds profile files without extending that registry or defining their typed parameters. Reproduce by authoring the promised hierarchy rule: loading rejects it, or the profile omits the check. Specify the new rule kinds and their evaluator ownership.

## adversarial: needs-attention

13. **D04 T06 §7: header/tag-table validation does not establish the claimed ICC safety boundary.** The listed checks do not validate internal LUT dimensions and payload lengths. Reproduce with an in-bounds `mft1` tag that claims 255 grid points on four inputs but has a short payload: all specified checks can pass before the malformed table reaches WCS. Validate supported tag payload structures with checked size calculations, or narrow the claim that hostile bytes never reach Windows. [ICC specification, `lut8Type`](https://www.color.org/icc1-v41.pdf).

14. **D04 T06 §5: fixed shading sampling can falsely certify TAC.** Sampling 256 positions cannot establish a maximum for arbitrary supported functions, and the ledger supplies no approximation status for this calculation. Reproduce with a narrow high-TAC interval between sample positions: all samples remain below the threshold and the rule passes. Require conservative bounds, exhaustive evaluation where feasible, or `Incomplete` when a maximum cannot be established.

15. **D04 T07 §7 and §9: multi-output publication lacks a failure transaction.** Outputs are published individually, while a failing file must leave no partial output. Reproduce a nine-image export and inject failure during publication of image five: images one through four remain public although the file is `Failed`. Define staging, validation and publication semantics for the complete output set, including safe cleanup and explicit reporting of any retained outputs.

16. **D04 T07 §3, §6 and §14: path checks do not enforce containment through output junctions.** Input enumeration refuses links, but mirrored output folders are created/traversed without a corresponding reparse-point policy. Reproduce with `<output>\a` as a junction to another directory and `keep_structure` publishing `a\x.pdf`: the output escapes the selected root while all filename checks pass. Validate destination ancestry and define race-resistant handling of directory replacement.

17. **D04 T05 §11–§12: raster fallback can change appearance while all required validation passes.** A non-Normal blend object depends on its backdrop. Rasterizing only that object’s bounds as an independent PNG and compositing normally is not generally equivalent. Reproduce with a Multiply object over a coloured image. The specified fallback fixture covers a gradient and path-count overflow; reopen validation checks structure and text, not this visual result. Define backdrop-aware compositing regions and test overlapping blend groups, clipping and soft masks.

18. **D04 T05 §14: a 200-operation undo limit does not bound memory.** Each operation may retain an entire large subtree or rewritten content stream. Reproduce by repeatedly deleting and restoring a large tagged subtree: history grows by document-sized snapshots despite satisfying the operation-count limit. Add a byte budget for retained history and an explicit oversized-operation policy.

## record: needs-attention

19. **D04 T06 §17 overstates standards validation.** It promises PDF/E and PDF/VT validation while explicitly implementing only document-level rules the engine can check. There is no requirement inventory or coverage status for unimplemented normative requirements. Reproduce with a claimed file satisfying those checks but violating an unsupported requirement: the proposed “valid fixture passes” acceptance permits a conformance verdict. Reserve conformance for complete rule coverage; otherwise report identification plus partial checks.

20. **D04 T07 Goal, §8 and §9 overstate “nothing leaves the machine.”** Referenced paths and output folders are not restricted to local storage. Reproduce with an imported action using an SMB output folder or remote watermark path: ordinary filesystem access transfers data despite no explicit HTTP call. Define whether network filesystems are supported and how destinations are disclosed; otherwise enforce local-only paths and test filesystem-mediated network access.

**Record limitation:** the parity ledger and D19 implementation were not available in the supplied diff, and local reads failed. AI key storage, consent, provider APIs, rich-media/3D ownership and certificate-encryption parity are therefore unassessed, not approved.

OVERALL: needs-attention

## Response to round 1

Every finding was checked against the commit; all 20 were accepted and fixed in the next commit, in the section that owns each:

1. D04 T05 §10: one deck-wide `p:sldSz` under a slide-size policy; pages scale uniformly and mixed sizes are reported.
2. D04 T06 §17: PDF/VT-3 (ISO 16612-3 on PDF/X-6) is identified only; PDF/E-1, PDF/VT-1 and -2 get partial profiles.
3. D04 T06 §5, §9: hairlines use per-segment perpendicular thickness.
4. D04 T02 §14, §15: clipboard reads run on a dedicated thread with a per-format timeout; the UI thread never calls `GetClipboardData`.
5. D04 T06 §10: provisional identification lives in the temp file and is revalidated or stripped before publishing.
6. D04 T06 §15: each exit code has a defined output rule.
7. D04 T02 §15: the clipboard command is always enabled so its refusal is reachable.
8. D04 T07 §5, §9, §14: truncation is fatal for redaction and other declared steps.
9. D04 T02 §14-§16: a list of typed outputs, each built or failed with a reason.
10. D04 T05 §11: OCR for scanned pages is built, not only exposed.
11. D04 T07 §1: `ParamSchema` gains object, list, union and provided kinds.
12. D04 T06 §2, §17: check kinds are a registry with typed parameters.
13. D04 T06 §7: ICC tag payloads are validated with checked arithmetic.
14. D04 T06 §5: TAC is exact for function types 0, 2 and 3; sampling can only prove failure.
15. D04 T07 §7: a file's outputs publish as one transaction with rollback.
16. D04 T07 §6: output paths are resolved and checked against reparse points, before and after creation.
17. D04 T05 §11: raster fallback flattens with its backdrop and reports flattened text.
18. D04 T05 §14: undo history is capped by bytes, not count.
19. D04 T06 §17: "conformant" is reserved; profiles report what was and was not checked.
20. D04 T07: actions are local-only (UNC and remote drives refused); the Goal no longer overstates.
