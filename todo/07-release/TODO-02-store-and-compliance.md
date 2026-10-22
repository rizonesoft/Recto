---
schema_version: 1
id: store-and-compliance
domain: 07-release
status: active
title: "TODO-02 -- About, GPL-3.0-or-later Compliance, Local Crash Bundle and Release Distribution"
track: M6
---

# TODO-02 -- About, GPL-3.0-or-later Compliance, Local Crash Bundle and Release Distribution

> **Goal:** Ship Recto with complete legal, license, privacy, and localization hygiene: build-time version and git provenance generation (`§1`), the `SPEC 9.8` `AboutPage` (`D-About.png`) with in-app `LICENSE` viewer and `Built-with` component table (`§2`), a build-derived dependency enumeration script and `GPL-3.0-or-later` compatibility audit in `NOTICE.md` and `docs/license-audit.md` (`§3`), opt-in local minidump-plus-log save-and-share crash bundles (`crash-reports/recto-crash-<timestamp>.zip`) that send zero network telemetry (`§4`), full string extraction to `src/app/strings/en-US.rc` (Win32 `STRINGTABLE`, `ui::Strings`) with a 40%-expansion pseudolocale layout pass (`§5`), the GitHub Release and optional Microsoft Store runbook and listing kit (`§6`), and the final verified v1 distribution bundle (`RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip`, `Recto-Portable-ARM64.zip`, `SHA256SUMS.txt`) closing foundation plan section 10 (`§7`).

> **Current state:** Measured 2026-10-14. `D00 T01 §1` landed `LICENSE` (`GPL-3.0-or-later`), `CLA.md`, and the initial `NOTICE.md` skeleton. No `AboutPage`, build-version generator, crash bundle writer, localization sweep, or release runbook exists yet. On 2026-10-14 the owner approved the opt-in **local minidump-plus-log save-and-share `.zip` bundle** (`D16`) and made Inno Setup (`Full Install` + `Self-Contained Portable`) plus Portable `.zip` archives the primary distribution mechanism with Microsoft Store MSIX submission as an optional extra (`D6`). 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); UI references rewritten against `todo/10-ui/`.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D1 name, D2 `GPL-3.0-or-later`, D6 Inno Setup Full/Portable + optional Store with custom `GPL-3.0-or-later` terms, D11 CLA, D16 local crash bundle), 7 (M6 done-when), 10 (licensing and compliance checklist), and 11 (trademark and optional Store reservation owned by `D99 T01`).
- `design/SPEC.md` sections 3.5 (banner and social preview), 9.7 (`Portable mode`, `Save local crash report bundles`, and `About Recto` rows), and 9.8 (`About` page layout, `D-About.png`, and `Built-with` roster).
- -> XREF: D00 T01 §1 -- the root `LICENSE`, `CLA.md`, and `NOTICE.md` skeleton authored there are completed and verified against the actual linked dependency set in `§2` and `§3` here.
- -> XREF: D02 T04 §6 -- the `About Recto` navigation link and the off-by-default `Save local crash report bundles` toggle in Settings (`D02 T04 §6`) wire into `§2` and `§4` here.
- -> XREF: D02 T04 §10 -- the `KeyboardShortcutsDialog` (`Ctrl+/`) built there is opened by the `Keyboard shortcuts` action link on `AboutPage` (`§2`).
- -> XREF: D03 T02 §6 -- the M3 accessibility verification pass (`D03 T02 §6`) and interactive markup/forms/UIA peers (`D03 T03 §5`, `§10`) complete before the release kit and real-app screenshots in `§6`.
- -> XREF: D04 T03 §6 -- the honest PDF-to-Word conversion scope wording recorded there is incorporated into `docs/store-listing.md` (`§6`).
- -> XREF: D06 T01 §8 -- the `TextTables.*` (`D06 T01 §4`), `RoundTrips.*` (`D06 T01 §6`), reference performance baseline (`D06 T01 §8`), and security/fuzz/broker gates (`D06 T02 §4`, `§6`) must all be green before assembling the v1 release kit and distribution bundle in `§6` and `§7`.
- -> XREF: D07 T01 §7 -- the verified `RectoSetup-*.exe` and `Recto-Portable-*.zip` artifacts tested there are assembled into the signed-off v1 release bundle in `§6` and `§7`.
- -> XREF: D99 T01 §3 -- the operator's optional Partner Center account and app reservation that `§6`'s optional Store submission kit prepares for.
- -> XREF: D99 T01 §8 -- the operator's native-speaker review runs `scripts/set-language-reviewed.py` from `§17` here per language, and the next release picks up the flag.
- -> XREF: D99 T01 §11 -- the v1 release checklist publishes the GitHub Release from the candidate built by `§7` here.
- -> XREF: D07 T03 §2 -- `Report a problem` reuses §4's diagnostics text, and §4 and §5 there extend §6's release runbook with closing on release and the release gate.
- -> XREF: D08 T02 §35 -- AssistantDisabled policy.
- -> XREF: D07 T01 §19 -- runbook.
- -> XREF: D07 T01 §20 -- release kit.
- -> XREF: D08 T01 §15 -- AssistantDisabled policy.
- -> XREF: D07 T03 §7 -- update notice policy.
- -> XREF: D02 T04 §2 -- SettingSource and locks.
- -> XREF: D02 T04 §4 -- lock note rows.
- -> XREF: D02 T04 §6 -- find a setting filter.
- -> XREF: D02 T04 §2 -- UiLanguage setting.
- -> XREF: D06 T02 §7 -- fips_mode in crash bundle.
- -> XREF: D02 T06 §5 -- CloudConnectorsDisabled policy.
- -> XREF: D02 T06 §15 -- crash bundle scan.
- -> XREF: D06 T04 §23 -- the `D-Managed.png` board and `design/SPEC.md` section 9.7.1 that §11 here build against.
- -> XREF: D06 T04 §25 -- the `D-Lifecycle.png` board and `design/SPEC.md` section 9.19 that §14, §16 here build against.
- -> XREF: D10 T03 §7 -- `ui::Strings`, the `STRINGTABLE` `.rc` layout, `scripts/check-strings.py`, the pseudo-locale generator and the right-to-left mirroring that §5, §13, §14, §15, §16 and §17 here build on.
- -> XREF: D10 T02 §23 -- the control gallery whose headless `qps-ploc` render §5 here extends to the real screens.
- -> XREF: D10 T02 §17 -- the `ui::Dialog` that `LicenseDialog` (§2) and the control set (`D10 T01 §11`, `D10 T02 §16`) of the About page build on.
- -> XREF: D10 T01 §8 -- the generated `Recto*Brush` tokens that the About page, language row and right-to-left surfaces consume.

## Outcome

- `AboutPage` (`§2`) displays build-stamped version truth (`§1`): app version, CPU architecture (`x64` or `ARM64`), `recto-ui` toolkit version, `recto-pdf` engine version and git commit SHA, active install mode (`Standard` vs `Portable`), a source link to the exact release tag, an in-app `LICENSE` modal byte-identical to root `LICENSE`, a one-click `Copy version info` clipboard block, and a `Built-with` card generated from the audited dependency roster (`§3`).
- `scripts/audit-dependencies.py` enumerates every shipped library and font from the actual build and verifies 100% parity with `NOTICE.md`, `docs/license-audit.md`, and `AboutPage`'s `Built-with` table (`§3`).
- When opted in via Settings (`Save local crash report bundles`, off by default), unhandled exceptions write a local `crash-reports/recto-crash-<timestamp>.zip` inside `AppPaths::data_dir()` containing only `version.json`, `ring-log.txt` (`D16`), and a thread-stack `minidump.dmp` (`MiniDumpNormal`, zero document heap bytes) with zero network sockets opened (`§4`).
- Every user-facing UI string lives in `src/app/strings/en-US.rc` (enforced by `scripts/check-unresourced-strings.py`) and survives a 40%-expansion pseudolocale layout pass without clipping (`§5`).
- `docs/release-and-store-runbook.md` and `docs/store-listing.md` (`§6`) and the final v1 distribution bundle (`§7`) verify every checkbox in foundation plan section 10 with quoted evidence.
- Administrators can lock Recto with Group Policy: six documented policies in `policy/Recto.admx`, read by `PolicyProvider`, applied by `SettingsStore` with a visible "Managed by your organization" note, and shipped as `Recto-PolicyTemplates-<version>.zip` (`§8` to `§12`).
- Recto ships ten reviewed interface languages with a checked translation process, a language setting, localised errors and plurals, right-to-left readiness and matching installer languages (`§13` to `§17`).

**Adjacency:** list=not-applicable (component roster lives inside AboutPage in §2); document=applicable @ D07 T02 §2; settings=applicable @ D02 T04 §6; reporting=applicable @ D07 T02 §4; notifications=not-applicable (next-launch crash folder prompt is part of §4's reporting flow); permissions=not-applicable (no user roles); audit=applicable @ D07 T02 §3; exchange=not-applicable (crash `.zip` bundles and clipboard version blocks are covered in §2 and §4); reverse=not-applicable (release rollback and optional Store delisting are operator procedures documented in §6's runbook)

**Adjacency rationale:** `AboutPage` (`§2`) is the in-app legal/provenance document surface, `NOTICE.md` + `docs/license-audit.md` (`§3`) is the build-verified compliance audit, and the opt-in local crash bundle (`§4`) is the sole diagnostic reporting artifact Recto produces.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Build-time version and provenance generator (version, SHA, `Standard` vs `Portable`) | D02 T04 §6 | [ ] |
| 2 | §2 | `AboutPage` (`SPEC 9.8`, `D-About.png`: version truth, `LICENSE` modal and `Built-with`) | §1, D02 T04 §10, D10 T01 §11, D10 T02 §16, D10 T02 §17 | [ ] |
| 3 | §3 | Build-derived dependency enumeration script, `NOTICE.md` and `docs/license-audit.md` | §2, D00 T01 §1 | [ ] |
| 4 | §4 | Opt-in local crash report bundle writer (`MiniDumpNormal` + D16 ring log `.zip` prompt) | §2 | [ ] |
| 5 | §5 | Localization readiness (`en-US.rc` string extraction and 40% pseudolocale check) | §2, D10 T03 §7, D10 T02 §23 | [ ] |
| 6 | §6 | GitHub Release runbook (`docs/release-and-store-runbook.md`) and Store submission kit | §3, §4, §5, D03 T02 §6, D03 T03 §5, D03 T03 §10, D04 T03 §6, D06 T01 §4, D06 T01 §6, D06 T01 §8, D06 T02 §4, D06 T02 §6, D07 T01 §7 | [ ] |
| 7 | §7 | End-to-end v1 distribution bundle (`Setup.exe`, `Portable.zip`, `SHA256SUMS.txt`) & GPL | §6, §12, §17 | [ ] |
| 8 | §8 | Group Policy catalogue and templates: `policy/Recto.admx` and `Recto.adml` | -- | [ ] |
| 9 | §9 | `PolicyProvider`: read machine and user policy values, typed and bounded | D01 T02 §10 | [ ] |
| 10 | §10 | `SettingsStore` policy precedence: locked settings and policy gates | §9, D02 T04 §2 | [ ] |
| 11 | §11 | "Managed by your organization" notes on locked settings and `recto-cli policy show` | §10, D02 T04 §4, D02 T04 §6, D02 T03 §11, D06 T04 §23, D10 T02 §19 | [ ] |
| 12 | §12 | Policy templates release artifact and end-to-end Group Policy proof | §6, §8, §11, D07 T01 §5 | [ ] |
| 13 | §13 | Translation catalogue: languages manifest, glossary, plural rules and the checker | §5, D10 T03 §7 | [ ] |
| 14 | §14 | Interface language setting: Follow Windows or a shipped language | §13, D02 T04 §2, D02 T04 §6, D02 T03 §11, D06 T04 §25, D10 T03 §7, D10 T02 §9, D10 T02 §20 | [ ] |
| 15 | §15 | Localised errors, plurals, dates and numbers: no English assembled in code | §13, D01 T01 §1, D10 T03 §7 | [ ] |
| 16 | §16 | Right-to-left readiness: flow direction, mirrored icons and a mirrored pseudolocale | §13, §14, D06 T04 §25, D10 T03 §7 | [ ] |
| 17 | §17 | First language set: ten drafted translations, reviewed flag and installer languages | §12, §14, §15, §16, D07 T01 §4, D10 T03 §7 | [ ] |

## 1. Build-time version and provenance generator (version, SHA, `Standard` vs `Portable`)

Eliminate hand-typed version strings across the codebase by writing a deterministic build-time generator (`scripts/generate-version-header.py` wired into `Directory.Build.targets` before `ClCompile`) that reads the canonical version from `VERSION` (`1.0.0`), queries `git rev-parse --short=12 HEAD`, records the `recto-ui` toolkit version (the in-tree toolkit versions with `VERSION`), embeds the root `LICENSE` text and component metadata as `constexpr` string views in `build/generated/recto_version.h`, and provides `recto::core::VersionInfo::current(bool is_portable)` returning the structured runtime provenance block used by `AboutPage` (`§2`), `recto-cli --version`, and `version.json` inside crash bundles (`§4`).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Create root `VERSION` (`1.0.0`), author `scripts/generate-version-header.py` emitting `build/generated/recto_version.h` (only overwriting the output file when content actually changes so incremental MSBuild compiles never re-trigger needlessly), and wire a `BeforeTargets="ClCompile"` MSBuild target in `Directory.Build.targets`. Done when: `.\build.cmd` generates `build/generated/recto_version.h` and a second run of `scripts/generate-version-header.py` preserves the file mtime when nothing changed.
2. Implement `recto::core::VersionInfo` (`src/core/version_info.h` and `src/core/version_info.cpp`) exposing `app_version`, `architecture` (`"x64"` or `"ARM64"`), `windows_app_sdk_version`, `engine_name_and_version` (`"recto-pdf 1.0.0"`), `git_commit_sha`, `source_tag_url` (`"https://github.com/rizonesoft/Recto/tree/v1.0.0"`), `license_spdx` (`"GPL-3.0-or-later"`), `embedded_license_text`, `install_mode` (`"Portable"` when `AppPaths::is_portable()` is true, else `"Standard"`), `to_clipboard_markdown()`, and `to_json()`. Done when: `VersionInfo.*` unit tests in `recto-tests` pass on `x64` and `ARM64`.

- [ ] Read the canonical version from root `VERSION` (`1.0.0`) in `scripts/generate-version-header.py`, which stamps the same value as the `recto-ui` toolkit version, never hard-coding version strings in C++ source files. Done when: changing `VERSION` in a test `TempDir` updates `app_version` and `source_tag_url` in the generated header with zero C++ edits. Cheaper substitute: typing `"0.4.0"` or `"1.0.0"` as string literals inside `AboutPage.cpp` that drift out of sync across `Recto.exe`, `recto-cli.exe`, and `installer/recto.iss`.
- [ ] Stamp the exact `git rev-parse --short=12 HEAD` commit SHA and build target architecture (`#if defined(_M_ARM64) "ARM64" #else "x64" #endif`) into `build/generated/recto_version.h`. Done when: `VersionInfo.CurrentMatchesGitHead` verifies `VersionInfo::current().git_commit_sha` is non-empty and matches the 12-hex-char format.
- [ ] Embed the exact bytes of root `LICENSE` into `build/generated/recto_version.h` as `recto::core::kEmbeddedLicenseText` so the in-app License modal works even if the user deletes external text files, and is guaranteed byte-for-byte identical to root `LICENSE`. Done when: `VersionInfo.EmbeddedLicenseMatchesRootFile` compares `kEmbeddedLicenseText` against `read_file_bytes(repo_root() / "LICENSE")` and asserts `0` byte differences.
- [ ] Implement `VersionInfo::to_clipboard_markdown()` and `VersionInfo::to_json()` including `app_version`, `architecture`, `install_mode` (`Standard` vs `Portable`), `windows_app_sdk_version`, `engine_version`, `git_commit_sha`, `os_version_build`, and `license_spdx`. Done when: `VersionInfo.ClipboardAndJsonSerialization` verifies both formats contain all 8 fields for both `Standard` and `Portable` modes.
- [ ] Ensure `scripts/generate-version-header.py` uses write-if-changed semantics so running `.\build.cmd` twice does not touch `build/generated/recto_version.h`'s modification timestamp if `VERSION`, `LICENSE`, and `HEAD` have not changed. Done when: `VersionInfo.GeneratorWriteIfChanged` verifies identical `st_mtime_ns` across a no-op second invocation.
- [ ] Update `recto-cli --version` to print `VersionInfo::current().to_clipboard_markdown()` so CLI and GUI version reporting come from the exact same struct. Done when: `recto-cli --version` outputs the generated version, architecture, engine version, and commit SHA.
- [ ] Commit: `build: add build-time version, git commit, and embedded LICENSE generator`

**Test checkpoint:** `recto-tests --gtest_filter=VersionInfo.*` passes and `recto-cli --version` output is quoted showing the generated version, architecture, `recto-pdf` version, and git commit SHA.

## 2. `AboutPage` (`SPEC 9.8`, `D-About.png`: version truth, `LICENSE` modal and `Built-with`)

Implement `src/app/views/AboutPage.h/.cpp` as `recto-ui` elements matching `design/SPEC.md` section 9.8 and `design/screenshots/D-About.png` token-for-token, reachable from Settings -> `About Recto` (`D02 T04 §6`), the Classic `Help -> About Recto` menu item (`D02 T01 §10`), and the `Ctrl+K` Command Palette (`help.about`):
- The Recto lockup (icon plus outlined wordmark) from `resources/svg/lockup-light.svg` and `resources/svg/lockup-dark.svg` through `SvgImageSource` at `64 epx` high as `D-About.png` shows, switched by theme resource; no Newsreader text element and no font file, because SPEC 3.3 has the app use only the outlined SVGs and ship or load no Newsreader font. Then the version and architecture caption (`Version 1.0.0, x64`), one-sentence description, and `Released under the GNU General Public License v3.0 or later (GPL-3.0-or-later)`.
- Technical details card (`RectoRaisedBrush`, `RectoBorderBrush`) showing `Windows` (the version and build, for example `11, build 26100`, from `RtlGetVersion`; SPEC 9.8 and `D-About.png`), `Engine` (`recto-pdf 1.0.0 (<commit>)`), `Install mode` (`Standard` or `Portable`), and `Source` link pointing to the exact release tag URL.
- Primary action bar: `View source` (accent button opening the release tag URL via `ExternalLinkConfirmDialog` or system browser), `License` (opening the in-app `LicenseDialog` displaying `kEmbeddedLicenseText` in a monospace `JetBrains Mono` / `Cascadia Mono` scroll viewer), and `Copy version info` (copying `VersionInfo::to_clipboard_markdown()` to the Windows clipboard with a brief `"Copied to clipboard"` confirmation + UIA polite announcement).
- Secondary links row: `Report a problem`, `Contribute`, and `Keyboard shortcuts` (invoking `D02 T04 §10`'s `KeyboardShortcutsDialog`).
- `Built with` card rendering the six component rows of SPEC 9.8 and `D-About.png` in this order (`recto-pdf`, `recto-core`, `recto-ui`, `Windows platform`, `Fluent UI System Icons`, `Newsreader`) with each component's role and license from the shared `kBuiltWithComponents` table audited in `§3`, and the card's `All notices` link opening the in-app `NOTICE.md` text.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.8 and `design/screenshots/D-About.png`.
**Job:** The user (or anyone verifying `GPL-3.0-or-later` compliance) can inspect Recto's exact build version, architecture, install mode (`Standard` vs `Portable`), full `GPL-3.0-or-later` license text in-app, source tag link, and every component Recto is built with, or copy a bug-report-ready version block in one click. Consumer: end users, bug reporters pasting `Copy version info`, and open-source compliance auditors.
**Treatment:** All fields populated dynamically from `recto::core::VersionInfo::current()` (`§1`) and `recto::core::built_with_components()` (`§3`), with the in-app `LicenseDialog` displaying `kEmbeddedLicenseText` byte-identical to root `LICENSE`. Cheaper substitute that fails the checkpoint: hard-coded mockup strings (`"0.4.0"`, `"a1b2c3d"`) or opening an external web browser instead of displaying the full `GPL-3.0-or-later` license text in-app.
**Chrome:** Consume the `Recto*Brush` semantic tokens from `recto_tokens.g.h` (`D10 T01 §8`), the `resources/svg/lockup-light.svg` and `lockup-dark.svg` brand lockup (SPEC 3.3) through `ui::SvgImage` (`D10 T01 §11`), `ui::Card` (`D10 T02 §16`) for the details cards, and `ui::Dialog` (`D10 T02 §17`) for `LicenseDialog`.

**Build order:**

1. Implement `AboutViewModel` (`src/app/viewmodels/AboutViewModel.h/.cpp`) backed by `VersionInfo::current(AppPaths::is_portable())` and `built_with_components()`, plus `copy_version_info_to_clipboard()` and `LicenseDialog` (`src/app/dialogs/LicenseDialog.h/.cpp`). Done when: `About.*` unit tests in `recto-tests` verify every property, the `Built with` component rows, `LicenseDialog` text byte-equality with root `LICENSE`, and clipboard readback.
2. Author `src/app/views/AboutPage.h/.cpp` matching `SPEC 9.8` and `D-About.png`, and wire navigation from `Settings -> About Recto` (`D02 T04 §6`), Classic `Help -> About Recto`, and `Ctrl+K` Command Palette (`help.about`). Done when: every control on `SPEC 9.8` is working, carries an automation name and tooltip, and passes `About.*` tests.

- [ ] Populate every version, commit, architecture, install-mode (`Standard` vs `Portable`), and tag URL field in `AboutViewModel` from `VersionInfo::current()` (`§1`) with zero hard-coded version strings in `AboutPage.cpp` or `AboutViewModel.cpp`. Done when: `About.ViewModelReflectsGeneratedVersionInfo` verifies that every string in `AboutViewModel` matches `VersionInfo::current()`. Cheaper substitute that fails the checkpoint: hard-coding version strings in view code that go stale on the next commit or version bump.
- [ ] Build the brand header and technical details card in `AboutPage.cpp` matching `D-About.png`: the theme-switched `resources/svg/lockup-light.svg` / `lockup-dark.svg` lockup (`ui::SvgImage`, `64 epx` high, no Newsreader `ui::TextBlock` and no bundled font file), version + architecture caption, `GPL-3.0-or-later` license statement, and the 4-row technical details box (`Windows`, `Engine`, `Install mode`, `Source`). Done when: light and dark theme renders of `AboutPage` match `D-About.png` using only `recto_tokens.g.h` tokens, and `About.LockupIsOutlinedSvg` finds the lockup `ui::SvgImage` bound to `lockup-light.svg` in Light and `lockup-dark.svg` in Dark, no `ui::TextStyle` with the Newsreader family in the page, and no `.ttf` or `.otf` Newsreader file in the build output.
- [ ] Implement the in-app `LicenseDialog` opened by the `License` button, rendering `VersionInfo::current().embedded_license_text` in a scrollable monospace view with a `Copy license` button and `Close` button. Done when: `About.InAppLicenseMatchesRootLicenseFile` verifies the text bound to `LicenseDialog` equals root `LICENSE` (`35,149` bytes, SHA-256 matched) byte-for-byte.
- [ ] Implement the `Copy version info` button writing `VersionInfo::to_clipboard_markdown()` to the system clipboard, showing a transient `"Copied"` status confirmation, and firing a polite `AnnouncementService` notification. Done when: `About.CopyVersionInfoPopulatesClipboard` invokes the command and reads back the exact multi-line version block from the clipboard.
- [ ] Wire `View source`, `Report a problem`, and `Contribute` links to the repository URLs, and wire the `Keyboard shortcuts` link to open `D02 T04 §10`'s `KeyboardShortcutsDialog`. Done when: clicking `Keyboard shortcuts` on `AboutPage` opens `KeyboardShortcutsDialog` in integration tests.
- [ ] Render the `Built with` card from `recto::core::built_with_components()` listing, in SPEC 9.8 order, `recto-pdf` (Recto's native C++23 PDF engine, `GPL-3.0-or-later`), `recto-core` (High-Accuracy Neural OCR, OpenXML and OLE2 layout engines, `GPL-3.0-or-later`), `recto-ui` (Recto's own UI toolkit, `GPL-3.0-or-later`), `Windows platform` (Direct2D, DirectWrite, WIC, CNG, `Windows.Media.Ocr`; `Part of Windows`), `Fluent UI System Icons` (interface glyphs, `MIT`, shipped as the `RectoIcons.ttf` subset), and `Newsreader` (wordmark, `SIL OFL 1.1`, outlined into the SVG lockup), plus the `All notices` link. Done when: `About.BuiltWithRosterMatchesNotice` verifies exactly 6 component rows in that order with their roles and license labels, and fails on a roster that drops `recto-core` or swaps `Fluent UI System Icons` and `Newsreader`.
- [ ] Commit: `feat(about): implement SPEC 9.8 AboutPage, in-app LICENSE dialog, and Built-with card`

**Test checkpoint:** `recto-tests --gtest_filter=About.*` passes, the `Copy version info` clipboard output is quoted, the in-app license byte-comparison against root `LICENSE` passes, and light/dark screenshots of `AboutPage` are compared against `design/screenshots/D-About.png`.

## 3. Build-derived dependency enumeration script, `NOTICE.md` and `docs/license-audit.md`

Complete the documentary and automated compliance half of foundation plan section 10: write `scripts/audit-dependencies.py` (with unit tests in `tests/scripts/test_dependency_audit.py`) that inspects the actual repository build inputs (`vcpkg.json`, `src/app/packages.config`, every `.vcxproj` `<AdditionalDependencies>` and `<ProjectReference>`, `resources/`, and `dumpbin /dependents` on staged `Recto.exe` and `recto-cli.exe`), enumerates every third-party and system dependency, and verifies 100% 3-way parity between (1) the build-derived dependency set, (2) `NOTICE.md` and `docs/license-audit.md`, and (3) `recto::core::built_with_components()` rendered by `AboutPage` (`§2`).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `scripts/audit-dependencies.py` scanning `vcpkg.json`, `src/app/packages.config`, `src/**/*.vcxproj`, `resources/README.md`, and (when built binaries exist) `dumpbin /dependents`, and verifying that every discovered library/asset appears in `NOTICE.md` and `docs/license-audit.md` and that every shipped component appears in `recto::core::built_with_components()`. Done when: `python scripts/audit-dependencies.py --check` exits `0` and fails if an unlisted package is added to `packages.config` or `vcpkg.json`.
2. Complete `NOTICE.md` and author `docs/license-audit.md` recording for each dependency: exact version, upstream URL, SPDX license identifier, whether it ships in `RectoSetup-*.exe` / `Recto-Portable-*.zip` or is test-only (`gtest`), and the `GPL-3.0-or-later` compatibility analysis (including the GPL v3 section 1 System Libraries exception for Windows OS DLLs). Done when: `NOTICE.md` and `docs/license-audit.md` are committed and `python scripts/audit-dependencies.py --check` exits `0`.

- [ ] Implement `scripts/audit-dependencies.py` parsing `vcpkg.json`, `src/app/packages.config`, `.vcxproj` `<AdditionalDependencies>`, and `src/core/version_info.cpp` (`built_with_components()`), failing with exit code `2` if any dependency in the build files is missing from `NOTICE.md` or `docs/license-audit.md`. Done when: `python scripts/audit-dependencies.py --check` exits `0` on the repo and exits `2` in a unit test that injects an undocumented NuGet or vcpkg dependency. Cheaper substitute: manually editing `NOTICE.md` once from memory with no script to detect when a new library or NuGet package is added later.
- [ ] Inspect staged `Recto.exe` and `recto-cli.exe` via `dumpbin /dependents` in `scripts/audit-dependencies.py` (when `--binaries-dir` is provided) and verify every imported DLL is either a documented Windows 11 OS system DLL (`d2d1.dll`, `dwrite.dll`, `d3d11.dll`, `dxgi.dll`, `dcomp.dll`, `dwmapi.dll`, `uiautomationcore.dll`, `msctf.dll`, `shcore.dll`, `windowscodecs.dll`, `bcrypt.dll`, `crypt32.dll`, `kernel32.dll`, `user32.dll`, `ole32.dll`, `oleaut32.dll`, `shell32.dll`, `shlwapi.dll`, `comdlg32.dll`, `winspool.drv`, `dbghelp.dll`, MSVC CRT) and no framework runtime DLL (`Microsoft.*.dll`) is imported (Recto is a plain Win32 app with its own `recto-ui` toolkit). Done when: zero unclassified DLL imports exist on `Recto.exe` or `recto-cli.exe`.
- [ ] Complete `NOTICE.md` covering every component: `Recto`, `recto-pdf`, `recto-core` and `recto-ui` (`GPL-3.0-or-later`), `Fluent UI System Icons` (`@fluentui/svg-icons` `1.1.344`, `MIT`, shipped as the `resources/fonts/RectoIcons.ttf` subset), `Windows 11 SDK System Libraries` (Direct2D, DirectWrite, DirectComposition, UI Automation, WIC, CNG, CryptoAPI, `Windows.Media.Ocr`, `DbgHelp`), `Newsreader` typeface (`SIL Open Font License 1.1`, converted to static vector paths at build time in `design/tools/build_resources.py`), and `GoogleTest` (`BSD-3-Clause`, explicitly marked `test-only: linked only into recto-tests.exe, never shipped in release packages`). Done when: `NOTICE.md` contains full attribution text and license summaries for every item.
- [ ] Author `docs/license-audit.md` recording the formal `GPL-3.0-or-later` compatibility table for every dependency (citing GPL-3.0 section 1 "System Libraries" exception for Windows OS components, FSF compatibility of `MIT`, `BSD-3-Clause`, and `OFL-1.1` outlines, and confirming zero `GPL-2.0-only`, proprietary non-system, or incompatible copyleft dependencies exist). Done when: `docs/license-audit.md` answers foundation plan section 10's license-compatibility item with zero open rows.
- [ ] Verify 1-to-1 parity between `NOTICE.md`'s shipped components and `recto::core::built_with_components()` (`§2`) in both `scripts/audit-dependencies.py --check` and `About.BuiltWithRosterMatchesNotice` in `recto-tests`. Done when: both the Python check and the C++ test pass.
- [ ] Add `tests/scripts/test_dependency_audit.py` testing `scripts/audit-dependencies.py` against the live repo and synthetic drift cases. Done when: `python -m unittest tests.scripts.test_dependency_audit` passes.
- [ ] Commit: `docs(compliance): complete NOTICE.md, docs/license-audit.md, and build-derived dependency audit script`

**Test checkpoint:** `python scripts/audit-dependencies.py --check` exits `0`, `python -m unittest tests.scripts.test_dependency_audit` passes, and `recto-tests --gtest_filter=About.BuiltWithRosterMatchesNotice` passes.

## 4. Opt-in local crash report bundle writer (`MiniDumpNormal` + D16 ring log `.zip` prompt)

Implement the opt-in local crash reporting system per `SPEC 9.7`, decision D16, and the 2026-10-14 owner decision in `src/core/crash_reporter.h/.cpp` and `src/app/CrashReportPrompt.h/.cpp`:
- Off by default (`SettingsService::save_crash_reports() == false` from `D02 T04 §6`). When disabled, unhandled exceptions write nothing to disk.
- When the user explicitly enables `Save local crash report bundles` in Settings, `CrashReporter` registers a Win32 unhandled-exception handler (`SetUnhandledExceptionFilter`) that writes a self-contained `.zip` archive (`crash-reports/recto-crash-<timestamp>.zip`, specifically `crash-reports/recto-crash-<UTC-YYYYMMDD-HHMMSS>-<pid>.zip` inside `AppPaths::data_dir()`, which resolves to `%LOCALAPPDATA%\Recto\crash-reports\` in Standard mode or `<exe-dir>\data\crash-reports\` in Portable mode).
- The `.zip` bundle contains **strictly three allowlisted entries**:
  1. `version.json` (`VersionInfo::current().to_json()` from `§1`: app version, architecture, engine version, git commit SHA, install mode, OS build).
  2. `ring-log.txt` (the in-memory `D16` structured diagnostic ring buffer from `D01 T01 §1`, which logs error codes, byte offsets, and timings, never document text or file contents).
  3. `minidump.dmp` written via `DbgHelp.dll` `MiniDumpWriteDump` with `MiniDumpNormal` (`MiniDumpWithThreadInfo` only, explicitly **excluding** `MiniDumpWithFullMemory` or `MiniDumpWithDataSegs` so document heap buffers and rendered page bitmaps are never written).
- Zero network calls (`winhttp.dll`, `wininet.dll`, `ws2_32.dll`) are ever linked or invoked. On the next app launch, if a new `.zip` bundle exists in `crash-reports/` since the last acknowledged timestamp, Recto shows a non-intrusive notification banner/dialog offering `Open crash-reports folder` and `Dismiss` so the user can inspect the `.zip` and optionally attach it to a GitHub issue.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.7 (`Save local crash report bundles` toggle, off by default, in `D-Settings.png`) and section 9.9 (dialog family for the next-launch prompt).
**Job:** A user diagnosing a crash can opt in to save a local, privacy-preserving `.zip` bundle (`version.json`, `ring-log.txt`, and thread-stack `minidump.dmp` with zero document bytes) inside their Recto data folder (including `<exe-dir>\data\crash-reports\` in Portable mode), inspect every file inside the `.zip` themselves, and share it if they choose, with zero background network traffic. Consumer: the user and developers triaging user-attached crash bundles.
**Treatment:** Opt-in gate checked before writing, `MiniDumpNormal` (`DbgHelp.dll`) excluding heap memory, `zip_archive.cpp` (`D04 T03 §1`) assembling the 3-entry `.zip` under `AppPaths::data_dir() / "crash-reports"`, and a next-launch prompt with `Open crash-reports folder`. Cheaper substitute that fails the checkpoint: default-on crash writing, `MiniDumpWithFullMemory` dumps that leak open PDF contents to disk, or background HTTP telemetry uploads.
**Chrome:** Reuse the existing `Save local crash report bundles` toggle in `SettingsPage` (`D02 T04 §6`) and the standard `SPEC 9.9` dialog/banner tokens for the next-launch `"Crash report saved locally"` prompt.

**Build order:**

1. Implement `recto::core::CrashReporter` in `src/core/crash_reporter.h` and `src/core/crash_reporter.cpp` (`initialize`, `set_enabled`, `write_crash_bundle_for_exception(EXCEPTION_POINTERS*)`, `check_unacknowledged_bundles`, `acknowledge_bundles`) writing `version.json`, `ring-log.txt`, and `minidump.dmp` (`MiniDumpNormal`) into `AppPaths::data_dir() / "crash-reports" / "recto-crash-<timestamp>-<pid>.zip"`. Done when: `CrashReport.*` tests in `recto-tests` verify opt-out (writes `0` files), opt-in in Standard mode, and opt-in in Portable mode (`<portable-dir>\data\crash-reports\`).
2. Wire `CrashReporter` at the `ui::Application` startup in `src/app/` to `SettingsService::save_crash_reports()` (`D02 T04 §6`) and implement the next-launch prompt (`CrashReportPrompt` in `src/app/`) offering `Open crash-reports folder` (opening `Explorer.exe` to the `crash-reports` directory) and `Dismiss` (updating `last_acknowledged_crash_utc` in settings). Done when: launching after a simulated crash with opt-in displays the prompt once and dismissing it prevents repeat prompts on subsequent launches.

- [ ] Enforce the off-by-default privacy gate: verify `SettingsService::save_crash_reports()` defaults to `false`, and when `false`, `CrashReporter::write_crash_bundle_for_test()` writes `0` files and does not create a `crash-reports/` directory. Done when: `CrashReport.OptOutWritesNothing` passes in `recto-tests`. Cheaper substitute that fails the checkpoint: writing minidumps unconditionally before checking the user's opt-in setting.
- [ ] Write `minidump.dmp` using `MiniDumpWriteDump` with `MiniDumpNormal` (`0x00000000` / thread stacks and module list only, explicitly asserting `(dump_type & MiniDumpWithFullMemory) == 0` and `(dump_type & MiniDumpWithIndirectlyReferencedMemory) == 0`) so heap-allocated `pdf::Document` buffers and `Bitmap` pixels never enter the dump. Done when: `CrashReport.HeapSecretCanaryExcludedFromBundle` allocates a `64 KB` heap buffer filled with `"SECRET_PDF_DOCUMENT_CANARY_BYTES"`, triggers a synthetic `EXCEPTION_POINTERS` bundle capture, decompresses all entries in `recto-crash-*.zip`, and asserts `0` occurrences of the canary string anywhere in the `.zip` or `minidump.dmp`.
- [ ] Package the crash bundle as a standard `.zip` archive (`recto-crash-<YYYYMMDD-HHMMSS>-<pid>.zip`) containing **strictly** three entries (`version.json`, `ring-log.txt`, `minidump.dmp`) using `zip_archive` (`D04 T03 §1`) and removing the intermediate unzipped files via `AtomicFileWriter` patterns. Done when: `CrashReport.BundleContainsOnlyThreeAllowlistedFiles` opens the generated `.zip` and asserts the entry list equals `{"version.json", "ring-log.txt", "minidump.dmp"}` with valid `MINIDUMP_SIGNATURE` (`"MDMP"`), valid `version.json`, and recent `D16` ring log lines.
- [ ] Resolve the output directory through `AppPaths::data_dir() / "crash-reports"` (`D02 T04 §1`) so in Portable mode (`<exe-dir>\data\portable.mode`) crash bundles are saved inside `<exe-dir>\data\crash-reports\` and never touch `%LOCALAPPDATA%\Recto`. Done when: `CrashReport.PortableModeSavesInsideDataDir` verifies the `.zip` is created inside `<temp-portable>\data\crash-reports\` and `%LOCALAPPDATA%` remains untouched.
- [ ] Verify via `dumpbin /dependents` and source scan (`SecurityAudit` / `CrashReport.ZeroNetworkImports`) that `recto-core.lib`, `recto-services.lib`, and `Recto.exe` import zero functions from `winhttp.dll`, `wininet.dll`, or `ws2_32.dll`. Done when: `CrashReport.ZeroNetworkImports` passes.
- [ ] Implement the next-launch `CrashReportPrompt` detection (`CrashReporter::find_unacknowledged_bundles(last_seen_utc)`): when an unacknowledged `recto-crash-*.zip` is present at startup, show a non-blocking banner/dialog naming the `.zip` filename with `Open folder` and `Dismiss` actions, and record acknowledgment so it does not re-prompt on every subsequent launch. Done when: `CrashReport.NextLaunchDetectionAndAcknowledge` verifies detection before acknowledgment and silence after acknowledgment.
- [ ] Commit: `feat(privacy): add opt-in local crash report .zip bundle writer and next-launch folder prompt (D16)`

**Test checkpoint:** `recto-tests --gtest_filter=CrashReport.*` passes under ASan with the 3-entry `.zip` inventory (`version.json`, `ring-log.txt`, `minidump.dmp`), `MiniDumpNormal` heap-canary exclusion check, Portable mode path check, and zero-network check quoted.

## 5. Localization readiness (`en-US.rc` string extraction and 40% pseudolocale check)

Prepare Recto for future translations without a UI refactor: move every user-facing string in `src/app/**/*.cpp` and `src/app/**/*.h` into `src/app/strings/en-US.rc` as `STRINGTABLE` entries loaded through `ui::Strings::get` (`D10 T03 §7`, ids in the generated `string_ids.g.h`), author `scripts/check-unresourced-strings.py` (with unit tests in `tests/scripts/test_unresourced_strings.py`) enforcing that no unresourced user-facing string literals appear in `src/app`, and verify the `qps-ploc` pseudo-locale that `scripts/generate-pseudolocale.py` (`D10 T03 §7`) emits (about 40 percent expansion, accented characters) across the primary app surfaces (`Home`, `DocumentView`, `Convert`, `Organize`, `Settings`, `About`).

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 through 9; layouts must survive 40% string length expansion (`qps-ploc`) without text truncation clip, overlapping controls, or off-screen buttons.
**Job:** Future translators can localize Recto by translating `en-US.rc` into a new `.rc` file alone without touching C++, and longer languages (such as German, French, or Polish) do not break toolbar, card, or settings layouts. Consumer: translators and non-English Windows users.
**Treatment:** Complete `src/app/strings/en-US.rc` string table, automated static scanner `scripts/check-unresourced-strings.py`, and the generated `qps-ploc.rc` pseudolocale verified by headless layout tests and golden captures. Cheaper substitute that fails the checkpoint: leaving English string literals hard-coded in C++ calls such as `set_text("...")`, `set_name("...")` and `set_tooltip("...")` and in view models.
**Chrome:** Existing `SPEC.md` layouts using `ui::Grid` star and auto tracks (`D10 T01 §7`) and `ui::TextBlock` trimming with an ellipsis plus tooltips where appropriate so 40%-expanded strings wrap or elide gracefully without overlapping sibling controls.

**Build order:**

1. Extract all user-facing strings from `src/app/**/*.cpp` and `src/app/**/*.h` (the text, header, placeholder, title, tooltip and automation-name arguments of `ui::` controls) into `src/app/strings/en-US.rc` with `IDS_<AREA>_<NAME>` ids per `D10 T03 §7` (for example `IDS_SHELL_TAB_HOME`, `IDS_SETTINGS_PORTABLEMODE_TITLE`), and write `scripts/check-unresourced-strings.py` + `tests/scripts/test_unresourced_strings.py`. Done when: `python scripts/check-unresourced-strings.py` exits `0` with `0` unresourced user-facing literals.
2. Run the `qps-ploc` pseudo-locale (generated by `scripts/generate-pseudolocale.py` of `D10 T03 §7`, which preserves `{0}` / `%1` format placeholders) through `Localization.*` tests in `tests/recto-tests/localization_tests.cpp` verifying every id loads cleanly and main surfaces (`Home`, `Convert`, `Settings`, `About`) lay out headless with zero overlapping bounding boxes (the `Ui.PseudolocaleLayout` harness of `D10 T03 §7`, applied here to the real screens). Done when: `recto-tests --gtest_filter=Localization.*` passes and pseudolocale golden captures are written.

- [ ] Populate `src/app/strings/en-US.rc` with every user-facing string across `src/app/` (shell tabs, menus, command palette labels, toolbar tooltips, automation names, Convert cards and Step 3 options, Organize bar and dialogs, Settings rows, About page, and error/confirmation dialogs). Done when: `rc.exe` compiles `en-US.rc` with `0` warnings and `python scripts/build-string-ids.py --check` exits `0`. Cheaper substitute that fails the checkpoint: moving only 10 strings into `en-US.rc` while leaving hundreds of hard-coded English strings across `src/app/**/*.cpp`.
- [ ] Write `scripts/check-unresourced-strings.py` scanning all `src/app/**/*.cpp` and `src/app/**/*.h` files for literal user-facing string arguments to `ui::` text, header, placeholder, title, tooltip and automation-name setters that are not `ui::Strings::get(IDS_...)` results or an explicit documented non-translatable allowlist (such as numeric zoom percentages `"100%"`, brand name `"Recto"`, or `RectoIcons` glyph names passed to `ui::Icon`). Done when: `python scripts/check-unresourced-strings.py` exits `0` on `src/app` and fails with file and line numbers when a raw literal `set_text("Hello")` is injected in `tests/scripts/test_unresourced_strings.py`.
- [ ] Consume `scripts/generate-pseudolocale.py` (built by `D10 T03 §7`) over the populated `en-US.rc`. Done when: `python scripts/generate-pseudolocale.py --check` verifies `src/app/strings/qps-ploc.rc` has the exact same id set as `en-US.rc` and intact format placeholders (`{0}`, `%s`, `%d`).
- [ ] Add `Localization.AllStringIdsHaveNonEmptyValuesAndMatchingPlaceholders` in `tests/recto-tests/localization_tests.cpp` comparing `en-US.rc` and `qps-ploc.rc` id by id. Done when: `Localization.AllStringIdsHaveNonEmptyValuesAndMatchingPlaceholders` passes.
- [ ] Add `Localization.PseudolocaleSurfaceLayoutSurvival` in `tests/recto-tests/localization_tests.cpp` loading the `qps-ploc` strings into `SettingsPage`, `ConvertPage`, `HomePage`, and `AboutPage` through `ui::Strings` at `1280 x 800` (`100%` scale) in the headless harness, running a layout pass, and asserting that no interactive control (`ui::Button`, `ui::ToggleSwitch`, `ui::ComboBox`) has a zero-width/height clip or overlaps a sibling control's bounding box, and capture `qps-ploc` golden images of `Settings` and `Convert`. Done when: `Localization.PseudolocaleSurfaceLayoutSurvival` passes with `0` layout collisions.
- [ ] Commit: `feat(i18n): extract all UI strings to en-US.rc, add literal scanner, and verify qps-ploc layout`

**Test checkpoint:** `python scripts/check-unresourced-strings.py` exits `0`, `python -m unittest tests.scripts.test_unresourced_strings` passes, and `recto-tests --gtest_filter=Localization.*` passes with `qps-ploc` screenshots of `Settings` and `Convert` recorded.

## 6. GitHub Release runbook (`docs/release-and-store-runbook.md`) and Store submission kit

Author the step-by-step release engineering runbook (`docs/release-and-store-runbook.md`) and the optional Microsoft Store Partner Center submission kit (`docs/store-listing.md` and `docs/store-license-terms.txt`):

> **Groomed 2026-10-22:** v1 ships unsigned (owner decision 2026-10-22 (foundation plan section 1)): the runbook and the release notes template state that `Setup.exe` and the Portable `.zip` carry no code signature, explain the SmartScreen notice and how to verify `SHA256SUMS.txt`, and say MSIX is signed by Microsoft only through the optional Store submission.
- `docs/release-and-store-runbook.md`: exact commands for bumping `VERSION`, tagging `vX.Y.Z`, running `build/build-installer.ps1` and `tests/scripts/verify_installer.ps1`, generating and verifying `SHA256SUMS.txt`, publishing a GitHub Release (`gh release create v1.0.0`) with the four primary artifacts (`RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip`, `Recto-Portable-ARM64.zip`, plus `SHA256SUMS.txt`), rolling back a faulty release, and (when `D99 T01 §3` is completed by the operator) building and submitting the optional `x64`/`ARM64` MSIX package to Microsoft Partner Center.
- `docs/store-listing.md` and `docs/store-license-terms.txt`: honest feature description (incorporating the PDF-to-Word quality scope statement from `D04 T03 §6`), privacy policy statement (100% offline local processing, zero telemetry, opt-in local-only crash bundles), age-rating questionnaire answers (`IARC` all-ages productivity utility), real-app screenshots captured from the built `Recto.exe` matching the `SPEC` boards, and custom Store license terms (`docs/store-license-terms.txt`) byte-for-byte identical to root `LICENSE` (`GPL-3.0-or-later`, explicitly replacing the Standard Microsoft Store Application License Terms per decision D6).

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Author `docs/release-and-store-runbook.md`, `docs/store-listing.md`, and `docs/store-license-terms.txt` (generated/copied directly from root `LICENSE`), and capture real-app release screenshots from the built `Recto.exe` (`Home`, `DocumentView` in Focus and Classic presets, `Convert`, `Organize`, `Settings`, `About`) under `docs/release-screenshots/`. Done when: all runbook, listing, license, and screenshot files exist and are committed.
2. Write `scripts/verify-release-kit.py` (with unit tests in `tests/scripts/test_release_kit.py`) verifying that `docs/store-license-terms.txt` is byte-for-byte identical to root `LICENSE`, `docs/store-listing.md` contains the required sections and privacy/conversion disclosures, and all referenced screenshots in `docs/release-screenshots/` exist with non-empty PNG dimensions. Done when: `python scripts/verify-release-kit.py` exits `0`.

- [ ] Document the complete GitHub Release procedure in `docs/release-and-store-runbook.md`: pre-release verification commands (`todo-graph.py validate`, `.\build.cmd`, `build/build-installer.ps1`, `tests/scripts/verify_installer.ps1`, `scripts/audit-dependencies.py --check`), git tag creation (`git tag -a v1.0.0`), `gh release create` asset upload list (`RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip`, `Recto-Portable-ARM64.zip`, `SHA256SUMS.txt`), post-publish download/checksum verification, and emergency release rollback steps. Done when: every step in `docs/release-and-store-runbook.md` has exact copy-pasteable PowerShell commands. Cheaper substitute: informal tribal knowledge with no documented rollback or checksum verification procedure.
- [ ] Document the optional Microsoft Store Partner Center MSIX submission workflow in `docs/release-and-store-runbook.md` section 2 (referencing `D99 T01 §3`'s Partner Center identity values, `build/build-installer.ps1 -EnableMsix`, Windows App Certification Kit `appcert.exe` command line, Partner Center custom license upload, and Store delisting/rollback steps). Done when: section 2 covers the entire optional Store lifecycle end-to-end.
- [ ] Author `docs/store-listing.md` containing the GitHub Release notes template and Microsoft Store listing copy: short description, full feature list across M1-M5, explicit PDF-to-Word scope note (`D04 T03 §6`), explicit privacy declaration (zero network telemetry, zero cloud uploads, opt-in local-only crash `.zip` bundles), and IARC age-rating answers. Done when: `docs/store-listing.md` is complete and accurate against the shipped feature set.
- [ ] Create `docs/store-license-terms.txt` as an exact byte-for-byte copy of root `LICENSE` (`GPL-3.0-or-later`) for the Partner Center "License terms" field (where decision D6 requires supplying Recto's own `GPL-3.0-or-later` terms rather than the Standard Microsoft Store EULA). Done when: `scripts/verify-release-kit.py` asserts `sha256(docs/store-license-terms.txt) == sha256(LICENSE)`.
- [ ] Capture real-app release screenshots from the built `Recto.exe` (`1440 x 900`) into `docs/release-screenshots/` covering `Home`, `DocumentView` (`Focus` and `Classic`), `Convert`, `Organize`, `Settings`, and `About`. Done when: `scripts/verify-release-kit.py` verifies all 7 PNG screenshots exist and have dimensions `1440 x 900`.
- [ ] Add `tests/scripts/test_release_kit.py` testing `scripts/verify-release-kit.py` (including failure on a 1-byte drift between `docs/store-license-terms.txt` and `LICENSE`). Done when: `python -m unittest tests.scripts.test_release_kit` passes.
- [ ] Add the Group Policy templates archive and the driver signing runbook to the release procedure: list `Recto-PolicyTemplates-<version>.zip` in the pre-release verification and `gh release create` asset list, and link `docs/driver-signing-runbook.md` from the Store and release steps. Done when: `docs/release-and-store-runbook.md` names both and `scripts/verify-release-kit.py` fails when the runbook text names neither (a reference check only; the check that the archive and the signing runbook exist runs in `scripts/verify-release-kit.py --artifacts` from §7, after they are produced).
- [ ] Add the unsigned-release notice to `docs/store-listing.md` (release notes template) and the runbook: "Recto v1 installers and archives are not code signed, so Windows SmartScreen may show an unrecognized-app notice; choose More info, then Run anyway, or verify the download against SHA256SUMS.txt first", and add the same sentence under a Downloads heading in `README.md` when it describes downloads. Done when: `scripts/verify-release-kit.py` finds the sentence in `docs/store-listing.md` and the runbook, and fails when `README.md` has a Downloads heading without it.
- [ ] Commit: `docs(release): add GitHub Release runbook, optional Store kit, and real-app release screenshots`

**Test checkpoint:** `python scripts/verify-release-kit.py` exits `0` (confirming `docs/store-license-terms.txt` equals `LICENSE` byte-for-byte and all screenshots exist) and `python -m unittest tests.scripts.test_release_kit` passes.

## 7. End-to-end v1 distribution bundle (`Setup.exe`, `Portable.zip`, `SHA256SUMS.txt`) & GPL

Execute the complete v1 release candidate build, reference-machine performance verification (`tests/perf/run_perf.ps1`), and compliance pipeline from a clean state, produce the final `build/dist/` distribution bundle (`RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip`, `Recto-Portable-ARM64.zip`, `SHA256SUMS.txt`), verify every item in foundation plan section 8 ("Performance Targets") and section 10 ("Licensing and Compliance Checklist"), and update `docs/plans/2026-10-13-recto-foundation.md` section 10 with the verified checkboxes and evidence references.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Run the full v1 release gate (`.\build.cmd`, `pwsh -NoProfile -File tests/perf/run_perf.ps1 -Output docs/perf/v1-release-candidate.md`, `pwsh -NoProfile -File build/build-installer.ps1`, `pwsh -NoProfile -File tests/scripts/verify_installer.ps1`, `python scripts/audit-dependencies.py --check`, `python scripts/check-package-assets.py`, `python scripts/check-unresourced-strings.py`, `python scripts/verify-release-kit.py`) and verify `build/dist/SHA256SUMS.txt` against all four release artifacts and `PASS` on all five foundation plan section 8 performance targets. Done when: all commands exit `0`, all five performance targets pass on the release candidate commit, and `build/dist/SHA256SUMS.txt` hashes verify cleanly.
2. Update `docs/plans/2026-10-13-recto-foundation.md` section 10 ("Licensing and Compliance Checklist") ticking each verified checkbox and citing its owning proof file (`LICENSE`, `AboutPage`, `NOTICE.md`, `docs/license-audit.md`, `docs/store-license-terms.txt`, `CLA.md`), and run `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check`. Done when: every section 10 checkbox in `docs/plans/2026-10-13-recto-foundation.md` is ticked `[x]` with its evidence citation and `todo-graph.py validate` exits `0`.

- [ ] Build the complete v1 release bundle via `pwsh -NoProfile -File build/build-installer.ps1` and verify that `build/dist/RectoSetup-x64.exe`, `build/dist/RectoSetup-ARM64.exe`, `build/dist/Recto-Portable-x64.exe` / `Recto-Portable-x64.zip`, `build/dist/Recto-Portable-ARM64.zip`, and `build/dist/SHA256SUMS.txt` exist and are non-empty. Done when: `Get-FileHash -Algorithm SHA256` on all four artifacts matches `build/dist/SHA256SUMS.txt` line-for-line. Cheaper substitute: signing off on section 10 without building and hashing all four (`x64`/`ARM64` Setup and Portable) release artifacts together.
- [ ] Run a fresh candidate-bound performance benchmark pass via `pwsh -NoProfile -File tests/perf/run_perf.ps1 -Output docs/perf/v1-release-candidate.md` against the `Release | x64` release candidate on the `D00 T01 §5` reference machine (default `512 MB` tile cache cap), requiring every one of foundation plan section 8's five targets (`Cold launch < 400 ms`, `100p open < 300 ms`, `Scroll <= 16.6 ms p95 with 0 blank frames`, `Zoom < 16 ms preview / < 150 ms sharp tiles`, `500p working set < 600 MB`) to record `PASS` (a deferred remediation owner is refused at the terminal release gate). Done when: `docs/perf/v1-release-candidate.md` is committed with the release candidate git SHA and all 5 targets `PASS`.
- [ ] Run `pwsh -NoProfile -File tests/scripts/verify_installer.ps1` against the newly built `build/dist/RectoSetup-x64.exe` and `build/dist/Recto-Portable-x64.zip` and verify both Full Install (install, `.pdf` association, upgrade, uninstall) and Self-Contained Portable Install (`/PORTABLE=1` and `.zip` zero-registry/zero-`%LOCALAPPDATA%` isolation) pass. Done when: `verify_installer.ps1` exits `0`.
- [ ] Verify all six checkboxes in `docs/plans/2026-10-13-recto-foundation.md` section 10:
  1. `LICENSE` at repo root is `GPL-3.0-or-later` and displayed in-app under `Settings -> About` (`D00 T01 §1`, `D07 T02 §2`).
  2. `Settings -> About` includes a prominent link to the public GitHub repository and the exact source tag corresponding to the installed build (`D07 T02 §1`, `§2`).
  3. `NOTICE.md` lists every third-party library and font with its license text (`D07 T02 §3`).
  4. Every dependency's license is verified compatible with `GPL-3.0-or-later` before adding (`D07 T02 §3`, `docs/license-audit.md`).
  5. Optional Microsoft Store submission kit supplies `GPL-3.0-or-later` as the app's own license terms rather than the Standard Store Application License Terms (`D07 T02 §6`, `docs/store-license-terms.txt`).
  6. Contributor License Agreement (`CLA.md`) is in place before accepting external pull requests (`D00 T01 §1`).
  Done when: all 6 checkboxes in `docs/plans/2026-10-13-recto-foundation.md` section 10 are marked `[x]` with their evidence paths cited.
- [ ] Run `python scripts/audit-dependencies.py --check --binaries-dir build/stage/x64` and `--binaries-dir build/stage/arm64` to verify the final staged release binaries contain zero undocumented DLL imports. Done when: both architecture checks exit `0`.
- [ ] Run `python scripts/todo-graph.py validate`, `python scripts/todo-graph.py plan --check`, and `python -m unittest discover -s tests/scripts` to verify the entire repository tooling and TODO graph are green. Done when: all three commands exit `0`.
- [ ] Verify the release bundle also contains `Recto-PolicyTemplates-<version>.zip` in `build/dist/SHA256SUMS.txt` and, when the driver signature gate of `D07 T01 §20` passes, that the Full installer carries the `Recto PDF` printer task (otherwise that it carries none). Done when: `Get-FileHash` on the archive matches `SHA256SUMS.txt`, `python scripts/verify-release-kit.py --artifacts` exits `0` (archive and `docs/driver-signing-runbook.md` exist), and `tests/scripts/verify_installer.ps1 -Mode Printer` agrees with the gate result.
- [ ] Commit: `release: verify v1 distribution bundle and sign off foundation plan section 10 compliance checklist`

**Test checkpoint:** the contents of `build/dist/SHA256SUMS.txt`, the 5-row `PASS` summary from `docs/perf/v1-release-candidate.md`, the output of `tests/scripts/verify_installer.ps1`, the output of `python scripts/audit-dependencies.py --check --binaries-dir build/stage/x64`, and the ticked section 10 checklist in `docs/plans/2026-10-13-recto-foundation.md` are quoted verbatim.

## 8. Group Policy catalogue and templates: `policy/Recto.admx` and `Recto.adml`

Foxit PDF Editor ships Group Policy templates so administrators can lock settings across a fleet; Acrobat Pro uses registry feature lockdown, and owner decision D21 gives Recto ADMX templates under `HKLM\Software\Policies\Recto` and `HKCU\Software\Policies\Recto`. This section defines the six policies, writes the templates and validates them. It adds no network use, no telemetry and no remote management: a policy is a registry value that Windows Group Policy, a management tool or an administrator writes, and Recto only reads it (§9).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `docs/group-policy.md` with the catalogue below and author `policy/Recto.admx` and `policy/en-US/Recto.adml` to match it. Done when: `python scripts/check-admx.py` exits `0`.
2. Add `tests/scripts/test_check_admx.py` proving the checker catches drift. Done when: `python -m unittest tests.scripts.test_check_admx` passes.

- [ ] Write `docs/group-policy.md` with one row per policy: `AssistantDisabled` (DWORD, `1` turns the AI assistant off and locks its settings), `CloudConnectorsDisabled` (DWORD, `1` turns the cloud storage connectors off), `UpdateNoticeDisabled` (DWORD, `1` turns the opt-in update notice off), `DefaultLayoutPreset` (string, `Focus`, `Classic` or `ReadingRoom`) and `DefaultSaveFolder` (expandable string, an absolute folder), each with scope (machine and user), effect, the setting it locks, and the behaviour for a malformed value. Done when: every policy name used in `src/` appears in the table and the table has no policy missing from the templates.
- [ ] Author `policy/Recto.admx` with the category `Recto`, the six policies under `Software\Policies\Recto` for the machine and user classes, enumerated items for the layout preset and an expandable text element for the folder. Done when: `python scripts/check-admx.py` finds each policy's key, value name and class.
- [ ] Author `policy/en-US/Recto.adml` with a display name, explanatory text and presentation for every policy, written plainly and stating what each policy locks. Done when: `check-admx.py` finds every `stringTable` reference resolved and no unused string.
- [ ] Write `scripts/check-admx.py`, which parses both files as XML, checks namespaces and ids, that every policy in `docs/group-policy.md` exists with the same value name and type, and that no template names a value the table lacks. Done when: it exits `0` on the repository and exits `2` for a synthetic template with a missing string and one with an undocumented policy.
- [ ] Add `tests/scripts/test_check_admx.py` covering the two synthetic failures and the live files. Done when: `python -m unittest tests.scripts.test_check_admx` passes.
- [ ] Commit: `feat(policy): add Group Policy catalogue and ADMX and ADML templates (D21)`

**Test checkpoint:** `python scripts/check-admx.py` exits `0` and `python -m unittest tests.scripts.test_check_admx` passes.

## 9. `PolicyProvider`: read machine and user policy values, typed and bounded

Recto needs one place that answers "is this policy set, and to what" (owner decision D21). This section implements `PolicyProvider` in `src/services/policy.h/.cpp`: it reads the six values from `HKLM\Software\Policies\Recto` and `HKCU\Software\Policies\Recto`, validates them, and notifies when they change. It reads only; it writes no registry value, so it adds no footprint in Portable mode, and it opens no network connection.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `PolicyProvider` over injected root keys (`HKEY` values passed to the constructor, the real hives in production). Done when: `Policy.ReadsAllSixValues` passes against a scratch key.
2. Add the change watcher, armed before every read. Done when: `Policy.LiveChange` observes a value change within `2 s` and `Policy.WatcherNoLostUpdate` passes.

- [ ] Implement `PolicyProvider::read_all()` returning a `PolicySnapshot` of `PolicyValue` entries (`id`, `state` of `NotSet`, `Set` or `Invalid`, typed value, `source` of `Machine` or `User`), reading with `RegGetValueW` and bounded buffers (strings at most `4096` characters). Done when: `Policy.ReadsAllSixValues` returns the expected typed values for a scratch key under `HKCU\Software\RectoTest`.
- [ ] Apply precedence: a machine value wins over a user value, and both win over any user setting. Done when: `Policy.MachineBeatsUser` sets different values in both hives and gets the machine value.
- [ ] Apply fail-closed rules for the four disable policies (any non-zero DWORD, or a value of the wrong type, means disabled) and ignore-with-`Invalid` rules for the two value policies (unknown preset, relative or empty folder). Done when: `Policy.MalformedValues` covers a string in a DWORD slot, `2` in a DWORD slot, a preset `Sepia` and a folder `..\x`.
- [ ] Expand environment variables in `DefaultSaveFolder` with `ExpandEnvironmentStringsW` and reject the result unless it is an absolute path. Done when: `Policy.FolderExpansion` expands `%USERPROFILE%\Documents` and rejects `Documents`.
- [ ] Implement the watcher with `RegNotifyChangeKeyValue` on a dedicated thread owned by `Runtime` (`D01 T02 §10`) and joined before shutdown, raising a change event on the settings thread, never the UI thread. The policy keys usually do not exist, so each hive is watched through its nearest existing ancestor (with the subtree flag and `REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET`). Each cycle runs in this order so no change can fall between a read and a registration: open the nearest existing ancestor with `KEY_NOTIFY`, arm `RegNotifyChangeKeyValue` asynchronously on it, re-resolve the nearest ancestor and restart the cycle if a deeper key now exists or the arm call failed because the key was deleted, and only then read the snapshot and publish it if it differs from the last one; a signalled event (including the signal Windows raises when the watched key is deleted) starts the next cycle, so a change made while the watcher reads is either in that read or signals the already armed event. The cycle exposes test hooks `after_open`, `after_arm` and `after_read` (compiled only under `RECTO_TEST_HOOKS`). Done when: `Policy.LiveChange` sees a change within `2 s`, `Policy.WatcherKeyLifecycle` runs absent, created, value changed, key deleted and key recreated and sees each step within `2 s`, `Policy.WatcherNoLostUpdate` writes `AssistantDisabled` from each of the three hooks (and creates the policy key from `after_open`, and deletes it from `after_arm`) and finds the published snapshot matches the registry within `2 s` every time, failing for a watcher that reads before it arms, and `Policy.WatcherJoinsOnShutdown` finds no live thread after `Runtime` shuts down.
- [ ] Add `PolicyProvider` to the static banned-API scan so it uses no network and writes no registry value. Done when: `SecurityAudit.PolicyProviderReadOnly` finds no `RegSetValue*`, `RegCreateKey*` or socket import in `src/services/policy.cpp`.
- [ ] Commit: `feat(policy): add PolicyProvider reading machine and user policy values`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Policy.ReadsAllSixValues:Policy.MachineBeatsUser:Policy.MalformedValues:Policy.FolderExpansion:Policy.LiveChange:Policy.WatcherKeyLifecycle:Policy.WatcherNoLostUpdate:Policy.WatcherJoinsOnShutdown:SecurityAudit.PolicyProviderReadOnly` passes under ASan.

## 10. `SettingsStore` policy precedence: locked settings and policy gates

A policy only helps if every consumer sees it the same way. This section makes `SettingsStore` (`D02 T04 §2`) report whether a setting is locked by policy and return the policy value at read time, and gives features without a stored setting (the assistant, the connectors, the update notice) one gate to ask. A policy never writes `settings.json`, so removing a policy restores the user's own choice.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `SettingSource` and `SettingsStore::is_locked(key)` with policy-first reads. Done when: `Policy.SettingsPrecedence` passes.
2. Add `PolicyGate::is_disabled(PolicyId)` and the lock registration API for other sections. Done when: `Policy.GateContract` passes with a stub feature.

- [ ] Add `SettingSource` (`Default`, `User`, `Policy`) and `SettingsStore::source(key)`, `is_locked(key)` and `effective_value(key)` in `src/services/settings.h/.cpp`; reads return the policy value while a policy is set, and `set` on a locked key returns `SettingError::LockedByPolicy` without touching disk. Done when: `Policy.SettingsPrecedence` shows a user value of `Classic` with policy `Focus` reads `Focus`, `set` fails, and removing the policy returns `Classic`.
- [ ] Register the two built-in locks: `DefaultLayoutPreset` locks `LayoutPreset`, and `DefaultSaveFolder` sets `SaveConvertedLocation` to `CustomFolder` and locks it and `CustomSaveConvertedFolder`. Done when: `Policy.BuiltInLocks` checks both mappings and that an invalid policy value leaves the setting unlocked.
- [ ] Expose `register_policy_lock(setting_key, PolicyId, mapper)` and `PolicyGate::is_disabled(PolicyId)` so the assistant (`D08 T01`), the cloud connectors (`D02 T06`) and the update notice (`D07 T03 §7`) register their own locks or query the gate without reading the registry. Done when: `Policy.GateContract` registers a stub setting and a stub feature and sees both lock when the policy is set.
- [ ] Raise the existing change notification when a policy appears, changes or goes away while the app runs. Done when: `Policy.LiveChange` shows a subscribed view model receives one notification per change.
- [ ] Keep every persisted file unchanged by policy: assert `settings.json` bytes are identical before and after a policy is applied and removed. Done when: `Policy.SettingsFileUntouched` passes. Cheaper substitute: writing the policy value into the user's settings file, which survives after the policy is removed.
- [ ] Commit: `feat(policy): apply policy precedence and locks in SettingsStore`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Policy.SettingsPrecedence:Policy.BuiltInLocks:Policy.GateContract:Policy.LiveChange:Policy.SettingsFileUntouched` passes under ASan.

## 11. "Managed by your organization" notes on locked settings and `recto-cli policy show`

A locked control that gives no reason looks broken. This section shows a lock note on every setting a policy controls, hides or disables the commands of a disabled feature with the reason, and gives administrators `recto-cli policy show` to see the effective policy. It follows the Settings page of `design/SPEC.md` section 9.7 and the Foxit PDF Editor behaviour of showing managed options as unavailable.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.7 (Settings rows and helper text); the lock note is a genuinely new element that follows the helper text style. Design board: `design/SPEC.md` section 9.7.1 with `design/screenshots/D-Managed.png` (drawn by `D06 T04 §23`).
**Job:** A user opens Settings and sees which options their organisation controls and why they cannot change them; an administrator runs `recto-cli policy show --json` and sees every policy, its source and its state. Consumer: `SettingsStore` and `PolicyProvider`.
**Treatment:** Disabled controls with an inline `Managed by your organization` note and a `16 epx` `lock_shield` glyph (Fluent UI System Icons (`RectoIcons`), SPEC 9.7), a `Managed` marker on locked rows in `Find a setting` results, `Ctrl+K` commands disabled with the same `Managed by your organization` reason (SPEC 9.7), and a `policy show` verb. Cheaper substitute that fails the checkpoint: leaving locked controls enabled and ignoring the click, or hiding them with no explanation.
**Chrome:** `ui::SettingsCard` and the `recto-ui` controls (`D10 T02 §19`), `Recto*Brush` tokens (`D10 T01 §8`), helper-text style of section 9.7.

**Build order:**

1. Bind the lock state to the Settings rows of the layout, save-location and update notice settings and show the note. Done when: `Policy.SettingsRowsShowNote` finds the note on each locked row.
2. Add the `Ctrl+K` command reasons and `recto-cli policy show`. Done when: `Policy.CommandsDisabledWithReason` and `Policy.CliShow` pass.

- [ ] Add a `ManagedNote` element (a `ui::TextBlock`) to the `ui::SettingsCard` row template in `src/app/views/SettingsPage.cpp`, visible when `SettingsStore::is_locked(key)`, with the text and automation name `Managed by your organization` and the control disabled. Done when: `Policy.SettingsRowsShowNote` locks `LayoutPreset` through a scratch policy and finds the disabled control and the note. Cheaper substitute: a tooltip only.
- [ ] Make the "Find a setting" filter (`D02 T04 §4`) keep locked rows searchable and mark them `Managed`. Done when: `Policy.FindSettingIncludesManaged` finds a locked row by name with its `Managed` marker.
- [ ] Disable layout preset commands in `CommandRegistry` while `LayoutPreset` is locked and show the reason `Managed by your organization` in `Ctrl+K`. Done when: `Policy.CommandsDisabledWithReason` checks `Ctrl+K` shows the reason and the shortcut does nothing.
- [ ] Implement `recto-cli policy show [--json]` printing each policy, its state, value, source hive and any invalid reason, exit code `0`. Done when: `Policy.CliShow` matches the documented JSON schema in `docs/group-policy.md` for set, unset and invalid values.
- [ ] Give the lock note and glyph an automation name, tooltip and a keyboard path, and check light, dark and high contrast. Done when: `Policy.NoteAccessibility` passes and screenshots are committed under `docs/release-screenshots/`.
- [ ] Commit: `feat(policy): show managed-by-organization notes and add recto-cli policy show`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Policy.SettingsRowsShowNote:Policy.FindSettingIncludesManaged:Policy.CommandsDisabledWithReason:Policy.CliShow:Policy.NoteAccessibility` passes on the display-session host.

## 12. Policy templates release artifact and end-to-end Group Policy proof

Administrators need the templates as a download and need proof that a real registry value changes real behaviour. This section packages `Recto-PolicyTemplates-<version>.zip`, adds it to the hash list, copies `policy/` into the Full install and Portable folders, and proves the whole path with `tests/scripts/verify_policy.ps1`. The proof reads and writes only test values under the policy keys and removes them in a `finally` block.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Extend `build/build-installer.ps1` to build the templates zip. Done when: `build/dist/Recto-PolicyTemplates-<version>.zip` exists and is in `SHA256SUMS.txt`.
2. Write `tests/scripts/verify_policy.ps1`. Done when: it exits `0` and leaves no policy value behind.

- [ ] Add the packaging step to `build/build-installer.ps1`: zip `policy/Recto.admx`, `policy/en-US/Recto.adml`, every shipped language folder and a `README.txt` explaining the copy to `%SystemRoot%\PolicyDefinitions` or the central store; copy `policy/` into the Full install and Portable stage folders. Done when: inspecting the zip with Python `zipfile` finds the files and `SHA256SUMS.txt` lists the archive.
- [ ] Write `tests/scripts/verify_policy.ps1` that backs up and restores the policy keys, sets `AssistantDisabled`, `DefaultLayoutPreset` and `DefaultSaveFolder` in `HKCU\Software\Policies\Recto`, runs `recto-cli policy show --json`, and checks the values and states. Done when: the script exits `0` with a `[PASS]` line per policy.
- [ ] Launch `Recto.exe` under the script with `DefaultLayoutPreset` set and assert the shell opens in that preset and the layout setting is locked, then change the value while the app runs and assert the shell reacts within `2 s`. Done when: `verify_policy.ps1` reports both `[PASS]` lines.
- [ ] Run the same checks from a Portable folder and assert the run adds no key under `HKCU\Software\Classes\Recto.PDF` or the uninstall hive and writes nothing under `%LOCALAPPDATA%\Recto`. Done when: `verify_policy.ps1 -Portable` exits `0`.
- [ ] Add the templates archive and the policy copy to `docs/release-and-store-runbook.md` (the pre-release commands and the `gh release create` asset list) and to `scripts/verify-release-kit.py`. Done when: `python scripts/verify-release-kit.py` fails when the archive is missing from the runbook's asset list, and `python scripts/verify-release-kit.py --artifacts` fails when `build/dist/Recto-PolicyTemplates-<version>.zip` is absent.
- [ ] Commit: `release: ship Group Policy templates and prove policy end to end`

**Test checkpoint:** `pwsh -NoProfile -File tests/scripts/verify_policy.ps1` exits `0` with its `[PASS]` lines quoted and `python scripts/verify-release-kit.py` exits `0`.

## 13. Translation catalogue: languages manifest, glossary, plural rules and the checker

PDFelement, UPDF and Foxit PDF Editor ship translated interfaces, and `§5` made Recto ready for translation without choosing a language or a process. This section defines the process so anyone can add a language by editing files: a manifest of shipped languages, a glossary of terms that stay fixed, plural handling, and one checker that blocks a language that is incomplete or unreviewed. No cloud translation service is required and no string leaves the machine.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/app/strings/languages.json`, `src/app/strings/glossary.json` and `docs/translating.md`. Done when: `python scripts/check-translations.py` exits `0` with only `en-US` listed.
2. Add plural rules and the checker tests. Done when: `Localization.PluralRules` and `python -m unittest tests.scripts.test_translations` pass.

- [ ] Define the `languages.json` schema: `tag` (BCP 47), `native_name`, `direction` (`ltr` or `rtl`), `status` (`draft` or `reviewed`), `reviewer` and `reviewed_on`. Only `reviewed` languages are built into `Recto.exe`, offered in Settings and installed. Done when: `check-translations.py` rejects a `reviewed` entry with no reviewer or date.
- [ ] Define `glossary.json` (terms that stay in English or have a fixed translation, such as `Recto`, `PDF/A`, `OCR`, `Ctrl+K`) and have the checker flag a translated value that drops or alters a fixed term. Done when: a test fixture with `PDF/A` translated is reported.
- [ ] Write `scripts/check-translations.py`: for every `src/app/strings/<tag>.rc` (run after `scripts/check-strings.py` of `D10 T03 §7`, which owns id completeness and placeholder counts), the set of logical message keys (a plural group counts as one key, with its `_ONE`, `_FEW`, `_MANY` and `_OTHER` id suffixes removed) equals that of `en-US`, a plural group has exactly the categories CLDR defines for that language, no value is empty, numbered placeholders (`{0}`, `{1}`) appear with the same set of indices and types as the English value but may be reordered by the translation, and values identical to English are reported unless listed in the glossary. Done when: `tests/scripts/test_translations.py` injects each defect and the checker exits `2` for each, and accepts Russian `_FEW` and `_MANY` ids that English lacks and a reordered `{1} {0}` translation.
- [ ] Add `src/core/plural_rules.h/.cpp` selecting a plural category from a language and a number for the first language set, with rules taken from the CLDR plural rules for the pinned CLDR release recorded in `docs/translating.md`, and plural ids written as `IDS_X_ONE`, `IDS_X_FEW`, `IDS_X_MANY` and `IDS_X_OTHER` in the `.rc` files (`D10 T03 §7`). Done when: `Localization.PluralRules` passes the sample numbers per category, including Russian `1`, `2`, `5`, `21` and `0`.
- [ ] Write `docs/translating.md`: how to add a language by copying `en-US`, the review rule, the glossary, how to test with `--language <tag>`, and that no translation is accepted without a named reviewer. Done when: the steps are followed in a scratch checkout to add a `draft` language that the checker accepts and the build ignores.
- [ ] Commit: `feat(i18n): add the translation catalogue, plural rules and checker`

**Test checkpoint:** `python scripts/check-translations.py` exits `0`, `python -m unittest tests.scripts.test_translations` passes and `recto-tests.exe --gtest_filter=Localization.PluralRules` passes.

## 14. Interface language setting: Follow Windows or a shipped language

A user who works in a language other than Windows' must be able to choose Recto's language, as in PDFelement, UPDF and Foxit PDF Editor. This section adds the setting and its Settings row. The language is applied on the next start with a restart prompt, which is the reliable behaviour because `ui::Strings` (`D10 T03 §7`) fixes the language per process at startup; the cost of changing to live switching is a rebuild of every cached string in the element trees on language change. `recto-cli` keeps English, stable output for scripts.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.7 (the `Language and updates` group). Design board: `design/SPEC.md` section 9.19 with `design/screenshots/D-Lifecycle.png` (drawn by `D06 T04 §25`).
**Job:** A user picks German in Settings, restarts, and sees every menu, dialog and error in German; picking `Follow Windows` returns to the system language. Consumer: `ui::Strings` at startup.
**Treatment:** `SettingsStore` key `UiLanguage` (`FollowWindows` default or a shipped tag), a `ui::ComboBox` of native language names, the language applied before the first window through `ui::Strings::select(UiLanguage)`, and a restart prompt. Cheaper substitute that fails the checkpoint: a setting that is saved but not applied, or a list that shows languages the build does not contain.
**Chrome:** `ui::ComboBox` and `ui::InfoBar` (`D10 T02 §9`, `§20`), `Recto*Brush` tokens (`D10 T01 §8`).

**Build order:**

1. Add `UiLanguage` and apply it at startup. Done when: `Localization.LanguageOverrideAppliesOnLaunch` starts with a draft test language and reads its string.
2. Add the Settings row and the restart prompt. Done when: `Localization.SettingsRow` passes.

- [ ] Add `UiLanguage` to `src/services/settings.h/.cpp` with validation against the shipped list from `languages.json`, and apply it at the `ui::Application` startup in `src/app/` before the first `ui::Window` is created. Done when: `Localization.LanguageOverrideAppliesOnLaunch` shows the German test string after launch with `de-DE` and English after `FollowWindows` on an English system.
- [ ] Add the `Language` row to the `Language and updates` group of the Settings page (placed by `kSettingsGroupOrder`, `D02 T04 §6`) with native names, "Follow Windows" first, and a `ui::InfoBar` with `Restart now` and `Later` after a change. Done when: `Localization.SettingsRow` changes the value, sees the prompt, and `Restart now` relaunches into the language. Cheaper substitute: asking the user to restart manually with no prompt.
- [ ] Store the choice in `<exe-dir>\data\settings.json` in Portable mode like every setting. Done when: `Localization.PortableLanguageStaysInData` finds no write under `%LOCALAPPDATA%`.
- [ ] Register `settings.language` in `CommandRegistry` so `Ctrl+K` finds "Language". Done when: `Localization.CommandReachable` passes.
- [ ] Add an automation name and tooltip, then check the keyboard path. Done when: `Localization.SettingsRowAccessibility` passes.
- [ ] Commit: `feat(i18n): add the interface language setting with restart prompt`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Localization.LanguageOverrideAppliesOnLaunch:Localization.SettingsRow:Localization.PortableLanguageStaysInData:Localization.CommandReachable:Localization.SettingsRowAccessibility` passes on the display-session host.

## 15. Localised errors, plurals, dates and numbers: no English assembled in code

A translated menu is not enough if an error says "Could not open file" in English or a count reads "1 pages". This section makes every engine error message, plural and formatted value come from resources and the system locale, and extends the literal scanner of `§5` to catch assembled English.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Map every error code to a resource key. Done when: `Localization.EveryErrorCodeHasMessage` passes.
2. Replace assembled strings with plural keys and locale formatting. Done when: `Localization.PluralsAndFormats` passes.

- [ ] Add an `IDS_ERROR_<CODE>` id to `en-US.rc` for every `PdfErrorCode` and services error code, and a function that maps a typed error to its localised message, keeping the English technical text for the diagnostic copy. Done when: `Localization.EveryErrorCodeHasMessage` iterates the error enums and fails when one lacks an id.
- [ ] Move every count-dependent string (pages, matches, files, results) to plural groups selected with `plural_rules` (`§13`). Done when: `Localization.PluralsAndFormats` renders `1` and `2` for English and `1`, `2` and `5` for Russian test data.
- [ ] Format dates, times, file sizes and numbers with `Windows.Globalization.DateTimeFormatting.DateTimeFormatter` and `Windows.Globalization.NumberFormatting` using the interface language, not hand-built strings. Done when: `Localization.PluralsAndFormats` formats a date and `1234567.5` under `en-US` and `de-DE` with the expected separators.
- [ ] Extend `scripts/check-unresourced-strings.py` to report prose literals of two or more words passed to UI setters, `std::format` and string concatenation in `src/app`. Done when: its test injects `L"Opened " + name` and the checker reports the line.
- [ ] Commit: `feat(i18n): localise error messages, plurals, dates and numbers`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Localization.EveryErrorCodeHasMessage:Localization.PluralsAndFormats` passes and `python -m unittest tests.scripts.test_unresourced_strings` passes.

## 16. Right-to-left readiness: flow direction, mirrored icons and a mirrored pseudolocale

Arabic and Hebrew users are part of the Windows install base and the same apps that translate to them mirror the interface. No right-to-left language is in the first set (§17), but the interface must be ready so adding one is a translation task and not a layout rewrite. This section keeps the page canvas left to right (PDF page coordinates are not mirrored) and mirrors only the interface.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 through 9; layouts mirror about the vertical axis while the document canvas does not. Design board: `design/SPEC.md` section 9.19 with `design/screenshots/D-Lifecycle.png` (drawn by `D06 T04 §25`).
**Job:** With a right-to-left language selected, panels, toolbars and dialogs mirror and directional icons flip, while the page keeps reading left to right. Consumer: translators and users of right-to-left Windows.
**Treatment:** The root `ui::FlowDirection` from `languages.json` (`D10 T03 §7`), `LeftToRight` pinned on the page canvas and thumbnails, the `mirror_in_rtl` glyph-map flag on directional glyphs, and a mirrored pseudolocale generated beside `qps-ploc`. Cheaper substitute that fails the checkpoint: mirroring the whole window including the PDF page.
**Chrome:** The `ui::FlowDirection` mirroring of `D10 T03 §7`, `Recto*Brush` tokens (`D10 T01 §8`).

**Build order:**

1. Add the flow direction plumbing and the mirrored pseudolocale. Done when: `Localization.RtlFlowDirection` passes.
2. Check layouts. Done when: `Localization.RtlSurfaceLayout` passes with screenshots.

- [ ] Set the root `ui::FlowDirection` at startup from the selected language's `direction` and pin the document canvas, page thumbnails and page number box to `LeftToRight`. Done when: `Localization.RtlFlowDirection` finds the shell right to left and the canvas left to right.
- [ ] Set the `mirror_in_rtl` flag in the `RectoIcons` glyph map on back, forward, previous, next and panel toggle glyphs only, not on glyphs that carry meaning independent of reading direction (search, print, zoom). Done when: `Localization.RtlIconAudit` lists each directional glyph and finds the flag only on those.
- [ ] Extend `scripts/generate-pseudolocale.py` to emit a mirrored pseudolocale and check it with `--check`. Done when: the generated resource has the same keys and the script exits `0`.
- [ ] Run the `Localization.PseudolocaleSurfaceLayoutSurvival` checks of `§5` under the mirrored pseudolocale and capture `Settings` and `Convert` screenshots. Done when: `Localization.RtlSurfaceLayout` finds no overlapping controls.
- [ ] Commit: `feat(i18n): add right-to-left readiness and a mirrored pseudolocale`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Localization.RtlFlowDirection:Localization.RtlIconAudit:Localization.RtlSurfaceLayout` passes on the display-session host.

## 17. First language set: ten drafted translations, reviewed flag and installer languages

PDFelement, UPDF and Foxit PDF Editor ship many interface languages, and Recto needs a first set chosen on a stated basis. Default (a decision, with its cost of changing): the ten languages with the largest Windows install bases that Windows and Inno Setup both support well, `zh-Hans`, `es-ES`, `pt-BR`, `ru-RU`, `de-DE`, `fr-FR`, `ja-JP`, `it-IT`, `ko-KR` and `tr-TR`. Changing the set costs one folder and one `languages.json` entry per language plus a review; a second set (including `ar-SA` and `he-IL` for §16, `hi-IN`, `pl-PL`, `nl-NL`, `id-ID`, `uk-UA`, `vi-VN`) follows the same steps. Agents draft all ten translations here and add them to `languages.json` as `draft`; a language ships only after its `status` is flipped to `reviewed` (the flag defined in `§13`) following a native-speaker review recorded by the operator in `D99 T01 §8`. Until then the release ships the languages already reviewed, English at least.

> **Groomed 2026-10-22:** removed `third-party` and the dependency on `D07 T01 §19` (this section only needs the installer language list, `D07 T01 §4`). Agents draft all ten translations and the build and installer read the per-language `reviewed` flag, so the release is complete without a person; the native-speaker review and the scripted flip of the flag moved to `D99 T01 §8` (owner decision 2026-10-22 (foundation plan section 1)).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Draft the ten `<tag>.rc` files, record them in `languages.json` as `draft`, and add `scripts/set-language-reviewed.py`. Done when: `python scripts/check-translations.py` exits `0` with ten `draft` entries and `en-US` as the only `reviewed` entry.
2. Add the installer languages (generated from the `reviewed` entries) and the layout test for every drafted and reviewed language. Done when: `python -m unittest tests.scripts.test_installer_script` and `Localization.AllShippedLanguagesLayout` pass.

- [ ] Create `src/app/strings/<tag>.rc` for each of the ten tags from `en-US` using the glossary, and add each to `languages.json` with `status` `draft`. Done when: `check-translations.py` exits `0` and the ten drafts are complete against `en-US`. Cheaper substitute: an unreviewed machine translation listed as shipped, or a draft that the build compiles into `Recto.exe`.
- [ ] Make the build compile only the `.rc` files of `reviewed` languages into `Recto.exe` (the include list is generated from `languages.json`), and make the Settings list (`§14`) and the installer language list read the same file. Done when: `Localization.ShippedListMatchesBuild` compares the Settings list and the string-table languages found in the built `Recto.exe`, finds only `en-US` with the committed manifest, and finds exactly the flipped tags when a fixture manifest marks two languages `reviewed`.
- [ ] Write `scripts/set-language-reviewed.py <tag> --reviewer <name> --date <YYYY-MM-DD>`, the scripted edit the operator runs after a native-speaker review: it refuses an unknown tag, a missing reviewer or date, and a language whose `.rc` fails `check-translations.py`, sets `status` to `reviewed` with `reviewer` and `reviewed_on`, and changes nothing else in `languages.json`. Done when: `tests/scripts/test_translations.py` flips a fixture language, asserts the diff is those three fields only, and exits `2` for each refusal.
- [ ] Add a `[Languages]` entry in `installer/recto.iss` for each `reviewed` language (the list is generated from `languages.json`) using the `.isl` files that ship with Inno Setup 7, vendoring any missing file under `installer/languages/` with its licence recorded in `NOTICE.md`, set `ShowLanguageDialog=auto`, and write `[CustomMessages]` for the Installation Mode page and the printer task for all ten drafted languages so a flipped flag needs no further edit. Done when: `ISCC.exe` compiles with zero warnings and `tests/scripts/test_installer_script.py` finds the `[Languages]` list equal to the `reviewed` tags and custom messages for all ten.
- [ ] Draft `policy/<tag>/Recto.adml` for the ten languages and include those of `reviewed` languages in the templates archive (`§12`). Done when: `check-admx.py` finds a resolved string table for each drafted tag and the archive holds only the `reviewed` ones.
- [ ] Add `Localization.ShippedLanguagesErrorsAndPlurals` rendering a representative error (`Error_AlgorithmNotPermittedByFips`), a plural count and a formatted date in every drafted and reviewed language (drafts load through a test-only override) and failing on any English fallback. Done when: it passes for all ten languages.
- [ ] Add `Localization.AllShippedLanguagesLayout` running the surface layout survival check of `§5` for the `Home`, `Settings`, `Convert` and `About` pages in every drafted and reviewed language at `1280 x 800`. Done when: it finds no clipped or overlapping control.
- [ ] Record in `docs/store-listing.md` that the first Store listing is English only and that the listing languages follow the shipped set later. Done when: `scripts/verify-release-kit.py` finds the sentence.
- [ ] Commit: `feat(i18n): draft ten interface languages with a reviewed flag and installer languages`

**Test checkpoint:** `python scripts/check-translations.py` exits `0`, `python -m unittest tests.scripts.test_installer_script tests.scripts.test_translations` passes and `recto-tests.exe --gtest_filter=Localization.ShippedListMatchesBuild:Localization.ShippedLanguagesErrorsAndPlurals:Localization.AllShippedLanguagesLayout` passes.

## Verification

- Foundation plan section 10 is 100% ticked `[x]` with machine-verified evidence for every item.
- `AboutPage`, `NOTICE.md`, `docs/license-audit.md`, `installer/recto.iss`, `build/dist/SHA256SUMS.txt`, and `docs/store-license-terms.txt` agree on version, `GPL-3.0-or-later` license text, and component roster with zero hand-copied drift.
- `python scripts/todo-graph.py validate` passes.
