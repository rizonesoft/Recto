---
schema_version: 1
id: operator-setup
domain: 99-manual
status: active
title: "TODO-01 -- Operator Setup (Manual)"
track: M0
---

# TODO-01 -- Operator Setup (Manual)

> **Goal:** The things only the project owner can do, each written so it can be done from this file alone with zero assumed GitHub, trademark or Partner Center knowledge: the Recto name cleared and reserved, the public repository face finished, the Partner Center account and app identity created, and the reference machines confirmed, each leaving proof a later agent session can verify from the public record.

> **Current state:** Measured 2026-10-13. The name Recto, its banner and social preview exist as design assets (`design/brand/`, with an `[OWNER]` placeholder in the social preview that must be replaced); no trademark search, GitHub repository, Store reservation or Partner Center enrollment is recorded. The obligations come from foundation plan D1 (search USPTO and EUIPO before reserving the Store name), sections 8 and 11 (reference machines chosen in M0), and SPEC 3.5. Every section here has Needs `operator`: no agent runner takes these rows; the operator works them, commits the proof (or asks a session to), and the section stamps like any other once its Needs line is updated by hand.

## Inputs

- `design/SPEC.md` sections 3.5 (banner and social preview files and the `[OWNER]` replacement), 3.1 (icon), and `design/brand/github-banner.png`, `design/brand/github-social-preview.png`.
- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D1, D6), 8, 11 (trademark search and name reservation, reference machines, both needed by M0).
- -> XREF: D00 T01 §1 -- the repository this file finishes the public face of; §2 here assumes the first push happened there.
- -> XREF: D07 T02 §4 -- the submission that uses the Partner Center app and identity created in §3 here.

## Outcome

- "Recto" is trademark-searched with the result recorded, and the Store name is reserved (or a rename decision is taken before any listing exists).
- The GitHub repository shows the banner, description, topics and social preview a visitor should see.
- Partner Center holds the app reservation whose identity values D07 T01 §2 packages with.
- The two reference machines are named and confirmed for D00 T01 §5's record.

**Adjacency:** all=not-applicable (operator procedures in external systems; their lists, documents and settings live in GitHub, the trademark registers and Partner Center, and this file only records their outcomes)

**Adjacency rationale:** Nothing here is a Recto product surface. Each section's record (the outcome, date and proof) is written back into this file or the document it names, which is the audit trail; the systems themselves keep their own.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Trademark search and Store name reservation | -- | [ ] |
| 2 | §2 | GitHub repository face | D00 T01 §1 | [ ] |
| 3 | §3 | Partner Center account and app reservation | §1 | [ ] |
| 4 | §4 | Reference machines confirmed | -- | [ ] |

## 1. Trademark search and Store name reservation

D1's condition, before any public listing investment: search the USPTO (TESS) and EUIPO (eSearch) registers for "Recto" in software classes (Nice class 9 above all, 42 beside it), read what comes back as a layperson carefully rather than a lawyer conclusively, and record the outcome here with dates and application numbers of anything close. If clear, reserve the name in Partner Center as soon as §3's account exists (order between §1 and §3 can swap in practice; the search record must predate the reservation). If a conflict looks real, stop and take advice before spending on listing assets; the rename cost is low now and high after a listing (D1).

**Phase:** 99
**Needs:** operator
**Surface:** no

**Build order:**

1. Run both register searches and save the result pages (screenshots or PDFs) under the repo-external records location the owner keeps, recording search terms, classes, date and the closest hits in this section. Done when: a later reader can tell exactly what was searched and what was found without redoing it.
2. Decide go or no-go on the name from the record; on go, note the reservation as pending §3. Done when: the decision and its date are written here.

- [ ] Both register searches recorded with close hits named. Done when: the record names dates and classes, not a bare "searched, fine". Cheaper substitute: a web search of the name, which is not a register search.
- [ ] Go or no-go decision recorded. Done when: D1's reversal-cost note is either discharged or escalated visibly.
- [ ] Commit: `manual: Recto trademark search record and name decision`

**Test checkpoint:** `python scripts/todo-graph.py validate` passes with this record committed; the record in this section names both registers, the classes, the dates and the decision; an agent session verifies the reservation exists once §3 completes (public listing or Partner Center proof the operator provides).

## 2. GitHub repository face

After D00 T01 §1's first push: set the repository description ("Open-source PDF viewer and converter for Windows", matching the social preview), topics (for example pdf, pdf-viewer, winui, windows, mupdf, converter), replace `[OWNER]` in `design/brand/github-social-preview.png` by regenerating it through the design tooling or an image edit, upload it under Settings, General, Social preview, confirm the README banner renders, and set branch protection on `main` requiring the `plan-gates` and build checks (names exactly as D00 T01 §4 records them) once both workflows have run.

**Phase:** 99
**Needs:** operator
**Surface:** no

**Build order:**

1. Repository About: description, website when one exists, topics, and the social preview image with the real owner name. Done when: the repository home and a link-unfurl preview show Recto's banner and sentence, screenshotted into this section's proof.
2. Branch protection on `main`: require status checks (both workflows), disallow force pushes. Done when: a deliberate red pull request (opened and closed for the proof) cannot merge.

- [ ] About, topics and social preview live. Done when: the `[OWNER]` placeholder exists nowhere in the uploaded image. Cheaper substitute: uploading the placeholder image as-is.
- [ ] Branch protection enforced with both checks required. Done when: the red-PR proof is recorded with its link.
- [ ] Commit: `manual: GitHub repository face and branch protection proof`

**Test checkpoint:** an agent session verifies from the public repository page (`gh repo view` shows the description and topics) (description, topics, preview) and the recorded protected-branch proof without owner help.

## 3. Partner Center account and app reservation

Enroll in the Microsoft Partner Center Windows developer program under the owner's chosen publisher identity, reserve the app name decided in §1, and record the assigned identity values D07 T01 §2 packages with (Package/Identity/Name, Publisher, and the publisher display name) in that section's build inputs, not in chat. Note the account's credential recovery belongs in the owner's password vault per standing rules, never in this repository.

**Phase:** 99
**Needs:** operator, store-account
**Surface:** no

**Build order:**

1. Complete enrollment and reserve the product name, choosing the product type for a desktop MSIX app. Done when: the app exists in Partner Center with its identity values visible.
2. Hand the identity values to D07 T01 §2 (entered into the repo's packaging configuration by an agent session at that time, values pasted by the operator, never guessed). Done when: a test package built with them passes the §2-of-that-file audit's identity row.

- [ ] App reserved with identity values recorded for packaging. Done when: the values are in the repo's packaging config and nowhere else sensitive. Cheaper substitute: reserving a near-enough name and renaming later (Store names do not work that way).
- [ ] Publisher certificate and account recovery confirmed in the owner's vault. Done when: the operator confirms recoverability without printing any secret.
- [ ] Commit: `manual: Partner Center reservation and package identity recorded`

**Test checkpoint:** `msbuild` produces D07 T01 §2's first real-identity package and it installs in its audit step; the reservation is visible in Partner Center proof the operator shows the verifying session.

## 4. Reference machines confirmed

D00 T01 §5 needs the owner's call: one mid-range x64 laptop and one ARM64 laptop that plan section 8's targets bind to. This section is the decision and its proof: name both machines (model, CPU, RAM, GPU, Windows build, display scale), confirm they are available for M2 and release measurements, and record whether a clean VM host exists for D07 T01 §3's install proof (or which physical machine resets for it).

**Phase:** 99
**Needs:** operator
**Surface:** no

**Build order:**

1. Choose and record both machines in `docs/reference-machines.md` (created by D00 T01 §5 from this decision). Done when: D00 T01 §5's `owner-decision` Need can be removed with this section cited.
2. Confirm the clean-machine plan for install proof (VM image or resettable host) and who runs attended sessions on it. Done when: D07 T01 §3 can name its machine in advance.

- [ ] Both machines named with specs and availability. Done when: performance quotes elsewhere can cite them unambiguously. Cheaper substitute: "the dev box plus whatever ARM device turns up".
- [ ] Clean-machine plan recorded. Done when: the install-proof section has a machine before it needs one.
- [ ] Commit: `manual: reference machines and clean-machine plan confirmed`

**Test checkpoint:** `docs/reference-machines.md` exists with both machines, and D00 T01 §5 resolves without its waiting Need once updated.

## Verification

- Every outcome here is verifiable from a public page, a recorded proof, or a committed record, by a session that was not present.
- No secret, certificate or credential appears in the repository at any point (searched at stamp time).
- `python scripts/todo-graph.py validate` passes with this file's sections honestly waiting until worked.
