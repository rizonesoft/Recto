# What Recto does and does not do

Recto is a free, open-source PDF viewer, OCR tool and document converter for Windows. This page answers the scope questions people ask most, so you can tell what is planned, what was decided, and what Recto will not do. Every recurring request and its answer is in the [user demand ledger](user-demand.md).

Recto is in early development (milestone M0): the features below are the plan for version 1, not shipped behaviour.

## Which Windows versions and processors does Recto support?

Windows 11 version 22H2 or later (build 22621 or newer), on x64 and ARM64 PCs (foundation plan decision D15). Windows 10 is not supported: Recto uses Windows 11 features such as Mica, Segoe UI Variable and the built-in Windows OCR engine. There is no macOS, Linux, web or mobile version, and none is planned.

## What does Recto never do?

- **Run PDF JavaScript.** Recto's PDF engine has no JavaScript interpreter by construction (D9). Launch actions and scripts in a PDF never run; links and attachments ask before they open.
- **Contact the network without your consent.** Nothing is sent by default. Network features are opt-in, each one names what it sends: the update check (off by default) and the cloud storage connectors for your own OneDrive, SharePoint, Google Drive, Dropbox or Box. The AI assistant, planned for after version 1 (D19), works the same way: off until you add your own key, with a preview of every request.
- **Collect telemetry.** Recto has no usage analytics and no crash upload (D16). Diagnostics stay on your PC; you choose whether to share them in a bug report.
- **Run a hosted service.** Recto has no accounts, no Recto cloud, no share links and no hosted e-signature service (D20, withdrawn 2026-10-21).
- **Need Microsoft Office, LibreOffice or another converter.** Office, HTML, Markdown and image conversions are built into Recto (D7, D10).
- **Run macros.** Macros in Office files are stripped and never run.

## How does Recto handle XFA and dynamic forms?

Recto converts an XFA form's template and data into standard PDF form fields, so the form can be viewed, filled and flattened without running its scripts (D03 T03 §7). A banner says the form was converted. Behaviour that depends on the form's JavaScript (calculations, field logic, dynamic layout changes) does not run, because Recto never runs PDF JavaScript.

## Where do I ask a question or report a problem?

- **A bug:** open a [bug report](https://github.com/rizonesoft/Recto/issues/new?template=bug_report.yml). It asks for the Recto version, install type, Windows build and the file or a reproducer.
- **A feature:** check the [user demand ledger](user-demand.md) and the [open issues](https://github.com/rizonesoft/Recto/issues?q=is%3Aissue+is%3Aopen) first, then open a [feature request](https://github.com/rizonesoft/Recto/issues/new?template=feature_request.yml).
- **A security problem:** report it privately, as [SECURITY.md](../SECURITY.md) explains. Never in a public issue.
