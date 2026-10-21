# Recto Cloud design

Status: approved direction, owner decisions 2026-10-21 (foundation plan D20). Plan: `todo/09-cloud/`, phase 98. Author: Derick Payne. Last reviewed: 2026-10-21.

## What it is

Recto Cloud is a web service and web app that bring Acrobat Pro's cloud tools into Recto's parity plan (D18): request signatures from other people, run shared reviews with live comments, and store documents online. It is a separate product surface beside the Windows app. The desktop app stays offline by default (D16) and talks to Recto Cloud only after the user signs in, asking before every upload or send.

## Decisions

| Topic | Decision | Cost of changing |
| --- | --- | --- |
| Hosting | TypeScript on Cloudflare Workers, with D1 (database), R2 (files), Durable Objects (one per live review and one per signature envelope), Queues (email and processing) and Cron Triggers (reminders, expiry, retention) | High after launch: data migration; the storage and database layers sit behind interfaces so a second backend is a module, not a rewrite |
| Self-hosting | The same code runs on the open-source `workerd` runtime; storage has an R2 adapter and an S3-compatible adapter, and the database layer targets SQLite (D1 is SQLite) | Medium: adapters are bounded |
| Licence | `cloud/` is AGPL-3.0-or-later with its own `cloud/LICENSE`; dependencies must be AGPL-compatible (MIT, BSD, Apache-2.0) | Low before external contributions, high after |
| Signatures | Simple electronic signatures (US ESIGN and UETA, EU eIDAS SES) with a tamper-evident audit trail; the completed PDF is sealed by the service with a PAdES signature and an RFC 3161 timestamp; certificate-based advanced and qualified signatures stay in the desktop signer (D05 T01 §8) | Medium: adding AdES or QES is additive |
| Sign-in | Passkeys (WebAuthn) or one-time email links for account holders; signers and invited guests need no account and act through single-use, expiring links | Low |
| Web viewer | `pdf.js` in the browser with scripting and `eval` disabled, so the web app keeps Recto's zero PDF JavaScript rule (D9) | Medium: a WebAssembly build of `recto-pdf` could replace it later |
| Repository | A `cloud/` npm workspace (`cloud/apps/api`, `cloud/apps/web`, `cloud/packages/*`), outside `Recto.sln`; the desktop app uses only the documented HTTPS API (foundation plan section 3.2, corrected 2026-10-21) | Low |

## Owner decisions

The owner answered these on 2026-10-21; they replace the open owner decisions this spec listed before. The work still waiting on someone else is marked `third-party` or `operator` in the sections that name it: counsel's review of the legal texts, the seal certificate's identity validation, DNS records and secrets.

| Topic | Decision | Cost of changing |
| --- | --- | --- |
| Domain | Service (web app and API) at `https://cloud.recto.co.za`; the apex `recto.co.za` stays free for the product website. `PUBLIC_ORIGIN` is `https://cloud.recto.co.za` and the WebAuthn `RP_ID` is `cloud.recto.co.za`. | High after launch: passkeys are bound to the RP ID and every sent share or signing link carries the host |
| Email sender | Mail is sent from the subdomain `mail.recto.co.za` (for example `no-reply@mail.recto.co.za`) through SMTP2GO, with SPF, DKIM and DMARC on that subdomain; the abuse and security contacts are `abuse@recto.co.za` and `security@recto.co.za` | Medium: a new sender subdomain starts with no reputation |
| Data region | EU jurisdiction: R2 buckets created with the `eu` jurisdiction, the D1 database created with a jurisdiction (not only a location hint), and Durable Objects restricted to the EU where the product supports it; the `cloud/README.md` data location table states what is and is not covered | High: none of these can be changed after creation, so moving means a full data migration |
| Seal certificate | GlobalSign Digital Signing Service (AATL-listed, cloud HSM, signing API), organization validated; the private key never leaves GlobalSign's HSM | Medium: another AATL CA means a new contract and identity validation, and no code change beyond the signer adapter |
| Launch staging | Two stages. Stage 1 opens document storage, sharing and shared reviews once counsel has approved the terms, privacy policy and DPA. Stage 2 opens signature requests once the ESIGN disclosure is approved and the GlobalSign certificate is issued. Until stage 2, production keeps `SIGNATURES_ENABLED` false, so the API refuses envelope creation and the web app and desktop show the feature as not yet available | Low: the switch is one configuration value and the stages share one codebase |
| Pricing | Free only at launch, with the limits below; anyone needing more can self-host the AGPL code on `workerd`. No billing system | Low: a paid tier is a configuration and terms change plus a billing provider |
| Account limits | `DEFAULT_QUOTA_BYTES` 2 GiB (2147483648), `MAX_UPLOAD_BYTES` 100 MiB (104857600), `TRASH_RETENTION_DAYS` 30, `LINK_MAX_DAYS` 90, account deletion grace 30 days, `AUDIT_RETENTION_DAYS` 2557 (7 years) | Low to raise; lowering needs notice to existing users under the terms |
| Signature requests | Default expiry 30 days; a reminder every 3 days, at most 5; completed envelopes kept until the sender deletes them (their audit records follow `AUDIT_RETENTION_DAYS`); unfinished envelopes removed 90 days after they expire; 25 sends per account per month; documents for signing up to 25 MiB | Low to raise; lowering needs notice under the terms |
| Legal review | The owner's counsel reviews the terms of service, privacy policy, DPA and the ESIGN consent disclosure. The service uses a South African domain and stores data in the EU, so the facts given to counsel ask whether South Africa's Protection of Personal Information Act (POPIA), including its cross-border transfer rules, applies alongside GDPR | Not applicable: it is a launch requirement |

## Architecture

```text
Browser (web app, pdf.js, no PDF scripts)        Recto for Windows (opt-in, signed in)
              \                                       /
               HTTPS JSON API + WebSocket (reviews)
                              |
                    Cloudflare Worker: api
      auth | documents | shares | reviews | envelopes | audit | admin
         |            |               |                |
        D1           R2        Durable Objects     Queues -> email (SMTP2GO), processing
  (metadata,   (PDF blobs,   (ReviewRoom per      Cron -> reminders, expiry, retention
   audit log)   versions)     review, Envelope
                              per signature request)
```

- **Documents** are immutable versions in R2, keyed by content hash, with metadata (owner, folder, name, size, page count, created) in D1. Uploads are size-capped (default 200 MB), type-checked by magic bytes, and never parsed by the Worker except in the bounded signing pipeline.
- **Sharing** grants `view`, `comment` or `edit` to accounts or to link holders; links can expire and carry a password; every grant and access is an audit event.
- **Reviews**: a `ReviewRoom` Durable Object per review owns the comment state for one document version, accepts WebSocket connections from the web app and the desktop app, orders edits, and persists to D1; comments are stored in Recto's annotation JSON (the same model as the desktop's XFDF and JSON exchange, D03 T03 §2), so a review round-trips into a PDF.
- **Signature requests**: an `Envelope` Durable Object per request serializes its state machine (draft, sent, in progress, completed, declined, voided, expired), recipients (signers, approvers, copy receivers; sequential or parallel order), fields (signature, initials, date, text, checkbox) placed on pages, and every event.
- **Audit trail**: an append-only event table where each event stores the SHA-256 of the previous event; the envelope's final chain head is embedded in the certificate of completion and covered by the PAdES seal, so any later change is detectable.
- **Completion**: when the last signer finishes, a Queue job renders field values and signature appearances into the PDF, appends a certificate of completion page (or a separate PDF, per envelope setting), seals the result with PAdES-B-T, stores it as a new version, and emails every party a link.
- **Email** goes through SMTP2GO's API from a Queue consumer, using the owner's verified sender; links in email are single-use or short-lived tokens that never carry the document.

## Security and privacy

- Every uploaded file is hostile: the browser renders it in `pdf.js` with scripting off; the Worker's signing pipeline uses bounded parsers with size, depth and time limits and refuses encrypted or malformed input with a clear message.
- Sessions are `HttpOnly`, `Secure`, `SameSite=Lax` cookies for the web app and short-lived bearer tokens for the desktop app (OAuth 2.0 authorization code with PKCE through a loopback redirect), stored by the desktop in Windows Credential Manager, never in settings or logs.
- A strict Content Security Policy, no third-party scripts or trackers, rate limits on sign-in and link redemption, and CSRF protection on state-changing requests.
- Logs carry identifiers and event types, never document content, file names of other users, email bodies or tokens.
- Users can export all their data and delete their account; deletion removes R2 objects and D1 rows after a recorded grace period, except completed envelopes other parties hold rights to, which follow the retention policy.

## Desktop integration

The Windows app gains a `Recto Cloud` group in Settings (off until the user signs in), `Open from Recto Cloud` and `Save to Recto Cloud`, `Send for signature` and `Start a shared review`, each asking before upload and showing what will be sent. Shared review comments appear in the existing Comments panel through the annotation JSON model. Nothing in the desktop changes when the user never signs in.

## Testing

- Unit and integration tests run on `workerd` locally through Vitest with the Workers pool, against D1, R2 and Durable Object bindings, in CI on every pull request.
- The signing pipeline has golden PDFs whose PAdES signatures validate with an independent validator in CI, and tamper tests that break the hash chain or the signed bytes and expect detection.
- End-to-end browser tests (Playwright) cover sign-in, upload, share, review and the full signing ceremony.
- A self-host smoke test runs the same build on plain `workerd` with the SQLite and S3-compatible adapters.

## Phasing

Phase 98, in dependency order: platform and accounts, documents and sharing, the web app viewer, shared reviews, signature requests, desktop integration, then launch work (domain, legal, seal certificate, monitoring) in the two stages the owner decided above: storage, sharing and reviews first, signature requests second.
