## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T00:52:26Z
- Effort: medium
- Lines: 45
- Digest: aaff0c80a9fa637e

**Review basis:** supplied candidate diff only. Direct repository reads failed twice because the command runner could not start (`helper_unknown_error`). Cross-checking the foundation plan, design spec and sibling TODOs remains incomplete. No files were modified and no tests ran.

References below use **T01** for `todo/09-cloud/TODO-01-platform-and-accounts.md` and **T02** for `todo/09-cloud/TODO-02-documents-and-sharing.md`.

**source-defect: needs-attention**

1. **T02 §16: the scripting mutation does not enable scripting.** `PDFViewer` takes a `scriptingManager`; the plan deliberately supplies none. Adding `enableScripting: true` to that constructor does not create one, so the required mutation test cannot demonstrate the claimed failure. `maxCanvasPixels` also belongs to `PDFViewer`, not the specified `getDocument` options. Pin the version, use its actual interfaces, and define a mutation that activates the scripting execution path. [Mozilla source](https://raw.githubusercontent.com/mozilla/pdf.js/master/web/pdf_viewer.js)

2. **T01 §9: treating every HTTP 4xx as permanent drops recoverable email deliveries.** SMTP2GO explicitly recommends exponential backoff for HTTP 429. The checkpoint currently rejects that correct behaviour. Distinguish permanent failures from throttling and test both. [SMTP2GO response codes](https://developers.smtp2go.com/docs/response-codes)

**consistency: needs-attention**

3. **T01 §15 versus T02 §§4, 6: account deletion can destroy another account’s bytes.** T01 requires deleting blobs before account rows; T02 deduplicates blobs globally and permits deletion only after the final version reference disappears. A retention hold is not a substitute for checking other accounts’ references. Account deletion must use the same reference-aware purge mechanism, tested with two accounts sharing a hash.

4. **T02 §§4, 6: the sweeper targets active verification work.** Completion changes an upload to `verifying`, but the sweeper deletes staging objects “without an open upload.” That includes the object the queued verifier still needs. Specify protected states, leases and recovery rules; test sweeping while verification is queued and running.

5. **T02 §§3, 7: quota admission contradicts deduplication at capacity.** Creation rejects a declared size that cannot fit, while §7 requires identical bytes to succeed at the limit. The optional client hash cannot establish that the bytes are identical. Define bounded staging admission separately from committed quota accounting and test a full-quota duplicate upload without trusting its claimed hash.

**integration: needs-attention**

6. **T02 §§2, 9 versus T01 §7 and T02 §12: link sessions lack a CSRF-token bootstrap.** Link principals must satisfy session-equivalent CSRF protection, but redemption returns no CSRF token and `/api/session` is specified around account sessions. Anonymous comment/edit links consequently have no defined way to make protected writes. Specify token issuance and validation for link sessions, including simultaneous account and link cookies.

7. **T01 §16: the admin CLI has no usable authentication contract.** Admin endpoints require a session with recent passkey authentication, while the CLI supplies an environment token. The defined desktop OAuth scopes do not provide an admin credential, and `ADMIN_TOKEN_PEPPER` alone defines no issuance or verification mechanism. Specify how the CLI obtains and presents the required authenticated authority without bypassing the passkey requirement.

**adversarial: needs-attention**

8. **T02 §4: commit recovery loses its source before committing durable state.** The prescribed sequence deletes staging before inserting version rows. A crash between those steps leaves a `verifying` upload whose retry cannot hash its source. The existing crash test stops earlier, after copy, and misses this failure. Persist recoverable progress and retain staging until the committed transaction succeeds; inject crashes at every storage/database boundary.

9. **T02 §§3, 4, 7: concurrent commits lack atomic preconditions.** Quota and `baseVersion` checks precede the write batch; merely batching the writes does not make those earlier checks atomic. Two jobs can both observe spare quota or the same base version and then commit. Require conditional database updates or serialization covering the checks and writes, with simultaneous-job tests.

10. **T02 §§3, 4: upload authorization ends too early.** Creation checks access, but part replacement, status, abort, completion and queued commit have no explicit upload-owner authorization contract. Nor does commit require fresh authorization after revocation, suspension or trashing. Specify both session ownership checks and commit-time resource checks; test revocation between upload creation and completion.

11. **T01 §10: the same-device guard is bypassable through the redeem payload.** The missing login cookie is accepted when the request supplies `allowCrossDevice: true`; no server-recorded initiation choice is required. Anyone possessing a token can supply that flag, so it does not prove the stated prior user choice. Bind any exception to the stored login transaction and protect redemption against login CSRF.

12. **T01 §17: paginated backups are not consistent snapshots.** Independently reading tables while writes continue can capture a version without its blob row, inconsistent quota totals, or incompatible account/session state. A trailer hash proves byte integrity, not database consistency. Define a snapshot mechanism and coordinate retained blobs with that snapshot; test restore while commits and purges run concurrently.

13. **T01 §§15, 17: deletion does not cover retained copies.** The plan creates export ZIPs, daily/monthly dumps and a mirror that only copies missing keys, but defines no expiry deletion for exports or deletion propagation for mirrored blobs. Account purge can therefore leave indefinite document copies outside its primary storage checks. Specify retention and deletion for each copy, plus suppression of deleted data during restore.

**record: needs-attention**

14. **T01 §21: a D1 location hint cannot substantiate an EU residency decision.** The deployment instructions pair an EU R2 jurisdiction with a D1 location hint. Cloudflare explicitly says hints provide no location guarantee; D1 jurisdiction must be selected at creation. Record the actual jurisdiction configuration and separately scope backups, mirrors and other data stores before promising regional storage. [Cloudflare D1 data location](https://developers.cloudflare.com/d1/configuration/data-location/)

15. **T02 Goal, Outcome and §19: the blanket WCAG claim exceeds the proposed evidence.** The goal says the whole web app meets WCAG 2.2 AA, while §19 explicitly omits PDF headings, tables and alternative text. Automated scans and a first-page screen-reader walkthrough cannot establish full conformance; an explanatory statement does not cure missing semantics. Scope the claim to demonstrably conforming content or provide a conforming alternative and the necessary evidence. [WCAG conformance requirements](https://www.w3.org/TR/WCAG22/#conformance-reqs)

OVERALL: needs-attention

## Response to round 1

All 15 findings were accepted and fixed in D09 T01 and D09 T02 in the next commit: pdf.js options verified against the pinned typings with a mutation bundle and a dist check (1); email retries on 429 and 5xx with `Retry-After` (2); account purge hands blobs to reference-aware collection (3); the sweeper spares open and verifying uploads and young blobs, with a verify lease (4); quota is enforced at commit with a separate staging allowance (5); link sessions carry their own CSRF token and declared authority (6); admin access uses a PKCE public client with a recent-passkey requirement (7); staging is deleted only after the commit batch, crash-tested at every boundary (8); quota and `baseVersion` are enforced in one guarded batch (9); upload routes and the commit job re-check access (10); cross-device sign-in is decided at start and every auth POST checks Origin (11); backups take a write fence and are tested under concurrent writes (12); export ZIPs expire and deletion tombstones outlive dumps (13); the EU option is set per store and no residency claim goes beyond it (14); the accessibility claim covers the web app's own interface only (15).

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T01:09:37Z
- Effort: medium
- Lines: 33
- Digest: 9accfaf66f41638b

Review limited to the supplied diff and primary web sources: direct repository reads failed twice with `helper_unknown_error`. Foundation/design cross-checking remains incomplete. No files were modified, Git used, or tests run.

T01 and T02 below refer to the two candidate TODO files.

**record: needs-attention**

Round 1 findings **1, 11, 12, 13 and 15 remain incompletely resolved**, as detailed below. Finding **10** also retains an authorization race.

**adversarial: needs-attention**

1. **T01 §10: login CSRF still works through the confirmation page. Round 1 #11.** An attacker starts a cross-device login for their own address, receives the token, then sends the victim `/auth/email/confirm#token=...`. The legitimate confirmation page submits the token with the correct Origin, and the stored cross-device flag bypasses the cookie. Merely displaying the address does not prevent the specified automatic redemption. Require explicit confirmation before creating or replacing a session in this flow, and test navigation to an attacker-supplied confirmation URL, not just a cross-origin POST.

2. **T02 §4: authorization is checked before lengthy storage work, not atomically at commit. Round 1 #10.** A grant or link can be revoked after `resolveAccess` succeeds but before hashing/copying finishes. The specified guarded batch checks quota and `baseVersion`, but not current authorization, suspension, trash state or takedown state. It can therefore commit after revocation. Include these predicates in the commit transaction or an equivalent serialization contract. Test revocation after authorization but before the batch.

3. **T02 §§4, 6: reference-aware collection still races new references.** A collector can observe an old hash with no references, a concurrent upload can reuse that existing blob and commit a version, and the collector can then delete its bytes. The 24-hour protection covers young blobs, not reuse of old ones. Specify coordination between collection and reference creation, including storage deletion outside the database transaction. Test that exact interleaving.

4. **T01 §§15, 17: deletion tombstones are not durably published before purge completes. Round 1 #13.** The external tombstone list is written only by a later mirror run. A disaster after account purge but before that run leaves both the older dump and `tombstones.json` without the deletion, so restoration resurrects the account. Require durable deletion-journal publication before declaring purge complete, with retry-safe ordering and a restore test interrupted at that boundary.

5. **T02 §3: the staging allowance excludes verifying uploads.** The allowance counts only open sessions, while completion moves their potentially large objects into `verifying`. Repeated completion frees admission capacity without freeing storage, allowing queued verification work to exceed the purported staging bound. Count all retained staging reservations until cleanup, and reserve/release capacity atomically. Test repeated completion while the verifier is stalled.

**consistency: needs-attention**

6. **T01 §17: retained database dumps have no corresponding retained blob set. Round 1 #12.** The mirror now propagates primary deletions after 24 hours, while dumps survive for up to 13 months. Restoring an older dump can therefore recover metadata whose document bytes have disappeared from both stores. Marking versions `missing_blob` detects loss; it does not provide the requested recoverable snapshot. Define snapshot-associated blob retention with explicit deletion-policy exceptions, and test restoration after subsequent purges and mirror cleanup.

7. **T01 §17 step 2: restore acceptance criteria contradict deletion replay.** Replaying newer tombstones deliberately removes rows from the restored dump, but the same step requires row counts to equal the dump header and every table’s rows to be equal. A correct deletion-suppressing restore cannot satisfy those checks. Validate dump integrity before replay, then validate the expected transformed state and deletion effects separately.

**source-defect: needs-attention**

8. **T02 §19: the revised WCAG claim still misstates the standard. Round 1 #15.** WCAG conformance applies to full pages; excluding uncontrolled content supports a *statement of partial conformance*, not the prescribed claim that the interface conforms while part of the page is excluded. Downloading an arbitrary untagged or scanned PDF also does not establish a conforming alternative, and download-disabled links make that route unavailable. Use the prescribed partial-conformance approach or demonstrate an actual conforming alternative. [WCAG full-page requirements](https://www.w3.org/TR/WCAG22/#cc2), [third-party partial conformance](https://www.w3.org/TR/WCAG22/#conformance-partial)

9. **T02 §16: the scripting fix still describes `enableScripting` as a constructor option. Round 1 #1.** The mutation now supplies a manager, but still says to pass `enableScripting: true`, contradicting its own requirement to validate option names against typings. Mozilla’s source defines `enableScripting` as a getter derived from the presence of the manager. Specify the mutation entirely through the selected version’s supported manager setup, and identify that version before claiming API verification. [Mozilla PDFViewer source](https://raw.githubusercontent.com/mozilla/pdf.js/master/web/pdf_viewer.js)

OVERALL: needs-attention

## Response to round 2

All 9 findings were accepted and fixed in the next commit: cross-device email sign-in removed, so the login cookie is always required (1); an `access_epoch` bumped by every access-reducing change guards the commit batch (2); blobs are claimed with `deleting_at` before collection and claimed blobs cannot gain references (3); tombstones are published before purge and kept 14 months (4); the staging allowance counts every upload until its staged object is deleted, admitted in one conditional insert (5); the restore promise is narrowed to metadata plus blobs that still exist (6); restore checks run in three stages (dump integrity, tombstone replay with counted removals, final state) (7); the accessibility claim is a WCAG statement of partial conformance for third-party content (8); the pdf.js mutation wires scripting the way the pinned version's reference viewer does and records the verified API in `pdfjs-pin.json` (9).

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T05:08:06Z
- Effort: medium
- Lines: 27
- Digest: 6f09063fc140c2e7

Review basis: supplied diff. Repository reads failed twice with `helper_unknown_error`, so governing-document cross-checking remains incomplete. No files changed, Git used or tests run.

T01 and T02 denote the two candidate files under `todo/09-cloud/`.

**record: needs-attention**

Round 2 **#2 and #3** remain incomplete (findings 1 and 2 below); **#8** still does not deliver the specified WCAG statement (finding 6). Round 1 **#12** retains a snapshot-consistency gap (finding 3). Round 2 **#6** was addressed by withdrawing historical blob recovery, rather than implementing it; the supplied diff does not establish approval of that reduced recovery promise.

**adversarial: needs-attention**

1. **T02 §§1, 4: the authorization epoch does not cover suspension of a collaborating uploader.** The commit guard checks the resource owner’s epoch. Suspending an editor who uploads into another account’s document changes the editor’s status and epoch, leaving the resource owner’s epoch unchanged. A previously authorized job can therefore commit after its principal is suspended. Guard the acting account’s current status as well as the resource owner’s authority. Also require epoch capture and authorization to represent the same snapshot, rather than reading a fresh epoch after an obsolete authorization result. Test both interleavings.

2. **T02 §6 step 3: collector recovery can delete a newly recreated blob.** `deleting_at` excludes new references during one deletion, but does not exclude overlapping recovery workers. Collector A claims a hash and pauses; B resumes that claim, deletes the object and row; an upload recreates the hash and commits; A resumes its storage deletion and destroys the newly referenced bytes. The specified two interleavings do not exercise this case. Require coordination that prevents stale storage deletes across blob generations, and test overlapping collectors with recreation between them.

3. **T01 §17 step 1: an expired fence can produce a successful inconsistent dump.** Writes resume after 60 seconds, but the plan does not require the dumper to abort and discard its output if its fence expires before the last page is read. “Maximum 60 seconds” alone supplies no enforcement or publication condition. Require proof that one uninterrupted fence covered every read before publishing the dump. Test expiry mid-pagination followed by a concurrent commit or purge.

4. **T01 §17 step 2 and T02 §6: restore can resurrect deliberately purged documents.** Deletion replay covers accounts only. If a document is permanently deleted while another account still references its hash, restoring an older dump restores that document’s metadata and finds its bytes intact. It becomes readable again, contradicting the new promise that deliberate deletion outranks recovery. Require durable document/version deletion records replayed during restore, with a test using a shared hash. Blob absence cannot serve as a deletion record.

**consistency: needs-attention**

5. **T02 §13 step 5 versus §21: the deletion dialog promises the wrong grace period.** The account screen explicitly states 14 days, while the owner’s production decision is 30 days. Bind the dialog to the effective configuration and test its displayed deadline against the deletion endpoint’s scheduled purge date.

**source-defect: needs-attention**

6. **T02 §19 step 5: an intention to conform is not the prescribed partial-conformance statement.** The required text says everything else “is intended to conform.” WCAG’s statement requires that the page **would conform** at the specified level if the identified uncontrolled content were removed. The current acceptance test can pass with only aspirational wording and an axe scan. Require that conditional statement and evidence for the remaining page, or describe the text as an accessibility target rather than the standard’s partial-conformance statement. [WCAG 2.2 §5.4](https://www.w3.org/TR/WCAG22/#conformance-partial)

OVERALL: needs-attention

## Response to round 3

1. Fixed: T02 §4 step 2 now reads the resource owner's epoch and the uploading account's own epoch and status before calling `resolveAccess` and records those pre-read epochs (`uploads.access_epoch`, new `uploads.actor_epoch`), and the commit guard also requires the uploader's epoch unchanged and `status = 'active'`; T02 §1 adds `actor_epoch` and makes every suspension bump the suspended account's own epoch; tests added for suspending an uploading collaborator and for a revoke landing on either side of the resolver read.
2. Fixed: T02 §1 adds `blobs.object_key` and T02 §4 stores each blob generation at `blobs/<sha256>/<uploadId>`, so T02 §6 step 3's collector deletes only the key it read from its claimed row and deletes the row only `WHERE object_key = :k`; a stale collector can no longer touch a re-created generation; the overlapping-collector test (A claims and pauses, B finishes, an upload re-creates, A resumes) is required and the sweeper (step 4) now routes unreferenced rows through `collectBlobs` and deletes only objects no row names. The matching wording in D09 T04 (`commitVersion` copying to `blobs/<sha256>`) is outside this record's files and needs the same key change.
3. Fixed: T01 §17 step 1 publishes a dump only by completing its multipart upload after a post-read statement confirms, by the database clock, that its own fence row still exists and is unexpired (only the dumper removes its fence, cleanup removes only expired rows); otherwise it aborts, records `fence_expired` and publishes nothing, with a test that expires the fence mid-pagination and lands a commit and a purge.
4. Fixed: T01 §15 adds `publishTombstone(kind, id)` and `registerTombstoneReplayer` (`tombstones/<kind>/<id>.json`), T02 §6 step 3 publishes `document` and `folder` tombstones before any row deletion and registers its row deletion as the replayer, T01 §16 step 4 publishes `blocked-hash` tombstones so a restore never lifts a takedown, and T01 §17 stage 2 replays every kind (unknown kind fails) with a shared-hash restore test; T01 §16 and §17 now depend on §15.
5. Fixed: T01 §15 step 3 names `ACCOUNT_DELETION_GRACE_DAYS` (default 30, the owner's value), returns `deletionGraceDays` from `GET /api/account` and `purgeAfter` from the delete call; T02 §13 step 5 renders those values with no number in web code and tests 30 days, a 7-day override and the displayed date against the stored `purge_after`; T02 §21 step 1 names the variable.
6. Fixed: T02 §19 context and step 5 now publish the WCAG 2.2 section 5.4 statement verbatim in its conditional form ("do not conform, but would conform ... if the following parts from uncontrolled sources were removed"), forbid "intended to conform" or "built to" wording, and back the claim with evidence for the rest of the page: the axe sweep excludes only `[data-uncontrolled="pdf"]`, with a test that fails if the exclusion reaches the toolbar.
