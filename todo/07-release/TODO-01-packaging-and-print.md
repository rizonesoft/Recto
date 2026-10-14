---
schema_version: 1
id: packaging-and-print
domain: 07-release
status: active
title: "TODO-01 -- Printing, Inno Setup (Full & Portable) Installer, Optional MSIX and Install Proof"
track: M6
---

# TODO-01 -- Printing, Inno Setup (Full & Portable) Installer, Optional MSIX and Install Proof

> **Goal:** Recto becomes an installable and portable product: printing through the system print dialog on the API this file decides first, an Inno Setup installer (`installer/recto.iss` built with `ISCC.exe`) supporting both a **Full Install** (with `.pdf` file association, icons, shortcuts and uninstaller) and a **Self-Contained Portable Install** (`<exe-dir>\data\portable.mode` with zero registry or `%LOCALAPPDATA%` footprint), plus a standalone Portable `.zip` archive, optional MSIX packaging, and an automated install, portable, file-association, upgrade and clean-uninstall proof.

> **Current state:** Measured 2026-10-14. No code exists. D02 T01 §1 authors the self-contained unpackaged and MSIX-ready project, manifest and assets; D02 T04 §1 and §2 implement Portable mode path resolution (`<exe-dir>\data\portable.mode`) and the in-app Portable mode setting toggle; D02 T02 provides page rendering the print path reuses at print resolution. On 2026-10-14 decision D6 was revised to make Inno Setup (`ISCC.exe`, installed at `C:\Program Files\Inno Setup 7\ISCC.exe`) the primary distribution mechanism supporting both Full Install and Self-Contained Portable Install, with MSIX / Microsoft Store packaging as an optional extra. SPEC section 10 gives Ctrl+P the system print dialog; section 9.7's Default PDF app row opens the Windows settings page.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D6 Inno Setup Full & Portable installer + optional MSIX), 2 (packaging row), 7 (M6 and the printing note), 11 (print API open item).
- `resources/README.md` (asset-to-manifest mapping, `.ico` and `.pdf` association logo rules, splash background) and `design/SPEC.md` sections 3.2 (file-type icon behavior), 9.7 (Portable mode and Default PDF app), 10.
- -> XREF: D02 T01 §1 -- the self-contained app configuration, manifest and assets authored there are what §2 here packages into the Inno Setup installer, Portable `.zip` and optional MSIX.

## Outcome

- Ctrl+P prints the current document through the Windows print dialog with page range and copies honored, rendered at printer resolution through the engine.
- `installer/recto.iss` builds `RectoSetup-x64.exe` and `RectoSetup-ARM64.exe` via `ISCC.exe`, supporting both Full Install (registering `.pdf` file association and Recto's file-type icon, shortcuts, and uninstaller) and Self-Contained Portable Install (writing `data\portable.mode` next to `Recto.exe` with zero registry changes), alongside `Recto-Portable-x64.zip` / `Recto-Portable-ARM64.zip` and optional MSIX packages.
- Automated silent Full Install, Portable Install, upgrade continuity, and clean uninstall are proven end to end.

**Adjacency:** list=not-applicable (installed apps are listed by Windows, not Recto); document=applicable @ D07 T01 §1; settings=not-applicable (print choices live in the system dialog; the Portable mode and Default PDF app rows live in D02 T04 §2); reporting=not-applicable (crash reporting is D07 T02 §3); notifications=not-applicable (none originate here); permissions=not-applicable (package capabilities are declared in the manifest, D02 T01 §1, and audited in section 2); audit=not-applicable (release provenance is D07 T02 §2 and §4); exchange=not-applicable (no data import); reverse=applicable @ D07 T01 §3

**Adjacency rationale:** Print is the viewer's document surface delivered in M6 because its API choice was a named M6 open item. The reverse of install is a clean uninstall (for Full Install) and self-contained directory removal (for Portable mode), verified in section 3. Optional MSIX capabilities and Inno Setup file/registry entries are audited inside section 2.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Print API decision and printing | D02 T02 §3 | [ ] |
| 2 | §2 | Inno Setup installer (Full & Portable), Portable archive and optional MSIX | §1, D02 T01 §1, D02 T04 §1 | [ ] |
| 3 | §3 | Automated Full Install, Portable Install, association and upgrade proof | §2 | [ ] |

## 1. Print API decision and printing

First the decision, recorded with its evidence: spike both candidate paths on Windows App SDK 2.4.0 in the app (the print contract through `PrintManagerInterop` against the window handle, and `ID2D1PrintControl` over the existing D2D device), judging on preview correctness, page-range and copies support, and how page rendering at printer DPI composes (vector through display lists where the path allows, raster otherwise at stated DPI). Then build printing on the winner: system dialog and programmatic print-to-file seam for automated verification, current document, range and copies honored, rendering through the engine off the UI thread, cancellable.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 10 (Ctrl+P, system print dialog); print output fidelity is to the page itself, checked against `recto-cli` reference renders.
**Job:** The user can print a document or a range of it, as it looks. Consumer: the printer (and Microsoft Print to PDF / XPS print-Control round-trip used as the automated test oracle).
**Treatment:** The decided API after a recorded spike, engine-rendered pages. Cheaper substitute that fails the checkpoint: printing the screen-resolution tiles, or deciding the API by documentation alone when the plan demands confirmation.
**Chrome:** consume the print dialog the platform provides and the command registry's Print entry (toolbar, Ctrl+P, command box). Do not invent a custom print preview.

**Build order:**

1. Spike both APIs in the WinUI 3 app and record the decision (with reversal cost) as a dated resolution to plan section 11's open item. Done when: each spike prints the test page or names its concrete failure, and the decision names the loser accurately.
2. Implement printing on the chosen path: page range parsing shared with D05 T01 §1's parser, printer-DPI rendering through a job-shaped engine path (not the viewer's actor), preview pages generated on demand, plus a non-interactive print-to-PDF test entry for `recto-tests`. Done when: Print-to-PDF output of a corpus file renders page-equal to the source at matched pages.

- [ ] Recorded print API decision from working spikes. Done when: plan section 11's row is answered in the plan and here identically. Cheaper substitute that fails the checkpoint: picking the familiar API untested in a WinUI 3 context.
- [ ] Printing with range, copies and cancellation. Done when: a Print-to-PDF run of a middle range yields exactly those pages.
- [ ] Commit: `release: printing via the decided API with engine-rendered pages`

**Test checkpoint:** `recto-tests --gtest_filter=Print.*` and `recto-cli render` comparisons of the Print-to-PDF outputs against sources (quoted pages), with the spike evidence and decision recorded in findings.

## 2. Inno Setup installer (Full & Portable), Portable archive and optional MSIX

Build the primary Inno Setup installer (`installer/recto.iss` compiled with `ISCC.exe`) and standalone Portable `.zip` archive for `x64` and `ARM64`, plus optional MSIX packaging (`build/build.ps1 -Package`). `installer/recto.iss` presents an Installation Mode wizard page (and supports silent `/PORTABLE=1` CLI flag) choosing between **Full Installation** (installs to `{autopf}\Recto` or user path, registers `.pdf` `OpenWithProgids` file association with `resources/ico/recto.ico` and `PdfFile.png`, creates Start Menu shortcut and uninstaller, displays `LICENSE` `GPL-3.0-or-later`) and **Self-Contained Portable Installation** (extracts the self-contained Windows App SDK + `Recto.exe` + `recto-cli.exe` + `LICENSE` + `NOTICE.md` into the selected folder, writes `data\portable.mode`, and makes zero registry, Start Menu, or uninstaller modifications).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `installer/recto.iss` and wire `build/build-installer.ps1` (invoked by `build/build.ps1 -Package` and `.github/workflows/build.yml`): stage self-contained `Release|x64` and `Release|ARM64` outputs (`Recto.exe`, `recto-cli.exe`, Windows App SDK runtime DLLs, PRI/XAML resources, `LICENSE`, `NOTICE.md`), compile `RectoSetup-x64.exe` and `RectoSetup-ARM64.exe` via `ISCC.exe`, create `Recto-Portable-x64.zip` and `Recto-Portable-ARM64.zip` with `data\portable.mode` pre-created, and support optional MSIX generation when `-EnableMsix` is passed. Done when: `powershell -ExecutionPolicy Bypass -File build/build-installer.ps1` produces `RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip` and `Recto-Portable-ARM64.zip` under `build/dist/`.
2. Capability and asset audit: verify every icon and asset in `installer/recto.iss` and `src/app/Package.appxmanifest` resolves to a generated file from `resources/` at the right size, and verify `src/app/Package.appxmanifest` capabilities are minimal and justified. Done when: the scripted asset and manifest check exits 0 with zero missing assets.

- [ ] `installer/recto.iss` + `build/build-installer.ps1` producing `RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip` and `Recto-Portable-ARM64.zip` (plus optional MSIX target). Done when: `ISCC.exe` compiles both architectures with 0 warnings/errors and the portable `.zip` contains the complete self-contained runtime and `data\portable.mode`. Cheaper substitute: an installer that lacks Portable mode or requires separate external runtime installs.
- [ ] Asset and optional MSIX manifest audit recorded. Done when: every icon/logo reference resolves to `resources/` and `Package.appxmanifest` capabilities are justified in one sentence each.
- [ ] Commit: `release: Inno Setup Full and Portable installer, portable zip and optional MSIX packaging`

**Test checkpoint:** `powershell -ExecutionPolicy Bypass -File build/build-installer.ps1` exits 0, `ISCC.exe` output and artifact SHA-256 hashes for `RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip` and `Recto-Portable-ARM64.zip` quoted in findings, and the asset/manifest check exits 0.

## 3. Automated Full Install, Portable Install, association and upgrade proof

Prove both installation modes end to end via an automated verification script (`tests/scripts/verify_installer.ps1`): silent **Full Install** (`RectoSetup-x64.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /DIR="..."`), verification of `.pdf` `ProgId` and file-type icon registration in `HKCU\Software\Classes`, first launch and command-line PDF open, in-place upgrade from version `N` to `N+1` preserving settings and recents, clean silent uninstall (`unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`) removing program files and registry associations while leaving user documents untouched, and silent **Self-Contained Portable Install** (`RectoSetup-x64.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /PORTABLE=1 /DIR="..."`) proving `data\portable.mode` is created, `Recto.exe` writes `settings.json` and `recents.json` inside `<portable-dir>\data\`, and zero registry keys or `%LOCALAPPDATA%\Recto` files are created.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 3.2 (file-type icon when default) and 9.7 (Portable mode and Default PDF app entries), with `resources/preview.png` as the asset reference.
**Job:** A new user can install Recto either as a standard desktop PDF app or as a zero-footprint portable folder, upgrade without losing settings, and uninstall cleanly. Consumer: Windows Explorer, registry file associations, and portable USB/folder deployments.
**Treatment:** Automated end-to-end execution of the real `RectoSetup-x64.exe` binary in both Full and Portable modes plus upgrade and uninstall verification. Cheaper substitute that fails the checkpoint: testing only from the `build/bin/` output folder without running the compiled Inno Setup installer and uninstaller.
**Chrome:** consume the generated icons and assets from `resources/` unchanged.

**Build order:**

1. Author and run `tests/scripts/verify_installer.ps1` testing Full Install (`v1.0.0`), registry `.pdf` ProgId and icon verification, app launch and PDF open, upgrade install (`v1.0.1`) verifying `settings.json` and `recents.json` continuity, and silent `unins000.exe` clean removal. Done when: every assertion in the Full Install, upgrade and uninstall sequence passes and logs its output.
2. Extend `tests/scripts/verify_installer.ps1` to test Self-Contained Portable Install (`/PORTABLE=1`) and standalone `Recto-Portable-x64.zip` extraction: launch `Recto.exe`, change a setting, open a PDF, verify `<portable-dir>\data\settings.json` and `<portable-dir>\data\recents.json` exist, and assert zero `HKCU\Software\Classes\Recto.PDF` keys and zero writes to an isolated `%LOCALAPPDATA%\Recto`. Done when: the portable isolation check passes 100%.

- [ ] Full Install, `.pdf` file-association, upgrade continuity and clean-uninstall proof. Done when: `verify_installer.ps1` proves `.pdf` icon registration, settings/recents survival across `v1.0.0 -> v1.0.1`, and clean uninstall removal. Cheaper substitute that fails the checkpoint: verifying file copies without checking registry cleanup on uninstall.
- [ ] Self-Contained Portable Install and in-app Portable mode proof. Done when: `verify_installer.ps1` proves `<portable-dir>\data\` holds all state and zero registry or `%LOCALAPPDATA%` artifacts are left on the host.
- [ ] Commit: `release: automated Full Install, Portable Install, upgrade and uninstall proof`

**Test checkpoint:** `powershell -ExecutionPolicy Bypass -File tests/scripts/verify_installer.ps1` exits 0 with its full step-by-step verification output quoted in findings.

## Verification

- M6's packaging (Inno Setup Full & Portable + Portable `.zip` + optional MSIX), `.pdf` association, portable isolation, upgrade and printing clauses each carry dated, machine-named evidence.
- The installer and portable archives built by `build/build-installer.ps1` are the exact binaries tested by `verify_installer.ps1` (artifact SHA-256 hashes recorded).
- `python scripts/todo-graph.py validate` passes.
