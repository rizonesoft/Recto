# 07 Release

> **Phase 6 (M6 Ship)**

From a working app to a shippable desktop and portable product: printing on the decided API, Inno Setup installer (`Full Install` and `Self-Contained Portable`) plus standalone Portable `.zip` and optional MSIX packaging, automated install/portable/upgrade proof, the About page and full GPL-3.0-or-later compliance, local crash report bundles, and the release bundle + optional Store submission kit.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-packaging-and-print.md) | Printing, Inno Setup (Full & Portable) Installer, Optional MSIX and Install Proof | active |
| [TODO-02](./TODO-02-store-and-compliance.md) | About, GPL-3.0-or-later Compliance, Local Crash Bundle and Release Distribution | active |
| [TODO-03](./TODO-03-listening-to-users.md) | Listening to Users: Intake, Triage, Diagnostics, Releases and Demand | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- Print path decision and implementation, Inno Setup installer (`Full` + `Self-Contained Portable`) and Portable `.zip` for x64 and ARM64, optional MSIX packaging, automated install, portable and upgrade proof, upgrade and uninstall edge cases with a zero-residue diff, and versioned settings and recents migration
- About, NOTICE and license audit, opt-in local minidump-plus-log crash bundle, localization readiness, GitHub Release bundle and optional Store submission runbook
- Versioning: SemVer 2.0.0 derived from annotated git tags with `VERSION` naming the next release, the release version check, `recto::core::SemVer`, `VERSIONINFO` in every binary, versioned installer and archive names, the downgrade warning, writer versions in stored files, and the generated changelog and next-version suggestion
- Listening to users: issue forms, `SECURITY.md`, triage policy, in-app `Report a problem`, reproducer scrubber, closing on release, release gate, scope page and demand ledger

## Out of scope

- The operator's account, name and reservation steps (99-manual)
- Post-v1 sandbox implementation (designed in 06 T02 §5, built after v1)

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
