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

## Outcome

- `cloud/packages/pdf-lite` reads hostile PDFs within hard limits and appends incremental updates, and `cloud/packages/audit-chain` appends and verifies a SHA-256 hash chain of events.
- An `Envelope` Durable Object enforces the draft, sent, in progress, completed, declined, voided and expired state machine, and an API plus a field placement UI let a sender prepare and send an envelope with signer, approver and copy recipients.
- Signers complete a consent, identity, adopt, fill and sign ceremony through single-use links, with an optional access code, a decline path, Cron reminders and expiry, and sender void.
- A Queue job produces a sealed PDF with a certificate of completion, a PAdES-B-T signature and an RFC 3161 timestamp, validated by an independent validator in CI, with tamper tests that detect any change to the bytes or the audit chain.
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

## 1. Bounded PDF reader and incremental update writer package

The cloud must read an uploaded PDF enough to count pages, find page boxes and append content, and must never trust it. This section creates `pdf-lite`, a small TypeScript package that parses within hard limits and writes only incremental updates, so the original bytes stay intact and an earlier signature survives. It is shared with shared-review export (`D09 T03 §11`). It must not execute or interpret any PDF content stream, must refuse encrypted or malformed input with a typed error, and must stay inside Worker time and memory limits. It keeps Recto's rule that hostile files never run code (D9).

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Create `cloud/packages/pdf-lite` (`@recto-cloud/pdf-lite`, AGPL-3.0-or-later, strict TypeScript) with `readPdf(bytes, limits): Result<PdfView, PdfLiteError>` that parses the header, the last cross-reference section (table or stream), the trailer and the page tree only. Done when: a corpus of 30 valid PDFs reports correct page counts and boxes.
2. Define limits as constants: input 200 MiB read only through a `RangeSource` (`read(offset, length)` backed by R2 range reads, never the whole file in memory), 20000 objects visited, object nesting depth 32, page tree depth 16, 10000 pages, cross-reference chain length 64, decompressed object stream 16 MiB, at most 16 MiB of new content per update, a peak heap budget of 64 MiB for any operation (a Worker isolate has 128 MB shared by concurrent requests), and a wall-clock budget of 2 seconds for parsing checked cooperatively. Done when: each limit has a fixture that fails one unit over.
3. Refuse with typed errors: `encrypted`, `malformed`, `too_large`, `too_complex`, `unsupported_xref`, `timeout`. Done when: a table test maps 25 hostile fixtures to the expected error and none throws an untyped exception.
4. Implement `writeUpdate(view, changes, sink: WritableStream): Promise<UpdateInfo>` that streams the original bytes from the `RangeSource` to the sink and then appends only the new and replaced objects, a new cross-reference section and trailer with `/Prev`, so memory holds the appended tail and never the original. Done when: the output starts with the exact original bytes, re-parses with the same page count, and a 200 MiB fixture is written with a peak heap under 64 MiB measured by the package test run with a 96 MiB V8 heap limit.

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
- [ ] Define `Recipient` columns: `id`, `envelope_id`, `role`, `name`, `email`, `routing_order`, `access_code_hash`, `access_code_attempts`, `status` (`pending`, `notified`, `viewed`, `consented`, `completed`, `declined`). Done when: two recipients cannot share the same email within one envelope for the same role.
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
3. Implement two functions and keep their meanings apart. `verifyChain(events): VerifyResult` checks internal consistency only and returns `consistent` with the head and count, or the first failing `seq` and reason (`gap`, `prev_mismatch`, `hash_mismatch`, `time_regression`, `schema`); it cannot see removed trailing events or a fully recomputed chain. `verifyAgainstCheckpoint(events, checkpoint: {count, head})` additionally requires the event at `seq = count` to have `hash = head`, and is the only function that may report `authenticated`. Done when: mutating any single field of any event in a 50-event chain is detected at the right `seq` by `verifyChain`, and truncating to a shorter prefix or recomputing every hash after an edit is detected only by `verifyAgainstCheckpoint`.
4. Implement the authoritative append inside the `Envelope` Durable Object storage (`cloud/apps/api/src/envelopes/audit.ts`, called by `§4` in the same storage transaction as the transition) and the D1 projection `envelope_audit` with unique `(envelope_id, seq)`. Add SQL triggers on the projection refusing every `UPDATE`, and refusing `DELETE` unless a row exists in `envelope_audit_purge(envelope_id, authorized_at, reason)`; only the purge job of `§18` writes that row, in the same D1 batch as the deletes. Done when: a test that updates or deletes a row without authorization fails and the authorized purge succeeds.

- [ ] Define the event types: `envelope.created`, `envelope.sent`, `link.issued`, `link.redeemed`, `recipient.viewed`, `consent.accepted`, `consent.declined`, `access_code.failed`, `access_code.locked`, `signature.adopted`, `field.completed`, `recipient.signed`, `recipient.approved`, `recipient.declined`, `reminder.sent`, `envelope.voided`, `envelope.expired`, `envelope.completing`, `envelope.sealed`, `envelope.completed`, `legal_hold.set`, `legal_hold.cleared`. Done when: the schema test lists exactly these.
- [ ] Add a CLI `cloud/packages/audit-chain/bin/verify.mjs <audit.json> [--checkpoint <count>:<head>]` that prints `CONSISTENT <head> (not authenticated)` without a checkpoint, `AUTHENTIC <head>` with a matching one, or the failure, and exits 0 or 2. The checkpoint is read from a sealed PDF whose seal verified (`§13`, `§17`). Done when: running it on a good, a tampered and a truncated export gives 0, 2 and 2 with a checkpoint, and the output never says `AUTHENTIC` without one.
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
- [ ] Make the Durable Object's own SQLite storage the single authority: one storage transaction writes the new `status`, the next audit event (with its `prev` and `hash`) and one `outbox` row per side effect (D1 projection of the status and the event, queue messages, emails). An alarm drains the outbox in order and deletes a row only after its effect succeeded; every effect is idempotent (D1 upserts by `(envelope_id, seq)`, queue messages carry a `dedupe_key` that consumers check, emails keyed by event `seq`), and on wake the Durable Object resumes any undrained rows. D1 is a query projection and never an input to a transition. Done when: crash-injection tests that abort after the storage commit and before each effect (D1 projection, enqueue, email) end, after restart, with exactly one of each effect and a D1 chain equal to the authority's chain.
- [ ] Add a concurrency test: 10 parallel `sign` commands from the last two parallel signers produce exactly one completion enqueue. Done when: the queue stub shows one message, and a replay of the drained outbox after a crash does not enqueue a second.
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
2. Print the sealed checkpoint: the `seq` and hash of the `envelope.completing` event (the last event the seal covers), as `count` and `head`, and write both into the sealed file's Info dictionary keys `RectoEnvelopeChainCount` and `RectoEnvelopeChainHead`. Events appended after sealing (`envelope.sealed`, `envelope.completed`, notices) are outside the checkpoint by design and are only chained after it. Done when: `verify.mjs --checkpoint` on the exported audit, which holds more events than the checkpoint, reports `AUTHENTIC` with the printed head.
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

- [ ] Implement `sealPdf(source: RangeSource, update, signer, tsa, sink, options): Promise<Result<SealInfo, SealError>>` in two streamed passes: hash the original bytes and the in-memory tail pieces around `/Contents` with a digest stream, build the CMS, then write the original and the filled tail once to the sink, so the tail (not the original) is the only buffer and no unsealed full PDF is ever written. Done when: a re-read finds the signature and `/ByteRange` covering all bytes except `/Contents`, and a 200 MiB fixture seals under the 64 MiB heap budget.
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

1. Add `cloud/apps/api/src/envelopes/completion-job.ts` consuming `envelope-completion` messages carrying only the envelope id. Done when: a message with an unknown id is acknowledged and logged without retry.
2. Implement the pipeline: take the frozen checkpoint (the `envelope.completing` event, already in the chain), load the pinned version, verify its SHA-256 matches the envelope record and that each recipient's recorded `values_hash` (`§8`) equals the hash of the values and image hashes being rendered, apply values (`§11`) and images (`§12`) as a streamed incremental update, build the certificate from events 1 to the checkpoint (`§13`), seal (`§14` or `§15`), store the result as a new immutable version through `commitVersion(bytes, {source: 'envelope'})`, a function extracted in this section from `cloud/apps/api/src/uploads/commit-job.ts` so the upload path and this path share the blob and version row logic, record `sealed_version_id`, then call the Durable Object `finalize(generation)` (the `seq` of `envelope.completing`), which appends `envelope.sealed` (sealed SHA-256, version id) and `envelope.completed` after the checkpoint and moves the state to `completed`. Done when: an end-to-end test on the Workers pool yields a new version whose bytes verify.
3. Make the job idempotent: if `sealed_version_id` is set, only the notification step may run; use a unique index on `(envelope_id)` for the version link. Done when: running the job twice creates one version.
4. Implement failure handling: retry up to 6 times with exponential backoff, then dead-letter, append `envelope.completion_failed` through the Durable Object, which allows the sender to void or request a retry, and email the sender and the operator contact. The job's final `finalize(generation)` is conditional on the generation still matching and the status still `in_progress`. Done when: a forced seal failure test shows the dead-letter path and the sender email.

- [ ] Notify every party (signers, approvers, copy recipients, sender) with a link: account holders get the document link, others get a single-use download link valid 14 days that returns the sealed file and the certificate. Done when: each recipient kind receives the expected link type.
- [ ] Store the exported audit trail JSON next to the sealed version as `audit.json` and expose it through an authenticated route. Done when: `verify.mjs` on the download returns `OK` with the printed head.
- [ ] Extract `commitVersion` into `cloud/apps/api/src/uploads/commit-version.ts` and call it from `commit-job.ts` and this job. Done when: `test/uploads/commit-job.test.ts` still passes unchanged and the job test shows one blob keyed by the sealed bytes' SHA-256.
- [ ] Charge the sealed version to the owner's quota (`D09 T02 §7`) without blocking completion when the quota is exceeded: record the overage and notify the owner. Done when: a test over quota still completes and the owner is notified.
- [ ] Never write an unsealed full PDF anywhere: the sealed object is written once by the streamed pipeline. Done when: a test lists R2 keys after the job, including after a failure injected mid-seal, and finds only the sealed outputs and no staging object.
- [ ] Bound resource use: read through `RangeSource`, write through the sink, hold at most the 64 MiB heap budget of `§1`, and refuse inputs over 200 MiB with a typed error before any work. Done when: an over-limit fixture ends in `completion_failed` with `too_large`, and a 200 MiB fixture completes within the heap budget.
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
4. Add the audit tamper tests: edit one event, remove one event, reorder two events, truncate the trail; edits, removal and reordering inside the chain are detected by `verifyChain`; truncation of the prefix and a fully recomputed chain are detected only by `verifyAgainstCheckpoint` against the checkpoint read from the verified seal. Done when: all four are detected, the first three at the right `seq`, and the last two are shown to pass `verifyChain` alone and fail the checkpoint check.

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
3. Implement the purge cron in `cloud/apps/api/src/envelopes/purge.ts` that deletes R2 objects and D1 rows for envelopes past retention and not held, keeping a tombstone row of id, completion time, checkpoint count and head, and purge time. The job first writes the `envelope_audit_purge` authorization row (`§3`), then deletes the D1 rows and R2 objects in that order, and calls the Durable Object `purge()`, which marks it purged, cancels its alarm, drains no further outbox rows and calls `deleteAll()`, so a delayed outbox flush cannot restore data. Done when: the purged envelope has no objects, no D1 rows and no Durable Object content, a paused outbox flush resumed after the purge writes nothing, and the tombstone matches the checkpoint the recipients hold in their sealed copy.
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

## Verification

- Run the full path in staging with the test certificate: a sender sends an envelope to three signers in sequence, they sign in the browser (one on a phone width), the completion job produces the sealed PDF and certificate, and the independent validators report an intact signature and a valid timestamp against the test root and `untrusted` against public roots.
- Run the tamper suite and keep the output: eight cases (four byte-level, four audit-level) are all detected.
- Confirm the production gates: with `ENVIRONMENT=production` and no approved consent version or no HSM signer, sending and sealing refuse to run (`§15`, `§20`).
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
