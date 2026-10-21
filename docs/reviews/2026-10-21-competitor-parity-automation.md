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
