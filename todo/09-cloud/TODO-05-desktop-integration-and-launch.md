---
schema_version: 1
id: cloud-desktop-launch
domain: 09-cloud
status: active
title: "TODO-05 -- Recto Cloud Desktop Integration and Launch"
---

# TODO-05 -- Recto Cloud Desktop Integration and Launch

> **Goal:** A Recto user who never signs in sees no change and no network traffic, and a user who signs in to Recto Cloud through their browser can open and save documents online with version-conflict protection, send the open document for signature (after seeing exactly what will be uploaded) and continue in the web field-placement page, start a shared review, and watch the reviewers' comments appear live in the existing Comments panel, with tokens held only in Windows Credential Manager and every failure explained in plain language. The launch half of the file turns the finished service into a public product: domain and region decisions recorded, a seal certificate from an AATL CA in an HSM or signing service, counsel-reviewed terms, privacy policy, ESIGN disclosure and DPA, a production deployment that monitors without ever seeing document content, a status page and abuse contact, and a go-live gate that sets the desktop's default server.

> **Current state:** Measured 2026-10-21. `src/services/` has no `cloud/` folder and the repository has no WinHTTP use; `src/app/` has no cloud code. The desktop sections this file builds on are plans: `SettingsStore` and `AppPaths` (`D02 T04 §1`, `§2`), the settings page groups (`D02 T04 §6`), `CommandRegistry` and the `Ctrl+K` box (`D02 T03 §11`, `§12`), `OpenDocumentPipeline` and `SavePipeline` (`D02 T01 §5`, `§8`), the `Comments / Annotations` panel and the annotation `JSON` model (`D03 T03 §5`, `§2`), and the assistant's WinHTTP transport and Credential Manager pattern (`D08 T01 §2`, `§9`). The cloud service itself (`cloud/`, domain `09-cloud` files `TODO-01` to `TODO-04`) is planned by other files and defines the HTTPS API this file calls. `docs/cloud-launch.md`, `scripts/check-cloud-launch.py` and the legal pages do not exist. Every section is Phase 98 and the launch sections wait on the open owner decisions in `docs/specs/2026-10-21-recto-cloud-design.md`.

## Inputs

- Foundation plan D20 (Recto Cloud, the opt-in rule), D16 (offline by default, no telemetry, local diagnostics), D19 (credentials in Windows Credential Manager, per-request consent), D9 (no PDF JavaScript), and `AGENTS.md` (the UI thread never blocks, every write is temp-file-then-replace, document and remote content is hostile, colours only from `design/tokens/RectoColors.xaml`, every command in `Ctrl+K`, icon-only controls carry an automation name and tooltip).
- `docs/specs/2026-10-21-recto-cloud-design.md`: the desktop integration paragraph, the sign-in decision (OAuth 2.0 authorization code with PKCE through a loopback redirect, tokens in Credential Manager), the open owner decisions, and the phasing note that launch work waits on them.
- `D02 T04 §1`, `§2`, `§6` (Portable mode paths, typed settings, settings page), `D02 T03 §3`, `§11`, `§12` (side panel host, command registry, command box), `D02 T01 §3`, `§5`, `§8`, `§10` (tab sessions, open pipeline, save pipeline, dialog chrome), `D03 T03 §2`, `§4`, `§5` (annotation `JSON`, undo, Comments panel), `D03 T02 §3`, `§5` (accessibility inventory, `AnnouncementService`), `D04 T02 §1` (`AtomicFileWriter`), `D05 T02 §9` (pending redaction marks), `D07 T02 §4` and `D07 T03 §2` (crash bundle and diagnostics, which must never carry a token), `D07 T01 §7` (Portable zero-registry proof), `D08 T01 §4`, `§9` (loopback TLS test server and credential backend, reused here), `D01 T01 §1` (`PdfResult<T>`), `D01 T02 §7` (`CancelToken`).
- -> XREF: D09 T01 §14 -- signs in through the OAuth 2.0 PKCE endpoints of D09 T01 §14.
- -> XREF: D09 T02 §3 -- uploads with `baseVersion` through the upload sessions of D09 T02 §3.
- -> XREF: D09 T02 §11 -- downloads through the read API of D09 T02 §11 and verifies the hash.
- -> XREF: D09 T03 §10 -- syncs live review comments through the exchange API of D09 T03 §10.
- -> XREF: D09 T04 §5 -- creates envelopes through the envelope API of D09 T04 §5.
- -> XREF: D08 T01 §2 -- the cloud transport follows the same HTTPS-only, no-redirect-credential rules.
- -> XREF: D07 T03 §2 -- Recto Cloud tokens never appear in diagnostics, and a scan proves it.
- -> XREF: D02 T04 §6 -- the `RECTO CLOUD` settings group joins the settings page and its search.
- -> XREF: D03 T03 §5 -- live review comments appear in the existing Comments panel.
- -> XREF: D09 T04 §27 -- in-person option.
- -> XREF: D09 T03 §5 -- protocol messages.
- -> XREF: D09 T03 §7 -- chunks, fixtures, schema.
- -> XREF: D09 T01 §14 -- scope assets.
- -> XREF: D09 T01 §4 -- BlobStore.
- -> XREF: D09 T01 §15 -- export and deletion of assets.
- -> XREF: D03 T03 §20 -- stamp library sync.
- -> XREF: D05 T02 §10 -- signature appearance library sync.
- -> XREF: D07 T02 §10 -- CloudDisabled policy.

## Outcome

- With no sign-in, Recto makes zero Recto Cloud network calls and creates no cloud transport object, and a test proves it; the cloud commands other than `Sign in to Recto Cloud` are absent from menus and `Ctrl+K`.
- Sign-in uses the system browser, PKCE `S256`, a single-use `127.0.0.1` loopback redirect with a `state` check, and stores only a refresh token and account label in Windows Credential Manager; sign-out revokes the token and deletes the credential even when the server is unreachable.
- `Open from Recto Cloud`, `Save to Recto Cloud`, `Send for signature` and `Start a shared review` each show exactly what will be uploaded and to which server before any byte leaves, and a version conflict never loses either side's work.
- Reviewers' comments reach the Comments panel through the annotation `JSON` model within seconds while the tab is open, survive disconnects with an in-order resend, and are validated as hostile input.
- Signature and initials appearances and custom stamps follow a signed-in user across their PCs only after an explicit opt-in, are encrypted per account at rest, and never include certificates, keys or passwords, with the service's reach documented honestly (§19, §20).
- The launch ledger `docs/cloud-launch.md` records, with evidence, the domain, region, seal certificate, legal documents, production deployment, monitoring, status page and abuse contact, and `scripts/check-cloud-launch.py` refuses go-live until every row is done.

**Adjacency:** list=applicable @ D09 T05 §7; document=applicable @ D09 T05 §8; settings=applicable @ D09 T05 §6; reporting=not-applicable (no telemetry, D16; usage numbers live in the service files); notifications=applicable @ D09 T05 §11; permissions=applicable @ D09 T05 §12; audit=applicable @ D09 T05 §5; exchange=applicable @ D09 T05 §11; reverse=applicable @ D09 T05 §6

**Adjacency rationale:** The list is the cloud document picker (§7). The document surface is the save-and-conflict flow (§8), with send-for-signature (§9) and shared review (§10) as the two ways a document leaves the machine. Settings are the `RECTO CLOUD` group and the server address (§6). The permission boundary is the consent gate plus the signed-out silence rule, both exercised both ways in §12. The in-memory upload ledger (operation, host, byte count, never content) is the desktop audit trail (§5); the service keeps its own hash-chained audit. Sync status chips and polite announcements are the notifications (§11), and annotation `JSON` in and out of a review is the exchange (§11). Sign-out with token revocation and discarding a draft envelope or closing a review are the reversals (§6, §9, §10). There are no reports because Recto keeps no telemetry.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | HTTPS-only WinHTTP cloud transport with no redirect credential leakage | D01 T02 §7, D08 T01 §4 | [ ] |
| 2 | §2 | Windows Credential Manager `TokenStore` for Recto Cloud sessions | D08 T01 §9, D02 T04 §1 | [ ] |
| 3 | §3 | OAuth 2.0 PKCE sign-in through the system browser and a loopback redirect | §1, §2, D09 T01 §14 | [ ] |
| 4 | §4 | `CloudClient` with typed errors, token refresh, retry and offline detection | §1, §2, D01 T01 §1, D09 T01 §13, D09 T02 §3, D09 T02 §4 | [ ] |
| 5 | §5 | Upload consent gate and exact-send preview dialog | §4, D02 T01 §10, D05 T02 §9 | [ ] |
| 6 | §6 | Settings `RECTO CLOUD` group, sign-in and sign-out commands | §3, §4, D02 T04 §2, D02 T04 §6, D02 T03 §11 | [ ] |
| 7 | §7 | `Open from Recto Cloud` picker and verified download | §4, §5, §6, D02 T01 §3, D02 T01 §5, D09 T02 §5, D09 T02 §8, D09 T02 §11 | [ ] |
| 8 | §8 | `Save to Recto Cloud` with version conflict handling | §7, D02 T01 §8, D04 T02 §1, D09 T02 §3, D09 T02 §4 | [ ] |
| 9 | §9 | `Send for signature`: upload preview and hand-off to web field placement | §5, §6, §8, D09 T04 §5, D09 T04 §6 | [ ] |
| 10 | §10 | `Start a shared review` from the open document | §5, §6, §8, D09 T03 §4, D09 T03 §10, D09 T03 §13, D03 T03 §2 | [ ] |
| 11 | §11 | Live review comments in the Comments panel | §10, D03 T03 §4, D03 T03 §5, D03 T02 §5, D09 T03 §5, D09 T03 §6, D09 T03 §7 | [ ] |
| 12 | §12 | Zero network before sign-in, token privacy and redirect safety suite | §3, §6, §8, §9, §11, D07 T02 §4, D07 T03 §2, D07 T01 §7 | [ ] |
| 13 | §13 | Record domain, sender domain and data region decisions | -- | [ ] |
| 14 | §14 | Obtain the seal certificate from an AATL CA for an HSM or signing service | -- | [ ] |
| 15 | §15 | Terms, privacy policy, ESIGN disclosure and DPA reviewed by counsel | §13, D09 T04 §20 | [ ] |
| 16 | §16 | Production verification, content-free monitoring and acceptance run | §13, §14, D09 T01 §21, D09 T04 §15 | [ ] |
| 17 | §17 | Status page, abuse contact and security disclosure | §16 | [ ] |
| 18 | §18 | Go-live gate: desktop default server, parity ledger and release note | §12, §15, §16, §17 | [ ] |
| 19 | §19 | Account assets API: encrypted signature appearance and stamp storage | D09 T01 §3, D09 T01 §4, D09 T01 §14, D09 T01 §15 | [ ] |
| 20 | §20 | Signature and stamp sync across devices, opt-in | §4, §6, §19, D02 T04 §2, D03 T03 §20, D05 T02 §10 | [ ] |

## 1. HTTPS-only WinHTTP cloud transport with no redirect credential leakage

All desktop traffic to Recto Cloud leaves through one transport so the privacy rules live in one place. It must refuse any non-HTTPS URL, bind each instance to the one origin the user signed in to, use the system proxy configuration, validate TLS certificates with no override, follow no redirect at all (WinHTTP carries added headers across redirects, so a redirect is how a bearer token would reach another origin), bound request and response sizes, stream bodies to and from files, and abort promptly on cancel. It must not be created while no session exists (§12 proves it), no other code in `src/services/cloud/` may include `winhttp.h`, and it must not be linked into `recto-core`, `recto-pdf` or the crash reporter (`D07 T02 §4`). It keeps the same rules as `D08 T01 §2` without sharing its namespace, so removing the assistant never touches cloud and the reverse.

**Phase:** 98
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/cloud/cloud_transport.h` (`ICloudTransport::send(CloudRequest, BodySource, ResponseSink, CancelToken)` and `open_websocket(path, headers, CancelToken)`; `BodySource` is bytes in memory or an already-open read handle (never a path, so the transport cannot reopen a file) read at explicit offsets in 64 KiB chunks; `ResponseSink` receives `on_head(status, headers)` then streamed `on_chunk` for 2xx, or one buffered error body of at most 64 KiB for status 400 and above) and `src/services/cloud/winhttp_cloud_transport.h/.cpp`. The transport is constructed with one `Origin{scheme=https, host, port}` and a `TokenProvider` callback; it adds `Authorization: Bearer` only while sending, from the callback, then zeroes its buffer. Done when: `Cloud.TransportRejectsNonHttps` shows `http://`, `file://`, `ftp://`, a URL with userinfo, and a URL whose origin differs from the bound origin each return `CloudErrorCode::InvalidUrl` with zero sockets opened.
2. Enforce limits and redirects: request body at most the per-call cap the caller passes (the service's 8 MiB upload part size in practice, never above 64 MiB), JSON response at most 16 MiB, downloads at most the declared `Content-Length` and never above the caller's cap, WinHTTP automatic redirect policy set to `WINHTTP_OPTION_REDIRECT_POLICY_NEVER`, and every 3xx mapped to `UnexpectedRedirect`. Done when: `Cloud.TransportRefusesAllRedirects` shows a same-origin, a cross-origin and an HTTPS-to-HTTP redirect each return `UnexpectedRedirect` while the redirect target host records zero requests and zero auth headers.
3. Map WinHTTP failures to `CloudErrorCode` (name not resolved or connect failure to `NetworkUnreachable`, proxy 407 to `ProxyAuthRequired`, certificate errors to `TlsCertificateInvalid`, timeouts to `Timeout`) with defaults connect 15 s, send 60 s, receive-idle 60 s and a total deadline of 30 minutes for transfers. Done when: `Cloud.TransportErrorMapping` covers each mapping with injected WinHTTP error codes and `Cloud.TransportDefaultsAreBounded` asserts every timeout is finite and non-zero.
4. Implement cancellation: a watcher polls the `CancelToken` every 100 ms and closes the request handle so a blocked read or write returns; the WebSocket path uses `WinHttpWebSocketCompleteUpgrade` with the same origin pin and no redirect. Done when: `Cloud.TransportCancelWithin500ms` cancels a stalled download and a stalled upload and each returns `Cancelled` in under 500 ms.
5. Add `src/services/cloud/cloud_transport_factory.h/.cpp` as the only place a transport is constructed, taking a `SessionPermit` that only `TokenStore` (§2) and the sign-in flow (§3) can mint, with a process-wide construction counter used by §12. Done when: `Cloud.TransportFactoryRequiresPermit` shows construction without a permit fails to compile in a negative-compile test and the counter increments once per construction.

- [ ] Add `cloud_transport.h` and the error enum `CloudErrorCode` stub with `InvalidUrl`, `RequestTooLarge`, `NetworkUnreachable`, `ProxyAuthRequired`, `TlsCertificateInvalid`, `Timeout`, `UnexpectedRedirect`, `Cancelled` in `src/services/cloud/cloud_error.h`. Done when: `Cloud.TransportRejectsNonHttps` compiles and passes.
- [ ] Implement `winhttp_cloud_transport.cpp` with `WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY`, the origin pin, the redirect-never policy and bounded timeouts. Done when: `Cloud.TransportRefusesAllRedirects` and `Cloud.TransportDefaultsAreBounded` pass.
- [ ] Never set `WINHTTP_OPTION_SECURITY_FLAGS` and never use any `SECURITY_FLAG_IGNORE_*` value; add a source scan. Done when: `Cloud.NoTlsOverrideInSource` greps `src/services/cloud/` and fails if either appears. Cheaper substitute: a settings toggle named "ignore certificate errors", which is forbidden.
- [ ] Add a source scan that only the transport includes `winhttp.h` and that `recto-core`, `recto-pdf` and `src/core/crash_reporter.cpp` include no cloud header. Done when: `Cloud.WinHttpOnlyInTransport` passes and adding `#include <winhttp.h>` to `cloud_client.cpp` fails it.
- [ ] Add the untrusted-certificate case against the `D08 T01 §4` loopback TLS server with a certificate no store trusts. Done when: `Cloud.TransportRejectsSelfSignedCert` returns `TlsCertificateInvalid` and the server records zero request bytes.
- [ ] Implement streamed file upload and download with the size caps and the cancel watcher. Done when: `Cloud.TransportEnforcesSizeCaps` shows an oversize request body returns `RequestTooLarge` before connecting and an oversize download stops with `MalformedResponse`, and `Cloud.TransportCancelWithin500ms` passes.
- [ ] Add the WebSocket upgrade with the bearer sent as a header (never in the URL or query string). Done when: `Cloud.WebSocketBearerNotInUrl` shows the server saw no token in the request line and `Cloud.WebSocketRefusesRedirect` passes.
- [ ] Add the factory, permit type and construction counter. Done when: `Cloud.TransportFactoryRequiresPermit` passes.
- [ ] Commit: `cloud: add HTTPS-only WinHTTP transport pinned to one origin with no redirects`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Cloud.Transport*:Cloud.NoTlsOverrideInSource:Cloud.WinHttpOnlyInTransport:Cloud.WebSocket*` exits 0, and adding `SECURITY_FLAG_IGNORE_CERT_CN_INVALID` to `winhttp_cloud_transport.cpp` fails `Cloud.NoTlsOverrideInSource`.

## 2. Windows Credential Manager `TokenStore` for Recto Cloud sessions

A session token is a secret, so the only place Recto keeps it is the Windows Credential Manager of the signed-in Windows account. It must never enter `SettingsStore`, the Portable `data\` folder, logs, the crash bundle (`D07 T02 §4`), diagnostics (`D07 T03 §2`), exports, test output, a command line or a URL. It must not log a token's length, prefix or suffix, and must not hold an access token anywhere but memory.

**Phase:** 98
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Reuse `ICredentialBackend` from `D08 T01 §9`: if it still lives under `src/services/assistant/`, move it to `src/services/credentials/credential_backend.h/.cpp` and keep the assistant include working, so both stores share one Windows backend and one in-memory test backend. Done when: `Assistant.CredentialRoundTrip` still passes and `Cloud.TokenStoreUsesSharedBackend` shows the cloud store compiles against the shared header.
2. Author `src/services/cloud/token_store.h/.cpp` over that backend with `CRED_TYPE_GENERIC`, target `Recto/Cloud/<lowercase origin>`, `CRED_PERSIST_LOCAL_MACHINE`, and a UTF-8 JSON blob `{"v":1,"refresh":"...","label":"..."}` holding only the refresh token and the account label (the label is shown in Settings so no network call is needed to display it). The access token lives only in `TokenSession` memory. Expose `has_session(origin)`, `account_label(origin)`, `with_refresh_token(origin, callback)` (view valid only for the call, buffer zeroed with `SecureZeroMemory` after), `save_session(origin, refresh, label)` and `clear_session(origin)`. Done when: `Cloud.TokenStoreRoundTrip` writes, reads, replaces and deletes a session on the in-memory backend and ends with no credential left.
3. Reject empty, control-character or oversize blobs (the Credential Manager blob limit is 2560 bytes) with a typed error, and replace the credential atomically on refresh-token rotation (write the new blob, then read it back, then report success). Done when: `Cloud.TokenStoreRejectsBadInput` and `Cloud.TokenStoreRotationIsReadBackVerified` pass.
4. Mint the `SessionPermit` of §1 only from `has_session` true or from the sign-in flow, so no transport exists before sign-in. Done when: `Cloud.PermitNeedsSession` passes with no session and with a stored session.
5. Add the key-never-on-disk scan. Done when: `Cloud.TokenNeverInSettingsOrPortableFolder` stores a sentinel token, saves settings, toggles Portable mode through `AppPaths` (`D02 T04 §1`) and finds the sentinel in no file under `%LOCALAPPDATA%\Recto` or the portable `data\` folder.

- [ ] Move or share `ICredentialBackend` without breaking the assistant. Done when: `Assistant.Credential*` and `Cloud.TokenStoreUsesSharedBackend` pass.
- [ ] Implement `TokenStore` with callback-scoped refresh token access and zeroing. Done when: `Cloud.TokenIsZeroedAfterUse` reads the buffer after use and finds zeros.
- [ ] Add a review-guard test that `with_refresh_token` has exactly two callers, `cloud_client.cpp` (refresh) and `oauth_flow.cpp` (revocation). Done when: `Cloud.WithRefreshTokenHasReviewedCallers` greps `src/` and `tests/` for the symbol and fails on a third caller.
- [ ] Implement validation and `clear_session`. Done when: `Cloud.TokenStoreRejectsBadInput` and `Cloud.TokenStoreClearDeletesEntry` pass.
- [ ] Run one integration test against the real Credential Manager with a sentinel origin removed at teardown. Done when: `Cloud.TokenStoreRealStoreRoundTrip` passes on the Windows host and leaves nothing behind.
- [ ] Add the settings and portable scan. Done when: `Cloud.TokenNeverInSettingsOrPortableFolder` passes, and writing the token into `SettingsStore` makes it fail. Cheaper substitute: storing the token in `settings.json` with a note that Portable users should be careful.
- [ ] Commit: `cloud: store Recto Cloud sessions in Windows Credential Manager only`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Cloud.TokenStore*:Cloud.Token*:Cloud.PermitNeedsSession:Cloud.WithRefreshTokenHasReviewedCallers` exits 0, and writing the refresh token into `SettingsStore` fails `Cloud.TokenNeverInSettingsOrPortableFolder`.

## 3. OAuth 2.0 PKCE sign-in through the system browser and a loopback redirect

Sign-in must never put a password or a web form inside Recto. It follows the OAuth 2.0 authorization code flow with PKCE for native apps (RFC 7636 and RFC 8252): Recto opens the user's default browser at the service's authorization page, the user signs in there with a passkey or email link (the service owns that), and the browser returns the code to a single-use listener on `127.0.0.1`. It must not use an embedded web view, a custom URI scheme or a client secret, must accept exactly one valid callback, and must be cancellable.

**Phase:** 98
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/cloud/oauth_flow.h/.cpp`: generate a 32-byte `code_verifier` and a 16-byte `state` with `BCryptGenRandom`, base64url without padding, derive `code_challenge = BASE64URL(SHA256(verifier))` with CNG, and build the authorization URL from the server origin's `/oauth/authorize` endpoint with `response_type=code`, `client_id=recto-desktop`, `code_challenge_method=S256`, `redirect_uri=http://127.0.0.1:<port>/callback`, `state` and `scope=documents reviews signatures` (the scopes of the platform file's client registry). Done when: `Cloud.OAuthPkceMatchesRfc7636Vector` passes against the RFC 7636 Appendix B verifier and challenge, and `Cloud.OAuthUsesS256Only` asserts the URL never contains `plain`.
2. Author `src/services/cloud/loopback_listener.h/.cpp` on Winsock: bind `127.0.0.1` only (never `0.0.0.0` or `::`), port 0 (ephemeral), accept one connection, require method `GET`, path `/callback`, and a `Host` header equal to `127.0.0.1:<port>` (defence against DNS rebinding), read at most 8 KiB, compare `state` in constant time, reply with a fixed static page that contains no token and no query echo, then close the socket; time out after 5 minutes. Done when: `Cloud.LoopbackListenerBindsLoopbackOnly`, `Cloud.LoopbackRejectsWrongHostHeader`, `Cloud.LoopbackRejectsWrongState`, `Cloud.LoopbackAcceptsOnlyOneCallback` and `Cloud.LoopbackTimesOut` pass.
3. Open the browser with `ShellExecuteW` on the `https` authorization URL only (refuse any other scheme), exchange the code at `/oauth/token` through the §1 transport (`grant_type=authorization_code`, `code`, `code_verifier`, `redirect_uri`, `client_id`), read the account email and display name once with `GET /api/account` (§4) for the label, and store the refresh token and that label through `TokenStore` (§2). The access token stays in memory with its expiry. Done when: `Cloud.SignInEndToEnd` against the scripted server and the `D08 T01 §4` TLS server ends with `has_session` true, one credential written, and no token in any log line.
4. Implement sign-out: `POST /oauth/revoke` for the refresh token, then `clear_session` whether or not the call succeeded, then drop the in-memory access token and stop any live work (§11 subscribes to a `SessionEnded` event). Done when: `Cloud.SignOutRevokesAndClears` shows revoke is called once and the credential is gone, and `Cloud.SignOutClearsWhenOffline` shows an unreachable server still clears the credential and returns `RevocationPending` for the UI to explain.
5. Handle the browser outcomes: the user denies access (`error=access_denied`), closes the tab (timeout), starts a second sign-in (the first is cancelled), or the server is unreachable. Done when: `Cloud.SignInDenied`, `Cloud.SignInTimeout`, `Cloud.SignInSecondAttemptCancelsFirst` and `Cloud.SignInServerUnreachable` each return a typed result with a `describe` message and leave no listener socket open (`Cloud.NoListenerLeak`).

- [ ] Implement the PKCE and state generation and the authorization URL builder. Done when: `Cloud.OAuthPkceMatchesRfc7636Vector` passes.
- [ ] Implement the loopback listener with the host, path, size, state and single-use checks. Done when: the five `Cloud.Loopback*` tests pass.
- [ ] Implement the code exchange and token storage over §1 and §2. Done when: `Cloud.SignInEndToEnd` passes.
- [ ] Implement sign-out with best-effort revocation. Done when: `Cloud.SignOutRevokesAndClears` and `Cloud.SignOutClearsWhenOffline` pass.
- [ ] Refuse a token response that arrives over a non-pinned origin, lacks `token_type` `Bearer`, or carries an `expires_in` outside 60 to 86400 seconds. Done when: `Cloud.TokenResponseValidation` passes. Cheaper substitute: trusting whatever JSON the server returns.
- [ ] Never log the authorization URL, the code, the verifier or the state; log only the event type. Done when: `Cloud.OAuthLogsAreContentFree` captures the D16 ring buffer during sign-in and finds none of the generated values.
- [ ] Commit: `cloud: add OAuth 2.0 PKCE sign-in with a loopback redirect and token revocation`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Cloud.OAuth*:Cloud.Loopback*:Cloud.SignIn*:Cloud.SignOut*:Cloud.TokenResponseValidation:Cloud.NoListenerLeak` exits 0, and binding the listener to `0.0.0.0` fails `Cloud.LoopbackListenerBindsLoopbackOnly`.

## 4. `CloudClient` with typed errors, token refresh, retry and offline detection

Every cloud feature calls the service through one typed client so refresh, retry, error wording and version negotiation are written once. It must run only on worker threads, return `PdfResult`-style typed results instead of throwing, never block the UI thread, and turn every failure into a title, a cause and a next step, never a bare `http status: 403`. It must not retry a request that could create a duplicate, and must not add background polling of any kind.

**Phase:** 98
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/cloud/cloud_client.h/.cpp` with `Result<T, CloudError>` calls for the endpoints this file uses, as the service files define them: the account (`GET /api/account`, used once after sign-in for the label) and usage (`GET /api/account/usage`), folder and shared-with-me listing (`GET /api/folders/:id/children`, `GET /api/shared-with-me`), document and version metadata (`GET /api/documents/:id`, `GET /api/documents/:id/versions`), version download (`GET /api/documents/:id/versions/:n/content`), the resumable upload session (`POST /api/uploads`, `PUT /api/uploads/:id/parts/:n`, `POST /api/uploads/:id/complete`, `GET /api/uploads/:id`), envelope draft creation and discard, and review creation, closing and annotations. Each call takes a `CancelToken` and runs on the caller's worker thread. Done when: `Cloud.ClientCallsAreOffUiThread` asserts every public call asserts it is not on the UI thread in Debug and the scripted-server tests for each endpoint pass.
2. Extend `CloudErrorCode` and add `describe(CloudError) -> {title, cause, next_step}` for `NotSignedIn`, `SessionExpired`, `NetworkUnreachable`, `Timeout`, `TlsCertificateInvalid`, `ProxyAuthRequired`, `UnexpectedRedirect`, `Forbidden`, `NotOwner`, `NotFound`, `VersionConflict`, `QuotaExceeded`, `FileTooLarge`, `UnsupportedFile`, `RateLimited`, `ServerError`, `ServiceUnavailable`, `MalformedResponse`, `ClientTooOld`, `ServerTooOld` and `Cancelled`, with strings in `Resources.resw`. Done when: `Cloud.EveryErrorHasDescription` shows no code has an empty title, cause or next step and none contains a bare number such as `403` as its whole message.
3. Implement refresh: when the access token is missing or within 60 seconds of expiry, call `/oauth/token` with `grant_type=refresh_token` once (single-flight under a mutex so parallel calls share one refresh), store the rotated refresh token through `TokenStore` (§2) and read it back before the new access token is used (the service rotates refresh tokens and revokes the whole token family when an old one is presented again, so a lost rotation must sign the user out cleanly rather than retry with the old token), and on `invalid_grant` clear the session and return `SessionExpired`. Done when: `Cloud.RefreshIsSingleFlight`, `Cloud.RefreshRotatesToken`, `Cloud.RefreshStoreFailureSignsOut` and `Cloud.RefreshInvalidGrantSignsOut` pass.
4. Implement retry: only for reads, for `PUT` of an upload part (the same part number replaces the earlier bytes) and for `complete` (the service answers the same committed state on a repeat), never for creating an upload session, an envelope draft or a review without first listing to see whether the earlier attempt succeeded, on `429`, `502`, `503`, `504` and connection failures, at most 3 attempts with exponential backoff and jitter honouring `Retry-After` up to 60 seconds, abandoned immediately on cancel. Done when: `Cloud.RetryOnlyRepeatSafeCalls`, `Cloud.RetryHonoursRetryAfter` and `Cloud.RetryStopsOnCancel` pass.
5. Add API version negotiation: send `Recto-Client: <version>` and `Recto-Api: 1`, map `426` to `ClientTooOld` ("Update Recto to keep using Recto Cloud") and an unsupported-API response to `ServerTooOld`. Done when: `Cloud.VersionNegotiation` covers both.
6. Treat every JSON response as hostile: parse with a bounded parser (depth 32, 16 MiB), reject unknown required-field types, cap string lengths, and return `MalformedResponse` instead of throwing. Done when: `Cloud.ResponseParserIsBounded` passes and `fuzz/fuzz_cloud_json.cpp` runs 5 minutes under `/fsanitize=fuzzer,address` over seeds in `tests/corpus/seeds/cloud/` with no finding.

- [ ] Implement the client calls over §1 with `Cloud.Client*` fixtures from a scripted server in `tests/recto-tests/cloud/fake_cloud_server.h/.cpp` (HTTPS through the `D08 T01 §4` TLS server). Done when: each endpoint has a success, a 4xx and a malformed-body test and all pass.
- [ ] Implement `describe` and the resource strings. Done when: `Cloud.EveryErrorHasDescription` passes.
- [ ] Implement single-flight refresh and sign-out on `invalid_grant`. Done when: the four `Cloud.Refresh*` tests pass.
- [ ] Implement the retry policy. Done when: the three `Cloud.Retry*` tests pass.
- [ ] Implement version headers and `ClientTooOld`. Done when: `Cloud.VersionNegotiation` passes.
- [ ] Add the bounded JSON reader and the fuzz target `fuzz/fuzz_cloud_json.cpp` with seeds. Done when: `Cloud.ResponseParserIsBounded` passes and the 5 minute fuzz run is clean.
- [ ] Add an offline probe used only after a failure: no ping, no background check; a failed call classifies as `NetworkUnreachable` and the UI offers Retry. Done when: `Cloud.NoBackgroundTraffic` runs a signed-in session for 60 seconds of simulated idle and records zero requests.
- [ ] Commit: `cloud: add typed CloudClient with refresh, bounded retry and plain-language errors`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Cloud.Client*:Cloud.EveryErrorHasDescription:Cloud.Refresh*:Cloud.Retry*:Cloud.VersionNegotiation:Cloud.ResponseParserIsBounded:Cloud.NoBackgroundTraffic` exits 0, and replacing a `describe` title with an empty string fails `Cloud.EveryErrorHasDescription`.

## 5. Upload consent gate and exact-send preview dialog

Nothing leaves the machine without the user seeing it first (D16, D20). One gate and one dialog serve Save to Recto Cloud, Send for signature and Start a shared review, so the wording and the checks cannot drift. The dialog must show exactly the bytes that will be sent, not a description of them, must not be skippable by a "do not ask again" option, and must not upload while it is open.

**Phase:** 98
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` section 9.9 (the dialog family, the `Replace the original?` layout) with the `D02 T01 §10` shared `ContentDialog` chrome; no screenshot exists.
**Job:** A user about to upload a document sees the file name, size, page count, the server and account it goes to, what the recipient will be able to do, and any warning, then confirms or cancels. Consumer: §8, §9 and §10, and the in-memory ledger of §12.
**Treatment:** One `CloudUploadDialog` over `RectoDialogStyle` with a fact list, a warnings `InfoBar`, a "What happens next" sentence per operation, `Cancel` as the default button and an enabled `Upload` button only after the upload copy has been prepared. Cheaper substitute that fails the checkpoint: a generic "Send to cloud?" confirmation with no file facts, or a "Do not ask again" check box.
**Chrome:** `RectoDialogStyle` (`D02 T01 §10`), stock `ContentDialog`, `InfoBar` severities and card brushes from `RectoColors.xaml`, Segoe Fluent Icons `E898` (upload) in the title, no hex colours.

**Build order:**

1. Author `src/services/cloud/upload_gate.h/.cpp`: `UploadGate::prepare(tab, operation) -> UploadPlan{temp_path, bytes, sha256, page_count, file_name, includes_unsaved_changes, warnings[]}` that asks the `Save a copy` path of `SavePipeline` (`D02 T01 §8`) for a copy of the tab's current state in `AppPaths::temp_dir()\cloud\` (temp-file-then-replace; the open document is never modified), hashes it, and refuses before any network call for: an encrypted PDF (`Remove the password first`), a file over the client's hard cap of 2 GiB (the service's own cap, 200 MiB by default, is enforced by the service when the upload session is created, before the first byte, and its `too_large` and `quota_exceeded` answers become `FileTooLarge` and `QuotaExceeded` with the used and limit figures from `GET /api/account/usage`), and a file that fails the `recto-pdf` open check. Done when: `Cloud.GatePreparesCopyWithoutTouchingOpenDocument`, `Cloud.GateRefusesEncrypted`, `Cloud.GateRefusesOversize` (2 GiB), `Cloud.GateShowsServiceCapRefusal` and `Cloud.GateRefusesUnreadable` pass.
2. Add warnings: pending unapplied redaction marks (`D05 T02 §9`) with their count ("The file will be sent without them applied"), and unsaved changes included. Done when: `Cloud.GateWarnsPendingRedactions` and `Cloud.GateWarnsUnsavedChanges` pass.
3. Author `src/app/CloudUploadDialog.xaml/.h/.cpp` showing the plan facts, the destination host and account label, the operation sentence, the warnings, and `Upload` enabled only after `prepare` completed on a worker thread (spinner meanwhile, cancel aborts and deletes the temp copy). Done when: `AppTests.CloudUploadDialogShowsExactFacts` shows the file name, size in bytes, page count, first 12 hex characters of the SHA-256 and the host, and `AppTests.CloudUploadDialogDefaultIsCancel` passes.
4. Record each confirmed upload in an in-memory `UploadLedger` (operation, host, byte count, page count, time; never a file name or content) cleared on exit and not persisted. Done when: `Cloud.LedgerHoldsNoNamesOrContent` serializes the ledger and finds no file name, path or byte of the document.
5. Guarantee the confirmed bytes are the uploaded bytes without a time-of-check gap: `prepare` creates the temp copy, reopens it once with `CreateFileW` for `GENERIC_READ` and share mode `FILE_SHARE_READ` only (no `FILE_SHARE_WRITE` or `FILE_SHARE_DELETE`, so nothing can write, truncate, rename or replace it), hashes through that handle, and keeps the handle in `UploadPlan`; the uploader reads every part from that same handle (never from the path), hashes the bytes again as they stream, sends the preview `sha256` in `POST /api/uploads` so the service rejects a mismatch (`checksum_mismatch`), and aborts with `UploadAborted` if the streamed hash differs at the end. The handle closes and the copy is deleted after success, failure or cancel. Done when: `Cloud.UploadedBytesMatchPreviewHash` attempts to open the temp copy for write, delete and rename after the hash check and during streaming and expects every attempt to fail with a sharing violation, and `Cloud.UploadStreamedHashMismatchAborts` swaps the handle's source for altered bytes and expects `UploadAborted`; `Cloud.TempCopyAlwaysDeleted` passes for success, failure and cancel.

- [ ] Implement `UploadGate` and its refusals. Done when: the four `Cloud.Gate*` refusal and preparation tests pass.
- [ ] Implement the warnings. Done when: `Cloud.GateWarnsPendingRedactions` and `Cloud.GateWarnsUnsavedChanges` pass.
- [ ] Add `CloudUploadDialog` with `AutomationProperties.Name` and tooltips on every icon-only control and `x:Uid` strings. Done when: the `D03 T02 §3` accessibility inventory lists the dialog with zero missing names. Cheaper substitute: an unnamed icon-only close button.
- [ ] Implement the worker-thread prepare, spinner and cancel. Done when: `AppTests.CloudUploadDialogNeverBlocksUi` shows the UI thread responds within 100 ms while a 150 MiB copy is prepared.
- [ ] Implement the ledger, the share-locked handle and the streamed rehash. Done when: `Cloud.LedgerHoldsNoNamesOrContent`, `Cloud.UploadedBytesMatchPreviewHash` and `Cloud.UploadStreamedHashMismatchAborts` pass. Cheaper substitute: rehashing the path before sending and then reopening it.
- [ ] Render the dialog in Light, Dark and HighContrast with token brushes only. Done when: `AppTests.CloudUploadDialogThemes` finds no literal colour in the dialog XAML. Cheaper substitute: a hex value on a control.
- [ ] Commit: `cloud: add upload consent gate and exact-send preview dialog`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Cloud.Gate*:Cloud.Ledger*:Cloud.UploadedBytesMatchPreviewHash:Cloud.UploadStreamedHashMismatchAborts:Cloud.TempCopyAlwaysDeleted:AppTests.CloudUploadDialog*` passes on the display-session host, and opening the temp copy with `FILE_SHARE_WRITE` fails `Cloud.UploadedBytesMatchPreviewHash`.

## 6. Settings `RECTO CLOUD` group, sign-in and sign-out commands

Recto Cloud is configured where every other setting is, and it is off until the user signs in. The group must show only a sign-in prompt while signed out, never show a token, state plainly what is sent and when, work in Portable mode without copying the session, and add no second settings surface or hex colour. It owns the cloud commands in `Ctrl+K` and the menus.

**Phase:** 98
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` section 9.7 (a settings group with `ToggleSwitch`, `TextBox` and `Button` rows) and the existing Privacy group chrome; no screenshot exists.
**Job:** A user can read what Recto Cloud is, sign in, see which account and server they are signed in to, sign out, change the server address when self-hosting, and find every cloud command. Consumer: the four feature sections (§7 to §10) and `Ctrl+K` settings search.
**Treatment:** One `RECTO CLOUD` group in `SettingsPage` after `PERFORMANCE AND PRIVACY` (and before `ASSISTANT` when that group exists) with: a state line, `Sign in to Recto Cloud` while signed out; the account label, server host, `Sign out` and `Open Recto Cloud in browser` while signed in; a `Server address` row with HTTPS validation and a `Use default` button; and a Portable note. Cheaper substitute that fails the checkpoint: a toggle that stores a flag and shows a username field, or a signed-in indicator that survives sign-out.
**Chrome:** Shared Settings group styles (`D02 T04 §4`, `§6`), stock WinUI 3 controls, `RectoColors.xaml` tokens, Segoe Fluent Icons `E77B` (account) and `E8D7` (sign out), `InfoBar` severities from tokens.

**Build order:**

1. Add settings keys in `src/services/settings.h/.cpp` (`D02 T04 §2`): `cloud.serverOrigin` (default from `src/services/cloud/cloud_config.h`, `https` origin only, no path, query or userinfo) and nothing else; there is no `cloud.enabled` flag because the signed-in state is the credential. Done when: `Settings.CloudServerOriginValidation` rejects `http://`, a path, userinfo, an IP literal with a port below 1, and an empty value (which restores the default).
2. Add the group to `src/app/SettingsPage.xaml/.h/.cpp`, indexed by `Find a setting` with the terms "cloud", "sign in", "account", "signature", "review", "server". Done when: typing "sign in" reveals the group.
3. Register the commands in `CommandRegistry` (`D02 T03 §11`): `cloud.signIn` (visible only signed out), and `cloud.signOut`, `cloud.open`, `cloud.save`, `cloud.sendForSignature`, `cloud.startReview`, `cloud.closeReview`, `cloud.openOnWeb` (visible only signed in), each with a Segoe Fluent Icon, a tooltip and a File menu entry in Classic and a More menu entry in Focus. Done when: `Commands.CloudCommandsVisibilityFollowsSession` shows signed out lists only `cloud.signIn` in `Ctrl+K` and the menus, and signed in lists all of them.
4. Run sign-in and sign-out on a worker thread with a spinner, cancel, and an `InfoBar` result using `describe` (§4); after sign-out explain `RevocationPending` ("Signed out on this PC. We could not reach the server to revoke access; sign out of all devices from the web app"). Done when: `Settings.CloudSignInNeverBlocksUi` and `Settings.CloudSignOutExplainsPendingRevocation` pass.
5. Show the Portable note when Portable mode is on ("The session stays in this Windows account's Credential Manager and is not copied to the portable folder") and prove a portable folder moved to another PC arrives signed out. Done when: `Settings.CloudPortableNoteAndSignedOutAfterMove` passes.

- [ ] Add the group XAML with `AutomationProperties.Name` and tooltips on every icon-only control and `x:Uid` for each string. Done when: the `D03 T02 §3` accessibility inventory lists the group with zero missing names. Cheaper substitute: unnamed icon buttons.
- [ ] Implement the signed-out and signed-in states from `TokenStore::has_session`, with no network call to render either. Done when: `Settings.CloudGroupRendersWithoutNetwork` records zero requests while the group opens in both states.
- [ ] Implement the `Server address` row with validation and `Use default`; changing it while signed in signs out first after a confirmation dialog. Done when: `Settings.CloudServerChangeSignsOut` passes.
- [ ] Register the eight commands with visibility bound to the session. Done when: `Commands.CloudCommandsVisibilityFollowsSession` passes.
- [ ] Wire sign-in and sign-out to §3 on worker threads. Done when: the two `Settings.Cloud*` UI tests pass.
- [ ] Add the account label from `TokenStore` (never a token) and `Open Recto Cloud in browser` using `ShellExecuteW` on the configured `https` origin. Done when: `Settings.CloudNeverDisplaysToken` asserts no control property contains the sentinel token.
- [ ] Render the group in Light, Dark and HighContrast with token brushes only. Done when: `Settings.CloudGroupThemes` finds no literal colour in the group's XAML. Cheaper substitute: a hex value on a control.
- [ ] Commit: `app: add Settings RECTO CLOUD group with sign-in, sign-out and commands`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Settings.Cloud*:Commands.CloudCommandsVisibilityFollowsSession` passes on the display-session host, and registering `cloud.save` unconditionally fails `Commands.CloudCommandsVisibilityFollowsSession`.

## 7. `Open from Recto Cloud` picker and verified download

A signed-in user can open a document stored in Recto Cloud as a normal tab. The download must be verified, must reach the viewer only through `OpenDocumentPipeline`, must never block the UI thread, and must not name local files after untrusted server text. It matches Acrobat's Document Cloud open while keeping Recto's rule that the open document is a local copy owned by one `DocumentActor`.

**Phase:** 98
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` section 9.9 (the dialog family) and the file list conventions of the Home screen recents (`D02 T03 §2`); no screenshot exists.
**Job:** A user finds a document by name or from the shared-with-me list, sees its owner, modified time and size, and opens it in a new tab in one action. Consumer: `OpenDocumentPipeline` (`D02 T01 §5`) and the tab binding of §8.
**Treatment:** A `CloudOpenDialog` with `My documents` and `Shared with me` pivots, a breadcrumb folder path, a name filter box, a virtualized `ListView` paged by server cursor, per-row size and modified time, a download progress bar with cancel, and an `InfoBar` for errors with Retry. Cheaper substitute that fails the checkpoint: a text box for pasting a document id, or loading every document before showing the list.
**Chrome:** `RectoDialogStyle` (`D02 T01 §10`), stock `Pivot`, `ListView` and `ProgressBar`, `RectoColors.xaml` tokens, Segoe Fluent Icons `E753` (cloud) and `E896` (download).

**Build order:**

1. Author `src/app/CloudOpenDialog.xaml/.h/.cpp` and `CloudOpenViewModel` over `GET /api/folders/root/children`, `GET /api/folders/:id/children` (folders first, then documents, cursor-paged) and `GET /api/shared-with-me`, with the next page requested as the list scrolls, folder navigation with a breadcrumb, a name filter that narrows only the entries already loaded (the service has no search endpoint, and the dialog says so in its empty state), and a worker-thread loader. Done when: `AppTests.CloudOpenListsPagesLazily` shows a 1,500-entry fake folder renders the first page without requesting the second until scrolled, `AppTests.CloudOpenFolderNavigation` opens a subfolder and returns by breadcrumb, and `AppTests.CloudOpenFilterNarrowsLoadedEntries` passes.
2. Author `src/services/cloud/cloud_download.h/.cpp`: download a document version to `AppPaths::temp_dir()\cloud\<document-id>-v<version>.pdf.part` through the §1 file sink, verify the byte count against `Content-Length` and the SHA-256 against the `ETag` (which the service sets to the version's SHA-256) of the content response, then `MoveFileExW` to the final name; delete the `.part` on any failure. Done when: `Cloud.DownloadVerifiesHash`, `Cloud.DownloadRejectsHashMismatch`, `Cloud.DownloadDeletesPartOnFailure` and `Cloud.DownloadCancelCleansUp` pass.
3. Open the verified file through `OpenDocumentPipeline` and attach a `CloudBinding{server_origin, document_id, version, content_hash}` to the new `DocumentTabSession` (`D02 T01 §3`), titled with the document's name. Done when: `AppTests.CloudOpenCreatesBoundTab` shows the tab opens, shows the cloud name, and `Cloud.BoundTabHasNoLocalPath` shows `Save a copy` is the only local save for it.
4. Sanitize server-provided names before they reach a window title or a suggested file name: strip control characters and bidi overrides, reserved device names (`CON`, `NUL`, `COM1`), trailing dots and spaces, cap at 120 characters, keep non-Latin text intact. Local temp names use ids only. Done when: `Cloud.NameSanitization` covers each case including a Hebrew and a Japanese name, and `AppTests.CloudNonLatinNameRenders` shows the title renders.
5. Sweep stale `temp_dir()\cloud\` files at startup (files older than 24 hours that no open tab uses), with no network involved. Done when: `Cloud.StaleTempSweep` passes and `Cloud.SweepNeverDeletesOpenTabFile` passes.

- [ ] Implement the dialog, view model and paging. Done when: the two `AppTests.CloudOpen*` list tests pass.
- [ ] Implement the verified download. Done when: the four `Cloud.Download*` tests pass.
- [ ] Implement tab binding and the title. Done when: `AppTests.CloudOpenCreatesBoundTab` and `Cloud.BoundTabHasNoLocalPath` pass.
- [ ] Implement `Sanitize` for names. Done when: `Cloud.NameSanitization` passes.
- [ ] Show every failure as an `InfoBar` with `describe` and Retry, including `Forbidden` ("You no longer have access to this document") and `NotFound`. Done when: `AppTests.CloudOpenShowsTypedErrorsWithRetry` covers `NetworkUnreachable`, `Forbidden`, `NotFound` and `SessionExpired`.
- [ ] Add `AutomationProperties.Name` and tooltips on every icon-only control and keyboard navigation (arrow keys, `Enter` to open, `Escape` to cancel). Done when: the accessibility inventory lists the dialog with zero missing names. Cheaper substitute: a mouse-only list.
- [ ] Add the temp sweep. Done when: `Cloud.StaleTempSweep` and `Cloud.SweepNeverDeletesOpenTabFile` pass.
- [ ] Commit: `app: add Open from Recto Cloud picker with verified atomic download`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Cloud.Download*:Cloud.NameSanitization:Cloud.BoundTabHasNoLocalPath:Cloud.*Sweep*:AppTests.CloudOpen*` passes on the display-session host, and skipping the hash comparison fails `Cloud.DownloadRejectsHashMismatch`.

## 8. `Save to Recto Cloud` with version conflict handling

Saving to the cloud must never lose either side's work. Cloud versions are immutable (design: documents are immutable versions in R2), so a conflict is resolved by choosing where the new version goes, never by overwriting. The local rules still hold: the open viewer document is never modified in place, local files are written temp-file-then-replace, and closing a modified cloud tab never silently drops changes when the network is down.

**Phase:** 98
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.9 (the `Replace the original?` and `Save your changes first?` dialogs are the layout model) and 10 (`Ctrl+S` behaviour); the conflict dialog is a genuinely new surface on that chrome; no screenshot exists.
**Job:** A user saves the current tab as a new cloud version of the document it came from, or as a new cloud document, and when someone else saved first chooses what to do with full knowledge of who and when. Consumer: the tab's dirty state, the unsaved-changes dialog and the cloud document list.
**Treatment:** `Save to Recto Cloud` runs the §5 preview, then uploads with the bound version checked first; on a newer server version a `ContentDialog` names who saved version N and when and offers `Save as a separate document` (default), `Save on top as the next version` (the other version stays in history) and `Cancel`. Cheaper substitute that fails the checkpoint: last-write-wins with no conflict dialog, or a conflict dialog whose only choices overwrite.
**Chrome:** `RectoDialogStyle` (`D02 T01 §10`), `CloudUploadDialog` (§5), `InfoBar` severities from `RectoColors.xaml`, Segoe Fluent Icons `E74E` (save).

**Build order:**

1. Add a `SaveTarget` seam to `SavePipeline` (`src/app/SavePipeline.h/.cpp`, `D02 T01 §8`): `LocalFileTarget` (existing behaviour, unchanged) and `CloudTarget` (uploads the prepared copy through §5 and §4). A cloud-bound tab's `Ctrl+S` and the `Save your changes first?` dialog's `Save` use `CloudTarget`; `Save a copy` always writes locally through `AtomicFileWriter` (`D04 T02 §1`). Done when: the existing `Save.*` tests still pass unchanged and `AppTests.CloudBoundCtrlSSavesToCloud` passes.
2. Implement `CloudClient::upload_file(plan, document_id_or_null, folder_id_or_null, bound_version_or_null)` (§4) over the service's resumable upload session (`D09 T02 §3`, `§4`): create the session, send 8 MiB parts with `PUT` (retrying a failed part, resuming from `GET /api/uploads/:id` after a restart of the call), complete, and poll until `committed` or `failed` on a worker thread with cancel. Before creating a session for a bound document, read the document's current version (`GET /api/documents/:id`) and, when it differs from the tab's bound version, return `VersionConflict{current_version, saved_by_label, saved_at}` without uploading anything; pass the bound version as the session's `baseVersion` (`D09 T02 §3`) so the service refuses a concurrent commit with `409 version_conflict`, mapped to the same `VersionConflict`. Done when: `Cloud.UploadChecksCurrentVersionFirst`, `Cloud.UploadReturnsConflictDetails`, `Cloud.UploadPartRetryReplacesPart` and `Cloud.UploadResumesAfterInterruption` pass.
3. Implement the conflict dialog and its three outcomes: `Save as a separate document` calls `upload_file` with no document id and rebinds the tab to the new document; `Save on top` calls `upload_file` for the same document with the bound version set to the server's current version after a second confirmation of the preview; `Cancel` leaves the tab dirty and uploads nothing. Done when: `AppTests.CloudConflictThreeOutcomes` covers each outcome and `Cloud.ConflictNeverLosesEitherVersion` shows after any outcome, including a second save that lands between the check and the upload, both versions still exist on the fake server (the service keeps immutable versions, so the race costs a version number, never data).
4. After success update the binding (`version`, `content_hash`) and clear the dirty state; after any failure keep the tab dirty and offer `Retry` and `Save a copy...` so offline close never discards work. Done when: `AppTests.CloudSaveFailureKeepsTabDirty` and `AppTests.CloudCloseOfflineOffersLocalCopy` pass.
5. Add first-save naming for an unbound tab: `Save to Recto Cloud` asks for a document name (default the tab title, sanitized per §7, max 120 characters) inside the §5 dialog. Done when: `AppTests.CloudFirstSaveAsksForName` passes.

- [ ] Add the `SaveTarget` seam with local behaviour unchanged. Done when: `Save.*` and `AppTests.CloudBoundCtrlSSavesToCloud` pass.
- [ ] Implement the resumable upload with the current-version check. Done when: the four `Cloud.Upload*` tests pass.
- [ ] Implement the conflict dialog with three outcomes and the second preview for `Save on top`. Done when: `AppTests.CloudConflictThreeOutcomes` passes. Cheaper substitute: a conflict `InfoBar` that only says "try again".
- [ ] Implement binding update and dirty handling. Done when: `AppTests.CloudSaveFailureKeepsTabDirty` passes.
- [ ] Implement the offline-close path through the unsaved-changes dialog. Done when: `AppTests.CloudCloseOfflineOffersLocalCopy` passes.
- [ ] Add a fault-injection case to `D06 T03 §5`'s pattern: kill the network mid-upload and mid-rename. Done when: `Cloud.SaveFaultInjection` shows no truncated local file, no half-uploaded version visible on the fake server and the tab still dirty.
- [ ] Add `AutomationProperties.Name`, tooltips and `x:Uid` strings for the dialog. Done when: the accessibility inventory lists it with zero missing names.
- [ ] Commit: `app: add Save to Recto Cloud with version conflict handling`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Save.*:Cloud.Upload*:Cloud.Conflict*:Cloud.SaveFaultInjection:AppTests.CloudConflict*:AppTests.CloudSave*:AppTests.CloudClose*:AppTests.CloudFirstSave*:AppTests.CloudBound*` passes on the display-session host, and making the conflict path overwrite the server version fails `Cloud.ConflictNeverLosesEitherVersion`.

## 9. `Send for signature`: upload preview and hand-off to web field placement

Request-a-signature is Acrobat Sign's headline cloud tool. On the desktop it is deliberately thin: Recto uploads the document after the user has seen exactly what is sent, asks the service to create a draft signature request, then opens the web field-placement page in the browser where the user adds recipients and fields and sends. Nothing reaches a signer from the desktop, so the web page's own review step is the last gate. It must not carry a token in the URL, must not open a URL outside the signed-in origin (it builds the URL itself from the envelope id), and must keep Recto's improvement that certificate-based signing stays local (`D05 T01 §8`).

**Phase:** 98
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` section 9.9 (dialog family) and reusing `CloudUploadDialog` (§5); no screenshot exists.
**Job:** A user with a finished PDF in a tab chooses `Send for signature`, confirms the upload, and lands on the web page where signature fields are placed; if they change their mind they can discard the draft from Recto. Consumer: the signature request web flow (the signature requests file) and the §12 ledger.
**Treatment:** `cloud.sendForSignature` runs the §5 preview with the sentence "Your document will be uploaded to <host>. Nothing is sent to signers until you place fields and press Send in your browser.", uploads (reusing the bound version when the tab is unmodified and already in the cloud), creates the draft, builds and opens the prepare page URL, and shows an `InfoBar` in the tab with `Open again` and `Discard draft`. Cheaper substitute that fails the checkpoint: opening a web URL with the document attached to it, or a one-click send to typed email addresses from the desktop with no preview.
**Chrome:** `CloudUploadDialog` (§5), tab-level `InfoBar` from `RectoColors.xaml`, Segoe Fluent Icons `E8C8` (signature request) and `E74D` (discard).

**Build order:**

1. Implement `CloudClient::create_envelope_draft(document_id, version)` and `discard_envelope_draft(envelope_id)` (§4) against the signature requests API, returning `{envelope_id}` (`POST /v1/envelopes` and `DELETE /v1/envelopes/:id`, draft only). Done when: `Cloud.EnvelopeDraftCreateAndDiscard` passes against the scripted server.
2. Build the page URL on the desktop instead of trusting one from the server: `https://<signed-in origin>/envelopes/<envelope_id>/prepare`, where `envelope_id` must match the id charset the service documents (letters, digits, `-` and `_`, at most 64 characters), so a hostile or compromised response cannot send the browser to another site; any `web_url` field in a response is ignored. Done when: `Cloud.PrepareUrlBuiltFromValidatedId` covers a normal id, an id containing `/`, `..`, `?`, `#`, `@` or `:`, an over-long id and a response with a hostile `web_url`, and `Cloud.PrepareUrlIsSameOriginHttps` asserts the result always has the configured origin.
3. Implement the command: prepare and preview (§5), reuse the bound version when the tab is bound and unmodified (no second upload, the preview says so), upload otherwise through `create_document`, create the draft, open the browser with `ShellExecuteW`, and show the draft `InfoBar`. No token, document id secret or email address appears in the URL beyond what the service returned. Done when: `AppTests.SendForSignatureHappyPath` and `Cloud.SendForSignatureReusesBoundVersion` pass.
4. Handle failures and retries: upload failure leaves no draft, a draft failure after upload keeps the uploaded document and offers Retry without re-uploading, the browser failing to open shows the URL with a `Copy link` button. Done when: `Cloud.SendForSignatureNoDuplicateUpload` and `AppTests.SendForSignatureShowsCopyLinkWhenBrowserFails` pass.
5. Add `Discard draft` with a confirmation and a plain statement that no recipient was contacted. Done when: `AppTests.SendForSignatureDiscardCallsApi` passes.
6. Handle the owner-only rule (`D09 T04 §5`: only the document owner may send an envelope; other roles get `403 not_owner`): for a tab bound to a cloud document, `cloud.sendForSignature` is enabled only when the caller's role from `GET /api/documents/:id` is owner (checked when the command runs, and shown in the tooltip), otherwise it stays visible but disabled with the explanation "Only the owner of this document can send it for signature. Save a copy to your own library first."; an unbound tab uploads a new document the user owns and is always allowed; a `403 not_owner` that still arrives (role changed meanwhile) maps to `CloudErrorCode::NotOwner` with the same wording and a `Save a copy to my library` action. Done when: `AppTests.SendForSignatureDisabledForNonOwner` covers view, comment and edit roles, `Cloud.NotOwnerMapsToClearMessage` passes, and `Cloud.SendForSignatureNonOwnerUploadsNothing` shows the disabled path and the 403 path each upload zero bytes.

- [ ] Implement draft create and discard calls. Done when: `Cloud.EnvelopeDraftCreateAndDiscard` passes.
- [ ] Implement the prepare URL builder with the id check. Done when: `Cloud.PrepareUrlBuiltFromValidatedId` and `Cloud.PrepareUrlIsSameOriginHttps` pass. Cheaper substitute: opening whatever URL the response carries.
- [ ] Implement the command flow and bound-version reuse. Done when: `AppTests.SendForSignatureHappyPath` and `Cloud.SendForSignatureReusesBoundVersion` pass.
- [ ] Implement failure paths without duplicate uploads. Done when: `Cloud.SendForSignatureNoDuplicateUpload` passes.
- [ ] Implement the draft `InfoBar` with `Open again`, `Copy link` and `Discard draft`, all with automation names and tooltips. Done when: the accessibility inventory lists them with zero missing names and `AppTests.SendForSignatureDiscardCallsApi` passes.
- [ ] Implement the owner-only enablement, explanation and `NotOwner` mapping. Done when: the three owner tests of step 6 pass. Cheaper substitute: leaving the command enabled and showing a bare `403`.
- [ ] Add a note in the preview that certificate-based signatures are made locally with `Sign` (`D05 T02 §10`) and that this service creates simple electronic signatures. Done when: `AppTests.SendForSignaturePreviewStatesSignatureKind` finds the sentence.
- [ ] Commit: `app: add Send for signature with upload preview and web field placement hand-off`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Cloud.Envelope*:Cloud.PrepareUrl*:Cloud.SendForSignature*:Cloud.NotOwner*:AppTests.SendForSignature*` passes on the display-session host, and building the URL from the response's `web_url` instead of the id fails `Cloud.PrepareUrlBuiltFromValidatedId`.

## 10. `Start a shared review` from the open document

A shared review is Acrobat's Send for comments: reviewers open the document in a browser or in Recto and their comments appear for everyone. On the desktop this section only creates the review and shows how to invite people; comments flow in §11. It must show exactly what will be uploaded and who will be invited, validate addresses locally, and be reversible (close the review) without touching the document.

**Phase:** 98
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` section 9.9 (dialog family) and reusing `CloudUploadDialog` (§5); no screenshot exists.
**Job:** A user names a review, adds reviewer email addresses (optional), sees what will be uploaded, starts the review, and gets a link to copy and a button to open it in the browser. Consumer: §11 live sync and the review web page.
**Treatment:** `cloud.startReview` opens a `StartReviewDialog` with a name box, an email list (add, remove, paste several, duplicates merged), an optional deadline, and the §5 facts including the invitee count; confirming uploads (or reuses the bound version), creates the review, binds the tab (`CloudBinding.review_id`), and shows the result with `Copy link`, `Open in browser` and `Close review`. Cheaper substitute that fails the checkpoint: a link-only review with no preview, or email addresses accepted without validation.
**Chrome:** `CloudUploadDialog` (§5), `RectoDialogStyle` (`D02 T01 §10`), stock `TextBox`, `ListView` and `CalendarDatePicker`, Segoe Fluent Icons `E8F2` (review) and `E71B` (link).

**Build order:**

1. Implement `CloudClient::create_review(document_id, version, name, invitees[], deadline)` (`POST /v1/reviews`, every invitee a reviewer), `close_review(review_id)` (`POST /v1/reviews/:id/close`) and `get_review_annotations(review_id)` (`GET /v1/reviews/:id/annotations?format=json`, with the `seq` header) (§4) against the shared reviews API. Done when: `Cloud.ReviewCreateCloseSnapshot` passes against the scripted server.
2. Implement address handling in `src/services/cloud/invitee_list.h/.cpp`: trim, lowercase the domain, validate against an addr-spec subset (one `@`, a dotted domain, no spaces or control characters, local part at most 64 and total at most 254 characters), merge duplicates ignoring case, split pasted text on commas, semicolons, spaces and newlines, and cap at 50 invitees per review (the service limit). Done when: `Cloud.InviteeListValidation` covers valid, invalid, duplicate, pasted-multi and 51-address input and `Cloud.InviteeListHasNoNetwork` records zero requests.
3. Author `src/app/StartReviewDialog.xaml/.h/.cpp` with the name box (default the sanitized tab title), the list, the optional deadline (not in the past), and the §5 facts. Done when: `AppTests.StartReviewDialogValidatesInput` shows `Start review` stays disabled for an empty name and for an invalid address and enables otherwise.
4. Implement the command: preview, upload or reuse the bound version, create the review, bind the tab, show the result panel with `Copy link` (clipboard), `Open in browser` (built from the validated review id and the configured origin, like §9 step 2, at the review room path of `D09 T03 §13`) and `Close review`; `Close review` calls `close_review` after confirmation, stops sync (§11) and keeps the document and every received comment in the tab. Done when: `AppTests.StartReviewHappyPath` and `AppTests.CloseReviewKeepsCommentsAndDocument` pass.
5. Make the command safe to repeat: a second `Start a shared review` on a tab already in a review offers `Open the current review` rather than creating another. Done when: `AppTests.StartReviewTwiceOffersExisting` passes.
6. Handle the owner-only rule (`D09 T03 §4`: only the document owner may start a review or invite; other roles get `403 not_owner`): for a tab bound to a cloud document, `cloud.startReview` is enabled only when the caller's role from `GET /api/documents/:id` is owner, otherwise it stays visible but disabled with "Only the owner of this document can start a review. Save a copy to your own library first."; an unbound tab uploads a new document the user owns and is allowed; a `403 not_owner` that still arrives maps to `NotOwner` with the same wording. Done when: `AppTests.StartReviewDisabledForNonOwner` covers view, comment and edit roles, `Cloud.NotOwnerMapsToClearMessage` passes, and `Cloud.StartReviewNonOwnerUploadsNothing` shows both paths upload zero bytes.

- [ ] Implement the review calls. Done when: `Cloud.ReviewCreateCloseSnapshot` passes.
- [ ] Implement `InviteeList`. Done when: `Cloud.InviteeListValidation` and `Cloud.InviteeListHasNoNetwork` pass. Cheaper substitute: accepting any non-empty string as an address.
- [ ] Implement `StartReviewDialog` with `AutomationProperties.Name`, tooltips and `x:Uid` strings. Done when: `AppTests.StartReviewDialogValidatesInput` passes and the accessibility inventory lists it with zero missing names.
- [ ] Implement the command flow and binding. Done when: `AppTests.StartReviewHappyPath` passes.
- [ ] Implement `Close review` and the repeat guard. Done when: `AppTests.CloseReviewKeepsCommentsAndDocument` and `AppTests.StartReviewTwiceOffersExisting` pass.
- [ ] Implement the owner-only enablement, explanation and `NotOwner` mapping. Done when: the three owner tests of step 6 pass. Cheaper substitute: leaving the command enabled and showing a bare `403`.
- [ ] Export the review's comments with the existing `Export Comments (JSON)` action after close. Done when: `AppTests.ClosedReviewCommentsExportAsJson` passes through the `D03 T03 §2` serializer.
- [ ] Commit: `app: add Start a shared review with validated invitees and preview`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Cloud.Review*:Cloud.InviteeList*:Cloud.StartReview*:AppTests.StartReview*:AppTests.CloseReview*:AppTests.ClosedReview*` passes on the display-session host, and accepting `not-an-address` fails `Cloud.InviteeListValidation`.

## 11. Live review comments in the Comments panel

While a tab is bound to a review, comments made by anyone appear in the existing `Comments / Annotations` panel within seconds, and the user's own comments reach the others, through the same annotation `JSON` model as file exchange (`D03 T03 §2`) so a review round-trips into the PDF. Remote content is hostile input: it must never run actions, grow without bound, or block the UI thread. Sync runs only while the tab is open and the user is signed in, and it must not add a second comments surface.

**Phase:** 98
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 8.3 (`Comments / Annotations` side panel); the sync status chip and the reviewer name on cards are a genuinely new addition on that chrome; no screenshot exists.
**Job:** A reviewer or owner sees other people's comments and replies appear in the panel and on the page without refreshing, can reply, and can see whether they are live, reconnecting or offline. Consumer: `CommentsPanel` (`D03 T03 §5`), `DocumentView` rendering, and the web room that orders edits.
**Treatment:** A `ReviewSyncService` on a worker thread holds a WebSocket (§1) to the review room, converts room messages to annotation operations through the `D03 T03 §2` `JSON` reader, applies them to the document through a `DocumentActor` message, and sends local create, edit, reply and delete operations with client operation ids; the panel header shows a status chip (`Live`, `Reconnecting`, `Offline, N changes waiting`). Cheaper substitute that fails the checkpoint: polling `GET` every few seconds, a separate reviews list beside the Comments panel, or applying remote comments on the UI thread.
**Chrome:** `CommentsPanel` and `SidePanelHost` (`D03 T03 §5`, `D02 T03 §3`), card and chip brushes from `RectoColors.xaml`, Segoe Fluent Icons `E895` (sync), `E783` (offline), `AnnouncementService` for polite notices (`D03 T02 §5`).

**Build order:**

1. Author `src/services/cloud/review_sync.h/.cpp` and `review_protocol.h/.cpp` following `cloud/packages/annotations/src/protocol.ts` (`D09 T03 §5`, `§7`) as the only source of the wire format: connect with the §1 WebSocket to `/v1/reviews/:id/socket` for the bound review id and send `hello {lastSeq, pendingOpIds}`; send `op`, `ping` and (optionally) `presence`; receive `welcome {seq, mode, chunks}` where `mode` is `current`, `replay` or `state`, then `chunk {index, kind, items}` messages of kind `ops` or `state`, then `chunk_end {seq, sha256}`, plus `ack {opId, seq}`, live `op`, `presence` and `error`. Every received message is checked against the size caps in encoded bytes before parsing (server messages at most 56 KiB, and the client never sends more than 64 KiB per message), and an unknown type, an out-of-order chunk `index` or an oversize message closes the socket and reconnects. Operation kinds are `add`, `edit`, `move`, `reply`, `resolve`, `reopen`, `delete` and `restore`, each with an `opId`, a `commentId` and annotation `JSON` in the `D03 T03 §2` model; local operations use fresh UUID `opId`s, the room's `seq` order is authoritative and replaying an `opId` is harmless because the room answers with the original `ack`. Done when: `Cloud.ReviewSyncAppliesRemoteInOrder`, `Cloud.ReviewSyncSendsLocalWithOpIds` and `Cloud.ReviewSyncRejectsOversizeServerMessage` pass against the scripted room.
2. Validate every incoming operation as hostile with the limits of the cloud annotation package (`D09 T03 §1`): the annotation must parse through the bounded `D03 T03 §2` reader returning `PdfResult`, text at most 3,000 characters, an annotation at most 40 KiB, at most 2,000 annotations and 8 MiB of annotation JSON per review, reply depth at most 8, at most 10,000 points per ink list, subtype limited to the 15 supported types, and every `/A` action, `Link` annotation, URI, `Launch`, `JavaScript` and `/AA` entry removed before apply. Done when: `Cloud.ReviewOpsStripActionsAndLinks`, `Cloud.ReviewOpsRejectOversize` (one unit over each limit) and `Cloud.ReviewOpsRejectUnknownSubtype` pass and `fuzz/fuzz_review_ops.cpp` runs 5 minutes under `/fsanitize=fuzzer,address` over `tests/corpus/seeds/cloud/` with no finding.
3. Apply remote operations through a new `DocumentActor` message `ApplyRemoteAnnotationOps` that creates, edits or deletes annotations with `/AP` synthesis (`D03 T03 §2`), pushes nothing onto the undo stack (`D03 T03 §4`) and does not set the tab dirty, so a remote comment is never an unsaved local change and never blocks closing; the next save includes them. Done when: `Cloud.RemoteOpsDoNotDirtyOrUndo` and `AppTests.RemoteCommentAppearsInPanelAndPage` pass.
4. Send local operations from the panel and annotation tools: create, edit, reply and delete on a bound tab emit one operation each after the local undoable edit, and an undo emits exactly the inverse in the undo table of `D09 T03 §6` and `§7` (`cloud/packages/annotations/src/undo.ts`), mirrored in `src/services/cloud/review_undo_table.h` and never anything else: undo of add or reply sends `delete` (as the author); undo of delete sends `restore` for the same `commentId` (the author or the review owner, within the room's 7 day window, after which the room answers `gone`); undo of edit sends `edit` with the previous text after a warning that it overwrites any later edit by someone else (last writer wins); undo of resolve sends `reopen` and undo of reopen sends `resolve`; undo of move sends `move` back to the previous rectangle; every inverse gets a fresh `opId`, never a reused one. When the room answers `forbidden` or `gone`, the undo is reported as "Could not undo: the comment changed or can no longer be restored" and the local undo stack stays consistent, never stuck. Done when: `AppTests.LocalCommentReachesRoom`, `AppTests.UndoSendsOnlyTableOperations` (each undo kind against the scripted room, with a test that fails on any operation kind or `opId` outside the `D09 T03 §6` table), `AppTests.UndoDeleteSendsRestoreSameCommentId`, `AppTests.UndoEditWarnsBeforeOverwrite` and `AppTests.UndoRejectedByRoomIsReported` (covering `forbidden` and `gone`) pass.
5. Implement reconnection and catch-up: on disconnect show `Reconnecting`, retry with exponential backoff 1 s to 30 s with jitter, and send `hello` with the last applied `seq` and the `opId`s of local operations not yet acknowledged. For `mode: current` nothing is applied. For `replay` and `state` the client buffers the `chunk` items in `index` order, computes the SHA-256 over the concatenated chunk bytes as `protocol.ts` defines, and applies nothing until `chunk_end` arrives and the hash matches; on a mismatch, a missing or repeated `index` or a disconnect mid-transfer it discards the whole result and sends `hello` again (at most 3 immediate retries, then the normal backoff). A verified `replay` applies its operations in `seq` order; a verified `state` replaces the review's annotations atomically through one `DocumentActor` message. Live `op` messages that arrive during catch-up are the server's to queue until `chunk_end` (the room closes the socket past 1,000 queued operations and the client reconnects); the client applies them in `seq` order after the result and drops any whose `seq` is not above the result's. Buffered local operations (cap 1,000, then `Offline, N changes waiting` and local edits continue normally) are sent only after catch-up completes, in order. On `SessionExpired` stop sync with a sign-in prompt. Done when: `Cloud.ReviewSyncReconnectsAndResumes`, `Cloud.ReviewSyncStateReplacesAtomically`, `Cloud.ReviewSyncDropsResultOnHashMismatch`, `Cloud.ReviewSyncBuffersLocalWhileOffline` and `Cloud.ReviewSyncStopsOnSessionEnd` pass.
6. Add the status chip, reviewer names on cards, and polite announcements (`New comment from <name> on page <n>`, coalesced to at most one per 5 seconds). Done when: `AppTests.ReviewStatusChipStates` covers `Live`, `Reconnecting` and `Offline, N changes waiting`, and `AppTests.ReviewAnnouncementsAreCoalesced` passes.
7. Stop cleanly: close the socket and release the worker when the tab closes, the review closes or the user signs out, and never reopen after sign-out. Done when: `Cloud.ReviewSyncStopsOnTabCloseAndSignOut` passes and a signed-out session shows zero WebSocket handles.

- [ ] Implement `ReviewSyncService` and the protocol codec from `protocol.ts`. Done when: the three connect, order and size tests of step 1 pass.
- [ ] Replay the shared protocol transcripts: read `cloud/packages/annotations/fixtures/protocol/*.jsonl` (reconnect with replay, reconnect with state, a remote `restore`, a dropped chunk) and `cloud/packages/annotations/protocol.schema.json` directly from the repository, validate every message against the schema, and feed the server side of each transcript to the client through a fake socket, comparing the client's resulting annotation set and outgoing messages with the transcript's expectations. Done when: `Cloud.ReviewSyncReplaysProtocolFixtures` passes for all four transcripts, `Cloud.ReviewSyncMessagesMatchSchema` passes, and deleting a transcript or editing a hash in the dropped-chunk one fails them. Cheaper substitute: hand-written desktop copies of the messages, which drift from the server.
- [ ] Implement the hostile-input validator and the fuzz target `fuzz/fuzz_review_ops.cpp` with seeds. Done when: the three `Cloud.ReviewOps*` tests pass and the 5 minute fuzz run is clean.
- [ ] Add the `ApplyRemoteAnnotationOps` actor message with no undo and no dirty. Done when: `Cloud.RemoteOpsDoNotDirtyOrUndo` passes. Cheaper substitute: applying remote annotations on the UI thread through the local edit commands.
- [ ] Emit local operations and table-only undo operations from the annotation tools and panel. Done when: the local-operation and undo `AppTests` pass. Cheaper substitute: sending a new `add` or an `edit` to a deleted comment to undo a delete.
- [ ] Implement reconnect, hash-verified catch-up and the offline buffer. Done when: the five step 5 tests pass.
- [ ] Add the status chip, names and announcements with `AutomationProperties.Name` on the chip. Done when: the two status `AppTests` pass and the accessibility inventory lists the chip with zero missing names.
- [ ] Add a UI-thread budget test: 500 remote operations in one second never block the UI thread for more than 50 ms. Done when: `AppTests.ReviewBurstNeverBlocksUi` passes.
- [ ] Implement the clean stop. Done when: `Cloud.ReviewSyncStopsOnTabCloseAndSignOut` passes.
- [ ] Commit: `app: sync shared review comments live into the Comments panel`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Cloud.ReviewSync*:Cloud.ReviewOps*:Cloud.RemoteOps*:AppTests.RemoteComment*:AppTests.LocalComment*:AppTests.UndoSends*:AppTests.UndoDelete*:AppTests.UndoEdit*:AppTests.UndoRejected*:AppTests.ReviewStatus*:AppTests.ReviewAnnouncements*:AppTests.ReviewBurst*` passes on the display-session host, leaving a `/A` `URI` action on a remote annotation fails `Cloud.ReviewOpsStripActionsAndLinks`, and applying a `state` result before `chunk_end` fails `Cloud.ReviewSyncDropsResultOnHashMismatch`.

## 12. Zero network before sign-in, token privacy and redirect safety suite

The promises of this file are negative ones (nothing leaves, nothing leaks, nothing is followed), and a negative promise needs a test that watches. This section adds the suite that fails when any cloud code path makes a request before sign-in, writes a token anywhere but Credential Manager, or lets a redirect carry credentials. It also proves the opt-in is real for users who never touch the feature.

**Phase:** 98
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Add `tests/recto-tests/cloud/test_zero_network.cpp`: start the app shell and the CLI with no session, open, save, convert, print, search and use every command in `Ctrl+K` for a scripted scenario, with the transport factory counter (§1), a Winsock connect hook and a DNS-resolution hook recording attempts. Done when: `Cloud.ZeroNetworkBeforeSignIn` asserts zero transports constructed, zero sockets and zero name lookups for the whole scenario, and `Cloud.NoCloudCommandsWhenSignedOut` shows only `cloud.signIn` is registered visible.
2. Prove the signed-in session is quiet too: sign in, then idle and use only local features. Done when: `Cloud.SignedInIdleIsSilent` records zero requests until the first cloud command and `Cloud.StartupDoesNoNetworkWhenSignedIn` shows a relaunch with a stored session sends nothing at startup.
3. Add the token scan: sign in with sentinel tokens, run every flow of §7 to §11, write the crash bundle (`D07 T02 §4`) and the diagnostics block (`D07 T03 §2`), dump the D16 ring buffer and the ETW test session, and scan every file under `%LOCALAPPDATA%\Recto`, the portable `data\` folder, the clipboard text and the process command line for the sentinels and for the refresh token. Done when: `Cloud.TokensNeverLeakToDiskLogsOrDiagnostics` finds none, and the diagnostics block contains no cloud file name, document id or account label (`Cloud.DiagnosticsHaveNoCloudIdentity`).
4. Add the redirect and origin matrix: for every endpoint the client calls and for the WebSocket, make the fake server answer `301`, `302`, `303`, `307` and `308` to another origin that records requests. Done when: `Cloud.NoRedirectEverFollowed` shows each returns `UnexpectedRedirect` and the other origin recorded zero requests and zero `Authorization` headers.
5. Add the source guards: no `http://` literal in `src/services/cloud/` except the loopback redirect string in `oauth_flow.cpp`, no `SECURITY_FLAG_IGNORE`, no token-bearing log call. Done when: `Cloud.SourceGuards` greps and fails on a planted violation of each kind.
6. Prove Portable mode: with `portable.mode` present, sign in, then confirm zero Registry writes (the `D07 T01 §7` harness), no session file in `data\`, and a signed-out state after the folder is copied to a clean profile. Done when: `Cloud.PortableModeKeepsSessionOutOfFolder` passes.

- [ ] Write the zero-network scenario with the connect and DNS hooks. Done when: `Cloud.ZeroNetworkBeforeSignIn` and `Cloud.NoCloudCommandsWhenSignedOut` pass. Cheaper substitute: asserting only that the cloud service object is null.
- [ ] Write the signed-in silence tests. Done when: the two silence tests pass.
- [ ] Write the token leak scan and the diagnostics identity scan. Done when: the two scan tests pass and planting a token in the ring buffer fails them.
- [ ] Write the redirect matrix. Done when: `Cloud.NoRedirectEverFollowed` passes.
- [ ] Write the source guards. Done when: `Cloud.SourceGuards` passes and each planted violation fails it.
- [ ] Write the Portable test. Done when: `Cloud.PortableModeKeepsSessionOutOfFolder` passes.
- [ ] Wire the suite into CI so a failure blocks a merge. Done when: `.github/workflows` runs `--gtest_filter=Cloud.*` and a deliberately failing test turns the job red.
- [ ] Commit: `tests: prove zero cloud network before sign-in and no token or redirect leaks`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Cloud.ZeroNetworkBeforeSignIn:Cloud.NoCloudCommandsWhenSignedOut:Cloud.SignedInIdleIsSilent:Cloud.StartupDoesNoNetworkWhenSignedIn:Cloud.TokensNeverLeakToDiskLogsOrDiagnostics:Cloud.DiagnosticsHaveNoCloudIdentity:Cloud.NoRedirectEverFollowed:Cloud.SourceGuards:Cloud.PortableModeKeepsSessionOutOfFolder` exits 0, and constructing a transport at startup fails `Cloud.ZeroNetworkBeforeSignIn`.

## 13. Record domain, sender domain and data region decisions

The service cannot launch until the owner chooses the public domain, the email sender domain and the data region (design: open owner decisions). This section records each decision once, in a ledger the rest of the launch reads, and adds the checker that later sections run. It must not guess a domain or a region: until the owner answers, the section stays out of `query ready`.

**Phase:** 98
**Needs:** owner-decision
**Surface:** no

**Build order:**

1. Author `docs/cloud-launch.md` as the launch ledger: one table per topic (`Domain`, `Email sender`, `Data region`, `Seal certificate`, `Legal`, `Production`, `Monitoring`, `Status and abuse`), each row with `Item`, `Status` (`open`, `done`), `Owner`, `Decided on`, `Evidence` (a file path, URL or receipt reference, never a secret) and `Cost of changing`. Done when: `python scripts/check-cloud-launch.py ledger` exits 0 on the file with all rows `open` and exits 2 when a table or column is missing.
2. Author `scripts/check-cloud-launch.py` (stdlib Python) with subcommands `ledger` (structure), `decisions` (domain, sender domain, region rows done with evidence), `seal`, `legal`, `deploy`, `status`, `smoke --origin <https origin>` and `go-live` (all rows done), each exiting 2 with the first missing item, plus `tests/scripts/test_check_cloud_launch.py`. Done when: `python -m unittest tests.scripts.test_check_cloud_launch` exits 0 and covers a complete ledger, a missing evidence cell, a `done` row naming a non-existent file, and an evidence cell that looks like a secret (a long base64 string or a `-----BEGIN` marker).
3. Record the owner's answers: the public domain and the email sender domain (a subdomain the owner controls), with the SPF, DKIM and DMARC records created at the DNS host, and the data region (Cloudflare default global storage or R2 and D1 restricted to the EU jurisdiction); the quota, retention, expiry and terms-version numbers are recorded by `D09 T02 §21` and `D09 T04 §19`, so this ledger does not ask for them twice, and `D09 T01 §21` reads the three answers recorded here when it fills the production block of `cloud/apps/api/wrangler.jsonc`. Done when: `python scripts/check-cloud-launch.py decisions` exits 0 and the ledger rows are `done` with evidence paths such as `docs/cloud/dns-records.txt` that contain record names and no keys.
4. Remove the `owner-decision` Need from this section and from `§15` only after the answers are in the ledger. Done when: `python scripts/todo-graph.py query ready` lists `D09 T05 §13` or the section is `done`, and `python scripts/todo-graph.py validate` exits 0.

- [ ] Add `docs/cloud-launch.md` with every row `open` and a `Cost of changing` for each. Done when: `python scripts/check-cloud-launch.py ledger` exits 0.
- [ ] Add `scripts/check-cloud-launch.py` and its unit tests. Done when: `python -m unittest tests.scripts.test_check_cloud_launch` exits 0.
- [ ] Ask the owner for the domain, sender domain and region decisions in one batched question and record each with its date. Done when: the three `Domain`, `Email sender` and `Data region` rows are `done` with evidence.
- [ ] Create the sender domain DNS records (SPF, DKIM, DMARC) and record the record names, not any key material. Done when: `python scripts/check-cloud-launch.py decisions` exits 0.
- [ ] Add the checker to the scripted tests in CI. Done when: `.github/workflows` runs `python -m unittest discover -s tests/scripts` and the new test file is collected.
- [ ] Commit: `docs: add the Recto Cloud launch ledger and its checker`

**Test checkpoint:** `python -m unittest tests.scripts.test_check_cloud_launch` exits 0 and `python scripts/check-cloud-launch.py decisions` exits 0 once the owner has answered, and a ledger row marked `done` with a missing evidence file makes the command exit 2.

## 14. Obtain the seal certificate from an AATL CA for an HSM or signing service

The service seals every completed signature request with a PAdES signature, and Adobe Reader shows it as trusted only when the certificate chains to the Adobe Approved Trust List (design: the seal certificate). The seal signer code and its staging validation are `D09 T04 §14` and `§15`; this section is the business step they wait on: choosing the CA and the signing service, passing identity validation and obtaining the certificate, with the private key generated and held in a cloud HSM or signing service and never in a Worker secret, a repository file or a log. Until the certificate is issued, staging seals with a test certificate that validators show as untrusted. It must not store key material anywhere under version control.

**Phase:** 98
**Needs:** third-party, operator
**Surface:** no

**Build order:**

1. Choose and record the provider in `docs/cloud-launch.md` (`Seal certificate` table): a CA on the AATL that sells a document-signing certificate with a cloud-HSM or remote-signing option, the signing service's API, signing latency and rate limits, signing cost per operation, the certificate's validity period and the renewal mechanism (a long-lived certificate and an automated renewal, never a rotation alarm). Done when: the ledger rows `Provider`, `Certificate type` and `Key custody` are `done` with the order or contract reference as evidence.
2. Complete the CA's identity validation as the operator (organization validation documents, a signed subscriber agreement), have the key generated inside the HSM or signing service, receive the certificate chain, and record the certificate's SHA-256 thumbprint, issuer, validity dates and the CA's AATL listing evidence. Done when: the ledger row `Issued` is `done` with the thumbprint and a link to the CA's AATL confirmation, and the secret scan in CI finds no private key or PIN in the repository.
3. Hand the result to the signer work: record the provider decision in `docs/specs/2026-10-21-recto-cloud-design.md` as `D09 T04 §15` step 1 requires, and give the operator the signing-service credential through the platform secret store by name only. Done when: the ledger row `Handed to the seal signer` is `done` and `python scripts/check-cloud-launch.py seal` exits 0.
4. Record the renewal path: the CA's automated renewal or a documented reissue procedure the operator can run in one sitting, with no calendar alarm (the signer logs `seal_cert_days_remaining` and refuses an expired certificate, `D09 T04 §15`). Done when: the ledger row `Renewal path` is `done`.

- [ ] Choose the provider and record custody, cost and limits. Done when: the three provider rows are `done`.
- [ ] Complete identity validation and receive the certificate chain. Done when: `Issued` is `done` with the thumbprint.
- [ ] Record the decision in the design spec and hand the credential name to the signer work. Done when: `python scripts/check-cloud-launch.py seal` exits 0. Cheaper substitute: using the staging test certificate in production, which validators show as untrusted.
- [ ] Record the renewal path. Done when: `Renewal path` is `done`.
- [ ] Commit: `docs: record the AATL seal certificate procurement`

**Test checkpoint:** `python scripts/check-cloud-launch.py seal` exits 0, and clearing the thumbprint evidence from the `Issued` row makes it exit 2.

## 15. Terms, privacy policy, ESIGN disclosure and DPA reviewed by counsel

Signatures, stored documents and email make Recto Cloud a service that handles other people's legal records, so its terms of service, privacy policy, the ESIGN consumer consent disclosure and the data processing agreement must be reviewed by counsel before launch. Recto does not write legal text as final: drafts are prepared for the lawyer, and the published versions carry the reviewer's approval. The ESIGN consent disclosure and the signer legal texts are authored and versioned by `D09 T04 §20`; this section sends them to counsel in the same engagement as the other three and records one ledger entry for all four. The privacy policy must match what the code does (no telemetry on the desktop, logs without document content, deletion and export) and the ESIGN disclosure must match the signing ceremony.

**Phase:** 98
**Needs:** owner-decision, third-party
**Surface:** no

**Build order:**

1. Prepare the drafts for counsel in `docs/cloud/legal/` as plain Markdown (`terms-of-service.md`, `privacy-policy.md`, `dpa.md`, `acceptable-use.md`), and reference the ESIGN checklist and consent text of `D09 T04 §20` (`cloud/docs/legal/esign-checklist.md`), each draft beginning with a "Facts the lawyer must check" list drawn from the code: what is stored, for how long (the retention defaults recorded in §13), where (the region recorded in §13), who the sub-processors are (Cloudflare, SMTP2GO, the seal signing service from §14), how deletion and export work, who with account or storage privileges can technically read stored PDFs (the service holds them unencrypted, so the policy must not claim operator-blind storage), and the desktop's opt-in rule. Done when: each draft exists and `python scripts/check-cloud-launch.py legal` lists the four as `drafted` and finds the `D09 T04 §20` checklist.
2. Send the drafts to the owner's counsel and record the reviewer, the review date and the version reviewed in the ledger (`Legal` table, one row each for terms, privacy policy, ESIGN disclosure and DPA), with the lawyer's approval as evidence kept outside the repository and referenced by a receipt id. Done when: the four `Legal` rows are `done`, each with a reviewer name, date and receipt reference.
3. Publish the approved texts on the web app's legal routes and link them from the sign-in page, the signature ceremony's consent step and the desktop `RECTO CLOUD` settings group (§6) as plain `https` links to the configured origin. Done when: `Cloud.SettingsLinksToLegalPages` shows the group offers the terms and privacy links and `python scripts/check-cloud-launch.py legal` fetches each published page and exits 0 only when each returns 200 and contains its approved version marker.
4. Record how users receive material changes to the terms (email notice and a version bump the sign-in flow shows), and the DPA process for business customers. Done when: the ledger rows `Change notice` and `DPA process` are `done`.

- [ ] Write the four drafts with their "Facts the lawyer must check" lists. Done when: `python scripts/check-cloud-launch.py legal` reports four `drafted` rows.
- [ ] Obtain counsel's review of the four documents and record it. Done when: the four rows are `done` with reviewer, date and receipt.
- [ ] Publish the approved texts at stable routes and add the version markers. Done when: the `legal` subcommand's fetch passes against the production origin.
- [ ] Link the legal pages from the desktop settings group. Done when: `Cloud.SettingsLinksToLegalPages` passes.
- [ ] Record the change-notice and DPA processes. Done when: the two rows are `done`.
- [ ] Commit: `docs: publish the counsel-reviewed Recto Cloud legal documents`

**Test checkpoint:** `python scripts/check-cloud-launch.py legal` exits 0 with the approved version markers found on the published pages, and publishing a page without its marker makes it exit 2.

## 16. Production verification, content-free monitoring and acceptance run

The platform file's deploy job (`D09 T01 §21`) creates production, applies migrations and installs secrets once the owner's domain, region and sender are known; this section proves the result is safe to open to the public and records it in the launch ledger. Production is the same code as staging, monitored so the operator knows the service is healthy from metrics and logs that carry no document content. This does not mean the operator cannot reach documents: the service stores plaintext PDFs in R2 for rendering, export and sealing (design: Architecture), so anyone with storage or deployment privileges on the owner's Cloudflare account could read them, and the privacy policy of §15 must say so rather than claim otherwise. What is enforced is narrower: logs, metrics, alerts and the status page never carry document content, and access to the account is limited to named operators. It must not log document content, file names of other users, email bodies or tokens (design: security and privacy), must keep secrets out of the repository, and must be revertible without a hand-edited step.

**Phase:** 98
**Needs:** owner-decision, third-party, operator
**Surface:** no

**Build order:**

1. Record the production resources `D09 T01 §21` created for the region recorded in §13 (D1 database, R2 buckets, Durable Object namespaces, Queues, Cron Triggers, domain route) by name and id, never credentials, in the ledger `Production` table, with the CI run URL of the deploy, the migration rollback command and the restore drill date. Done when: `python scripts/check-cloud-launch.py deploy` exits 0 with every `Production` row `done`.
2. List the production secrets (the SMTP2GO key for the sender domain of §13, the seal signing credentials of §14, the session and OAuth signing keys) by name and storage location only. Done when: `python scripts/check-cloud-launch.py deploy` confirms each secret row has no value and the secret scan in CI is clean.
3. Add content-free monitoring: uptime checks on `/api/health` and the web app, alerts on error rate, queue depth, email bounce rate, Durable Object errors and seal failures, and structured logs that carry request ids, event types and status codes only (the platform logger of `D09 T01 §5`). Done when: `python scripts/check-cloud-launch.py smoke --origin <origin>` fetches `/api/health`, verifies TLS, `Strict-Transport-Security`, a restrictive `Content-Security-Policy` and no `Set-Cookie` without `HttpOnly; Secure`, and exits 0; and the platform's canary log test (`cloud/apps/api/test/log.test.ts`) passes in CI against the production configuration with a canary document and a canary file name found in no log line.
4. Run the full acceptance pass against production with a canary account: sign in from the desktop (§3), save and open a document (§7, §8), run a shared review across the desktop and a browser (§11), complete a signature request, and validate the sealed PDF in Adobe Acrobat Reader and in the independent validator of `D09 T04 §17`, expecting a trusted chain to the certificate of §14. Done when: the ledger row `Acceptance run` is `done` with the run date, the desktop build number, the canary account's pseudonymous id and the validators' versions and screenshots under `docs/cloud/evidence/` as evidence, and `python scripts/check-cloud-launch.py seal` re-run exits 0 only when those screenshots exist.

- [ ] Record the production resources, deploy run, rollback and restore drill in the ledger. Done when: the `Production` rows are `done`.
- [ ] List the production secrets by name only. Done when: the secrets row is `done` and the secret scan is clean.
- [ ] Add monitoring and the smoke command. Done when: `python scripts/check-cloud-launch.py smoke --origin <origin>` exits 0 and a unit test of the checker shows it exits 2 when the CSP header is removed.
- [ ] Run the platform canary log test against the production configuration. Done when: `log.test.ts` passes in CI.
- [ ] Run the end-to-end acceptance pass and record it. Done when: `Acceptance run` is `done`. Cheaper substitute: accepting the staging run as the production proof.
- [ ] Commit: `docs: record the Recto Cloud production verification and monitoring`

**Test checkpoint:** `python scripts/check-cloud-launch.py deploy` and `python scripts/check-cloud-launch.py smoke --origin <the production origin>` both exit 0, and a response missing `Strict-Transport-Security` makes `smoke` exit 2.

## 17. Status page, abuse contact and security disclosure

A service that stores other people's documents and sends signature emails needs a place to say when it is down, an address to report abuse (phishing through signature requests is the obvious one), and a route for security reports. These must exist before launch, work without a Recto account, and be linked from the web app, the emails and the desktop settings group. It must not depend on the production service being up for the status page to load.

**Phase:** 98
**Needs:** owner-decision, third-party, operator
**Surface:** no

**Build order:**

1. Stand up a status page on infrastructure independent of the production Worker (a separate provider or a static page on another host), fed by the monitors of §16 manually or automatically, and record its URL. Done when: the ledger row `Status page` is `done` and `python scripts/check-cloud-launch.py status` fetches the URL and exits 0 only on a 200 from a host different from the production origin.
2. Create the abuse contact: a monitored mailbox on the owner's domain (for example `abuse@` on the domain recorded in §13), a `Report abuse` link in every signature request email and on the signer and reviewer pages (the service files own those pages and read the address from configuration), and a written response target in the ledger. Done when: the ledger rows `Abuse mailbox` and `Response target` are `done` and a test message to the mailbox is recorded as received.
3. Publish `/.well-known/security.txt` (RFC 9116) on the production origin with a contact, expiry, preferred languages and the policy link, and add the cloud scope to the repository's `SECURITY.md` (`D07 T03 §1`). Done when: `python scripts/check-cloud-launch.py status` fetches `security.txt` and exits 0 only when the `Contact` and `Expires` fields parse and `Expires` is in the future.
4. Link the status page, abuse address and security policy from the desktop `RECTO CLOUD` group (§6) and from the legal pages of §15. Done when: `Cloud.SettingsLinksToStatusAndAbuse` passes and the linked pages return 200.
5. Write the incident runbook in `docs/cloud/incident-runbook.md`: who is paged, how to pause signature request sending, how to take a document or account offline for abuse, how to post to the status page, and how to notify affected users, with no secrets. Done when: `python scripts/check-cloud-launch.py status` finds the runbook and exits 0 only when each of the five headings is present.

- [ ] Stand up the status page and record it. Done when: the `Status page` row is `done`.
- [ ] Create the abuse mailbox and the response target. Done when: the two abuse rows are `done`.
- [ ] Publish `security.txt` and extend `SECURITY.md`. Done when: the `status` subcommand's `security.txt` check passes.
- [ ] Add the desktop links. Done when: `Cloud.SettingsLinksToStatusAndAbuse` passes.
- [ ] Write the incident runbook. Done when: the runbook heading check passes.
- [ ] Commit: `docs: add the Recto Cloud status page, abuse contact and incident runbook`

**Test checkpoint:** `python scripts/check-cloud-launch.py status` exits 0, and an expired `Expires` date in `security.txt` makes it exit 2.

## 18. Go-live gate: desktop default server, parity ledger and release note

The last step connects the finished service to the shipped app. Until now the desktop's default server address is empty or points at staging, so a released build cannot reach the public service by accident. This gate refuses go-live until every launch row is done and every desktop promise is proven, then sets the default server, confirms the Acrobat parity ledger rows for cloud tools, and writes the release note. It must not add any default-on network behaviour: the desktop still asks before every upload and stays silent until the user signs in.

**Phase:** 98
**Needs:** owner-decision, third-party, operator
**Surface:** no

**Build order:**

1. Run `python scripts/check-cloud-launch.py go-live`, which exits 2 naming the first ledger row that is not `done` (domain, sender, region, seal, legal, production, monitoring, status, abuse, acceptance run). Done when: the command exits 0.
2. Set `RECTO_CLOUD_DEFAULT_ORIGIN` in `src/services/cloud/cloud_config.h` to the production origin recorded in §13 (an `https` origin with no path), leaving the build-time override used by staging and self-hosters. Done when: `Cloud.DefaultOriginIsValidHttps` passes and `Cloud.ZeroNetworkBeforeSignIn` still passes with the production origin set.
3. Confirm the Acrobat parity ledger `docs/acrobat-parity.md` rows for `Request e-signatures (Acrobat Sign)`, `Shared review, Send for comments` and `Document Cloud storage, sharing links` reference `D09 T01` to `D09 T05` and read `Planned, phase 98` until this file's sections are stamped, then `Done` (the coordinator edits the ledger). Done when: `python scripts/todo-graph.py validate` exits 0 and `python -m unittest tests.scripts.test_todo_graph` exits 0 with the ledger rows present.
4. Write the cloud section of the release notes in `docs/release-notes/`: what Recto Cloud is, that it is opt-in and silent until sign-in, how to self-host, the AGPL licence of `cloud/`, and links to the terms, privacy policy, status page and abuse address. Done when: `Release.NotesMentionCloudOptIn` finds the opt-in sentence and the four links.
5. Run the clean-machine smoke (`D06 T03 §12`) on a profile that never signs in and record zero cloud network calls in the release candidate. Done when: the release candidate's smoke report lists `cloud network calls: 0`.

- [ ] Run the go-live gate. Done when: `python scripts/check-cloud-launch.py go-live` exits 0.
- [ ] Set the default origin and keep the override. Done when: `Cloud.DefaultOriginIsValidHttps` passes.
- [ ] Confirm the parity ledger rows. Done when: `python scripts/todo-graph.py validate` exits 0.
- [ ] Write the release note section. Done when: `Release.NotesMentionCloudOptIn` passes.
- [ ] Record the never-signed-in smoke. Done when: the smoke report lists `cloud network calls: 0`.
- [ ] Commit: `cloud: set the default server and record the Recto Cloud go-live gate`

**Test checkpoint:** `python scripts/check-cloud-launch.py go-live` exits 0 and `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Cloud.DefaultOriginIsValidHttps:Cloud.ZeroNetworkBeforeSignIn:Release.NotesMentionCloudOptIn` exits 0, and leaving any ledger row `open` makes `go-live` exit 2.

## 19. Account assets API: encrypted signature appearance and stamp storage

Acrobat keeps a user's saved signatures and stamps with their account so they follow the user to another computer. Recto Cloud does the same for the two things that are only pictures or text: saved signature and initials appearances (the drawn, typed or image appearances of `D05 T02 §10`) and custom stamps (`D03 T03 §20`). This section is the service half, in `cloud/apps/api`. It must never store a private key, certificate, PKCS#12 file or password (those stay in the Windows certificate store and the desktop signer, `D05 T01 §8`), must encrypt each asset at rest under a key owned by the account, and must be honest in its documentation: the service holds the wrapping key, so this protects stored objects from a storage-only leak and from other accounts, and it does not make the service blind to them.

**Phase:** 98
**Needs:** none
**Surface:** no

**Build order:**

1. Add the schema and routes in `cloud/apps/api/src/assets/`: table `account_assets` (`id`, `user_id`, `kind` in `signature`, `initials`, `stamp`, `name` up to 80 characters without control characters, `revision` integer, `size`, `deleted`, `updated_at`) in a forward-only migration, and routes `GET /api/account/assets` (manifest, cursor-paged), `GET /api/account/assets/:id/content`, `PUT /api/account/assets/:id` (requires `If-Match: <revision>`, or `If-None-Match: *` to create; a stale revision returns `409 revision_conflict` with the current revision) and `DELETE /api/account/assets/:id` (tombstone with revision bump). Done when: route tests cover create, update, stale update, delete, resurrect-after-delete conflict and another user's id returning 404.
2. Add the OAuth scope `assets` to the client registry of `D09 T01 §14` and require it on these routes with `requireScope('assets')`; the web session needs no scope. Done when: a token with only `documents` gets `403 insufficient_scope`.
3. Enforce limits: at most 5 MiB per asset (the stamp image limit of `D03 T03 §20`), 64 stamps and 32 signature or initials appearances per account, 50 MiB total, content types limited to `image/png`, `application/pdf` (image stamps only, header check as `D09 T02 §3`), and `application/json` for drawn-ink strokes, typed signatures and text or dynamic stamps (depth 16, 256 KiB), with a magic-byte check and no parsing beyond the header and the JSON bound; reject anything else with `unsupported_type`. Done when: each limit has a test one unit over, a ZIP renamed `.png` is refused, and an asset whose JSON names a certificate, key or password field is refused with `forbidden_content`.
4. Encrypt at rest: a random 256-bit data key per account, wrapped with AES-256-GCM under the platform secret `ASSET_KEK` (key id stored with the wrapped key so the secret can be changed by re-wrapping), each asset stored in the `BlobStore` (`D09 T01 §4`) at `assets/<userId>/<assetId>/<revision>` as AES-256-GCM with a fresh 96-bit nonce and additional data `userId|assetId|revision`; plaintext never touches R2 or D1. Done when: tests show the stored object contains neither the PNG signature bytes nor the JSON text, a swapped object from another asset or revision fails authentication, and re-wrapping under a new `ASSET_KEK` keeps every asset readable.
5. Register the data exporter and deletion hook with `D09 T01 §15` (assets export as their original files plus a manifest; account deletion removes objects, rows and the wrapped key after the grace period) and write audit events `asset.created`, `asset.updated`, `asset.deleted` with ids and kinds only. Done when: the export test finds a seeded asset byte-identical and the audit rows hold no name or content.
6. Document exactly what is protected in `cloud/docs/security/assets.md` and in the privacy facts of `D09 T05 §15`: encrypted per account at rest, not end-to-end encrypted, readable by the service when a request is authorised and by anyone who holds both the storage and the `ASSET_KEK` secret. Done when: the document exists and states what an operator with storage access only, and with storage plus the secret, can and cannot read.

- [ ] Add the migration, repository and routes with `If-Match` revision rules. Done when: `npm --prefix cloud run test:api -- test/assets/routes` passes.
- [ ] Add the `assets` scope and its test. Done when: the scope test passes.
- [ ] Add the limits and content checks, including the forbidden-field scan. Done when: `test/assets/limits.test.ts` passes. Cheaper substitute: trusting the file extension.
- [ ] Add envelope encryption with wrapped per-account keys and re-wrap. Done when: `test/assets/crypto.test.ts` passes the tamper, swap and re-wrap cases. Cheaper substitute: relying on R2 default encryption alone.
- [ ] Register export, deletion and audit. Done when: `test/assets/lifecycle.test.ts` passes.
- [ ] Write `cloud/docs/security/assets.md`. Done when: the file states the operator's reach without claiming blindness.
- [ ] Commit: `cloud/api: encrypted per-account signature appearance and stamp storage`

**Test checkpoint:** `npm --prefix cloud run test:api -- test/assets` exits 0; storing an asset unencrypted, or accepting a JSON asset with a `privateKey` field, makes the crypto and limits tests fail.

## 20. Signature and stamp sync across devices, opt-in

A user with Recto on two computers should not redraw their signature and rebuild their stamps on each. This is the desktop half of §19, matching Acrobat's saved signatures that follow the account (D18). It is off by default, exists only while signed in, and syncs only the signature and initials appearances (`D05 T02 §10`) and the stamp library (`D03 T03 §20`), never certificates, private keys, PKCS#12 files or passwords. Local files stay the working copy and stay usable offline; sync never blocks the UI thread, never runs in the background without a user action, and never overwrites a local item without keeping it.

**Phase:** 98
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` section 9.7 (settings rows) and the Stamp Manager and signature picker chrome of `D03 T03 §20` and `D05 T02 §10`; no screenshot exists.
**Job:** A signed-in user turns on sync after reading what it will send, sees their signatures and stamps appear on another computer when they open the picker or press Sync now, and can turn it off and remove the online copies. Consumer: `StampLibrary`, the signature appearance library, and the §19 API.
**Treatment:** A `Sync signatures and stamps` row in the `RECTO CLOUD` group (§6) with a consent dialog listing the item kinds, counts and sizes that will upload and the sentence that certificates and keys never leave the PC, a `Sync now` button, a last-synced line, per-library `InfoBar` status in the Stamp Manager and signature picker, and `Remove synced copies`. Cheaper substitute that fails the checkpoint: a default-on toggle, a sync that runs on a timer in the background, or one that also uploads the certificate chooser state.
**Chrome:** `RectoDialogStyle` (`D02 T01 §10`), the §6 group styles, `InfoBar` severities and card brushes from `RectoColors.xaml`, Segoe Fluent Icons `E895` (sync) and `E74D` (remove), automation names and tooltips on icon-only controls.

**Build order:**

1. Add the setting `cloud.syncAssets` (default `false`) to `SettingsStore` (`D02 T04 §2`) and make it ignored, with the row disabled, when no session exists; signing out leaves local libraries untouched and stops sync. Done when: `Settings.AssetSyncOffByDefault` and `Settings.AssetSyncDisabledWhenSignedOut` pass.
2. Author `src/services/cloud/asset_sync.h/.cpp` over `CloudClient` (§4, extended with the §19 routes and the `assets` scope requested at sign-in, §3): build a local manifest from the two libraries (id, kind, name, content hash, local revision, last synced revision stored in `AppPaths::data_dir()\cloud-sync.json`, containing no token), diff it with `GET /api/account/assets`, and apply: upload new or changed items with `If-Match`, download new or changed remote items, propagate deletions as tombstones. Runs only on a worker thread when the user enables sync, presses `Sync now`, opens the Stamp Manager or the signature picker, or saves a change to either library; never at startup and never on a timer. Done when: `Cloud.AssetSyncDiffsAndApplies`, `Cloud.AssetSyncRunsOnlyOnUserActions` and `Cloud.AssetSyncStartupIsSilent` pass.
3. Resolve conflicts without loss: a `409 revision_conflict`, or an item edited on both sides since the last sync, keeps both (the remote copy is stored locally as `<name> (from another device)`) and tells the user; a deletion on one side and an edit on the other keeps the edited item. Done when: `Cloud.AssetSyncKeepsBothOnConflict` and `Cloud.AssetSyncEditBeatsDelete` pass.
4. Treat downloaded items as hostile: images decode through WIC with a 5 MiB and 8192 by 8192 pixel bound, PDF stamps open in a job-owned `recto::pdf::Document` and are refused if they contain actions or scripts, JSON is parsed with the bounded reader, the 64 stamp and 32 appearance limits of the local libraries are enforced, and names are sanitized as in §7. Local writes go through the libraries' own temp-file-then-replace saves. Done when: `Cloud.AssetSyncRejectsHostileDownloads` and `Cloud.AssetSyncWritesAtomically` pass.
5. Guarantee nothing secret syncs: the manifest builder reads only the two libraries' item files, and a source and content scan fails if the sync code references the certificate store, `PKCS12`, `PFX` or a password field. Done when: `Cloud.AssetSyncNeverTouchesKeysOrCertificates` passes and adding a certificate reference to `asset_sync.cpp` fails it.
6. Build the consent dialog, the setting row, `Sync now`, status `InfoBar`s and `Remove synced copies` (deletes every remote asset after a confirmation, leaves local items, and turns sync off). Done when: `AppTests.AssetSyncConsentListsKindsAndCounts`, `AppTests.AssetSyncStatusAndSyncNow`, `AppTests.AssetSyncRemoveCopiesDeletesRemoteKeepsLocal` and `AppTests.AssetSyncNeverBlocksUi` pass.
7. Cover Portable mode and errors: the sync state file lives in the portable `data\` folder in Portable mode, a copied portable folder arrives signed out (no sync until sign-in), and every failure shows `describe` text with Retry (`NetworkUnreachable`, `SessionExpired`, `QuotaExceeded` with the used and limit figures). Done when: `Cloud.AssetSyncPortableStateHasNoToken` and `AppTests.AssetSyncShowsTypedErrors` pass.

- [ ] Add the setting and its signed-out behaviour. Done when: the two `Settings.AssetSync*` tests pass.
- [ ] Implement `AssetSync` with the diff, apply and user-action triggers. Done when: the three step 2 tests pass.
- [ ] Implement conflict handling. Done when: the two step 3 tests pass.
- [ ] Implement hostile-download validation and atomic writes. Done when: the two step 4 tests pass and `fuzz/fuzz_asset_json.cpp` runs 5 minutes under `/fsanitize=fuzzer,address` with no finding.
- [ ] Add the key and certificate source scan. Done when: `Cloud.AssetSyncNeverTouchesKeysOrCertificates` passes. Cheaper substitute: a code comment saying keys are excluded.
- [ ] Build the consent dialog, row, `Sync now`, statuses and removal with `AutomationProperties.Name`, tooltips and `x:Uid` strings. Done when: the four step 6 `AppTests` pass and the accessibility inventory lists the controls with zero missing names.
- [ ] Render the surfaces in Light, Dark and HighContrast with token brushes only. Done when: `AppTests.AssetSyncThemes` finds no literal colour in the new XAML. Cheaper substitute: a hex value on a control.
- [ ] Add the Portable and error-state tests. Done when: the two step 7 tests pass.
- [ ] Commit: `app: sync signature appearances and stamps across devices, opt-in`

**Test checkpoint:** `build\bin\x64\Release\recto-tests\recto-tests.exe --gtest_filter=Settings.AssetSync*:Cloud.AssetSync*:AppTests.AssetSync*` passes on the display-session host, and making the sync run at startup fails `Cloud.AssetSyncStartupIsSilent`.

## Verification

- A Recto build with no sign-in makes zero Recto Cloud network calls and exposes only `Sign in to Recto Cloud`, proven by `Cloud.ZeroNetworkBeforeSignIn` and `Cloud.NoCloudCommandsWhenSignedOut` (§12).
- A user can sign in through the browser with PKCE and a loopback redirect, open a cloud document, edit it, save it back with a conflict dialog when someone else saved first, send it for signature after a preview and land on the web field-placement page, start a shared review, and see a reviewer's comment appear in the Comments panel, all against the scripted server in `Cloud.*` and `AppTests.*`.
- No token appears in settings, the Portable folder, logs, the crash bundle, diagnostics, a URL or the clipboard, and no redirect is ever followed, proven by `Cloud.TokensNeverLeakToDiskLogsOrDiagnostics` and `Cloud.NoRedirectEverFollowed`.
- `python scripts/check-cloud-launch.py go-live` exits 0 only after the domain, region, seal certificate, counsel-reviewed legal documents, production deployment, monitoring, status page and abuse contact are recorded with evidence in `docs/cloud-launch.md`.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
