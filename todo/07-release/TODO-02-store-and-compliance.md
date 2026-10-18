---
schema_version: 1
id: store-and-compliance
domain: 07-release
status: active
title: "TODO-02 -- About, GPL-3.0-or-later Compliance, Local Crash Bundle and Release Distribution"
track: M6
---

# TODO-02 -- About, GPL-3.0-or-later Compliance, Local Crash Bundle and Release Distribution

> **Goal:** Ship Recto with complete legal, license, privacy, and localization hygiene: build-time version and git provenance generation (`§1`), the `SPEC 9.8` `AboutPage` (`D-About.png`) with in-app `LICENSE` viewer and `Built-with` component table (`§2`), a build-derived dependency enumeration script and `GPL-3.0-or-later` compatibility audit in `NOTICE.md` and `docs/license-audit.md` (`§3`), opt-in local minidump-plus-log save-and-share crash bundles (`crash-reports/recto-crash-<timestamp>.zip`) that send zero network telemetry (`§4`), full string extraction to `Resources.resw` with a 40%-expansion pseudolocale layout pass (`§5`), the GitHub Release and optional Microsoft Store runbook and listing kit (`§6`), and the final verified v1 distribution bundle (`RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip`, `Recto-Portable-ARM64.zip`, `SHA256SUMS.txt`) closing foundation plan section 10 (`§7`).

> **Current state:** Measured 2026-10-14. `D00 T01 §1` landed `LICENSE` (`GPL-3.0-or-later`), `CLA.md`, and the initial `NOTICE.md` skeleton. No `AboutPage`, build-version generator, crash bundle writer, localization sweep, or release runbook exists yet. On 2026-10-14 the owner approved the opt-in **local minidump-plus-log save-and-share `.zip` bundle** (`D16`) and made Inno Setup (`Full Install` + `Self-Contained Portable`) plus Portable `.zip` archives the primary distribution mechanism with Microsoft Store MSIX submission as an optional extra (`D6`).

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

## Outcome

- `AboutPage` (`§2`) displays build-stamped version truth (`§1`): app version, CPU architecture (`x64` or `ARM64`), Windows App SDK version, `recto-pdf` engine version and git commit SHA, active install mode (`Standard` vs `Portable`), a source link to the exact release tag, an in-app `LICENSE` modal byte-identical to root `LICENSE`, a one-click `Copy version info` clipboard block, and a `Built-with` card generated from the audited dependency roster (`§3`).
- `scripts/audit-dependencies.py` enumerates every shipped library and font from the actual build and verifies 100% parity with `NOTICE.md`, `docs/license-audit.md`, and `AboutPage`'s `Built-with` table (`§3`).
- When opted in via Settings (`Save local crash report bundles`, off by default), unhandled exceptions write a local `crash-reports/recto-crash-<timestamp>.zip` inside `AppPaths::data_dir()` containing only `version.json`, `ring-log.txt` (`D16`), and a thread-stack `minidump.dmp` (`MiniDumpNormal`, zero document heap bytes) with zero network sockets opened (`§4`).
- Every user-facing UI string lives in `src/app/Strings/en-US/Resources.resw` (enforced by `scripts/check-unresourced-strings.py`) and survives a 40%-expansion pseudolocale layout pass without clipping (`§5`).
- `docs/release-and-store-runbook.md` and `docs/store-listing.md` (`§6`) and the final v1 distribution bundle (`§7`) verify every checkbox in foundation plan section 10 with quoted evidence.

**Adjacency:** list=not-applicable (component roster lives inside AboutPage in §2); document=applicable @ D07 T02 §2; settings=applicable @ D02 T04 §6; reporting=applicable @ D07 T02 §4; notifications=not-applicable (next-launch crash folder prompt is part of §4's reporting flow); permissions=not-applicable (no user roles); audit=applicable @ D07 T02 §3; exchange=not-applicable (crash `.zip` bundles and clipboard version blocks are covered in §2 and §4); reverse=not-applicable (release rollback and optional Store delisting are operator procedures documented in §6's runbook)

**Adjacency rationale:** `AboutPage` (`§2`) is the in-app legal/provenance document surface, `NOTICE.md` + `docs/license-audit.md` (`§3`) is the build-verified compliance audit, and the opt-in local crash bundle (`§4`) is the sole diagnostic reporting artifact Recto produces.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Build-time version and provenance generator (version, SHA, `Standard` vs `Portable`) | D02 T04 §6 | [ ] |
| 2 | §2 | `AboutPage` (`SPEC 9.8`, `D-About.png`: version truth, `LICENSE` modal and `Built-with`) | §1, D02 T04 §10 | [ ] |
| 3 | §3 | Build-derived dependency enumeration script, `NOTICE.md` and `docs/license-audit.md` | §2, D00 T01 §1 | [ ] |
| 4 | §4 | Opt-in local crash report bundle writer (`MiniDumpNormal` + D16 ring log `.zip` prompt) | §2 | [ ] |
| 5 | §5 | Localization readiness (`Resources.resw` `x:Uid` extraction and 40% pseudolocale check) | §2 | [ ] |
| 6 | §6 | GitHub Release runbook (`docs/release-and-store-runbook.md`) and Store submission kit | §3, §4, §5, D03 T02 §6, D03 T03 §5, D03 T03 §10, D04 T03 §6, D06 T01 §4, D06 T01 §6, D06 T01 §8, D06 T02 §4, D06 T02 §6, D07 T01 §7 | [ ] |
| 7 | §7 | End-to-end v1 distribution bundle (`Setup.exe`, `Portable.zip`, `SHA256SUMS.txt`) & GPL | §6 | [ ] |

## 1. Build-time version and provenance generator (version, SHA, `Standard` vs `Portable`)

Eliminate hand-typed version strings across the codebase by writing a deterministic build-time generator (`scripts/generate-version-header.py` wired into `Directory.Build.targets` before `ClCompile`) that reads the canonical version from `VERSION` (`1.0.0`), queries `git rev-parse --short=12 HEAD`, reads the Windows App SDK version from `src/app/packages.config`, embeds the root `LICENSE` text and component metadata as `constexpr` string views in `build/generated/recto_version.h`, and provides `recto::core::VersionInfo::current(bool is_portable)` returning the structured runtime provenance block used by `AboutPage` (`§2`), `recto-cli --version`, and `version.json` inside crash bundles (`§4`).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Create root `VERSION` (`1.0.0`), author `scripts/generate-version-header.py` emitting `build/generated/recto_version.h` (only overwriting the output file when content actually changes so incremental MSBuild compiles never re-trigger needlessly), and wire a `BeforeTargets="ClCompile"` MSBuild target in `Directory.Build.targets`. Done when: `.\build.cmd` generates `build/generated/recto_version.h` and a second run of `scripts/generate-version-header.py` preserves the file mtime when nothing changed.
2. Implement `recto::core::VersionInfo` (`src/core/version_info.h` and `src/core/version_info.cpp`) exposing `app_version`, `architecture` (`"x64"` or `"ARM64"`), `windows_app_sdk_version`, `engine_name_and_version` (`"recto-pdf 1.0.0"`), `git_commit_sha`, `source_tag_url` (`"https://github.com/<owner>/RectoPDF/tree/v1.0.0"`), `license_spdx` (`"GPL-3.0-or-later"`), `embedded_license_text`, `install_mode` (`"Portable"` when `AppPaths::is_portable()` is true, else `"Standard"`), `to_clipboard_markdown()`, and `to_json()`. Done when: `VersionInfo.*` unit tests in `recto-tests` pass on `x64` and `ARM64`.

- [ ] Read the canonical version from root `VERSION` (`1.0.0`) and `packages.config` (`Microsoft.WindowsAppSDK` version `2.4.0`) in `scripts/generate-version-header.py`, never hard-coding version strings in C++ or XAML source files. Done when: changing `VERSION` in a test `TempDir` updates `app_version` and `source_tag_url` in the generated header with zero C++ edits. Cheaper substitute: typing `"0.4.0"` or `"1.0.0"` as string literals inside `AboutPage.xaml` that drift out of sync across `Recto.exe`, `recto-cli.exe`, and `installer/recto.iss`.
- [ ] Stamp the exact `git rev-parse --short=12 HEAD` commit SHA and build target architecture (`#if defined(_M_ARM64) "ARM64" #else "x64" #endif`) into `build/generated/recto_version.h`. Done when: `VersionInfo.CurrentMatchesGitHead` verifies `VersionInfo::current().git_commit_sha` is non-empty and matches the 12-hex-char format.
- [ ] Embed the exact bytes of root `LICENSE` into `build/generated/recto_version.h` as `recto::core::kEmbeddedLicenseText` so the in-app License modal works even if the user deletes external text files, and is guaranteed byte-for-byte identical to root `LICENSE`. Done when: `VersionInfo.EmbeddedLicenseMatchesRootFile` compares `kEmbeddedLicenseText` against `read_file_bytes(repo_root() / "LICENSE")` and asserts `0` byte differences.
- [ ] Implement `VersionInfo::to_clipboard_markdown()` and `VersionInfo::to_json()` including `app_version`, `architecture`, `install_mode` (`Standard` vs `Portable`), `windows_app_sdk_version`, `engine_version`, `git_commit_sha`, `os_version_build`, and `license_spdx`. Done when: `VersionInfo.ClipboardAndJsonSerialization` verifies both formats contain all 8 fields for both `Standard` and `Portable` modes.
- [ ] Ensure `scripts/generate-version-header.py` uses write-if-changed semantics so running `.\build.cmd` twice does not touch `build/generated/recto_version.h`'s modification timestamp if `VERSION`, `LICENSE`, and `HEAD` have not changed. Done when: `VersionInfo.GeneratorWriteIfChanged` verifies identical `st_mtime_ns` across a no-op second invocation.
- [ ] Update `recto-cli --version` to print `VersionInfo::current().to_clipboard_markdown()` so CLI and GUI version reporting come from the exact same struct. Done when: `recto-cli --version` outputs the generated version, architecture, engine version, and commit SHA.
- [ ] Commit: `build: add build-time version, git commit, and embedded LICENSE generator`

**Test checkpoint:** `recto-tests --gtest_filter=VersionInfo.*` passes and `recto-cli --version` output is quoted showing the generated version, architecture, `recto-pdf` version, and git commit SHA.

## 2. `AboutPage` (`SPEC 9.8`, `D-About.png`: version truth, `LICENSE` modal and `Built-with`)

Implement `src/app/views/AboutPage.xaml` and `src/app/views/AboutPage.xaml.h/.cpp` matching `design/SPEC.md` section 9.8 and `design/screenshots/D-About.png` token-for-token, reachable from Settings -> `About Recto` (`D02 T04 §6`), the Classic `Help -> About Recto` menu item (`D02 T01 §10`), and the `Ctrl+K` Command Palette (`help.about`):
- Brand icon (`resources/png/recto-128.png`) and `Newsreader` 24 px lockup, version and architecture badge (`Version 1.0.0 (x64)`), one-sentence description, and `Released under the GNU General Public License v3.0 or later (GPL-3.0-or-later)`.
- Technical details card (`--bg-elevated`, `--border-subtle`) showing `Windows App SDK` version, `Engine` (`recto-pdf 1.0.0 (<commit>)`), `Install mode` (`Standard` or `Portable`), and `Source` link pointing to the exact release tag URL.
- Primary action bar: `View source` (`--accent` filled button opening the release tag URL via `ExternalLinkConfirmDialog` or system browser), `License` (opening the in-app `LicenseDialog` displaying `kEmbeddedLicenseText` in a monospace `JetBrains Mono` / `Cascadia Mono` scroll viewer), and `Copy version info` (copying `VersionInfo::to_clipboard_markdown()` to the Windows clipboard with a brief `"Copied to clipboard"` confirmation + UIA polite announcement).
- Secondary links row: `Report a problem`, `Contribute`, and `Keyboard shortcuts` (invoking `D02 T04 §10`'s `KeyboardShortcutsDialog`).
- `Built with` card rendering the component table (`recto-pdf`, `Windows App SDK`, `Direct2D / DirectWrite / WIC / CNG / Windows.Media.Ocr`, `Newsreader`, and `Segoe Fluent Icons`) with each component's role and license badge from the shared `kBuiltWithComponents` table audited in `§3`.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.8 and `design/screenshots/D-About.png`.
**Job:** The user (or anyone verifying `GPL-3.0-or-later` compliance) can inspect Recto's exact build version, architecture, install mode (`Standard` vs `Portable`), full `GPL-3.0-or-later` license text in-app, source tag link, and every component Recto is built with, or copy a bug-report-ready version block in one click. Consumer: end users, bug reporters pasting `Copy version info`, and open-source compliance auditors.
**Treatment:** All fields populated dynamically from `recto::core::VersionInfo::current()` (`§1`) and `recto::core::built_with_components()` (`§3`), with the in-app `LicenseDialog` displaying `kEmbeddedLicenseText` byte-identical to root `LICENSE`. Cheaper substitute that fails the checkpoint: hard-coded mockup strings (`"0.4.0"`, `"a1b2c3d"`) or opening an external web browser instead of displaying the full `GPL-3.0-or-later` license text in-app.
**Chrome:** Consume `RectoColors.xaml` semantic tokens, `resources/png/recto-128.png` brand mark, and the standard `ContentDialog` family for `LicenseDialog`.

**Build order:**

1. Implement `AboutViewModel` (`src/app/viewmodels/AboutViewModel.h/.cpp`) backed by `VersionInfo::current(AppPaths::is_portable())` and `built_with_components()`, plus `copy_version_info_to_clipboard()` and `LicenseDialog` (`src/app/dialogs/LicenseDialog.xaml/.h/.cpp`). Done when: `About.*` unit tests in `recto-tests` verify every property, the `Built with` component rows, `LicenseDialog` text byte-equality with root `LICENSE`, and clipboard readback.
2. Author `src/app/views/AboutPage.xaml` and `.cpp` matching `SPEC 9.8` and `D-About.png`, and wire navigation from `Settings -> About Recto` (`D02 T04 §6`), Classic `Help -> About Recto`, and `Ctrl+K` Command Palette (`help.about`). Done when: every control on `SPEC 9.8` is working, carries `AutomationProperties.Name`, and passes `About.*` tests.

- [ ] Populate every version, commit, architecture, install-mode (`Standard` vs `Portable`), and tag URL field in `AboutViewModel` from `VersionInfo::current()` (`§1`) with zero hard-coded version strings in `AboutPage.xaml` or `AboutViewModel.cpp`. Done when: `About.ViewModelReflectsGeneratedVersionInfo` verifies that every string in `AboutViewModel` matches `VersionInfo::current()`. Cheaper substitute that fails the checkpoint: hard-coding version strings in XAML that go stale on the next commit or version bump.
- [ ] Build the brand header and technical details card in `AboutPage.xaml` matching `D-About.png`: `resources/png/recto-128.png` icon, `Newsreader` title, version + architecture subtitle, `GPL-3.0-or-later` license statement, and the 4-row technical details box (`Windows App SDK`, `Engine`, `Install mode`, `Source`). Done when: light and dark theme renders of `AboutPage` match `D-About.png` using only `RectoColors.xaml` tokens.
- [ ] Implement the in-app `LicenseDialog` opened by the `License` button, rendering `VersionInfo::current().embedded_license_text` in a scrollable monospace view with a `Copy license` button and `Close` button. Done when: `About.InAppLicenseMatchesRootLicenseFile` verifies the text bound to `LicenseDialog` equals root `LICENSE` (`35,149` bytes, SHA-256 matched) byte-for-byte.
- [ ] Implement the `Copy version info` button writing `VersionInfo::to_clipboard_markdown()` to the system clipboard, showing a transient `"Copied"` status confirmation, and firing a polite `AnnouncementService` notification. Done when: `About.CopyVersionInfoPopulatesClipboard` invokes the command and reads back the exact multi-line version block from the clipboard.
- [ ] Wire `View source`, `Report a problem`, and `Contribute` links to the repository URLs, and wire the `Keyboard shortcuts` link to open `D02 T04 §10`'s `KeyboardShortcutsDialog`. Done when: clicking `Keyboard shortcuts` on `AboutPage` opens `KeyboardShortcutsDialog` in integration tests.
- [ ] Render the `Built with` card from `recto::core::built_with_components()` listing `recto-pdf` (`GPL-3.0-or-later`), `Windows App SDK 2.4.0` (`MIT`), `Direct2D / DirectWrite / WIC / CNG / Windows.Media.Ocr` (`Windows 11 OS Components`), `Newsreader` (`SIL OFL 1.1`), and `Segoe Fluent Icons` (`Windows 11 OS Font`). Done when: `About.BuiltWithRosterMatchesNotice` verifies all 5 component rows and their license labels.
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
- [ ] Inspect staged `Recto.exe` and `recto-cli.exe` via `dumpbin /dependents` in `scripts/audit-dependencies.py` (when `--binaries-dir` is provided) and verify every imported DLL is either a documented Windows 11 OS system DLL (`d2d1.dll`, `dwrite.dll`, `d3d11.dll`, `dxgi.dll`, `windowscodecs.dll`, `bcrypt.dll`, `crypt32.dll`, `kernel32.dll`, `user32.dll`, `ole32.dll`, `oleaut32.dll`, `shell32.dll`, `shlwapi.dll`, `comdlg32.dll`, `winspool.drv`, `dbghelp.dll`, MSVC CRT) or part of the documented `Microsoft.WindowsAppSDK` / `Microsoft.Windows.SDK.BuildTools` runtime (`MIT` / redistributable). Done when: zero unclassified DLL imports exist on `Recto.exe` or `recto-cli.exe`.
- [ ] Complete `NOTICE.md` covering every component: `Recto` and `recto-pdf` (`GPL-3.0-or-later`), `Microsoft.WindowsAppSDK 2.4.0` (`MIT` + Microsoft Windows App SDK License), `Windows 11 SDK System Libraries` (Direct2D, DirectWrite, WIC, CNG, CryptoAPI, `Windows.Media.Ocr`, `DbgHelp`), `Newsreader` typeface (`SIL Open Font License 1.1`, converted to static vector paths at build time in `design/tools/build_resources.py`), and `GoogleTest` (`BSD-3-Clause`, explicitly marked `test-only: linked only into recto-tests.exe, never shipped in release packages`). Done when: `NOTICE.md` contains full attribution text and license summaries for every item.
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
2. Wire `CrashReporter` in `App.xaml.cpp` to `SettingsService::save_crash_reports()` (`D02 T04 §6`) and implement the next-launch prompt (`CrashReportPrompt` in `src/app/`) offering `Open crash-reports folder` (opening `Explorer.exe` to the `crash-reports` directory) and `Dismiss` (updating `last_acknowledged_crash_utc` in settings). Done when: launching after a simulated crash with opt-in displays the prompt once and dismissing it prevents repeat prompts on subsequent launches.

- [ ] Enforce the off-by-default privacy gate: verify `SettingsService::save_crash_reports()` defaults to `false`, and when `false`, `CrashReporter::write_crash_bundle_for_test()` writes `0` files and does not create a `crash-reports/` directory. Done when: `CrashReport.OptOutWritesNothing` passes in `recto-tests`. Cheaper substitute that fails the checkpoint: writing minidumps unconditionally before checking the user's opt-in setting.
- [ ] Write `minidump.dmp` using `MiniDumpWriteDump` with `MiniDumpNormal` (`0x00000000` / thread stacks and module list only, explicitly asserting `(dump_type & MiniDumpWithFullMemory) == 0` and `(dump_type & MiniDumpWithIndirectlyReferencedMemory) == 0`) so heap-allocated `pdf::Document` buffers and `Bitmap` pixels never enter the dump. Done when: `CrashReport.HeapSecretCanaryExcludedFromBundle` allocates a `64 KB` heap buffer filled with `"SECRET_PDF_DOCUMENT_CANARY_BYTES"`, triggers a synthetic `EXCEPTION_POINTERS` bundle capture, decompresses all entries in `recto-crash-*.zip`, and asserts `0` occurrences of the canary string anywhere in the `.zip` or `minidump.dmp`.
- [ ] Package the crash bundle as a standard `.zip` archive (`recto-crash-<YYYYMMDD-HHMMSS>-<pid>.zip`) containing **strictly** three entries (`version.json`, `ring-log.txt`, `minidump.dmp`) using `zip_archive` (`D04 T03 §1`) and removing the intermediate unzipped files via `AtomicFileWriter` patterns. Done when: `CrashReport.BundleContainsOnlyThreeAllowlistedFiles` opens the generated `.zip` and asserts the entry list equals `{"version.json", "ring-log.txt", "minidump.dmp"}` with valid `MINIDUMP_SIGNATURE` (`"MDMP"`), valid `version.json`, and recent `D16` ring log lines.
- [ ] Resolve the output directory through `AppPaths::data_dir() / "crash-reports"` (`D02 T04 §1`) so in Portable mode (`<exe-dir>\data\portable.mode`) crash bundles are saved inside `<exe-dir>\data\crash-reports\` and never touch `%LOCALAPPDATA%\Recto`. Done when: `CrashReport.PortableModeSavesInsideDataDir` verifies the `.zip` is created inside `<temp-portable>\data\crash-reports\` and `%LOCALAPPDATA%` remains untouched.
- [ ] Verify via `dumpbin /dependents` and source scan (`SecurityAudit` / `CrashReport.ZeroNetworkImports`) that `recto-core.lib`, `recto-services.lib`, and `Recto.exe` import zero functions from `winhttp.dll`, `wininet.dll`, or `ws2_32.dll`. Done when: `CrashReport.ZeroNetworkImports` passes.
- [ ] Implement the next-launch `CrashReportPrompt` detection (`CrashReporter::find_unacknowledged_bundles(last_seen_utc)`): when an unacknowledged `recto-crash-*.zip` is present at startup, show a non-blocking banner/dialog naming the `.zip` filename with `Open folder` and `Dismiss` actions, and record acknowledgment so it does not re-prompt on every subsequent launch. Done when: `CrashReport.NextLaunchDetectionAndAcknowledge` verifies detection before acknowledgment and silence after acknowledgment.
- [ ] Commit: `feat(privacy): add opt-in local crash report .zip bundle writer and next-launch folder prompt (D16)`

**Test checkpoint:** `recto-tests --gtest_filter=CrashReport.*` passes under ASan with the 3-entry `.zip` inventory (`version.json`, `ring-log.txt`, `minidump.dmp`), `MiniDumpNormal` heap-canary exclusion check, Portable mode path check, and zero-network check quoted.

## 5. Localization readiness (`Resources.resw` `x:Uid` extraction and 40% pseudolocale check)

Prepare Recto for future translations without a UI refactor: extract all user-facing strings from `src/app/**/*.xaml` and `src/app/**/*.cpp` into `src/app/Strings/en-US/Resources.resw` (bound via `x:Uid` in XAML and `ResourceLoader` in C++), author `scripts/check-unresourced-strings.py` (with unit tests in `tests/scripts/test_unresourced_strings.py`) enforcing that no unresourced user-facing string literals appear in `src/app`, and add a deterministic 40%-expansion pseudolocale generator (`scripts/generate-pseudolocale.py` -> `src/app/Strings/qps-ploc/Resources.resw` `[!! ... !!]` with accented characters) verified across the primary app surfaces (`Home`, `DocumentView`, `Convert`, `Organize`, `Settings`, `About`).

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 through 9; layouts must survive 40% string length expansion (`qps-ploc`) without text truncation clip, overlapping controls, or off-screen buttons.
**Job:** Future translators can localize Recto by translating `Resources.resw` alone without touching C++ or XAML files, and longer languages (such as German, French, or Polish) do not break toolbar, card, or settings layouts. Consumer: translators and non-English Windows users.
**Treatment:** Complete `src/app/Strings/en-US/Resources.resw` string table, automated static scanner `scripts/check-unresourced-strings.py`, and generated `qps-ploc` pseudolocale resource file verified by layout tests and screenshots. Cheaper substitute that fails the checkpoint: leaving English string literals hard-coded across XAML `Text="..."` / `Content="..."` attributes and C++ viewmodels.
**Chrome:** Existing `SPEC.md` layouts using flexible `Grid` (`*` / `Auto`) and `TextTrimming="CharacterEllipsis"` + tooltips where appropriate so 40%-expanded strings wrap or elide gracefully without overlapping sibling controls.

**Build order:**

1. Extract all user-facing strings from `src/app/**/*.xaml` (`Text`, `Content`, `Header`, `PlaceholderText`, `Title`, `ToolTipService.ToolTip`, `AutomationProperties.Name`) and `src/app/**/*.cpp` into `src/app/Strings/en-US/Resources.resw` with structured keys (`Shell_Tab_Home.Text`, `Settings_PortableMode_Title.Text`, etc.), and write `scripts/check-unresourced-strings.py` + `tests/scripts/test_unresourced_strings.py`. Done when: `python scripts/check-unresourced-strings.py` exits `0` with `0` unresourced user-facing literals.
2. Author `scripts/generate-pseudolocale.py` generating `src/app/Strings/qps-ploc/Resources.resw` (expanding every English value by ~40% with accented characters and `[!! ... !!]` brackets while preserving `{0}` / `%1` format placeholders), and add `Localization.*` tests in `tests/recto-tests/localization_tests.cpp` verifying every key loads cleanly and main surfaces (`Home`, `Convert`, `Settings`, `About`) layout with zero overlapping bounding boxes under `qps-ploc`. Done when: `recto-tests --gtest_filter=Localization.*` passes and pseudolocale screenshots are captured.

- [ ] Populate `src/app/Strings/en-US/Resources.resw` with every user-facing string across `src/app/` (shell tabs, menus, command palette labels, toolbar tooltips, `AutomationProperties.Name`, Convert cards and Step 3 options, Organize bar and dialogs, Settings rows, About page, and error/confirmation dialogs). Done when: `Resources.resw` is valid XML and compiles into `resources.pri` with `0` warnings. Cheaper substitute that fails the checkpoint: moving only 10 strings into `Resources.resw` while leaving hundreds of hard-coded English strings across `.xaml` and `.cpp` files.
- [ ] Write `scripts/check-unresourced-strings.py` scanning all `src/app/**/*.xaml` and `src/app/**/*.cpp` files for literal user-facing attributes (`Text="..."`, `Content="..."`, `Header="..."`, `PlaceholderText="..."`, `Title="..."`, `ToolTipService.ToolTip="..."`, `AutomationProperties.Name="..."`) that are not bound via `x:Uid`, `{x:Bind}`, or an explicit documented non-translatable allowlist (such as numeric zoom percentages `"100%"`, brand name `"Recto"`, or glyph codes `"\uE700"`). Done when: `python scripts/check-unresourced-strings.py` exits `0` on `src/app` and fails with file and line numbers when a raw literal `Text="Hello"` is injected in `tests/scripts/test_unresourced_strings.py`.
- [ ] Write `scripts/generate-pseudolocale.py` transforming every entry in `src/app/Strings/en-US/Resources.resw` into a 40%-expanded pseudolocalized string (`[!! Ŝëţţîñğš ăñđ Pŕëƒëŕëñčëš ~~~ !!]`) while preserving format specifiers (`{0}`, `%s`, `%d`) intact. Done when: `python scripts/generate-pseudolocale.py --check` verifies `src/app/Strings/qps-ploc/Resources.resw` has the exact same key set as `en-US` and intact format placeholders.
- [ ] Add `Localization.AllReswKeysHaveNonEmptyValuesAndMatchingPlaceholders` in `tests/recto-tests/localization_tests.cpp` comparing `en-US/Resources.resw` and `qps-ploc/Resources.resw` key-by-key. Done when: `Localization.AllReswKeysHaveNonEmptyValuesAndMatchingPlaceholders` passes.
- [ ] Add `Localization.PseudolocaleSurfaceLayoutSurvival` in `tests/recto-tests/localization_tests.cpp` loading the `qps-ploc` strings into `SettingsPage`, `ConvertPage`, `HomePage`, and `AboutPage` at `1280 x 800` (`100%` scale), running a layout pass, and asserting that no interactive control (`Button`, `ToggleSwitch`, `ComboBox`) has a zero-width/height clip or overlaps a sibling control's bounding box, and capture `qps-ploc` screenshots of `Settings` and `Convert`. Done when: `Localization.PseudolocaleSurfaceLayoutSurvival` passes with `0` layout collisions.
- [ ] Commit: `feat(i18n): extract all UI strings to Resources.resw, add literal scanner, and verify qps-ploc layout`

**Test checkpoint:** `python scripts/check-unresourced-strings.py` exits `0`, `python -m unittest tests.scripts.test_unresourced_strings` passes, and `recto-tests --gtest_filter=Localization.*` passes with `qps-ploc` screenshots of `Settings` and `Convert` recorded.

## 6. GitHub Release runbook (`docs/release-and-store-runbook.md`) and Store submission kit

Author the step-by-step release engineering runbook (`docs/release-and-store-runbook.md`) and the optional Microsoft Store Partner Center submission kit (`docs/store-listing.md` and `docs/store-license-terms.txt`):
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
- [ ] Commit: `docs(release): add GitHub Release runbook, optional Store kit, and real-app release screenshots`

**Test checkpoint:** `python scripts/verify-release-kit.py` exits `0` (confirming `docs/store-license-terms.txt` equals `LICENSE` byte-for-byte and all screenshots exist) and `python -m unittest tests.scripts.test_release_kit` passes.

## 7. End-to-end v1 distribution bundle (`Setup.exe`, `Portable.zip`, `SHA256SUMS.txt`) & GPL

Execute the complete v1 release candidate build and verification pipeline from a clean state, produce the final `build/dist/` distribution bundle (`RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip`, `Recto-Portable-ARM64.zip`, `SHA256SUMS.txt`), verify every item in foundation plan section 10 ("Licensing and Compliance Checklist"), and update `docs/plans/2026-10-13-recto-foundation.md` section 10 with the verified checkboxes and evidence references.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Run the full v1 release gate (`.\build.cmd`, `pwsh -NoProfile -File build/build-installer.ps1`, `pwsh -NoProfile -File tests/scripts/verify_installer.ps1`, `python scripts/audit-dependencies.py --check`, `python scripts/check-package-assets.py`, `python scripts/check-unresourced-strings.py`, `python scripts/verify-release-kit.py`) and verify `build/dist/SHA256SUMS.txt` against all four release artifacts. Done when: all commands exit `0` and `build/dist/SHA256SUMS.txt` hashes verify cleanly.
2. Update `docs/plans/2026-10-13-recto-foundation.md` section 10 ("Licensing and Compliance Checklist") ticking each verified checkbox and citing its owning proof file (`LICENSE`, `AboutPage`, `NOTICE.md`, `docs/license-audit.md`, `docs/store-license-terms.txt`, `CLA.md`), and run `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check`. Done when: every section 10 checkbox in `docs/plans/2026-10-13-recto-foundation.md` is ticked `[x]` with its evidence citation and `todo-graph.py validate` exits `0`.

- [ ] Build the complete v1 release bundle via `pwsh -NoProfile -File build/build-installer.ps1` and verify that `build/dist/RectoSetup-x64.exe`, `build/dist/RectoSetup-ARM64.exe`, `build/dist/Recto-Portable-x64.exe` / `Recto-Portable-x64.zip`, `build/dist/Recto-Portable-ARM64.zip`, and `build/dist/SHA256SUMS.txt` exist and are non-empty. Done when: `Get-FileHash -Algorithm SHA256` on all four artifacts matches `build/dist/SHA256SUMS.txt` line-for-line. Cheaper substitute: signing off on section 10 without building and hashing all four (`x64`/`ARM64` Setup and Portable) release artifacts together.
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
- [ ] Commit: `release: verify v1 distribution bundle and sign off foundation plan section 10 compliance checklist`

**Test checkpoint:** the contents of `build/dist/SHA256SUMS.txt`, the output of `tests/scripts/verify_installer.ps1`, the output of `python scripts/audit-dependencies.py --check --binaries-dir build/stage/x64`, and the ticked section 10 checklist in `docs/plans/2026-10-13-recto-foundation.md` are quoted verbatim.

## Verification

- Foundation plan section 10 is 100% ticked `[x]` with machine-verified evidence for every item.
- `AboutPage`, `NOTICE.md`, `docs/license-audit.md`, `installer/recto.iss`, `build/dist/SHA256SUMS.txt`, and `docs/store-license-terms.txt` agree on version, `GPL-3.0-or-later` license text, and component roster with zero hand-copied drift.
- `python scripts/todo-graph.py validate` passes.
