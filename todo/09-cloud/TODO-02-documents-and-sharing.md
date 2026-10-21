---
schema_version: 1
id: cloud-documents
domain: 09-cloud
status: active
title: "TODO-02 -- Recto Cloud Documents, Sharing and Web Viewer"
---

# TODO-02 -- Recto Cloud Documents, Sharing and Web Viewer

> **Goal:** A signed-in person can upload PDFs of up to the configured size cap to Recto Cloud, organise them in folders, rename, move, trash and restore them, stay inside a quota, and share them with view, comment or edit roles to accounts or to expiring, optionally password-protected links, with every grant and access recorded. People and link holders read documents in a web app whose pdf.js viewer runs with scripting and `eval` disabled (D9), with page navigation, search, zoom, thumbnails and download, and the whole web app meets WCAG 2.2 AA with keyboard and screen reader support.

> **Current state:** Measured 2026-10-21. `cloud/` does not exist yet; `D09 T01` builds the workspace, the `SqlDatabase` and `BlobStore` interfaces, sessions, principals, rate limits, the job queue, security headers and the admin takedown table this file consumes. `design/SPEC.md` sections 3 to 7, 9.2, 9.9, 10 and 11 and `design/tokens/RectoColors.xaml` define the brand, colour, type, dialog copy and keyboard the web app follows; `resources/svg/` holds the lockup, wordmark and `drop-files-{light,dark}.svg`. The web app has no screenshots of its own: every web surface here is a "Genuinely new surface" that translates the SPEC. Quotas, retention defaults and the terms text are open owner decisions (design spec), and only section 21 waits on them.

## Inputs

- `docs/specs/2026-10-21-recto-cloud-design.md`: documents are immutable versions in R2 keyed by content hash with metadata in D1, uploads are size-capped (default 200 MB) and type-checked by magic bytes and never parsed by the Worker, sharing grants `view`, `comment` or `edit` to accounts or links with expiry and password, and every grant and access is an audit event.
- `design/SPEC.md` sections 3 (brand), 4 (colour), 5 (typography), 6 (icons), 7 (shape, spacing, elevation), 9.2 (loading: outlines at once, never a window spinner), 9.9 (dialog copy), 10 (keyboard), 11 (accessibility) and `design/tokens/RectoColors.xaml`.
- W3C WCAG 2.2 (success criteria 1.4.3, 1.4.10, 1.4.11, 1.4.12, 2.1.1, 2.4.1, 2.4.7, 2.4.11, 2.5.7, 2.5.8, 3.3.7, 3.3.8, 4.1.2, 4.1.3) and the `pdfjs-dist` documentation for `getDocument` and `PDFViewer` options.
- D9 (zero PDF JavaScript) and the AGENTS.md rule that links in documents ask before opening, applied to the web viewer.
- -> XREF: D09 T05 §8 -- Save to Recto Cloud passes `baseVersion` so concurrent commits refuse with 409.
- -> XREF: D09 T05 §7 -- Open from Recto Cloud verifies downloads against the version SHA-256.
- -> XREF: D09 T03 §4 -- reviews authorize through `resolveAccess` and the `comment` capability.
- -> XREF: D09 T04 §16 -- sealed results are stored as new versions through `commitVersion`.
- -> XREF: D09 T04 §19 -- envelope retention, quota and expiry defaults join the same owner batch.

## Outcome

- `POST /api/uploads` then part uploads then completion stores a PDF as an immutable version at `blobs/<sha256>`; a file over the cap, a non-PDF renamed to `.pdf`, or a mismatched hash is refused with a clear code and leaves no stored object.
- Folders, rename, move, trash, restore and purge work through the API and the library page; a trashed folder restores with its contents; quota use is shown and enforced.
- A document can be shared with a person by email or with a link that expires and asks for a password; an owner sees who was granted what and who opened it; revoking takes effect on the next request.
- A signed-in user and a link holder open a document in the web viewer; a PDF with a JavaScript open action runs nothing, produces no dialog and no CSP violation, and causes no request to another origin.
- An automated WCAG 2.2 AA check passes on every screen, and the keyboard-only path through sign-in, upload, share and read is covered by Playwright.

**Adjacency:** list=applicable @ §14; document=applicable @ §11; settings=applicable @ §7; reporting=applicable @ §10; notifications=applicable @ §8; permissions=applicable @ §2; audit=applicable @ §10; exchange=applicable @ §3; reverse=applicable @ §6

**Adjacency rationale:** The library (§14) and shared-with-me (§15) pages are the list surface, backed by the paged list API (§11). What a person carries out is the original bytes of a version, downloaded through §11 and exported in bulk by D09 T01 §15 through the exporter registered in §11. The tunables (upload cap, quota, trash retention, link maximum lifetime) have named consumers in §3, §7, §6 and §9 and their values are recorded in §21. Reporting is the owner's audit view and quota usage summary (§10, §7). Notifications are the share-invitation email (§8). Permissions are the single access resolver of §2 that every route calls. Audit is §10. Exchange is the upload protocol (§3) the Windows app uses in D09 T05, with the OAuth scope `documents`. Reverse is trash and restore (§6), link and grant revocation (§8, §9), and the immutable version history, which means an edit never overwrites earlier bytes.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Document, folder, version and sharing schema with repositories | D09 T01 §3 | [ ] |
| 2 | §2 | Access resolver: roles, inheritance and one decision point | §1, D09 T01 §7 | [ ] |
| 3 | §3 | Upload sessions: size cap, part uploads and magic-byte checks | §2, D09 T01 §4, D09 T01 §8 | [ ] |
| 4 | §4 | Version commit: hash check and immutable content-addressed blobs | §3, D09 T01 §16, D09 T01 §18 | [ ] |
| 5 | §5 | Folders: create, rename, move and list | §2 | [ ] |
| 6 | §6 | Trash, restore and purge with blob garbage collection | §4, §5, D09 T01 §15 | [ ] |
| 7 | §7 | Quotas and usage accounting | §4 | [ ] |
| 8 | §8 | Account grants: invite, change role and revoke | §2, D09 T01 §9, D09 T01 §10 | [ ] |
| 9 | §9 | Share links: expiry, password, redeem and revoke | §2, D09 T01 §8 | [ ] |
| 10 | §10 | Audit of grants and access with an owner-visible log | §8, §9 | [ ] |
| 11 | §11 | Read API: metadata, ranged content and download | §4, §10 | [ ] |
| 12 | §12 | Design tokens from RectoColors.xaml and the web app scaffold | D09 T01 §1 | [ ] |
| 13 | §13 | Web shell with sign-in, account and consent screens | §12, D09 T01 §10, D09 T01 §11 | [ ] |
| 14 | §14 | Library page: folders, upload, rename, move, trash and quota | §5, §6, §7, §13 | [ ] |
| 15 | §15 | Sharing dialog and shared-with-me list | §8, §9, §14 | [ ] |
| 16 | §16 | Document page and scriptless pdf.js viewer | §11, §13 | [ ] |
| 17 | §17 | Viewer navigation: page controls, zoom and thumbnails | §16 | [ ] |
| 18 | §18 | Viewer search and download | §17 | [ ] |
| 19 | §19 | Accessibility: WCAG 2.2 AA checks, keyboard paths and statement | §15, §18 | [ ] |
| 20 | §20 | Playwright end-to-end suite for sign-in, upload, share and read | §15, §18 | [ ] |
| 21 | §21 | Quota, retention and terms values recorded from the owner | §7 | [ ] |

## 1. Document, folder, version and sharing schema with repositories

Everything in this file stores to a small set of tables, so the shapes are fixed once. Versions are immutable by construction, not by convention, because shared review and signing both rely on a version never changing under them. The Worker never parses the PDF, so nothing here reads PDF structure.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Write `cloud/packages/db/migrations/0010_documents.sql`: `folders` (id, owner_id, parent_id, name, name_key, trashed_at, trash_root_id, created_at), `documents` (id, owner_id, folder_id, name, name_key, current_version_id, trashed_at, trash_root_id, created_at), `versions` (id, document_id, number, sha256, size, page_count, created_by, created_at, source `web|desktop|signing|api`), `blobs` (sha256 primary key, size, created_at), `uploads` (id, user_id, document_id nullable, folder_id nullable, name, size, part_size, state `open|verifying|committed|failed|aborted`, error_code, created_at, expires_at), `upload_parts` (upload_id, number, etag, size), `grants` (id, resource_type `document|folder`, resource_id, grantee_user_id nullable, invitee_email nullable, role `view|comment|edit`, created_by, created_at, revoked_at), `share_links` (id, resource_type, resource_id, token_hash unique, role, password_hash nullable, password_iterations, expires_at, allow_download, created_by, created_at, revoked_at), `access_events` (id, at, resource_type, resource_id, version_id nullable, actor_kind `user|link|system`, actor_id, link_id nullable, event, detail JSON). Unique indexes on `(owner_id, folder_id, name_key)` for live rows. Done when: the migration applies on D1 and on the Durable Object SQLite adapter and `PRAGMA foreign_key_check` is empty.
2. Add triggers that abort an `UPDATE` of `versions.sha256`, `versions.size`, `versions.number` or `versions.document_id`, and any `DELETE FROM versions` outside the purge path (guarded by a one-row `purge_mode` flag the purge job sets inside its own batch). Done when: a test that updates a version hash fails with an abort and the purge test still deletes.
3. Implement repositories in `cloud/apps/api/src/documents/repo.ts` (`createDocument`, `addVersion`, `getDocument`, `listFolder`, `moveDocument`, `setTrashed`) over `SqlDatabase`, each multi-row change in one `batch`. Done when: the repository tests pass on both database adapters.
4. Name rules in `cloud/packages/shared/src/names.ts`: NFC-normalise, trim, 1 to 255 characters, reject `/`, `\`, control characters and names ending in a dot or space, `name_key` is the case-folded form. Done when: unit tests cover Unicode composition, reserved Windows names kept legal (it is a cloud name, not a file path) and an emoji name.

- [ ] Create `cloud/packages/db/migrations/0010_documents.sql`. Done when: `cloud/apps/api/test/documents/schema.test.ts` finds every table, index and trigger in `sqlite_master`.
- [ ] Add the immutability triggers. Done when: the update-hash test fails with `ABORT`. Cheaper substitute: a repository method that merely omits an update function.
- [ ] Create `cloud/apps/api/src/documents/repo.ts`. Done when: `test/documents/repo.test.ts` passes on D1 and on the Durable Object adapter.
- [ ] Create `cloud/packages/shared/src/names.ts`. Done when: `npm --prefix cloud run test:pkg -- names` passes.
- [ ] Run `node cloud/scripts/lint-migrations.mjs`. Done when: it exits 0 with the new migration present.
- [ ] Commit: `cloud/api: document, folder, version and sharing schema with immutable versions`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/documents/schema.test.ts test/documents/repo.test.ts` exits 0; dropping the immutability trigger makes the update-hash test fail.

## 2. Access resolver: roles, inheritance and one decision point

Every document route asks one function the same question, so a permission mistake can exist in one place only. Roles are ordered `none < view < comment < edit < owner`. A document's effective role is the highest of: ownership, a direct account grant, a grant on any ancestor folder, and the redeemed share link. A trashed resource, a revoked or expired grant, a suspended owner and a blocked content hash each deny, with distinct reasons.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Extend `Principal` in `cloud/apps/api/src/auth/principal.ts` with `Link{linkId, role, resourceType, resourceId}` resolved from the `__Host-recto_link` cookie (set by section 9). Done when: handlers receive it and CSRF applies to it exactly as to a cookie session.
2. Implement `resolveAccess(principal, resource)` in `cloud/apps/api/src/access/resolve.ts` returning `{role, via: 'owner'|'grant'|'folder-grant'|'link', deniedReason?}`. Done when: a table-driven test in `test/access/resolve.test.ts` covers owner, direct grant, ancestor-folder grant at three depths, link role, the max of several sources, trashed, revoked, expired, suspended owner, blocked hash and an anonymous principal with no link.
3. Define the capability table in `cloud/apps/api/src/access/capabilities.ts`: `view` reads and downloads; `comment` adds viewing and creating or editing the principal's own comments (used by D09 T03); `edit` adds uploading new versions and renaming the document; `owner` adds moving, trashing, sharing, deleting and viewing the audit log. Link holders never exceed `edit` and never share. Done when: `can(role, 'upload-version')` is true for `edit` and false for `comment`, tested for every capability and role.
4. Add `requireAccess(capability)` route middleware that returns 404 (not 403) for resources the principal cannot see at all, so existence does not leak, and 403 for visible resources lacking the capability. Done when: the leak test compares the 404 body for a missing id and for another user's id and finds them identical.

- [ ] Extend `Principal` with `Link`. Done when: `test/auth/principal.test.ts` covers the new variant.
- [ ] Create `cloud/apps/api/src/access/resolve.ts`. Done when: the table-driven test passes every row.
- [ ] Create `cloud/apps/api/src/access/capabilities.ts`. Done when: the capability matrix test passes.
- [ ] Create `requireAccess` and apply it as the only way a document route reads a resource. Done when: `cloud/scripts/check-routes-use-resolver.mjs` fails for a route under `/api/documents` or `/api/folders` that does not call it, and passes for the current routes.
- [ ] Commit: `cloud/api: one access resolver with roles, folder inheritance and link principals`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/access` exits 0; changing the maximum to the minimum of sources makes the "max of several sources" row fail.

## 3. Upload sessions: size cap, part uploads and magic-byte checks

Files over the Worker request body limit cannot arrive in one request, and the Worker must never hold a file in memory, so an upload is a session of numbered parts streamed into the `BlobStore` multipart interface under a staging key. Hostile input is refused early: the size cap before the first byte, the `%PDF-` header on the first part, and nothing is parsed beyond the header.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `POST /api/uploads` with `{name, size, folderId?, documentId?, sha256?}`: requires `edit` on the target document or folder (owner for a new top-level document), enforces `size` between 1 byte and `MAX_UPLOAD_BYTES` (default 200 MiB, 413 `too_large`) and the quota check of section 7, rate-limited by a new `upload-start` policy (60 per hour per user, added to `cloud/apps/api/src/ratelimit/policy.ts`), creates the multipart upload at `staging/<uploadId>` and returns `{uploadId, partSize: 8 MiB, partCount}`. Done when: tests cover the cap boundary (cap accepted, cap plus one refused), zero size, a folder the caller cannot edit and the rate limit.
2. `PUT /api/uploads/:id/parts/:n` streams the body into the multipart part with the exact expected length (`Content-Length` required and checked, last part may be smaller), rejects out-of-range numbers and a body that exceeds its declared length, and returns the part etag. On part 1 the first 1024 bytes are inspected before further bytes are accepted: the file must contain `%PDF-` followed by a version `1.0` to `2.0` in that window, otherwise the session fails with `unsupported_type` and the staged object is aborted. Done when: a ZIP, a PNG, an HTML file and an empty file renamed to `.pdf` are each refused at part 1, and a PDF with 200 bytes of leading junk is accepted.
3. `GET /api/uploads/:id` lists received parts and the session state (so a browser resumes after a reload), `DELETE /api/uploads/:id` aborts and removes the staged object, and the Cron handler aborts sessions open for more than 24 hours. Done when: the resume test uploads parts 1 and 3, reloads the client state, uploads part 2 and completes; the expiry test removes an abandoned staged object.
4. The allowed types live in `cloud/apps/api/src/documents/types.ts` as a table (`application/pdf` with its signature detector) so new types are a table row, not a code path. Done when: the detector is unit tested and a type not in the table cannot be uploaded.

- [ ] Create `cloud/apps/api/src/uploads/create.ts` and the `upload-start` policy. Done when: `test/uploads/create.test.ts` passes the boundary and permission cases.
- [ ] Create `cloud/apps/api/src/uploads/part.ts`. Done when: the magic-byte refusals and the exact-length check pass in `test/uploads/part.test.ts`. Cheaper substitute: trusting the file extension or the `Content-Type` header.
- [ ] Create `cloud/apps/api/src/uploads/session.ts` (status, resume, abort, expiry). Done when: the resume and expiry tests pass.
- [ ] Create `cloud/apps/api/src/documents/types.ts`. Done when: the detector tests pass for the signature window edges (offset 0, offset 1023, offset 1024).
- [ ] Confirm no hostile-input parse beyond the header with `cloud/scripts/check-no-pdf-parse.mjs`, which fails when `apps/api/src` imports a PDF parser package. Done when: it exits 0, and exits 1 when a fixture import of `pdfjs-dist` is added under `apps/api/src/uploads`.
- [ ] Accept an optional `baseVersion` when creating an upload session for an existing document, and at commit (§4) refuse with `409 version_conflict` (carrying the current version, who saved it and when) when the document's current version is no longer `baseVersion`, so two writers can never silently replace each other's version; the desktop's `Save to Recto Cloud` passes it (`D09 T05 §8`). Done when: `uploads.baseVersionConflict` commits with a matching `baseVersion`, returns 409 with the current version after another commit, and commits unconditionally when `baseVersion` is omitted.
- [ ] Commit: `cloud/api: upload sessions with a size cap, streamed parts and a PDF header check`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/uploads` exits 0; raising the cap check to allow `cap + 1` makes the boundary test fail.

## 4. Version commit: hash check and immutable content-addressed blobs

Completion is where an upload becomes a version, so it is where the content hash is established. The server hashes the staged bytes itself and never trusts a client claim, then stores the blob under its hash. Large files take time to hash, so completion answers `202` and a queue job finishes the work; the client polls the session. The same bytes uploaded twice produce one blob and the second upload reuses it.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `POST /api/uploads/:id/complete` with `{parts:[{n, etag}]}` validates the part list against recorded parts, completes the multipart upload at `staging/<id>`, sets state `verifying`, enqueues a `commit-upload` job on the job queue (D09 T01 §18) and returns `202`. Done when: a missing or out-of-order part returns 400 `parts_mismatch` and nothing is enqueued.
2. The `commit-upload` handler streams the staged object through `crypto.DigestStream('SHA-256')`, re-checks the `%PDF-` window, compares to the client's `sha256` when supplied (`checksum_mismatch`), looks up `blocked_hashes` through `isBlocked` (D09 T01 §16, state `failed` with code `blocked_content`), re-checks the quota (section 7) with the true size, copies to `blobs/<sha256>` when the blob row is absent, deletes the staged object, and writes the `blobs`, `versions` and `documents` rows in one `batch` (a new document when `documentId` is empty, otherwise the next version number). If the hash equals the current version's hash it returns that version and creates none. Done when: tests cover each failure state, leave no `blobs/*` object and no rows behind, and cover the idempotent same-hash case.
3. A retry-safe commit: the job may run twice; the second run finds state `committed` and does nothing. A crash between the copy and the batch leaves an unreferenced blob that the sweeper (section 6) removes. Done when: the crash-injection test (throw after copy) retries to success without duplicate rows.
4. `GET /api/uploads/:id` reports `verifying`, `committed` (with document and version ids), or `failed` (with the code), and the Windows app and the web app both poll it. Done when: the polling test reaches `committed` after the job runs.

- [ ] Create `cloud/apps/api/src/uploads/complete.ts`. Done when: `test/uploads/complete.test.ts` passes the part-list validation.
- [ ] Create `cloud/apps/api/src/uploads/commit-job.ts` and register it in the job dispatcher. Done when: `test/uploads/commit-job.test.ts` passes every failure state and the same-hash case.
- [ ] Add the retry-safety and crash-injection test. Done when: it passes without duplicates. Cheaper substitute: relying on the queue never delivering twice.
- [ ] Verify two accounts uploading identical bytes share one `blobs/<sha256>` object and own separate `versions` rows. Done when: `list('blobs/')` shows one object for two uploads.
- [ ] Commit: `cloud/api: hash-verified immutable versions stored by content hash`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/uploads/commit-job.test.ts test/uploads/complete.test.ts` exits 0; trusting the client `sha256` instead of the computed one makes the mismatch test fail.

## 5. Folders: create, rename, move and list

Folders are plain containers owned by one account. Names are unique among live siblings case-insensitively, trees are bounded, and a move can never create a cycle or reach into someone else's tree. Moving and renaming are owner actions; people with `edit` can rename the document they edit but cannot rearrange the owner's library.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `POST /api/folders`, `PATCH /api/folders/:id` (rename), `POST /api/folders/:id/move`, `PATCH /api/documents/:id` (rename), `POST /api/documents/:id/move` using the repositories and name rules of section 1; a collision on create or move returns 409 `name_taken`, an upload collision auto-suffixes ` (2)`. Done when: tests cover collisions that differ only by case, rename to the same name (no-op), and the suffix on upload.
2. Bounds: depth at most 20, at most 5000 live folders per account, at most 1000 entries returned per page. Done when: the 21st nested folder returns 400 `too_deep`.
3. Cycle and ownership checks on move: a folder cannot move into itself or a descendant, and the destination must belong to the same owner. Done when: both attempts return 400 or 404 and change nothing, verified by reading the table.
4. `GET /api/folders/:id/children?cursor&sort=name|modified|size&dir=` lists folders then documents with the current version's size and modified time, cursor-paged, and `GET /api/folders/root/children` for the top level. Done when: a 1500-entry folder pages stably across a rename during paging.

- [ ] Create `cloud/apps/api/src/folders/routes.ts`. Done when: `test/folders/routes.test.ts` passes create, rename, move and collision cases.
- [ ] Add depth and count bounds. Done when: the bound tests pass.
- [ ] Add cycle and ownership checks. Done when: the cycle test leaves the tree unchanged. Cheaper substitute: checking only that the destination exists.
- [ ] Create the children listing with cursor paging. Done when: the stable-paging test passes.
- [ ] Commit: `cloud/api: folders with unique names, bounded depth, safe moves and paged listing`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/folders` exits 0; removing the descendant check makes the cycle test fail.

## 6. Trash, restore and purge with blob garbage collection

Trash is the undo for deletion. Trashing a folder trashes its whole subtree under one `trash_root_id`, so restoring the folder restores exactly what it took with it. Purging removes rows first and storage second, and a blob is deleted only when no version references it.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `POST /api/documents/:id/trash`, `POST /api/folders/:id/trash`, `POST /api/trash/:id/restore`, `DELETE /api/trash/:id` (delete forever), `GET /api/trash`: owner only. Trashing sets `trashed_at` and `trash_root_id` on the whole subtree in one `batch`. Restore puts the root back in its original parent, or at the top level when that parent is gone or trashed, renaming on a collision (` (restored)`). Done when: a folder with three nested documents round-trips through trash and restore unchanged.
2. While trashed, every non-owner request and every link to the resource is denied by the resolver (section 2) and grants and links stay in place, so restore reactivates them. Done when: a link redeemed before trashing returns the `trashed` denial, and works again after restore.
3. `TRASH_RETENTION_DAYS` (default 30, set in `config.ts`) and a Cron purge deleting trashed roots older than the retention, skipping any resource with a `RetentionHold` (D09 T01 §15), by deleting `versions` rows (under `purge_mode`), then documents and folders, then blob rows and storage objects with no remaining reference. Done when: the purge test removes the right rows and objects and keeps a held fixture.
4. The orphan sweeper deletes `blobs` rows and `blobs/*` objects with no referencing version older than 24 hours, and `staging/*` objects without an open upload. Done when: a crash-orphaned blob is removed and a blob referenced by another account's version is kept.

- [ ] Create `cloud/apps/api/src/trash/routes.ts`. Done when: `test/trash/routes.test.ts` passes the subtree round trip and the collision rename.
- [ ] Add the resolver denial and link reactivation test. Done when: it passes both directions.
- [ ] Create `cloud/apps/api/src/trash/purge.ts` and the Cron entry. Done when: `test/trash/purge.test.ts` passes retention, hold and shared-blob cases.
- [ ] Create `cloud/apps/api/src/trash/sweeper.ts`. Done when: the orphan tests pass. Cheaper substitute: deleting the blob whenever one version is deleted, which breaks other accounts' copies.
- [ ] Commit: `cloud/api: trash with subtree restore, retention purge and blob garbage collection`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/trash` exits 0; deleting a blob while a second version still references it makes the shared-blob test fail.

## 7. Quotas and usage accounting

A free service needs a fence. Usage is the sum of distinct blob sizes referenced by an owner's versions, counted once per owner, so re-uploading the same bytes costs nothing and versions by other people on a shared document count against the document's owner. The limit is configuration; the number itself is an owner decision recorded in section 21.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `quota_usage` table in `cloud/packages/db/migrations/0011_quota.sql` (user_id, bytes, updated_at) maintained inside the same `batch` as version insert and purge, plus `cloud/apps/api/src/quota/usage.ts` with `recompute(userId)` that derives the true value from `versions` and `blobs` for repair. Done when: a property test performing random upload, trash and purge sequences ends with `quota_usage.bytes` equal to `recompute`.
2. `DEFAULT_QUOTA_BYTES` (default 2 GiB, config) with a per-user override column `quota_override_bytes` on `users` set by the admin script. Enforcement: `POST /api/uploads` refuses a declared size that cannot fit (413 `quota_exceeded` with `{used, limit}`), and the commit job re-checks with the true size. Done when: tests cover exactly-at-limit success and one-byte-over failure, and a second upload of identical bytes succeeds at the limit.
3. `GET /api/account/usage` returns `{used, limit, trashBytes}` so the library can show it; trashed bytes count until purge. Done when: the response equals the recomputed values.
4. Add `usage-over-80` and `usage-full` emails (`kind` additions in the T01 email types) sent once per threshold crossing. Done when: the outbox shows one message per crossing and none on a re-upload that does not cross.

- [ ] Create `cloud/packages/db/migrations/0011_quota.sql` and `cloud/apps/api/src/quota/usage.ts`. Done when: the property test passes.
- [ ] Add enforcement to upload creation and the commit job. Done when: the boundary tests pass.
- [ ] Create `GET /api/account/usage`. Done when: its test passes.
- [ ] Add the threshold emails. Done when: the once-per-crossing test passes. Cheaper substitute: a counter in memory that resets on every deploy.
- [ ] Commit: `cloud/api: per-owner quota accounting with enforcement and threshold notices`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/quota` exits 0; counting identical blobs twice makes the property test fail.

## 8. Account grants: invite, change role and revoke

Sharing with a person means a role on a document or a folder. If the person already has a verified account the grant attaches to it; otherwise it waits as an invitation for that email address and attaches when that address is verified at sign-in. Guests who want no account use links (section 9). Changing or revoking a grant is effective on the next request.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `POST /api/shares/grants` with `{resourceType, resourceId, email, role, message?}` (owner only): creates or updates the grant (one live grant per resource and person), sends a `share-invite` email carrying a link to the document page (never the document), and returns the grant. Roles `view`, `comment` and `edit`. Done when: tests cover a new grant, a role change, an invitation to an unknown address, and a grant to the owner's own address (400).
2. `PATCH /api/shares/grants/:id` (role) and `DELETE /api/shares/grants/:id` (sets `revoked_at`). Done when: the next request of the grantee after revoke is a 404 from the resolver.
3. Claiming invitations: when a user's email is first verified or changed (the hook in D09 T01 §10 and §12), pending grants for that address attach by setting `grantee_user_id` and clearing `invitee_email`. Done when: signing up with the invited address shows the shared document in the shared-with-me list; a different address does not.
4. `GET /api/shares?resourceType&resourceId` (owner) lists grants and links for a resource; `GET /api/shared-with-me` lists resources shared to the caller with role and owner display name. Done when: both are paged and the second never lists revoked or expired grants.
5. Invitation limits: at most 50 grants per resource, 100 invitation emails per day per account, and `share-invite` rate-limited per owner. Done when: the 51st grant returns 400 `too_many_grants`.

- [ ] Create `cloud/apps/api/src/shares/grants.ts` and routes. Done when: `test/shares/grants.test.ts` passes create, change and revoke.
- [ ] Add the claim hook and its test. Done when: the invited sign-up test passes and the wrong-address test finds nothing.
- [ ] Create the list endpoints. Done when: `test/shares/list.test.ts` passes.
- [ ] Add the `share-invite` template and limits. Done when: the outbox contains only a link and the daily limit test passes. Cheaper substitute: attaching the PDF to the email.
- [ ] Commit: `cloud/api: account grants with invitations, role changes and revocation`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/shares` exits 0; skipping the revoked check in the shared-with-me query makes the list test fail.

## 9. Share links: expiry, password, redeem and revoke

A link is a capability: anyone holding it has the link's role. Links therefore expire, can require a password, can be revoked, and are never recoverable from the database (only a hash is stored). The token lives in the URL fragment, so it is never sent to a server, a log or a `Referer`; the page posts it to redeem.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `POST /api/shares/links` (owner) with `{resourceType, resourceId, role, expiresAt?, password?, allowDownload}` returns the URL `PUBLIC_ORIGIN/s#<token>` once (256-bit token, SHA-256 stored); `expiresAt` must be in the future and no later than `LINK_MAX_DAYS` (default 365); `allowDownload` applies to `view` links. Password: stored with the strongest Web Crypto key-derivation setting the runtime allows (PBKDF2-SHA-256 with the iteration count recorded per hash and a per-link salt), minimum length 8. Done when: tests cover creation, expiry bounds and that no table holds the token or the password.
2. `POST /api/links/redeem` with `{token, password?}`: anonymous allowed; looks up the hash; checks revoked, expired, trashed resource, suspended owner, and the password with a constant-time compare; rate-limited by `token-redeem` per IP and `share-unlock` per link. On success it sets the `__Host-recto_link` cookie (HttpOnly, Secure, SameSite=Lax, a random link-session id bound to the link id, 8 hours) and returns `{resourceType, resourceId, role, allowDownload, name}`. A wrong password returns the same body as an unknown token (`invalid_link`) so existence is not revealed, and the password gate response (`password_required`) is returned only after the token is found, which leaks only that a valid link exists to someone who already holds it. Done when: the sixth wrong password in ten minutes returns 429 and an expired, revoked and trashed link each return their own denial after a correct token.
3. `DELETE /api/shares/links/:id` revokes and invalidates live link sessions; `PATCH /api/shares/links/:id` changes role, expiry or download (changing the password re-hashes it). Done when: after revoke the cookie from a prior redeem is rejected by the resolver on the next request.
4. A link on a folder grants its role on the folder's contents at redeem time and for later additions; it never reveals the parent or siblings. Done when: the folder-link test lists children but a request for the parent returns 404.
5. A link session can be reduced to one document view: opening `/s#<token>` for a folder link shows the folder listing, for a document link goes straight to the viewer. Done when: both redeem responses carry `resourceType` that the web app routes on.

- [ ] Create `cloud/apps/api/src/shares/links.ts`. Done when: `test/shares/links.test.ts` passes creation and the storage-inspection test.
- [ ] Create `cloud/apps/api/src/shares/redeem.ts`. Done when: `test/shares/redeem.test.ts` passes every denial, the lockout and the identical-body case.
- [ ] Add revoke and patch with live-session invalidation. Done when: the revoke test passes. Cheaper substitute: waiting for sessions to expire.
- [ ] Add the folder-link visibility test. Done when: parent access returns 404.
- [ ] Commit: `cloud/api: expiring password-protected share links redeemed from the URL fragment`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/shares/links.test.ts test/shares/redeem.test.ts` exits 0; storing the raw token makes the inspection test fail.

## 10. Audit of grants and access with an owner-visible log

The owner of a shared document must be able to answer "who has seen this and who changed who can". Grants and links write events when they change, and views and downloads write events when they happen. The log holds identifiers and event types, not content, and shows link holders as the link, not as a person.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `cloud/apps/api/src/audit/access-log.ts` with `record(db, event)` for `grant.created`, `grant.changed`, `grant.revoked`, `link.created`, `link.changed`, `link.revoked`, `link.redeemed`, `link.unlock_failed`, `document.viewed`, `document.downloaded`, `version.created`, `document.trashed`, `document.restored`. Each row has the actor kind and id, link id where relevant, a coarse user-agent class and never an IP or the token. Done when: a test asserts every event type writes the expected row and none contains a canary token, password or file name.
2. Dedupe views: one `document.viewed` per actor, version and hour. Done when: ten opens in an hour produce one row and an open in the next hour a second.
3. `GET /api/documents/:id/audit?cursor&type=` and `GET /api/folders/:id/audit` (owner only) and `GET /api/account/audit` (the caller's own actions), newest first. Done when: a non-owner gets 404 and the owner sees the rows for a seeded scenario in order.
4. Retention `AUDIT_RETENTION_DAYS` (default 400) purged by Cron, and rows for a purged document are removed with it unless a `RetentionHold` covers it. Done when: the purge test removes only expired rows.
5. Register an exporter for `access_events` with `registerExporter` (D09 T01 §15) so the data export includes them. Done when: the export test contains the owner's events.

- [ ] Create `cloud/apps/api/src/audit/access-log.ts` and call it from sections 4, 6, 8, 9 and 11. Done when: `test/audit/access-log.test.ts` covers every event type.
- [ ] Add view dedupe. Done when: the hourly test passes.
- [ ] Create the audit read routes. Done when: `test/audit/routes.test.ts` passes ownership and ordering.
- [ ] Add retention purge and the exporter. Done when: both tests pass. Cheaper substitute: logging to the Worker log stream only, which is not owner-visible.
- [ ] Commit: `cloud/api: access and grant audit log visible to the owner`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/audit` exits 0; recording the raw token in `link.redeemed` makes the canary test fail.

## 11. Read API: metadata, ranged content and download

Reading is where hostile files meet browsers, so content is only ever delivered as bytes to a script that asked for them, with headers that stop a browser from treating them as a page. The viewer fetches ranges; download is a separate, attachment response. Blocked content is refused with its own status.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `GET /api/documents/:id` returns name, folder, role for the caller, current version summary and `pageCount`; `GET /api/documents/:id/versions` lists versions; both through `requireAccess('view')`. `PATCH /api/documents/:id/versions/:n/page-count` lets the owner or an `edit` principal set the viewer-reported count once (integer 1 to 100000, display hint only). Done when: a second set returns 409 and a `comment` principal gets 403.
2. `GET /api/documents/:id/versions/:n/content` streams from the `BlobStore` with `Range` support (single range, `206`, `Content-Range`, `Accept-Ranges: bytes`, `416` on invalid ranges), `ETag` equal to the SHA-256, `Cache-Control: private, no-cache`, and the `download` header kind of D09 T01 §6 (`nosniff`, sandbox CSP). `?download=1` sets `Content-Disposition: attachment` with an RFC 6266 `filename*` that is sanitised (no path separators, no control characters). A link with `allowDownload=false` refuses `download=1` with 403 `download_disabled` (the viewer still receives bytes, which is stated in the UI as "download is hidden", never as "protected"). A hash found in `blocked_hashes` returns 451. Done when: tests cover full read, first byte, last byte, suffix range, past-the-end range, a conditional `If-None-Match` 304, a filename with `"`, `\r\n` and a path separator, the 403 and the 451.
3. Each content response records `document.viewed` or `document.downloaded` through section 10. Done when: the audit rows appear once per dedupe window.
4. Register a data exporter `documents` with `registerExporter` (D09 T01 §15) that writes the library as `documents.json` and every version's bytes. Done when: the export test of D09 T01 §15 finds a seeded document byte-identical.

- [ ] Create `cloud/apps/api/src/documents/read.ts` for metadata and versions. Done when: `test/documents/read.test.ts` passes.
- [ ] Create `cloud/apps/api/src/documents/content.ts` with ranges and headers. Done when: `test/documents/content.test.ts` passes every range and header case. Cheaper substitute: redirecting to a signed storage URL, which bypasses the resolver, the audit and takedown.
- [ ] Add the filename sanitiser to `cloud/packages/shared/src/names.ts`. Done when: its unit tests cover injection characters.
- [ ] Register the exporter. Done when: the export assertion passes.
- [ ] Commit: `cloud/api: authorised ranged content, download and metadata read API`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/documents/content.test.ts test/documents/read.test.ts` exits 0; dropping `nosniff` from the content response makes the header test fail.

## 12. Design tokens from RectoColors.xaml and the web app scaffold

The web app must look like Recto without a second palette. The colour source of truth is `design/tokens/RectoColors.xaml`, so the CSS is generated from it and a check fails when they drift. The scaffold fixes the web stack: Vite (MIT) with Preact and TypeScript (MIT), no CSS framework, inline Fluent System Icons (`@fluentui/svg-icons`, MIT, as the web stand-in for Segoe Fluent Icons, which are not redistributable), and the shipping SVG brand assets copied from `resources/svg/`.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `cloud/scripts/build-tokens.mjs` parses `design/tokens/RectoColors.xaml` (Light, Dark and HighContrast dictionaries; `#AARRGGBB` converted to `#RRGGBB` or `rgb(r g b / a)`) and writes `cloud/apps/web/src/styles/tokens.css`: `--recto-window`, `--recto-surface`, `--recto-raised`, `--recto-canvas`, `--recto-border`, `--recto-border-strong`, `--recto-text`, `--recto-text-secondary`, `--recto-accent`, `--recto-accent-hover`, `--recto-on-accent`, `--recto-accent-text`, `--recto-accent-tint`, `--recto-search-hit`, `--recto-search-hit-current`, `--recto-text-selection`, `--recto-success`, `--recto-warning`, `--recto-danger` for light on `:root`, for dark under `@media (prefers-color-scheme: dark)` guarded by `:root:not([data-theme="light"])` and again under `:root[data-theme="dark"]`, and the HighContrast dictionary mapped to CSS system colours under `@media (forced-colors: active)`. Done when: the file is generated and `npm --prefix cloud run tokens:check` fails when the XAML changes without regeneration.
2. Hand-written `cloud/apps/web/src/styles/scale.css` from SPEC sections 5 and 7: type scale 12, 14, 20, 28, 40 px with the SPEC weights, font stack `"Segoe UI Variable Text", "Segoe UI Variable", "Segoe UI", system-ui, sans-serif`, 4 px spacing grid, radii 4, 8, 12, 16, 20, shadow tokens for floating elements only, minimum interactive target 32 px, motion 167 ms disabled under `prefers-reduced-motion`. Done when: a test fails if any `*.css` or `*.tsx` file under `apps/web/src` contains a hex colour literal outside `tokens.css`.
3. `cloud/scripts/copy-brand.mjs` copies `resources/svg/lockup-{light,dark}.svg`, `wordmark-*.svg`, `app-icon-small.svg` (favicon) and `drop-files-{light,dark}.svg` into `cloud/apps/web/public/brand/`, and the icon wrapper `cloud/apps/web/src/ui/Icon.tsx` takes a glyph name and a required `label` (decorative icons pass `label=""` and `aria-hidden`). Done when: a unit test fails when an icon-only button renders without an accessible name.
4. Create `cloud/apps/web` as a Vite project (`index.html`, `src/main.tsx`, a history-API router in `src/router.ts`, `src/api.ts` fetch client adding the CSRF header from `GET /api/session` and mapping the error body), building to `apps/web/dist` that `ASSETS` serves. Done when: `npm --prefix cloud run build -w @recto-cloud/web` emits `dist/index.html` with no inline script (the CSP forbids it).
5. Contrast gate `cloud/scripts/check-contrast.mjs`: computes WCAG ratios for the token pairs the app uses (text on window, surface and raised; secondary text on raised; accent text on raised; on-accent on accent; danger, success and warning on raised) in light and dark and fails below 4.5:1 (3:1 for the non-text pair accent against raised, WCAG 1.4.11). Done when: it exits 0 and exits 1 when a fixture token pair is lowered.

- [ ] Create `cloud/scripts/build-tokens.mjs` and `tokens:check`. Done when: both light and dark blocks contain all 19 variables and the forced-colors block maps every one.
- [ ] Create `cloud/apps/web/src/styles/scale.css` and the no-hex-literals test. Done when: the test fails on a fixture CSS file with `#fff`. Cheaper substitute: copying hex values into component CSS.
- [ ] Create `cloud/scripts/copy-brand.mjs` and `Icon.tsx`. Done when: the accessible-name test passes.
- [ ] Create the Vite project, router and API client. Done when: the build emits a script-free `index.html`.
- [ ] Create `cloud/scripts/check-contrast.mjs`. Done when: the pass and fail cases behave as above.
- [ ] Commit: `cloud/web: tokens generated from RectoColors.xaml, type scale, brand assets and Vite scaffold`

**Test checkpoint:** `npm --prefix cloud run tokens:check && node cloud/scripts/check-contrast.mjs && npm --prefix cloud run test:web` exits 0; changing `RectoAccentBrush` in the XAML without regenerating makes `tokens:check` exit 1.

## 13. Web shell with sign-in, account and consent screens

The first thing a person sees is the sign-in page, and the same shell frames every later screen. It carries Recto's identity so the web app feels like the desktop one, offers passkey sign-in first and the email link second, and gives people control of their own account. It also hosts the consent page the desktop's OAuth flow opens.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface, following `design/SPEC.md` sections 3.3 (wordmark and lockup), 4 (colour, translated to CSS custom properties), 5 (type), 7 (shape, spacing, 20 px dialog radius), 8.1 (title bar order: icon, wordmark, then actions at the right) translated to a page header, and 9.9 (dialog copy and button order); no screenshot exists for the web.
**Job:** A person signs in with a passkey or an email link, sees and ends their sessions, manages passkeys, recovery codes and email, exports data or deletes the account, and approves or denies the desktop app. Consumers: `POST /api/auth/*`, `GET /api/account*`, `POST /oauth/authorize/decision` (D09 T01 §10 to §15).
**Treatment:** One page shell (`<header>` with lockup, `<nav>`, `<main id="main">`, skip link first) and screens that use the shared form, button, dialog and toast components; the sign-in page offers `Sign in with a passkey` as the primary button and `Email me a sign-in link` as the secondary, with copy that names what happens next. Cheaper substitute that fails the checkpoint: a single email field with no passkey option, or unlabeled inputs with placeholder-only labels.
**Chrome:** consume `--recto-*` tokens only, `lockup-{light,dark}.svg`, `Icon.tsx` glyphs from Fluent System Icons, the dialog pattern of SPEC 9.9 (icon, title, text, full-width stacked buttons for alerts) and a `role="status"` toast region.

**Build order:**

1. Create `cloud/apps/web/src/ui/` primitives: `Button` (primary, secondary, danger; 32 px minimum, visible focus ring in `--recto-accent` at 2 px with 2 px offset), `TextField` (visible label, described-by hint and error), `Dialog` (modal, focus trap, `Esc` closes, focus returns to the opener, 20 px radius), `Toast` (`role="status"`, auto-dismiss never shorter than 6 seconds and pausable). Done when: `test/ui/primitives.test.tsx` checks roles, labels and focus return for each.
2. Create `cloud/apps/web/src/app/Shell.tsx` (skip link, header, nav, main landmark, `document.title` updated per route and announced), light and dark following the system with a stored override in `localStorage` wrapped in try/catch. Done when: a Playwright check finds exactly one `main` and the skip link as the first tab stop.
3. Create `cloud/apps/web/src/screens/SignIn.tsx`: passkey sign-in with `navigator.credentials.get` (conditional mediation for autofill when supported), the email-link form with a terms checkbox on first sign-up (terms text and links come from configuration, owner-supplied in section 21), a clear "Check your email" state with a resend after 60 seconds, and a same-device or other-device choice. Done when: errors (rate limited, passkey cancelled, expired link) each have a distinct, specific message and no message reveals whether an account exists.
4. Create `cloud/apps/web/src/screens/ConfirmLink.tsx` (reads the token from the URL fragment, shows "Sign in as <email>?" with a button that posts it, then clears the fragment from history) and `Consent.tsx` (client name, requested scopes in plain words, Allow and Deny, Deny the default focus). Done when: a scanner-style `GET` of the confirm page signs nobody in.
5. Create `cloud/apps/web/src/screens/Account.tsx`: profile name, sessions list with Sign out here and Sign out everywhere else, passkeys list with add, rename and remove, recovery codes (shown once, with copy and download as text, and an acknowledgement checkbox before close), email change, data export request, and delete account behind a dialog that states the 14-day grace period. Done when: each action calls its endpoint and shows success or failure through the status region.

- [ ] Create `cloud/apps/web/src/ui/Button.tsx`, `TextField.tsx`, `Dialog.tsx` and `Toast.tsx`. Done when: `test/ui/primitives.test.tsx` passes role, label and focus-return checks. Cheaper substitute: unstyled native elements with divs for dialogs.
- [ ] Create `cloud/apps/web/src/app/Shell.tsx`. Done when: the landmark and skip-link Playwright check passes.
- [ ] Create `cloud/apps/web/src/screens/SignIn.tsx`. Done when: `e2e/signin.spec.ts` signs in by email link read from `/__dev/outbox` and by a virtual authenticator passkey.
- [ ] Create `cloud/apps/web/src/screens/ConfirmLink.tsx` and `Consent.tsx`. Done when: the scanner-GET test and a consent Allow that returns a code to a loopback test server pass.
- [ ] Create `cloud/apps/web/src/screens/Account.tsx`. Done when: `e2e/account.spec.ts` revokes a second session and removes a passkey.
- [ ] Take screenshots of light, dark and a 320 px width of the sign-in and account screens into `cloud/apps/web/e2e/__screenshots__/`. Done when: the visual spec compares them with a stated pixel tolerance and fails on a layout shift.
- [ ] Commit: `cloud/web: shell, sign-in, email confirmation, consent and account screens`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/signin.spec.ts e2e/account.spec.ts` exits 0; removing the visible label from the email field makes the accessibility assertion in the spec fail.

## 14. Library page: folders, upload, rename, move, trash and quota

The library is where people live in this product. It must make uploading easy without dragging being required, keep folder operations reachable by keyboard, and show quota in words as well as a bar. It is a table, not a drawing, so screen readers announce rows, names and sizes.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface, following `design/SPEC.md` sections 4 (surfaces and selected-row tint `--recto-accent-tint`), 5 (14 px body, 12 px captions), 7 (8 px row radius, 16 px card radius), 9.1 (Home: empty state with `drop-files-{light,dark}.svg`) and 9.9 (dialog copy); no screenshot exists for the web.
**Job:** A signed-in person uploads PDFs, finds them by browsing folders, renames, moves, trashes and restores them, sees how much space they have used, and opens a document. Consumers: the endpoints of sections 3 to 7 and 11.
**Treatment:** A breadcrumb above a sortable table (`Name`, `Modified`, `Size`, `Shared`) with row checkboxes, a toolbar that acts on the selection (`Upload`, `New folder`, `Rename`, `Move to`, `Share`, `Move to trash`), row-level actions in a menu button, and a `Trash` view; upload progress and quota are in a status area. Dragging files onto the page is an extra, never the only way. Cheaper substitute that fails the checkpoint: drag-and-drop only upload, or move by dragging only.
**Chrome:** consume `Button`, `Dialog`, `Toast`, `TextField` (section 13), `--recto-*` tokens, `drop-files-{light,dark}.svg`, Fluent System Icons through `Icon.tsx`, and the `role="progressbar"` pattern for quota and uploads.

**Build order:**

1. Create `cloud/apps/web/src/screens/Library.tsx` and `src/lib/useFolder.ts` (cursor paging with a "Load more" button, not infinite scroll, so keyboard users keep their place), sorting by column header buttons with `aria-sort`, and a selection model (`Space` toggles the row checkbox, `Shift+Click` and `Shift+Arrow` extend, `Ctrl+A` selects all loaded). Done when: `e2e/library.spec.ts` navigates a 120-entry folder with the keyboard only.
2. Create the upload client `src/lib/upload.ts`: an `Upload` button and hidden file input (multiple, `accept=".pdf,application/pdf"`) plus drop on the page; 8 MiB parts, three parallel parts, each part retried three times with backoff, resume from `GET /api/uploads/:id` after a reload (upload id kept in `sessionStorage` in try/catch), a per-file row with `role="progressbar"`, Cancel and Retry, and error messages from the server code (`too_large`: "This file is larger than 200 MB."; `unsupported_type`: "This isn't a PDF."; `quota_exceeded`: "You're out of space."). Polls `GET /api/uploads/:id` until `committed`. Done when: a 25 MiB fixture uploads in four parts, a forced failure on part 2 retries and succeeds, and an uploaded non-PDF shows its message.
3. Folder operations: `New folder` and `Rename` dialogs with inline validation from the name rules, `Move to` dialog with a keyboard-operable folder tree (`role="tree"`, arrow keys, type-ahead), `Move to trash` with an undo toast for 10 seconds, and the `Trash` view with `Restore` and `Delete forever` (the latter in a confirmation dialog following SPEC 9.9 wording). Done when: `e2e/library.spec.ts` creates, renames, moves, trashes and restores through the keyboard.
4. Quota display: "1.2 GB of 2 GB used" text with a `role="progressbar"` bar, a warning state at 80 percent using the warning token plus an icon plus text, and a full state that disables upload with an explanation. Done when: the library test with a seeded usage of 85 percent shows the warning with an icon and text.
5. Empty state with the `drop-files` illustration (`alt=""`, decorative), the heading "Nothing here yet", the `Upload` button focused-first, and loading rows shown as skeletons that keep the table geometry (no window spinner). Done when: the empty and loading states render in the visual spec.

- [ ] Create `cloud/apps/web/src/screens/Library.tsx` and `useFolder.ts`. Done when: the keyboard navigation spec passes. Cheaper substitute: rendering a flat unpaged list.
- [ ] Create `cloud/apps/web/src/lib/upload.ts` and the upload rows. Done when: the multipart, retry, resume and error-message specs pass.
- [ ] Create the folder dialogs, tree and trash view. Done when: the keyboard create, rename, move, trash and restore spec passes.
- [ ] Create the quota display. Done when: the 85 percent state spec passes.
- [ ] Create the empty and loading states. Done when: the visual spec compares both in light and dark.
- [ ] Commit: `cloud/web: library page with resumable upload, folder operations, trash and quota`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/library.spec.ts` exits 0; removing the file input button (leaving drop only) makes the keyboard upload test fail.

## 15. Sharing dialog and shared-with-me list

Sharing is a decision people make carefully, so the dialog says exactly who can do what, what a link allows, and how to take it back. Roles are described in plain words, and nothing is shared until the owner presses the primary button.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface, following `design/SPEC.md` section 9.9 (form dialog: left-aligned fields, Cancel and primary side by side, radius 20, 28 px padding), 4, 5 and 7; no screenshot exists for the web.
**Job:** The owner gives named people a role, creates a link with a role, expiry and optional password, copies it, sees who has access and revokes; a recipient finds what was shared with them. Consumers: `/api/shares/*`, `/api/shared-with-me`, `/api/documents/:id/audit` (sections 8 to 10).
**Treatment:** A `Share` dialog with two parts: `People with access` (email field plus role select, a list with role selects and Remove) and `Link` (switch to create, role select, expiry date field, password field, `Allow download` checkbox for view links, `Copy link` button that announces "Link copied", `Stop sharing link`); a collapsible `Activity` list from the audit log; and a `Shared with me` page listing resources with role and owner. Cheaper substitute that fails the checkpoint: a single "anyone with the link" switch with no role, expiry or revoke.
**Chrome:** consume `Dialog`, `Button`, `TextField`, `Toast` (section 13), native `<select>` styled with tokens, `--recto-*`, and the library table component for the shared-with-me page.

**Build order:**

1. Create `cloud/apps/web/src/screens/ShareDialog.tsx` with the people section, validating the email, listing pending invitations as "Invited" and showing role descriptions: View ("Can read and download"), Comment ("Can also add comments"), Edit ("Can also upload new versions and rename"). Done when: `e2e/share.spec.ts` shares with an address, changes the role and removes it.
2. Add the link section: create shows the URL once in a read-only field with `Copy link` (Clipboard API with a selectable-text fallback) and a status announcement; expiry uses a native `<input type="date">` bounded by the maximum; password strength hint without composition rules; an existing link shows its role and expiry but never the token again ("The link can't be shown again. Create a new one to get a new link."). Done when: the spec copies the link and opens it in a clean browser context.
3. Add the Activity list reading the audit routes (event, who as name or "Link", relative and absolute time in a `<time>` element) with Load more. Done when: opening the link in step 2 appears as `Link opened`.
4. Create `cloud/apps/web/src/screens/SharedWithMe.tsx` at `/shared`. Done when: the invited user sees the resource with the role and owner after signing up with the invited address.

- [ ] Create `cloud/apps/web/src/screens/ShareDialog.tsx` people section. Done when: the people part of `e2e/share.spec.ts` passes. Cheaper substitute: a text field that takes a raw role word.
- [ ] Add the link section with copy and announcement. Done when: the link copy and clean-context open pass.
- [ ] Add the Activity list. Done when: the `Link opened` row appears.
- [ ] Create `cloud/apps/web/src/screens/SharedWithMe.tsx`. Done when: the invited-user spec passes.
- [ ] Commit: `cloud/web: sharing dialog with people and links, activity list and shared-with-me page`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/share.spec.ts` exits 0; creating a link with an expiry beyond the maximum makes the validation assertion fail.

## 16. Document page and scriptless pdf.js viewer

The web viewer renders hostile PDFs in the visitor's browser, so it carries the same promise as the desktop: no PDF JavaScript, ever (D9). `pdfjs-dist` (Apache-2.0) is configured with scripting off, no `eval`, no XFA, form fields shown but not interactive, and everything it loads comes from the same origin. Links in a document ask first, as in SPEC 9.9.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface, following `design/SPEC.md` sections 9.2 (continuous vertical scroll on the canvas colour, pages centred with 16 px gaps and a soft shadow, page outlines shown at once, never a window spinner; password dialog before any page), 9.9 (Password, Could not open and Link in a document dialogs and their copy), 4 (canvas, and the Dimmed page treatment for dark theme) and 11; no web screenshot exists.
**Job:** A signed-in person or a link holder opens a document and reads it, with the page area behaving the same for both. Consumers: `GET /api/documents/:id/versions/:n/content` with ranges and `POST /api/links/redeem` (sections 9 and 11).
**Treatment:** Routes `/d/:id` (latest version) and `/d/:id/v/:n`, and `/s` (reads the fragment token, redeems, then renders the document or the shared folder listing); a header with the name, the caller's role and a version menu; the viewer region on `--recto-canvas`. `getDocument` receives `isEvalSupported: false`, `enableXfa: false`, `useWorkerFetch: false`, a same-origin `cMapUrl`, `standardFontDataUrl` and worker, `maxImageSize` and `maxCanvasPixels` bounds; `PDFViewer` is created with `enableScripting: false`, no `PDFScriptingManager`, annotations displayed but form widgets not interactive, and a link service that opens an `Open this link?` dialog showing the full URL before `window.open(url, '_blank', 'noopener,noreferrer')`. Cheaper substitute that fails the checkpoint: embedding the browser's built-in PDF viewer in an `iframe`, or leaving pdf.js defaults.
**Chrome:** consume `Dialog`, `Button`, `Toast`, `--recto-canvas`, `--recto-raised`, the Password and Could-not-open copy of SPEC 9.9 and `Icon.tsx`.

**Build order:**

1. Add `pdfjs-dist` (pinned, recorded in `cloud/package-lock.json`) and `cloud/scripts/copy-pdfjs.mjs`, which copies the worker, cmaps, standard fonts and any WebAssembly decoders the pinned version needs into `apps/web/public/pdfjs/` so nothing loads from a CDN. If a pinned build needs `wasm-unsafe-eval`, record that exact need, add only that source to `script-src` for the viewer route in `cloud/apps/api/src/security-headers.ts`, and keep `unsafe-eval` out; otherwise leave the policy unchanged. Done when: `csp-policy.test.ts` still passes and the viewer loads under the policy.
2. Create `cloud/apps/web/src/viewer/openDocument.ts` wrapping `getDocument` with the options above, loading through `fetch` with `Range` and the CSRF-free `GET`, and handling `PasswordException` (the Password dialog: lock icon, "Enter the password", "<name> is protected.", the wrong-password text "That password didn't work.") and other errors (the Could-not-open dialog: "Couldn't open this file", "<name> is damaged or isn't a PDF.", `OK` and `Copy error details` copying the error name and request id only). Done when: tests cover a password PDF, a corrupt PDF and a truncated PDF.
3. Create `cloud/apps/web/src/viewer/Viewer.tsx` with `PDFViewer`, page outlines sized from page 1 immediately and filled as pages render, "Pages in dark theme: Dimmed" by multiplying the page canvas with `#D9D5CE`-equivalent token (the SPEC default), and `Esc` closing the topmost dialog. Done when: the visual spec shows outlines before text appears.
4. Create the document route (`src/screens/DocumentPage.tsx`) and link route (`src/screens/SharedEntry.tsx`): the latter redeems the fragment token, asks for the password when `password_required`, shows `This link has expired`, `This link was turned off` or `This link isn't valid` with no hint to guess, then renders. After the first open it reports `pageCount` once for owner and `edit` principals. Done when: `e2e/viewer.spec.ts` opens a document by id and by link, and the page count is stored.
5. Create the hostile-PDF fixtures with `cloud/apps/web/e2e/fixtures/make-fixtures.mjs` (a deterministic hand-assembled PDF writer, no dependency): `js-open-action.pdf` (an `OpenAction` that runs `app.alert('x')` and a field with an `AA` action), `launch-action.pdf`, `external-link.pdf`, `encrypted.pdf` (password `recto`), `corrupt.pdf`, `thirty-pages.pdf`. Done when: running the script twice produces byte-identical files.
6. The scripting-off proof in `e2e/viewer-safety.spec.ts`: open `js-open-action.pdf` and assert no `dialog` event fired, no `securitypolicyviolation` event, no console error from pdf.js scripting, and that every network request in the run went to the page's own origin. Done when: the spec passes, and fails when the viewer is temporarily constructed with `enableScripting: true`.

- [ ] Add `pdfjs-dist` and create `cloud/scripts/copy-pdfjs.mjs`. Done when: `apps/web/public/pdfjs/` holds the worker and a built page loads it from the same origin with the CSP unchanged (or with the one recorded wasm source).
- [ ] Create `cloud/apps/web/src/viewer/openDocument.ts`. Done when: its unit tests cover password, wrong password, corrupt and truncated files.
- [ ] Create `cloud/apps/web/src/viewer/Viewer.tsx` and the link-confirmation dialog. Done when: the external-link fixture shows `Open this link?` with the full URL and opens nothing on Cancel. Cheaper substitute: letting pdf.js open links itself.
- [ ] Create `DocumentPage.tsx` and `SharedEntry.tsx`. Done when: the open-by-id and open-by-link specs pass.
- [ ] Create `cloud/apps/web/e2e/fixtures/make-fixtures.mjs`. Done when: the byte-identical rerun check passes.
- [ ] Create `cloud/apps/web/e2e/viewer-safety.spec.ts`. Done when: the mutation of `enableScripting: true` makes it fail.
- [ ] Commit: `cloud/web: document page and a pdf.js viewer with scripting and eval disabled`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/viewer.spec.ts e2e/viewer-safety.spec.ts` exits 0; constructing the viewer with scripting enabled makes `viewer-safety.spec.ts` fail.

## 17. Viewer navigation: page controls, zoom and thumbnails

People move through a document by page number, by scrolling and by thumbnails, and expect the same keys as the desktop app where the browser allows. The viewer exposes its state to assistive technology instead of hiding it in a canvas.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface, following `design/SPEC.md` sections 8.2 (floating page toolbar: icon buttons at 16 px, 36 px targets, radius 16), 8.3 (floating side panel for thumbnails, closing when the page is clicked in a narrow window), 9.2 (zoom, fit width, page stepping), 10 (keyboard) and 11 (hit targets, tooltips with shortcuts); no web screenshot exists.
**Job:** A reader jumps to a page, steps pages, changes zoom, fits to width, rotates the view and picks a page from thumbnails. Consumers: the `PDFViewer` instance of section 16.
**Treatment:** A floating toolbar (`role="toolbar"`, roving tabindex) with Previous page, a page-number field reading "Page <n> of <total>" (`aria-live="polite"` only on change by command, not on scroll), Next page, Zoom out, a zoom select (50, 75, 100, 125, 150, 200, Fit width, Fit page), Zoom in, Rotate, and Thumbnails; thumbnails render lazily in a `role="listbox"` panel with the current page selected using `--recto-accent-tint` plus a check badge. Keys when focus is inside the viewer: `PageUp`/`PageDown`, `Home`/`End`, `Ctrl+Plus`, `Ctrl+Minus`, `Ctrl+0` (fit width), `Ctrl+1` (actual size), `Ctrl+G` (go to page), `Ctrl+B` (thumbnails), `Ctrl+R` (rotate right), each overriding the browser only while focus is inside the viewer region. Cheaper substitute that fails the checkpoint: scrolling and browser zoom only, or icon buttons without names and shortcuts in tooltips.
**Chrome:** consume the toolbar and side-panel styling tokens of `scale.css` (radius 16, floating shadow only), `Button`, `Icon.tsx`, and the tooltip pattern (name plus shortcut, also exposed through `aria-keyshortcuts`).

**Build order:**

1. Create `cloud/apps/web/src/viewer/Toolbar.tsx` with every control named, tooltips showing the shortcut, and `aria-keyshortcuts`; hit targets 36 px. Done when: the accessibility unit test lists no unnamed control.
2. Wire page and zoom state to `PDFViewer` (`currentPageNumber`, `currentScaleValue`, `pagesRotation`), clamp zoom to 25 to 400 percent, and keep a page-number input that commits on Enter or blur and reverts on invalid input with an error message tied by `aria-describedby`. Done when: `e2e/viewer-nav.spec.ts` goes to page 12 of `thirty-pages.pdf`, steps back, zooms and fits width.
3. Create `cloud/apps/web/src/viewer/Thumbnails.tsx`: pdf.js thumbnail rendering with `IntersectionObserver`, 160 px width, current page selected, Enter or click goes to the page, arrow keys move focus, and the panel closes on a page click below 1100 px wide as in SPEC 7. Done when: 30 thumbnails render lazily (only visible ones draw, measured by render calls) and keyboard selection navigates.
4. Create `cloud/apps/web/src/viewer/keys.ts` with the shortcut table and the rule that handlers run only when the event target is inside the viewer region and no dialog is open; `Esc` closes the thumbnail panel. Done when: pressing `Ctrl+G` outside the viewer keeps the browser behaviour.
5. Pinch and `Ctrl+wheel` zoom come from pdf.js; confirm they work and that animated scale respects `prefers-reduced-motion`. Done when: the reduced-motion Playwright emulation shows no transition on panel open.

- [ ] Create `cloud/apps/web/src/viewer/Toolbar.tsx`. Done when: the no-unnamed-control unit test passes.
- [ ] Wire page, zoom and rotation state. Done when: the navigation spec passes the page jump, step, zoom and fit steps.
- [ ] Create `cloud/apps/web/src/viewer/Thumbnails.tsx`. Done when: the lazy rendering and keyboard selection specs pass. Cheaper substitute: rendering all thumbnails at once.
- [ ] Create `cloud/apps/web/src/viewer/keys.ts`. Done when: the outside-viewer test keeps default behaviour.
- [ ] Add the reduced-motion handling. Done when: the emulation spec shows no transition.
- [ ] Commit: `cloud/web: viewer page navigation, zoom, rotation and lazy thumbnails`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/viewer-nav.spec.ts` exits 0; removing the viewer-region check from `keys.ts` makes the outside-viewer test fail.

## 18. Viewer search and download

Find and download are the other two things a reader does. Search runs in the browser over the text pdf.js already extracts; nothing about the query goes to the server. Download is the original bytes through the attachment route, and it is hidden, honestly, when a link turns it off.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface, following `design/SPEC.md` section 9.3 (find card: query field, match count "3 of 17", next and previous, match case and whole word options; hit colours `--recto-search-hit` and `--recto-search-hit-current`, current hit text on `--recto-on-accent`), section 10 (`Ctrl+F`, `F3`, `Shift+F3`, `Esc`) and section 11; no web screenshot exists.
**Job:** A reader finds words in the document, moves between matches and downloads the original file. Consumers: pdf.js `PDFFindController` and `GET .../content?download=1`.
**Treatment:** A find card anchored over the viewer with a `role="search"` form, a live count (`aria-live="polite"`, "No matches" and "3 of 17" as text), next and previous buttons, `Match case` and `Whole word` toggles, `Esc` to close and return focus to the viewer, and highlights drawn on the text layer with the tokens; a `Download` button in the header that is omitted for links with `allowDownload=false` and replaced by the note "Download is turned off for this link." Cheaper substitute that fails the checkpoint: relying on the browser's own find, which cannot see unrendered pages, or a download button that always shows and fails.
**Chrome:** consume the find-card styling (radius 16, floating shadow), `--recto-search-hit*` tokens, `Button`, `TextField`, `Toast` and `Icon.tsx`.

**Build order:**

1. Create `cloud/apps/web/src/viewer/FindCard.tsx` using `PDFFindController` over the full document (not only rendered pages), debounced input, result count text, `F3` and `Shift+F3` stepping when the viewer or card has focus, and the SPEC colours with the current match also marked with an outline (not colour alone). Done when: `e2e/viewer-search.spec.ts` finds a word that appears on pages 3 and 28 of `thirty-pages.pdf`, steps to the second match and the page changes.
2. Handle the empty query, a query with regular expression characters (treated as plain text), a 500-character query (truncated with a message) and a document with no text layer ("No text to search in this document. It may be a scan."). Done when: the four cases show distinct messages.
3. Create `cloud/apps/web/src/viewer/DownloadButton.tsx` using a plain `<a href download>` to `?download=1` (same-origin, so cookies apply), a clear name that includes the file name, and the link-holder state. Done when: a view link with downloads off shows the note and a direct `?download=1` request returns 403.
4. Announce results: the match count and "Match 2 of 2 on page 28" go to the status region once per command. Done when: the accessibility spec reads the live region text after a search.

- [ ] Create `cloud/apps/web/src/viewer/FindCard.tsx`. Done when: the two-page match spec passes.
- [ ] Add the four edge cases. Done when: each case's message is asserted. Cheaper substitute: leaving the field silently empty.
- [ ] Create `cloud/apps/web/src/viewer/DownloadButton.tsx`. Done when: the download spec saves bytes whose SHA-256 equals the version hash.
- [ ] Add live announcements. Done when: the live-region assertion passes.
- [ ] Commit: `cloud/web: viewer find across all pages and original-file download`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/viewer-search.spec.ts` exits 0; making the downloaded bytes differ from the stored version makes the SHA-256 assertion fail.

## 19. Accessibility: WCAG 2.2 AA checks, keyboard paths and statement

Accessibility is a property of each screen, not a late audit. This section adds the automated gate that runs on every screen in CI, the manual keyboard and screen reader paths with their evidence, and an honest accessibility statement that names the limits of PDF viewing in a browser.

**Phase:** 98
**Needs:** none
**Surface:** yes

**Fidelity:** Genuinely new surface, following `design/SPEC.md` section 11 (keyboard order, visible focus in the accent, names and tooltips on icon-only controls, 32 px hit targets, colour never the only signal) translated to WCAG 2.2 AA; the statement page follows sections 5 and 7.
**Job:** People who use a keyboard, a screen reader, zoom, forced colours or reduced motion can sign in, upload, share and read, and can read what is and is not supported. Consumers: every screen of sections 13 to 18.
**Treatment:** An `axe-core` (MPL-2.0; run as a test-only dependency, not shipped, so it never enters the AGPL runtime and is recorded as a dev tool in `NOTICE`) scan configured for WCAG 2.2 AA in Playwright on every screen and state; extra specs for the criteria axe cannot decide; a route `/accessibility` rendering the statement. Cheaper substitute that fails the checkpoint: running axe on the sign-in page only, or an accessibility statement that claims full conformance without limits.
**Chrome:** consume the shared primitives, `--recto-*` tokens, the `forced-colors` block and the `/accessibility` route in the shell navigation.

**Build order:**

1. Create `cloud/apps/web/e2e/a11y.spec.ts` that visits sign-in, confirm, consent, account, library (empty, filled, selection, upload in progress), trash, share dialog, shared-with-me, document page with the thumbnails and find card open, error dialogs and the statement page, in light and dark, and fails on any axe violation for tags `wcag2a`, `wcag2aa`, `wcag21aa` and `wcag22aa`. Done when: it passes, and fails on a fixture button with no name.
2. Add specs for what axe cannot judge, in `e2e/a11y-manual.spec.ts`: skip link is the first tab stop (2.4.1); every interactive element shows a focus ring that is not hidden by the sticky header (2.4.7, 2.4.11); every drag action has a button alternative (2.5.7: upload and move); targets are at least 24 CSS px and the shipped controls 32 px (2.5.8); at 320 CSS px width and 400 percent zoom there is no horizontal scrolling of the page (1.4.10, the PDF canvas excepted and scrollable by keyboard); text spacing overrides do not clip content (1.4.12); sign-in uses no cognitive test and allows paste and password managers (3.3.8) and does not ask for the same information twice in a flow (3.3.7); status messages are in `role="status"` regions (4.1.3). Done when: each criterion has a named test.
3. Forced colours and reduced motion: run the a11y specs with Playwright `forcedColors: 'active'` and `reducedMotion: 'reduce'`, and check that focus rings, selected rows, errors and the quota warning remain distinguishable without the accent colour. Done when: screenshots in forced-colors mode show a visible border on selected rows and an icon on every status.
4. Record a manual screen reader pass (NVDA with Firefox and Narrator with Edge on Windows) for sign-in, upload, share and reading the first page of `thirty-pages.pdf`, as a dated checklist in `cloud/apps/web/ACCESSIBILITY-TESTING.md` with the versions used and the result of each step, and fix every blocker found. Done when: the file lists the date, the tools and a pass or a filed fix for every step.
5. Create `cloud/apps/web/src/screens/Accessibility.tsx` at `/accessibility`: the conformance target (WCAG 2.2 AA), the date last reviewed, what was tested, the known limits stated plainly (the viewer exposes pdf.js's text layer, not the PDF's tag structure, so headings, tables and alternative text inside a PDF are not announced; a downloaded copy can be opened in an accessible reader; scanned PDFs have no text), and a contact address from configuration. Done when: the page renders those items and passes the axe scan.

- [ ] Create `cloud/apps/web/e2e/a11y.spec.ts`. Done when: the full-screen sweep passes in light and dark. Cheaper substitute: scanning only the default screen state.
- [ ] Create `cloud/apps/web/e2e/a11y-manual.spec.ts` with one test per listed criterion. Done when: all pass.
- [ ] Run the sweep under forced colours and reduced motion. Done when: the specs pass and the screenshots are stored.
- [ ] Create `cloud/apps/web/ACCESSIBILITY-TESTING.md` from a real NVDA and Narrator pass. Done when: every step has a result and any blocker has a fix commit.
- [ ] Create `cloud/apps/web/src/screens/Accessibility.tsx`. Done when: the statement page passes axe and states the viewer limit.
- [ ] Add `axe-core` to `NOTICE` as a test-only dependency and keep it out of `apps/web/dist`. Done when: `cloud/scripts/check-dist-deps.mjs` fails when `axe-core` appears in the production bundle.
- [ ] Commit: `cloud/web: WCAG 2.2 AA sweep, criterion specs, screen reader record and accessibility statement`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/a11y.spec.ts e2e/a11y-manual.spec.ts` exits 0; removing the accessible name from one icon-only button makes the axe sweep fail.

## 20. Playwright end-to-end suite for sign-in, upload, share and read

The unit tests prove each piece; this suite proves a person can do the whole job through a real browser against the real Worker, with the dev email outbox standing in for a mailbox and a virtual authenticator standing in for a passkey. It is the regression net for the other cloud files too.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Create `cloud/apps/web/e2e/journey.spec.ts`: Alice signs up by email link, adds a passkey, signs out, signs in with the passkey, uploads a 25 MiB multipart PDF, creates a folder and moves the document into it, shares it with Bob's address as `comment`, creates a password-protected view link that expires in 7 days, and Bob signs up with the invited address and opens the document; an anonymous context opens the link, fails with a wrong password, then opens with the right one and sees the viewer; Alice revokes the link and the anonymous context gets "This link was turned off"; Alice trashes and restores the document. Done when: the spec passes in under the CI timeout.
2. Negative journeys in `e2e/security.spec.ts`: CSRF (a cross-origin form POST is refused), a second user's document id returns the same not-found page as a missing id, a non-PDF renamed `.pdf` is refused with the message, a file over the cap is refused before upload completes, session cookie flags (`HttpOnly`, `Secure`, `SameSite=Lax`, `__Host-` prefix) read from the browser context, CSP header present on every navigation, and no `securitypolicyviolation` on any visited page. Done when: each case asserts its observable outcome.
3. Make the suite hermetic: `cloud/apps/web/e2e/global-setup.ts` resets local state (D1, R2, Durable Objects) through the Worker's persistence directory, and each test uses unique addresses from a counter. Done when: running the suite twice in a row, and in parallel with two workers, passes both times.
4. Wire `e2e` into `cloud-ci.yml` (D09 T01 §20) with traces and screenshots kept on failure. Done when: a deliberately failing assertion in a draft pull request uploads a trace artifact.

- [ ] Create `cloud/apps/web/e2e/journey.spec.ts`. Done when: the full journey passes. Cheaper substitute: API-only tests that never load the browser pages.
- [ ] Create `cloud/apps/web/e2e/security.spec.ts`. Done when: every negative case passes and fails when its protection is removed.
- [ ] Create `cloud/apps/web/e2e/global-setup.ts`. Done when: the repeated and parallel runs pass.
- [ ] Add trace upload on failure to the workflow. Done when: the draft pull request run shows the artifact.
- [ ] Commit: `cloud/web: end-to-end journeys and negative security specs`

**Test checkpoint:** `npm --prefix cloud run e2e -- e2e/journey.spec.ts e2e/security.spec.ts` exits 0; disabling the CSRF check in `cloud/apps/api/src/auth/csrf.ts` makes the security spec fail.

## 21. Quota, retention and terms values recorded from the owner

The mechanisms above run on defaults. The numbers that become a public promise are the owner's decision: how much space a person gets, how long trash, audit and unclaimed uploads are kept, and the terms and privacy text people accept at sign-up. This section records the answers in configuration; it changes values, not code.

**Phase:** 98
**Needs:** owner-decision, third-party
**Surface:** no

**Build order:**

1. Obtain from the owner and record in `cloud/apps/api/wrangler.jsonc` production vars: `DEFAULT_QUOTA_BYTES`, `MAX_UPLOAD_BYTES`, `TRASH_RETENTION_DAYS`, `AUDIT_RETENTION_DAYS`, `LINK_MAX_DAYS`, the account deletion grace period, and any paid or free tier names. Done when: `wrangler deploy --dry-run --env production` passes and `cloud/apps/api/test/config.test.ts` asserts the recorded values.
2. Obtain the terms of service and privacy policy text and version from the legal reviewer (`third-party`), serve them at `/terms` and `/privacy` from `cloud/apps/web/src/screens/Legal.tsx` rendering the supplied Markdown, and set the configured `termsVersion` so sign-up (D09 T01 §10) records acceptance of that version. Done when: sign-up refuses a stale `termsVersion` and the pages render the supplied text unmodified.
3. Record each decision, its date and the cost of changing it in the design spec's decision table by a dated correction, so the next reader sees the owner's choice. Done when: `docs/specs/2026-10-21-recto-cloud-design.md` lists the values with the decision date.

- [ ] Record the owner's limits in `cloud/apps/api/wrangler.jsonc`. Done when: the production dry run and the config test pass. Cheaper substitute: leaving the engineering defaults and calling them final.
- [ ] Create `cloud/apps/web/src/screens/Legal.tsx` and wire `/terms` and `/privacy`. Done when: `e2e/legal.spec.ts` shows the supplied text and the stale-version refusal.
- [ ] Add the dated correction to `docs/specs/2026-10-21-recto-cloud-design.md`. Done when: the file lists each value with its date.
- [ ] Commit: `cloud: record owner-approved limits, retention and terms`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/config.test.ts && npm --prefix cloud run e2e -- e2e/legal.spec.ts` exits 0; setting `termsVersion` to an older value makes the stale-version assertion fail.

## Verification

- A clean checkout runs `npm --prefix cloud run lint`, `typecheck`, `test`, `e2e` and `smoke:selfhost` to exit 0 with the Playwright journey, security, safety and accessibility specs included, quoted in the stamp.
- The scripting-off proof (`e2e/viewer-safety.spec.ts`) fails when scripting is enabled and passes with the shipped configuration; no request leaves the page's own origin.
- Identical bytes uploaded by two accounts produce one blob and two versions; a trashed, revoked, expired or blocked resource is unreadable on the next request.
- `node cloud/scripts/check-contrast.mjs` passes for light and dark, and `npm --prefix cloud run tokens:check` proves the CSS matches `design/tokens/RectoColors.xaml`.
- Sections 1 to 20 need no owner input; section 21 stays open until the owner records the limits and the legal text.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
