## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T02:03:58Z
- Effort: medium
- Lines: 33
- Digest: 0129a55c615fc733

Review is based on the supplied candidate diff. Local file reads failed because the command runner could not start, so cross-file contracts beyond the supplied AGENTS.md remain unverified. No files were modified and no git commands ran.

**source-defect: needs-attention**

1. **Job memory limits do not implement the specified kill behavior.** `todo/04-convert/TODO-08-automation-mcp.md` §8 requires `McpCall.JobLimits` to kill a child allocating past its limit, but specifies only `JOB_OBJECT_LIMIT_PROCESS_MEMORY`. That flag makes excess allocation fail; it does not automatically terminate the process. A stub that handles allocation failure can continue and return success. Specify detection, termination and error reporting, and test a child that survives allocation failure. [Microsoft’s limit semantics](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_limit_information).

**consistency: needs-attention**

2. **The result privacy contract contradicts the exposed tools.** `todo/04-convert/TODO-08-automation-mcp.md` Outcome and Verification prohibit *any file content* in results, while §§3, 8 and 11 explicitly expose extracted document text through `data` and `structuredContent`. An executor cannot satisfy both. Distinguish authorized document content in results from content prohibited in logs, and define the secret-handling boundary separately.

3. **Blank-page outcomes are contradictory.** `todo/04-convert/TODO-07-action-wizard.md` §18 defines `on_all_blank = skip_file|fail`, but mandates `Skipped` whenever every page is blank. The default 50-percent deletion guard also implies failure before that outcome. Specify precedence and implement both policy values; test an all-blank document under each value and with the default guard.

**integration: needs-attention**

4. **New output files cannot pass the specified path check.** `todo/04-convert/TODO-08-automation-mcp.md` §6 requires opening *every* `Path` argument and checking its final handle path. An `OutputFile` that does not exist cannot be opened this way, although creating new outputs is the primary contract. Define separate output resolution: validate and retain the existing parent directory handle, validate the leaf name, then publish exclusively. Add a successful test targeting a nonexistent filename.

5. **The print interface cannot carry all specified settings.** `todo/04-convert/TODO-07-action-wizard.md` §22 exposes a `paper` parameter but omits it from `PrintRequest`. It also requires sending only changed settings without defining how the request distinguishes an unset setting from an explicit value equal to the default. Add paper and explicit optional/override semantics to the interface, with adapter tests.

6. **The insertion option is absent from its parameter schema.** `todo/04-convert/TODO-07-action-wizard.md` §19 branches on `keep_source_bookmarks`, but never declares that parameter or its default. The generated editor cannot offer it and schema validation cannot reliably accept it. Define the parameter and test both values, including bookmark destination remapping after insertion.

**adversarial: needs-attention**

7. **Print confirmation budgets the wrong document.** `todo/04-convert/TODO-07-action-wizard.md` §23 counts original input pages before execution, while §§19 and 22 allow insertion before printing the final scratch PDF. A one-page input can therefore print hundreds of pages after the user confirms one. Prepare the transformed documents before confirming, or calculate and enforce an authoritative final print plan before any spool submission. Test `insert_pdf -> print`, page deletion, ranges and copies.

8. **Top-level path staging does not confine transitive reads.** `todo/04-convert/TODO-08-automation-mcp.md` §§3, 6 and 8 expose HTML/Markdown conversion and profile-driven operations, but confine only declared CLI arguments. A staged HTML file can still reference an absolute local image outside allowed roots; a profile can contain additional paths. The specified job object supplies resource limits, not filesystem isolation. Require a confined resource resolver or exclude affected verbs until all transitive reads are mediated. Test an allowed input referencing a forbidden local canary. [Microsoft distinguishes job limits from process security](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects).

9. **The sample test accepts the exact failure it claims to catch.** `todo/04-convert/TODO-08-automation-mcp.md` §2 accepts any exit code listed by a verb, yet expects a misspelled flag to fail `Cli.VerbsSamples`. Usage exit `1` is a legitimate listed code, so the malformed sample passes. Give each sample an expected exit code and observable result assertions.

10. **Resource limits leave disk usage and queued requests unbounded.** `todo/04-convert/TODO-08-automation-mcp.md` §§6 and 8 check output bytes/file count at publication and limit only running children. A child can fill staging storage before publication, and an unlimited queue of near-4-MiB requests can exhaust server memory. Define runtime staging limits and bounded queue admission; test an ongoing disk-writing child and request flooding while all execution slots are occupied.

11. **The dispatcher contract lacks valid invocation construction.** `todo/01-engine/TODO-03-cli-and-fuzz.md` §10 probes every PDF-taking verb with broken/encrypted inputs, but `VerbEntry` supplies no valid argument template for required subverbs, output arguments or additional inputs. Such invocations can correctly return usage exit `1` before opening the PDF, making the mandated `2`/`3` checks unusable for later verbs. Add per-verb contract fixtures that first prove a valid invocation succeeds, then replace only the input under test.

OVERALL: needs-attention

## Response to finding 11 (D01 T03)

11. D01 T03 §10: each verb registers at least one complete valid invocation with input and output placeholders; registration refuses verbs without one; the contract test proves the valid invocation succeeds before swapping in broken (exit 2) and encrypted (exit 3) inputs.

## Response to round 1 (D04 T07, D04 T08)

All 10 remaining findings were verified and fixed in the next commit:

1. D04 T08 §8: a Job Object with an I/O completion port and a peak-memory watchdog; any call that hit the limit is `memory_limit_exceeded` whatever the exit code.
2. D04 T08 Outcome, Verification: secrets are barred from logs, arguments and results; file content from logs only; results are marked `content_origin: "document"`.
3. D04 T07 §18: `on_all_blank` takes precedence over the 50 percent guard; `ActionCleanup.AllBlank`.
4. D04 T08 §6: output arguments resolve through a retained parent handle and a validated leaf name; `McpPolicy.OutputResolution`.
5. D04 T07 §22: `PrintRequest` uses optional overrides including `paper`.
6. D04 T07 §19: `keep_source_bookmarks` is declared in the schema with tests of both values.
7. D04 T07 §22, §23: printing is two-phase with a plan counted from the final scratch PDFs and re-checked before spooling; `ActionPrint.TwoPhase`.
8. D04 T08 §1, §3, §12: verbs that read external resources (`office-to-pdf`, `html-to-pdf`, `md-to-pdf`) are never exposed; `McpGates.NoTransitiveReads`.
9. D04 T08 §1, §2: each sample has an `expected_exit` and result assertions.
10. D04 T08 §6, §8: a staging quota (4 GiB) and a bounded queue (16 calls, 32 MiB) with `server_busy`.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T05:03:42Z
- Effort: medium
- Lines: 27
- Digest: 02dcaab04cf7e7ee

Review is limited to the supplied diff. Repository reads failed because the command runner could not start, so cross-file contracts remain unverified. No files were modified and no git commands ran.

**record: needs-attention**

Round 1 findings **1 and 10 remain incompletely resolved**, as detailed below. Finding **4’s fix introduces an unresolved `OutputDir` case**. Resolution of finding **8’s profile-driven reads cannot be verified from the supplied changes**, whose new gate exercises only HTML, Markdown and Office inputs.

**source-defect: needs-attention**

1. **Memory-limit detection still permits success after a rejected allocation.** `todo/04-convert/TODO-08-automation-mcp.md` §8. Delivery of `JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT` is not guaranteed. The fallback measures peak working set, whereas `JOB_OBJECT_LIMIT_PROCESS_MEMORY` limits committed memory. A child can attempt an oversized commitment, handle the failure and exit `0` without its working set reaching the threshold. Consequently, the new mechanisms cannot guarantee the promised `memory_limit_exceeded` result. Define a reliable failure-reporting contract for rejected allocations and settle it before accepting success or publishing. Test a failed large commitment with low resident memory and an absent completion notification. [Microsoft notification guarantees](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_associate_completion_port), [memory-limit semantics](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_limit_information).

**consistency: needs-attention**

2. **Output directories are treated as filename leaves without defining how they become validated publication directories.** `todo/04-convert/TODO-08-automation-mcp.md` §6. The replacement rule validates only the parent of an `OutputDir`, then publishes “into the requested folder.” For `C:\Allowed\Results`, it checks `C:\Allowed` but never requires opening and validating `Results` if it already exists, or securely creating it if absent. An existing `Results` junction can therefore redirect publication outside the allowed root under the specified checks. Separate `OutputFile` leaf resolution from `OutputDir` resolution, and require publication relative to the retained, validated destination-directory handle. Test an existing junction, a newly created directory and replacement of the destination during publication.

**adversarial: needs-attention**

3. **The staging watchdog is not an enforceable disk quota.** `todo/04-convert/TODO-08-automation-mcp.md` §§6, 8, 12. Sampling directory size every `250 ms` allows unrestricted writes between samples. A child can exhaust remaining volume space before termination, or create and remove oversized temporary data between samples without detection. The test only proves eventual termination of a continuously growing writer; it does not prove the stated storage bound. Enforce the bound at allocation/write time through bounded storage or mediated writes, including input staging and temporary files. Test burst writes, write-then-delete activity and concurrent calls on a nearly full volume. Round 1 finding **10’s disk-exhaustion concern remains open**.

**consistency: advisory**

4. **The secret boundary remains ambiguous for document-derived results.** `todo/04-convert/TODO-08-automation-mcp.md` Outcome, §8 and Verification. Allowing document text while prohibiting every “password or token” in results does not distinguish supplied authentication secrets from passwords or tokens that happen to occur in the document. Define the prohibition in terms of supplied secrets and credential sources, and explicitly state how authorized document content is treated. The content marker alone does not establish that boundary.

**record: advisory**

5. **The transitive-read gate does not cover the profile half of Round 1 finding 8.** `todo/04-convert/TODO-08-automation-mcp.md` §§1, 3, 12. The new exclusion flag provides a mechanism, but the added test covers only three document formats. Add a profile-bearing exposed invocation whose profile references a forbidden canary, and require either mediated access or exclusion. Without the surrounding profile contracts, I cannot establish whether this is an exploitable remaining path.

OVERALL: needs-attention

## Response to round 2

1. Fixed: D04 T08 §8 replaces the working-set watchdog with a child contract that does not depend on notifications or resident memory: `install_child_contract()` (`src/cli/child_contract.cpp`) installs a new handler and routes every `ErrorCode::OutOfMemory` through `out_of_memory_error()`, both setting an inherited `RECTO_MCP_OOM_EVENT` that the server waits on, so the call ends as `memory_limit_exceeded` with nothing published whatever the exit code; the job message and `PeakProcessMemoryUsed` (commit) are extra triggers only; `McpCall.MemoryLimit` tests a failed `new (std::nothrow)` of twice the limit under `64 MiB` working set with every completion-port message dropped, plus §9 stub mode `memory-quiet` and §12 `McpGates.LimitsHold`.
2. Fixed: D04 T08 §6 now resolves `OutputFile` (retained parent handle plus validated leaf) and `OutputDir` (the folder itself opened through `CheckedOpen` with no reparse point at the leaf, or created handle-relative with `FILE_CREATE` and `FILE_OPEN_REPARSE_POINT`) separately, holds the publication handle without `FILE_SHARE_DELETE` with an identity re-check, and publishes only into that handle; `McpPolicy.OutputDirResolution` tests an existing junction, a newly created folder and a rename-and-junction swap during publication.
3. Fixed: D04 T08 §6 and §8 replace the sampled folder quota with a write-time budget: staged inputs are charged before the copy, and `recto::core::WriteBudget` (charged by `AtomicFileWriter::write`, the `OutputPublisher` writer and the WIC stream adapter before each `WriteFile`, counting bytes written so deletes refund nothing) fails the write with `write_budget_exceeded`; a guaranteed `JOB_OBJECT_LIMIT_JOB_WRITE_BYTES` notification limit is the backstop for writes outside Recto's write path, and an admission-time disk reservation (`min_free_bytes`, `insufficient_disk_space`) covers concurrent calls; `McpCall.StagingBudget` and `McpCall.DiskReservation` test a burst, write-then-delete cycles, a raw write and four calls on a nearly full volume, and §12 `McpGates.LimitsHold` includes them.
4. Fixed: D04 T08 Outcome, §8 result shaping, §12 `McpGates.NoSecretsAnywhere` and Verification define a secret as a `Secret` argument value or a credential the server or child holds, and state that document-derived text is returned unredacted and unscanned; the gate now asserts a document's own `password: <doc-canary>` text comes back unchanged while the `Secret` canary is absent, and fails on a masking filter.
5. Fixed: D04 T08 §1 adds `external_refs` (`None` with a reason, or `Follows`, which forces `reads_external_resources` and `Never`) to every `InputFile`; §3 declares it for preflight and redaction profiles, redaction marks, forms data, e-invoice XML and image, SVG and XPS inputs with a canary template per role and format (`VerbSpecs.ExternalRefs`); §12 `McpGates.NoTransitiveReads` runs each exposed profile- or data-bearing role with its canary and fails when a role has no template or a preflight loader resolves `base` as a path.
