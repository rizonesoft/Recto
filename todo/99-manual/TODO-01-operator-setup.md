---
schema_version: 1
id: operator-setup
domain: 99-manual
status: active
title: "TODO-01 -- Operator Setup (Manual)"
track: M0
---

# TODO-01 -- Operator Setup (Manual)

> **Goal:** Complete the external operator setup actions that only the project owner can execute, written so each can be performed from this file alone with zero assumed GitHub, trademark, or Partner Center background: clear and reserve the Recto name (`§1`), configure the public GitHub repository face and branch protection (`§2`), enroll in Microsoft Partner Center and reserve the optional Store app identity (`§3`), and confirm the physical `ARM64` device and clean-machine install-test host (`§4`), leaving committed or publicly verifiable proof for later agent sessions.

> **Current state:** Measured 2026-10-14. The name Recto, its banner, and its social preview exist as design assets (`design/brand/`, with an `[OWNER]` placeholder in the social preview that must be replaced); no trademark search, Store reservation, or Partner Center enrollment is recorded yet. On 2026-10-14, the primary `x64` benchmark host machine was recorded in `docs/reference-machines.md` (`D00 T01 §5`). Every section in this file carries `Needs: operator` (and `store-account` on `§3`): automated agent runners never claim these rows; the operator completes them, records the non-sensitive proof here, and stamps the section once complete.

## Inputs

- `design/SPEC.md` sections 3.5 (banner and social preview files and the `[OWNER]` replacement), 3.1 (icon), and `design/brand/github-banner.png`, `design/brand/github-social-preview.png`.
- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D1, D6), 8, and 11 (trademark search and name reservation, reference machines).
- -> XREF: D00 T01 §1 -- the repository whose public GitHub face `§2` completes after the initial push.
- -> XREF: D00 T01 §5 -- records the primary `x64` benchmark host machine resolved on 2026-10-14 and defers naming a specific physical `ARM64` device to `§4` here.
- -> XREF: D07 T02 §6 -- the optional Microsoft Store Partner Center submission kit in `D07 T02 §6` consumes the Partner Center app reservation and identity values created in `§3` here.

## Outcome

- "Recto" is searched across USPTO and EUIPO trademark registers in Nice classes 9 and 42 with the dated result and go/no-go decision recorded (`§1`).
- The public GitHub repository displays the description, topics, rendered banner, owner-stamped social preview image, and required branch-protection status checks on `main` (`§2`).
- Microsoft Partner Center holds the optional Store app reservation whose non-sensitive identity values (`Package/Identity/Name`, `Publisher`, `PublisherDisplayName`) are recorded for `D07 T01 §5` and `D07 T02 §6` (`§3`).
- The physical `ARM64` reference machine (when attached) and the clean-machine install-verification host (`D07 T01 §6`) are recorded in `docs/reference-machines.md` (`§4`).

**Adjacency:** all=not-applicable (operator procedures executed in external web portals and hardware setups; their records are committed back into this file and `docs/reference-machines.md`, and none are Recto product surfaces)

**Adjacency rationale:** Nothing in `99-manual` is a product UI surface. Each section's outcome, date, and non-sensitive proof are committed into this file or `docs/reference-machines.md` as the permanent audit trail.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Trademark search and Store name reservation | -- | [ ] |
| 2 | §2 | GitHub repository face and branch protection | D00 T01 §1 | [ ] |
| 3 | §3 | Partner Center account and optional Store app reservation | §1 | [ ] |
| 4 | §4 | Physical ARM64 reference machine and clean-machine host confirmation | -- | [ ] |

## 1. Trademark search and Store name reservation

Fulfill decision D1's precondition before investing in public Store listing assets: search both the USPTO Trademark Electronic Search System (TESS / Trademark Search) and the EUIPO eSearch plus register for `"Recto"` in software classes (Nice Class 9 for downloadable computer software and Class 42 for software services), review any live/active hits for PDF, document, or productivity software, and record the exact search dates, query strings, Nice classes, closest application/registration numbers, and the go/no-go name decision in this section.

**Phase:** 99
**Needs:** operator
**Surface:** no

**Build order:**

1. Execute the USPTO and EUIPO trademark searches for `"Recto"` in Nice Classes 9 and 42, archive the search result PDFs/screenshots in the owner's external records folder, and record the search date, query parameters, and closest registration numbers in a Markdown table in this section. Done when: a reader can see the exact registers, classes, dates, and closest hits without repeating the search.
2. Record the explicit go or no-go decision on the `"Recto"` name in this section (and in `docs/plans/2026-10-13-recto-foundation.md` section 11 item 3). Done when: the dated decision is committed and `python scripts/todo-graph.py validate` passes.

- [ ] Search the USPTO trademark register (`tmsearch.uspto.gov`) for word mark `"Recto"` filtered to Active/Live marks in Nice Classes `009` and `042`, and record the search date, query syntax, total hit count, and any close software marks. Done when: the USPTO row in this section's record table is complete. Cheaper substitute: running a casual Google search instead of searching the official USPTO and EUIPO trademark registers.
- [ ] Search the EUIPO eSearch plus register (`euipo.europa.eu/eSearch`) for `"Recto"` in Nice Classes `9` and `42`, and record the search date, query syntax, total hit count, and any close software marks. Done when: the EUIPO row in this section's record table is complete.
- [ ] Evaluate whether any active registration in Nice Class 9 or 42 covers PDF viewers, document converters, or office software, and record the layperson assessment and reversal cost note per decision D1. Done when: the assessment paragraph is written in this section.
- [ ] Record the dated `GO` (proceed with `"Recto"`) or `NO-GO` (rename before public release) decision in this section and update `docs/plans/2026-10-13-recto-foundation.md` section 11 item 3. Done when: both files reflect the dated decision.
- [ ] Commit: `manual: record USPTO and EUIPO trademark search results and Recto name decision`

**Test checkpoint:** `python scripts/todo-graph.py validate` exits `0` with the USPTO and EUIPO search dates, Nice classes (`9`, `42`), closest registration numbers, and go/no-go decision quoted in this section.

## 2. GitHub repository face and branch protection

Configure the public GitHub repository settings and branch protection after `D00 T01 §1`'s initial push and `D00 T01 §4`'s CI workflows are live:
- Replace the `[OWNER]` placeholder in `design/brand/github-social-preview.png` via `design/tools/build_resources.py` (or the owner's handle), and upload the 1280x640 social preview image under GitHub `Settings -> General -> Social preview`.
- Set the repository About description to `"Open-source PDF viewer, OCR tool and document converter for Windows"` and configure repository topics (`pdf`, `pdf-viewer`, `ocr`, `document-converter`, `winui3`, `windows`, `cpp20`).
- Configure branch protection on `main` requiring pull-request status checks (`plan-gates` and `build` from `D00 T01 §4`) to pass before merging and disallowing force pushes.

**Phase:** 99
**Needs:** operator
**Surface:** no

**Build order:**

1. Regenerate `design/brand/github-social-preview.png` with the real owner handle (removing `[OWNER]`), upload it to GitHub `Settings -> General -> Social preview`, and set the repository description and topics. Done when: `gh repo view` shows the exact description and topics and the `[OWNER]` placeholder is gone.
2. Enable branch protection on `main` requiring `plan-gates` and `build` status checks and blocking force pushes, and verify by recording `gh api repos/<owner>/RectoPDF/branches/main/protection` (or a test red-PR screenshot). Done when: the branch protection proof is recorded in this section.

- [ ] Replace `[OWNER]` in `design/brand/github-social-preview.png` with the actual repository owner/organization handle and verify the image dimensions are `1280 x 640`. Done when: visual inspection confirms zero `[OWNER]` placeholder text remains in `design/brand/github-social-preview.png`. Cheaper substitute: uploading the raw template with the literal `[OWNER]` placeholder still visible.
- [ ] Upload `design/brand/github-social-preview.png` under GitHub `Settings -> General -> Social preview` and verify the README header banner (`design/brand/github-banner.png`) renders on the repository home page. Done when: the repository home page and social preview card render cleanly.
- [ ] Set the GitHub repository About description and topics (`pdf`, `pdf-viewer`, `ocr`, `document-converter`, `winui3`, `windows`, `cpp20`). Done when: `gh repo view --json description,repositoryTopics` returns the configured description and topics.
- [ ] Configure branch protection rules on `main` requiring status checks (`plan-gates` and `build` from `.github/workflows/ci.yml`) to pass before merge and disabling force pushes. Done when: branch protection is active on `main` and recorded in this section.
- [ ] Commit: `manual: configure GitHub repository face, social preview, and main branch protection`

**Test checkpoint:** `gh repo view --json description,repositoryTopics` output and the `main` branch protection verification record are quoted in this section.

## 3. Partner Center account and optional Store app reservation

For the optional Microsoft Store MSIX distribution channel (`D6`, `D07 T01 §5`, `D07 T02 §6`): enroll in the Microsoft Partner Center Windows & Xbox developer program under the owner's publisher identity, reserve the app name cleared in `§1`, and record the non-sensitive Store package identity strings (`Package/Identity/Name`, `Package/Identity/Publisher`, and `PublisherDisplayName`) in `src/app/Package.appxmanifest` (never committing PFX certificates, passwords, or Partner Center API keys to the repository).

**Phase:** 99
**Needs:** operator, store-account
**Surface:** no

**Build order:**

1. Complete Microsoft Partner Center developer enrollment and reserve the product name (`Recto`) as a Windows desktop (`MSIX / PWA`) application. Done when: the app reservation exists in Partner Center and displays its assigned `Package/Identity/Name`, `Package/Identity/Publisher` (`CN=...`), and `PublisherDisplayName`.
2. Record the non-sensitive `Package/Identity` attributes in this section and update `src/app/Package.appxmanifest` for optional `-EnableMsix` builds (`D07 T01 §5`), storing account recovery details and any signing certificates exclusively in the owner's external password vault. Done when: `python scripts/check-package-assets.py` passes and zero secrets exist in git.

- [ ] Complete Microsoft Partner Center developer account registration and identity verification, storing all login and recovery credentials in the owner's external password vault (never in the repository). Done when: Partner Center account status is active. Cheaper substitute: committing PFX signing keys or credentials into the repository tree.
- [ ] Reserve the app name in Partner Center (matching the name cleared in `§1`) and retrieve the Partner Center `Product identity` table (`Package/Identity/Name`, `Package/Identity/Publisher`, `Package/Properties/PublisherDisplayName`). Done when: all 3 identity strings are assigned by Partner Center.
- [ ] Record the 3 non-sensitive `Package/Identity` values in this section and update `src/app/Package.appxmanifest` so `build/build-installer.ps1 -EnableMsix` (`D07 T01 §5`) stamps Store-ready MSIX packages when requested. Done when: `src/app/Package.appxmanifest` carries the assigned identity strings and compiles cleanly.
- [ ] Run a repository secret check (`git grep -iE "BEGIN.*PRIVATE KEY|\.pfx|password"`) confirming zero certificates, private keys, or credentials were added. Done when: the check returns zero matches.
- [ ] Commit: `manual: record Partner Center app reservation and non-sensitive MSIX package identity`

**Test checkpoint:** `python scripts/check-package-assets.py` exits `0` with the updated `src/app/Package.appxmanifest` identity attributes quoted.

## 4. Physical ARM64 reference machine and clean-machine host confirmation

Complete the physical hardware records complementing `D00 T01 §5`'s primary `x64` reference machine record in `docs/reference-machines.md`:
1. When a physical `ARM64` Windows 11 machine (Snapdragon X Plus / X Elite or equivalent) is attached, record its exact CPU, core count, RAM, GPU, and Windows 11 OS build fingerprint in `docs/reference-machines.md` and `docs/plans/2026-10-13-recto-foundation.md` section 8 (replacing the non-binding `ARM64` target hardware class profile).
2. Confirm and record the clean-machine environment (Windows Sandbox `.wsb` configuration, Hyper-V checkpointed VM, or dedicated test PC) used for attended validation of `D07 T01 §6` and `§7`'s Full and Portable installer suites.

> **Corrected:** 2026-10-14 -- Updated after the 2026-10-14 owner decision (`docs/plans/2026-10-13-recto-foundation.md` section 11 item 2) resolved the primary `x64` reference machine in `D00 T01 §5`; this section tracks attaching and naming a specific physical `ARM64` device and confirming the clean-machine host for `D07 T01 §6` and `§7`.

**Phase:** 99
**Needs:** operator
**Surface:** no

**Build order:**

1. Attach the physical `ARM64` Windows 11 device, query its hardware fingerprint via `Get-CimInstance Win32_Processor`, `Win32_PhysicalMemory`, `Win32_VideoController`, and `Win32_OperatingSystem`, and record the exact specification in `docs/reference-machines.md` and `docs/plans/2026-10-13-recto-foundation.md` section 8. Done when: `docs/reference-machines.md` names a specific physical `ARM64` machine with reproducible hardware identifiers.
2. Document the clean-machine test host / VM snapshot specification in `docs/reference-machines.md` (`## Clean-Machine Installer Verification Host`) used by `D07 T01 §6` and `§7`. Done when: `docs/reference-machines.md` contains both the physical `ARM64` record and the clean-machine host specification.

- [ ] Run the PowerShell hardware fingerprint query on the physical `ARM64` Windows 11 machine and record its exact processor model, logical/physical core count, RAM capacity, GPU driver, and Windows 11 build number in `docs/reference-machines.md`. Done when: the `ARM64 Reference Machine` section in `docs/reference-machines.md` contains measured hardware output rather than a generic target class. Cheaper substitute: claiming `ARM64` runtime benchmark numbers without recording the physical `ARM64` machine they were measured on.
- [ ] Update `docs/plans/2026-10-13-recto-foundation.md` section 8's `ARM64` reference machine bullet to match `docs/reference-machines.md`. Done when: both files list identical `ARM64` hardware details.
- [ ] Run `build/dist/Recto-Portable-ARM64.zip` and `recto-tests.exe` (`Release | ARM64`) natively on the physical `ARM64` machine and record the pass summary in `docs/reference-machines.md`. Done when: native `ARM64` test execution is recorded.
- [ ] Record the clean-machine environment (Hyper-V VM checkpoint or Windows Sandbox configuration) in `docs/reference-machines.md` for `D07 T01 §6` and `§7`'s Full Install and Portable Install verification. Done when: the `Clean-Machine Installer Verification Host` section is complete in `docs/reference-machines.md`.
- [ ] Commit: `manual: confirm physical ARM64 reference machine and clean-machine installer host`

**Test checkpoint:** `docs/reference-machines.md` records both the primary `x64` machine (`D00 T01 §5`), the specific physical `ARM64` machine, and the clean-machine installer verification host, and `python scripts/todo-graph.py validate` exits `0`.

## Verification

- Every outcome in `99-manual` is verifiable from a public page or a committed record in `todo/99-manual/TODO-01-operator-setup.md` and `docs/reference-machines.md` by a session that was not present when the operator performed it.
- Zero secrets, private keys, or credentials appear anywhere in the repository (`git grep` clean).
- `python scripts/todo-graph.py validate` passes.
