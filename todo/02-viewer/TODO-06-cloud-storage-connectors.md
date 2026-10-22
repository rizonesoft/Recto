---
schema_version: 1
id: cloud-storage-connectors
domain: 02-viewer
status: active
title: "TODO-06 -- Cloud Storage Connectors: OneDrive, SharePoint, Google Drive, Dropbox and Box"
---

# TODO-06 -- Cloud Storage Connectors: OneDrive, SharePoint, Google Drive, Dropbox and Box

> **Goal:** Opt-in, off-by-default connectors (D21) that let a user browse, open, save and save a copy of PDFs in OneDrive, SharePoint, Google Drive, Dropbox and Box. Sign-in is OAuth 2.0 authorization code with PKCE through the system browser and a loopback redirect; refresh tokens live in Windows Credential Manager; a downloaded file opens as a temporary local copy like any other; an upload is conditional on the provider's version token so a newer remote file is never overwritten. When the user never signs in, or Group Policy disables connectors, no request is ever made.

> **Current state:** Measured 2026-10-21. Nothing exists. `D02 T01 §5` and `§8` provide `OpenDocumentPipeline` and `SavePipeline`, `D02 T01 §10` the dialog chrome, `D02 T03 §2` and `§12` Home and the command box, `D02 T04 §2` and `§6` the settings store and the `Performance and privacy` group. The assistant's HTTPS transport (`D08 T01 §2`) refuses every redirect and cannot be reused for downloads that redirect, so this file has its own client. The Group Policy templates and the `SettingsStore` policy layer are owned by `todo/07-release/TODO-02-store-and-compliance.md`; this file reads the policy name `CloudConnectorsDisabled` through an interface. 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); sections rewritten against `todo/10-ui/`.

## Inputs

- `AGENTS.md` architecture rules (UI thread never blocks, atomic writes, links ask first, tokens and Fluent UI System Icons (`RectoIcons`), `Ctrl+K` reachability), foundation plan D16 (offline by default, no telemetry), D18 and D21.
- RFC 7636 (PKCE) and RFC 8252 (OAuth 2.0 for native apps: system browser, loopback redirect); each provider's current developer documentation for endpoints, scopes and conflict handling, checked while building each provider section.
- `design/SPEC.md` sections 4, 5, 6, 7, 9.1, 9.7, 9.9, 10 and 11; `design/tokens/colors.json` and the generated `src/ui/generated/recto_tokens.g.h`.
- Builds on D02 T01 §5: `OpenDocumentPipeline` opens the downloaded temporary copy (§12 here).
- Builds on D02 T01 §8: `SavePipeline` writes the local save before the upload (§13 here).
- Builds on D02 T01 §13: `SessionStore` excludes cloud temporary copies.
- Builds on D02 T01 §17: sharing a cloud-origin document shares a fresh copy.
- Builds on D02 T04 §6: the Cloud storage card (§5 here) sits in the `Performance and privacy` group.
- Builds on D08 T01 §2: the assistant transport is deliberately not reused (redirect rule).
- Builds on D10 T02 §17: the sign-in, browser and conflict dialogs are `ui::Dialog` (§3, §11, §13 here).
- Builds on D10 T02 §19 and §7: the Cloud storage card is a `ui::SettingsCard` with a `ui::ToggleSwitch` (§5 here).
- Builds on D10 T02 §11: the cloud browser list is a virtualized `ui::ItemsView` (§11 here).
- Builds on D10 T02 §16, §8, §3 and §20: the waiting and progress cards are `ui::Card` with `ui::ProgressBar`, tab tooltips are `ui::ToolTip` and the cloud tab badge is `ui::Badge` (§3, §12 here).
- Builds on D10 T02 §21: cloud recents appear in the `ui::SearchBox` command box results (§14 here).
- Builds on D10 T01 §3: transfer completions reach the UI thread through `ui::Dispatcher` (§12 here).
- Builds on D10 T03 §1 and §2: automation names and patterns come from `ui::Element` automation properties and the UIA providers (§3, §11 here).
- -> XREF: D07 T02 §10 -- CloudConnectorsDisabled policy.
- -> XREF: D99 T01 §9 -- the operator registers the five OAuth apps, records the public client IDs in `config/cloud-clients.json` and proves live sign-in, browse, open and save for each provider built in `§6` to `§10`.
- -> XREF: D02 T04 §6 -- cloud storage card.
- -> XREF: D07 T02 §4 -- crash bundle scan.
- -> XREF: D08 T01 §2 -- separate transport.
- -> XREF: D02 T01 §13 -- session exclusion of cloud copies.
- -> XREF: D10 T02 §17 -- the sign-in, browser and conflict dialogs build on ui::Dialog.
- -> XREF: D10 T02 §19 -- the Cloud storage card builds on ui::SettingsCard.
- -> XREF: D10 T02 §11 -- the cloud browser list builds on ui::ItemsView.
- -> XREF: D10 T02 §16 -- the waiting and progress cards build on ui::Card.
- -> XREF: D10 T02 §21 -- cloud recents appear in the ui::SearchBox command box.
- -> XREF: D10 T01 §3 -- transfer completions marshal through ui::Dispatcher.
- -> XREF: D10 T03 §1 -- automation names and the UIA tree for the cloud dialogs.
- -> XREF: D06 T04 §22 -- the `D-Cloud.png` board and `design/SPEC.md` section 9.17 that §3, §5, §11, §12, §13, §14 here build against.

## Outcome

- With connectors off (the default) or on but never signed in, Recto issues no network request to any provider or identity host, and the menus show no cloud entries while off.
- A signed-in user browses a provider, opens a PDF as a temporary local copy, edits it, and saves back with a conditional upload; a changed remote file produces a conflict dialog and never an overwrite.
- Tokens exist only in Windows Credential Manager (refresh) and memory (access); sign-out deletes them; Group Policy `CloudConnectorsDisabled` locks the feature off.

**Adjacency:** list=applicable @ D02 T06 §11; document=applicable @ D02 T06 §13; settings=applicable @ D02 T06 §5; reporting=not-applicable (no reports; transfer failures show in the dialogs of §12 and §13); notifications=applicable @ D02 T06 §12; permissions=applicable @ D02 T06 §3; audit=applicable @ D02 T06 §15; exchange=applicable @ D02 T06 §13; reverse=applicable @ D02 T06 §4

**Adjacency rationale:** The list surface is the cloud browser (§11) and the recents on Home (§14). The document surface is saving to the cloud (§13). Settings are the Cloud storage card (§5). Notifications are the transfer cards and failure messages (§12). Permissions are the explicit sign-in consent (§3). Audit is the local proof that no request happens before sign-in (§15), not a persisted log. Exchange is the upload and download of the file (§12, §13). Reverse is sign-out, which deletes tokens and recents (§4). Reporting does not apply: no report is produced.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Provider-neutral connector interface, errors and the network gate | D02 T04 §2 | [ ] |
| 2 | §2 | HTTPS client for connectors with same-provider redirects only | §1 | [ ] |
| 3 | §3 | OAuth 2.0 authorization code with PKCE through a loopback redirect | §2, D02 T01 §10, D02 T02 §9, D10 T02 §16, D10 T02 §17, D10 T03 §1, D06 T04 §22 | [ ] |
| 4 | §4 | Token store in Windows Credential Manager, refresh and sign-out | §3 | [ ] |
| 5 | §5 | Cloud storage settings group: off by default, per-provider sign-out, policy lock | §1, §4, D02 T04 §6, D10 T02 §7, D10 T02 §19, D06 T04 §22 | [ ] |
| 6 | §6 | OneDrive provider over Microsoft Graph | §2, §3, §4, §5 | [ ] |
| 7 | §7 | SharePoint document libraries over Microsoft Graph | §6 | [ ] |
| 8 | §8 | Google Drive provider over Drive API v3 | §2, §3, §4, §5 | [ ] |
| 9 | §9 | Dropbox provider over API v2 | §2, §3, §4, §5 | [ ] |
| 10 | §10 | Box provider over API 2.0 | §2, §3, §4, §5 | [ ] |
| 11 | §11 | Open from cloud: provider and folder browser | §1, §3, D02 T01 §9, D02 T01 §10, D02 T03 §2, D02 T03 §11, D10 T01 §13, D10 T02 §11, D10 T02 §17, D10 T02 §18, D10 T03 §1, D06 T04 §22 | [ ] |
| 12 | §12 | Open a cloud file as a temporary local copy | §11, D02 T01 §5, D02 T01 §11, D02 T01 §13, D02 T03 §8, D10 T01 §3, D10 T02 §3, D10 T02 §8, D10 T02 §16, D10 T02 §20, D06 T04 §22 | [ ] |
| 13 | §13 | Save and Save a copy to the cloud with conflict detection | §12, D02 T01 §8, D10 T02 §17, D06 T04 §22 | [ ] |
| 14 | §14 | Recent cloud files on Home and in the command box | §4, §12, D02 T03 §2, D02 T03 §12, D10 T02 §11, D10 T02 §21, D06 T04 §22 | [ ] |
| 15 | §15 | Privacy and security tests: zero requests until sign-in, no secrets on disk | §6, §7, §8, §9, §10, §13, §14, D07 T02 §4 | [ ] |

## 1. Provider-neutral connector interface, errors and the network gate

Every provider (OneDrive, SharePoint, Google Drive, Dropbox, Box) is reached through one interface so the browser, open, save and recents sections never name a provider. The same section owns the gate: until the user turns connectors on and signs in to a provider, no code in this file may open a socket. Foxit PDF Editor, PDFelement and UPDF integrate cloud storage; Acrobat Pro has its own cloud. Recto keeps offline by default (D16): this is opt-in and per provider.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `src/services/cloud/provider.h`: `ICloudProvider` with `id()`, `display_name()`, `list_folder(CloudFolderId, PageToken) -> CloudResult<CloudPage>`, `get_metadata(CloudItemId)`, `download(CloudItemId, ByteSink, CancelToken)`, `upload_replace(CloudItemId, ByteSource, VersionToken expected, CancelToken)`, `upload_new(CloudFolderId, name, ByteSource, CancelToken)`; `CloudItem { id, name, is_folder, size, modified_utc, version_token, mime }`; and `CloudError` codes `NotSignedIn`, `Disabled`, `Conflict`, `NotFound`, `QuotaExceeded`, `RateLimited`, `Network`, `TlsInvalid`, `Cancelled`, `Unsupported`, each with a plain user message. Done when: a fake provider implements the interface and every `CloudError` has a non-empty message in the string table.
2. Implement `CloudGate` (`src/services/cloud/cloud_gate.h/.cpp`) with `allowed(provider_id) -> GateDecision`: false unless `CloudConnectorsEnabled` is on and the policy source (`IPolicySource`, default: no policy) does not report `CloudConnectorsDisabled`, and then only for a request class that matches the provider's state: `Api` requests need a signed-in provider; `Authorize` requests (the loopback listener and the authorization-code exchange at the token endpoint) need an `AuthorizationInProgress` token that §3 obtains only after the user presses `Open browser` in the sign-in dialog, is bound to one provider, expires after 180 s, and permits only that provider's authorize and token hosts; `Refresh` requests (the refresh-token grant at the token endpoint) need the provider to hold a stored refresh credential (§4) and permit only its token host; and `Revoke` requests need a `SignOutInProgress` token that `sign_out` (§4) creates for one provider, which permits only its revocation host and expires after 30 s; every provider and the HTTP client (§2) must ask it before any request, and it keeps a counter of requests allowed and refused for the tests in §15. Done when: with connectors off, a provider call returns `Disabled` and the request counter stays 0; with connectors on and no sign-in or token, `Api`, `Authorize`, `Refresh` and `Revoke` requests are all refused; a stored refresh credential allows `Refresh` to the token host only, and a `SignOutInProgress` token allows `Revoke` to the revocation host only.

- [ ] Add a `CloudStorageRegistry` listing providers in a fixed order and returning only the signed-in ones to the UI. Done when: a fake registry with two signed-in providers lists exactly those two.
- [ ] Make `ByteSink` and `ByteSource` streaming interfaces with a 4 MiB chunk and cancellation so no file is held whole in memory. Done when: a 1 GB fake upload peaks under 16 MiB resident.
- [ ] Bound names (255 characters), folder depth shown (no limit but paged 200 at a time) and total pages listed per call (50). Done when: a hostile provider returning endless pages stops at the bound with a typed error.
- [ ] Add `Cloud.InterfaceAndGate` covering the error table, gate truth table (setting, policy, sign-in, stored refresh credential, authorization and sign-out tokens, the four request classes, expiry) and counters. Done when: `recto-tests.exe --gtest_filter=Cloud.InterfaceAndGate` passes. Cheaper substitute: provider code that checks the setting itself.
- [ ] Commit: `services: add the cloud connector interface and network gate`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.InterfaceAndGate` passes and a gate bypass added to a scratch provider fails `Cloud.GateRefusesWhenOff`.

## 2. HTTPS client for connectors with same-provider redirects only

Providers hand downloads to another host with a redirect (Graph and Box do), so the assistant transport, which refuses every redirect (`D08 T01 §2`), cannot be reused. Connectors get their own WinHTTP client with a narrower rule: HTTPS only, certificate errors never overridden, proxy settings honoured, redirects followed only to the provider's declared host suffixes, and the `Authorization` header never forwarded to a different host. No other file in `src/services/cloud/` may include `winhttp.h`.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/services/cloud/http_client.h/.cpp` over WinHTTP: `WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY`, connect 15 s, send 30 s, receive-idle 60 s and a total deadline per call, refusal of any non-HTTPS URL, validation of every request destination (initial URLs, redirects, pagination links such as `@odata.nextLink` and `nextPageToken` URLs, and resumable-upload session URLs) against the calling provider's `allowed_api_hosts` and `allowed_redirect_hosts` with the credential audience fixed to the original provider host, no `SECURITY_FLAG_IGNORE_*` use, streaming of request and response bodies in 4 MiB chunks, and a cancel watcher that closes the request handle within 500 ms. Done when: `Cloud.HttpRefusesPlainHttp`, `Cloud.HttpRejectsSelfSigned` and `Cloud.HttpCancelWithin500ms` pass against the test TLS server.
2. Follow at most 5 redirects, each only to an HTTPS host that matches the calling provider's `allowed_redirect_hosts` list, strip `Authorization` on any hop whose host differs from the original, and map every other redirect to `CloudError::Network` with the host named. Done when: a redirect to an unlisted host records zero requests, a listed cross-host redirect arrives without an auth header, and an allowed API response containing an attacker-host `nextLink` or upload-session URL is refused with zero requests to that host (`Cloud.HttpRefusesForeignServerUrls`).

- [ ] Call `CloudGate::allowed` (§1) before the first byte of every request and return `Disabled` without a socket when refused. Done when: `Cloud.HttpHonoursGate` shows a refused call opens no connection.
- [ ] Scrub bearer tokens, codes and `refresh_token` values from every log line and error text. Done when: `Cloud.HttpNeverLogsSecrets` finds none of a set of sentinel tokens in logs or error strings.
- [ ] Map HTTP 401, 403, 404, 409, 412, 429 (honouring `Retry-After`, at most 2 retries) and 5xx to `CloudError`. Done when: each status maps as listed.
- [ ] Add a source scan that only `http_client.cpp` includes `winhttp.h` and that `WINHTTP_OPTION_SECURITY_FLAGS` appears nowhere. Done when: `Cloud.NoTlsOverrideInSource` passes and fails when either is added to a scratch copy.
- [ ] Add the loopback TLS test server (a certificate not installed anywhere) to `tests/` for these tests. Done when: the server records requests and headers for assertions.
- [ ] Commit: `services: add the HTTPS client for cloud connectors`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.Http*:Cloud.NoTlsOverrideInSource` passes.

## 3. OAuth 2.0 authorization code with PKCE through a loopback redirect

Sign-in follows RFC 7636 (PKCE, S256) and RFC 8252 (native apps use the system browser and a loopback redirect), so Recto never sees the user's password and never embeds a web view. Recto asks first, naming the provider's host, before it opens the browser (links ask first). The loopback listener binds only to `127.0.0.1`, accepts one request, checks `state`, and closes.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.9 form dialog layout. Design board: `design/SPEC.md` section 9.17 with `design/screenshots/D-Cloud.png` (drawn by `D06 T04 §22`).
**Job:** A user signs in to a provider in their own browser and returns to Recto. Consumer: provider sections §6 to §10.
**Treatment:** System browser plus loopback listener. Cheaper substitute that fails the checkpoint: an embedded browser control, or a listener bound to all interfaces.
**Chrome:** `ui::Dialog` (`D10 T02 §17`), `ui::Card` (`D10 T02 §16`), `recto_tokens.g.h` tokens.

**Build order:**

1. Implement `src/services/cloud/oauth_pkce.h/.cpp`: generate a 64-byte `code_verifier` and its S256 `code_challenge` with `BCryptGenRandom` and `BCryptHash`, a random `state`, build the authorization URL from a provider descriptor (`authorize_url`, `token_url`, `scopes`, `client_id`, `extra_params`), listen on `127.0.0.1` with an OS-chosen port for one request (Winsock, 120 s timeout, first line only parsed, 8 KB cap), verify `state` in constant time, exchange the code at `token_url` through the §2 client under the gate's `AuthorizationInProgress` token (so first sign-in passes through the real gate, not a bypass), and return `OAuthTokens { access, refresh, expires_in, scope }`. Done when: against a fake identity server and the real `CloudGate` the full flow returns tokens, a request outside the provider's authorize and token hosts is refused during sign-in, a wrong `state` is refused, and a second connection to the listener is refused.
2. Show `SignInDialog` (a `ui::Dialog`, `D10 T02 §17`, with the `D02 T01 §10` dialog chrome) stating `Recto will open <host> in your browser to sign in to <provider>` with `Open browser` and `Cancel`, then open the URL with `ShellExecuteExW` (`open` verb, default browser), show a waiting `ui::Card` with `Cancel`, and return a friendly page to the browser (`You can close this tab and return to Recto`) with no scripts. Done when: cancelling closes the listener within 500 ms and nothing is stored.

- [ ] Handle `error=access_denied` and provider error pages without leaking the response into logs. Done when: a denied sign-in shows `Sign-in was cancelled` and stores nothing.
- [ ] Bind the listener to loopback only and fail closed if binding to `127.0.0.1` fails. Done when: `Cloud.OAuthListenerLoopbackOnly` shows no other interface accepts.
- [ ] Reject an authorization response that arrives after the timeout or twice. Done when: a late or repeated callback is ignored.
- [ ] Give the dialog and waiting card `ui::Element` automation names (`D10 T03 §1`) and keyboard access through `ui::FocusManager` (`D10 T01 §13`). Done when: the UIA tree lists both buttons with names.
- [ ] Add `Cloud.OAuthPkceFlow` covering challenge maths against RFC 7636 test vectors, state checks, timeouts, cancel and denial. Done when: `recto-tests.exe --gtest_filter=Cloud.OAuth*` passes. Cheaper substitute: an embedded web view or a client secret in the URL.
- [ ] Read provider client IDs from `config/cloud-clients.json` (`{"onedrive": {"client_id": ""}, "sharepoint": ..., "gdrive": ..., "dropbox": ..., "box": ...}`; public desktop-client IDs only, an empty value means `Not configured`) through `src/services/cloud/cloud_clients.h/.cpp`, and add `scripts/check-cloud-clients.py` rejecting any key named like a secret or a value that is not a plain client ID. Done when: `Cloud.ClientConfig` reads a fixture, reports an empty ID as `Not configured` and hides that provider, and `python scripts/check-cloud-clients.py` exits `2` on a planted `client_secret` key.
- [ ] Add `Cloud.OAuthFakeAuthorizationEndpoint`, driving the whole flow (authorization URL, loopback redirect, code exchange, refresh) against a fake authorization and token server on `127.0.0.1` that checks the `code_challenge` against the later `code_verifier`. Done when: the flow ends with fake tokens and the fake rejects a wrong `code_verifier`.
- [ ] Commit: `services: add OAuth PKCE sign-in through a loopback redirect`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.OAuth*` passes on the display-session host.

## 4. Token store in Windows Credential Manager, refresh and sign-out

Refresh tokens are secrets, so they live only in the Windows Credential Manager of the signed-in Windows account, one generic credential per provider (`Recto/Cloud/<provider>`); access tokens live in memory only and expire. Tokens never enter `SettingsStore`, the Portable data folder, logs, crash bundles or diagnostics. Sign-out deletes the credential and, where the provider has a revocation endpoint, asks it to revoke; offline, it still deletes locally and says the provider grant remains until revoked in the provider's account settings.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/services/cloud/token_store.h/.cpp` over `CredWriteW`, `CredReadW` and `CredDeleteW` with `CRED_TYPE_GENERIC` and `CRED_PERSIST_LOCAL_MACHINE`, an injectable `ICredentialBackend` for tests, `has_refresh(provider)`, `with_refresh(provider, callback)`, `set_refresh`, `remove`, and an in-memory `AccessTokenCache` that is zeroed on expiry and sign-out. Done when: `Cloud.TokenStoreRoundTrip` writes, reads, replaces and deletes a token per provider and leaves no credential behind.
2. Implement `TokenBroker::access_token(provider)`: return a cached unexpired token, otherwise refresh through the §2 client and §3 descriptors with at most one refresh in flight per provider, persisting a replacement refresh token whenever the response carries one (Box refresh tokens are single-use) before using the new access token, mapping `invalid_grant` to `NotSignedIn` and removing the dead refresh token; a refresh that completes after `sign_out` began discards its result and writes no credential: a per-provider mutex, which `sign_out` also holds while it increments the provider's generation and deletes the credential, is held across the generation check and `CredWriteW`, so no sign-out can run between them. Refresh goes through the gate as a `Refresh` request (§1) without reopening sign-in. Done when: two concurrent calls cause one refresh, two successive refreshes with a single-use fake token succeed across an app restart through the real `CloudGate`, an `invalid_grant` response signs the provider out, a refresh pending during `sign_out` leaves no credential, and `Cloud.RefreshSignOutRace` parks the refresh at a test barrier between the generation check and `CredWriteW`, runs `sign_out`, releases the barrier and finds no credential.

- [ ] Implement `sign_out(provider)`: take the provider mutex, increment the generation, create a `SignOutInProgress` gate token (§1), best-effort revoke through the real gate (never blocks the delete), delete the credential, zero the cache, clear the provider's recents (§14), and fire a settings change event. Done when: after sign-out `has_refresh` is false and the registry omits the provider.
- [ ] Scan `%LOCALAPPDATA%\Recto` and the Portable `data` folder after a run with sentinel tokens. Done when: `Cloud.TokensNeverOnDisk` finds none.
- [ ] Add `Cloud.TokenBrokerRefresh` and `Cloud.SignOutDeletes` covering expiry, concurrency, `invalid_grant`, revoke failure and offline sign-out. Done when: `recto-tests.exe --gtest_filter=Cloud.Token*:Cloud.SignOut*` passes. Cheaper substitute: tokens kept in a settings or JSON file.
- [ ] Commit: `services: store connector tokens in Credential Manager with refresh and sign-out`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.Token*:Cloud.SignOut*` passes.

## 5. Cloud storage settings group: off by default, per-provider sign-out, policy lock

The settings half of the opt-in. `CloudConnectorsEnabled` is off by default; with it off the app contains no cloud menu entries that issue requests and the gate (§1) refuses everything. Each provider shows its sign-in state with `Sign in` or `Sign out`. When Group Policy sets `CloudConnectorsDisabled` (value under `HKLM\Software\Policies\Recto`, defined by the managed-settings section in `todo/07-release/TODO-02-store-and-compliance.md`), the toggle is locked off with the note `Managed by your organization`. This file reads the policy through `IPolicySource` and does not parse the registry itself.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.7 (`Performance and privacy` group, Cloud storage card) with `design/screenshots/D-Settings.png`. Design board: `design/SPEC.md` section 9.17 with `design/screenshots/D-Cloud.png` (drawn by `D06 T04 §22`).
**Job:** A user decides whether cloud connectors exist at all, sees what is signed in and signs out. Consumer: `CloudGate` (§1), `TokenStore` (§4), `SettingsPage` (`D02 T04 §6`).
**Treatment:** The `recto-ui` `ui::ToggleSwitch` (`D10 T02 §7`) and `ui::SettingsCard` rows (`D10 T02 §19`) matching the Fluent look. Cheaper substitute that fails the checkpoint: a setting nothing reads, or a toggle that does not stop requests.
**Chrome:** `recto_tokens.g.h` tokens, Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Add `CloudConnectorsEnabled` (default false) to `RectoSettings` and a `Cloud storage` `ui::SettingsCard` in the `Performance and privacy` `ui::SettingsGroup` of the Settings page (`D02 T04 §6`, `D10 T02 §19`): the master toggle, a list of the five providers each with state (`Not signed in`, `Signed in as <account>`), `Sign in` and `Sign out`, a `Sign out of all` button, and the sentence `Recto contacts a provider only after you sign in, and only to open or save files you choose.` Done when: the card renders with the toggle off, the provider buttons disabled and no request issued.
2. Implement `PolicyAwareCloudGate` wiring `IPolicySource` to `SettingsStore`'s policy precedence once that exists; when `CloudConnectorsDisabled` is set, force the toggle off and disabled, sign out nothing automatically, and show `Managed by your organization`. Done when: a fake policy source makes the toggle locked and `CloudGate::allowed` false even with the setting on.

- [ ] Hide `Open from cloud`, `Save to cloud` and the cloud recents section while the master toggle is off. Done when: with the toggle off no cloud entry appears in menus, `Home` or `Ctrl+K`.
- [ ] Turning the toggle off signs out nothing but closes open connections and stops queued transfers with a cancel. Done when: an in-flight fake download is cancelled when the toggle turns off.
- [ ] Register `Cloud storage settings` in `CommandRegistry` and the Settings `Find a setting` index. Done when: searching `cloud` finds the card.
- [ ] Add `Settings.CloudCard` and `Cloud.PolicyLock` covering defaults, states, sign-out, policy lock and visibility rules. Done when: `recto-tests.exe --gtest_filter=Settings.CloudCard:Cloud.PolicyLock` passes. Cheaper substitute: a toggle that only hides the menu while requests remain possible.
- [ ] Commit: `viewer: add the Cloud storage settings card and policy lock`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Settings.CloudCard:Cloud.PolicyLock` passes on the display-session host.

## 6. OneDrive provider over Microsoft Graph

Foxit, PDFelement and UPDF open and save OneDrive files. This provider implements `ICloudProvider` (§1) for a personal or work OneDrive over Microsoft Graph with the Microsoft identity platform (authorization code with PKCE, public client with a loopback redirect, scopes `offline_access` and `Files.ReadWrite`; confirm the least-privilege scope names against Microsoft's current documentation when registering). Download follows Graph's redirect to a pre-authenticated URL without the bearer header (§2). Replace is conditional on the item's `eTag`. The provider is built and proven against a local fake of its API (a loopback HTTP server replaying recorded responses), with sign-in driven against a fake authorization endpoint. The client ID is a public value read from `config/cloud-clients.json` (a public desktop client, no secret in the repository); a provider whose client ID is empty is reported as `Not configured` and hidden from the cloud menu. Registering the app and proving a live sign-in is the operator section `D99 T01 §9`.

> **Groomed 2026-10-22:** removed `third-party`. The OneDrive provider is proven against a fake Graph server and the client ID comes from `config/cloud-clients.json`. Registering the OAuth app and proving live sign-in, browse, open and save moved to the operator section `D99 T01 §9` (owner decision 2026-10-22 (foundation plan section 1)).

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/services/cloud/onedrive_provider.h/.cpp`: descriptor (authorize and token URLs on `login.microsoftonline.com/common/oauth2/v2.0`, `allowed_redirect_hosts` starting with `graph.microsoft.com`, `.sharepoint.com`, `.1drv.com` and `.microsoftpersonalcontent.com`), `list_folder` over `/me/drive/root/children` and `/me/drive/items/{id}/children` with `@odata.nextLink` paging, `get_metadata`, `download` from `/content`, and `upload_new`/`upload_replace`: a simple `PUT` for files under 250 MB and a resumable upload session in 10 MiB-aligned chunks above that. Done when: against the fake Graph server list, download, small upload and chunked upload all round-trip a 300 MB generated file under the memory bound.
2. Make `upload_replace` send the expected `eTag` in `If-Match`, map `412 Precondition Failed` to `CloudError::Conflict`, and return the new `eTag` as the version token. Done when: the fake server's changed `eTag` produces `Conflict` and the file on the server is unchanged.

- [ ] Show the account name from `/me` on the settings row. Done when: the row shows the fake account.
- [ ] Build the fake Graph server fixture `tests/fakes/cloud/fake_graph_server` (a loopback HTTP server replaying recorded responses from `tests/corpus/cloud/graph/`, with paging, redirect, `eTag` and chunked upload behaviour). Done when: `Cloud.OneDriveProvider` runs entirely against it with no outside request.
- [ ] Author `docs/cloud-connectors.md` with a section per provider (registration steps, scopes, redirect rule, conflict method, known limits) and read the OneDrive client ID from `config/cloud-clients.json` (`§3`). Done when: the document has the OneDrive section and no credential is in the repository.
- [ ] Author the opt-in live smoke script `tests/live/cloud_onedrive.ps1` (never in CI) that signs in, lists, downloads and uploads a test file, with a `-Fake` mode that runs the same steps against the fake Graph server. Done when: `-Fake` passes in `Cloud.OneDriveLiveScriptFake`; the live run is recorded by the operator in `D99 T01 §9`.
- [ ] Add `Cloud.OneDriveProvider` against the fake server covering paging, redirect without auth header, conflict and chunking. Done when: `recto-tests.exe --gtest_filter=Cloud.OneDriveProvider` passes. Cheaper substitute: treating a 412 as a generic network error.
- [ ] Commit: `services: add the OneDrive provider over Microsoft Graph`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.OneDriveProvider` passes against the fake server; the live smoke run is recorded by the operator in `D99 T01 §9`.

## 7. SharePoint document libraries over Microsoft Graph

UPDF and Foxit open files in SharePoint libraries. This reuses §6's Graph client, sign-in and conditional replace, and adds site and library discovery: search or follow sites (`/sites?search=`), list a site's document libraries (`/sites/{id}/drives`), then browse a library by drive and item ids. Work or school accounts only. The provider is built and proven against a local fake of its API (a loopback HTTP server replaying recorded responses), with sign-in driven against a fake authorization endpoint. The client ID is a public value read from `config/cloud-clients.json` (a public desktop client, no secret in the repository); a provider whose client ID is empty is reported as `Not configured` and hidden from the cloud menu. Registering the app and proving a live sign-in is the operator section `D99 T01 §9`.

> **Groomed 2026-10-22:** removed `third-party`. SharePoint reuses the fake Graph server of `§6` and the same client ID entry. Registering the OAuth app and proving live sign-in, browse, open and save moved to the operator section `D99 T01 §9` (owner decision 2026-10-22 (foundation plan section 1)).

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/services/cloud/sharepoint_provider.h/.cpp` as a second `ICloudProvider` sharing `GraphClient` with §6: a root listing of followed sites and a search box result, a site listing its libraries, then the same folder, download and conditional replace calls addressed by drive id; ids are encoded as `siteId/driveId/itemId` and validated before use. Done when: against the fake Graph server a site, a library and a nested file open, download and conditionally upload.
2. Request the scopes SharePoint needs in addition to §6's (confirm names against current Microsoft documentation) and ask for them at sign-in time only when the user chooses SharePoint. Done when: signing in to OneDrive alone never requests the site scopes.

- [ ] Map `403 Forbidden` on a library to a message `You don't have access to this library` rather than a sign-in error. Done when: the fake 403 shows that message and stays signed in.
- [ ] Cache the site and library lists for the session only. Done when: no site list is written to disk.
- [ ] Add `Cloud.SharePointProvider` covering discovery, paging, id validation, scopes and conditional replace. Done when: `recto-tests.exe --gtest_filter=Cloud.SharePointProvider` passes. Cheaper substitute: a second hand-rolled Graph client.
- [ ] Commit: `services: add the SharePoint provider over Microsoft Graph`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.SharePointProvider` passes against the fake server; the live work-account run is `D99 T01 §9`.

## 8. Google Drive provider over Drive API v3

Foxit, PDFelement and UPDF open and save Google Drive files. This provider implements `ICloudProvider` over Drive API v3 with Google's OAuth for installed apps (authorization code with PKCE, loopback redirect). Browsing a user's whole Drive needs the restricted `https://www.googleapis.com/auth/drive` scope, which Google must verify (an operator step, `D99 T01 §9`); the narrower `drive.file` scope only sees files the app created, so it cannot browse. The installed-app flow may require a client secret that Google documents as non-confidential; repository rules still keep credentials out of tracked files, so it is injected at build time. Drive v3 has no documented conditional-write header on `files.update`, so this provider cannot meet the no-overwrite guarantee for replacing a file: it saves a changed file as a new copy (`upload_new`), and replaces in place only when an atomic condition is proven (see the build order). The provider is built and proven against a local fake of its API (a loopback HTTP server replaying recorded responses), with sign-in driven against a fake authorization endpoint. The client ID is a public value read from `config/cloud-clients.json` (a public desktop client, no secret in the repository); a provider whose client ID is empty is reported as `Not configured` and hidden from the cloud menu. Registering the app and proving a live sign-in is the operator section `D99 T01 §9`.

> **Groomed 2026-10-22:** removed `third-party`. The Drive provider is proven against a fake Drive server; the client ID comes from `config/cloud-clients.json` and any client secret from a build environment variable, never from the repository. Registering the OAuth app and proving live sign-in, browse, open and save moved to the operator section `D99 T01 §9` (owner decision 2026-10-22 (foundation plan section 1)).

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/services/cloud/gdrive_provider.h/.cpp`: descriptor (`accounts.google.com/o/oauth2/v2/auth`, `oauth2.googleapis.com/token`, `allowed_redirect_hosts` starting with `www.googleapis.com` and `.googleusercontent.com`), `list_folder` via `files.list` with a parent query, paging by `nextPageToken` and `fields` limited to id, name, mimeType, size, modifiedTime, version, `get_metadata`, `download` via `files.get?alt=media`, `upload_new` and `upload_replace` via the resumable upload endpoint in 8 MiB chunks, and Google-native documents shown as unsupported (export is not part of v1). Done when: against the fake Drive server list, download and chunked upload round-trip, and a Google Docs item shows `Not a PDF`.
2. Implement `upload_replace` as `Unsupported` unless the Drive API documents an atomic conditional write when the section is built (check the current reference for `If-Match` or revision preconditions and record the finding in `docs/cloud-connectors.md`); until then the save flow (§13) offers only `Save as a new file` and `Save a copy to cloud...` for Google Drive, and says why. Done when: `Cloud.GoogleDriveNoUnconditionalOverwrite` changes the fake remote file after open and before save, and the server file is never overwritten: the save flow offers only a new copy.

- [ ] Record in `docs/cloud-connectors.md` that v1 requests the full `drive` scope (a default taken because `drive.file` cannot browse), that Google's restricted-scope verification is required before release, and that changing to a narrower scope removes browsing. Done when: the document states the default, the verification need and the cost of changing it.
- [ ] Read the client ID from `config/cloud-clients.json` and any client secret from a build environment variable, and add `Cloud.NoSecretInSource`, which scans tracked files under `src/`, `config/` and `docs/` for a `client_secret` literal. Done when: the test passes and fails when a planted literal is added to a scratch copy.
- [ ] Hide shortcuts, shared drives and trashed files from the browse list in v1 and say so in the folder empty state. Done when: shortcut and trashed items are absent.
- [ ] Build the fake Drive server fixture `tests/fakes/cloud/fake_drive_server` replaying recorded Drive v3 responses from `tests/corpus/cloud/drive/` and author `tests/live/cloud_gdrive.ps1` (with a `-Fake` mode). Done when: `Cloud.GoogleDriveProvider` runs entirely against the fake with no outside request.
- [ ] Add `Cloud.GoogleDriveProvider` covering paging, MIME filtering, chunking and the version conflict. Done when: `recto-tests.exe --gtest_filter=Cloud.GoogleDriveProvider` passes. Cheaper substitute: using `drive.file` and calling it browsing.
- [ ] Commit: `services: add the Google Drive provider over Drive API v3`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.GoogleDriveProvider` passes against the fake server; the live run is `D99 T01 §9`.

## 9. Dropbox provider over API v2

UPDF, PDFelement and Foxit open and save Dropbox files. This provider implements `ICloudProvider` over Dropbox API v2 with authorization code and PKCE (`token_access_type=offline` for a refresh token). Uploads use `mode: update` with the last known `rev`, `autorename` false and `strict_conflict` true, so a stale `rev` is rejected with a conflict response that the provider maps straight to `Conflict`; a preliminary metadata read is a courtesy for the message, not the guard. The provider is built and proven against a local fake of its API (a loopback HTTP server replaying recorded responses), with sign-in driven against a fake authorization endpoint. The client ID is a public value read from `config/cloud-clients.json` (a public desktop client, no secret in the repository); a provider whose client ID is empty is reported as `Not configured` and hidden from the cloud menu. Registering the app and proving a live sign-in is the operator section `D99 T01 §9`.

> **Groomed 2026-10-22:** removed `third-party`. The Dropbox provider is proven against a fake Dropbox server and the client ID comes from `config/cloud-clients.json`. Registering the OAuth app and proving live sign-in, browse, open and save moved to the operator section `D99 T01 §9` (owner decision 2026-10-22 (foundation plan section 1)).

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/services/cloud/dropbox_provider.h/.cpp`: descriptor (`www.dropbox.com/oauth2/authorize`, `api.dropboxapi.com/oauth2/token`, `allowed_redirect_hosts` `.dropboxapi.com` and `.dropboxusercontent.com`), `list_folder` and `list_folder/continue` (cursor paging, `limit` 200), `get_metadata`, `download` via the content endpoint with the `Dropbox-API-Arg` header, `upload_new`, `upload_replace` via `files/upload` below 150 MB and the upload-session calls (`start`, `append_v2`, `finish`) above it. Done when: against the fake Dropbox server a 200 MB file uploads in chunks and downloads back identical.
2. Implement conflict detection by sending the expected `rev` in `mode: update` with `autorename` false and `strict_conflict` true on both `files/upload` and `upload_session/finish` (the commit carries the `mode`), mapping the conflict error to `Conflict`, and failing closed with `Conflict` if a response ever reports a path other than the requested one. Done when: a remote change made after the preliminary metadata read and before the commit yields `Conflict` for a small upload and for a session finish, and the server file is unchanged.

- [ ] Escape and validate the JSON in the `Dropbox-API-Arg` header, rejecting non-ASCII by the documented escaping. Done when: a file named with accented and CJK characters round-trips.
- [ ] Build the fake Dropbox server fixture `tests/fakes/cloud/fake_dropbox_server` replaying recorded API v2 responses from `tests/corpus/cloud/dropbox/` and author `tests/live/cloud_dropbox.ps1` (with a `-Fake` mode), reading the client ID from `config/cloud-clients.json`. Done when: `Cloud.DropboxProvider` runs entirely against the fake with no outside request.
- [ ] Show the account display name on the settings row. Done when: the row shows the fake account.
- [ ] Add `Cloud.DropboxProvider` covering paging, unicode names, chunking and both conflict paths. Done when: `recto-tests.exe --gtest_filter=Cloud.DropboxProvider` passes. Cheaper substitute: overwriting the server file when the `rev` differs.
- [ ] Commit: `services: add the Dropbox provider over API v2`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.DropboxProvider` passes against the fake server; the live run is `D99 T01 §9`.

## 10. Box provider over API 2.0

Foxit and PDFelement open and save Box files. This provider implements `ICloudProvider` over Box API 2.0 with authorization code and PKCE. Download follows Box's redirect to a download host (§2 allow-list); a new version of an existing file is uploaded with the `If-Match` header carrying the file's `etag`, and a `412` is a conflict. Box documents PKCE for public clients; if the registration instead requires a client secret that cannot be kept public, this section records the gap and ships Box disabled rather than weakening the sign-in. The provider is built and proven against a local fake of its API (a loopback HTTP server replaying recorded responses), with sign-in driven against a fake authorization endpoint. The client ID is a public value read from `config/cloud-clients.json` (a public desktop client, no secret in the repository); a provider whose client ID is empty is reported as `Not configured` and hidden from the cloud menu. Registering the app and proving a live sign-in is the operator section `D99 T01 §9`.

> **Groomed 2026-10-22:** removed `third-party`. The Box provider is proven against a fake Box server and the client ID comes from `config/cloud-clients.json`. Registering the OAuth app and proving live sign-in, browse, open and save moved to the operator section `D99 T01 §9` (owner decision 2026-10-22 (foundation plan section 1)).

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/services/cloud/box_provider.h/.cpp`: descriptor (`account.box.com/api/oauth2/authorize`, `api.box.com/oauth2/token`, `allowed_redirect_hosts` `api.box.com`, `upload.box.com` and `.boxcloud.com`), `list_folder` via `/2.0/folders/{id}/items` with `offset` and `limit` paging (root is folder `0`), `get_metadata`, `download` via `/2.0/files/{id}/content`, `upload_new` via the upload host, `upload_replace` as a new file version with `If-Match: <etag>`, and chunked upload sessions for files above 50 MB. Done when: against the fake Box server list, download, small upload, chunked upload and version replace all round-trip.
2. Map `412` to `Conflict` and `409` name collisions on `upload_new` to a typed `NameExists` result that the save flow (§13) turns into a rename prompt. Done when: both statuses map as listed.

- [ ] Record in `docs/cloud-connectors.md` that Box documents PKCE for public clients, that the provider sets `shipping=false` while `config/cloud-clients.json` has no Box client ID or the operator records that PKCE failed (`D99 T01 §9`), and that nothing weakens the sign-in to compensate. Done when: the doc records the documented behaviour, the live-verification owner and the disabled state.
- [ ] Show the account name on the settings row. Done when: the row shows the fake account.
- [ ] Build the fake Box server fixture `tests/fakes/cloud/fake_box_server` replaying recorded API 2.0 responses from `tests/corpus/cloud/box/` and author `tests/live/cloud_box.ps1` (with a `-Fake` mode), reading the client ID from `config/cloud-clients.json`. Done when: `Cloud.BoxProvider` runs entirely against the fake with no outside request.
- [ ] Add `Cloud.BoxProvider` covering paging, redirect without auth header, `412`, `409` and chunking. Done when: `recto-tests.exe --gtest_filter=Cloud.BoxProvider` passes. Cheaper substitute: forwarding the bearer token to the download host.
- [ ] Commit: `services: add the Box provider over API 2.0`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.BoxProvider` passes against the fake server; the live run is `D99 T01 §9`.

## 11. Open from cloud: provider and folder browser

One browser for all providers, as in Foxit's and UPDF's cloud dialogs: pick a signed-in provider, move through folders with a breadcrumb, see name, size and modified date, filter to PDFs and folders, and open a file. It lists through `ICloudProvider` only, pages 200 items at a time, and issues nothing until the user opens it from `File`, `Home` or `Ctrl+K`, which exist only while connectors are on (§5).

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 9.9 form layout, section 5 type styles and the Home list styling of `D-Home.png`. Design board: `design/SPEC.md` section 9.17 with `design/screenshots/D-Cloud.png` (drawn by `D06 T04 §22`).
**Job:** A user finds a PDF in their cloud storage and opens it. Consumer: `ICloudProvider` (§1), `OpenDocumentPipeline` (`D02 T01 §5`) through §12.
**Treatment:** The `recto-ui` virtualized `ui::ItemsView` over paged provider calls. Cheaper substitute that fails the checkpoint: loading entire folders before showing anything, or one dialog per provider.
**Chrome:** `ui::Dialog` (`D10 T02 §17`), `recto_tokens.g.h` tokens, Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Build `CloudBrowserDialog` (`src/app/dialogs/cloud_browser_dialog.h/.cpp`, a `ui::Dialog`): a provider switcher (`ui::SegmentedControl`, `D10 T02 §18`) for signed-in providers, a breadcrumb, a virtualized `ui::ItemsView` (`D10 T02 §11`) with name, size and modified columns (folders first, then PDFs, other types hidden behind `Show all files`), a `Sign in to another provider` link that opens §3's dialog, loading and error states with `Retry`, and `Open` and `Cancel`. Done when: against a fake provider with 1,000 items the list pages in without a UI stall and `Open` returns the chosen item.
2. Cancel an in-flight listing when the user navigates or closes the dialog, and show `CloudError` messages inline (never a raw status code). Done when: navigating mid-load cancels the first request and shows only the second folder.

- [ ] Add `Open from cloud...` to the `File` menu (`D02 T01 §9`), the Home actions (`D02 T03 §2`) and `CommandRegistry`, visible only when connectors are on and at least one provider is signed in. Done when: `Ctrl+K` finds it only in that state.
- [ ] Support keyboard navigation (`Up`, `Down` from the `ui::ItemsView` arrow-key group, `Enter` to open a folder or file, `Backspace` up one level) and `ui::Element` automation names including the file size and date (`D10 T03 §1`). Done when: a keyboard-only run opens a nested file.
- [ ] Add `Cloud.BrowserDialog` covering paging, cancellation, filtering, empty and error states. Done when: `recto-tests.exe --gtest_filter=Cloud.BrowserDialog` passes. Cheaper substitute: listing the whole drive eagerly.
- [ ] Commit: `viewer: add the Open from cloud browser`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.BrowserDialog` passes on the display-session host.

## 12. Open a cloud file as a temporary local copy

Opening downloads the file to a per-session temp folder and then opens that copy exactly like a local file, so the viewer, the actor and every tool stay unchanged. The tab remembers where the file came from. The copy is deleted when the tab closes, at exit and by a sweep at launch; it is never added to the session snapshot or the local recents list.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new behaviour; the progress card follows `design/SPEC.md` section 9 toast and card chrome and the tab badge follows section 8.1. Design board: `design/SPEC.md` section 9.17 with `design/screenshots/D-Cloud.png` (drawn by `D06 T04 §22`).
**Job:** A user opens a cloud PDF and works on it as if local. Consumer: `OpenDocumentPipeline` (`D02 T01 §5`), `DocumentTabSession` (`D02 T01 §3`).
**Treatment:** Download to a managed temp copy then the normal open path. Cheaper substitute that fails the checkpoint: a second open pipeline, or leaving copies on disk.
**Chrome:** `recto_tokens.g.h` tokens, Fluent UI System Icons (`RectoIcons`), `ui::Card`, `ui::ProgressBar` and `ui::Badge`.

**Build order:**

1. Implement `CloudOpenService::open_async(provider, item)` (`src/app/cloud_open_service.h/.cpp`; completion and progress reach the UI thread only through `ui::Dispatcher`, `D10 T01 §3`): establish confinement before any byte is written: create `AppPaths::temp_dir()\cloud\<session>\<generated-guid>\` (the folder is application-generated, the name goes through the `D02 T03 §8` sanitiser, and the provider's item id is kept only as metadata in `CloudOrigin`, never in a path), open each folder component with `FILE_FLAG_OPEN_REPARSE_POINT` and refuse any that is a reparse point, create the destination with `CREATE_NEW` and `FILE_FLAG_OPEN_REPARSE_POINT`, and confirm through `GetFinalPathNameByHandleW` on the open handle that it lies inside the session folder; only then download through `ICloudProvider::download` into that handle, keeping the folder handles open through the write and the cleanup so no component can be swapped. Show a progress `ui::Card` with a `ui::ProgressBar` and `Cancel` (`D10 T02 §16`, `§8`), verify the byte count against the provider's size, then call `OpenDocumentPipeline::open_file_async` and attach `CloudOrigin { provider, item_id, parent_id, version_token, display_path }` to the `DocumentTabSession`. Done when: opening a 50 MB fake file shows progress, opens a tab with a cloud `ui::Badge` (`D10 T02 §20`), and cancelling deletes the partial file.
2. Delete the copy on tab close, on exit, and via a sweep of stale session folders at launch; keep Portable mode's temp under its own data folder; open an item already open and unmodified by selecting its tab, while an explicit reload or fork (§13) always creates a separate local copy with its own generated folder and its own `CloudOrigin` revision. Done when: after closing the tab and restarting, the cloud temp folder is empty, and a second open of an unmodified item selects the existing tab; a hostile item id containing separators, `..` or `:stream` syntax yields a path inside the session folder, and a junction planted in the temp folder that points at an outside folder is refused before any write, with an outside sentinel file's bytes and timestamp unchanged afterwards.

- [ ] Show a cloud badge and the provider path in the tab `ui::ToolTip` (`D10 T02 §3`) and an `Open in <provider>` label, never the temp path. Done when: the tab tooltip shows `OneDrive / Documents / a.pdf`.
- [ ] Treat a failed or truncated download as `Could not open` (`D02 T01 §11`) with a cloud-specific message and no leftover file. Done when: a truncated fake download shows the dialog and leaves no file.
- [ ] Exclude cloud-origin tabs from `SessionStore` (`D02 T01 §13`) and keep them out of local recents. Done when: relaunch does not reopen the temp path.
- [ ] Add `Cloud.OpenAsTempCopy` covering download, cancel, verification, cleanup, dedup and exclusion from the session. Done when: `recto-tests.exe --gtest_filter=Cloud.OpenAsTempCopy` passes. Cheaper substitute: opening straight from a network stream.
- [ ] Commit: `viewer: open cloud files as temporary local copies`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.OpenAsTempCopy` passes on the display-session host.

## 13. Save and Save a copy to the cloud with conflict detection

For a cloud-origin tab, `Save` writes to the temp copy through `SavePipeline` (`D02 T01 §8`) and then uploads that saved file with `upload_replace`, passing the version token from the open. The first upload per tab asks `Upload your changes to <provider>?`. A `Conflict` never overwrites: the dialog offers `Save as a new file`, `Reload from <provider>` (discarding nothing without a second confirmation) and `Cancel`. `Save a copy to cloud...` uploads a new file to a folder chosen in §11's browser. Local writes stay temp-file-then-replace. Two states stay separate: the tab's local dirty state is the actor's (`dirty` is the current revision against the locally saved revision, `D01 T02 §14`, `D02 T01 §8`), and `CloudOrigin.uploaded_revision` records the last revision the provider acknowledged; the tab is `upload pending` while the locally saved revision differs from it. A failed upload leaves the local copy and the upload-pending state intact.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.9 (`Replace original` dialog layout reused for the conflict dialog). Design board: `design/SPEC.md` section 9.17 with `design/screenshots/D-Cloud.png` (drawn by `D06 T04 §22`).
**Job:** A user saves edits back to the cloud file without silently overwriting someone else's changes. Consumer: `SavePipeline` (`D02 T01 §8`), `ICloudProvider::upload_replace` (§1).
**Treatment:** Local atomic save first, then conditional upload. Cheaper substitute that fails the checkpoint: uploading without the version token, or treating a local save as uploaded.
**Chrome:** `ui::Dialog` (`D10 T02 §17`, `D02 T01 §10`).

**Build order:**

1. Implement `CloudSaveService` (`src/app/cloud_save_service.h/.cpp`): after a successful local save of a cloud-origin tab, upload with `upload_replace(item, source, expected_version)`, bind each upload to an immutable snapshot of the saved revision (copied to a private upload file so a later save cannot change it), serialize saves per tab, update `CloudOrigin.version_token` from the response, and set `CloudOrigin.uploaded_revision` to the uploaded revision only when the upload succeeds (the actor's local dirty state is cleared by the local save as usual and is never touched here); on failure keep the temp copy, keep the tab `upload pending` with a tab badge `Not uploaded`, and show `Saved locally; upload failed` with `Retry`, and implement `save_copy_to_cloud` through `upload_new` with a `NameExists` rename prompt. Done when: `Cloud.SaveUploadPendingState`, driven through the real `SavePipeline` and `DocumentActor`, shows a successful save changes the fake server's bytes and version and clears both states; a failed upload leaves the tab locally clean but `upload pending` with a retry; and an edit or a second Save during a delayed upload leaves the tab `upload pending` with the newer revision uploaded next and the first upload's bytes unchanged.
2. Build `CloudConflictDialog` with `Save as a new file`, `Reload from <provider>`, `Cancel`; `Reload` calls the explicit fork operation of §12, which downloads the current remote revision to a new generated folder and opens it in a new tab, bypassing the deduplication, and keeps the edited tab (and its own local copy) until the user closes it. Done when: a fake version bump produces the dialog, the server file is untouched, and each button does what it says, and after `Reload` the two tabs hold different revisions and neither local copy is overwritten.

- [ ] Show the one-time `Upload your changes to <provider>?` confirmation per tab session with `Upload` and `Cancel`. Done when: the second save in the same tab uploads without asking.
- [ ] Hook `Save` and `Ctrl+Shift+S` for cloud-origin tabs and add `Save a copy to cloud...` to the `File` menu and `Ctrl+K`. Done when: all three reach the cloud flow only for cloud-origin tabs or the explicit command.
- [ ] Keep an unfinished upload cancellable and resumable from the beginning after an app restart is not supported; say so in the failure text and keep the local copy until close. Done when: closing a tab or the app with an `upload pending` tab prompts `Your changes are saved on this PC but not uploaded to <provider>` with `Retry upload`, `Save a copy elsewhere` and `Close anyway`, and closing a locally dirty tab still shows the usual unsaved-changes prompt.
- [ ] Add `Cloud.SaveAndConflict` covering success, failure retry, conflict choices, new-file save and name collision. Done when: `recto-tests.exe --gtest_filter=Cloud.SaveAndConflict` passes. Cheaper substitute: uploading without comparing the version token.
- [ ] Commit: `viewer: save to the cloud with conflict detection`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.SaveAndConflict` passes on the display-session host.

## 14. Recent cloud files on Home and in the command box

A short list of the cloud files the user opened lately, so they do not browse again. Entries hold provider, item id, display path and last-opened time only; no file content, thumbnails or tokens. Signing out of a provider, or turning connectors off, hides and (for sign-out) deletes that provider's entries.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.1 Home list with `design/screenshots/D-Home.png`. Design board: `design/SPEC.md` section 9.17 with `design/screenshots/D-Cloud.png` (drawn by `D06 T04 §22`).
**Job:** A user reopens a cloud file in two clicks. Consumer: `HomePage` (`D02 T03 §2`), `CommandBoxControl` (`D02 T03 §12`).
**Treatment:** A local metadata list with no network use. Cheaper substitute that fails the checkpoint: a list that contacts providers on startup.
**Chrome:** `recto_tokens.g.h` tokens, the Home recents card styling on `ui::ItemsView` rows (`D10 T02 §11`).

**Build order:**

1. Implement `CloudRecentsStore` (`src/services/cloud/cloud_recents.h/.cpp`) writing `cloud_recents.json` (cap 20, most recent first) through temp-file-then-replace in the data folder, with corrupt-file recovery to empty, and `remove_provider(provider)` called by sign-out (§4). Done when: sign-out removes the provider's entries and a corrupt file loads as empty.
2. Show a `Cloud` row group on `HomePage` (`D02 T03 §2`) and in `Ctrl+K` results (`D02 T03 §12`, `ui::SearchBox` results, `D10 T02 §21`) only while connectors are on and the provider is signed in; activating an entry calls §12 and shows `Sign in again` when the token is gone. Done when: with connectors off the group is absent, and with a signed-out provider the entry is hidden.

- [ ] Add `Remove from recents` and `Clear cloud recents` commands. Done when: both update the file and the Home list.
- [ ] Never preload or ping the provider to show recents. Done when: `Cloud.GateCounters` shows 0 requests while Home is shown.
- [ ] Add `Cloud.RecentsStore` and `Panels.CloudRecentsOnHome` covering cap, order, corruption, sign-out and visibility. Done when: `recto-tests.exe --gtest_filter=Cloud.RecentsStore:Panels.CloudRecentsOnHome` passes. Cheaper substitute: fetching metadata for every recent on Home.
- [ ] Commit: `viewer: add recent cloud files to Home and the command box`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cloud.RecentsStore:Panels.CloudRecentsOnHome` passes on the display-session host.

## 15. Privacy and security tests: zero requests until sign-in, no secrets on disk

This section proves the opt-in claims for the whole file. With connectors off, or on but never signed in, Recto makes no request to any provider or identity host. Tokens appear in no file, log or crash bundle. Redirects never carry credentials to another host. A hostile provider response cannot overflow or hang the client. Group Policy disables everything.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `Audit.CloudNoTrafficUntilSignIn`: launch the app harness with a recording network shim, run a full session (open, save, Home, settings, `Ctrl+K`) with connectors off, then on without sign-in, and assert zero requests and zero DNS lookups to any provider or identity host. Done when: both phases record zero requests, and enabling one provider's fake sign-in produces requests only to that provider's hosts.
2. Add `Audit.CloudSecretsNeverPersist`: sign in with sentinel tokens through fakes, exercise open, save, sign-out, crash-bundle writing (`D07 T02 §4`) and Portable mode, then scan every file under the data folders, temp and logs for the sentinels. Done when: no sentinel is found and the Credential Manager entry exists only while signed in.

- [ ] Add `Audit.CloudRedirectAndHostile`: cross-host redirect without auth header, redirect to an unlisted host refused, oversize and endless-paging responses bounded, malformed JSON rejected with typed errors. Done when: all cases end within bounds without a crash under ASan.
- [ ] Add `Audit.CloudPolicyOff`: with the policy source reporting `CloudConnectorsDisabled` every provider call returns `Disabled`. Done when: the request counter stays 0.
- [ ] Add the connector rows to `docs/security-audit.md` naming each test above. Done when: `rg Audit.Cloud docs/security-audit.md` finds all four names.
- [ ] Add a banned-call scan: only `http_client.cpp` includes `winhttp.h`, and no cloud file calls `ShellExecute`, `CreateProcess` or reads `settings.json` for secrets. Done when: `Audit.CloudBannedCalls` passes and fails when a banned call is added to a scratch copy.
- [ ] Commit: `tests: prove cloud connectors make no request until sign-in and keep no secrets`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Audit.Cloud*` passes, including under ASan for the hostile cases.

## Verification

- With connectors off, and on but never signed in, a full session makes zero requests to any provider or identity host (`Audit.CloudNoTrafficUntilSignIn`); tokens and sentinels appear in no file, log or crash bundle (`Audit.CloudSecretsNeverPersist`); redirects never carry credentials to another host (`Audit.CloudRedirectAndHostile`); Group Policy disables every provider call (`Audit.CloudPolicyOff`).
- Each provider passes its fake-server suite and, when its client ID exists, the recorded opt-in live smoke run; a stale remote version is a conflict, never an overwrite.
- Full `recto-tests` suite green; `python scripts/todo-graph.py validate` passes.
