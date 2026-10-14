---
schema_version: 1
id: store-and-compliance
domain: 07-release
status: active
title: "TODO-02 -- About, GPL-3.0-or-later Compliance and Store Submission"
track: M6
---

# TODO-02 -- About, GPL-3.0-or-later Compliance and Store Submission

> **Goal:** Recto ships legally and identifiably: the SPEC About page with true version data and the full Built-with license list, a complete NOTICE and dependency license audit, opt-in crash reporting that sends nothing by default, localization-ready resources, and a Store submission whose listing carries `GPL-3.0-or-later` as Recto's own terms, ending at certification.

> **Current state:** Measured 2026-10-14. No code exists. The compliance contract is foundation plan section 10's checklist (LICENSE and About license display, About source link to the exact tag, NOTICE completeness, per-dependency GPL-3.0-or-later compatibility, Partner Center custom license terms, CLA before first outside PR) with D2, D6 and D11 behind it. The About surface is SPEC 9.8 (`D-About.png`) including its Built-with list naming `recto-pdf`, Windows App SDK, Direct2D/DirectWrite/WIC/CNG/Windows.Media.Ocr, `zlib`, and Newsreader; crash reporting defaults off (SPEC 9.7). D00 T01 §1 lands LICENSE, CLA and the NOTICE skeleton this file completes.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D1 name, D2 license, D6 Store with own license terms, D11 CLA), 7 (M6 done-when: Store certification passes), 10 (the checklist), 11 (trademark and name reservation items owned by D99 T01).
- `design/SPEC.md` sections 3.5 (banner and social preview), 9.7 (crash toggle, About entry), 9.8 (About contents and Built-with roster).
- -> XREF: D00 T01 §1 -- the `LICENSE`, `CLA.md` and `NOTICE.md` skeleton authored there are completed and audited against the shipped dependency set in §2 here.
- -> XREF: D99 T01 §3 -- the operator's Partner Center account and app reservation this file's §4 submits into; listing text and license terms are prepared here and entered there together.

## Outcome

- About tells the exact truth: version and architecture, GPL-3.0-or-later shown, source link resolving to this release's tag, every bundled component with its license.
- Every plan section 10 checkbox is checked with evidence, in the plan and in reality.
- The Store listing is submitted with Recto's own GPL-3.0-or-later terms and passes certification (M6 done).

**Adjacency:** list=not-applicable (the Store lists Recto; Recto lists its components inside About, section 1); document=applicable @ D07 T02 §1; settings=applicable @ D02 T04 §2; reporting=applicable @ D07 T02 §3; notifications=not-applicable (Store-driven); permissions=not-applicable (submission roles are Partner Center's, D99 T01 §3); audit=applicable @ D07 T02 §2; exchange=not-applicable (no data export; crash payloads are reporting, section 3); reverse=not-applicable (delisting and rollback are Partner Center operations recorded in section 4's runbook)

**Adjacency rationale:** About is the carried document (license and provenance the user can read in-app), the dependency audit is this file's audit surface, and crash reporting is the one reporting channel Recto owns, opt-in by SPEC. The submission runbook records the reverse paths (halt rollout, delist) as operator procedure rather than product features.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | About page with license and Built-with | D02 T04 §2 | [ ] |
| 2 | §2 | NOTICE completion and dependency license audit | §1, D00 T01 §1 | [ ] |
| 3 | §3 | Crash reporting (opt-in) and localization readiness | §1 | [ ] |
| 4 | §4 | Store listing, submission and certification | D07 T01 §3, §2 | [ ] |

## 1. About page with license and Built-with

SPEC 9.8 exactly: icon and lockup, version and architecture from the running package (never hard-coded strings like the mockups' placeholders), the one-sentence description and GPL-3.0-or-later statement, the details box (Windows App SDK version, `recto-pdf` engine version and git commit, source link to the exact tag of the running version), View source (accent), License (full text in-app), Copy version info producing a bug-report-ready block, report-a-problem, contribute and keyboard-shortcut links (the last to D02 T04 §4's document), and the Built-with card listing `recto-pdf`, Windows App SDK, Direct2D / DirectWrite / WIC / CNG / Windows.Media.Ocr, `zlib`, and Newsreader with their licenses, generated from the same data §2 audits so About and NOTICE cannot drift.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.8 with `design/screenshots/D-About.png`.
**Job:** The user (and anyone auditing GPL-3.0-or-later compliance) can see what Recto is, under what license, built from what. Consumer: the clipboard block lands in bug reports; the source link lands on the exact tree.
**Treatment:** Version data read from the package and build-generated constants (`recto-pdf` version and git commit generated at build), Built-with rendered from the audited dependency data. Cheaper substitute that fails the checkpoint: hand-typed version strings that are wrong by the second release.
**Chrome:** consume the lockup graphics, settings-group card shapes and dialog family for the license viewer. Do not invent a marketing page.

**Build order:**

1. Build-time version stamping: app version from the package manifest identity at runtime, `recto-pdf` engine version and git commit generated into a header during build, source URL composed from the repo remote plus tag. Done when: a development build's About names its actual commit-described version, proving nothing is typed by hand.
2. The page and license viewer per the board, Copy version info asserted by clipboard readback in tests. Done when: the surface account covers every SPEC 9.8 element.

- [ ] About with generated version truth. Done when: bumping the package version changes About with no source edit. Cheaper substitute that fails the checkpoint: constants updated by release checklist discipline.
- [ ] License text in-app identical to root `LICENSE`. Done when: the two are byte-compared in a test or the file is embedded from the root at build.
- [ ] Commit: `release: About page with generated versions, license and Built-with`

**Test checkpoint:** screenshot against `D-About.png`, clipboard block quoted, license byte-compare green.

## 2. NOTICE completion and dependency license audit

Complete plan section 10's documentary half: `NOTICE.md` lists every dependency that ships (`recto-pdf`, Windows App SDK, `zlib`, GoogleTest marked test-only and not shipped, Newsreader as outlines with OFL attribution), each with its license, and the audit verifies GPL-3.0-or-later compatibility per dependency with the reasoning recorded. The Built-with data §1 renders comes from this audit's table (one source, generated or test-compared).

**Phase:** 6
**Needs:** none
**Surface:** no

**Build order:**

1. Enumerate the shipped dependency set from the actual build (linked libraries and `vcpkg.json` manifest), not from the plan's expectations. Done when: the enumeration method is repeatable and its output is the audit's first table.
2. Complete `NOTICE.md` and the compatibility audit in `docs/license-audit.md` (per dependency: license, where it ships, compatibility conclusion). Done when: plan section 10's NOTICE and compatibility checkboxes can be ticked with this file as evidence, and About's Built-with matches it (test or generation).

- [ ] Build-derived dependency enumeration. Done when: adding a library to the build without a NOTICE row is detectable by re-running the enumeration. Cheaper substitute: copying the plan's expected list forward.
- [ ] Compatibility conclusions recorded per dependency. Done when: no shipped dependency lacks a stated license and conclusion.
- [ ] Commit: `release: complete NOTICE from a build-derived dependency audit`

**Test checkpoint:** enumeration re-run diffs empty against `NOTICE.md`'s roster, and §1's Built-with comparison green.

## 3. Crash reporting (opt-in) and localization readiness

Crash reporting per SPEC 9.7: off by default; when the user opts in, a crash produces a minidump-plus-log package through the platform-appropriate channel (this section decides and records the service, or a local save-and-share flow if no service is chosen, with the privacy statement matching reality either way). No document content, file names or paths beyond the app's own install path leave the machine; the payload is inspectable before sending where the channel allows. Localization readiness: all user-facing strings through resource files (`Resources.resw` or the x:Uid pattern the app adopted), no string literals in XAML or code outside the resource system (searched), pseudolocalization pass proving layout survival.

**Phase:** 6
**Needs:** windows-host, display-session, owner-decision
**Surface:** yes

**Fidelity:** SPEC section 9.7 (Performance and privacy group) with `D-Settings.png`; the report flow itself has no SPEC board and follows the dialog family.
**Job:** The user can choose to help fix crashes without surrendering document privacy, and Recto can be translated later without a refactor. Consumer: the chosen crash channel's inbox; future translators work from resource files only.
**Treatment:** Opt-in gate at the settings store, payload allowlist in code (fields enumerated, not filtered after the fact). Cheaper substitute that fails the checkpoint: default-on telemetry, or a payload assembled then scrubbed.
**Chrome:** consume the §1 version block for report identity and the dialog family for consent wording. Do not invent a second identity format.

**Build order:**

1. Decide and record the crash channel with the owner (service versus local package), then implement capture (minidump callback or platform failure reporting), the field allowlist, and the off-by-default gate read by D02 T04 §2's toggle. Done when: a planted crash with opt-in produces exactly the allowlisted payload shown to the reviewer, and with opt-out produces nothing and no network traffic.
2. String-resource sweep: move every user-facing literal into resources, add the search that keeps it true, run a pseudolocalized build through the main surfaces. Done when: the literal search returns only the recorded non-user-facing exceptions and layouts hold under pseudolocale expansion.

- [ ] Opt-in crash capture with an enumerated payload. Done when: the payload is diffed field by field against the allowlist in the findings. Cheaper substitute that fails the checkpoint: full-memory dumps including document bytes.
- [ ] All user-facing strings resourced, pseudolocale pass recorded. Done when: the Settings and Convert pages survive 40 percent string expansion without clipping (screenshots).
- [ ] Commit: `release: opt-in crash reporting and localization-ready resources`

**Test checkpoint:** planted-crash payloads quoted for opt-in and opt-out, literal search output clean, pseudolocale screenshots in findings. The channel decision removes this section's `owner-decision` Need when recorded.

## 4. Store listing, submission and certification

The submission itself, prepared as a runbook so the second release is routine: listing text (honest per SPEC principle 4, conversion quality wording from D04 T03 §2), screenshots from the real app per the SPEC boards, age rating and category answers, the custom license terms carrying the GPL-3.0-or-later text (D6; the Store's standard license explicitly not used), the tagged release (packages from D07 T01 §2's CI at that tag), submission through the operator's Partner Center session (D99 T01 §3), and certification watch with failure triage back through `add-todo`. Rollout halt and delist steps close the runbook.

**Phase:** 6
**Needs:** store-account, attended, windows-host
**Surface:** yes

**Fidelity:** The listing's Recto identity per SPEC section 3.5 (social preview already set by D99 T01 §2) and screenshots of the shipped surfaces, which must match their SPEC boards as built.
**Job:** Anyone can install Recto from the Microsoft Store under GPL-3.0-or-later terms. Consumer: Store users; the runbook's next operator at the second release.
**Treatment:** Submission of CI-built tag artifacts, custom license terms pasted from the repo `LICENSE`. Cheaper substitute that fails the checkpoint: a locally built package differing from CI's, or the standard Store license silently accepted.
**Chrome:** consume D07 T01's packages and §1's version truth; listing screenshots are the app's real surfaces, not the design mockups. Do not invent feature claims the review in D04 T03 §2 did not measure.

**Build order:**

1. Prepare listing assets and the runbook `docs/store-runbook.md` (assets, license-terms source, tag procedure, certification triage, halt and delist). Done when: a second operator could submit from the runbook alone.
2. Tag the release, take CI's packages at that tag, submit with the operator, and track certification to a pass, triaging any failure into owned sections and resubmitting. Done when: the live listing URL is recorded and the installed Store build's About source link resolves to the tag.

- [ ] Runbook and listing assets complete. Done when: the custom license text in the submission equals the repo `LICENSE` byte for byte (compared from the entered text). Cheaper substitute that fails the checkpoint: paraphrased license terms.
- [ ] Certification passed and live listing verified from a clean machine install (with D07 T01 §3's steps against the Store build). Done when: M6's done-when is quotable from the live listing.
- [ ] Commit: `release: Store runbook, listing and v1 submission record`

**Test checkpoint:** the live listing (its URL recorded in `docs/store-runbook.md`), certification result and Store-build About verification quoted in findings; plan section 10's checklist fully ticked with each item's evidence named.

## Verification

- Foundation plan section 10 complete, checkbox by checkbox, with evidence in this file's findings.
- M6 done-when (Store certification passes) met and recorded; `python scripts/todo-graph.py validate` passes.
- About, NOTICE and the listing agree on version, license and components with zero hand-copied strings between them.
