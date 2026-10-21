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

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T00:56:41Z
- Effort: medium
- Lines: 27
- Digest: 21f745d8b55f86d7

Review basis: the supplied diff. Both repository-read attempts failed before PowerShell started (`helper_unknown_error: setup refresh had errors`), so surrounding repository contracts remain unverified. No files were modified; no Git commands or tests ran.

**record: needs-attention**

The claim that all 20 findings are resolved is unsupported. Round 1 findings **4, 8, 14, 15, 16, 19 and 20** remain incompletely resolved, as detailed below.

**source-defect: needs-attention**

1. **T06 §5: the replacement TAC algorithm still permits false passes. Round 1 #14 remains unresolved.** A Type 2 function’s component sum need not remain monotone after output clipping. For example, use `N=1`, `Domain=[0,1]`, `C0=[0,2,0,0]`, `C1=[2,0,0,0]` and component ranges `[0,1]`. Both endpoints produce 100% TAC, but the midpoint produces 200%. Endpoint-only checking therefore passes a 150% limit incorrectly. Type 0 also supports cubic interpolation, so the blanket “linear interpolation” assumption is unjustified. Require bounds that account for clipping and interpolation order, or return `Incomplete`; add this interior-maximum fixture. [Adobe PDF Reference, §3.9](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.3.pdf).

2. **T02 §14: the five-second timeout does not define a viable acquisition-thread lifetime. Round 1 #4 remains partially unresolved.** Moving acquisition off the UI thread fixes responsiveness, but `GetClipboardData` can remain blocked beyond five seconds. Its delayed-rendering timeout is controlled by Windows and is not configurable. Returning `ClipboardTimeout` cannot simultaneously end the acquisition lifetime as specified. Define ownership of the still-running call, late-result disposal, eventual clipboard closure, bounded concurrent requests and shutdown behavior. Test cancellation followed immediately by another paste and window closure. [Microsoft’s delayed-rendering timeout explanation](https://devblogs.microsoft.com/oldnewthing/20220609-00/?p=106731).

3. **T06 §1 and §5: the shear fixture cannot satisfy its required verdict.** The fixture specifies a `0.5 w` stroke under `[1 0 0.9 1 0 0]`, and §5 requires it to be flagged against the default `0.25 pt` minimum. Using the candidate’s own formula, this matrix’s maximum directional stretch is approximately `1.547`; its determinant is `1`, so the minimum possible thickness is approximately `0.323 pt`. No segment direction makes that stroke a hairline at this threshold. Change the fixture width or shear and explicitly specify its direction and expected thickness.

**consistency: needs-attention**

4. **T07 §6: containment checks still leave a publication race. Round 1 #16 remains unresolved.** The final-path check happens after temp-file creation, while publication later uses path-based `MoveFileExW`. Replace a destination ancestor after the check but before publication and the destination can resolve outside the approved root. No requirement pins directory identities through publication. Moreover, detecting escape after creation does not establish the test’s claim that nothing was written outside the root. Require containment-preserving directory ownership or handle-relative operations throughout creation, publication and rollback, with fault injection after the final-path check.

5. **T07 §6 and §14: the local-only policy checks path spelling and drive type, not the actual access target. Round 1 #20 remains unresolved.** A parameter such as `C:\assets\stamp.png` can traverse a local symbolic link to an SMB target while passing the enumerated UNC and `DRIVE_REMOTE` checks. The parameter-path policy specifies no corresponding reparse-point rejection before access. Test a local-looking stamp path whose ancestor redirects to a remote share, and require refusal before following that redirection. Windows symbolic links transparently redirect filesystem operations. [Microsoft symbolic-link documentation](https://learn.microsoft.com/en-us/windows/win32/fileio/symbolic-links).

6. **T07 Goal, §5 and §14: the explicit redaction exception still contradicts the unconditional guarantee and verification. Round 1 #8 remains partially unresolved.** Section 5 permits `skip_with_warning` to succeed without searching a page. The Goal still says a redaction step that could not see a page fails the file; the post-apply verification requires complete coverage; and §14 requires complete redaction or failure with no output. A scanned page explicitly skipped under this option cannot satisfy all these contracts. Define the exception consistently, including the post-verification scope and reported outcome, and separately test default fail-closed behavior and explicit skipped-page behavior.

7. **T07 §7 versus §9: rollback semantics contradict both failure isolation and cancellation. Round 1 #15 remains partially unresolved.** Section 7 permits `Failed` with retained outputs when rollback deletion fails, whereas §9 still requires a failing file to “never leave[] a partial output.” Section 7 also rolls back a set cancelled during publication, whereas §9 says stopping keeps outputs already published. Cancelling after image four produces incompatible required outcomes. Distinguish completed output sets from an in-progress set, and carry the retained-output exception into the runner contract and cancellation tests.

8. **T06 §17: the inventory test rejects the status explicitly required elsewhere in the same section. Round 1 #19 remains partially unresolved.** Valid fixtures must report `PartialChecksPassed` despite unimplemented requirements, but `Preflight.PdfEVtInventory` must fail when a profile “claims a pass status” while its inventory contains an unimplemented entry. `PartialChecksPassed` is such a status unless the test explicitly excludes it. Define the exact allowed status matrix: partial checks may pass while full conformance remains unclaimed. Also replace “every normative requirement the implementer read” with an authoritative requirement inventory; otherwise omitted reading can make the inventory appear complete.

OVERALL: needs-attention

## Response to round 2

All 8 findings were accepted and fixed in the next commit:

1. D04 T06 §5: a TAC maximum is accepted only where provable (Type 2 with endpoints in range, Type 3 of provable parts, Type 0 Order 1); everything else can only fail or be `Incomplete`.
2. D04 T02 §14: one long-lived clipboard thread; a blocked call stays outstanding, new requests get `ClipboardBusy`, and shutdown has a bounded wait.
3. D04 T06 §1, §5: the shear fixture is now `1 0 3 1 0 0` with verdicts per stroke direction.
4. D04 T07 §6: publication goes through held directory handles with identity rechecks; no path is re-resolved.
5. D04 T07 §6, §14: local-only is decided on the opened target's final path after a reparse-point walk.
6. D04 T07 Goal, §5, §14: `skip_with_warning` is the single, reported redaction exception, unavailable for `Truncated` pages.
7. D04 T07 §9: stop keeps completed sets and rolls back the set in progress; retained outputs are listed and the file stays `Failed`.
8. D04 T06 §17: an exact status matrix; `Conformant` needs a complete, second-read inventory.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T01:07:04Z
- Effort: medium
- Lines: 19
- Digest: fbf35b59aa16e689

**record: needs-attention**

Round 1 **#14** / Round 2 **#1**, and Round 1 **#20** / Round 2 **#5**, remain incompletely resolved. Findings below explain why.

Review limitation: both repository-read attempts failed before PowerShell started (`helper_unknown_error: setup refresh had errors`). This review uses the supplied diff and primary documentation. Surrounding repository contracts remain unverified. No files were modified; no Git commands or tests ran.

**source-defect: needs-attention**

1. **T06 §5: the Type 2 TAC proof still permits false passes.** `C0` and `C1` describe function values at inputs zero and one, not necessarily the actual domain endpoints. Consider a CMYK axial shading and function with `/Domain [0 4]`, `N=1`, `C0=[0 1 0 0]`, `C1=[1 0.75 0 0]`, and component ranges `[0 1]`. Both coefficient arrays satisfy the new eligibility rule. Nevertheless, after clipping, TAC is 100% at both domain endpoints and 175% at `x=1`. Endpoint checking incorrectly passes a 150% limit. The candidate’s new tests do not exercise this case. Prove monotonicity and absence of clipping over the actual evaluated domain, including stitching mappings, or return `Incomplete`. Add this fixture. This leaves Round 1 #14 and Round 2 #1 unresolved. [ISO 32000-1, §7.10.3](https://opensource.adobe.com/dc-acrobat-sdk-docs/standards/pdfstandards/pdf/PDF32000_2008.pdf?from=20423&from_column=20423).

**adversarial: needs-attention**

2. **T07 §6 and §14: the local-target check still races with parameter access.** The policy walks components, “then open[s] the target,” and runs at import, save and run start. Unlike output publication, it does not require retained parent handles or require step implementations to consume the checked target handle. Replace `C:\assets` with an SMB symlink after its inspection but before opening the stamp, or after run-start validation but before the watermark step reads it. A subsequent path-based open follows the replacement; checking its final path afterward cannot undo remote access. The static symlink fixture passes without testing either race. Require handle-relative acquisition through retained, checked parents and consumption of that same opened object, with fault injection between validation and use. This leaves Round 1 #20 and Round 2 #5 unresolved. [Microsoft’s `NtCreateFile` contract](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile).

**consistency: needs-attention**

3. **T07 §6: an output root that is a junction is simultaneously supported and forbidden.** The containment bullet explicitly permits a root that is itself a junction. The revised local-only bullet rejects any symbolic link or mount point at an ancestor **or leaf**, explicitly including output folders. A junction to another local directory therefore must both be accepted and refused. Choose one policy and align both bullets and their tests. If local junction roots remain supported, specify how their targets are resolved safely and retained without following a remote redirection.

OVERALL: needs-attention

## Response to round 3

All 3 findings were accepted and fixed in the next commit. This was the last allowed round:

1. D04 T06 §1, §5: a Type 2 maximum is accepted only with `/Domain [0 1]`, `N > 0` and `C0`, `C1` inside `/Range`; Type 3 parts only with `/Encode [0 1]` or `[1 0]`; Type 0 only when `/Encode` spans the sample grid; anything else is sampled and can only be `Failed` or `Incomplete`. The reviewer's `/Domain [0 4]` case is a fixture.
2. D04 T07 §3, §6, §14: parameter files are opened once through `CheckedOpen` (retained parent handles, `FILE_OPEN_REPARSE_POINT`, no `FILE_SHARE_DELETE`, final path local) and steps read them only from that handle; earlier checks are advisory. `ActionNaming.CheckedOpenRace` swaps a folder for an SMB symlink after each check.
3. D04 T07 §6: one rule: a symlink or junction output root is refused, and the root is held through `CheckedOpen` for the run; all bullets and tests agree.
