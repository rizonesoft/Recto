---
schema_version: 1
id: packaging-and-print
domain: 07-release
status: active
title: "TODO-01 -- Printing, MSIX Packaging and Install"
track: M6
---

# TODO-01 -- Printing, MSIX Packaging and Install

> **Goal:** Recto becomes an installable product: printing through the system print dialog on the API this file decides first, an MSIX package carrying the generated assets and the `.pdf` association, and a clean-machine install proven end to end including becoming the default PDF app.

> **Current state:** Measured 2026-10-13. No code exists. D02 T01 §1 will have authored the manifest and assets; D02 T02 provides page rendering the print path reuses at print resolution. Two decisions are deliberately open until this file: the print API (plan section 7's note: `PrintManagerInterop` with the window handle versus Direct2D `ID2D1PrintControl`, to be confirmed in M6 before designing the print UI, also plan section 11), and final package identity values, which Partner Center assigns (D99 T01 §3). SPEC section 10 gives Ctrl+P the system print dialog; section 9.7's Default PDF app row opens the Windows settings page.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D6 MSIX through the Store), 2 (packaging row), 7 (M6 and the printing note), 11 (print API open item).
- `resources/README.md` (asset-to-manifest mapping, the association's logo rules, the splash background) and `design/SPEC.md` sections 3.2 (file-type icon behavior), 9.7, 10.
- -> XREF: D02 T01 §1 -- the manifest and assets authored there are what §2 here packages; manifest defects found at packaging time correct there.

## Outcome

- Ctrl+P prints the current document through the Windows print dialog with page range and copies honored, rendered at printer resolution through the engine.
- The signed MSIX installs on a clean Windows 11 machine, shows Recto's tiles and splash, and offers the `.pdf` association with Recto's file-type icon when chosen as default.
- Uninstall leaves nothing behind but the user's own files and settings data the platform owns.

**Adjacency:** list=not-applicable (installed apps are listed by Windows, not Recto); document=applicable @ D07 T01 §1; settings=not-applicable (print choices live in the system dialog; the Default PDF app row is D02 T04 §2); reporting=not-applicable (Store health reporting is Partner Center, D07 T02 §4); notifications=not-applicable (update notices are the Store's); permissions=not-applicable (package capabilities are declared in the manifest, D02 T01 §1, and audited in section 2); audit=not-applicable (release provenance is D07 T02 §2 and §4); exchange=not-applicable (no data import); reverse=applicable @ D07 T01 §3

**Adjacency rationale:** Print is the viewer's document surface delivered late because its API choice was a named M6 open item. The reverse of install is a clean uninstall, verified in section 3 rather than assumed from MSIX defaults. Package capabilities get an audit row inside section 2 (least capability for a document app) without pretending to be a permissions system.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Print API decision and printing | D02 T02 §3 | [ ] |
| 2 | §2 | MSIX package, assets and association | §1, D02 T01 §1 | [ ] |
| 3 | §3 | Clean-machine install and default-app proof | §2 | [ ] |

## 1. Print API decision and printing

First the decision, recorded with its evidence: spike both candidate paths on Windows App SDK 2.4.0 in the packaged app (the print contract through `PrintManagerInterop` against the window handle, and `ID2D1PrintControl` over the existing D2D device), judging on preview correctness, page-range and copies support, and how page rendering at printer DPI composes (vector through display lists where the path allows, raster otherwise at stated DPI). Then build printing on the winner: system dialog, current document, range and copies honored, rendering through the engine off the UI thread, cancellable.

**Phase:** 6
**Needs:** windows-host, display-session, attended
**Surface:** yes

**Fidelity:** SPEC section 10 (Ctrl+P, system print dialog); print output fidelity is to the page itself, checked against `recto-cli` reference renders.
**Job:** The user can print a document or a range of it, as it looks. Consumer: the printer (and Print to PDF round-trip used as the test oracle).
**Treatment:** The decided API after a recorded spike, engine-rendered pages. Cheaper substitute that fails the checkpoint: printing the screen-resolution tiles, or deciding the API by documentation alone when the plan demands confirmation.
**Chrome:** consume the print dialog the platform provides and the command registry's Print entry (toolbar, Ctrl+P, command box). Do not invent a custom print preview.

**Build order:**

1. Spike both APIs in the packaged app and record the decision (with reversal cost) as a dated correction to plan section 11's open item. Done when: each spike prints the test page or names its concrete failure, and the decision names the loser accurately.
2. Implement printing on the chosen path: page range parsing shared with D05 T01 §1's parser, printer-DPI rendering through a job-shaped engine path (not the viewer's actor), preview pages generated on demand. Done when: Print to PDF of a corpus file renders page-equal to the source at matched pages.

- [ ] Recorded print API decision from working spikes. Done when: plan section 11's row is answered in the plan and here identically. Cheaper substitute that fails the checkpoint: picking the familiar API untested in a packaged WinUI context.
- [ ] Printing with range, copies and cancellation. Done when: a physical or Print-to-PDF run of a middle range yields exactly those pages.
- [ ] Commit: `release: printing via the decided API with engine-rendered pages`

**Test checkpoint:** `recto-cli render` comparisons of the Print-to-PDF outputs against sources (quoted pages), the spike evidence and decision in findings, attended physical-print smoke recorded by the reviewer.

## 2. MSIX package, assets and association

The package the Store ships: MSIX built from the app project for x64 and ARM64 (bundle or per-architecture packages as Partner Center wants), identity from the reservation (D99 T01 §3) with development identity clearly separated, capabilities audited to the minimum a document app needs, generated assets verified present at every scale the manifest references, the `.pdf` association with its file-type logos, and version numbering tied to release tags (each tag carrying the MuPDF submodule commit, plan section 10).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Packaging configuration in the app project and CI (D00 T01 §4's workflow gains the package step and artifact): store-ready MSIX per architecture, signed for sideload testing with the development certificate, Store signing left to submission (D07 T02 §4). Done when: CI produces installable packages for both architectures from a tag.
2. Capability and asset audit: manifest capabilities justified one by one in the section's findings; every logo reference resolves to a generated file at the right size (scripted check against `resources/msix/Assets/`). Done when: the audit table is empty of unjustified capabilities and missing assets.

- [ ] CI-built MSIX packages for x64 and ARM64. Done when: both install by double-click (development signing) on their hosts. Cheaper substitute: a loose-files zip "for now" that skips every packaged-behavior difference.
- [ ] Capability and asset audit recorded. Done when: `runFullTrust` and any broad capability is either absent or justified in one sentence each.
- [ ] Commit: `release: MSIX packaging for x64 and ARM64 with audited manifest`

**Test checkpoint:** the asset check over `src/app/Package.appxmanifest` and `src/app/Assets/` quoted in findings (capabilities table, asset check output), both architectures installing from CI artifacts.

## 3. Clean-machine install and default-app proof

The install story on a machine that has never seen Recto (a clean VM or reset host recorded by name): install, first launch to Home, open a PDF by double-click after choosing Recto as default through the SPEC 9.7 entry, file-type icon present in Explorer, print once, uninstall clean. Update behavior is the Store's; what this section proves is that a newer package version installs over the old keeping settings and recents (app data continuity through the platform's rules).

**Phase:** 6
**Needs:** windows-host, display-session, attended
**Surface:** yes

**Fidelity:** SPEC sections 3.2 (file-type icon when default) and 9.7 (Default PDF app entry), with `resources/preview.png` as the asset reference.
**Job:** A new user can install Recto and make it their PDF app without help. Consumer: Explorer, the Start menu and the user's double-click habit.
**Treatment:** The platform's own install and defaults flows, entered from Recto's Settings row. Cheaper substitute that fails the checkpoint: testing only on the dev machine where development installs hide packaged differences.
**Chrome:** consume the generated assets and manifest from §2 unchanged. Do not invent a custom installer or first-run wizard the SPEC does not describe.

**Build order:**

1. Clean-machine run recorded step by step (machine, Windows build, package version): install, first launch, set default via Settings entry, double-click open, icon check at two Explorer sizes, print, uninstall. Done when: every step's outcome is in the findings with screenshots.
2. Upgrade run: install version N, create settings and recents, install N+1, verify continuity and first open. Done when: recents and a changed setting survive, quoted before and after.

- [ ] Clean install proof with default-app and icon verification. Done when: Explorer shows the Recto file-type icon for PDFs at 16 and 256 px sizes. Cheaper substitute that fails the checkpoint: verifying the icon only inside Recto.
- [ ] Upgrade continuity proof. Done when: no setting or recent is lost across the version bump.
- [ ] Commit: `release: clean-machine install, default-app and upgrade proof`

**Test checkpoint:** `Get-AppxPackage` on the clean machine reports the installed version, the recorded sequence in findings (screenshots per step), upgrade before-and-after state quoted.

## Verification

- M6's packaging, association and printing clauses each carry dated, machine-named evidence.
- The packages CI builds from a tag are the packages tested here (same artifact hashes recorded).
- `python scripts/todo-graph.py validate` passes.
