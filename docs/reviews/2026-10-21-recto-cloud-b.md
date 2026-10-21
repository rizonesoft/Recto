## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T00:54:15Z
- Effort: medium
- Lines: 45
- Digest: 031e024912c341a4

The pasted candidate needs attention. Direct repository reads failed twice with `helper_unknown_error: setup refresh had errors`, so governing-file and checkout verification remain incomplete. Findings below cite the supplied diff. No files were modified, and no git commands or tests ran.

File references below use `T03`, `T04` and `T05` for the three supplied files under `todo/09-cloud/`.

**source-defect: needs-attention**

1. **The PDF pipeline exceeds Workers memory limits by design.** `T04 §1, §14, §16` accept 200 MiB inputs through whole-buffer APIs (`readPdf(bytes)`, `appendUpdate(): Uint8Array`, `sealPdf(bytes)`), retain unsealed intermediates in memory, and permit 256 MiB in the fuzz checkpoint. Workers have a **128 MB per-isolate** limit, shared across concurrent requests. Calling this pipeline “streaming” in §16 does not change its buffer contracts; runtime termination also cannot reliably become the promised typed error. Specify bounded random-access reads and streamed output throughout, or a smaller supported size with proven peak-memory bounds. [Cloudflare limits](https://developers.cloudflare.com/workers/platform/limits/)

2. **The state machine assumes an atomic operation across independent stores.** `T04 §3–4` require a JavaScript-computed audit append transaction and a status write “in the same operation” as Durable Object storage, followed by exactly one Queue enqueue. D1 batches transact within D1; Durable Object transactions cover that object’s storage. Neither supplies the stated cross-store transaction. Define an authoritative commit, durable outbox, idempotent projection and recovery protocol. Test crashes between each write and enqueue, not only a rejected D1 write. [D1 API](https://developers.cloudflare.com/d1/worker-api/d1-database/), [Durable Object storage](https://developers.cloudflare.com/durable-objects/api/sqlite-storage-api/)

**consistency: needs-attention**

3. **Consent and document access form a circular dependency, with an access-code bypass risk.** `T04 §8` forbids document access before consent, but rejects consent until the document preview was fetched. It then introduces a preview exception without defining its endpoint or authorization. Because the access code follows consent, serving that preview would disclose the protected document before checking the code. Define one coherent sequence, including an access-code-protected preview and explicit tests for every document-serving path.

4. **The sealed audit head cannot equal the exported audit head.** `T04 §13` prints the head preceding `envelope.sealed`; §16 instead appends a pending `envelope.sealed` before certification, then appends `envelope.sealed` and `envelope.completed` afterward. Both require the full exported trail’s head to equal the certificate. Those heads differ. Freeze an explicitly numbered audit prefix for sealing, define subsequent events separately, and verify the signed prefix against its authenticated head.

5. **Retention cannot delete the audit rows it promises to delete.** `T04 §3` installs triggers refusing every `DELETE` on `envelope_audit`; §18 purges envelope D1 rows, retaining only a tombstone. No authorized purge mechanism reconciles these requirements. Review deletion is also incomplete: `T03 §3, §6` preserve previous comment text in operation history, while §5 additionally stores it in the Durable Object write-ahead log. Define deletion across every content-bearing store and prevent a delayed flush from restoring purged content.

**integration: needs-attention**

6. **The ceremony cookie will not authenticate the ceremony API.** `T04 §7` sets `Path=/sign`, while §8 serves authenticated endpoints under `/v1/sign/...`. Browsers do not send that cookie to those paths. Align the cookie scope and endpoint namespace, and test with an actual browser cookie jar. [RFC 6265 §5.1.4](https://www.rfc-editor.org/info/rfc6265/)

7. **Desktop undo requires an operation the server rejects.** `T05 §11` requires each local undo to send a matching inverse operation. `T03 §6–7` defines deletion as a tombstone, rejects edits to deleted comments, and supplies no restore operation. Undoing deletion therefore has no defined protocol representation. Specify restoration, authorization and conflict semantics, including preservation of replies and annotation identity.

8. **A valid review can exceed both transport contracts.** `T03 §1` permits 5,000 annotations of 64 KiB each, approximately 312.5 MiB before envelope overhead. §7 requires a full resync snapshot, §9 caps messages at 64 KiB, and `T05 §4` caps JSON responses at 16 MiB. No chunked snapshot protocol or aggregate state limit reconciles these. Define bounded snapshot transfer and test the maximum admitted review, rather than only small-comment fixtures.

**adversarial: needs-attention**

9. **Comment permission becomes permission to redistribute a document.** `T03 §4` allows anyone with `comment` access to create a review and invite new people; `T04 §5` allows the same caller to send the document to signature recipients. These operations grant document access to outsiders. Require an explicit invitation/resharing capability or document-owner authority. Test that a comment-only collaborator cannot expand the audience.

10. **A completed recipient’s signature inputs are not explicitly frozen.** `T04 §4` blocks signing after whole-envelope finalization starts, but §8 does not prohibit an already-completed recipient from changing fields or adopting another signature while other recipients remain pending. §12 stores adoption images at a replaceable recipient-specific key; §16 loads them later. The resulting seal could cover values different from those accepted at that recipient’s signing event. Freeze recipient values and immutable image references atomically with `recipient.signed`, and reject subsequent mutations.

11. **Completion races with void, decline and expiry.** `T04 §4` keeps finalizing envelopes `in_progress`, a state that still permits terminal transitions. §16 independently renders, stores and publishes the completed result without a specified finalization generation or conditional commit through the authoritative Durable Object. A queued job can race a void or expiry. Define which operation wins and test pauses before sealing, storage and publication, with terminal-state changes injected at each point.

12. **Timestamp acceptance omits authentication of the timestamp.** `T04 §14` requires checking response status, nonce and message imprint, but does not require verifying the TSA’s CMS signature, trusted certificate chain or timestamping certificate purpose before accepting the result. A forged response can satisfy those listed checks. Require cryptographic TSA validation in the completion path, with forged-signature and untrusted-TSA tests. Independent validation of fixed golden files does not authenticate production responses. [RFC 3161](https://www.rfc-editor.org/rfc/rfc3161.html)

13. **The exact-upload guarantee has a time-of-check/time-of-use gap.** `T05 §5` rehashes the temporary file immediately before upload, while §1’s transport later opens a file path and streams it. The file can change after hashing or during transmission. Hold a file handle that prevents writes and replacement across verification and upload, or use an equivalent immutable source. The regression must mutate after the hash check and during streaming, not only before the check.

**record: needs-attention**

14. **The operator-blind privacy claim is unsupported.** `T05 §16` says monitoring operates without anyone, “including the operator,” being able to read a document. The design’s Architecture section gives the service access to plaintext PDFs in R2 for rendering, export and sealing. Content-free logs do not prevent an operator with storage or deployment privileges from obtaining those bytes. Narrow the statement to what is actually enforced, or specify a materially different confidentiality architecture.

15. **The standalone hash-chain verifier cannot detect valid-prefix truncation.** `T04 §3` gives `verifyChain(events)` only the supplied events; §17 nevertheless requires truncation detection by that verifier. Removing trailing events leaves a valid chain. Recomputing an entire altered chain also preserves internal consistency. Detection requires an independently authenticated expected head and event count, such as the sealed checkpoint after finding 4 is resolved. Distinguish internal chain consistency from authenticated evidence verification.

OVERALL: needs-attention

## Response to round 1

All 15 findings were accepted and fixed in D09 T03, D09 T04 and D09 T05 in the next commit: `pdf-lite` streams through ranged reads within a 64 MiB heap budget, tested with a 200 MiB fixture (1); the envelope Durable Object's own SQLite is the authority with a transactional outbox, D1 a projection (2); the ceremony order is summary, access code, consent with a static consent-check PDF, then document (3); the sealed checkpoint is `envelope.completing` (4); audit deletes need a purge authorization row written by the purge job (5); the signer cookie is `__Host-recto-sign` with `Path=/` (6); a `restore` operation and one undo table shared by web and desktop (7); review limits fit the transport caps and snapshots are chunked with a digest (8); starting reviews and sending envelopes is owner-only (9); a completed recipient's values are frozen with a hash (10); `envelope.completing` is the commit point that refuses late terminal transitions (11); RFC 3161 tokens are fully verified (12); uploads stream from the locked, hashed handle and the service checks the hash (13); the operator-blind claim is removed (14); `verifyChain` reports consistency only and authenticity needs the sealed checkpoint (15).

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T01:11:02Z
- Effort: medium
- Lines: 33
- Digest: c653edc18d0d87b4

**The candidate needs attention.** Repository reads failed twice with `helper_unknown_error: setup refresh had errors`. These findings cite the supplied diff; governing-file verification remains incomplete. No files were changed, and no git commands or tests ran.

`T03`, `T04` and `T05` refer to the three candidate files under `todo/09-cloud/`.

**record: needs-attention**

Round 1 findings **1, 2, 5, 7, 8, 11 and 15** remain incompletely resolved for the reasons below.

**source-defect: needs-attention**

1. **Purge deletes its own protection against resurrection.** `T03 §3`, `T04 §18`, unresolved **R1 #5**. Both write a Durable Object `purged` marker and then call `deleteAll()`, which removes that marker too. Cancelling an alarm does not cancel an already executing handler. Moreover, envelope purge deletes D1/R2 before stopping the authoritative outbox, allowing a flush to recreate rows during deletion. Preserve a durable deletion fence, stop producers before deleting projections, and make destination writes reject purged generations. Test a flush already past its marker check, object eviction, and delayed queue delivery. [Cloudflare storage API](https://developers.cloudflare.com/durable-objects/api/sqlite-storage-api/)

2. **The outbox still promises exactly-once enqueue without a protocol that provides it.** `T04 §4`, unresolved **R1 #2**. A crash after Queue accepts a message but before the outbox row is deleted causes another enqueue. Consumer deduplication cannot make the queue stub observe only one enqueue. The specified crash tests stop *before* effects and miss this boundary. Require at-least-once dispatch with durable, atomic consumer deduplication, and inject failures after effect acceptance but before acknowledgement recording. Email delivery needs its own explicit treatment of this ambiguity. [Cloudflare Queue delivery guarantees](https://developers.cloudflare.com/queues/reference/delivery-guarantees/)

3. **The memory fix does not establish an isolate-wide bound.** `T04 §1, §14, §16`, unresolved **R1 #1**. A 64 MiB budget per operation permits concurrent operations to exceed the shared 128 MB isolate limit. A Node test under a V8 heap limit also does not establish total Workers memory consumption. Add bounded admission/concurrency and measure simultaneous worst-case operations, including binary buffers. The contracts also remain inconsistent: `readPdf(bytes, ...)` and `commitVersion(bytes, ...)` survive alongside the new ranged/streamed interfaces. Specify those boundaries explicitly so implementation cannot reintroduce whole-file buffering. [Cloudflare Workers limits](https://developers.cloudflare.com/workers/platform/limits/)

**consistency: needs-attention**

4. **The desktop still implements the superseded review protocol.** `T03 §5–7` versus `T05 §11`, unresolved **R1 #7 and #8**. The server replaces `resync` with `snapshot_begin/chunk/end` and adds `restore`; desktop receive handling still lists `resync` and omits `restore`. Desktop limits also remain 5,000 annotations, 64 KiB and 20,000 characters rather than the new limits. Scripted-room tests can pass against this obsolete protocol while actual reconnects and another client's restore fail. Update both sides and test the desktop against the actual server protocol, including chunk loss, digest verification and remote restoration.

5. **The replay path still exceeds the message cap.** `T03 §7`, unresolved **R1 #8**. `welcome` still contains all missed operations for gaps up to 2,000. Just two near-limit operations exceed the 64 KiB socket cap; limiting current annotation state to 8 MiB does not bound historical operations. Stream or paginate replay by encoded byte size, and test a gap below the snapshot threshold containing large operations.

6. **Completion can become permanently stuck after its version is stored.** `T04 §16`, related to **R1 #11**. The pipeline records `sealed_version_id` before calling `finalize(generation)`, but retries with that field set may run only notifications. A crash between those steps leaves the envelope finalizing forever and may notify recipients before authoritative completion. Resume finalization idempotently when the version already exists; notify only after the completed transition commits. Also reconcile §16's message “carrying only the envelope id” with §4's required generation.

**adversarial: needs-attention**

7. **Checkpoint verification labels unauthenticated suffix events authentic.** `T04 §3, §13, §17`, unresolved **R1 #15**. The checkpoint authenticates only events through `envelope.completing`. An attacker can remove subsequent events, or replace them and recompute their hashes, without changing the checkpoint. The specified verifier nevertheless reports `AUTHENTIC` for the export containing that suffix. Return an explicitly authenticated prefix count/head and separately identify the unauthenticated suffix. Test both suffix truncation and suffix replacement; neither can be detected as tampering from this checkpoint alone.

8. **The dead-letter exception reopens the publication race.** `T04 §4, §16`, unresolved **R1 #11**. After completion failure, void is allowed, but the worker still commits a document version before its conditional `finalize(generation)`. A delayed worker can therefore create a library version after void even when finalization is rejected. No generation change on retry is specified either. Keep output unpublished until authoritative acceptance, fence failed/retried attempts with generations, and test an old worker resuming after failure, void and retry.

9. **Individual comment scrubbing still lacks coordination with the room's retained history.** `T03 §6`, unresolved **R1 #5**. The scrub handler explicitly clears D1 comments and operation rows, while its acceptance condition additionally promises removal from the room. It defines no removal of Durable Object WAL payloads, in-memory history, or protection against a pending flush restoring them. Specify the scrub as an authoritative room operation with projection fencing, and test eviction and replay of pending flushes after the seven-day scrub.

OVERALL: needs-attention

## Response to round 2

All 9 findings were accepted and fixed in the next commit: a D1 `purge_fences` table outside the deleted Durable Object storage guards every later write (1); the outbox promise is narrowed to at-least-once with atomic consumer dedupe through `processed_effects` (2); a per-isolate `PdfBudget` and a `pdf-jobs` queue consumer with `max_concurrency` 1 make the memory bound real (3); the desktop (D09 T05 §11) now follows the single protocol in D09 T03 §5-§7 and replays its fixture transcripts (4); `welcome` carries no operations and replay is paged in byte-bounded chunks (5); completion seals into staging and commits through the Durable Object's `accept`, then publishes and notifies (6); checkpoint verification reports an authenticated prefix and an unauthenticated suffix (7); nothing is published before `accept`, which checks the current generation and attempt (8); comment scrub is an authoritative `ReviewRoom.scrub` that clears the write-ahead log and queued flushes before D1, with guarded upserts (9).

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T05:09:22Z
- Effort: medium
- Lines: 33
- Digest: e7c6c9781931ed15

**The candidate needs attention.** Direct repository reads failed twice with `helper_unknown_error: setup refresh had errors`. This review therefore cites the supplied diff; governing-file verification remains incomplete. No files were modified, and no git commands or tests ran.

`T03`, `T04` and `T05` below mean the corresponding files under `todo/09-cloud/`.

**record: needs-attention**

Round 2 findings **#1, #3, #6 and #8** remain incompletely resolved, corresponding to Round 1 **#5, #1 and #11**. Findings 1–4 below identify the remaining defects. Unchanged portions of the earlier fixes could not be verified directly.

**adversarial: needs-attention**

1. **Accepted completion bytes remain replaceable.** `T04 §16`, Build order 2–3. A duplicate delivery can reseal and overwrite the same `generation-attempt.pdf` after the first worker’s `accept` commits but before `publish` copies it. A fresh timestamp can produce different bytes, while publication still uses the first accepted hash. The 48-hour sweep can also delete accepted output awaiting publication. Use immutable staging objects, bind acceptance to the exact object and digest, and retain accepted objects until publication completes. Test duplicate delivery between acceptance and publication, plus publication delayed beyond 48 hours. This leaves **R2 #6/#8** incomplete.

2. **Dead-lettering does not invalidate the failed attempt.** `T04 §4, §16`. `accept` requires `in_progress` and matching generation/attempt, but dead-lettering only appends `envelope.completion_failed`; the attempt increments only when the sender retries. A delayed worker can therefore still satisfy the stated acceptance predicate after failure, before retry or void. Atomically close the attempt on failure and require an active attempt in `accept`. Test a worker resuming immediately after dead-lettering without any sender action. This leaves **R2 #8** incomplete.

3. **The purge fence guards SQL, but not R2 writes.** `T03 §3, §11`; `T04 §16, §18`. An export or publication already executing can finish its R2 write after purge deletes the objects. Its subsequent D1 write being refused does not remove the recreated object. “Every effect consumer is a statement guarded” cannot atomically fence an R2 operation. Define coordination with in-flight object writers and durable cleanup before purge is considered complete. Pause an R2 write or copy before completion, purge, resume it, and verify no content remains. This leaves **R2 #1** incomplete.

4. **In-person hand-over leaves the sender’s authority available.** `T04 §27–29`. The plan adds a recipient cookie and hides sender controls, but never suspends the sender’s existing account session before handing over the browser. The signer can navigate directly to the library or another sender page. Reauthentication after hand-back is too late. Require server-enforced suspension of sender authority during hand-over, with explicit reauthentication to restore it. Test direct account API calls, navigation and another existing tab while the recipient ceremony is active.

5. **Asset sync consent and state are not scoped to an account and server.** `T05 §20`, Build order 1–2, 7. `cloud.syncAssets` survives sign-out, local libraries remain, and one `cloud-sync.json` stores synchronization state without a specified origin/account namespace. Signing into another account or self-hosted server can therefore upload the previous account’s appearances on opening a picker, or apply its revision/deletion state to the wrong account. Bind consent and synchronization metadata to `(origin, accountId)` and require fresh consent when either changes. Test account switching and a copied Portable folder signed into another account.

**source-defect: needs-attention**

6. **The memory contract still has an explicit whole-file escape.** `T04 §1, §17`. Section 1 forbids whole-file buffers throughout `seal`, but §17 still requires `verifySealedPdf(bytes, trustRoots)`. Furthermore, accounting only allocations of at least 64 KiB omits smaller buffers, parsed objects and library allocations; a reservation alone does not bound their actual consumption. Workers’ limit includes both JavaScript heap and binary allocations. Define a ranged verification interface and enforce bounds on all retained memory, with measured simultaneous worst-case verification, parsing and sealing. This leaves **R2 #3** incomplete. [Cloudflare memory limits](https://developers.cloudflare.com/workers/platform/limits/#memory)

**consistency: needs-attention**

7. **Signature activation requires evidence that cannot be produced while signatures are disabled.** `T05 §16, §18`; `T04 §20`. The signature go-live gate requires a completed production signature acceptance run, but §18 permits enabling signatures only after that gate passes. Production envelope creation is refused until enabled. Define a restricted production acceptance mode for the canary account, followed by the public activation gate. As written, completing the gate requires violating its activation order.

8. **The desktop’s zero-upload guarantee cannot hold for the create-time refusal.** `T05 §9`, Build order 4 and 7. Draft creation follows upload, yet step 7 requires zero uploaded bytes when creation returns `503 signatures_not_available`. The health check cannot guarantee this: the switch can change between health and creation. Either introduce an enforceable pre-upload authorization protocol or narrow the guarantee to the preflight refusal and specify what happens to an upload when creation subsequently fails. Test that exact interleaving.

9. **The launch switch is specified at one HTTP route rather than the shared creation boundary.** `T04 §20, §23–25`. The guard explicitly protects `POST /v1/envelopes`, while template instantiation, bulk consumers and form confirmation create envelopes through internal functions. Their tests do not require refusal while signatures are disabled. Enforce the switch in the shared envelope creation/send functions and test every entry point, including queued bulk rows and previously issued form confirmations.

OVERALL: needs-attention

## Response to round 3

1. Fixed: D09 T04 §16 now seals into a per-execution key `sealing/envelopes/<id>/<generation>-<attempt>-<runId>.pdf` (never overwritten, outside the upload `staging/*` prefix), `accept` binds the exact key, SHA-256 and size, `publish` copies only the accepted key after checking its recorded digest and deletes it afterwards, and the 48-hour sweep deletes only keys the Durable Object retired through `retireStaging` (never an accepted, unpublished key), with tests for a duplicate delivery between `accept` and `publish`, publication delayed past 48 hours and the sweep-versus-late-accept race.
2. Fixed: D09 T04 §4 and §16: the dead-letter transaction appends `envelope.completion_failed` and closes the attempt (`attempt_open = false`), `accept` requires an open attempt, a sender retry appends `envelope.completion_retried` and opens a new attempt, both events are added to the §3 event list, and a test resumes a worker immediately after the dead letter with no sender action and expects `409 attempt_closed` and no version.
3. Fixed: D09 T04 §18 (with §12 and §16) and D09 T03 §3 and §12 add R2 coordination: the fence row records the keys and prefixes to empty, every R2 writer checks the fence before and after its put or copy and deletes what it wrote when fenced, review exports move under `exports/reviews/<reviewId>/`, and a durable re-sweep 30 minutes (twice the Queue consumer wall-clock limit) and 24 hours after the fence must find nothing before `completed_at` is set, with pause-and-resume and killed-writer tests.
4. Fixed: D09 T04 §27 now revokes the sender's web session on the server in the request that starts the hand-over (session revocation of D09 T01 §7) and restores authority only through a fresh sign-in, with tests for direct account API calls, navigation and a second tab opened before the hand-over in §27 and §29.
5. Fixed: D09 T05 §20 binds the sync consent to `{origin, accountId}`, keeps sync state per account and server in `cloud-sync\<hash>.json`, asks for fresh consent when either changes, and adds `Cloud.AssetSyncConsentBoundToAccountAndOrigin`, `Cloud.AssetSyncStateNamespacedPerAccount` and `Cloud.AssetSyncCopiedPortableOtherAccountIsSilent`.
6. Fixed: D09 T04 §17 now defines `verifySealedPdf(source: RangeSource, trustRoots)` under the §1 buffer lint, and D09 T04 §1 makes reservations cover all retained memory (every buffer through `allocBuffer`, parsed objects charged by an upper-bound estimate in a 4 MiB capped cache, third-party work only on charged bounded inputs) and measures the simultaneous worst-case seal, verification and parses with `process.memoryUsage()` under 80 MiB plus a staging load test with no `exceededMemory` outcome.
7. Fixed: new D09 T04 §30 adds a restricted canary activation (`SIGNATURES_CANARY_ACCOUNTS`, sends only to `SIGNATURES_CANARY_RECIPIENTS`, public health flag still off), and the stage 2 order is now D09 T05 §21: `go-live --stage signatures-canary`, canary activation, signature acceptance run, then `go-live --stage signatures` and the public switch, with a checkpoint showing the canary stage passes while the public stage still refuses.
8. Fixed: D09 T05 §9 now requires a server admission (`POST /v1/envelopes/admissions`, D09 T04 §30 step 4) before any document byte is sent, states the narrowed guarantee exactly (a switch change after admission refuses the create, creates no envelope and leaves the upload only as an ordinary document in the user's library, reported with `Delete uploaded copy`), and adds `Cloud.SendForSignatureSwitchRaceAfterAdmission` for that interleaving.
9. Fixed: D09 T04 §30 enforces the switch in the single creation function `createEnvelopeDraft` (used by the route, template instantiation, bulk rows and form confirmation, with a lint against other inserts) and in every Durable Object command and consumer, holding queued effects through `holdEffect`, with tests for an existing draft, a queued bulk row, a previously issued form confirmation and an enqueued completion; the switch moved out of D09 T04 §20 so stage 1 no longer waits on that section's legal review.
