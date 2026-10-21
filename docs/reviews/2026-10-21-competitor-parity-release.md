## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T02:08:30Z
- Effort: medium
- Lines: 35
- Digest: c8b46331592d5d7c

Review based on the supplied candidate diff and AGENTS.md. Repository reads failed because the command helper could not start, so cross-file context beyond the supplied material remains unverified. No files were modified and git was not run.

**source-defect: needs-attention**

1. **Printable comments are incorrectly excluded by `NoView`.** `todo/07-release/TODO-01-packaging-and-print.md` §11 requires `NoView` clear when selecting printable comments. `NoView` suppresses screen display, not printing. A comment with `Print=1`, `NoView=1`, `Hidden=0` must still qualify. Add that exact fixture; the proposed Hidden/Link tests miss it. [Adobe PDF reference, annotation flags](https://printtechnologies.org/wp-content/uploads/2020/03/pdfreference_v1-4-1.pdf).

2. **The driver capability checkpoint queries configuration instead of capabilities.** Packaging §16 expects `Get-PrintConfiguration` to list all six supported paper sizes. That cmdlet retrieves configured settings, including the selected paper size, rather than enumerating supported sizes. Query PrintCapabilities and independently verify the default ticket. [Microsoft documentation](https://learn.microsoft.com/ja-jp/powershell/module/printmanagement/get-printconfiguration?view=windowsserver2022-ps).

3. **Policy watching misses absent and recreated keys.** `todo/07-release/TODO-02-store-and-compliance.md` §9 watches the two policy keys directly, but they commonly do not exist at startup. After deletion, the original handle also cannot observe a recreated key. Specify watching the nearest existing ancestor, reopening and rearming, with tests for absent → created → deleted → recreated keys. Otherwise §10’s live appearance/removal guarantee fails. [Microsoft explanation](https://devblogs.microsoft.com/oldnewthing/20200507-00/?p=103733).

**consistency: needs-attention**

4. **The translation checker has mutually incompatible requirements.** Store/compliance §13 requires every language’s exact key set to equal English while also requiring language-specific plural categories. Russian needs categories English does not ordinarily contain. Either define a shared superset of plural keys or compare logical message groups with locale-specific suffixes. Also, numbered placeholders must permit translation-driven reordering while preserving argument identities and types. [CLDR plural rules](https://cldr.unicode.org/index/cldr-spec/plural-rules).

5. **FIPS rules contradict themselves for MD5 signature verification.** `todo/06-quality/TODO-02-security-and-fuzzing.md` §7 refuses MD5, while §8 requires reporting an MD5 signature as verified with a warning. Verification still needs the digest operation. Define an explicit algorithm-by-use policy and its justification, or report verification as unavailable under policy. The proposed signature test checks SHA-1 only and cannot resolve this contradiction.

6. **The print dialog checkpoint demands the wrong count.** Packaging §14 labels its output “N sheets” but requires it to equal the resolved page count. Seven pages at three-up produce three sheets under §10, not seven. Compute the displayed count from the imposition plan and define how duplex sheets versus printed sides are counted.

**integration: needs-attention**

7. **Release preparation requires downstream artifacts before their producers run.** Store/compliance §6 adds a verifier that fails when the policy archive or driver-signing runbook is missing. The archive is produced in §12, which depends on §6; the signing runbook is produced in packaging §20, which also depends on §6. This is an artifact-ordering deadlock even if the declared dependency graph validates. Split documentation/reference checks from final artifact-existence checks and run the latter after production.

8. **Shipping translations can precede localization of errors and counts.** Store/compliance §17 depends on §§14 and 16 but not §15. Its checkpoint can therefore pass while engine errors and count-dependent UI remain assembled in English. Make §15 a prerequisite and include representative localized error and plural flows in the shipped-language checks.

**adversarial: needs-attention**

9. **Virtual printing can delete arbitrary caller-supplied files, including the corpus fixture.** Packaging §17 accepts unrestricted `--spool <file>` and mandates deleting it on success and failure. Its own checkpoint passes `tests/corpus/render/printed-sample.xps` directly, so a conforming implementation deletes that fixture. Separate owned spool jobs from imported files: authenticate job ownership, confine spool cleanup to the private inbox, retain file identity through validation/conversion, and copy corpus inputs into scratch. Test arbitrary paths and reparse-point substitution.

10. **Atomic replacement does not establish “never overwrite.”** Packaging §17 combines collision-free filename selection with temp-file-then-replace, but does not require atomic no-clobber publication. Another job can create the selected name between the existence check and replacement. Require publication that fails if the destination exists and retries a new name. The current sequential two-publish test cannot expose this race.

11. **The FIPS probe explicitly fails open.** Security/fuzzing §7 treats a failed `BCryptGetFipsAlgorithmMode` call as disabled, enabling algorithms that the machine may prohibit. The API distinguishes failure from a successful false result. Preserve an unknown/error state and refuse affected operations until policy is known; test refusal, not merely logging. [Microsoft API contract](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgetfipsalgorithmmode).

12. **The hostile-spool watchdog has no enforceable termination contract.** Packaging §17 promises a 30-second watchdog and 1 GiB ceiling inside the interactive application, but specifies neither comprehensive allocation accounting nor how a non-cooperative parser is stopped safely. A worker thread and `CancelToken` alone do not enforce those limits. Specify bounded parser operations and allocator budgets throughout the call chain, or an explicitly approved process boundary. Test a stalled operation and allocation exhaustion, rather than only one pathological fixture that happens to finish.

OVERALL: needs-attention

## Response to round 1

All 12 findings were verified and fixed in the next commit:

1. D07 T01 §11: `NoView` comments with the Print flag are no longer excluded; a fixture proves it.
2. D07 T01 §16: paper sizes are checked through the queue's PrintCapabilities document.
3. D07 T02 §9: the policy watcher arms on the nearest existing ancestor key and rearms after each notification; `Policy.WatcherKeyLifecycle`.
4. D07 T02 §13: plural groups are one logical key with the CLDR categories per language; numbered placeholders may reorder with the same indices and types.
5. D06 T02 §7, §8: FIPS policy per algorithm and use; MD5 or SHA-1 signatures report `DigestUnavailableUnderFips`; SHA-1 thumbprints stay allowed.
6. D07 T01 §14: sheets and printed sides come from the imposition plan.
7. D07 T02 §6, §7, §12: the runbook check is text only; `verify-release-kit.py --artifacts` checks the produced archive and runbook after they exist.
8. D07 T02 §17: depends on §15; `Localization.ShippedLanguagesErrorsAndPlurals` fails on any English fallback.
9. D07 T01 §17: only owned spools in the private inbox are deleted, through a held handle; `--import` copies and never deletes.
10. D07 T01 §17: publishing uses `MoveFileExW` without replace and retries numbered names; `VirtualPrint.PublishRace`.
11. D06 T02 §7: the FIPS probe returns Enabled, Disabled or Unknown, and Unknown refuses like Enabled; `Fips.ProbeFailureRefuses`.
12. D07 T01 §17: conversion runs in a child `recto-cli.exe virtual-print` in a Job Object with memory and time limits and kill-on-close; `VirtualPrint.WorkerBounds`.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T05:06:04Z
- Effort: medium
- Lines: 21
- Digest: 977eabfe72249aa0

Reviewed the supplied diff. Repository reads failed because the command helper could not start, so governing-document and cross-file consistency remain unverified. No files were modified; git was not run.

**record: needs-attention**

Earlier findings **3 and 12 remain incompletely resolved**, as detailed below. The response’s claim that all 12 are fixed is therefore premature.

**adversarial: needs-attention**

1. **The policy watcher can permanently miss a policy change.** `todo/07-release/TODO-02-store-and-compliance.md` §9 explicitly closes the watch, reads the snapshot, then reopens and rearms. A change between that read and registration leaves a stale snapshot with no pending notification. For example, `AssistantDisabled` can remain unapplied indefinitely. Open and arm before reading, handle key deletion during registration, and repeat reconciliation when signalled. Add a deterministic test inserting a change between snapshot and rearm; the sequential lifecycle test does not expose this. Earlier finding **3** remains incomplete. [Microsoft API contract](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue).

2. **Direct CLI conversion bypasses the specified supervisor.** `todo/07-release/TODO-01-packaging-and-print.md` §17 exposes `recto-cli virtual-print --spool … --import`, but assigns Job Object creation and the timeout exclusively to `Recto.exe`. Running the documented CLI command directly therefore has no specified enforcing parent. Require both public entry points to launch a supervised worker, and test stalled and allocating converters through direct CLI invocation as well as the app. This leaves earlier finding **12** incomplete.

**source-defect: needs-attention**

3. **The Job Object memory limit does not kill the allocating worker.** Packaging §17 claims the operating system stops an allocation-hungry parser and requires `ConversionMemoryLimit`. `JOB_OBJECT_LIMIT_JOB_MEMORY` instead rejects allocations exceeding the committed-memory limit. A worker can catch allocation failure and continue, eventually being classified as a timeout. Specify allocation-failure propagation and parent termination/result handling; test a stub that catches allocation failure and keeps running. `KILL_ON_JOB_CLOSE` does not supply memory-triggered termination. This also affects earlier finding **12**. [Microsoft limit semantics](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_limit_information).

**consistency: advisory**

`todo/06-quality/TODO-02-security-and-fuzzing.md` §7 now defines a three-state probe but retains `Fips.RealProbeReturnsBool` and leaves the JSON representation of `Unknown` unspecified. Align the checkpoint and serialization contract so diagnostic output preserves probe failure instead of presenting it as a successfully observed Boolean state.

OVERALL: needs-attention

## Response to round 2

1. Fixed: D07 T02 §9 watcher now opens the nearest existing ancestor, arms `RegNotifyChangeKeyValue`, re-resolves (restarting if a deeper key appeared or the key was deleted during registration) and only then reads and publishes the snapshot, so a change during the read signals the armed event; new `Policy.WatcherNoLostUpdate` writes `AssistantDisabled` from `after_open`, `after_arm` and `after_read` test hooks (plus key creation and deletion) and fails for a read-before-arm watcher; added to Build order step 2 and the Test checkpoint.
2. Fixed: D07 T01 §17 makes both public entry points (`Recto.exe /virtual-print` and `recto-cli virtual-print`) supervisors through one `VirtualPrintSupervisor`, the conversion runs only in `recto-cli virtual-print --worker` (an internal flag, so the `D04 T08 §3` verb registry is unchanged), the worker refuses to run outside a job with the expected limits (`VirtualPrint.WorkerRefusesUnsupervised`), and `VirtualPrint.CliWorkerBounds` repeats the stalled and allocating stubs through a direct CLI invocation (exit codes `6` and `5`); the Test checkpoint command now passes `--import` so the corpus file is not treated as an owned spool.
3. Fixed: D07 T01 §17 states that `JOB_OBJECT_LIMIT_JOB_MEMORY` only fails commits, adds a guaranteed `JobObjectNotificationLimitInformation` memory notification at `960 MiB` on a completion port (the `JOB_OBJECT_MSG_JOB_MEMORY_LIMIT` message is best effort per Microsoft Learn), and makes the supervisor call `TerminateJobObject` and return `ConversionMemoryLimit` on it; the worker maps allocation failure to `OutOfMemory` and exit `5`, and `VirtualPrint.WorkerBounds` uses an `allocate-and-swallow` stub that catches every allocation failure and keeps looping, which must end as `ConversionMemoryLimit` in under `5 s`, not as a timeout.
4. Fixed (consistency advisory): D06 T02 §7 renames the checkpoint test to `Fips.RealProbeReturnsState` and serialises `fips_mode` as `"enabled"`, `"disabled"` or `"unknown"` with `fips_probe_status` (the failing `NTSTATUS`, else `null`), tested for all three states by `Fips.CliInfoReportsMode`; D06 T02 §9 now requires `fips_mode` to be `"enabled"` instead of true.
5. Record: the round 1 claim is corrected here; earlier findings 3 and 12 are completed by items 1, 2 and 3 above.
