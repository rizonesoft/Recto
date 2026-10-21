---
schema_version: 1
id: cloud-reviews
domain: 09-cloud
status: active
title: "TODO-03 -- Recto Cloud Shared Reviews"
---

# TODO-03 -- Recto Cloud Shared Reviews

> **Goal:** A signed-in Recto Cloud user starts a shared review on one stored document version, invites account holders and email-verified guests, sets a deadline, and every invitee comments and marks up that version together in the browser with live updates. Comments live in Recto's annotation JSON model, so a review exports to XFDF or JSON, imports back, and lands in a reviewed PDF, and the desktop Comments panel reads and writes the same data. The feature matches Acrobat Pro's Shared Reviews and keeps Recto's zero PDF JavaScript rule (D9), because the web app renders with scripting off and the Worker never executes PDF content.

> **Current state:** Measured 2026-10-21. No `cloud/` code exists. `todo/09-cloud/TODO-01` (platform, accounts, storage, email, CI) and `TODO-02` (documents, sharing, web viewer) are being authored beside this file. The desktop annotation model, `XFDF` and `JSON` exchange and Comments panel are planned in `todo/03-text/TODO-03-annotations-and-forms.md` sections 2 and 5 and are not built. `docs/specs/2026-10-21-recto-cloud-design.md` fixes the design: a `ReviewRoom` Durable Object per review owns comment state for one document version, accepts WebSocket connections from the web app and the desktop app, orders edits and persists to D1.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` decisions D9, D16, D18, D20 and section 3.2 (the `cloud/` correction of 2026-10-21).
- `docs/specs/2026-10-21-recto-cloud-design.md` sections Architecture (Reviews), Security and privacy, Testing.
- `todo/03-text/TODO-03-annotations-and-forms.md` section 2 (`XFDF` and `JSON` exchange, `/IRT` reply threads) and section 5 (Comments panel with replies): the annotation JSON model this file mirrors in TypeScript.
- `design/SPEC.md` sections 3 (brand), 4 (colour), 5 (type), 7 (shape) and 8.3 (the Comments panel layout the web review panel follows).
- Cloudflare Workers, D1, Durable Objects (WebSocket Hibernation API), Queues and Cron Triggers documentation.
- -> XREF: D09 T02 §2 -- authorizes every review action through `resolveAccess` (D09 T02 §2).
- -> XREF: D09 T01 §9 -- sends review email through the Queue consumer of D09 T01 §9.
- -> XREF: D09 T04 §1 -- review export reuses the bounded `pdf-lite` reader and writer.
- -> XREF: D09 T05 §11 -- the desktop Comments panel syncs through the exchange API.
- -> XREF: D03 T03 §2 -- the cloud annotation JSON schema is the desktop's exchange model.

## Outcome

- `cloud/packages/annotations` validates and converts Recto annotation JSON and `XFDF`, with fixtures produced by `recto-cli annot export`, so the cloud and the desktop agree byte for byte on the model.
- `POST /v1/reviews` starts a review on a document version with invitees, a deadline and a status, and a `ReviewRoom` Durable Object serves one live room per review with ordered edits, presence, reconnection and D1 persistence.
- Invitees add, edit, reply to, mention and resolve comments in the browser, receive email through the platform queue, and the owner exports a reviewed PDF with the comments as annotations carrying appearance streams.
- Every route and socket message is authorised by role, rate limited and size capped, and tests prove concurrent edits converge and a dropped connection resumes without loss or duplicates.

**Adjacency:** list=applicable; document=applicable @ D09 T02 §4; settings=applicable; reporting=applicable; notifications=applicable; permissions=applicable @ D09 T02 §2; audit=applicable; exchange=applicable @ D03 T03 §2; reverse=applicable

**Adjacency rationale:** The reviews list is a filtered view of the owner's and invitee's reviews with status and deadline. A review pins one immutable document version from the documents layer, so the document row never changes under a review. Settings are the per-user email notification preferences and the owner's per-review deadline action. Reporting is the review summary (counts of open and resolved comments per reviewer) shown to the owner. Notifications are the invitation, mention, reply, reminder and closure emails. Permissions come from the sharing layer's access grants plus a review-specific role (owner, reviewer, viewer). Audit records review lifecycle events and exports in the platform audit log, never comment text. Exchange is the annotation JSON and `XFDF` round trip with the desktop. Reverse is closing a review and deleting a comment or review, which tombstones data rather than rewriting history.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Annotation JSON schema package for comments and markup | D03 T03 §2 | [ ] |
| 2 | §2 | Desktop exchange fixtures from recto-cli annot | §1, D03 T03 §2 | [ ] |
| 3 | §3 | Review, invitee and comment tables in D1 | §1, D09 T01 §3 | [ ] |
| 4 | §4 | Review lifecycle API: start, invite, accept, close | §3, D09 T01 §7, D09 T02 §2 | [ ] |
| 5 | §5 | ReviewRoom Durable Object: sockets, ordering, presence | §3, D09 T01 §7 | [ ] |
| 6 | §6 | Comment operations: add, edit, reply, mention, resolve, delete | §5, §1 | [ ] |
| 7 | §7 | Reconnection, resync and concurrent edit convergence | §6 | [ ] |
| 8 | §8 | Review email notifications and deadline reminders | §4, §6, D09 T01 §9, D09 T01 §18 | [ ] |
| 9 | §9 | Review access control, rate limits and size caps | §4, §5, D09 T01 §8 | [ ] |
| 10 | §10 | Annotation exchange API: JSON and XFDF import and export | §1, §6 | [ ] |
| 11 | §11 | Markup appearance streams for exported annotations | §1, D09 T04 §1 | [ ] |
| 12 | §12 | Reviewed PDF export job with comments | §10, §11, D09 T01 §9 | [ ] |
| 13 | §13 | Web review room: viewer overlay, comment panel, presence | §6, §7, D09 T02 §16 | [ ] |
| 14 | §14 | Start-a-review dialog and reviews list in the web app | §4, §13 | [ ] |
| 15 | §15 | End-to-end review tests in two browsers | §12, §14, D09 T01 §20 | [ ] |

## 1. Annotation JSON schema package for comments and markup

The review room stores comments in Recto's annotation JSON so a review round-trips with the desktop (`D03 T03 §2`). This section creates the TypeScript package that owns that model on the cloud side: types, strict validators, size and count limits, and the reply (`/IRT`) thread rules. It must not invent a second comment model, and it must stay in lockstep with the desktop exporter; section 2 proves that with fixtures. Matches Acrobat Pro's comment and markup data; keeps Recto's lossless `XFDF` and `JSON` exchange.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Create the workspace package `cloud/packages/annotations` (name `@recto-cloud/annotations`, AGPL-3.0-or-later, TypeScript strict, no runtime dependency beyond a MIT or Apache-2.0 validation library). Done when: `npm --prefix cloud run typecheck` compiles it and `npm --prefix cloud run test:pkg -- annotations` runs.
2. Define the document model: an `AnnotationSet` of `Annotation` records covering the 15 standard subtypes of `D03 T03 §1` with page index, rectangle, `QuadPoints`, `InkList`, colour, opacity, border, contents, author, created and modified timestamps, subject, state, and `inReplyTo` for threads. Done when: a record with an unknown subtype, a non-finite coordinate, or a reply cycle fails validation with a typed error naming the field.
3. Define hard limits that fit every transport: at most 2000 annotations per review, 48 KiB per annotation (below the 64 KiB socket message cap of `§9`), an aggregate of 8 MiB of annotation JSON per review (below the 16 MiB response cap of the desktop client in `D09 T05 §4`), 10000 points per ink list, reply depth 8, text 3000 characters. Done when: each limit has a test that fails one unit over the limit, and an add that would push the aggregate past 8 MiB is refused with `review_full`.

- [ ] Add `cloud/packages/annotations/src/model.ts` with the `Annotation`, `AnnotationSet`, `ReplyThread` types and `Subtype` union. Done when: `tsc --noEmit` passes and the union lists exactly the 15 subtypes named in `D03 T03 §1`.
- [ ] Add `cloud/packages/annotations/src/validate.ts` with `parseAnnotationSet(input: unknown): Result<AnnotationSet, ValidationError[]>` that never throws. Done when: `validate.test.ts` feeds 40 malformed inputs (wrong types, NaN, Infinity, cycles, oversize) and every one returns an error list, never an exception.
- [ ] Add `cloud/packages/annotations/src/limits.ts` exporting the limits as constants used by the validator and by the room (`§5`). Done when: the room and the validator import the same constants and a test asserts a single source.
- [ ] Add `cloud/packages/annotations/src/threads.ts` with `buildThreads(set)` and `wouldCreateCycle(set, id, parentId)`. Done when: a test with a 9-deep chain is rejected and a 8-deep chain is accepted.
- [ ] Add sanitisation of free text: strip control characters except newline and tab, normalise to NFC, and reject text that contains a U+0000. Done when: a test with bidi override characters keeps them (they are content) but removes `\u0000` and `\u0007`. Cheaper substitute: escaping HTML at write time, which corrupts the stored comment.
- [ ] Commit: `cloud: annotation JSON schema package with limits and thread rules`

**Test checkpoint:** `npm --prefix cloud run test:pkg -- annotations` passes; removing the cycle check in `threads.ts` makes `threads.test.ts` fail.

## 2. Desktop exchange fixtures from recto-cli annot

The cloud schema is only trustworthy if it accepts exactly what the desktop writes and the desktop accepts what the cloud writes. This section generates committed fixtures from the desktop CLI and adds conformance tests on the cloud side. It must not change the desktop exporter; a mismatch is fixed in the layer that is wrong, recorded against `D03 T03 §2`.

**Phase:** 98
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Build a fixture PDF set with all 15 annotation subtypes and a three-level reply thread using `scripts/build-corpus.py` data and `recto-cli annot import`. Done when: `recto-cli annot export --json` and `--xfdf` each produce a file for every fixture.
2. Commit the exports under `cloud/packages/annotations/fixtures/desktop/` with a `PROVENANCE.md` recording the `recto-cli` version and command lines. Done when: the files exist and the provenance names each command.
3. Add conformance tests: every desktop JSON fixture parses with `parseAnnotationSet`, and re-serialising returns a deep-equal document. Done when: the tests pass, and a fixture with one field edited fails.

- [ ] Add `cloud/packages/annotations/fixtures/desktop/PROVENANCE.md` and the exported `.json` and `.xfdf` files. Done when: `git ls-files cloud/packages/annotations/fixtures/desktop` lists at least 30 files and each is under 256 KiB.
- [ ] Add `cloud/packages/annotations/src/conformance.test.ts` parsing every desktop JSON fixture. Done when: all fixtures parse and a deliberately corrupted copy in the test fails with the expected field name.
- [ ] Add the reverse check as a desktop gtest note in the report: the cloud exports a `JSON` file the desktop `recto-cli annot import` accepts, with the cloud output stored as `fixtures/cloud/` and imported by `Cli.AnnotCommand`-style test in `D09 T05`. Done when: `fixtures/cloud/` holds one cloud-written file per subtype. Cheaper substitute: testing only desktop to cloud, which lets the cloud writer drift.
- [ ] Commit: `cloud: desktop annotation exchange fixtures and conformance tests`

**Test checkpoint:** `npm --prefix cloud run test:pkg -- annotations` passes against the committed fixtures; editing one coordinate type in a fixture makes it fail.

## 3. Review, invitee and comment tables in D1

Reviews need durable, queryable state that survives a Durable Object eviction and powers the reviews list. This section adds the D1 migrations and a typed data-access module behind the platform's database interface so the self-hosted SQLite adapter works unchanged. It must not store comment text in logs, and every table carries the owner and the pinned document version.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Write migration `cloud/packages/db/migrations/<next number>_reviews.sql` (the next free number) creating `reviews`, `review_invitees`, `review_comments`, `review_ops`, `review_guest_sessions`. Done when: the migration applies to an empty local D1 and to a database holding earlier migrations.
2. Add `cloud/apps/api/src/reviews/store.ts` with typed functions that take `SqlDatabase` from `cloud/packages/db`, not the D1 binding directly. Done when: the same tests pass against D1 and the SQLite adapter.
3. Add content deletion across every content-bearing store: when an owner deletes a review or retention expires it, the Worker calls `ReviewRoom.purge()`, which first writes a `purged` marker to Durable Object storage, cancels its alarm, closes sockets with code 1000, then calls `deleteAll()`; the flush path checks the marker before every D1 write, so a delayed flush cannot restore content. D1 then deletes `review_comments` and `review_ops` payloads for the review and keeps a tombstone row (review id, purge time) and the lifecycle audit events. Done when: a test pauses a flush, purges, resumes the flush and finds no review content in the Durable Object, D1 or R2 exports.

- [ ] Create `reviews(id, owner_id, document_id, version_id, title, status, deadline_at, deadline_action, created_at, closed_at, last_seq)` with `status` in `open`, `closed`. Done when: inserting any other status fails a `CHECK`.
- [ ] Create `review_invitees(review_id, invitee_id, kind, account_id, email_hash, display_name, role, invited_at, accepted_at, revoked_at)` with `role` in `reviewer`, `viewer` and `kind` in `account`, `guest`. Done when: a duplicate email per review is rejected by a unique index on `(review_id, email_hash)`.
- [ ] Create `review_comments(review_id, comment_id, annotation_json, author_id, parent_id, resolved, deleted, created_seq, updated_seq)` and `review_ops(review_id, seq, op_id, author_id, kind, payload, at)` with `(review_id, seq)` as primary key and `(review_id, op_id)` unique. Done when: replaying the same `op_id` cannot insert a second row.
- [ ] Add `cloud/apps/api/test/reviews/store.test.ts` on the Workers pool covering insert, status transitions, tombstoning and the unique indexes. Done when: tests pass and dropping the unique index in the migration fails one.
- [ ] Store only a salted hash of guest email in indexed columns and the address itself in a separate column read by the mailer only. Done when: a query log test shows no raw address in index keys. Cheaper substitute: indexing the plain address.
- [ ] Commit: `cloud: review tables, migration and typed store`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/reviews/store` passes on the Workers pool; removing the `(review_id, op_id)` unique index makes the duplicate-op test fail.

## 4. Review lifecycle API: start, invite, accept, close

The owner starts a review on a document version, adds invitees, and invitees accept. This section is the REST surface and state rules, separate from the live room. It must not let a review start on a version the caller cannot read, must not expose the document to an invitee beyond comment access, and must keep guests limited to the one review.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add routes in `cloud/apps/api/src/reviews/routes.ts`: `POST /v1/reviews`, `GET /v1/reviews`, `GET /v1/reviews/:id`, `PATCH /v1/reviews/:id` (title, deadline, status), `POST /v1/reviews/:id/invitees`, `DELETE /v1/reviews/:id/invitees/:inviteeId`, `POST /v1/reviews/:id/close`. Done when: each route has a request and response schema and an integration test.
2. Implement invite acceptance: an account invitee accepts after sign-in; a guest invitee redeems a single-use link from email and gets a guest session scoped to that review. Done when: a guest session cannot call any other review or document route.
3. Implement status rules: `open` to `closed` by owner or by the deadline action; closed reviews are read only and exports still work. Done when: a comment write on a closed review returns `409 review_closed`.

- [ ] Implement `startReview` in `cloud/apps/api/src/reviews/service.ts` checking through `resolveAccess` (`cloud/apps/api/src/access/resolve.ts`) that the caller is the document owner, because starting a review and inviting people extends the audience for the document, which `comment` and `edit` roles do not grant (a delegated share capability needs a decision and a change in `D09 T02 §2`; owner-only is the default and is cheap to relax later), and pinning the version. Done when: callers with `view`, `comment` and `edit` roles each get `403 not_owner`, an invitee cannot be added by anyone but the owner, and the document row is unchanged.
- [ ] Implement invitees with at most 50 per review, address normalisation, de-duplication and a per-day invite cap per owner. Done when: the 51st invitee returns `422 invitee_limit`.
- [ ] Implement guest redemption in `cloud/apps/api/src/reviews/guest.ts` using a 256-bit token stored only as a SHA-256 hash, expiry 14 days or the deadline plus 7 days, single use for the exchange to a session cookie (`HttpOnly`, `Secure`, `SameSite=Lax`, path limited to the review). Done when: reusing the link returns `410` and the session cookie works for exactly one review.
- [ ] Implement the deadline: `deadline_at` must be in the future and at most 365 days away; the `deadline_action` is `remind` or `close`. Done when: a past deadline returns `422`.
- [ ] Record `review.started`, `review.invited`, `review.accepted`, `review.closed` events in the platform audit log with identifiers only. Done when: the audit rows exist and contain no comment text or address.
- [ ] Commit: `cloud: review lifecycle API with guest redemption`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/reviews/routes` passes; a test that starts a review with a `view`-only grant must fail if the rights check is removed.

## 5. ReviewRoom Durable Object: sockets, ordering, presence

One `ReviewRoom` Durable Object per review serialises edits so every client sees the same order. This section builds the room: WebSocket upgrade with authentication, the message protocol, a server-assigned sequence, presence, hibernation and the write-ahead persistence to D1. It must not trust client ordering, must not hold the whole document, and must keep working when the Durable Object is evicted and recreated.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add `cloud/apps/api/src/reviews/room.ts` exporting the `ReviewRoom` class and bind it in `cloud/apps/api/wrangler.jsonc` as `REVIEW_ROOM` with a migration entry. Done when: a local worker starts and routes `GET /v1/reviews/:id/socket` to the room by `idFromName(reviewId)`.
2. Authenticate before upgrade in the Worker (account session or guest session, role from `review_invitees`) and pass the verified identity to the room as an attachment on the accepted socket. Done when: an unauthenticated upgrade returns `401` and a revoked invitee returns `403` without reaching the room.
3. Define the protocol in `cloud/packages/annotations/src/protocol.ts`: client messages `hello`, `op`, `ping`, `presence`; server messages `welcome`, `ack`, `op`, `presence`, `error`, `snapshot_begin`, `snapshot_chunk`, `snapshot_end`. A snapshot is sent as chunks of at most 48 KiB, each with `index` and `count`, and `snapshot_end` carries the `seq` and a SHA-256 of the assembled state. Done when: every message has a validator and an unknown type closes the socket with code 1008.
4. Implement ordering and persistence: the room assigns `seq = last_seq + 1`, writes the op to Durable Object storage in one transaction (the write-ahead log), acknowledges, broadcasts, and flushes batches to `review_ops` and `review_comments` in D1 from an alarm with retry, idempotently by `(review_id, seq)`. Done when: after the room is evicted and recreated, state loads from storage and D1 with no gap in `seq`.

- [ ] Use the WebSocket Hibernation API (`ctx.acceptWebSocket`, `webSocketMessage`, `webSocketClose`) so idle rooms cost nothing. Done when: a test sends a message after the room hibernates and receives the ack.
- [ ] Implement presence as ephemeral state (display name, colour index, current page) broadcast at most 4 times per second per client and never persisted. Done when: a test with 20 presence updates in one second shows at most 4 broadcasts from that client.
- [ ] Cap room size at 100 simultaneous sockets and 20 per account, and close the oldest socket of an account that exceeds the cap with code 1013. Done when: the 101st connection receives `error room_full`.
- [ ] Reject all `op` messages from `viewer` role sockets and from any socket when the review is closed, with typed errors. Done when: both cases are tested and no row is written.
- [ ] Commit: `cloud: ReviewRoom Durable Object with ordered ops and presence`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/reviews/room` passes on the Workers pool including the evict-and-recreate case; making the room trust a client-supplied `seq` makes the ordering test fail.

## 6. Comment operations: add, edit, reply, mention, resolve, delete

Comments are the product. This section defines the operations the room accepts and applies them to the annotation JSON, including replies, `@mentions` and resolve. It must not let one user rewrite another user's text, and it must record every change as an operation so history and undo are possible.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Define operation kinds in `cloud/packages/annotations/src/ops.ts`: `add`, `edit`, `move`, `reply`, `resolve`, `reopen`, `delete`, `restore`. Each carries `opId` (UUID from the client), `commentId`, and a validated payload. Done when: each kind has a validator and a reducer `apply(state, op)` that is pure.
2. Implement authorisation in the reducer's caller: reviewers may add, reply and edit or delete their own comments; the owner may delete or resolve any comment; viewers may do nothing. Done when: a reviewer editing another reviewer's comment gets `forbidden` and no op is persisted.
3. Implement mentions: `@` tokens in comment text resolve to invitees of the same review only, stored as `mentions: string[]` of invitee ids. Done when: mentioning an address outside the review is stored as plain text and produces no notification.

- [ ] Implement `apply` in `cloud/packages/annotations/src/reducer.ts` with `add` creating the annotation JSON, `reply` setting `inReplyTo` and rejecting a depth over 8, `edit` replacing text and updating `modified`, `resolve` and `reopen` toggling `state`, `delete` tombstoning with a `deleted` flag and keeping the content for a 7 day restore window, `restore` clearing the flag on the same `commentId` (same annotation identity, replies stay attached and are shown under a deleted placeholder meanwhile) when issued by the comment's author or the review owner inside that window, idempotent if already restored, and rejected with `gone` after the scrub. Done when: reducer unit tests cover every kind and every rejection.
- [ ] Wire the reducer into `ReviewRoom` so an op is validated, authorised, applied, logged and broadcast in that order. Done when: an invalid op returns `error` to the sender only and nothing is broadcast.
- [ ] Mark `author` from the verified identity, never from the payload. Done when: a client that sends another user's name in `author` has the field overwritten.
- [ ] Keep the original text in `review_ops` for edits and show `edited` in the model. Done when: a test reads both versions of an edited comment from the op log.
- [ ] Scrub deleted content: a handler in the review cron (`§8`) clears text and annotation payload from `review_comments` and from every `review_ops` row of a comment that was deleted more than 7 days ago, keeping ids, kinds and times. Done when: after the scrub, no earlier text of that comment exists in D1 or the room and `restore` returns `gone`.
- [ ] Document the undo contract in `cloud/packages/annotations/src/undo.ts` and in a table test: the inverse of `add` or `reply` is `delete` (author), of `delete` is `restore`, of `edit` is a new `edit` carrying the previous text (last writer wins, so another person's later edit is overwritten and the client warns first), of `resolve` is `reopen` and back, of `move` is a `move` to the previous rectangle. A client never sends an operation outside this table to undo, and never reuses an `opId`. Done when: a test applies each operation then its inverse through the reducer and ends in the original state, and `D09 T05 §11` consumes the same table.
- [ ] Commit: `cloud: review comment operations and reducer`

**Test checkpoint:** `npm --prefix cloud run test:pkg -- annotations` and `npm --prefix cloud run test:api -- test/reviews/comments` pass; a reviewer deleting another reviewer's comment must be rejected, and the test fails if the owner check is widened.

## 7. Reconnection, resync and concurrent edit convergence

Browsers sleep, Wi-Fi drops and two people edit the same comment at once. This section defines how a client resumes without losing or duplicating work and how conflicting edits resolve to one answer everywhere. It must not rely on client clocks.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Implement resume: the client sends `hello` with `lastSeq` and a list of unacknowledged `opId`s; the room replies `welcome` with all ops after `lastSeq` if the gap is at most 2000 ops, otherwise a chunked snapshot (`snapshot_begin`, `snapshot_chunk`, `snapshot_end`) with the current `seq`; a client discards a snapshot whose hash does not match and requests it again. Done when: a test disconnects a client after 10 ops and reconnects to receive exactly the missed ones.
2. Make ops idempotent by `opId`: a replayed op returns the original `ack` with the original `seq` and does not apply twice. Done when: sending the same op 5 times yields one row and 5 identical acks.
3. Define conflict rules: the room's `seq` order wins; edits to the same comment field apply in `seq` order (last writer wins on text, resolve and reopen toggle by order); an edit to a deleted comment is rejected with `gone`; the client rebases its pending ops on the server stream. Done when: property tests with 3 simulated clients and random delivery order converge to identical state.

- [ ] Add the client protocol library `cloud/packages/annotations/src/client.ts` with an op outbox, exponential reconnect (1 s doubling to 30 s with jitter), and rebase of pending ops. Done when: a test with a fake socket that drops after every third message still delivers all 100 ops once.
- [ ] Add a deterministic simulation test `cloud/apps/api/test/reviews/convergence.test.ts` with a seeded random generator, 3 clients, 200 ops and random disconnects. Done when: 50 seeds end with identical state on all clients and in D1.
- [ ] Add a test that kills the room (evict) mid-batch before the D1 flush and checks recovery from the write-ahead log. Done when: no acknowledged op is missing after recovery. Cheaper substitute: acknowledging after the D1 write only in the happy path.
- [ ] Add a test of the snapshot path with a gap over 2000 ops and with the maximum admitted review (2000 annotations, 8 MiB aggregate). Done when: the client ends in the server state without replaying the history, every message is at most 48 KiB, and a dropped chunk is detected by the hash and retried.
- [ ] Commit: `cloud: review reconnection, idempotent ops and convergence tests`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/reviews/convergence` passes for 50 seeds; disabling the `opId` de-duplication makes the replay test fail.

## 8. Review email notifications and deadline reminders

People respond to review email, not to a tab they forgot. This section sends invitation, mention, reply, closure and deadline reminder email through the platform queue and cron. It must not send comment text to anyone outside the review, must not email an author about their own action, and must respect each user's preferences.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Define notification events in `cloud/apps/api/src/reviews/notify.ts`: `invited`, `mentioned`, `reply`, `deadline_soon` (24 hours before), `deadline_passed`, `closed`. Done when: each maps to a template id and a recipient rule.
2. Enqueue through the `JobQueue` interface (`cloud/apps/api/src/jobs/queue.ts`) from the room and the lifecycle service as queue messages carrying identifiers only; the queue consumer loads text and renders the email. Done when: a message in the queue contains no comment text.
3. Batch comment activity: at most one digest email per recipient per review per 15 minutes, with mentions sent immediately. Done when: 10 comments in a minute produce one digest and one mention email.
4. Honour preferences: per user settings `review_email` in `all`, `mentions`, `none`, always sending invitations and guest links. Done when: a user with `none` receives the invitation but no digest.

- [ ] Add the review kinds to `cloud/packages/shared/src/email.ts` and their templates to `cloud/apps/api/src/email/templates.ts` as plain text and HTML with the link as a single button and no document content or tracking pixels. Done when: a snapshot test shows no image tags and no third-party hosts.
- [ ] Add a handler `cloud/apps/api/src/reviews/cron.ts` registered in `cloud/apps/api/src/jobs/scheduler.ts` and run every 15 minutes that finds reviews whose deadline is within 24 hours or passed, sends the reminder once (idempotent by a `reminded_at` column), and applies `deadline_action`. Done when: running the handler twice sends one reminder and closes the review once.
- [ ] Add an unsubscribe link per notification type signed with an HMAC token. Done when: following the link sets the preference to `mentions` or `none` and a tampered token returns `400`.
- [ ] Commit: `cloud: review notifications, digests and deadline cron`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/reviews/notify` passes using a fake mail transport; a test that mentions a non-invitee must fail if the recipient filter is removed.

## 9. Review access control, rate limits and size caps

Rooms are an easy place to leak data or be abused. This section is the hardening pass over reviews: a single authorisation function used by every route and socket message, rate limits, and size caps. It must not duplicate checks in each handler, and every refusal must be typed and tested.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add `cloud/apps/api/src/reviews/access.ts` with `authorize(identity, reviewId, action)` returning `allow` or a typed denial, and route every handler and the room through it. Done when: a lint-style test fails the build if a review route does not call `authorize`.
2. Add rate limits as named policies in `cloud/apps/api/src/ratelimit/policy.ts` applied with the `limit(policyName, keyFn)` middleware: 60 ops per minute per socket, 300 per minute per review, 10 invitations per hour per owner, 20 guest link redemptions per hour per IP. Done when: the 61st op in a minute returns `error rate_limited` and the socket stays open.
3. Add size caps: 64 KiB per message, 1 MiB per review comment payload per minute, 2000 comments per review and the 8 MiB aggregate of `§1`. Done when: each cap has an over-limit test, including a review filled to exactly the aggregate cap.

- [ ] Add a cross-review isolation test: an invitee of review A calls every route and opens a socket for review B. Done when: all return `403` or close with 1008.
- [ ] Add a revoked-invitee test: revoking an invitee closes that person's open sockets within 5 seconds. Done when: the test observes the close frame.
- [ ] Add output filtering at the model layer: viewers and guests receive display names but never other invitees' email addresses. Done when: a response test greps for `@` in the guest payload and finds none.
- [ ] Add audit events for denied access attempts, with identifiers only and rate limited to avoid log floods. Done when: 1000 denied attempts write at most 20 audit rows per minute.
- [ ] Commit: `cloud: review authorization, rate limits and caps`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/reviews/access` passes; removing the `authorize` call from the invitee-delete route fails the route-coverage test.

## 10. Annotation exchange API: JSON and XFDF import and export

Desktop and web users move comments between a PDF and a review. This section adds the HTTPS API for exporting a review's comments as Recto annotation JSON or `XFDF` (ISO 19444-1) and importing a file into a review as new comments. It must not accept a PDF here, parse nothing but JSON and XML, and bound both.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add `GET /v1/reviews/:id/annotations?format=json|xfdf` returning the live state at the current `seq`, with the `seq` in a response header. Done when: a downloaded file validates against the schema and parses with the desktop fixtures' reader (`§2`).
2. Add `POST /v1/reviews/:id/annotations` accepting JSON or `XFDF` up to 5 MiB, converting to operations applied through the room as one batch attributed to the caller, with imported author names preserved in a `importedFrom` field. Done when: importing a desktop export reproduces every annotation and reply thread in the room.
3. Implement the `XFDF` reader and writer in `cloud/packages/annotations/src/xfdf.ts` using a hardened XML parser with entity expansion off, external entities off, depth limit 64, and size limit 5 MiB. Done when: a billion-laughs file and an external-entity file are refused with a typed error.

- [ ] Map the model to `XFDF` for all 15 subtypes including `QuadPoints`, `InkList`, colours, dates and `inreplyto`. Done when: a round trip of the fixture set through `XFDF` and back is deep equal.
- [ ] Reject imports into a closed review, imports from viewers, and imports exceeding the review limits with all-or-nothing semantics. Done when: a batch with one invalid annotation applies nothing.
- [ ] Add a conformance test that imports each `fixtures/desktop/` file. Done when: all import and re-export deep equal.
- [ ] Commit: `cloud: annotation JSON and XFDF exchange endpoints`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/reviews/exchange` and `npm --prefix cloud run test:pkg -- annotations` pass; the entity-expansion fixture must fail the parse test if the parser's limit is removed.

## 11. Markup appearance streams for exported annotations

A PDF viewer only shows an annotation faithfully when it has an `/AP /N` appearance stream (`D03 T03 §2`). This section generates appearances in TypeScript for the markup subtypes reviewers use, using the bounded PDF writer from `D09 T04 §1`. It must not run or evaluate any PDF content, and subtypes it cannot draw are written without an appearance and listed in the export report rather than dropped.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add `cloud/packages/pdf-lite/src/appearance/markup.ts` generating Form XObject streams for `Highlight` (multiply blend), `Underline`, `StrikeOut`, `Squiggly`, `Text` (note icon), `FreeText`, `Square`, `Circle`, `Line`, `Ink`. Done when: each subtype produces a stream that parses with the independent `pdf-lite` reader.
2. Match the desktop geometry rules of `D03 T03 §2` (rounded highlight quad fill, sawtooth squiggle, arrowhead rotation). Done when: golden raster comparisons against desktop renders of the same input have `SSIM >= 0.98`.
3. Return an `ExportReport` listing subtypes written without appearance. Done when: a `Stamp` and `FileAttachment` input appear in the report and are not silently dropped.

- [ ] Add golden fixtures under `cloud/packages/pdf-lite/fixtures/appearance/` with expected PNG renders produced by `recto-cli` (provenance recorded). Done when: `PROVENANCE.md` lists the command for every PNG.
- [ ] Add a render-compare test harness running `pdf.js` in Node for the actual output. Done when: the harness reports SSIM per subtype and fails below `0.98`. Cheaper substitute: asserting only that the stream is non-empty.
- [ ] Bound output size: at most 2 MiB of appearance data per page. Done when: an ink with 10000 points still stays below the cap or is simplified with a recorded note.
- [ ] Commit: `cloud: markup appearance stream generation`

**Test checkpoint:** `npm --prefix cloud run test:pkg -- pdf-lite` passes with SSIM at or above 0.98 for each subtype; swapping the highlight blend mode to `Normal` makes the highlight golden fail.

## 12. Reviewed PDF export job with comments

The owner wants a PDF carrying the review's comments. This section exports the pinned document version with the current comments as real annotations, as a Queue job that writes a temporary file the owner downloads. It must never modify the stored version (documents are immutable), must work on a copy, and must keep the original bytes intact through an incremental update.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add `POST /v1/reviews/:id/export` returning `202` with an export id, and `GET /v1/reviews/:id/exports/:exportId` returning status and a short-lived download link when ready. Done when: the route tests cover queued, running, ready and failed.
2. Add the Queue consumer `cloud/apps/api/src/reviews/export-job.ts` that loads the pinned version from storage, appends the annotations and appearances as a streamed incremental update with `writeUpdate` of `D09 T04 §1` (reading the pinned version through R2 range reads, peak heap under 64 MiB), writes the result to `exports/<id>.pdf` in R2 with a 24-hour expiry, and records the `ExportReport`. Done when: the output opens in `pdf.js` and in `recto-cli annot list` showing every comment.
3. Add a flatten option `mode=flatten` that merges appearances into page content instead of keeping annotations. Done when: the flattened PDF has zero markup annotations and the same rendering.

- [ ] Refuse encrypted, malformed or oversized inputs with a typed `export_unsupported` failure and a human message, never a partial file. Done when: an encrypted fixture ends in `failed` with that code and no object in R2.
- [ ] Preserve existing signatures: when the version carries a signature, export uses an incremental update only and reports that further changes will mark the signature as modified after signing. Done when: a signed fixture keeps its first revision byte identical.
- [ ] Add a Cron sweep deleting expired exports. Done when: a test with a clock advance removes the object.
- [ ] Add audit event `review.exported` with identifiers only. Done when: the row exists after an export.
- [ ] Commit: `cloud: reviewed PDF export job`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/reviews/export` passes; the original-bytes prefix test fails if the writer rewrites the file instead of appending.

## 13. Web review room: viewer overlay, comment panel, presence

The browser is where most reviewers work. This section builds the review room page: the document in the web viewer with a markup layer, a Comments panel with threads and filters, presence avatars, and an honest connection status. It follows `design/SPEC.md` brand, colour and type, with the Comments panel layout of section 8.3. It must keep scripting off in the viewer and must not show a stale state as current.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` sections 3, 4, 5 and 7 (brand, colour, type, shape) and the Comments panel structure of section 8.3 with `design/screenshots/D-Focus-Light.png`.
**Job:** A reviewer opens the invitation link, sees the document, adds a highlight or note anywhere, replies to others, resolves threads, and sees who else is present, with a clear banner when the connection drops and when it recovers. Consumer: `D09 T02 §16` supplies the page canvas; this section supplies the layer and panel.
**Treatment:** A web component set under `cloud/apps/web/src/reviews/` using the CSS custom properties generated from `design/tokens/RectoColors.xaml` (light, dark and high contrast), the Segoe UI Variable stack with system fallbacks, and the anchor-to-page model of `§1`. Cheaper substitute that fails the checkpoint: a plain text comment list with no markup on the page, hex colours written in component CSS, or hiding a disconnected state.
**Chrome:** consume the token stylesheet (accent `#C8431F` light and `#F0603C` dark through variables, never literals), 8 px radius for cards, 12 px caption and 14 px body, and 36 px minimum touch targets; focus rings use the accent; icon-only buttons carry `aria-label` and a `title`.

**Build order:**

1. Add the markup layer `cloud/apps/web/src/reviews/MarkupLayer.ts` drawing annotations over the viewer canvas and capturing highlight, note, free text, rectangle and ink tools. Done when: a created highlight appears in a second browser within 1 second.
2. Add the Comments panel `CommentsPanel.ts` with threads, author, time, resolve toggle, reply box, `@mention` autocomplete restricted to invitees, filters (open, resolved, mine, by reviewer) and jump-to-page. Done when: keyboard-only use covers every action.
3. Add presence and connection status: avatars with names, a `Reconnecting` banner after 3 seconds offline, a `Back online` notice, and a read-only state when closed. Done when: an offline simulation shows the banner and queued comments flush on reconnect.

- [ ] Add `cloud/apps/web/src/reviews/ReviewRoomPage.ts` wiring the socket client from `§7`. Done when: the page loads a review and renders existing comments in under 2 seconds for 500 comments on the reference fixture.
- [ ] Add accessibility: the panel is a labelled region, new remote comments announce through a polite live region, colour is never the only signal, and `prefers-reduced-motion` removes animation. Done when: axe-core finds zero serious violations on the page.
- [ ] Add dark and high contrast themes. Done when: screenshot tests exist for light, dark and forced-colors.
- [ ] Add Content Security Policy compliance: no inline script, no `eval`. Done when: the page runs under the platform CSP with zero violations logged.
- [ ] Commit: `cloud-web: review room with markup layer, comments panel and presence`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/reviews-room.spec.ts` passes in Chromium; the spec fails if the Comments panel is replaced by a list without resolve and reply controls.

## 14. Start-a-review dialog and reviews list in the web app

An owner needs to start and manage reviews without a developer tool. This section adds the start dialog on a document (invitees, role per invitee, deadline, deadline action), the reviews list with status, deadline and unresolved counts, and the owner's close and export actions. It follows the same brand and tokens and must make clear who will receive what before anything is sent.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` sections 3, 4, 5, 7 and the dialog pattern of 9.9 with `design/screenshots/D-Dialogs.png`.
**Job:** An owner opens a document, chooses `Start a review`, adds people by email or from account contacts, sets a deadline, sees exactly who is invited and what they can do, starts the review, and later sees all reviews with open comment counts and closes or exports one. Consumer: `D09 T02 §13` hosts the page; `§4` is the API.
**Treatment:** A modal dialog and a table page in `cloud/apps/web/src/reviews/` with inline validation, a confirmation summary that lists every recipient, and an empty state. Cheaper substitute that fails the checkpoint: a single email text box that sends immediately with no summary, or a list without deadline and counts.
**Chrome:** consume the token stylesheet and the shell's dialog and table components; buttons use accent only for the single primary action; every icon-only button has `aria-label` and `title`.

**Build order:**

1. Add `StartReviewDialog.ts` with invitee chips, role select (reviewer, viewer), deadline picker with timezone shown, deadline action select, and the confirmation summary. Done when: invalid emails and duplicate invitees show inline errors and the submit stays disabled.
2. Add `ReviewsListPage.ts` with filters (open, closed, overdue), sorting and per-review actions (open, copy link for account holders, close, export, delete). Done when: the list shows live unresolved counts from the API.
3. Add owner summary panel with per-reviewer comment counts. Done when: counts match the database in the integration test.

- [ ] Add form validation shared with the API schema through the common validators. Done when: the UI and API reject the same 20 malformed inputs.
- [ ] Add empty, loading and error states with retry. Done when: each state has a screenshot test.
- [ ] Add keyboard and screen reader coverage for the dialog (focus trap, escape closes, labelled controls). Done when: axe-core finds zero serious violations.
- [ ] Commit: `cloud-web: start-a-review dialog and reviews list`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/reviews-start.spec.ts` passes; the spec fails if the confirmation summary is removed before sending.

## 15. End-to-end review tests in two browsers

The whole feature must work with two people at once. This section adds Playwright tests that run an owner and an invited guest in separate browser contexts through start, comment, reply, resolve, disconnect, reconnect, close and export, and wires them into CI. It must run against the local `workerd` stack and no external service.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add `cloud/apps/web/e2e/reviews.spec.ts` using two `BrowserContext`s, a fake mail transport that exposes the guest link, and a fixture PDF. Done when: the happy path passes locally.
2. Add the disruption case: toggle `context.setOffline(true)` for the guest, add three comments offline, reconnect, and assert the owner sees exactly three comments once. Done when: the assertion holds across 10 repeated runs.
3. Add the concurrent case: both users edit and resolve the same thread in the same moment. Done when: both screens converge to identical state.
4. Add the export case: owner exports the PDF and the test validates annotations with `pdf.js`. Done when: the comment count in the PDF matches the room.

- [ ] Wire the spec into the CI workflow from `D09 T01 §20` on every pull request with traces uploaded on failure. Done when: the workflow file lists the job and a deliberately failing assertion turns CI red.
- [ ] Add a revoked-guest case where the owner removes the guest and the guest's page shows a clear message within 5 seconds. Done when: the assertion passes.
- [ ] Add a performance guard: 300 comments from 5 simulated users sync to a new join in under 3 seconds on the CI runner. Done when: the timing assertion has a recorded threshold.
- [ ] Commit: `cloud-web: end-to-end shared review tests`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/reviews.spec.ts --repeat-each=3` passes all repeats; removing the reconnect outbox flush makes the offline case fail.

## Verification

- Run the two-browser end-to-end suite and the convergence suite and keep the output as evidence: an owner starts a review, a guest joins by email link, both comment, reply and resolve, a dropped connection resumes without loss, and the exported PDF shows every comment in `pdf.js` and in `recto-cli annot list`.
- Import the desktop fixtures into a room and export them again; the round trip is deep equal, and a cloud export imports into the desktop with `recto-cli annot import`.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
