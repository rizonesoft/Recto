---
schema_version: 1
id: office-to-pdf
domain: 04-convert
status: active
title: "TODO-04 -- Office to PDF (COM and LibreOffice)"
track: M5
---

# TODO-04 -- Office to PDF (COM and LibreOffice)

> **Goal:** Word, Excel and PowerPoint documents (and their ODF and RTF kin) become PDFs through the converter the machine already has: Microsoft Office by COM with macros forcibly disabled, else LibreOffice headless in an isolated profile, else the SPEC dialog telling the user exactly what to install. Recto never bundles either (D10).

> **Current state:** Measured 2026-10-13. No code exists. The whole design is plan section 5.1: ProgID detection, one STA office-bridge thread (plan 4.5), `AutomationSecurity = msoAutomationSecurityForceDisable` (3) before opening anything, read-only open, `ExportAsFixedFormat`, close without saving, quit only an instance Recto started; the LibreOffice path with `-env:UserInstallation` pointed at a Recto-owned temp profile and a 120-second kill timeout; inputs `.doc .docx .rtf .odt .xls .xlsx .ods .ppt .pptx .odp`. The Settings converter selector (Automatic, Microsoft Office, LibreOffice, showing what was found) is D02 T04 §2's row; detection depth lives here per that file's deferral.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D10), 4.5 (office bridge thread), 5.1 (the whole design), 7 (M5: tested with Office present, LibreOffice only, and neither), 9 (macros disabled).
- `design/SPEC.md` sections 9.5 (PDF from Office card), 9.7 (converter selector), 9.9 (converter-missing dialog).

## Outcome

- On an Office machine, the supported documents convert through Office with macros provably unable to run.
- On a LibreOffice-only machine, they convert headless without disturbing a LibreOffice the user has open.
- With neither, the user gets the SPEC dialog and no job pretends to run; the produced PDFs open in Recto as ordinary documents.

**Adjacency:** all=not-applicable (converters inside the D04 T01 frame; queue, notices, save settings and cancel are owned there, and detection results surface in the Settings row owned by D02 T04 §2)

**Adjacency rationale:** The only new surface is the missing-converter dialog in section 4, which is a state of the SPEC dialog family rather than a record or list. Converter selection is a setting whose value, label and consumer are split deliberately: D02 T04 §2 renders it, section 1 here computes what it shows, sections 2 and 3 obey it.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Converter detection and the selection rule | D04 T01 §1 | [ ] |
| 2 | §2 | Microsoft Office COM path | §1 | [ ] |
| 3 | §3 | LibreOffice headless path | §1 | [ ] |
| 4 | §4 | Missing-converter dialog and three-machine proof | §2, §3 | [ ] |

## 1. Converter detection and the selection rule

Detection is one service answering three questions for Settings and for job routing: is Office present (which of `Word.Application`, `Excel.Application`, `PowerPoint.Application` resolve), is LibreOffice present (registry install path to a runnable `soffice`), and what does the user's selector (Automatic, Office, LibreOffice) therefore choose per document family. Detection must be cheap, cached per app run, and honest under test doubles so §4's neither-machine case is provable without uninstalling anything.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/services/office_detect.cpp`: ProgID resolution without launching the applications, LibreOffice path discovery, per-family capability table, and the selector's resolution to a concrete path or none. Done when: unit tests over injected registry and ProgID fakes cover Office-only, LibreOffice-only, both, neither, and partial-Office (Word without Excel) cases.
2. Feed D02 T04 §2's selector row ("showing what was found") from this service. Done when: this machine's Settings row names the actual installed converter versions or their absence truthfully.

- [ ] Detection service with the five-case test matrix. Done when: no detection path starts an Office process (process list asserted in test). Cheaper substitute: checking file associations, which say nothing about automation.
- [ ] Settings row driven by real detection. Done when: forcing LibreOffice on an Office machine routes jobs there (proven in §3).
- [ ] Commit: `convert: Office and LibreOffice detection with the selection rule`

**Test checkpoint:** `recto-tests --gtest_filter=OfficeDetect.*` passes with the five cases (quote the matrix), and the Settings row on this machine reads truthfully (screenshot in §4's findings).

## 2. Microsoft Office COM path

The plan 5.1 sequence on one dedicated STA thread (the office bridge, plan 4.5): create or reuse per the rule (quit only what Recto started; never hijack the user's running instance's documents), set `AutomationSecurity` to force-disable before any open, open read-only with converters and macros inert, `ExportAsFixedFormat` to the job's temp destination, close without saving. Password-protected and corrupt documents fail as typed job results (D04 T01 §1), never as modal Office dialogs: alerts are suppressed for the job's documents and every failure surfaces as data.

**Phase:** 5
**Needs:** windows-host, office
**Surface:** no

**Build order:**

1. Implement `src/services/office_com.cpp` for Word, Excel and PowerPoint export behind the job contract, with per-app format constants isolated in one table. Done when: corpus `.docx`, `.xlsx` and `.pptx` files (generated per corpus rules) convert to PDFs that `recto-cli info` describes correctly.
2. Macro-safety proof: a generated macro-bearing document whose macro would write a marker file converts with the marker absent, and `AutomationSecurity`'s set-before-open ordering is asserted by construction (the open call is unreachable before the set). Done when: the test is in the suite and the marker's absence is the assertion.
3. Instance hygiene: Recto-started instances quit at job end (process count returns to baseline); a user-running instance is never quit and its open documents are untouched. Done when: both cases are demonstrated in the driven test with process listings.

- [ ] COM export for the three families on the STA bridge thread. Done when: cancelling the job closes its document and leaves no output and no orphan process. Cheaper substitute: automating on the UI thread, which freezes the app for the conversion's duration.
- [ ] Macro marker test proving force-disable. Done when: the marker file demonstrably does not exist after conversion.
- [ ] Commit: `convert: Office to PDF over COM with macros forced off`

**Test checkpoint:** `recto-tests --gtest_filter=OfficeCom.*` on an Office host quotes the three-family conversions plus macro-marker absence, ASan not applicable to the COM boundary itself (recorded as such).

## 3. LibreOffice headless path

`soffice --headless --convert-to pdf` into the job's temp folder with a Recto-owned `-env:UserInstallation` profile so a running user instance is never disturbed, macro security set in that profile to never execute, a 120-second default timeout that kills the process tree, and stdout captured into the job's error details on failure. Parallel jobs get distinct profiles and outdirs (the two-at-once bound from D04 T01 §1 applies).

**Phase:** 5
**Needs:** windows-host, libreoffice
**Surface:** no

**Build order:**

1. Implement `src/services/libreoffice.cpp`: profile seeding with macro security hard-off, process launch with timeout and tree-kill, output discovery (the converter names outputs itself; map them back to the job's chosen names through the atomic helper, D04 T02 §1). Done when: the §2 corpus set converts through LibreOffice with a user LibreOffice window open alongside, untouched.
2. Timeout and failure honesty: a pathological document (corpus-generated) exceeding the timeout dies as a named job failure with captured output. Done when: no `soffice` process survives the timeout in the test's process listing.

- [ ] Headless conversion in an isolated profile. Done when: two concurrent jobs convert without profile collision. Cheaper substitute: using the user's default profile, which collides with their open LibreOffice (the exact failure plan 5.1 designs out).
- [ ] Timeout with process-tree kill. Done when: the pathological case fails in bounded time with its captured output attached.
- [ ] Commit: `convert: Office documents to PDF via headless LibreOffice`

**Test checkpoint:** `recto-tests --gtest_filter=LibreOffice.*` quotes corpus conversions (info-verified), concurrent-pair run clean, timeout case bounded with no surviving processes.

## 4. Missing-converter dialog and three-machine proof

SPEC 9.9's dialog when the resolution is none: what is needed and why, Get LibreOffice (free), Get Microsoft 365, Not now; the Convert card for Office files is disabled with its SPEC reason before that point per input set, and the dialog is the backstop when a job still lands there (converter removed between detection and run, covered by re-resolving at job start). M5's done-when is the triptych: Office present, LibreOffice only, neither, each proven; the neither case uses §1's test doubles at integration level, recorded as such.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.5 and 9.9 with `design/screenshots/D-Dialogs.png` (converter-missing dialog) and `D-Convert.png`.
**Job:** The user is never stuck wondering why an Office file will not convert, and always has the next step. Consumer: the buttons open the vendors' pages in the browser; after installing, detection (§1) picks the converter up without reinstalling Recto.
**Treatment:** One dialog in the shared dialog family, reached from card enablement and from the job's `NeedsConverter` result alike. Cheaper substitute that fails the checkpoint: a generic "conversion failed" with no path forward.
**Chrome:** consume the D02 T01 §4 dialog style and the §1 detection service. Do not invent a second detection or dialog.

**Build order:**

1. The dialog plus card enablement for Office inputs driven by §1, and job-start re-resolution. Done when: with detection forced to none, the card disables with its reason and a forced job lands in the dialog, both captured.
2. The triptych run: Office host (§2), LibreOffice host (§3), neither (integration test doubles) each converting or refusing the same document, recorded as M5 evidence in the findings. Done when: all three outcomes are quoted side by side.

- [ ] Missing-converter dialog in both arrival paths. Done when: installing LibreOffice after seeing it makes the card enable on next detection refresh. Cheaper substitute that fails the checkpoint: caching "none" for the app's lifetime.
- [ ] Three-machine proof recorded. Done when: the M5 row of plan section 7 can be quoted complete for Office to PDF.
- [ ] Commit: `convert: missing-converter dialog and the three-machine proof`

**Test checkpoint:** dialog screenshots against `D-Dialogs.png` and the triptych outcomes quoted in findings.

## Verification

- M5 done-when complete with D04 T03: both converters proven, neither-machine honest.
- Macros provably inert on both paths (COM marker test, LibreOffice hard-off profile).
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes.
