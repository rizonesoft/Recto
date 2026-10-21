# 09 Cloud

> **Phase 98 (Recto Cloud, the web service of foundation plan D20)**

Recto Cloud brings Acrobat Pro's cloud tools into parity (D18) as a web service and web app: signature requests with simple electronic signatures, a tamper-evident audit trail and a PAdES seal; shared reviews with live comments; document storage and sharing; and opt-in desktop integration. It runs as TypeScript on Cloudflare Workers with D1, R2 and Durable Objects, is self-hostable on `workerd`, and is licensed AGPL-3.0-or-later under `cloud/`. Design: `docs/specs/2026-10-21-recto-cloud-design.md`.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-platform-and-accounts.md) | Recto Cloud Platform, Accounts and Security | active |
| [TODO-02](./TODO-02-documents-and-sharing.md) | Recto Cloud Documents, Sharing and Web Viewer | active |
| [TODO-03](./TODO-03-shared-reviews.md) | Recto Cloud Shared Reviews | active |
| [TODO-04](./TODO-04-signature-requests.md) | Recto Cloud Signature Requests, Audit Trail and Seal | active |
| [TODO-05](./TODO-05-desktop-integration-and-launch.md) | Recto Cloud Desktop Integration and Launch | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- The `cloud/` AGPL workspace: Workers API, web app, storage and database adapters, self-host build, CI
- Accounts (passkeys, email links), documents, sharing, shared reviews, signature requests, audit trail, PAdES seal, email
- The Windows app's opt-in Recto Cloud integration and the launch work that waits on owner decisions

## Out of scope

- Certificate-based advanced and qualified signatures (desktop signer, D05 T01 §8), native mobile apps, and any desktop network use before sign-in

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
