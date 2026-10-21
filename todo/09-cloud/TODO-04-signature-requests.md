---
schema_version: 1
id: cloud-signatures
domain: 09-cloud
status: active
title: "TODO-04 -- Recto Cloud Signature Requests, Audit Trail and Seal"
---

# TODO-04 -- Recto Cloud Signature Requests, Audit Trail and Seal

> **Goal:** A signed-in Recto Cloud user sends a stored PDF to other people for signature, approval or copy, in sequential or parallel order, with fields placed on the pages. Each signer, who needs no account, opens a single-use expiring link, accepts the electronic records disclosure, adopts a signature, fills the fields and signs or declines. The service keeps an append-only, hash-chained audit trail, and when the last signer finishes, a Queue job renders every value into the PDF, adds a certificate of completion, seals the result with a PAdES-B-T signature and an RFC 3161 timestamp, stores it as a new version and tells every party. This matches Acrobat Pro's Request Signatures with simple electronic signatures. It does not claim advanced or qualified electronic signatures; those stay in the desktop signer (`D05 T01 §8`).

> **Current state:** Measured 2026-10-21. No `cloud/` code exists. `todo/09-cloud/TODO-01` (platform, accounts, storage, email, CI) and `TODO-02` (documents, sharing, web viewer) are being authored beside this file. The desktop PAdES signer is planned in `todo/05-organize/TODO-01-page-operations.md` section 8 and is not built. `docs/specs/2026-10-21-recto-cloud-design.md` fixes the design: an `Envelope` Durable Object per request serialises the state machine; the audit trail is an append-only table where each event stores the SHA-256 of the previous event; the final chain head is embedded in the certificate of completion and covered by the PAdES seal; the seal key never sits in a Worker secret in production, and staging seals with a test certificate that validators show as untrusted. The seal certificate and the legal texts are open third-party decisions, and retention defaults are an open owner decision.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` decisions D9, D16, D18, D20 and section 3.2 (the `cloud/` correction of 2026-10-21).
- `docs/specs/2026-10-21-recto-cloud-design.md` sections Decisions (Signatures), Open owner decisions, Architecture (Signature requests, Audit trail, Completion, Email), Security and privacy, Testing.
- `todo/05-organize/TODO-01-page-operations.md` section 8 (the desktop `PAdES` signer whose signature dictionary conventions the seal follows).
- `design/SPEC.md` sections 3 (brand), 4 (colour), 5 (type), 7 (shape) and 9.9 (dialogs and system states).
- ESIGN Act (15 U.S.C. 7001 and following, especially 7001(c) consumer consent), UETA, eIDAS Regulation (EU) 910/2014 for simple electronic signatures, ETSI EN 319 142-1 (PAdES baseline), RFC 3161 (time-stamp protocol), RFC 5652 (CMS), RFC 8785 (JSON canonicalisation), ISO 32000-1 sections 12.8 and 7.5.6 (digital signatures, incremental updates).
- -> XREF: D09 T02 §4 -- stores each sealed result through `commitVersion` (D09 T02 §4).
- -> XREF: D09 T01 §20 -- runs in the CI workflow of D09 T01 §20.
- -> XREF: D09 T02 §21 -- records its defaults in the owner batch of D09 T02 §21.
- -> XREF: D09 T03 §11 -- `pdf-lite` also serves the reviewed-PDF export (D09 T03 §11).
- -> XREF: D09 T05 §9 -- the desktop Send for signature creates envelopes through this API.
- -> XREF: D05 T01 §8 -- the cloud seal follows the desktop signer's signature dictionary conventions; advanced and qualified signatures stay on the desktop.
- -> XREF: D09 T01 §15 -- export and deletion include templates.
- -> XREF: D09 T01 §8 -- rate-limit policies.
- -> XREF: D09 T01 §9 -- email kinds.
- -> XREF: D09 T01 §7 -- recent authentication.
- -> XREF: D09 T02 §13 -- web shell.
- -> XREF: D09 T02 §12 -- design tokens.
- -> XREF: D09 T05 §9 -- in-person option.

## Outcome

- `cloud/packages/pdf-lite` reads hostile PDFs within hard limits and appends incremental updates, and `cloud/packages/audit-chain` appends and verifies a SHA-256 hash chain of events.
- An `Envelope` Durable Object enforces the draft, sent, in progress, completed, declined, voided and expired state machine, and an API plus a field placement UI let a sender prepare and send an envelope with signer, approver and copy recipients.
- Signers complete a consent, identity, adopt, fill and sign ceremony through single-use links, with an optional access code, a decline path, Cron reminders and expiry, and sender void.
- A Queue job produces a sealed PDF with a certificate of completion, a PAdES-B-T signature and an RFC 3161 timestamp, validated by an independent validator in CI, with tamper tests that detect any change to the bytes or the audit chain.
- Senders can reuse templates, send one template to a CSV of recipients, publish a web form link that makes one envelope per submitter, and hand a device to a signer in person, all owner-only, audited and rate limited.
- Retention, legal hold, export and deletion follow the spec, the seal key is behind a signing-service interface, and no screen or document claims advanced or qualified signatures.

**Adjacency:** list=applicable; document=applicable @ D09 T02 §4; settings=applicable; reporting=applicable; notifications=applicable; permissions=applicable @ D09 T02 §2; audit=applicable; exchange=applicable; reverse=applicable

**Adjacency rationale:** The envelope list is the sender's dashboard filtered by status. A request pins one immutable document version and, on completion, creates a new version, so the documents layer never edits in place. Settings cover reminder cadence, expiry, certificate mode (appended page or separate PDF) and recipient access codes. Reporting is per-envelope status and who has acted. Notifications are the invitation, reminder, decline, void, completion and expiry emails through the platform queue. Permissions combine the sender's rights on the document with recipient link capability; signers never gain access to the document library. Audit is the core of this file: the hash-chained trail. Exchange is the download of the sealed PDF, the certificate and the audit export (JSON) for the sender and every party. Reverse is void by the sender and decline by a recipient, both recorded and neither deleting history.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Bounded PDF reader and incremental update writer package | D09 T01 §20 | [ ] |
| 2 | §2 | Envelope, recipient and field model with D1 tables | D09 T01 §3 | [ ] |
| 3 | §3 | Hash-chained audit trail package and verifier | D09 T01 §3 | [ ] |
| 4 | §4 | Envelope Durable Object state machine | §2, §3 | [ ] |
| 5 | §5 | Envelope API: draft, recipients, fields, send, void | §1, §4, D09 T01 §7, D09 T02 §2 | [ ] |
| 6 | §6 | Field placement UI for signature, initials, date, text and checkbox | §5, D09 T02 §16 | [ ] |
| 7 | §7 | Single-use signing links, routing order and invitation email | §4, D09 T01 §9, D09 T01 §8 | [ ] |
| 8 | §8 | Signer ceremony API: consent, access code, adopt, fill, sign, decline | §7, §3 | [ ] |
| 9 | §9 | Signer ceremony UI: consent, adopt, fill and sign | §8, D09 T02 §16 | [ ] |
| 10 | §10 | Reminders and expiry by Cron | §7, D09 T01 §18 | [ ] |
| 11 | §11 | Render text, date and checkbox values with an embedded subset font | §1 | [ ] |
| 12 | §12 | Signature and initials images: validate, store and embed | §1, §8 | [ ] |
| 13 | §13 | Certificate of completion with audit chain head | §3, §11 | [ ] |
| 14 | §14 | PAdES-B-T seal engine with RFC 3161 timestamp and test certificate | §1 | [ ] |
| 15 | §15 | Production seal signer through an HSM or signing service | §14 | [ ] |
| 16 | §16 | Completion Queue job: render, certify, seal, version and notify | §11, §12, §13, §14, D09 T02 §4, D09 T02 §7, D09 T01 §9 | [ ] |
| 17 | §17 | Golden PDFs, independent validator and tamper tests | §16 | [ ] |
| 18 | §18 | Retention, legal hold, data export and deletion for envelopes | §16, D09 T01 §18 | [ ] |
| 19 | §19 | Set retention, quota and expiry defaults | §18 | [ ] |
| 20 | §20 | Approve ESIGN consent disclosure and signer legal texts | §9 | [ ] |
| 21 | §21 | Sender dashboard: envelopes, status, audit view and downloads | §16, D09 T02 §13 | [ ] |
| 22 | §22 | End-to-end signature ceremony tests in the browser | §9, §10, §16, §21, D09 T01 §20 | [ ] |
| 23 | §23 | Envelope templates: reusable recipients, roles and fields | §5, §6 | [ ] |
| 24 | §24 | Bulk send: one template to a CSV, one envelope per row | §23, §7, D09 T01 §8 | [ ] |
| 25 | §25 | Web form link engine: one envelope per submitter with abuse limits | §23, §7, D09 T01 §8 | [ ] |
| 26 | §26 | Web form link page and owner management | §25, D09 T02 §13 | [ ] |
| 27 | §27 | In-person signing: hand a device to a signer, attested in the audit | §8, §13, D09 T01 §7 | [ ] |
| 28 | §28 | In-person hand-over screens and templates, bulk and form pages | §24, §26, §27, §21 | [ ] |
| 29 | §29 | End-to-end tests for templates, bulk send, web forms and in-person | §28, §22 | [ ] |

## 1. Bounded PDF reader and incremental update writer package

The cloud must read an uploaded PDF enough to count pages, find page boxes and append content, and must never trust it. This section creates `pdf-lite`, a small TypeScript package that parses within hard limits and writes only incremental updates, so the original bytes stay intact and an earlier signature survives. It is shared with shared-review export (`D09 T03 §11`). It must not execute or interpret any PDF content stream, must refuse encrypted or malformed input with a typed error, and must stay inside Worker time and memory limits. It keeps Recto's rule that hostile files never run code (D9).

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Create `cloud/packages/pdf-lite` (`@recto-cloud/pdf-lite`, AGPL-3.0-or-later, strict TypeScript) with `readPdf(source: RangeSource, limits): Promise<Result<PdfView, PdfLiteError>>` that parses the header, the last cross-reference section (table or stream), the trailer and the page tree only. No public function in `pdf-lite`, `seal` or the PDF jobs takes or returns a whole-file `Uint8Array`; whole-file inputs are `RangeSource` or `ReadableStream`, outputs are `WritableStream`, and only small objects, the update tail and images are buffers. Done when: a corpus of 30 valid PDFs reports correct page counts and boxes, and a lint rule fails the build on a whole-file buffer parameter or on `.arrayBuffer()` of an R2 body in these modules.
2. Define limits as constants: input 200 MiB read only through a `RangeSource` (`read(offset, length)` backed by R2 range reads, never the whole file in memory), 20000 objects visited, object nesting depth 32, page tree depth 16, 10000 pages, cross-reference chain length 64, decompressed object stream 16 MiB, at most 16 MiB of new content per update, and a wall-clock budget of 2 seconds for parsing checked cooperatively. Memory is bounded isolate-wide by admission control: `pdf-lite` exports `PdfBudget`, a per-isolate accountant of 80 MiB in total (a Worker isolate has 128 MB shared by all concurrent requests, so the rest is left for the runtime and the framework). Every buffer of 64 KiB or more is allocated through `allocBuffer(n)`, which charges the budget and throws `busy` when it would exceed it. A parse-only operation reserves 8 MiB, a write or seal operation reserves 48 MiB, and an operation that cannot reserve returns `busy` (HTTP `503` with `Retry-After`, or a queue retry). PDF-writing, sealing and export work runs only in a Queue consumer `pdf-jobs` configured with `max_concurrency` 1 and batch size 1, so at most one heavy operation per consumer isolate competes with request-path parses. Done when: each limit has a fixture that fails one unit over.
3. Refuse with typed errors: `encrypted`, `malformed`, `too_large`, `too_complex`, `unsupported_xref`, `timeout`. Done when: a table test maps 25 hostile fixtures to the expected error and none throws an untyped exception.
4. Implement `writeUpdate(view, changes, sink: WritableStream): Promise<UpdateInfo>` that streams the original bytes from the `RangeSource` to the sink and then appends only the new and replaced objects, a new cross-reference section and trailer with `/Prev`, so memory holds the appended tail and never the original. Done when: the output starts with the exact original bytes, re-parses with the same page count, and a 200 MiB fixture is written within the 48 MiB reservation, shown by `PdfBudget` accounting and by the package test run with a 96 MiB V8 heap limit, and a Workers-pool test that runs one heavy operation together with the maximum admitted parse-only operations at the same time completes without an isolate memory error, while a further operation returns `busy`.

- [ ] Add `src/reader/` (lexer, object parser, xref table and stream, object streams, page tree) with no `eval`, no `Function` and no dynamic import. Done when: a lint rule `no-eval` and a grep test over the package find zero uses.
- [ ] Add `src/writer/incremental.ts` with deterministic object numbering and offsets. Done when: two runs on the same input produce byte-identical output.
- [ ] Add a fuzz harness `cloud/packages/pdf-lite/fuzz/reader.fuzz.ts` driven by a seeded mutator over the corpus (byte flips, truncation, splice) running 20000 cases in CI. Done when: no case throws an untyped error, exceeds a 96 MiB heap, or runs over 3 seconds.
- [ ] Add a differential test against `pdf.js` page counts on the corpus. Done when: counts match for every valid file.
- [ ] Add `PROVENANCE.md` for the corpus in `cloud/packages/pdf-lite/fixtures/`, reusing files from `tests/corpus/` where licences allow. Done when: each fixture lists its origin and licence.
- [ ] Commit: `cloud: bounded pdf-lite reader and incremental writer`

**Test checkpoint:** `npm --prefix cloud run test:pkg -- pdf-lite` passes including the 25 hostile fixtures; deleting the object-nesting depth check makes the deep-nesting fixture fail with a timeout or stack error instead of `too_complex`.

## 2. Envelope, recipient and field model with D1 tables

Every other section needs one agreed model of an envelope. This section defines the types and the D1 tables, with validators shared by the API and the web app. It must keep the state, the recipients and the fields separate and normalised, and must not hold the document bytes.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Create `cloud/packages/envelope-schema` with types and validators for `Envelope`, `Recipient` (role `signer`, `approver`, `copy`), `Field` (kind `signature`, `initials`, `date`, `text`, `checkbox`) and `Settings`. Done when: a table test accepts valid shapes and rejects 30 invalid ones.
2. Add migration `cloud/packages/db/migrations/<next number>_envelopes.sql` (the next free number) with `envelopes`, `envelope_recipients`, `envelope_fields`, `envelope_values`, `envelope_links`, `envelope_audit`. Done when: it applies to an empty and to a populated database.
3. Add `cloud/apps/api/src/envelopes/store.ts` behind the platform database interface. Done when: tests pass on D1 and on the SQLite adapter.

- [ ] Define `Envelope` columns: `id`, `owner_id`, `document_id`, `version_id` (pinned), `title`, `status`, `routing` (`sequential` or `parallel`), `certificate_mode` (`appended` or `separate`), `expires_at`, `reminder_days`, `legal_hold`, `sealed_version_id`, `chain_head`, `created_at`, `sent_at`, `completed_at`. Done when: `status` rejects any value outside the seven states by a `CHECK`.
- [ ] Define `Recipient` columns: `id`, `envelope_id`, `role`, `name`, `email`, `routing_order`, `signing_mode` (`remote` or `in_person`), `access_code_hash`, `access_code_attempts`, `status` (`pending`, `notified`, `viewed`, `consented`, `completed`, `declined`). Done when: two recipients cannot share the same email within one envelope for the same role.
- [ ] Define `Field` columns: `id`, `envelope_id`, `recipient_id`, `kind`, `page`, `x`, `y`, `width`, `height` (PDF points from the bottom-left of the crop box), `required`, `label`, `options` (text max length, date format). Done when: a field outside the page box fails validation against the page box from `pdf-lite`.
- [ ] Enforce limits: 10 recipients per envelope, 500 fields, 100 fields per recipient per page, field minimum 8 by 8 points. Done when: each limit has a test.
- [ ] Store recipient email addresses as given for sending, with a salted hash column for lookups. Done when: lookups use only the hash.
- [ ] Commit: `cloud: envelope model, schema package and D1 migration`

**Test checkpoint:** `npm --prefix cloud run test:pkg -- envelope-schema` and `npm --prefix cloud run test:api -- test/envelopes/store` pass; inserting status `signed` fails the `CHECK` test.

## 3. Hash-chained audit trail package and verifier

The audit trail is the evidence. This section builds the append-only, hash-chained event log and its verifier as an independent package with no database or network dependency, so a third party can run the verifier on an exported trail. It must make silent edits detectable and must never store document content.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Create `cloud/packages/audit-chain` with `canonicalize(event)` (RFC 8785 JSON canonical form) and `hashEvent(prevHash, event)` as SHA-256 of `prevHash || canonical bytes`, hex encoded. Done when: published RFC 8785 test vectors and fixed SHA-256 vectors pass.
2. Define the event: `seq` (starting at 1), `type`, `actor` (`sender`, `recipient:<id>`, `system`), `at` (UTC ISO 8601 with milliseconds), `ip`, `userAgent`, `data` (small typed object), `prev` (hash of event `seq-1`, 64 zeros for the first) and `hash`. Done when: a validator rejects unknown types and oversized `data` over 2 KiB.
3. Implement two functions and keep their meanings apart. `verifyChain(events): VerifyResult` checks internal consistency only and returns `consistent` with the head and count, or the first failing `seq` and reason (`gap`, `prev_mismatch`, `hash_mismatch`, `time_regression`, `schema`); it cannot see removed trailing events or a fully recomputed chain. `verifyAgainstCheckpoint(events, checkpoint: {count, head})` returns `{authenticatedPrefix: {count, head}, unauthenticatedSuffix: {count, head}}` or a failure: it requires the event at `seq = count` to have `hash = head`, labels only events 1 to `count` authentic, and reports every later event as an unauthenticated suffix, because the seal cannot cover events that happen after it. Done when: mutating any single field of any event in a 50-event chain is detected at the right `seq` by `verifyChain`, truncating or recomputing events inside the prefix is detected only by `verifyAgainstCheckpoint`, and truncating or replacing suffix events is reported as an unauthenticated suffix, not as tampering and not as authentic.
4. Implement the authoritative append inside the `Envelope` Durable Object storage (`cloud/apps/api/src/envelopes/audit.ts`, called by `§4` in the same storage transaction as the transition) and the D1 projection `envelope_audit` with unique `(envelope_id, seq)`. Add SQL triggers on the projection refusing every `UPDATE`, and refusing `DELETE` unless a row exists in `envelope_audit_purge(envelope_id, authorized_at, reason)`; only the purge job of `§18` writes that row, in the same D1 batch as the deletes. Done when: a test that updates or deletes a row without authorization fails and the authorized purge succeeds.

- [ ] Define the event types: `envelope.created_from_template`, `envelope.created_from_form`, `inperson.started`, `inperson.handed_back`, `envelope.created`, `envelope.sent`, `link.issued`, `link.redeemed`, `recipient.viewed`, `consent.accepted`, `consent.declined`, `access_code.failed`, `access_code.locked`, `signature.adopted`, `field.completed`, `recipient.signed`, `recipient.approved`, `recipient.declined`, `reminder.sent`, `envelope.voided`, `envelope.expired`, `envelope.completing`, `envelope.sealed`, `envelope.completed`, `legal_hold.set`, `legal_hold.cleared`. Done when: the schema test lists exactly these.
- [ ] Add a CLI `cloud/packages/audit-chain/bin/verify.mjs <audit.json> [--checkpoint <count>:<head>]` that prints `CONSISTENT <head> (not authenticated)` without a checkpoint, `AUTHENTIC PREFIX <count> <head>; SUFFIX <n> events (not authenticated)` with a matching one, or the failure, and exits 0 or 2. The checkpoint is read from a sealed PDF whose seal verified (`§13`, `§17`). Done when: a good, a prefix-tampered and a prefix-truncated export give 0, 2 and 2 with a checkpoint, and the output never uses the word `AUTHENTIC` for any event after the checkpoint.
- [ ] Add concurrency protection: two appends racing for the same `seq` leave one winner and one retry. Done when: a test with 20 parallel appends yields a gap-free chain of 20.
- [ ] Record `ip` and `userAgent` only for recipient and sender actions, truncating the user agent to 256 bytes. Done when: system events carry null for both.
- [ ] Commit: `cloud: hash-chained audit trail and verifier`

**Test checkpoint:** `npm --prefix cloud run test:pkg -- audit-chain` passes including the 50-event mutation sweep; removing the `prev` comparison makes the splice test (swap two events) fail.

## 4. Envelope Durable Object state machine

One `Envelope` Durable Object per request serialises every state change so two clicks at once cannot complete an envelope twice or let a voided envelope be signed. This section implements the machine, with guards and idempotency. It must be the only writer of `status`, and it must append the audit event in the same step as the transition.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add `cloud/apps/api/src/envelopes/envelope-do.ts` exporting `Envelope`, bound in `wrangler.jsonc` as `ENVELOPE` with a migration entry. Done when: the worker routes internal calls by `idFromName(envelopeId)`.
2. Implement the transition table: `draft` to `sent` (send), `sent` to `in_progress` (first recipient action), `sent` or `in_progress` to `completed` (all required recipients done and seal stored), to `declined` (any signer or approver declines), to `voided` (sender void), to `expired` (Cron after `expires_at`). Terminal states accept nothing. Done when: a table-driven test covers every state against every event and only the listed pairs succeed.
3. Implement routing: sequential envelopes activate recipients one `routing_order` at a time and parallel envelopes activate all at once; copy recipients are notified only at completion. Done when: a test with three sequential signers shows signer 2 cannot act before signer 1 signs.
4. Make every command idempotent by a client-supplied `commandId` and return the original result on replay. Done when: replaying `sign` 5 times records one `recipient.signed` event.

- [ ] Implement `finalizing`: the append of `envelope.completing` is the commit point. In the same storage transaction set `completion_started_at` and `generation` (its `seq`) and add the completion message to the outbox; the job carries the generation. From then on `void`, `decline`, expiry and any further `sign` return `409 envelope_finalizing` (completion wins, because every required signature already exists), and the Cron expiry skips finalizing envelopes. Before the commit point a terminal transition wins and no completion is ever enqueued. Only the dead-letter state (`envelope.completion_failed`, `§16`) re-allows `void` by the sender. Done when: tests inject `void`, `decline` and expiry before and after the commit point, and at pauses before sealing, before storing the version and before publication, and each lands on the documented winner.
- [ ] Implement the guard that a voided, declined or expired envelope rejects `sign`, `view`-to-act and link redemption with typed errors. Done when: each terminal state has a rejection test.
- [ ] Make the Durable Object's own SQLite storage the single authority, with at-least-once effects and idempotent consumers (exactly-once enqueue is not promised): one storage transaction writes the new `status`, the next audit event (with its `prev` and `hash`) and one `outbox` row per side effect (D1 projection of the status and the event, queue messages, emails). An alarm drains the outbox in order and deletes a row only after its effect was accepted, so a crash after acceptance and before the delete sends the effect again. Consumers deduplicate atomically: each effect has a `dedupe_key`, and its consumer inserts the key into D1 `processed_effects(dedupe_key primary key)` in the same D1 batch as the effect's own writes, skipping the effect if the key exists. D1 upserts are keyed by `(envelope_id, seq)`. Emails are keyed by event `seq`; an email may rarely be delivered twice if a crash falls between the mail transport accepting it and the key being recorded, which is accepted, and the duplicate carries the same link because the link is created once and held in the outbox row until acknowledged. D1 is a query projection and never an input to a transition. Done when: crash-injection tests abort after the storage commit and before each effect, and after each effect was accepted and before its outbox row is deleted, and end with a D1 chain equal to the authority's chain, exactly one completion performed and one completion state change, and at most the documented duplicate email.
- [ ] Add a concurrency test: 10 parallel `sign` commands from the last two parallel signers produce one `envelope.completing` event and one completion performed. Done when: the queue stub may observe a repeated message after an injected crash but the consumer performs the completion once.
- [ ] Commit: `cloud: Envelope Durable Object state machine`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/envelopes/machine` passes on the Workers pool; allowing `voided` to `sent` in the table makes the exhaustive transition test fail.

## 5. Envelope API: draft, recipients, fields, send, void

The sender prepares an envelope through a REST API before any email goes out. This section adds the routes with validation and rights checks. It must refuse to send an incomplete envelope, must pin the document version, and must never let a sender edit an envelope after it is sent except to void it.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add routes in `cloud/apps/api/src/envelopes/routes.ts`: `POST /v1/envelopes`, `GET /v1/envelopes`, `GET /v1/envelopes/:id`, `PUT /v1/envelopes/:id/recipients`, `PUT /v1/envelopes/:id/fields`, `PATCH /v1/envelopes/:id/settings`, `POST /v1/envelopes/:id/send`, `POST /v1/envelopes/:id/void`, `DELETE /v1/envelopes/:id` (draft only). Done when: each has request and response schemas and an integration test.
2. Implement `send` preconditions: at least one signer; each signer has at least one required signature field; every field belongs to a signer or approver; the pinned version parses with `pdf-lite` and is not encrypted; page boxes contain every field. Done when: each failed precondition returns `422` with a list of problems and sends nothing.
3. Implement rights: sending a document to outsiders extends its audience, so the sender must be the document owner through `resolveAccess` (`cloud/apps/api/src/access/resolve.ts`); `view`, `comment` and `edit` roles are refused (a delegated share capability would be a change in `D09 T02 §2`; owner-only is the default and is cheap to relax later); `void` and reads need ownership or an admin role. Done when: callers with each of the three roles get `403 not_owner` and the envelope is unchanged.

- [ ] Parse the pinned version once at `send` and store page count and boxes, using `pdf-lite` limits. Done when: an encrypted document returns `422 document_unsupported` with a plain message.
- [ ] Add `message` (up to 2000 characters) and `subject` (up to 120) per envelope and a per-recipient message override. Done when: control characters are stripped and tested.
- [ ] Enforce daily send limits per account from the platform limiter and a maximum of 50 active envelopes per account. Done when: the 51st `send` returns `429` or `422 envelope_limit` as designed.
- [ ] Implement `void` with a required reason up to 500 characters, revoking all links and notifying recipients through the queue. Done when: links return `410` after void and the notification is enqueued.
- [ ] Commit: `cloud: envelope API with send preconditions and void`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/envelopes/routes` passes; a test that sends an envelope with a signer lacking a signature field must fail if the precondition is removed.

## 6. Field placement UI for signature, initials, date, text and checkbox

The sender needs to drop fields on the pages and assign them to people. This section builds the prepare screen: document pages on the left, recipients and tools on the right, drag to place, resize, assign, mark required, and review before sending. It follows `design/SPEC.md` brand, colour and type. It must keep the viewer scripting off, must show each recipient in a distinct colour that also has a text label, and must not allow a field outside a page.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` sections 3, 4, 5 and 7 (brand, colour, type, shape) and the Organize page layout idea of section 9.6 (page strip plus work area) with `design/screenshots/D-Organize.png`.
**Job:** A sender adds recipients with roles and order, drags a field type onto a page, sees it snap to the page bounds, assigns it to a recipient, sets required and label, copies it to other pages, sees problems listed with a link to each, and presses `Review and send`. Consumer: `§5` saves fields and sends; `D09 T02 §16` provides the page canvas.
**Treatment:** A web page at `/envelopes/:id/prepare` under `cloud/apps/web/src/envelopes/` with an absolutely positioned field layer over each page canvas, pointer and keyboard placement, undo and redo, and autosave of drafts. Cheaper substitute that fails the checkpoint: a form that takes field coordinates as numbers, or a mouse-only editor with no keyboard path.
**Chrome:** consume the token stylesheet generated from `design/tokens/RectoColors.xaml`; recipient colours come from a fixed 8-colour accessible set derived from tokens (never literals in components) and every field shows the recipient initials as text; 8 px radius cards; icon-only controls carry `aria-label` and `title`.

**Build order:**

1. Add `RecipientsPanel.ts` (add, remove, reorder, role, access code toggle, routing choice) and `FieldToolbox.ts` (five field kinds). Done when: keyboard-only use covers every action.
2. Add `FieldLayer.ts` with drag, resize with eight handles, nudge with arrow keys (1 point, 10 with Shift), snap to page bounds and to other fields, and multi-select. Done when: a field cannot leave its page in any gesture.
3. Add the review step listing every problem from the `send` preconditions with a button that scrolls to the offending field or recipient. Done when: fixing each problem clears its row live.

- [ ] Add undo and redo with `Ctrl+Z` and `Ctrl+Y`. Done when: 50 operations undo and redo without drift.
- [ ] Add autosave every 3 seconds when dirty with a visible `Saved` state and conflict handling on a stale version. Done when: two tabs editing show a conflict message, not silent overwrite.
- [ ] Add accessibility: fields are focusable with names like `Signature for Dana, page 2`, and axe-core finds zero serious violations. Done when: the axe check passes in the spec.
- [ ] Add dark and forced-colors themes. Done when: screenshot tests exist for light, dark and forced-colors.
- [ ] Commit: `cloud-web: envelope field placement UI`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/envelope-prepare.spec.ts` passes in Chromium; the spec fails if a field can be dropped outside the page.

## 7. Single-use signing links, routing order and invitation email

Recipients act through links, so links are the credential. This section issues, stores, redeems and expires them, applies the routing order, and sends the invitation through the platform queue. It must never put the document or a reusable secret in email, must store only hashes, and must not reveal whether an email address is registered.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `issueLink(recipient)` creating a 256-bit random token with `crypto.getRandomValues`, storing only `SHA-256(token)`, the recipient, an expiry (default 7 days, never past the envelope expiry) and a `redeemed_at` null. Done when: the database holds no value that can reconstruct a token.
2. Implement `redeem(token)`: constant-time hash lookup, atomically set `redeemed_at`, and exchange for a ceremony session cookie (named `__Host-recto-sign`, `HttpOnly`, `Secure`, `SameSite=Lax`, `Path=/` with no `Domain` so it reaches both `/sign` pages and `/v1/sign/...` API routes, idle 60 minutes, absolute 4 hours) bound to the recipient. Done when: the second redemption of the same token returns `410` even from the same IP.
3. Implement `POST /v1/sign/relink` letting a recipient whose link was used or expired request a new link to the verified address, always answering `202` with the same text, rate limited to 3 per hour per recipient and 20 per hour per IP. Done when: an unknown address and a known one return identical responses and timing within 50 ms.
4. Implement routing activation: when a recipient completes, the Durable Object activates the next order group and enqueues its invitations. Done when: a three-signer sequential test sends exactly one invitation at a time.

- [ ] Add the envelope kinds (invitation, reminder, declined, voided, completed, expired) to `cloud/packages/shared/src/email.ts` and their templates to `cloud/apps/api/src/email/templates.ts` in plain text and HTML, with one button, the sender name, the message and no document content. Done when: a snapshot test finds no image tags, no tracking links and no third-party hosts.
- [ ] Add the `Reply-To` rule: replies go to a no-reply address and the email names the sender's email address in the body for contact. Done when: the template test shows no sender address in headers.
- [ ] Rate limit redemption attempts per IP and per token prefix and respond with generic text. Done when: 30 bad tokens in a minute trigger `429` and one audit event, not 30.
- [ ] Add a browser test with a real cookie jar (Playwright `context.cookies()` and request interception) proving the cookie is sent to `/v1/sign/session` and rejected by `/v1/envelopes` routes. Done when: both assertions pass.
- [ ] Revoke all links when an envelope is voided, declined or expired. Done when: each terminal state test shows `410`.
- [ ] Commit: `cloud: single-use signing links and routing activation`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/envelopes/links` passes; a test that redeems the same link twice must fail if the atomic `redeemed_at` update is replaced by a read then write.

## 8. Signer ceremony API: consent, access code, adopt, fill, sign, decline

This is the heart of the signature request: the server side of what a signer does. It enforces order, consent before anything else, the optional access code, and that only the assigned recipient can fill their fields. It must record every step in the audit chain and must make the ESIGN consumer consent a hard precondition. It does not claim advanced or qualified signatures.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add routes under `cloud/apps/api/src/sign/routes.ts` using the ceremony session: `GET /v1/sign/session` (envelope summary, recipient, fields, consent version, no document bytes), `GET /v1/sign/document` (the pinned version, streamed, only after consent), `POST /v1/sign/consent`, `POST /v1/sign/access-code`, `POST /v1/sign/adopt`, `PUT /v1/sign/fields/:fieldId`, `POST /v1/sign/complete`, `POST /v1/sign/decline`. Done when: each route has schemas and tests.
2. Enforce one order of steps in the Durable Object: summary (no document content), access code if set, consent with the access demonstration (a static one-page consent-check PDF served from `/v1/sign/consent-check`, never the envelope document, containing a random 6 character code the recipient types back), then the document and fields, then complete. Done when: a table test over every document-serving route (`document`, `copy`, any thumbnail or text route, the audit export, relink) in each state (no session, wrong recipient, access code not entered, access code locked, consent not given, consent declined, terminal envelope) returns the document only when the access code step and the consent are both satisfied.
3. Implement the access code: PBKDF2-SHA256 hash with per-recipient salt via WebCrypto at the iteration count recorded in settings (at least 600000), five failures lock the recipient for 15 minutes with `access_code.locked`, and ten total failures need the sender to reissue. Done when: the lock tests pass and the code never appears in logs.
4. Implement `complete`: all required fields for the recipient have values, signature and initials fields have the adopted image, dates are valid, then in one Durable Object transaction compute `values_hash` (SHA-256 of the canonical list of field ids, values and image hashes), store the values and image hashes as frozen, append `recipient.signed` carrying `values_hash`, and advance routing. After that, `PUT fields`, `adopt` and a second `complete` from that recipient return `409 recipient_completed`. Done when: completing with a missing required field returns `422` listing the field ids, and an edit of a field or a new adoption after `recipient.signed` is rejected while other recipients are still pending and the sealed output uses the frozen values.

- [ ] Implement consent as the ESIGN 7001(c) flow: the consent record stores the disclosure version, the recipient, the time, and that the recipient confirmed they can access the document in the electronic format by displaying the consent-check PDF in the same viewer used for the document and typing back the code it shows. Done when: the consent route rejects a wrong or missing code, and no envelope document byte is served before consent.
- [ ] Implement `decline` with a reason up to 500 characters, moving the envelope to `declined`, revoking links and notifying the sender. Done when: all other recipients' links return `410` afterwards.
- [ ] Implement field value validation per kind: text length and pattern from the field options, date as `YYYY-MM-DD` within 10 years of today, checkbox boolean, signature and initials referencing the adopted image id. Done when: 30 invalid values are rejected and none changes state.
- [ ] Implement approver behaviour: approvers see the document, may approve or decline, have no fields, and approval appends `recipient.approved`. Done when: an approver completing with no fields works and a signer without fields is rejected.
- [ ] Implement access to the document with `Cache-Control: private, no-store` and a per-session rate limit on downloads. Done when: the header test passes.
- [ ] Add `GET /v1/sign/copy` allowing a completed recipient to download the sealed PDF after completion only. Done when: before completion it returns `404`.
- [ ] Commit: `cloud: signer ceremony API with consent, access code and decline`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/sign/ceremony` passes; removing the consent guard on `GET /v1/sign/document` makes the consent-order test fail.

## 9. Signer ceremony UI: consent, adopt, fill and sign

A signer, often on a phone and never seen before, must understand what they are doing in a minute. This section builds the ceremony pages: the electronic records disclosure, the access code prompt, the adopt-your-signature step (typed, drawn or uploaded), the guided fill and the final sign, plus the decline path. It follows `design/SPEC.md` brand, colour and type, with large touch targets. It must not use dark patterns, must show the sender and the document name before consent, and must not claim a stronger signature than a simple electronic signature.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` sections 3, 4, 5 and 7, the dialog pattern of 9.9 with `design/screenshots/D-Dialogs.png`, and the accessibility rules of section 11.
**Job:** A signer opens the link on any device, reads who is asking and what for, accepts or refuses the electronic records disclosure, enters an access code if set, adopts a signature by typing a name, drawing, or uploading an image, is guided field by field with a progress indicator, reviews the result, and presses `Sign`; or declines with a reason. Consumer: `§8` is the API; `§12` stores the images.
**Treatment:** Pages under `cloud/apps/web/src/sign/` for 360 px wide screens and up, with a sticky bottom action bar, a `Next required field` button, a canvas drawing pad using pointer events with a keyboard-accessible typed alternative, and a finish page offering the sealed copy when ready. Cheaper substitute that fails the checkpoint: a desktop-only layout, a drawing-only signature with no typed option, or a pre-ticked consent box.
**Chrome:** consume the token stylesheet; the single primary button uses the accent; touch targets are at least 44 CSS pixels; signature preview uses the text colour on white page tone; every icon-only control has `aria-label` and `title`; the footer text says `Simple electronic signature` and nothing stronger.

**Build order:**

1. Add `ConsentPage.ts` showing the sender, document name, the disclosure text of the approved consent version, links to the paper copy and withdrawal instructions the disclosure promises, the consent-check PDF in the viewer with a field for its code, an unticked checkbox, and `Continue` and `I do not consent` buttons. Done when: `Continue` stays disabled until the box is ticked and the code is entered.
2. Add `AdoptSignature.ts` with three tabs (type, draw, upload), a live preview for signature and initials, and a clear control. Done when: each tab produces a PNG with a transparent background of at most 800 by 300 pixels.
3. Add `FillPage.ts` guiding through required fields in page order, with progress `3 of 7`, jump to next, and a final review that lists all values. Done when: the keyboard can complete a whole envelope with no pointer.

- [ ] Add the decline dialog with a reason. Done when: declining returns the user to a clear finished page and no further action is possible.
- [ ] Add session timeout handling: on `401` the page offers `Send me a new link`. Done when: an expired session test shows the relink flow.
- [ ] Add accessibility and mobile checks: axe-core zero serious violations, viewport 360 by 640 without horizontal scroll, and `prefers-reduced-motion` honoured. Done when: the checks pass in the spec.
- [ ] Add a claims lint `cloud/apps/web/test/claims.test.ts` failing the build if any user-facing string contains `advanced electronic signature`, `qualified electronic signature`, `qualified`, `eIDAS-compliant`, `legally binding in all`, or `court-proof`. Done when: adding one of those phrases to a template makes the test fail.
- [ ] Commit: `cloud-web: signer ceremony pages`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/sign-ceremony.spec.ts` passes at 360 and 1280 pixel widths; the spec fails if the consent checkbox is pre-ticked.

## 10. Reminders and expiry by Cron

Signature requests stall unless someone nudges them, and stale links must die. This section adds the Cron handler that sends reminders on the sender's cadence and expires envelopes. It must not remind people who finished, must send each reminder once, and must leave an auditable trail.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add `cloud/apps/api/src/envelopes/cron.ts` registered in `cloud/apps/api/src/jobs/scheduler.ts` and run every 15 minutes (`*/15 * * * *`) selecting `sent` and `in_progress` envelopes due for reminders or expiry using indexed queries and a batch size of 200. Done when: a test with 1000 envelopes processes them across runs without duplicates.
2. Implement reminders: every `reminder_days` (default 3, range 1 to 14, or off) the active recipients who have not completed get one reminder, at most 5 per recipient, plus one 24 hours before expiry. Done when: a clock-controlled test counts the expected reminders exactly.
3. Implement expiry: past `expires_at` the Durable Object moves to `expired` unless it is finalizing (`§4`), links are revoked and the sender is notified. Done when: an expired envelope rejects `sign` with `410 expired`.

- [ ] Make each reminder idempotent through a `reminder_key` of `(recipient_id, sequence)` in the audit event. Done when: running the handler twice in a row sends once.
- [ ] Add the sender action `POST /v1/envelopes/:id/remind` for an immediate reminder limited to one per recipient per 24 hours. Done when: the second call returns `429`.
- [ ] Add the extend action `PATCH /v1/envelopes/:id/settings` for `expires_at` on active envelopes up to a maximum of 365 days from sending. Done when: extending past the maximum returns `422`.
- [ ] Commit: `cloud: envelope reminders and expiry cron`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/envelopes/cron` passes using a controlled clock; a test running the handler twice must fail if the idempotency key is removed.

## 11. Render text, date and checkbox values with an embedded subset font

Completed values must appear in the PDF as page content any viewer can read, in the right place, including non-English names. This section renders text, date and checkbox values using `pdf-lite` with a subset of a bundled font. It must not depend on viewer fonts, must refuse text it cannot draw instead of drawing boxes, and must keep text selectable.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Bundle one OFL-licensed font family covering Latin, Latin Extended, Greek and Cyrillic under `cloud/packages/pdf-lite/fonts/` with its licence file and provenance. Done when: the licence text is present and the repository licence check accepts it.
2. Implement TrueType subsetting and embedding (`/FontFile2`, `/CIDFontType2`, `/Identity-H`, `/ToUnicode`) in `cloud/packages/pdf-lite/src/writer/font.ts`. Done when: the output text extracts with `pdf.js` to the exact input string.
3. Implement value placement: fit text in the field box with a size from 6 to 14 points, left aligned with 2 points padding, single line for text and date, and a vector check mark for checkboxes. Done when: a layout test never lets glyphs cross the box.
4. Implement coverage checking at entry time and at render time: characters outside the font's coverage return `unsupported_characters` listing them. Done when: a CJK name in a text field is refused with a clear message at the API.

- [ ] Add the `ToUnicode` map and `/ActualText`-free simple text operators so search and copy work. Done when: `pdf.js` text extraction returns `Zoë Ångström Ольга`.
- [ ] Add a golden render of a filled form compared with `SSIM >= 0.98` against a committed PNG. Done when: the test passes and fails when the font size changes by 2 points.
- [ ] Add size accounting: the font subset adds under 150 KiB for a 20-field envelope. Done when: the test asserts the bound.
- [ ] Commit: `cloud: render text, date and checkbox values with subset font`

**Test checkpoint:** `npm --prefix cloud run test:pkg -- pdf-lite` passes with extraction and golden checks; replacing the embedded font with an unmapped one makes the extraction test fail.

## 12. Signature and initials images: validate, store and embed

The adopted signature arrives as an image from a browser. This section validates it as hostile input, stores it, and embeds it as an image XObject at each signature field. It must decode with a bounded decoder, accept only PNG, and never trust declared dimensions.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `POST /v1/sign/adopt` receiving PNG bytes up to 256 KiB, checking the signature bytes, chunk order, CRC for each chunk, dimensions up to 1200 by 400, bit depth 8, colour types 0, 2, 4, 6, non-interlaced, and refusing ancillary chunks except `pHYs`. Done when: 40 malformed PNG fixtures are refused with a typed error.
2. Decode with `fflate` (MIT) inflate bounded to `width * height * 4 + height` bytes, unfilter, split alpha, and re-encode RGB and alpha as two `FlateDecode` streams for `/Image` and `/SMask`. Done when: a decompression bomb fixture is refused without exceeding 8 MiB memory.
3. Store the normalised image in R2 under the immutable content-addressed key `envelopes/<id>/adoptions/<sha256>.png` (never overwritten) with `Cache-Control: private`, keep the SHA-256 in the audit event `signature.adopted`, and let fields reference the image by hash. Done when: the audit data holds the hash and not the image, and adopting again writes a second key rather than replacing the first.

- [ ] Implement embedding in `cloud/packages/pdf-lite/src/writer/image.ts` scaled to fit the field box preserving aspect ratio, centred, with `q cm Do Q`. Done when: a golden render matches with `SSIM >= 0.98`.
- [ ] Implement typed adoption on the client as a canvas-rendered PNG and on the server as the same PNG path, so there is one code path. Done when: no server code renders fonts for signatures. Cheaper substitute: a second server-side typed-signature renderer that can drift from the preview.
- [ ] Add the adoption record: name as typed, method (`typed`, `drawn`, `uploaded`), time, and image hash, shown on the certificate (`§13`). Done when: the certificate fixture lists method and hash prefix.
- [ ] Commit: `cloud: validate, store and embed adopted signature images`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/sign/adopt` and `npm --prefix cloud run test:pkg -- pdf-lite` pass; the decompression bomb fixture must fail the test if the inflate bound is removed.

## 13. Certificate of completion with audit chain head

The certificate is the human-readable record: who signed what, when, from where, with which consent, and the fingerprints that tie it to the audit trail. This section generates it with `pdf-lite`. It must state the facts only, include the chain head and the document hashes, and never claim a signature level beyond simple electronic signature.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `buildCertificate(envelope, recipients, events, hashes): PdfPages` in `cloud/apps/api/src/envelopes/certificate.ts` producing A4 or Letter pages (matching page one of the document) with title, envelope id, sender, document name, original document SHA-256, one block per recipient (name, email, role, status, consent version and time, adoption method, signed time, IP, user agent summary) and the full event list with `seq`, time, type and actor. Done when: a fixture envelope renders to a readable PDF with correct text extraction.
2. Print the sealed checkpoint: the `seq` and hash of the `envelope.completing` event (the last event the seal covers), as `count` and `head`, and write both into the sealed file's Info dictionary keys `RectoEnvelopeChainCount` and `RectoEnvelopeChainHead`. Events appended after sealing (`envelope.sealed`, `envelope.completed`, notices) are outside the checkpoint by design and are only chained after it. Done when: `verify.mjs --checkpoint` on the exported audit, which holds more events than the checkpoint, reports `AUTHENTIC PREFIX` with the printed count and head and lists the later events as an unauthenticated suffix.
3. Support `certificate_mode`: `appended` adds the pages after the last page inside the sealed file; `separate` produces a second PDF sealed on its own. Done when: both modes are tested end to end in `§16`.
4. Add a statement line: `This is a simple electronic signature record. It is not an advanced or qualified electronic signature under eIDAS.` Done when: the claims lint of `§9` accepts exactly this negated statement through an allow-list entry and rejects other uses.

- [ ] Paginate long audit lists with a header and page numbers `Page n of N`. Done when: a 300-event fixture produces correct pagination with no overflow.
- [ ] Use the embedded font from `§11` for all text so names with accents render. Done when: a fixture with `Zoë Ångström` extracts correctly.
- [ ] Redact nothing but never include email bodies or the access code. Done when: a test greps the certificate bytes for the code and finds none.
- [ ] Commit: `cloud: certificate of completion`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/envelopes/certificate` passes; changing one event type after building the certificate makes the `verify.mjs` head comparison test fail.

## 14. PAdES-B-T seal engine with RFC 3161 timestamp and test certificate

The seal is what makes the finished file tamper-evident to any PDF reader. This section implements a PAdES baseline B-T signature: a CMS signature over the document byte ranges plus a signature timestamp from an RFC 3161 authority. The private key is accessed only through a `SealSigner` interface, with a test-certificate implementation for development and staging. It must not hold a production key, must not claim a trusted status in staging, and must be independent of the rest of the envelope code.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Create `cloud/packages/seal` with the `SealSigner` interface (`certificateChain(): Promise<Uint8Array[]>`, `sign(data: Uint8Array, alg: 'RSA-PSS-SHA256' | 'RSASSA-PKCS1-v1_5-SHA256' | 'ECDSA-P256-SHA256'): Promise<Uint8Array>`) and a `TimestampClient` interface (`timestamp(digest: Uint8Array): Promise<Uint8Array>`). Done when: the types compile and a fake implementation passes the contract tests.
2. Implement signature dictionary preparation with `pdf-lite`: add `/Sig` with `/Filter /Adobe.PPKLite`, `/SubFilter /ETSI.CAdES.detached`, a `/ByteRange` placeholder, a `/Contents` placeholder of 24576 bytes, `/Reason`, `/M`, and a signature field on the first page with an empty invisible widget. Done when: the placeholders sit at known offsets and the ByteRange fills in after layout.
3. Implement CMS construction with a BSD or MIT licensed library such as `pkijs` and `asn1js`: `SignedData` version 1, SHA-256 digest, `content-type`, `message-digest`, `signing-certificate-v2` signed attributes, no `signing-time` attribute (the `/M` and the timestamp carry time), and the RFC 3161 token as the `signature-time-stamp-token` unsigned attribute (`id-aa-timeStampToken`). Done when: the CMS parses with `openssl cms -cmsout -print` in CI.
4. Implement the RFC 3161 client: a `TimeStampReq` over SHA-256 of the signature value with `certReq` true and a random nonce, POSTed to the configured TSA URL with `application/timestamp-query`, a 10 second timeout, and full verification before accepting the token: PKIStatus granted, nonce, message imprint, the token's CMS signature, a certificate chain to the configured `TSA_TRUST_ROOTS`, the signer certificate's critical `id-kp-timeStamping` extended key usage and validity at `genTime`, the `ESSCertIDv2` match, and `genTime` within 5 minutes of the Worker clock. Done when: a test TSA (an in-test RFC 3161 responder using WebCrypto with the test CA) issues tokens that verify, and wrong-nonce, wrong-imprint, forged-signature, untrusted-TSA, wrong-key-usage, expired-certificate and skewed-`genTime` responses are each rejected with a typed error.

- [ ] Implement `sealPdf(source: RangeSource, update, signer, tsa, sink, options): Promise<Result<SealInfo, SealError>>` in two streamed passes: hash the original bytes and the in-memory tail pieces around `/Contents` with a digest stream, build the CMS, then write the original and the filled tail once to the sink, so the tail (not the original) is the only buffer and no unsealed full PDF is ever written. Done when: a re-read finds the signature and `/ByteRange` covering all bytes except `/Contents`, and a 200 MiB fixture seals within the 48 MiB reservation of `PdfBudget`.
- [ ] Generate a staging test CA and signing certificate with a script `cloud/packages/seal/scripts/make-test-ca.mjs`, output kept out of the repository, with CN `Recto Cloud TEST Seal (not trusted)`, validity 5 years and `keyUsage` `digitalSignature`, `nonRepudiation`. Done when: the script runs offline and tests use its output from a temp directory.
- [ ] Implement `TestSealSigner` reading a test key from a path in the test environment only and refusing to start when `ENVIRONMENT` is `production`. Done when: a test sets production and sees the refusal.
- [ ] Implement size handling: if the CMS exceeds the `/Contents` capacity, fail with `seal_too_large` rather than truncate. Done when: a test with a long fake chain produces that error.
- [ ] Add a note field in the signature dictionary `/Name` `Recto Cloud` and `/Reason` `Sealed by Recto Cloud after all parties completed this envelope`, with no claim of identity of signers. Done when: a snapshot test pins both strings.
- [ ] Commit: `cloud: PAdES-B-T seal engine with RFC 3161 timestamp`

**Test checkpoint:** `npm --prefix cloud run test:pkg -- seal` passes including the in-test TSA; flipping one byte inside `/ByteRange` coverage after sealing makes the digest verification test fail.

## 15. Production seal signer through an HSM or signing service

In production the private key must never sit in a Worker secret (spec, Open owner decisions). This section implements the production `SealSigner` that sends only the digest to a cloud HSM or a signing service over authenticated HTTPS, and wires the certificate chain and timestamp authority configuration. It waits on the owner obtaining a document-signing certificate from a CA on the Adobe Approved Trust List and choosing the service, so it cannot complete in a local test run.

**Phase:** 98
**Needs:** third-party, operator
**Surface:** no

**Build order:**

1. Record the decision in `docs/specs/2026-10-21-recto-cloud-design.md` under Open owner decisions: provider, certificate subject, key algorithm and signing cost per operation. Done when: the spec section names the choice and the date.
2. Implement `HsmSealSigner` in `cloud/packages/seal/src/hsm.ts` calling the provider's remote signing API with a short-lived credential from the platform secret store (a credential to call the service, never the key), request signing with the provider's required scheme, and a 5 second timeout with 2 retries. Done when: a contract test against a local fake of the provider's API passes.
3. Add configuration `SEAL_SIGNER`, `SEAL_CERT_CHAIN_R2_KEY`, `TSA_URL` and fail-closed startup: in production the Worker refuses to enqueue completion when the signer is not `hsm`. Done when: a test with `ENVIRONMENT=production` and `SEAL_SIGNER=test` aborts at startup.
4. Run the live checks on staging with the real provider: seal a fixture and validate it with the independent validator, with the CA root added to the validator trust store. Done when: the validator reports an intact signature and a trusted chain (operator evidence stored in `docs/reviews/`).

- [ ] Add a startup check comparing the configured certificate's public key to the key the service reports. Done when: a mismatch refuses to seal.
- [ ] Add expiry monitoring as a log line `seal_cert_days_remaining` at each seal without any alarm schedule, plus a hard refusal when the certificate has expired. Done when: an expired fake certificate returns `seal_certificate_expired`.
- [ ] Document the operator steps in `cloud/docs/operations/seal-signer.md` without secrets. Done when: the document lists the commands and where each value comes from.
- [ ] Commit: `cloud: production seal signer through signing service`

**Test checkpoint:** `npm --prefix cloud run test:pkg -- seal` passes against the local fake; the operator evidence file in `docs/reviews/` records the staging validation output with the validator's verdict.

## 16. Completion Queue job: render, certify, seal, version and notify

When the last signer finishes, one Queue job must produce the final file exactly once. This section orchestrates rendering the values and signatures, building the certificate, sealing, storing the new version, and notifying all parties. It must be idempotent, retried with backoff, and must never expose an unsealed file to a recipient.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add `cloud/apps/api/src/envelopes/completion-job.ts` consuming `envelope-completion` messages from the `pdf-jobs` queue (`max_concurrency` 1, batch size 1) carrying `{envelopeId, generation, attempt}`, where `generation` is the `seq` of `envelope.completing` and `attempt` starts at 1 and is bumped by the Durable Object each time the sender requests a retry after a dead letter. Done when: a message with an unknown id, or a stale `generation` or `attempt`, is acknowledged and logged without retry.
2. Implement the pipeline in this order: take the frozen checkpoint (the `envelope.completing` event, already in the chain), open the pinned version through a `RangeSource`, verify its SHA-256 matches the envelope record and that each recipient's recorded `values_hash` (`§8`) equals the hash of the values and image hashes being rendered, apply values (`§11`) and images (`§12`) as a streamed incremental update, build the certificate from events 1 to the checkpoint (`§13`), seal (`§14` or `§15`) into the staging object `staging/envelopes/<id>/<generation>-<attempt>.pdf` computing its SHA-256 as it is written, and then call the Durable Object `accept(generation, attempt, stagingKey, sha256, size)`. Nothing is visible in the library yet. `accept` is the authoritative commit: it succeeds only if the envelope is still `in_progress` with that generation and attempt, then in one storage transaction appends `envelope.sealed` and `envelope.completed` after the checkpoint, sets `completed` and adds outbox rows `publish` then `notify`. The `publish` effect runs `commitVersion` (below) and records `sealed_version_id`, and `notify` runs only after `publish` was acknowledged. Done when: an end-to-end test on the Workers pool yields a new version whose bytes verify and recipients are notified only after the version exists.
3. Make every step idempotent and fenced: the staging key is deterministic per generation and attempt, so a retried job overwrites only its own unpublished object; a second `accept` for an already accepted `(generation, attempt)` returns success without effect; `commitVersion` is keyed by the envelope id with a unique index on `(envelope_id)` for the version link, so a repeated `publish` creates one version; and a worker holding a stale attempt, or a voided or expired envelope, gets `accept` refused and deletes its staging object. Staging objects older than 48 hours are swept by Cron. Done when: tests resume an old worker after failure, void and sender retry (new attempt) and show its `accept` refused and no version created, and crash a worker between sealing and `accept`, and between `accept` and `publish`, and end with one version and one notification set.
4. Implement failure handling: retry up to 6 times with exponential backoff, then dead-letter, append `envelope.completion_failed` through the Durable Object, which allows the sender to void or to request a retry (bumping `attempt`), and email the sender and the operator contact. Void after a dead letter wins over any older attempt because only `accept` can publish and it requires `in_progress` with the current attempt. Done when: a forced seal failure test shows the dead-letter path and the sender email.

- [ ] Notify every party (signers, approvers, copy recipients, sender) with a link: account holders get the document link, others get a single-use download link valid 14 days that returns the sealed file and the certificate. Done when: each recipient kind receives the expected link type.
- [ ] Store the exported audit trail JSON next to the sealed version as `audit.json` and expose it through an authenticated route. Done when: `verify.mjs` on the download returns `OK` with the printed head.
- [ ] Extract `commitVersion(stagingKey, {sha256, size, source: 'envelope', envelopeId})` into `cloud/apps/api/src/uploads/commit-version.ts`: it copies the already-hashed staging object to `blobs/<sha256>` with an R2 server-side copy (or multipart copy), never loading the bytes, and writes the version row; `commit-job.ts` calls it after its own streamed hashing. Done when: `test/uploads/commit-job.test.ts` still passes unchanged, the function signature has no `Uint8Array` parameter, and the job test shows one blob keyed by the sealed bytes' SHA-256.
- [ ] Charge the sealed version to the owner's quota (`D09 T02 §7`) without blocking completion when the quota is exceeded: record the overage and notify the owner. Done when: a test over quota still completes and the owner is notified.
- [ ] Never write an unsealed full PDF anywhere: the staging object is written once by the streamed pipeline and is already sealed. Done when: a test lists R2 keys after the job, including after a failure injected mid-seal, and finds only sealed staging objects (swept by Cron) and published blobs.
- [ ] Bound resource use: read through `RangeSource`, write through the sink, hold at most the 48 MiB `PdfBudget` reservation of `§1` (returning `busy` for retry when it cannot reserve), and refuse inputs over 200 MiB with a typed error before any work. Done when: an over-limit fixture ends in `completion_failed` with `too_large`, and a 200 MiB fixture completes within the heap budget.
- [ ] Commit: `cloud: envelope completion queue job`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/envelopes/completion` passes; running the job twice must fail the single-version assertion if the unique link is removed.

## 17. Golden PDFs, independent validator and tamper tests

A signature the service checks itself proves nothing. This section commits golden sealed PDFs and validates them in CI with a validator that shares no code with the service, and adds tamper tests that must be detected. It must run on every pull request, fail on a validator regression, and show exactly what the staging certificate means.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Generate golden sealed PDFs from fixed inputs and a fixed test CA kept in the repository for tests only (`cloud/packages/seal/test-fixtures/` with a clearly named `TEST ONLY` CA), covering: one signer, three sequential signers, parallel signers, appended certificate, separate certificate, a document with an earlier signature, and a non-Latin name. Done when: the files exist with a `PROVENANCE.md` giving the generation command and inputs.
2. Add the CI job `.github/workflows/cloud-seal-validation.yml` installing pyHanko (MIT) and Poppler `pdfsig`, then validating each golden file: pyHanko must report an intact signature, a valid timestamp and a trusted chain against the test root; `pdfsig` must report the signature as valid. Done when: the job passes and a golden file with one byte changed turns it red.
3. Add the tamper tests in `cloud/packages/seal/test/tamper.test.ts`: change one byte in the signed ranges, change the `/Contents` hex, append a new revision that edits page text, and replace the certificate page; each must make the validator or the service verifier report modification. Done when: each of the four cases is detected by both the service verifier and pyHanko.
4. Add the audit tamper tests: edit one event, remove one event, reorder two events, truncate the prefix, recompute a whole altered chain, truncate the suffix, replace the suffix with recomputed events. Edits, removal and reordering inside the chain are detected by `verifyChain`; prefix truncation and a recomputed prefix are detected only by `verifyAgainstCheckpoint` against the checkpoint read from the verified seal; suffix truncation and replacement are expected to be reported as an unauthenticated suffix and the test asserts exactly that, documenting the limit. Done when: every case produces its stated result and the report never labels a suffix event authentic.

- [ ] Add `cloud/packages/seal/src/verify.ts` exposing `verifySealedPdf(bytes, trustRoots)` used by the dashboard's `Verify` action, returning intact, modified-after-sealing, timestamp status and trust status. Done when: it agrees with pyHanko on all golden files.
- [ ] Add an assertion that staging output shows `untrusted` against the default trust store and `trusted` only against the test root. Done when: the test checks both.
- [ ] Add Adobe Acrobat as a recorded manual check in `docs/reviews/` for the first production seal only (operator evidence, `Needs: third-party` in `§15`). Done when: the review file template exists. Cheaper substitute: relying only on the service's own verifier.
- [ ] Commit: `cloud: golden sealed PDFs, independent validation and tamper tests`

**Test checkpoint:** `gh workflow run cloud-seal-validation.yml` (or `act` locally) passes on the golden files, and `npm --prefix cloud run test:pkg -- seal` passes with all eight tamper cases detected; the job must fail when one golden file is edited by a byte.

## 18. Retention, legal hold, data export and deletion for envelopes

Evidence must outlive convenience, and users must be able to leave. This section implements the retention policy mechanism, legal hold, export of a user's data and account deletion behaviour for envelopes per the spec. Completed envelopes other parties hold rights to follow the retention policy, not account deletion. It must never purge held data and must record every purge.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add policy settings `retention_days_completed`, `retention_days_unfinished` and `deletion_grace_days`, configurable per deployment, defaulting to keep completed envelopes until the owner deletes them and unfinished ones until expiry plus the grace period. Done when: a settings test reads the defaults and an override.
2. Implement legal hold: `POST /v1/envelopes/:id/hold` and `DELETE` for the owner or an admin, with a reason, appending `legal_hold.set` and `legal_hold.cleared`. Done when: a held envelope survives a purge run and an owner delete request returns `409 legal_hold`.
3. Implement the purge cron in `cloud/apps/api/src/envelopes/purge.ts` for envelopes past retention and not held, in this order. Step 1: write the durable fence `purge_fences(kind='envelope', id, purged_at)` and the `envelope_audit_purge` authorization row in D1, outside the Durable Object. Step 2: call the Durable Object `purge()`, which stops producers first (rejects commands, stops the outbox drain, cancels the alarm) and then calls `deleteAll()`; an object recreated after eviction reads the D1 fence on its first request and refuses to run. Step 3: delete R2 objects, then D1 rows, keeping a tombstone row of id, completion time, checkpoint count and head, and purge time. Every projection write and every effect consumer is a statement guarded by `WHERE NOT EXISTS (SELECT 1 FROM purge_fences WHERE kind = 'envelope' AND id = ?)` in the same D1 batch. Done when: the purged envelope has no objects, no D1 rows other than the tombstone and fence, and no Durable Object content; tests pause an outbox flush after its marker check, evict the object mid-purge, and deliver a delayed queue message after the purge, and in each case nothing is recreated; and the tombstone matches the checkpoint the recipients hold in their sealed copy.
4. Implement export and account deletion: the user data export includes their envelopes, sealed PDFs, certificates and audit JSON; account deletion removes unfinished envelopes after the grace period and, for completed envelopes where other parties hold copies, removes the owner link but keeps the sealed file until retention allows. Done when: an account deletion test leaves the completed envelope reachable by its recipients.

- [ ] Add the dry-run mode `POST /v1/admin/purge/dry-run` returning counts only. Done when: it deletes nothing and reports the count.
- [ ] Log every purge as a platform audit event with envelope id and policy used, never names. Done when: the audit event exists after a purge.
- [ ] Add restore protection: R2 object deletion happens only after the D1 tombstone commit. Done when: a forced failure between the steps leaves the data in place.
- [ ] Commit: `cloud: envelope retention, legal hold, export and deletion`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/envelopes/retention` passes; the held-envelope purge test must fail if the hold check is removed.

## 19. Set retention, quota and expiry defaults

The mechanism exists; the numbers are a business decision (spec, Open owner decisions: quotas, pricing or free limits, and retention defaults). This section records the owner's choices for default envelope expiry, reminder cadence, retention of completed and unfinished envelopes, monthly send quotas and the maximum document size for signing, and sets them in configuration. It waits for the owner.

**Phase:** 98
**Needs:** owner-decision
**Surface:** no

**Build order:**

1. Ask the owner for the values in one batch and record them with the date in `docs/specs/2026-10-21-recto-cloud-design.md` under Decisions. Done when: the table lists each value and the cost of changing it.
2. Apply the values to `cloud/apps/api/wrangler.jsonc` vars and the settings defaults, with a test that reads the deployed defaults. Done when: the test asserts every recorded number.
3. Update the sender UI help text and the `§18` policy defaults to match, recording them in the same owner batch as `D09 T02 §21`. Done when: no screen shows a number that differs from configuration.

- [ ] Add `cloud/apps/api/test/envelopes/defaults.test.ts` reading the spec table and the configuration and asserting equality. Done when: changing a configuration value without the spec fails the test.
- [ ] Commit: `cloud: record and apply envelope retention, quota and expiry defaults`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/envelopes/defaults` passes; editing one value in `wrangler.jsonc` only must make it fail.

## 20. Approve ESIGN consent disclosure and signer legal texts

The consent disclosure, terms of service and privacy policy are legal documents that need legal review (spec, Open owner decisions, third-party). This section gets the approved texts into the product as versioned content. Until approved, production refuses to send; staging uses a fixture version marked not for production. It does not provide legal advice.

**Phase:** 98
**Needs:** third-party, owner-decision
**Surface:** no

**Build order:**

1. Prepare the checklist for the reviewer from 15 U.S.C. 7001(c): the statement of the right to paper copies, the right to withdraw consent and the consequences, the scope of the consent (this envelope or all records), how to update contact details, how to request a paper copy and any fee, and the hardware and software requirements to access and retain the records. Done when: the checklist file `cloud/docs/legal/esign-checklist.md` lists each item with the field of the product that meets it.
2. Store each approved text as `cloud/apps/web/content/legal/consent-v<N>.md` with a version, effective date and reviewer name in frontmatter. Done when: the server loads the highest approved version and records it in `consent.accepted` events.
3. Make production fail closed: with no approved consent version configured, `POST /v1/envelopes/:id/send` returns `503 consent_not_configured`. Done when: a test with `ENVIRONMENT=production` and no version fails the send.
4. Link the terms of service, privacy policy and data processing agreement from the sign pages and email footers once approved. Done when: the links resolve and are versioned.

- [ ] Add the staging fixture `consent-v0-staging.md` containing the words `STAGING TEXT NOT LEGAL ADVICE` and refuse it when `ENVIRONMENT` is `production`. Done when: a test confirms both behaviours.
- [ ] Add the reviewer evidence record in `docs/reviews/` with date, reviewer and the version approved. Done when: the file exists and names `consent-v1.md`.
- [ ] Commit: `cloud: approved ESIGN consent disclosure and legal texts`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/sign/consent-config` passes; the production fail-closed test must fail if the guard is removed.

## 21. Sender dashboard: envelopes, status, audit view and downloads

The sender must see what is happening and fetch the results. This section builds the envelopes list, an envelope detail page with a timeline from the audit trail, actions (remind, void, extend, hold, download), and the `Verify` action using `verifySealedPdf`. It follows `design/SPEC.md` brand, colour and type. It must show status in text and not by colour alone, and must never imply a stronger signature than the one the service gives.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` sections 3, 4, 5 and 7 and the list-and-detail rhythm of section 9.7 (Settings groups) with `design/screenshots/D-Settings.png`.
**Job:** A sender opens `Envelopes`, filters by status, sees who has signed and who is waiting, opens one to read the timeline, sends a reminder, voids with a reason, extends the deadline, places or clears a legal hold, downloads the sealed PDF, the certificate and the audit JSON, and verifies a downloaded file. Consumer: `§5` and `§10` provide actions; `§16` provides outputs.
**Treatment:** Pages under `cloud/apps/web/src/envelopes/` with a table at 1280 pixels and a stacked list at 360 pixels, a timeline built from audit events with human text, and a confirmation dialog for void that names the recipients who will be told. Cheaper substitute that fails the checkpoint: a raw JSON dump of events, or a void button with no confirmation.
**Chrome:** consume the token stylesheet; status chips use text plus an icon from the shared icon set and the success, warning and danger tokens with AA contrast; icon-only buttons have `aria-label` and `title`.

**Build order:**

1. Add `EnvelopesListPage.ts` with filters (draft, sent, in progress, completed, declined, voided, expired), search by title or recipient, and sort. Done when: 500 envelopes page in under 1 second.
2. Add `EnvelopeDetailPage.ts` with recipient progress, the timeline, and action buttons enabled by state. Done when: each action is hidden or disabled in states where the API refuses it.
3. Add the downloads and `Verify` panel running `verifySealedPdf` in the browser on a chosen file against the published trust roots and showing intact, modified or not sealed by Recto Cloud. Done when: a tampered file shows `Modified after sealing`.

- [ ] Add the void dialog with a reason and a recipient list. Done when: cancelling sends nothing.
- [ ] Add empty, loading and error states with retry. Done when: each state has a screenshot test.
- [ ] Add accessibility: table semantics, labelled actions, axe-core zero serious violations, forced-colors screenshot. Done when: the checks pass.
- [ ] Commit: `cloud-web: sender dashboard for signature requests`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/envelope-dashboard.spec.ts` passes; the spec fails if void proceeds without the confirmation dialog.

## 22. End-to-end signature ceremony tests in the browser

The whole flow must work as a person would use it. This section adds Playwright tests for a sender and three signers, plus negative cases, against the local `workerd` stack with a fake mail transport and the test seal. It must run on every pull request and cover every state of the machine.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add `cloud/apps/web/e2e/signatures.spec.ts`: the sender uploads a PDF, places fields for three sequential signers, sends, each signer opens the emailed link in a fresh context, consents, adopts (typed, drawn, uploaded respectively), fills and signs, then the test downloads the sealed PDF and verifies it with the service verifier and `pdf.js` text extraction. Done when: the happy path passes locally.
2. Add the state cases: a signer declines (envelope `declined`, others' links `410`), the sender voids (`voided`), the Cron handler expires an envelope (`expired`), and a parallel envelope completes in either order. Done when: each case asserts the final status in the dashboard.
3. Add the security cases: replaying a used link gives the relink page, a wrong access code five times locks, a signer cannot call another recipient's field route, and a signature for a voided envelope is rejected. Done when: each asserts the expected status and page text.
4. Add the audit and seal assertions: export `audit.json`, run `verify.mjs`, check the head matches the certificate text, and run `pdfsig` where installed. Done when: all pass in CI.

- [ ] Wire the spec into the CI workflow from `D09 T01 §20` with traces on failure. Done when: a deliberately failing assertion turns CI red.
- [ ] Add the mobile project (viewport 390 by 844) for the signer path. Done when: the signer path passes at that size.
- [ ] Add a repeat run `--repeat-each=3` on pull requests to catch flakiness. Done when: the workflow configures it.
- [ ] Commit: `cloud-web: end-to-end signature ceremony tests`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/signatures.spec.ts --repeat-each=3` passes all repeats; removing the Durable Object transition guard for voided envelopes makes the void case fail.

## 23. Envelope templates: reusable recipients, roles and fields

Senders repeat the same document, roles and field layout. A template stores that so a new envelope starts prepared. It matches the template feature of Foxit, PDFelement and UPDF while keeping Recto's rules: owner-only sending, a pinned immutable version and no document bytes copied. A template never sends anything by itself.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add tables `envelope_templates` (id, owner_id, document_id, version_id, name, settings, created_at, archived_at) and `template_roles` and `template_fields` in a migration in `cloud/packages/db/migrations/` (next free number); roles carry `role`, `label`, `routing_order`, `signing_mode` and no email address, and fields reference a role, never a person. Done when: the migration applies and a field referencing a missing role fails a foreign key.
2. Add routes `POST /v1/templates` (from scratch or from an envelope draft), `GET /v1/templates`, `GET /v1/templates/:id`, `PUT /v1/templates/:id`, `DELETE /v1/templates/:id` (archive), `POST /v1/templates/:id/instantiate` taking the people for each role and returning a draft envelope. Done when: each route has schemas and tests, and only the document owner can create or use a template (`403 not_owner` for `view`, `comment` and `edit` roles, as in `§5`).
3. Implement `instantiate` as a pure copy into the `§5` draft API: same pinned version, the roles filled with the given names and emails, fields copied with new ids, and an `envelope.created_from_template` event appended with the template id (no names). Done when: the draft passes the same `send` preconditions as a hand-made one, and a template whose version was trashed returns `409 version_unavailable`.

- [ ] Add `cloud/apps/api/src/templates/` with store, routes and the instantiate function. Done when: `npm --prefix cloud run test:api -- test/templates` passes.
- [ ] Reuse the field placement UI of `§6` with a `template` mode that assigns fields to roles instead of people. Done when: the template editor saves roles and fields and the UI test of `§6` still passes. Cheaper substitute: a second placement editor.
- [ ] Enforce limits: 100 templates per account, 10 roles and 500 fields per template. Done when: the 101st template returns `422 template_limit`.
- [ ] Add the template data to the user data export of `D09 T01 §15` and to account deletion. Done when: an export test finds the templates and a deletion test removes them.
- [ ] Commit: `cloud: envelope templates with roles and fields`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/templates` passes; a test that instantiates a template as a `comment`-role user must fail if the owner check is removed.

## 24. Bulk send: one template to a CSV, one envelope per row

Sending the same agreement to many people is a common job. Bulk send turns a template and a CSV of recipients into one separate envelope per row, so each signer sees only their own envelope and one failure never blocks the rest. It must be owner-only, rate limited, bounded, audited per envelope and never a way to spam strangers.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add `POST /v1/templates/:id/bulk` accepting a CSV up to 1 MiB and 500 rows (columns: one `name` and `email` per role, plus optional `message`), parsed by a bounded RFC 4180 parser in `cloud/apps/api/src/bulk/csv.ts` (no formulas evaluated, UTF-8 only, a header row required, quoted fields supported, line and field length limits). Done when: 30 malformed CSV fixtures return `422` with row numbers and nothing is created.
2. Validate every row before creating anything: email syntax, duplicates within the file, role coverage, and the account's daily send limit and active-envelope limit from `§5`. Done when: a file with one bad row creates no envelope and returns all row errors.
3. Create a `bulk_jobs` row and process rows through the `JobQueue`, one message per row, each creating and sending its own envelope through the `§5` functions (so every `send` precondition applies) with the `envelope.created_from_template` event and a `bulk_job_id`. Rows are idempotent by `(bulk_job_id, row_number)`. Done when: killing the consumer mid-run and restarting produces exactly one envelope per row.
4. Add `GET /v1/bulk/:id` returning per-row results (`queued`, `sent`, `failed` with a code) and `GET /v1/bulk/:id/results.csv`, whose cells starting with `=`, `+`, `-` or `@` are prefixed with a quote so spreadsheets do not execute them. Done when: a test with such cells passes.

- [ ] Rate limit with named policies in `cloud/apps/api/src/ratelimit/policy.ts`: 5 bulk jobs per day per account, 500 envelopes per day per account, and at most 3 invitations per recipient address per day across all senders. Done when: boundary tests pass and a row beyond the per-address limit fails with `recipient_limited` while other rows proceed.
- [ ] Require an attestation field `attest_permission` (the sender confirms they may contact these people) stored on the job and shown in the audit view. Done when: a request without it returns `422`.
- [ ] Allow cancel: `POST /v1/bulk/:id/cancel` stops unprocessed rows and leaves sent envelopes alone. Done when: a mid-run cancel test shows the split.
- [ ] Add audit and security events with identifiers only (job id, counts), never addresses. Done when: a log test greps for `@` and finds none.
- [ ] Commit: `cloud: bulk send from a template and a CSV`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/bulk` passes including the kill-and-resume case; removing the `(bulk_job_id, row_number)` idempotency key makes that case create duplicates and fail.

## 25. Web form link engine: one envelope per submitter with abuse limits

A web form link lets anyone with the link start their own signature of a document, such as a waiver or an application, without the owner creating each envelope. This is the highest abuse risk in the file, so it is narrow: a published link belongs to a template with exactly one `signer` role, the submitter proves control of their email address before anything is created, and the link has hard caps. It must never become an open relay or reveal who has signed.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add table `form_links` (id, owner_id, template_id, token_hash, status `active|paused|closed`, max_submissions, expires_at, per_email_limit, created_at) and routes to create, list, pause, close and delete a link, owner-only. A link token is 256 bits and only its hash is stored. Done when: a template with more than one signer role or any role with `in_person` returns `422 form_unsupported`.
2. Add the public routes `GET /v1/forms/:token` (title and sender name only, no document content) and `POST /v1/forms/:token/start` taking `name`, `email` and a proof-of-work solution. Start creates nothing in the library: it stores a pending submission and emails a single-use confirmation link to that address, and only redeeming that link creates the envelope from the template (`envelope.created_from_form`) and begins the `§8` ceremony in the same session. Done when: a submitter who never confirms causes no envelope and no document access.
3. Implement abuse limits: a self-hosted proof-of-work challenge (SHA-256 with a difficulty that costs about one second of client CPU, issued per request and valid for 5 minutes; no third-party captcha or tracker), policies `form-start` (5 per hour per IP hash, 3 per day per email hash per link), `form-link` (200 starts per hour per link), the link's `max_submissions` (default 100, maximum 1000) and `expires_at` (maximum 365 days). Done when: boundary tests pass and the link auto-pauses at its cap with an owner email.
4. Make the response identical for known and unknown or blocked addresses and keep timing within 50 ms, so the form cannot be used to learn who has signed. Done when: the equality test passes.

- [ ] Add the owner's per-link submissions list (counts and status, with names and emails visible only to the owner). Done when: a non-owner request gets `403`.
- [ ] Add the abuse switch: a platform admin or the owner can pause a link, and a link whose submissions bounce above 30 percent pauses itself. Done when: a test with 10 bounced starts pauses the link.
- [ ] Record `form.started`, `form.confirmed`, `form.paused` as security events with identifiers only. Done when: a log test finds no email addresses.
- [ ] Commit: `cloud: web form links with confirmation, proof of work and caps`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/forms` passes; a test that posts 6 starts from one IP in an hour must fail if the `form-start` policy is removed.

## 26. Web form link page and owner management

People who follow a form link need a clear, short page, and the owner needs to manage links without a developer tool. This section builds both. It follows `design/SPEC.md` brand, colour and type, works on a phone, and never implies an identity check beyond email control.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` sections 3, 4, 5 and 7 and the dialog pattern of 9.9 with `design/screenshots/D-Dialogs.png`.
**Job:** A visitor opens the link, sees who is asking and the document title, enters their name and email, waits a moment while the browser solves the challenge, is told to check their email, and continues in the signing ceremony from the emailed link. The owner creates, copies, pauses and closes links and sees counts. Consumer: `§25` is the API; `§9` is the ceremony.
**Treatment:** A page at `/f/:token` and an owner list under `cloud/apps/web/src/forms/`, with the proof of work in a Web Worker so the page stays responsive, a progress message, and an error state that never says whether an address was seen before. Cheaper substitute that fails the checkpoint: a form that creates the envelope on submit without email confirmation, or a third-party captcha script.
**Chrome:** consume the token stylesheet; one primary button in accent; touch targets at least 44 CSS pixels; icon-only controls carry `aria-label` and `title`.

**Build order:**

1. Add `FormStartPage.ts` with the fields, the worker-based challenge, and the check-your-email state. Done when: the page works at 360 pixel width with no horizontal scroll.
2. Add `FormLinksPage.ts` (create from a template, copy link, pause, close, limits, counts). Done when: copy is announced to screen readers.
3. Add accessibility and theme checks. Done when: axe-core finds zero serious violations and screenshot tests exist for light, dark and forced-colors.

- [ ] Add the claims lint to cover the form pages. Done when: the lint of `§9` scans these strings too.
- [ ] Add the Content Security Policy check (no inline script; the worker is a same-origin file). Done when: zero violations logged.
- [ ] Commit: `cloud-web: web form link page and owner management`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/forms.spec.ts` passes; the spec fails if the page creates an envelope before the emailed confirmation.

## 27. In-person signing: hand a device to a signer, attested in the audit

Some signers are in the room. In-person signing lets the sender open the ceremony for that recipient on the sender's own device and hand it over, without emailing a link. The signer still sees and accepts the electronic records disclosure and signs themselves; what changes is the identity evidence, which is now the sender's attestation and not control of an email address, and the audit trail and certificate must say exactly that. It must be owner-only, short-lived and never usable to sign for someone who is not there.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Mark a recipient `signing_mode: in_person` at draft time (`§2`, `§5`). Such a recipient gets no emailed link. Done when: `send` for an envelope with an in-person recipient enqueues no invitation for that recipient and still emails the sender's own notices.
2. Add `POST /v1/envelopes/:id/recipients/:rid/in-person` for the document owner with a recent authentication (`D09 T01 §7`, within 10 minutes) when it is that recipient's turn in the routing order. It appends `inperson.started` (sender id, a device note up to 80 characters, no address) and issues a ceremony session cookie bound to that recipient with idle 15 minutes and absolute 60 minutes, valid only for that one recipient and only on the browser that requested it. Done when: a request out of turn, without recent authentication, or by a non-owner is refused.
3. Run the normal `§8` steps, including consent (the signer types the consent-check code themselves) and the optional access code. Add the sender hand-back: `POST /v1/sign/hand-back` ends the session and appends `inperson.handed_back`; the signer's screen then asks for the sender to re-authenticate before anything else shows. Done when: after hand-back the ceremony cookie is invalid and the sender's session has not been given the signer's adopted images.
4. Record the identity method on the recipient (`email_link` or `in_person_attested`), show it in the certificate block (`§13`: `Identity: attested in person by <sender name>`) and in the audit export. Done when: the certificate fixture for an in-person recipient contains that line and not an email-possession statement.

- [ ] Forbid in-person mode on form links (`§25`) and bulk rows (`§24`). Done when: both return `422`.
- [ ] Add a `sender_is_signer` guard: when the sender's account email equals the recipient email, the envelope records that fact and the certificate shows it. Done when: the fixture shows the line.
- [ ] Add the claims lint entry: no text may call in-person attestation a verified identity or a qualified signature. Done when: the lint fails on `verified identity`.
- [ ] Commit: `cloud: in-person signing attested by the sender in the audit trail`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/sign/in-person` passes; a test that starts an in-person session without recent authentication must fail if that check is removed.

## 28. In-person hand-over screens and templates, bulk and form pages

The owner needs screens to use sections 23, 24 and 27: pick or save a template, upload a CSV and see per-row results, and run the hand-over safely. This section builds them in the same web app, using the same tokens, and reuses `§6`, `§9` and `§21`.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` sections 3, 4, 5 and 7, the dialog pattern of 9.9 with `design/screenshots/D-Dialogs.png`, and the list-and-detail rhythm of `§21`.
**Job:** A sender saves an envelope as a template, starts an envelope from a template, uploads a CSV with a preview of the first rows and the exact count, confirms with the permission attestation, watches per-row results and downloads them, and for an in-person recipient presses `Sign in person`, hands the device over, and takes it back by authenticating again. Consumer: `§23`, `§24` and `§27` are the APIs.
**Treatment:** Pages under `cloud/apps/web/src/templates/` and `cloud/apps/web/src/envelopes/` with a confirmation summary before bulk send that states the count and the template, and a full-screen hand-over mode that hides every sender control. Cheaper substitute that fails the checkpoint: a bulk upload that sends on file selection, or a hand-over that leaves the sender's navigation visible.
**Chrome:** consume the token stylesheet; the hand-over screen uses large type from the shared scale and one primary button; icon-only controls carry `aria-label` and `title`.

**Build order:**

1. Add `TemplatesPage.ts` and the template picker in the start flow. Done when: a template creates a draft in two clicks.
2. Add `BulkSendPage.ts` with CSV preview, row errors, attestation, confirmation and live per-row results with the download. Done when: a file with errors shows them by row and sends nothing.
3. Add `HandOverScreen.ts` for in-person mode with the `Return device to sender` flow. Done when: after return, the sender view needs authentication to continue.

- [ ] Add accessibility and theme checks for the three pages. Done when: axe-core finds zero serious violations and forced-colors screenshots exist.
- [ ] Add the in-person option and role labels to the recipients panel of `§6`. Done when: the existing `§6` spec still passes.
- [ ] Commit: `cloud-web: template, bulk send and in-person hand-over screens`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/templates-bulk.spec.ts` passes; the spec fails if the bulk page sends before confirmation.

## 29. End-to-end tests for templates, bulk send, web forms and in-person

The four features must work end to end in the browser and refuse abuse. This section adds Playwright specs against the local stack with the fake mail transport and the test seal, and wires them into CI.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add `cloud/apps/web/e2e/templates-bulk.spec.ts`: create a template, send it to a 20 row CSV, assert 20 separate envelopes, sign two of them, and download the result CSV. Done when: the happy path passes.
2. Add `cloud/apps/web/e2e/forms.spec.ts`: publish a link, submit with a valid challenge, confirm from the emailed link, sign, then assert the cap pauses the link and a sixth rapid start from one context is refused. Done when: all assertions pass.
3. Add `cloud/apps/web/e2e/in-person.spec.ts`: the sender starts an in-person ceremony, the signer signs on the same device, the sender re-authenticates, and the certificate shows the attestation line; a second browser context cannot use the ceremony cookie. Done when: all assertions pass.
4. Add a negative suite: a non-owner cannot instantiate, bulk send or publish, a CSV with formula cells is neutralised in the results, and a form link for a multi-signer template is refused. Done when: each case asserts its status code.

- [ ] Wire the specs into the CI workflow from `D09 T01 §20` with traces on failure. Done when: a deliberately failing assertion turns CI red.
- [ ] Commit: `cloud-web: end-to-end tests for templates, bulk send, web forms and in-person signing`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/templates-bulk.spec.ts e2e/forms.spec.ts e2e/in-person.spec.ts` passes; removing the email confirmation requirement makes the forms spec fail.

## Verification

- Run the full path in staging with the test certificate: a sender sends an envelope to three signers in sequence, they sign in the browser (one on a phone width), the completion job produces the sealed PDF and certificate, and the independent validators report an intact signature and a valid timestamp against the test root and `untrusted` against public roots.
- Run the tamper suite and keep the output: eight cases (four byte-level, four audit-level) are all detected.
- Confirm the production gates: with `ENVIRONMENT=production` and no approved consent version or no HSM signer, sending and sealing refuse to run (`§15`, `§20`).
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
