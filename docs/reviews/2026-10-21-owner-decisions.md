## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T05:10:39Z
- Effort: medium
- Lines: 23
- Digest: 4e322430a96d4c4c

Review limited to the supplied candidate diff: repository reads failed because the command runner could not start. No files were modified and no git commands ran.

**consistency: needs-attention**

1. **Stage 1 still inherits stage 2 legal and production requirements.** `todo/09-cloud/TODO-05-desktop-integration-and-launch.md`, §15 requires all four legal documents before launch and records “one ledger entry for all four”, whereas §13 must exempt only the ESIGN disclosure for storage launch. §16 also requires seal credentials and both acceptance runs for completion. These requirements do not provide independently satisfiable stage 1 evidence. Split the legal evidence and production prerequisites by stage, including which signer credentials may be absent.

**integration: needs-attention**

2. **The stage 2 gate has a circular prerequisite.** `todo/09-cloud/TODO-05-desktop-integration-and-launch.md`, §18 step 1 allows `SIGNATURES_ENABLED=true` only after the signature acceptance run is recorded. But §16 step 4 requires completing a production signature request for that acceptance run, which the false switch prevents. Define a restricted production canary activation before public enablement, with explicit access controls, or otherwise change the acceptance/gate sequence.

3. **The desktop cannot guarantee zero upload bytes on the create-call `503` path.** `todo/09-cloud/TODO-05-desktop-integration-and-launch.md`, §9 steps 4 and 7 describe uploading before draft creation, yet require zero uploaded bytes if creation returns `503 signatures_not_available`. A health check can return true, followed by a deployment disabling signatures before creation. The document has already left the desktop when the refusal arrives. Admission must be enforced before accepting signature upload bytes; a preliminary health read alone cannot establish this guarantee. Test the configuration-change race explicitly.

**adversarial: needs-attention**

4. **The switch guards creation, not the signature workflow.** `todo/09-cloud/TODO-04-signature-requests.md`, §20 step 5 guards only `POST /v1/envelopes`; step 3 guards sending only on consent configuration. With approved consent and an existing draft, setting `SIGNATURES_ENABLED=false` does not require the send endpoint to refuse. Nor does the new requirement stop queued invitations, reminders or completion work. Thus the switch is not a reliable disabled-state boundary after rollback or temporary activation. Require server-side enforcement at send and asynchronous side-effect boundaries, and test existing drafts and queued jobs while disabled.

**source-defect: needs-attention**

5. **Preserving a PDF’s security handler does not establish the promised password protection or absence of plaintext.** `docs/plans/2026-10-13-recto-foundation.md`, owner decision “crash recovery snapshots”, and `todo/02-viewer/TODO-01-app-shell.md`, §12 equate preserving encryption with “cannot be opened without the document’s password”. PDF encryption can use an empty user password, and metadata can remain unencrypted. The single AES-256 password fixture does not exercise either case. These are defined capabilities of the PDF security model, not implementation anomalies. [ISO 32000-1, §7.6](https://developer.adobe.com/document-services/docs/assets/35e4369068f86065372c18787171a17e/PDF_ISO_32000-1.pdf)

   Specify how the owner’s confidentiality requirement applies to these inputs, without silently changing that decision. Also require verification that temporary files and interrupted writes contain no plaintext document content; testing only whether the final snapshot opens without a password misses a plaintext intermediate file.

OVERALL: needs-attention

## Response to round 1

The decisions themselves are the owner's (2026-10-21) and stand; these fixes change how the plan applies them.

1. Fixed: launch ledger rows carry a stage; `D09 T05 §15`, `§16` and `§18` are stage 1 only, and the ESIGN disclosure, seal credential and signature acceptance run moved to the new `D09 T05 §21`; `D09 T04 §15` allows `SEAL_SIGNER=none` while signatures are unavailable, so stage 1 production starts without the certificate.
2. Fixed: the new `D09 T04 §30` adds a canary account allowlist that may send only to listed operator mailboxes, and `D09 T05 §21` runs `go-live --stage signatures-canary`, then the signature acceptance run, then `--stage signatures`, so the gate is no longer circular.
3. Fixed: the desktop asks `POST /v1/envelopes/admissions` (`D09 T04 §30`) before any upload byte, and `D09 T05 §9` states the exact guarantee for a switch change after admission with `Cloud.SendForSignatureSwitchRaceAfterAdmission`.
4. Fixed: `D09 T04 §30` enforces `SIGNATURES_ENABLED` at send, the signing ceremony, invitations, reminders, completion, bulk rows and form confirmations, holding queued effects until re-enabled, with tests on existing drafts and queued jobs; the switch moved out of `D09 T04 §20`, which waits on counsel.
5. Fixed: the foundation plan's owner-decision line and `D02 T01 §12` now say a snapshot keeps the original's security handler, passwords, permissions and `/EncryptMetadata`, so it is exactly as protected as the original (an empty user password or unencrypted metadata is not strengthened or weakened), and the writer encrypts in memory before the first byte reaches the temporary file; `Shell.RecoveryEncryptedSnapshots` covers a non-empty and an empty user password, `/EncryptMetadata false`, and a plaintext canary absent even from an interrupted write's temporary file.
