---
schema_version: 1
id: cloud-platform
domain: 09-cloud
status: active
title: "TODO-01 -- Recto Cloud Platform, Accounts and Security"
---

# TODO-01 -- Recto Cloud Platform, Accounts and Security

> **Goal:** The `cloud/` npm workspace exists as an AGPL-3.0-or-later TypeScript project that runs on Cloudflare Workers (D1, R2, Durable Objects, Queues, Cron) and, from the same code, on plain `workerd` with a SQLite database and an S3-compatible store. People can sign up and sign in with passkeys or one-time email links, hold sessions in safe cookies, recover their account, export their data and delete their account, and the desktop app can obtain tokens through OAuth 2.0 authorization code with PKCE. Every response carries strict security headers, logs never contain document content, email goes out through a Queue consumer and SMTP2GO, abuse can be handled by an admin, backups restore, and CI proves all of it on every pull request.

> **Current state:** Measured 2026-10-21. No `cloud/` directory exists. Foundation plan D20 and `docs/specs/2026-10-21-recto-cloud-design.md` fix the stack and licence. `todo/09-cloud/` holds this file and four siblings (T02 to T05), all in phase 98. `.github/workflows/` has `build.yml`, `pins.yml` and `plan.yml` for the desktop and the TODO tree; none touches `cloud/`. `design/tokens/RectoColors.xaml` exists for the web tokens built in T02. The public domain, the email sender domain, the data region and the seal certificate are open owner decisions (design spec, "Open owner decisions"); only section 21 waits on them.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` row D20 and the 2026-10-21 correction under section 3.2 (the `cloud/` layout and its isolation from `Recto.sln`); D9 (zero PDF JavaScript) and D16 (offline by default) bound what the web app and the desktop may do.
- `docs/specs/2026-10-21-recto-cloud-design.md`: decisions table, architecture, security and privacy, testing, open owner decisions. Where a section here says "the spec", it means this file.
- RFC 8252 (OAuth 2.0 for native apps: loopback redirect), RFC 7636 (PKCE), RFC 6749, and the W3C WebAuthn Level 2 specification for the account endpoints.
- -> XREF: D09 T05 §3 -- the OAuth 2.0 PKCE endpoints and scopes the Windows app signs in through.
- -> XREF: D09 T03 §8 -- review invitations, mentions and reminders use the email Queue consumer.
- -> XREF: D09 T04 §22 -- the signing ceremony end-to-end suite runs in CI.

## Outcome

- `npm --prefix cloud ci && npm --prefix cloud run lint && npm --prefix cloud run typecheck && npm --prefix cloud test` exits 0 on a clean checkout, and `npm --prefix cloud run licenses` fails when a dependency has a licence outside the AGPL-compatible allowlist.
- A new person can create an account with a one-time email link, add a passkey, sign out and sign back in with the passkey, and recover access through email; the same flows run in Playwright against a locally running Worker.
- The desktop app's loopback flow gets an access token and a rotating refresh token from `/oauth/token`, and a reused refresh token revokes the whole grant.
- `npm --prefix cloud run smoke:selfhost` starts plain `workerd`, applies migrations, signs a user in through the dev email outbox and stores and reads a blob through the S3-compatible adapter.
- A user can download a ZIP of all their data and delete their account; an admin can suspend an account and take a document hash down; a backup restores into an empty database and passes the integrity check.

**Adjacency:** list=applicable @ §16; document=applicable @ §15; settings=applicable @ §13; reporting=applicable @ §16; notifications=applicable @ §9; permissions=applicable @ §7; audit=applicable @ §3; exchange=applicable @ §14; reverse=applicable @ §15

**Adjacency rationale:** The find-without-an-id surface is the admin account list and abuse queue (§16); documents themselves are listed in T02. What an account holder carries away is the data export ZIP (§15), and what they can undo is deletion within its grace period (§15) and session or passkey revocation (§13). Tunables with a named consumer are the account security settings in §13 (sessions, passkeys, email change) and the deployment variables read by §2. Reporting is the admin usage and abuse summary in §16. Notifications are the transactional emails of §9 (sign-in link, new passkey, email change, export ready, deletion scheduled). Permissions are the principal and role model in §7 that T02 §7 extends with document roles. Audit is the append-only `security_events` table created in §3 and written by every sign-in, recovery, token and admin action. Exchange is the OAuth surface for the desktop (§14) and the export (§15). The tamper-evident envelope chain is T04, not here.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Workspace scaffold, AGPL licence, NOTICE and licence allowlist check | -- | [ ] |
| 2 | §2 | Wrangler configuration for dev, staging and production with typed bindings | §1 | [ ] |
| 3 | §3 | D1 schema, forward-only migrations and the SQLite database interface | §2 | [ ] |
| 4 | §4 | Storage interface with R2 and S3-compatible adapters | §2 | [ ] |
| 5 | §5 | API foundation: router, error model, request ids and content-free logging | §2 | [ ] |
| 6 | §6 | Security headers and strict Content Security Policy | §5 | [ ] |
| 7 | §7 | Sessions, HttpOnly cookies, principals and CSRF protection | §3, §5 | [ ] |
| 8 | §8 | Rate limits with a Durable Object counter | §5 | [ ] |
| 9 | §9 | Email Queue consumer, SMTP2GO transport and dev outbox | §3, §5 | [ ] |
| 10 | §10 | Sign-up and sign-in with one-time email links | §7, §8, §9 | [ ] |
| 11 | §11 | Passkey registration and sign-in (WebAuthn) | §7, §8, §10 | [ ] |
| 12 | §12 | Account recovery and email change | §10, §11 | [ ] |
| 13 | §13 | Account settings API: profile, sessions and passkeys | §7, §11 | [ ] |
| 14 | §14 | OAuth 2.0 authorization code with PKCE endpoints for the desktop app | §7, §10 | [ ] |
| 15 | §15 | Data export and account deletion with a grace period | §3, §4, §7, §9 | [ ] |
| 16 | §16 | Admin API, abuse reports and takedown by content hash | §3, §7, §8 | [ ] |
| 17 | §17 | Backups and restore | §3, §4 | [ ] |
| 18 | §18 | Self-host adapters: Durable Object SQLite, job queue and scheduler | §3, §9 | [ ] |
| 19 | §19 | Self-host build on plain workerd with a smoke test | §4, §5, §18 | [ ] |
| 20 | §20 | GitHub Actions CI: lint, typecheck, Vitest, Playwright and self-host smoke | §1, §19 | [ ] |
| 21 | §21 | Staging and production deploy job gated on operator secrets | §20 | [ ] |

## 1. Workspace scaffold, AGPL licence, NOTICE and licence allowlist check

The workspace is the base every other cloud section builds on and must stay isolated from the desktop: it adds no file to `Recto.sln`, no dependency to `src/`, and no network use to the Windows app. It carries its own AGPL-3.0-or-later licence because the service is network software; the desktop stays GPL-3.0-or-later (D20). Every dependency must be AGPL-compatible (MIT, BSD, Apache-2.0 and similarly permissive terms), and that is enforced by a script, not by memory.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Create `cloud/package.json` (private, npm `workspaces`: `apps/*`, `packages/*`), `cloud/tsconfig.base.json` (strict, `noUncheckedIndexedAccess`, ES2022, `moduleResolution: bundler`), `cloud/eslint.config.js`, `cloud/.editorconfig`, `cloud/.gitignore` and `cloud/.nvmrc` (current Node LTS). Done when: `npm --prefix cloud install` creates `cloud/package-lock.json` and `npm --prefix cloud ls --workspaces` lists `@recto-cloud/api`, `@recto-cloud/web`, `@recto-cloud/db`, `@recto-cloud/storage` and `@recto-cloud/shared`.
2. Create the workspace packages with an `index.ts` and a `package.json` each: `cloud/apps/api`, `cloud/apps/web`, `cloud/packages/db`, `cloud/packages/storage`, `cloud/packages/shared`. Done when: `npm --prefix cloud run typecheck` compiles all five with `tsc -b`.
3. Add `cloud/LICENSE` (the verbatim AGPL-3.0 text), an SPDX `AGPL-3.0-or-later` `license` field in every `package.json`, `cloud/NOTICE` (each runtime dependency with its licence and copyright line) and `cloud/scripts/check-licenses.mjs`, which reads the lockfile and fails on any licence outside MIT, BSD-2-Clause, BSD-3-Clause, Apache-2.0, ISC, 0BSD, CC0-1.0, Unlicense and BlueOak-1.0.0 (dual-licensed packages pass if one option is allowed; packages the lockfile marks `dev: true`, which are never distributed, may additionally use MPL-2.0). Done when: `npm --prefix cloud run licenses` exits 0, and exits 1 naming the package when a test fixture lockfile lists a GPL-2.0-only package.
4. Add root scripts `lint`, `typecheck`, `test`, `test:api`, `test:web`, `test:pkg`, `e2e`, `licenses`, `smoke:selfhost` and `cloud/README.md` (how to run each, one line per command). Done when: every script name in `cloud/README.md` exists in `cloud/package.json`, checked by `cloud/scripts/check-readme-scripts.mjs`.

- [ ] Create `cloud/package.json`, `cloud/tsconfig.base.json` and `cloud/eslint.config.js`. Done when: `npm --prefix cloud run lint` and `npm --prefix cloud run typecheck` exit 0 on the empty packages. Cheaper substitute: root-level `src/` TypeScript outside the workspaces layout.
- [ ] Add `cloud/LICENSE` with the AGPL-3.0 text and SPDX fields in each `package.json`. Done when: `cloud/scripts/check-licenses.mjs` fails if any workspace `package.json` lacks `AGPL-3.0-or-later`. Cheaper substitute: a licence badge in the README only.
- [ ] Write `cloud/NOTICE` and `cloud/scripts/check-licenses.mjs` with its allowlist. Done when: the script's fixture test passes for allowed licences and fails for `GPL-2.0-only`, `LGPL-2.1-only` and `UNLICENSED`. Cheaper substitute: checking only direct dependencies.
- [ ] Write `cloud/scripts/check-readme-scripts.mjs` and `cloud/README.md`. Done when: the script fails when the README names a script that does not exist.
- [ ] Add `cloud/` to `.gitattributes` text normalisation only if needed, and confirm `Recto.sln` and `src/` are untouched. Done when: `git diff --stat -- Recto.sln src` is empty.
- [ ] Commit: `cloud: scaffold the AGPL workspace with a licence allowlist check`

**Test checkpoint:** `npm --prefix cloud run licenses && npm --prefix cloud run typecheck && npm --prefix cloud run lint` exits 0; replacing a `license` field with `MIT` in one workspace `package.json` makes `npm --prefix cloud run licenses` exit 1.

## 2. Wrangler configuration for dev, staging and production with typed bindings

One configuration source per environment keeps production differences visible. It declares every binding once, and a generated `Env` type means code cannot read a binding that is not declared. Production names that depend on owner decisions (domain, data region) are variables, not literals, and are filled in section 21.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Create `cloud/apps/api/wrangler.jsonc` with a default (dev) environment and `env.staging` and `env.production` blocks. Bindings: D1 `DB`, R2 `BLOBS` and `BACKUPS`, Durable Objects `RATE_LIMITER` (and the classes later sections add), Queues `EMAIL_QUEUE`, `JOBS_QUEUE` plus their consumers, assets `ASSETS` (the built `apps/web/dist`, `run_worker_first: true` so section 6 headers apply), cron triggers, and vars `ENVIRONMENT`, `PUBLIC_ORIGIN`, `RP_ID`, `EMAIL_FROM`, `MAX_UPLOAD_BYTES`. Done when: `npx wrangler deploy --dry-run --env staging` (run from `cloud/apps/api`) validates with no error.
2. Generate `cloud/apps/api/src/env.d.ts` with `wrangler types` and add a script `types:check` that fails when it is stale. Done when: editing a binding name in `wrangler.jsonc` without regenerating makes `npm --prefix cloud run types:check` exit 1.
3. Create `cloud/apps/api/.dev.vars.example` listing every secret (`SMTP2GO_API_KEY`, `SESSION_PEPPER`, `OAUTH_SIGNING_KEY`) with a note that the real file is gitignored. Done when: `git check-ignore cloud/apps/api/.dev.vars` prints the path.
4. Add `cloud/apps/api/src/config.ts` that reads and validates the environment variables once (typed, with a clear startup error naming the missing key, never its value). Done when: a test that omits `PUBLIC_ORIGIN` gets an error naming `PUBLIC_ORIGIN`.

- [ ] Create `cloud/apps/api/wrangler.jsonc` with the three environments. Done when: `wrangler deploy --dry-run` succeeds for default, staging and production. Cheaper substitute: one environment with comments describing the others.
- [ ] Generate `cloud/apps/api/src/env.d.ts` and the `types:check` script. Done when: the stale-type edit above fails the script.
- [ ] Add `cloud/apps/api/.dev.vars.example` and the `.gitignore` entry. Done when: `git check-ignore` confirms `.dev.vars` is ignored and the example is tracked.
- [ ] Write `cloud/apps/api/src/config.ts` and `cloud/apps/api/test/config.test.ts`. Done when: the missing-variable and malformed-origin tests pass and a value never appears in an error message.
- [ ] Add `cloud/apps/api/vitest.config.ts` using the Workers pool with `wrangler.jsonc` bindings. Done when: a smoke test reads `env.DB` and `env.BLOBS` inside workerd.
- [ ] Commit: `cloud: wrangler environments, typed bindings and validated config`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/config.test.ts` exits 0; deleting `PUBLIC_ORIGIN` from the test environment makes the config test for a clear error fail only if the error text stops naming the key.

## 3. D1 schema, forward-only migrations and the SQLite database interface

Account data lives in D1, which is SQLite, so the data layer is a small interface over SQL that has two implementations (D1 here, Durable Object SQLite in section 18). Migrations are plain numbered `.sql` files so both implementations run the same text. Nothing may drop a table or column without an explicit marker, because a bad migration on production data is expensive to reverse.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Define `SqlDatabase` in `cloud/packages/db/src/index.ts`: `query<T>(sql, params)`, `run(sql, params)`, `batch(statements)` (atomic), `transaction`-free by design (D1 has no interactive transactions, so all multi-statement writes use `batch`). Add `D1Database` adapter `cloud/packages/db/src/d1.ts`. Done when: a Workers-pool test inserts and reads a row through the interface.
2. Write `cloud/packages/db/migrations/0001_accounts.sql`: `users` (id, email unique lower-cased, email_verified_at, display_name, role `user|admin`, status `active|suspended|pending_deletion`, terms_version, terms_accepted_at, deletion_requested_at, created_at), `passkeys` (id, user_id, credential_id unique, public_key, sign_count, transports, label, created_at, last_used_at), `sessions` (id, user_id, token_hash unique, created_at, last_seen_at, expires_at, user_agent_class, revoked_at), `login_tokens` (token_hash, purpose, email, user_id, cross_device, expires_at, consumed_at), `webauthn_challenges`, `oauth_codes`, `oauth_refresh_tokens` (with `family_id`), `security_events` (append-only: id, at, actor_user_id, type, subject, request_id, detail JSON without content), `abuse_reports`, `blocked_hashes`, `deleted_accounts` (opaque user id, purged_at), `backup_fence` (single row, expires_at). Done when: the migration applies to an empty D1 and `PRAGMA foreign_key_check` returns no rows.
3. Add `cloud/packages/db/src/migrate.ts` (applies numbered files in order, records them in `schema_migrations`, refuses gaps and edits to applied files by checksum) and `cloud/scripts/lint-migrations.mjs` (rejects `DROP TABLE`, `DROP COLUMN` and `DELETE FROM` without a `-- destructive: approved <reason>` line, rejects non-sequential numbers). Done when: both fail on fixture migrations that break the rule.
4. Add `cloud/packages/db/src/ids.ts` (`newId(prefix)`: prefix plus 128 random bits, base32) and `cloud/packages/db/src/time.ts` (integer Unix milliseconds, injected clock for tests). Done when: ids are unique across 100000 calls and the clock is replaceable in tests.

- [ ] Create `cloud/packages/db/src/index.ts` and `cloud/packages/db/src/d1.ts`. Done when: the round-trip test passes in workerd. Cheaper substitute: calling `env.DB.prepare` directly across the code.
- [ ] Create `cloud/packages/db/migrations/0001_accounts.sql`. Done when: `cloud/apps/api/test/db/migrations.test.ts` applies it and checks every table, index and unique constraint exists by reading `sqlite_master`.
- [ ] Create `cloud/packages/db/src/migrate.ts`. Done when: re-applying is a no-op and editing an applied file fails with a checksum error.
- [ ] Create `cloud/scripts/lint-migrations.mjs` and wire it into `npm --prefix cloud run lint`. Done when: a `DROP TABLE` fixture fails and the same fixture with the approval marker passes.
- [ ] Create `cloud/packages/db/src/ids.ts` and `cloud/packages/db/src/time.ts`. Done when: `cloud/packages/db/test/ids.test.ts` passes.
- [ ] Write the `security_events` writer `cloud/packages/db/src/events.ts` (`recordEvent(db, {type, actor, subject, requestId, detail})`) that rejects `detail` keys outside an allowlist (`reason`, `count`, `role`, `target`, `scopes`, `ip_class`). Done when: a test passing `detail: {email: 'a@b.c'}` is rejected. Cheaper substitute: free-form JSON detail.
- [ ] Commit: `cloud: accounts schema, migration runner and the SQL database interface`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/db` exits 0 and `npm --prefix cloud run lint` rejects an unmarked `DROP TABLE` migration.

## 4. Storage interface with R2 and S3-compatible adapters

Documents are large hostile files, so the Worker never buffers them: the interface moves streams and supports multipart uploads. R2 is the production adapter; the S3-compatible adapter lets self-hosters use MinIO, Backblaze B2 or AWS S3. Keys are chosen by the caller, so content addressing stays in T02.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Define `BlobStore` in `cloud/packages/storage/src/index.ts`: `head(key)`, `get(key, {range})` returning a `ReadableStream` plus size and etag, `put(key, stream, {size, sha256?, contentType})`, `delete(key)`, `copy(from, to)`, `list(prefix, cursor)`, and `multipart`: `create(key)`, `uploadPart(handle, n, stream, size)`, `complete(handle, parts)`, `abort(handle)`. Errors are a closed union (`not_found`, `too_large`, `checksum_mismatch`, `unavailable`). Done when: a shared contract suite `cloud/packages/storage/test/contract.ts` exports one function that any adapter test calls.
2. Implement `R2BlobStore` in `cloud/packages/storage/src/r2.ts` over the R2 binding. Done when: the contract suite passes in the Workers pool.
3. Implement `S3BlobStore` in `cloud/packages/storage/src/s3.ts` using SigV4 signing with Web Crypto only (no AWS SDK, so no heavy dependency), path-style addressing and a configurable endpoint. Done when: the contract suite passes against `cloud/selfhost/fake-s3.mjs` (an in-repo S3 subset server: put, get with range, head, delete, copy, list, multipart) and, when `S3_TEST_ENDPOINT` is set, against a real MinIO.
4. Add `cloud/packages/storage/src/factory.ts` selecting the adapter from `STORAGE_DRIVER` (`r2` or `s3`). Done when: an unknown driver fails at startup naming the variable.

- [ ] Create `cloud/packages/storage/src/index.ts` with the interface and error union. Done when: `tsc -b` compiles both adapters against it.
- [ ] Create `cloud/packages/storage/test/contract.ts`. Done when: it covers range reads, zero-byte objects, overwrite, copy, list paging, multipart complete and abort, and a wrong `sha256` rejected with `checksum_mismatch`.
- [ ] Create `cloud/packages/storage/src/r2.ts`. Done when: the contract suite passes in `cloud/apps/api/test/storage/r2.test.ts`.
- [ ] Create `cloud/packages/storage/src/s3.ts` and `cloud/selfhost/fake-s3.mjs`. Done when: `npm --prefix cloud run test:pkg -- storage` passes the contract suite. Cheaper substitute: mocking `fetch` per call, which would not catch signing errors.
- [ ] Create `cloud/packages/storage/src/factory.ts`. Done when: the unknown-driver test passes.
- [ ] Commit: `cloud: storage interface with R2 and S3-compatible adapters`

**Test checkpoint:** `npm --prefix cloud run test:pkg -- storage && npm --prefix cloud run test:api -- test/storage` exits 0; breaking SigV4 canonical ordering in `s3.ts` makes the fake server answer 403 and the contract suite fail.

## 5. API foundation: router, error model, request ids and content-free logging

Every later route sits on this layer, so the rules the spec requires live here: one error shape, one request id, and logs that carry identifiers and event types but never document content, other users' file names, email bodies or tokens. The logger accepts typed fields only, so a careless `console.log(request)` cannot happen.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add a small router (Hono, MIT, or an in-repo router if the licence check or bundle size objects) in `cloud/apps/api/src/app.ts` with `createApp(deps)` so tests build an app with fakes. Done when: `GET /api/health` returns `{"ok":true}` and a request to an unknown path returns the error shape below.
2. Define `ApiError` and the JSON error body `{error:{code, message, requestId}}` in `cloud/apps/api/src/errors.ts`; unexpected exceptions return `internal` with the request id and no stack. Done when: a route that throws returns 500 with no exception text.
3. Add request-id middleware (`X-Request-Id`, generated, never trusted from the client) and `cloud/apps/api/src/log.ts`: `log.event(type, fields)` with fields limited to `requestId`, `route` (the route pattern such as `/s/:token`, never the concrete path), `status`, `durationMs`, `userId`, `eventType`, `errorCode`. Done when: a test sends canary strings in the path token, query, headers, JSON body and a file name, then asserts none appears in any captured log line.
4. Add an ESLint rule in `cloud/eslint.config.js` banning `console.*`, `eval` and `new Function` in `apps/api/src`. Done when: a fixture file using `console.log` fails lint.

- [ ] Create `cloud/apps/api/src/app.ts` and `cloud/apps/api/src/index.ts` (Worker entry exporting `fetch`, `queue`, `scheduled`). Done when: `GET /api/health` passes in `test/app.test.ts`.
- [ ] Create `cloud/apps/api/src/errors.ts`. Done when: validation, not-found, forbidden, rate-limited and internal cases return the documented body and status.
- [ ] Create `cloud/apps/api/src/log.ts` and the request-id middleware. Done when: the canary test in `cloud/apps/api/test/log.test.ts` passes. Cheaper substitute: redacting known secrets with a regex after formatting, which misses new fields.
- [ ] Add the `no-console`, `no-eval` and `no-new-func` rules. Done when: the lint fixture fails.
- [ ] Add `cloud/apps/api/src/validate.ts` (strict body parsing with Zod, MIT; unknown fields rejected; body size cap 64 KiB for JSON routes). Done when: an oversized JSON body returns 413 `too_large`.
- [ ] Commit: `cloud/api: router, error model, request ids and content-free logging`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/log.test.ts test/app.test.ts` exits 0; adding `requestUrl` to the logger fields makes the canary test fail.

## 6. Security headers and strict Content Security Policy

The web app and the API are one origin, so one header layer protects both. The default is the most restrictive policy; routes opt in to what they need. User-supplied files are never served as active content: they are attachments with `nosniff` and a sandbox policy, and the viewer reads bytes with `fetch`, not by navigating to them.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Write `cloud/apps/api/src/security-headers.ts` with `headersFor(kind)` for `html`, `json`, `asset` and `download`. HTML: `default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; font-src 'self'; connect-src 'self'; worker-src 'self'; form-action 'self'; base-uri 'none'; object-src 'none'; frame-ancestors 'none'; report-uri /api/csp-report` (no `unsafe-inline`, no `unsafe-eval`). JSON: `default-src 'none'; frame-ancestors 'none'`. Download: `default-src 'none'; sandbox`, `Content-Disposition: attachment`. All kinds: `Strict-Transport-Security` (2 years, includeSubDomains), `X-Content-Type-Options: nosniff`, `Referrer-Policy: no-referrer`, `Cross-Origin-Opener-Policy: same-origin`, `Cross-Origin-Resource-Policy: same-origin`, `X-Frame-Options: DENY`, `Permissions-Policy` denying camera, microphone, geolocation, payment and USB, `Cache-Control: no-store` on API and sensitive HTML. Done when: unit tests assert each header per kind.
2. Apply the headers in one middleware that wraps every response, including errors and static assets served through `ASSETS`. Done when: a test enumerates every registered route and asserts the headers on a sample response of each, so a new route cannot skip them.
3. Add `POST /api/csp-report` that logs only the violated directive and blocked URI scheme (no URL path, no query), rate-limited by section 8. Done when: a report containing a canary path logs no canary.
4. Add `cloud/apps/api/test/csp-policy.test.ts` that parses the HTML policy and fails if it contains `unsafe-inline`, `unsafe-eval`, a wildcard source or any host. Done when: adding `'unsafe-eval'` to the policy fails the test.

- [ ] Create `cloud/apps/api/src/security-headers.ts`. Done when: per-kind header tests pass.
- [ ] Wire the middleware around the app and the `ASSETS` fetcher in `cloud/apps/api/src/index.ts`. Done when: a response from `ASSETS` carries the HTML policy.
- [ ] Create the CSP report route. Done when: the canary test passes.
- [ ] Create `cloud/apps/api/test/csp-policy.test.ts`. Done when: it fails on any weakening of the policy. Cheaper substitute: a snapshot of the header string, which accepts a weakened policy after one update.
- [ ] Commit: `cloud/api: security headers and a strict content security policy on every response`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/security-headers.test.ts test/csp-policy.test.ts` exits 0; adding `'unsafe-inline'` to `script-src` makes it exit 1.

## 7. Sessions, HttpOnly cookies, principals and CSRF protection

A session is a random 256-bit secret in a `__Host-` cookie; the database stores only its SHA-256 hash, so a database leak does not yield live sessions. Every request resolves to one `Principal` (anonymous, user, or bearer-token client), which T02 extends with document roles. State-changing browser requests need CSRF protection beyond `SameSite=Lax`.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `cloud/apps/api/src/auth/session.ts`: `createSession(userId)` returns the secret once, stores the hash; `resolveSession(cookie)` checks `expires_at` (sliding 30 days, absolute 90 days, both defaults named in `config.ts`), `revoked_at` and the user's status (a suspended user resolves to anonymous). Cookie: `__Host-recto_session`, `HttpOnly; Secure; SameSite=Lax; Path=/`, no `Domain`. Done when: tests cover expiry, revocation, suspension and rotation of the secret on privilege change (sign-in, passkey added).
2. Implement `cloud/apps/api/src/auth/principal.ts`: `Principal = Anonymous | User{userId, role, sessionId, via:'cookie'|'bearer', scopes?}` resolved from the cookie or an `Authorization: Bearer` access token (token verification arrives in section 14; this section defines the interface and an injectable verifier). Done when: handlers receive a principal and `requireUser`, `requireAdmin` and `requireRecentAuth(minutes)` helpers return 401, 403 or 401 `reauth_required`.
3. Implement CSRF in `cloud/apps/api/src/auth/csrf.ts`: cookie-authenticated `POST`, `PUT`, `PATCH`, `DELETE` need a matching `Origin` (or `Sec-Fetch-Site: same-origin` when `Origin` is absent) and an `X-CSRF-Token` header equal to a per-session token returned by `GET /api/session`. Bearer-authenticated requests are exempt because the browser never attaches them. Done when: tests cover missing, mismatched and cross-site origin, and bearer exemption.
4. Add `GET /api/session` and `POST /api/session/logout`. Done when: logout revokes the row and clears the cookie.

- [ ] Create `cloud/apps/api/src/auth/session.ts`. Done when: `test/auth/session.test.ts` covers hash-only storage by reading the table and finding no secret.
- [ ] Create `cloud/apps/api/src/auth/principal.ts`. Done when: the three helper outcomes are tested.
- [ ] Create `cloud/apps/api/src/auth/csrf.ts`. Done when: the cross-site POST test returns 403 `csrf`. Cheaper substitute: relying on `SameSite=Lax` alone.
- [ ] Add the session routes. Done when: logout then reuse of the old cookie returns 401.
- [ ] Record `session.created`, `session.revoked` through `recordEvent`. Done when: the events table shows them with the request id and no secret.
- [ ] Commit: `cloud/api: hashed sessions, __Host cookies, principals and CSRF protection`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/auth/session.test.ts test/auth/csrf.test.ts` exits 0; removing the `Origin` check makes the cross-site test fail.

## 8. Rate limits with a Durable Object counter

Sign-in, link redemption, password unlock, reports and exports are the abuse targets named in the spec. A Durable Object per limit key gives an exact counter that works the same on Workers and `workerd`; keys are hashed so the limiter never stores an email address or an IP in the clear.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `RateLimiter` Durable Object in `cloud/apps/api/src/ratelimit/limiter.ts`: sliding-window counter with `check(limit, windowMs)` returning `{allowed, remaining, retryAfterMs}`, state pruned by alarm. Done when: tests drive it with an injected clock across the window edge.
2. Add `cloud/apps/api/src/ratelimit/policy.ts` with named policies: `signin-email` (5 per hour per email hash and 20 per hour per IP hash), `token-redeem` (10 per 10 minutes per IP hash), `passkey-attempt`, `share-unlock` (5 per 10 minutes per link), `report` (5 per hour per IP hash), `export` (3 per day per user). Done when: each policy is a constant tested at its boundary.
3. Add middleware `limit(policyName, keyFn)` that returns 429 `rate_limited` with `Retry-After` and logs only the policy name. Done when: the 6th sign-in request for one email in an hour returns 429 and a different email is unaffected.
4. Document the hashing: key = HMAC-SHA-256 of the raw key with `SESSION_PEPPER`. Done when: a test inspects the Durable Object storage and finds no raw email or IP.

- [ ] Create `cloud/apps/api/src/ratelimit/limiter.ts` and export the class from the Worker entry. Done when: `wrangler.jsonc` migration lists `RateLimiter` and the clock test passes.
- [ ] Create `cloud/apps/api/src/ratelimit/policy.ts`. Done when: boundary tests pass for all six policies.
- [ ] Create the `limit` middleware. Done when: the 429 body and `Retry-After` are asserted.
- [ ] Add the storage-inspection test. Done when: it fails if a raw email is stored. Cheaper substitute: an in-memory `Map` in the Worker, which resets on every isolate.
- [ ] Commit: `cloud/api: Durable Object rate limits for sign-in, redemption, unlock and reports`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/ratelimit` exits 0; changing `signin-email` to 6 per hour makes the boundary test fail.

## 9. Email Queue consumer, SMTP2GO transport and dev outbox

Email must never block a request, so routes enqueue a typed message and a Queue consumer sends it. The transport is an interface: SMTP2GO in staging and production, an in-memory outbox in tests and local development, so every sign-in flow is testable without a mailbox. Templates are plain text first, with a small HTML part, and carry only a short-lived link, never a document.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Define `EmailMessage` types in `cloud/packages/shared/src/email.ts`: `kind` (`signin-link`, `email-change-verify`, `email-change-notice`, `passkey-added`, `export-ready`, `deletion-scheduled`, `deletion-cancelled`, plus kinds that T03 and T04 add), `to`, `locale`, `data` (typed per kind). Done when: `tsc -b` rejects a message missing required data.
2. Implement `cloud/apps/api/src/email/templates.ts`: subject, text and HTML per kind, escaping every interpolated value, links built only from `PUBLIC_ORIGIN`. Done when: a test with `<script>` in a display name shows it escaped and a link to another origin cannot be produced.
3. Implement `cloud/apps/api/src/email/transport.ts`: `Smtp2goTransport` posting to the SMTP2GO HTTPS send API at `SMTP2GO_API_URL` (default the vendor URL) with `SMTP2GO_API_KEY` and sender `EMAIL_FROM`; `MemoryTransport` for `ENVIRONMENT` of `test` or `dev`. A 429 (throttling) retries with exponential backoff and honours `Retry-After` when present, a 5xx or network error retries, and any other 4xx (rejected address, bad key, bad request) is a permanent failure. Done when: tests against a local fake SMTP2GO server cover success, 429 with and without `Retry-After` (retry), 5xx (retry), and 400, 401 and 422 (no retry).
4. Implement the Queue consumer `queue()` handler in `cloud/apps/api/src/email/consumer.ts` with retries (`max_retries` in `wrangler.jsonc`), a dead-letter queue, and an `email_log` row (kind, recipient hash, outcome, attempt; no body). Done when: a permanent failure lands in the dead-letter queue, an exhausted retry budget on 429 does too, and neither logs a recipient address.
5. Add `GET /__dev/outbox` and `DELETE /__dev/outbox` served only when `ENVIRONMENT` is `test` or `dev`. Done when: the same request in staging configuration returns 404.

- [ ] Create `cloud/packages/shared/src/email.ts`. Done when: the type test passes.
- [ ] Create `cloud/apps/api/src/email/templates.ts`. Done when: `test/email/templates.test.ts` passes the escaping and origin tests.
- [ ] Create `cloud/apps/api/src/email/transport.ts` and `cloud/selfhost/fake-smtp2go.mjs`. Done when: the success, throttling, server-error and permanent-failure cases pass.
- [ ] Create `cloud/apps/api/src/email/consumer.ts` and the `email_log` migration `cloud/packages/db/migrations/0002_email_log.sql`. Done when: retry and dead-letter tests pass.
- [ ] Add the dev outbox routes. Done when: staging configuration returns 404. Cheaper substitute: an outbox route enabled by a query flag.
- [ ] Commit: `cloud/api: email queue consumer with SMTP2GO and a dev outbox`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/email` exits 0; making the consumer retry on a 422 makes the permanent-failure test fail, and treating a 429 as permanent makes the throttling test fail.

## 10. Sign-up and sign-in with one-time email links

Account holders sign in with a passkey or a one-time email link; this section is the email path and also the verified-email step that creates an account. The token is single use, short lived and stored hashed. Mail scanners often open links, so opening the link only shows a confirmation page; a `POST` consumes the token.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `POST /api/auth/email/start` with `{email}`: always answers 202 with the same body whether or not the account exists (no enumeration), enforces `signin-email` limits, stores a hashed 256-bit token (15 minutes, single use) in `login_tokens`, and enqueues `signin-link`. Done when: responses for known and unknown emails are byte-identical apart from the request id, and timing differs by less than the test's tolerance because both paths enqueue.
2. `GET /auth/email/confirm#token=...` is a static page route carrying the token in the URL fragment (never sent to servers or logs); the page posts `{token}` to `POST /api/auth/email/redeem`, which consumes the token, creates the user on first use (recording terms acceptance version from the request), starts a session and records `auth.email.redeemed`. Done when: a second redeem of the same token returns 400 `invalid_token`, an expired token the same, and a token for a suspended user signs nothing in.
3. First-time sign-up requires `acceptTerms: true` and a `termsVersion` equal to the configured version; the text itself is the owner's legal copy (section 21). Done when: sign-up without acceptance returns 400.
4. Same-device guard: the start request carries `crossDevice` (default false), which the server stores on the `login_tokens` row; the start response sets a short-lived `__Host-recto_login` cookie bound to that row. Redeem requires the cookie unless the stored row says `crossDevice`, and ignores any `crossDevice` or similar field in the redeem payload. Redeem is also a login-CSRF target (an attacker signing a victim into the attacker's account), so the CSRF origin check of §7 applies to every auth `POST` including anonymous ones, and the confirm page always shows the address being signed in to. Done when: a redeem with the cookie missing fails when the stored flag is false even if the payload says otherwise, succeeds when the stored flag is true, and a cross-origin redeem `POST` returns 403.

- [ ] Create `cloud/apps/api/src/auth/email-link.ts` with `start` and `redeem`. Done when: `test/auth/email-link.test.ts` covers single use, expiry, enumeration equality and suspended users.
- [ ] Add the routes and the `limit` policies. Done when: the 6th start request for one email returns 429.
- [ ] Add the terms acceptance fields to the redeem route and `users`. Done when: the stored `terms_version` equals the posted one.
- [ ] Add the login-cookie binding. Done when: the cross-device test passes both ways.
- [ ] Record `auth.email.start` and `auth.email.redeemed` events without the address. Done when: the events table has no `@` character.
- [ ] Commit: `cloud/api: sign-up and sign-in with single-use email links`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/auth/email-link.test.ts` exits 0; changing redeem to leave `consumed_at` null makes the single-use test fail.

## 11. Passkey registration and sign-in (WebAuthn)

Passkeys are the primary sign-in. The server verifies registration and assertion with a maintained WebAuthn library that runs on Web Crypto (SimpleWebAuthn server, MIT, subject to the licence check in §1; replace it with an in-repo verifier only if it fails the check or does not run in workerd). Challenges are single use and expire in five minutes.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `POST /api/auth/passkey/register/options` (signed-in user, recent auth) and `.../verify`: stores `credential_id`, `public_key`, `sign_count`, `transports`, a user-chosen label. Requires user verification and resident (discoverable) credentials. `rp.id` is `RP_ID`; the expected origin is `PUBLIC_ORIGIN`. Done when: a test using a software authenticator fixture registers a credential and rejects a wrong origin, a wrong `rp.id` hash, a replayed challenge and a credential id already registered to another user.
2. `POST /api/auth/passkey/signin/options` and `.../verify`: usernameless discoverable flow. A sign count that does not increase (when the authenticator reports counters) rejects the attempt and records `auth.passkey.clone_suspected`. Done when: tests cover success, wrong signature, replayed challenge, suspended user and a regressing counter.
3. Send the `passkey-added` email on registration and record events. Done when: the outbox holds one message per registration.
4. Limit attempts with `passkey-attempt` and expire challenges by Cron (`scheduled`) cleanup. Done when: the 11th attempt in the window returns 429 and expired challenges are removed.
5. Prove the browser path in Playwright with the Chromium virtual authenticator (`WebAuthn.addVirtualAuthenticator` over CDP): `cloud/apps/web/e2e/passkey.spec.ts`. Done when: register then sign out then sign in with the passkey passes against the local Worker (this spec is completed with the UI in T02 §13; here it drives the API through `page.request`).

- [ ] Create `cloud/apps/api/src/auth/passkeys.ts` with options and verify functions. Done when: `test/auth/passkeys.test.ts` passes all rejection cases.
- [ ] Add routes, limits and the `webauthn_challenges` cleanup in `scheduled`. Done when: the cleanup test removes only expired rows.
- [ ] Add the `passkey-added` email and events. Done when: the outbox and events tests pass.
- [ ] Create `cloud/apps/web/e2e/passkey.spec.ts` using the virtual authenticator. Done when: the API-level register and sign-in cycle passes. Cheaper substitute: asserting only that the options endpoint returns JSON.
- [ ] Commit: `cloud/api: passkey registration and sign-in with WebAuthn`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/auth/passkeys.test.ts` exits 0; accepting an assertion whose challenge was already consumed makes the replay test fail.

## 12. Account recovery and email change

Because sign-in is passkey or email link, the verified email is the recovery root, and losing a passkey never locks anyone out while they control their inbox. Recovery codes cover the case where the inbox is also unavailable. Changing the email is the dangerous operation: it needs the new address verified and warns the old one.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Recovery by email link is the sign-in link of §10 followed by an offer to register a new passkey; add `GET /api/auth/recovery/status` so the web app knows whether the user has no passkeys. Done when: a user with a deleted passkey signs in by email and can register another.
2. Recovery codes: `POST /api/account/recovery-codes` (recent auth) generates 10 single-use codes of 80 bits, shown once, stored as hashes; `POST /api/auth/recovery-code/redeem` signs in with one, consumes it, rate-limited, and emails the owner. Add migration `cloud/packages/db/migrations/0003_recovery.sql`. Done when: a code works once, regenerating invalidates the old set, and the plain codes appear in no table.
3. Email change: `POST /api/account/email/change` (recent auth) sends a verification link to the new address and a notice with a cancel link to the old address; the change takes effect when the new address is verified, the old address can cancel for 24 hours after, and all other sessions are revoked on change. Done when: tests cover verify, cancel, a new address already in use (generic answer, no enumeration) and session revocation.
4. Record `account.recovery.*` and `account.email.*` events. Done when: events carry no addresses.

- [ ] Create `cloud/apps/api/src/auth/recovery.ts` and `cloud/packages/db/migrations/0003_recovery.sql`. Done when: `test/auth/recovery.test.ts` passes the single-use and regeneration cases.
- [ ] Create `cloud/apps/api/src/account/email-change.ts`. Done when: `test/account/email-change.test.ts` passes verify, cancel and conflict cases.
- [ ] Add routes with recent-auth and rate limits. Done when: a request older than the recent-auth window returns 401 `reauth_required`.
- [ ] Add the two email kinds' templates. Done when: both appear in the outbox with only links.
- [ ] Commit: `cloud/api: recovery codes, email recovery and verified email change`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/auth/recovery.test.ts test/account/email-change.test.ts` exits 0; leaving other sessions alive after an email change makes the revocation test fail.

## 13. Account settings API: profile, sessions and passkeys

People must be able to see and end their own access. This API is what the web app's account page (T02 §13) shows, and it is the reverse path for a lost device: revoke one session or one passkey without deleting the account.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `GET /api/account` returns id, email, display name, role, created date, passkey count and terms version; `PATCH /api/account` changes the display name (1 to 80 characters, no control characters). Done when: a test rejects a 200-character name.
2. `GET /api/account/sessions` lists the caller's sessions with created, last seen and a coarse user-agent class (never the raw string or IP); `DELETE /api/account/sessions/:id` revokes one; `POST /api/account/sessions/revoke-others`. Done when: revoking another user's session id returns 404.
3. `GET /api/account/passkeys`, `PATCH /api/account/passkeys/:id` (label) and `DELETE /api/account/passkeys/:id` (recent auth; refuses removing the last sign-in method unless recovery codes exist). Done when: deleting the only passkey with no recovery codes is refused with `last_credential`, because email-link sign-in still exists but the user must acknowledge it with `confirmEmailOnly: true`.

- [ ] Create `cloud/apps/api/src/account/profile.ts` and routes. Done when: `test/account/profile.test.ts` passes validation cases.
- [ ] Create `cloud/apps/api/src/account/sessions.ts`. Done when: the cross-user revoke returns 404.
- [ ] Create `cloud/apps/api/src/account/passkeys.ts`. Done when: the last-credential guard test passes.
- [ ] Commit: `cloud/api: account profile, session, passkey and grant management`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/account` exits 0; removing the owner check on session revocation makes the cross-user test fail.

## 14. OAuth 2.0 authorization code with PKCE endpoints for the desktop app

The Windows app (T05) signs in through the system browser and a loopback redirect, so no password or cookie ever reaches the desktop. The server implements one public client, the Recto desktop app, per RFC 8252 and RFC 7636: S256 PKCE only, single-use short-lived codes, short-lived access tokens, rotating refresh tokens with reuse detection. Scopes decide what the token may do; later TODOs check them.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Client registry `cloud/apps/api/src/oauth/clients.ts`: one built-in public client `recto-desktop` with allowed redirect URIs `http://127.0.0.1:<any port>/callback` and `http://[::1]:<any port>/callback` (loopback, exact path), scopes `documents`, `reviews`, `signatures`; and a second built-in public client `recto-admin-cli` with the same loopback rules and the single scope `admin`, which is never offered to `recto-desktop` and for which the token endpoint issues no refresh token. Done when: a redirect to `http://localhost/...`, a non-loopback host or a different path is rejected, and `recto-desktop` requesting `admin` is refused.
2. `GET /oauth/authorize` (browser): validates `client_id`, `redirect_uri`, `response_type=code`, `code_challenge` with `code_challenge_method=S256` (plain is refused), `state` (required), `scope`; if not signed in it sends the user through sign-in and back; then shows a consent page listing the app name and scopes with Allow and Deny; `POST /oauth/authorize/decision` (CSRF protected) issues a 60-second single-use code bound to the challenge, client, redirect and scopes. Done when: tests cover each rejection and a code reused or used after 60 seconds.
3. `POST /oauth/token`: `grant_type=authorization_code` with `code_verifier` checked against the stored challenge (constant-time), returns `access_token` (15-minute opaque or signed token, section 7 verifier), `refresh_token` (rotating, 30-day sliding, 180-day absolute), `expires_in`, `scope`. `grant_type=refresh_token` rotates; presenting an already-rotated refresh token revokes the whole token family and records `oauth.refresh.reuse`. Done when: the reuse test revokes the family and the next legitimate token fails.
4. `POST /oauth/revoke` (RFC 7009) and `GET /.well-known/oauth-authorization-server` (RFC 8414 metadata). Done when: revoked tokens fail on the next API call and the metadata lists only S256.
5. Bearer tokens plug into the `Principal` verifier of §7 with scopes; a route declares `requireScope('documents')`. Done when: a token without the scope gets 403 `insufficient_scope`.
6. The consent page markup lives in `cloud/apps/web` (T02 §13); `GET /oauth/authorize` for a signed-in user redirects to `/oauth/consent` with only a server-issued `request_id` in the query, and `GET /api/oauth/request/:id` returns the validated client name and scopes to that page. Done when: the redirect carries no `code_challenge` or `state` in its URL and the request endpoint returns 404 for another user's request.
7. `GET /api/account/grants` lists the caller's authorized apps and `DELETE /api/account/grants/:id` revokes that refresh token family. Done when: after revocation the refresh token is rejected.

- [ ] Create `cloud/apps/api/src/oauth/clients.ts`. Done when: redirect validation tests pass for IPv4 loopback, IPv6 loopback, wrong host, wrong path and userinfo tricks.
- [ ] Create `cloud/apps/api/src/oauth/authorize.ts`. Done when: `test/oauth/authorize.test.ts` passes all rejections.
- [ ] Create `cloud/apps/api/src/oauth/token.ts` with rotation and reuse detection. Done when: `test/oauth/token.test.ts` passes the family-revocation case. Cheaper substitute: long-lived non-rotating refresh tokens.
- [ ] Create `cloud/apps/api/src/oauth/revoke.ts` and the metadata route. Done when: the revocation and metadata tests pass.
- [ ] Add `requireScope` and the bearer verifier. Done when: scope tests pass.
- [ ] Create `cloud/apps/api/src/oauth/grants.ts` with the grant list and revoke routes and the consent request endpoint. Done when: `test/oauth/grants.test.ts` passes the revocation and cross-user cases.
- [ ] Commit: `cloud/api: OAuth authorization code with PKCE for the desktop app`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/oauth` exits 0; accepting `code_challenge_method=plain` makes the S256-only test fail.

## 15. Data export and account deletion with a grace period

People own their data. Export packages everything the account holds into a ZIP they download once; deletion removes the account and its files after a recorded grace period and can be cancelled until then. Completed signature envelopes that other parties hold rights to are not deleted by account deletion; they follow the retention policy (T04), so purge consults a hold registry that T04 fills.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `cloud/packages/shared/src/zip.ts`: a streaming ZIP writer (store and deflate through the `CompressionStream` API, ZIP64 for files over 4 GiB), no third-party dependency. Done when: the output opens with a reference unzip in the test (`node:zlib` based verifier) for empty, large and unicode-named entries.
2. `POST /api/account/export` (recent auth, `export` rate limit) enqueues a job; the job writes a ZIP to `exports/<userId>/<id>.zip` in `BLOBS` containing `account.json`, `security-events.json`, `documents.json` (metadata, folders, shares) and every document version file, then emails a one-time download link valid 7 days. Rows owned by features added later register an exporter through `registerExporter(name, fn)`. Done when: the test unzips an export of a seeded account and finds every seeded record and byte-identical files, and the expiry job deletes the ZIP after 7 days.
3. `POST /api/account/delete` (recent auth) marks `pending_deletion`, revokes other sessions, sends `deletion-scheduled` with a cancel link, and sets `purge_after` to now plus the grace period (default 14 days, in `config.ts`). Signing in or following the link within the grace period calls `POST /api/account/delete/cancel`. Done when: tests cover schedule, cancel and a sign-in during grace showing the pending state.
4. A Cron purge job, for accounts past `purge_after`, skips any row for which a registered `RetentionHold` returns true, deletes the account's rows and its personal objects (`exports/<userId>/`), and records `account.purged` with counts only. It never deletes a content-addressed `blobs/<sha256>` object itself: it hands the hashes of the removed versions to the reference-aware blob collector (D09 T02 §6), which deletes a blob only when no version of any account references it. Done when: a seeded account purges to zero rows and no personal objects except a held envelope fixture, a blob also referenced by a second account's version survives with that account's version still readable, and a failed object delete leaves the account pending for retry.
5. Deletion is recorded as a tombstone row in `deleted_accounts` (opaque user id and purge date only, no email) kept for 14 months, so restoring an older backup can re-apply the deletion (§17). Export ZIPs expire: the Cron job deletes `exports/*` objects past their 7-day link life, and account deletion removes the rest. Done when: a restore of a dump taken before the purge removes the purged account again, and an expired export object is deleted.

- [ ] Create `cloud/packages/shared/src/zip.ts`. Done when: `cloud/packages/shared/test/zip.test.ts` verifies with an independent reader.
- [ ] Create `cloud/apps/api/src/account/export.ts` with `registerExporter`. Done when: `test/account/export.test.ts` passes the byte-identical check.
- [ ] Create `cloud/apps/api/src/account/delete.ts` and the Cron purge. Done when: `test/account/delete.test.ts` passes schedule, cancel, purge and hold cases.
- [ ] Add `RetentionHold` registry `cloud/apps/api/src/account/holds.ts`. Done when: a registered hold prevents the purge of the matching fixture.
- [ ] Add `export-ready` and `deletion-*` emails. Done when: outbox assertions pass.
- [ ] Commit: `cloud/api: account data export and deletion with a recorded grace period`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/account/export.test.ts test/account/delete.test.ts && npm --prefix cloud run test:pkg -- shared` exits 0; skipping the hold check in purge makes the hold test fail, and deleting a shared blob directly makes the two-account test fail.

## 16. Admin API, abuse reports and takedown by content hash

A public file service gets abused, and the operator needs a way to respond that is itself accountable. Admin routes are API-only plus a CLI; every admin action is a `security_events` row. Takedown works on a content hash so a removed file cannot be re-uploaded or re-shared by a different account.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Admin role: `users.role='admin'` granted only by `cloud/scripts/admin.mjs grant <email>` (runs SQL against the target D1 or SQLite through the db interface with the operator's own database credentials, records an event). Admin routes under `/api/admin/*` require either a browser admin session with passkey authentication in the last 5 minutes, or a bearer token carrying the `admin` scope. The `admin` scope is issued only to the built-in public client `recto-admin-cli` (§14), only to a user whose role is `admin` and whose approving browser session used a passkey in the last 5 minutes, with a 15-minute access token and no refresh token. Done when: a non-admin gets 403, an email-link-only admin session gets 401 `reauth_required`, and an `admin`-scoped token for a user demoted since issue is rejected.
2. `GET /api/admin/accounts?query=&status=&cursor=` (paged, email searched by exact match or prefix of a hash column, never full-table dumps beyond the page), `POST /api/admin/accounts/:id/suspend` and `/unsuspend` (suspension revokes sessions and tokens and disables share links the account owns). Done when: tests cover paging, suspension effects and the audit rows.
3. `POST /api/reports` (anonymous allowed, `report` rate limit): `{shareToken?|documentId?, reason: 'malware'|'illegal'|'copyright'|'privacy'|'other', contactEmail?, note (max 1000)}` stored in `abuse_reports`. `GET /api/admin/reports?status=open`, `POST /api/admin/reports/:id/resolve` with action `dismiss`, `disable-link`, `takedown`. Done when: each action changes state and is audited.
4. Takedown inserts the SHA-256 into `blocked_hashes`; uploads of a blocked hash fail with 451 `blocked_content` (the check is called by T02 §4), existing versions with that hash become undownloadable (T02 §11 reads `blocked_hashes`), and share links to them are disabled. Done when: after takedown a seeded version returns 451 on read and a new upload of the same bytes is refused.
5. `GET /api/admin/summary` returns counts only (accounts by status, open reports, storage bytes, uploads in the last 24 hours) for the reporting need. Done when: the response contains no email or file name.
6. `cloud/scripts/admin.mjs` wraps the API (`login`, `suspend`, `reports`, `takedown <sha256>`): `login` runs the authorization code with PKCE loopback flow of §14 for `recto-admin-cli` (the admin approves in the browser with a passkey) and keeps the token in memory for the command; it never reads a long-lived token from an environment variable or a file. The `grant` subcommand of step 1 is the one exception: it runs against the database directly with the operator's own database credentials. Done when: `node cloud/scripts/admin.mjs --help` lists commands, and a test drives `login` against the local Worker with a virtual authenticator and then runs a dry-run `suspend`.

- [ ] Create `cloud/apps/api/src/admin/accounts.ts`. Done when: `test/admin/accounts.test.ts` passes.
- [ ] Create `cloud/apps/api/src/admin/reports.ts`. Done when: `test/admin/reports.test.ts` passes every action.
- [ ] Create `cloud/apps/api/src/admin/takedown.ts` exporting `isBlocked(db, sha256)`. Done when: the 451 test passes.
- [ ] Create `cloud/apps/api/src/admin/summary.ts`. Done when: the no-personal-data test passes.
- [ ] Create `cloud/scripts/admin.mjs`. Done when: the dry-run test passes. Cheaper substitute: direct SQL edits with no audit row.
- [ ] Commit: `cloud/api: admin accounts, abuse reports and takedown by content hash`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/admin` exits 0; skipping the audit write in suspension makes the audit assertion fail.

## 17. Backups and restore

A backup nobody has restored is not a backup. The service writes a daily logical dump of the database to the backup bucket through the storage interface, and a restore script loads a dump into an empty database and checks it. Blob objects are content-addressed and immutable, so mirroring them to a second store is a copy of missing keys.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `cloud/apps/api/src/backup/dump.ts`: reads every table in `sqlite_master` order through `SqlDatabase` as one consistent snapshot, writes newline-delimited JSON (`{table, row}`) with a header carrying the schema migration level and a trailing SHA-256, in pages so memory stays bounded; a `scheduled` Cron entry runs it daily and writes `db/<yyyy-mm-dd>.ndjson.gz` to `BACKUPS`. Consistency: paged reads on D1 are separate calls, so the dump takes a write fence first. Every mutating `batch` in the application goes through one `writeBatch` helper in `cloud/packages/db/src/write.ts` that appends a guard statement failing (NOT NULL violation, rolling back the whole batch) while a non-expired `backup_fence` row exists, and the caller answers 503 `try_again` with `Retry-After`. The fence is held only for the read (maximum 60 seconds, expiry stored in the row so a crashed dump cannot hold it), and the Durable Object SQLite adapter reads inside one synchronous `transactionSync` instead. Retention: keep 30 daily and 12 monthly dumps, so the oldest dump is under 13 months old (defaults in `config.ts`). Done when: a seeded database dumps and the trailer hash verifies, and a concurrency test running commits, purges and a dump together produces a dump whose restore passes the integrity check, with no version missing its blob row and no quota total that differs from `recompute`.
2. `cloud/scripts/restore.mjs <dump>`: refuses a non-empty target, applies migrations to the dump's level, loads rows in dependency order, re-applies every `deleted_accounts` tombstone (from the dump and from `tombstones.json` in `BACKUPS`, §15) so purged accounts stay deleted, then runs integrity checks (`PRAGMA foreign_key_check`, row counts equal to the header, every version's blob present in `BLOBS` or the mirror, with missing blobs reported and the versions marked `missing_blob` rather than silently kept). Done when: the restore drill test restores a seeded dump into an empty database and every table's rows are equal, a dump taken before an account purge restores without that account, and a version whose blob was purged is reported as `missing_blob`.
3. `cloud/apps/api/src/backup/mirror.ts`: makes a second `BlobStore` configured as `MIRROR_*` a replica of `BLOBS`: it copies keys missing in the mirror, verifying sizes, and deletes mirror keys absent from `BLOBS` for more than 24 hours, so deletions propagate. Each run also writes the tombstone list to `BACKUPS` as `tombstones.json`. Done when: deleting a mirrored object and re-running restores it, and a blob deleted from `BLOBS` disappears from the mirror after 24 hours.
4. Failure handling: a failed dump records `backup.failed` and emails the operator address `OPERATOR_EMAIL` once per day. Done when: the failure-injection test sends one email and a second failure the same day sends none.

- [ ] Create `cloud/apps/api/src/backup/dump.ts` and `cloud/packages/db/src/write.ts` with the `backup_fence` guard. Done when: `test/backup/dump.test.ts` verifies the trailer hash and the retention pruning, and `test/backup/consistency.test.ts` passes the concurrent commit, purge and dump run. Cheaper substitute: paging each table without a fence.
- [ ] Create `cloud/scripts/restore.mjs`. Done when: `test/backup/restore.test.ts` restores a seeded dump and fails on a corrupted dump.
- [ ] Create `cloud/apps/api/src/backup/mirror.ts`. Done when: the mirror test passes. Cheaper substitute: relying on the provider's bucket durability alone.
- [ ] Add the Cron entries to `wrangler.jsonc`. Done when: `wrangler deploy --dry-run` lists the schedules.
- [ ] Commit: `cloud/api: daily database dumps, blob mirroring and a tested restore`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/backup` exits 0; flipping one byte in a dump makes the restore test report a hash mismatch and exit non-zero.

## 18. Self-host adapters: Durable Object SQLite, job queue and scheduler

The plain `workerd` runtime has no D1 service, no Cloudflare Queues and no Cron Triggers, but it does run Durable Objects with SQLite storage and alarms. These adapters implement the same interfaces so the application code is identical. Verify the pinned `workerd` release supports SQLite-backed Durable Objects before relying on this; if it does not, record a dated correction in the foundation plan and use a Node adapter instead.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `cloud/packages/db/src/do-sqlite.ts`: `SqlHost` Durable Object exposing `query`, `run` and `batch` over `ctx.storage.sql` with `transactionSync` for `batch`, and `DurableObjectSqlDatabase` implementing `SqlDatabase`. Done when: the whole `test/db` suite passes against it by parameterising the factory.
2. Schema parity test `cloud/apps/api/test/db/parity.test.ts`: apply all migrations to D1 and to `SqlHost` and compare normalised `sqlite_master`. Done when: the two schemas match, and an adapter-specific column makes the test fail.
3. `cloud/apps/api/src/jobs/queue.ts`: `JobQueue` interface with `send(message)`, `CloudflareQueue` adapter and `DurableObjectQueue` adapter (messages in SQLite, delivery with retry and backoff through alarms, dead-letter table). Done when: the email consumer tests run unchanged against both.
4. `cloud/apps/api/src/jobs/scheduler.ts`: one `scheduled(event)` dispatcher plus a `DurableObjectScheduler` that fires the same handlers on the cron expressions from config via alarms. Done when: a test with an injected clock fires the daily and hourly jobs once each.

- [ ] Create `cloud/packages/db/src/do-sqlite.ts`. Done when: the parameterised `test/db` suite passes on it.
- [ ] Create `cloud/apps/api/test/db/parity.test.ts`. Done when: it compares both schemas after all migrations.
- [ ] Create `cloud/apps/api/src/jobs/queue.ts` and move the email producer behind it. Done when: `test/email` passes on both queue adapters.
- [ ] Create `cloud/apps/api/src/jobs/scheduler.ts`. Done when: the injected-clock test passes. Cheaper substitute: an external system cron calling an unauthenticated URL.
- [ ] Commit: `cloud: self-host adapters for SQLite, the job queue and the scheduler`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/db test/jobs` exits 0; adding a column in a D1-only migration makes the parity test fail.

## 19. Self-host build on plain workerd with a smoke test

Self-hosting is a promise that the same code runs outside Cloudflare, so it is tested, not documented. The build bundles the Worker and the web assets, a `workerd` configuration wires the Durable Object SQLite database, the S3-compatible store and a directory of static assets, and a smoke script proves a real sign-in and a blob round trip.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. `cloud/selfhost/build.mjs` bundles `apps/api` (esbuild, MIT) into `cloud/selfhost/dist/worker.js` and copies `apps/web/dist`. Done when: the bundle contains no `import` of a `cloudflare:` module that `workerd` lacks.
2. `cloud/selfhost/workerd.capnp` defines the worker, Durable Object namespaces (`SqlHost`, `RateLimiter`, `DurableObjectQueue`, `DurableObjectScheduler`, and the classes later TODOs add) with `uniqueKey`, a disk directory for assets, and environment bindings from files or variables (`STORAGE_DRIVER=s3`, `S3_ENDPOINT`, `S3_BUCKET`, keys, `PUBLIC_ORIGIN`, `RP_ID`). Done when: `npx workerd serve --verbose cloud/selfhost/workerd.capnp` starts and `GET /api/health` answers.
3. `cloud/selfhost/smoke.mjs`: starts `fake-s3.mjs` and `fake-smtp2go.mjs`, starts `workerd`, runs migrations through an admin task, signs in with an email link read from the outbox, stores and reads a blob through the S3 adapter, creates a rate-limit rejection, and exits non-zero on any failure with the failing step named. Done when: `npm --prefix cloud run smoke:selfhost` exits 0 on a clean checkout.
4. `cloud/selfhost/README.md` is not written; instead `cloud/README.md` gets a "Self-host" paragraph listing the exact command, the required variables and the limits (single Durable Object SQLite database per deployment, no horizontal write scaling). Done when: `check-readme-scripts.mjs` still passes.

- [ ] Create `cloud/selfhost/build.mjs`. Done when: the bundle runs under `workerd` with no missing module error.
- [ ] Create `cloud/selfhost/workerd.capnp`. Done when: `workerd serve` starts and answers `/api/health`.
- [ ] Create `cloud/selfhost/smoke.mjs`. Done when: the happy path exits 0 and a deliberately broken S3 endpoint exits 1 naming the storage step. Cheaper substitute: starting `workerd` and checking only `/api/health`.
- [ ] Add the Self-host paragraph to `cloud/README.md`. Done when: it names the command, variables and limits.
- [ ] Commit: `cloud: self-host build on plain workerd with a smoke test`

**Test checkpoint:** `npm --prefix cloud run smoke:selfhost` exits 0; pointing `S3_ENDPOINT` at a closed port makes it exit 1 with the storage step named.

## 20. GitHub Actions CI: lint, typecheck, Vitest, Playwright and self-host smoke

Every pull request that touches `cloud/` must prove the whole workspace, and a pull request that touches only the desktop must not pay for it. The deploy job is separate (section 21) and never runs on pull requests from forks.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Create `.github/workflows/cloud-ci.yml` triggered on `pull_request` and `push` to `main` with `paths: ['cloud/**', '.github/workflows/cloud-ci.yml']`. Jobs: `static` (install with `npm ci`, `lint`, `typecheck`, `licenses`, `types:check`, migration lint), `unit` (`test`, which runs the Workers pool and package tests, with a MinIO service container and `S3_TEST_ENDPOINT` set so the storage contract suite runs against it), `e2e` (Playwright with browsers installed and the report uploaded on failure), `selfhost` (`smoke:selfhost`). Pin actions by full commit SHA, the way `build.yml` does. Done when: the workflow passes `actionlint` and a draft pull request shows all four jobs green.
2. Fail the build if the MinIO-backed contract tests were skipped. Done when: unsetting `S3_TEST_ENDPOINT` in a test run makes the guard script `cloud/scripts/assert-no-skips.mjs` exit 1.
3. Cache npm and the Playwright browsers keyed on `cloud/package-lock.json`. Done when: a second run restores both caches.
4. Use read-only `GITHUB_TOKEN` permissions and no repository secrets in these jobs. Done when: the workflow file declares `permissions: contents: read` at the top.

- [ ] Create `.github/workflows/cloud-ci.yml`. Done when: `actionlint .github/workflows/cloud-ci.yml` exits 0. Cheaper substitute: adding cloud steps to `build.yml`, which would run them on desktop-only changes.
- [ ] Create `cloud/scripts/assert-no-skips.mjs`. Done when: it fails on a Vitest JSON report containing a skipped test in `test/storage`.
- [ ] Add `cloud/playwright.config.ts` starting the local Worker with `webServer` and the `ENVIRONMENT=test` outbox. Done when: `npm --prefix cloud run e2e -- --list` lists the specs.
- [ ] Verify path filters. Done when: a pull request touching only `src/` does not start `cloud-ci`, observed in the Actions run list.
- [ ] Commit: `ci: cloud workflow with lint, tests, end-to-end and self-host smoke`

**Test checkpoint:** `actionlint .github/workflows/cloud-ci.yml && npm --prefix cloud run lint && npm --prefix cloud test` exits 0; a failing test in `cloud/apps/api/test` turns the `unit` job red in the Actions run.

## 21. Staging and production deploy job gated on operator secrets

Deployment is the first step that needs the owner's Cloudflare account and decisions, so it is the one section in this file that waits. The workflow is written and tested to refuse safely without secrets; the owner then supplies the domain, sender domain, data region and secrets, and the job deploys migrations and the Worker.

**Phase:** 98
**Needs:** operator, owner-decision, third-party
**Surface:** no

**Build order:**

1. Record the owner decisions in `cloud/apps/api/wrangler.jsonc` production block: public domain (`PUBLIC_ORIGIN`, `RP_ID`, custom domain route), email sender (`EMAIL_FROM` with a domain verified in SMTP2GO, SPF, DKIM and DMARC records documented in `cloud/README.md`), data region (default storage, or the EU jurisdiction chosen when each store is created: R2 buckets with the `eu` jurisdiction, the D1 database created with a jurisdiction rather than only a location hint because Cloudflare documents a hint as no guarantee, and Durable Objects restricted by jurisdiction where the product supports it; none of these can be changed after creation, so confirm the choice first), and the final terms version. Done when: `wrangler deploy --dry-run --env production` passes with those values.
2. Create `.github/workflows/cloud-deploy.yml` triggered by `workflow_dispatch` and by a `v*` tag, using GitHub Environments `staging` and `production` (production requires a reviewer), `if: vars.CLOUD_DEPLOY_ENABLED == 'true'`, and a first step that fails with a clear message when `CLOUDFLARE_API_TOKEN` or `CLOUDFLARE_ACCOUNT_ID` is empty. Order: build, `wrangler d1 migrations apply` (staging first), `wrangler deploy`, then a post-deploy check that `/api/health` and the security-header test run against the new URL. Done when: a dispatch on a repository without secrets fails at the first step naming the missing secret and deploys nothing.
3. Set Worker secrets (`SMTP2GO_API_KEY`, `SESSION_PEPPER`, `OAUTH_SIGNING_KEY`) through `wrangler secret put` from the 1Password Conclave vault entries by the operator; no secret enters the repository, workflow logs or arguments. Done when: `wrangler secret list` shows the three names and `git grep` finds no value.
4. Create the R2 `BACKUPS` bucket and the mirror store in the same jurisdiction as the primary stores, run the restore drill of section 17 against a staging dump, and record the date in `cloud/README.md`. Add a "Data location" table to `cloud/README.md` listing, for each store and processor (D1, R2 `BLOBS`, R2 `BACKUPS`, mirror, Durable Objects, Queues, Worker execution, logs, SMTP2GO), the jurisdiction setting actually applied and verified in the Cloudflare dashboard or API, and which are not covered; the product makes no residency statement beyond that table. Done when: the drill output shows matching row counts and every table row cites the verified setting.

- [ ] Fill the production block of `cloud/apps/api/wrangler.jsonc` with the owner's domain, region and sender. Done when: the production dry run passes. Cheaper substitute: leaving placeholder hostnames in the committed file.
- [ ] Create `.github/workflows/cloud-deploy.yml`. Done when: `actionlint` passes and the secret-less dispatch fails safely.
- [ ] Provision secrets and buckets as the operator. Done when: the staging deploy succeeds and `/api/health` answers over HTTPS.
- [ ] Verify staging email: request a sign-in link to an owner address. Done when: the mail arrives from the verified sender and the link signs in.
- [ ] Run the restore drill on staging and record it. Done when: `cloud/README.md` lists the drill date and row counts.
- [ ] Commit: `cloud: staging and production deploy workflow gated on operator secrets`

**Test checkpoint:** `actionlint .github/workflows/cloud-deploy.yml` exits 0, and a `workflow_dispatch` run on a repository without `CLOUDFLARE_API_TOKEN` fails at the secrets step and shows no deploy step run.

## Verification

- A clean checkout runs `npm --prefix cloud ci`, `lint`, `typecheck`, `licenses`, `test`, `e2e` and `smoke:selfhost` to exit 0, with the CI run quoted in the stamp.
- A user signs up by email link, registers a passkey, signs in with it, revokes a session, exports data and schedules deletion, shown by the Playwright run and the API tests.
- The desktop OAuth loopback flow is exercised by `test/oauth` end to end, including refresh reuse revoking the family.
- A seeded backup restores into an empty database and the integrity check passes; a corrupted dump is refused.
- Sections 1 to 20 need no owner input; section 21 stays open until the owner records the domain, region, sender and terms version.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
