---
schema_version: 1
id: store-and-compliance
domain: 07-release
status: active
title: "TODO-02 -- About, GPL-3.0-or-later Compliance, Local Crash Bundle and Release Distribution"
track: M6
---

# TODO-02 -- About, GPL-3.0-or-later Compliance, Local Crash Bundle and Release Distribution

> **Goal:** Recto ships legally and identifiably: the SPEC About page with true version data and the full Built-with license list, a complete NOTICE and dependency license audit, opt-in local minidump-plus-log save-and-share crash bundles that send zero network telemetry, localization-ready resources, and a complete GitHub Release distribution bundle (Inno Setup Full/Portable installers, Portable `.zip` archives, `SHA256SUMS.txt`) plus an optional Microsoft Store submission kit carrying `GPL-3.0-or-later` as Recto's own terms.

> **Current state:** Measured 2026-10-14. No code exists. The compliance contract is foundation plan section 10's checklist (LICENSE and About license display, About source link to the exact tag, NOTICE completeness, per-dependency GPL-3.0-or-later compatibility, custom license terms for optional Store submission, CLA before first outside PR) with D2, D6, D11 and D16 behind it. On 2026-10-14 the owner approved the **local minidump-plus-log save-and-share `.zip` bundle** for §3 and made Inno Setup (`Full Install` + `Self-Contained Portable`) the primary distribution channel with Microsoft Store MSIX submission as an optional extra in §4. The About surface is SPEC 9.8 (`D-About.png`) including its Built-with list naming `recto-pdf`, Windows App SDK, Direct2D/DirectWrite/WIC/CNG/Windows.Media.Ocr, and Newsreader; local crash report bundles default off (SPEC 9.7). D00 T01 §1 lands LICENSE, CLA and the NOTICE skeleton this file completes.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D1 name, D2 license, D6 Inno Setup Full/Portable + optional Store with own license terms, D11 CLA, D16 local crash bundle), 7 (M6 done-when), 10 (the checklist), 11 (trademark and optional Store reservation items owned by D99 T01).
- `design/SPEC.md` sections 3.5 (banner and social preview), 9.7 (Portable mode and local crash bundle toggles, About entry), 9.8 (About contents and Built-with roster).
- -> XREF: D00 T01 §1 -- the `LICENSE`, `CLA.md` and `NOTICE.md` skeleton authored there are completed and audited against the shipped dependency set in §2 here.
- -> XREF: D99 T01 §3 -- the operator's optional Partner Center account and app reservation that §4's optional Store submission kit prepares for.

## Outcome

- About tells the exact truth: version and architecture, GPL-3.0-or-later shown, source link resolving to this release's tag, every bundled component with its license.
- Every plan section 10 checkbox is checked with evidence, in the plan and in reality.
- Opt-in crash reporting writes an inspectable local `.zip` bundle (`minidump.dmp` + ring-buffer log + version JSON, zero document bytes, zero network traffic) in `%LOCALAPPDATA%\Recto\crash-reports` or `<exe-dir>\data\crash-reports` in Portable mode.
- The v1 GitHub Release distribution bundle (Inno Setup Full/Portable installers, Portable `.zip` archives, `SHA256SUMS.txt`) and optional Microsoft Store submission kit (`docs/release-and-store-runbook.md` with custom `GPL-3.0-or-later` terms) are complete and verified.

**Adjacency:** list=not-applicable (Recto lists its components inside About, section 1); document=applicable @ D07 T02 §1; settings=applicable @ D02 T04 §2; reporting=applicable @ D07 T02 §3; notifications=not-applicable (none originate here); permissions=not-applicable (no roles); audit=applicable @ D07 T02 §2; exchange=not-applicable (crash bundles are reporting in section 3); reverse=not-applicable (release rollback and optional Store delisting are operator procedures recorded in section 4's runbook)

**Adjacency rationale:** About is the carried document (license and provenance the user can read in-app), the dependency audit is this file's audit surface, and the local crash bundle is the one reporting artifact Recto produces, opt-in by SPEC. The release and optional Store runbook records rollback and delist paths as operator procedure rather than product features.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | About page with license and Built-with | D02 T04 §2 | [ ] |
| 2 | §2 | NOTICE completion and dependency license audit | §1, D00 T01 §1 | [ ] |
| 3 | §3 | Crash reporting (opt-in local minidump-plus-log bundle) and localization readiness | §1 | [ ] |
| 4 | §4 | Release bundle, GitHub Release runbook and optional Store submission kit | D07 T01 §3, §2 | [ ] |

## 1. About page with license and Built-with

SPEC 9.8 exactly: icon and lockup, version and architecture from the running binary (never hard-coded strings like the mockups' placeholders), the one-sentence description and GPL-3.0-or-later statement, the details box (Windows App SDK version, `recto-pdf` engine version and git commit, active install mode Standard vs Portable, source link to the exact tag of the running version), View source (accent), License (full text in-app), Copy version info producing a bug-report-ready block, report-a-problem, contribute and keyboard-shortcut links (the last to D02 T04 §4's document), and the Built-with card listing `recto-pdf`, Windows App SDK, Direct2D / DirectWrite / WIC / CNG / Windows.Media.Ocr, and Newsreader with their licenses, generated from the same data §2 audits so About and NOTICE cannot drift.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.8 with `design/screenshots/D-About.png`.
**Job:** The user (and anyone auditing GPL-3.0-or-later compliance) can see what Recto is, under what license, built from what. Consumer: the clipboard block lands in bug reports; the source link lands on the exact tree.
**Treatment:** Version data read from build-generated constants (`recto-pdf` version and git commit generated at build), Built-with rendered from the audited dependency data. Cheaper substitute that fails the checkpoint: hand-typed version strings that are wrong by the second release.
**Chrome:** consume the lockup graphics, settings-group card shapes and dialog family for the license viewer. Do not invent a marketing page.

**Build order:**

1. Build-time version stamping: app version, `recto-pdf` engine version and git commit generated into a header during build, source URL composed from the repo remote plus tag. Done when: a build's About names its actual commit-described version, proving nothing is typed by hand.
2. The page and license viewer per the board, Copy version info asserted by clipboard readback in tests. Done when: the surface account covers every SPEC 9.8 element.

- [ ] About with generated version truth. Done when: bumping the version changes About with no source edit. Cheaper substitute that fails the checkpoint: constants updated by release checklist discipline.
- [ ] License text in-app identical to root `LICENSE`. Done when: the two are byte-compared in a test or the file is embedded from the root at build.
- [ ] Commit: `release: About page with generated versions, license and Built-with`

**Test checkpoint:** screenshot against `D-About.png`, clipboard block quoted, license byte-compare green.

## 2. NOTICE completion and dependency license audit

Complete plan section 10's documentary half: `NOTICE.md` lists every dependency that ships (`recto-pdf`, Windows App SDK, GoogleTest marked test-only and not shipped, Newsreader as outlines with OFL attribution), each with its license, and the audit verifies GPL-3.0-or-later compatibility per dependency with the reasoning recorded. The Built-with data §1 renders comes from this audit's table (one source, generated or test-compared).

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

## 3. Crash reporting (opt-in local minidump-plus-log bundle) and localization readiness

Crash reporting per SPEC 9.7 and the 2026-10-14 owner decision (D16): off by default; when the user opts in (`Save local crash report bundles`), an unhandled exception or crash callback writes a local inspectable save-and-share `.zip` bundle (`crash-reports/recto-crash-<timestamp>.zip` inside `AppPaths::data_dir()`, which is `%LOCALAPPDATA%\Recto\crash-reports\` in standard mode or `<exe-dir>\data\crash-reports\` in Portable mode) containing a thread/stack minidump (`MiniDumpNormal`, excluding document heap buffers), the structured ring-buffer log (`D16`), and the §1 version info JSON. Zero network traffic is ever sent; on next launch Recto offers a non-intrusive banner/dialog to open the `crash-reports/` folder so the user can inspect and optionally attach the `.zip` to a GitHub issue. Localization readiness: all user-facing strings through resource files (`Resources.resw` or the `x:Uid` pattern), no unresourced string literals in XAML or UI code (searched), and a pseudolocalization pass proving layout survival.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.7 (Performance and privacy group) with `D-Settings.png`; the local crash bundle notification follows the SPEC 9.9 dialog family.
**Job:** The user can choose to capture local diagnostic crash bundles without surrendering document privacy or sending network telemetry, and Recto can be translated later without a refactor. Consumer: the user inspecting `crash-reports/*.zip`; future translators working from resource files only.
**Treatment:** Opt-in gate at the settings store (`SettingsService::save_crash_reports()`), strict payload allowlist in code (`version.json`, `ring-log.txt`, `minidump.dmp` with thread stacks only), and `AppPaths::data_dir()` path resolution so Portable mode keeps crash bundles inside `<exe-dir>\data\crash-reports\`. Cheaper substitute that fails the checkpoint: default-on capture, network uploads, or full-heap dumps containing document bytes.
**Chrome:** consume the §1 version block for report identity and the dialog family for the next-launch "Crash report saved" prompt. Do not invent a second identity format.

**Build order:**

1. Implement the unhandled-exception filter (`SetUnhandledExceptionFilter` + `MiniDumpWriteDump` with `MiniDumpNormal`), the allowlisted `.zip` bundle writer (`version.json`, `ring-log.txt`, `minidump.dmp`), and the off-by-default gate read from D02 T04 §2's toggle. Done when: a planted test crash with opt-in produces `crash-reports/recto-crash-*.zip` containing only the three allowlisted files (and writes inside `<exe-dir>\data\crash-reports\` when Portable mode is active), and with opt-out writes nothing.
2. String-resource sweep: move every user-facing literal into `Resources.resw`, add the search check that keeps it true, and run a pseudolocalized build through the main surfaces. Done when: the literal search returns only the recorded non-user-facing exceptions and layouts hold under pseudolocale expansion.

- [ ] Opt-in local `.zip` crash bundle capture with an enumerated allowlist and Portable mode path support. Done when: the `.zip` archive entries and `version.json` fields are verified by `recto-tests --gtest_filter=CrashReport.*` and zero network sockets are opened. Cheaper substitute that fails the checkpoint: full-memory dumps including document bytes.
- [ ] All user-facing strings resourced, pseudolocale pass recorded. Done when: the Settings and Convert pages survive 40 percent string expansion without clipping (screenshots).
- [ ] Commit: `release: opt-in local crash report bundle and localization-ready resources`

**Test checkpoint:** `recto-tests --gtest_filter=CrashReport.*` green with planted-crash payloads quoted for opt-in (standard and Portable modes) and opt-out, literal search output clean, and pseudolocale screenshots in findings.

## 4. Release bundle, GitHub Release runbook and optional Store submission kit

Assemble the complete v1 release distribution bundle and runbook (`docs/release-and-store-runbook.md`) so releasing via GitHub Releases (Inno Setup Full/Portable installers + Portable `.zip` archives + `SHA256SUMS.txt`) is automated and repeatable, and optional Microsoft Store submission (`D99 T01 §3`) is ready to run whenever the owner chooses: real-app screenshots matching the SPEC boards, honest listing copy (`docs/store-listing.md`, incorporating conversion quality wording from D04 T03 §2), custom license terms carrying the exact `GPL-3.0-or-later` text (D6; the Store's standard EULA explicitly not used), tagged release verification (`v1.0.0`), and foundation plan section 10 compliance sign-off.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `docs/release-and-store-runbook.md` and `docs/store-listing.md` (GitHub Release tag and asset upload procedure for `RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip`, `Recto-Portable-ARM64.zip`, `SHA256SUMS.txt`, rollback steps, plus the optional Partner Center MSIX submission checklist, age-rating answers, real-app screenshots, and custom `GPL-3.0-or-later` license terms matching root `LICENSE`). Done when: a second operator can publish a GitHub Release or submit the optional MSIX to Partner Center from the runbook alone.
2. Run the full release verification gate (`build/build-installer.ps1` + `tests/scripts/verify_installer.ps1` + license/NOTICE/About parity check), generate `build/dist/SHA256SUMS.txt`, and verify every checkbox in foundation plan section 10 with quoted evidence. Done when: all release artifacts, checksums, and plan section 10 items are verified and recorded.

- [ ] Release runbook (`docs/release-and-store-runbook.md`), optional Store listing kit (`docs/store-listing.md`), and custom `GPL-3.0-or-later` license terms complete. Done when: the custom license text in the Store kit equals root `LICENSE` byte for byte. Cheaper substitute that fails the checkpoint: paraphrased license terms or missing portable release artifacts.
- [ ] Full v1 distribution bundle (`RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip`, `Recto-Portable-ARM64.zip`, `SHA256SUMS.txt`) verified end to end and foundation plan section 10 checklist ticked with evidence. Done when: M6's definition of done is met and quotable from the verified release artifacts.
- [ ] Commit: `release: v1 Inno Setup and Portable release bundle, runbook and optional Store kit`

**Test checkpoint:** `build/dist/SHA256SUMS.txt` and `verify_installer.ps1` output quoted in findings, byte-comparison of custom license terms against `LICENSE` exits 0, and foundation plan section 10's checklist is fully ticked with each item's evidence named.

## Verification

- Foundation plan section 10 complete, checkbox by checkbox, with evidence in this file's findings.
- M6 done-when met and recorded; `python scripts/todo-graph.py validate` passes.
- About, NOTICE, the Inno Setup installer metadata, and the release/Store kit agree on version, license and components with zero hand-copied strings between them.
