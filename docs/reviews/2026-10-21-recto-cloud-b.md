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
