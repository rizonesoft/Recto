# Security policy

Recto opens files that may come from anyone, so it treats every PDF, Office and HTML file as hostile. If you find a way a file can make Recto run code, read or write outside what you asked, leak data, or crash in a way an attacker controls, please tell us privately.

## Supported versions

| Version | Security fixes |
| ------- | -------------- |
| Latest release | Yes |
| Older releases | No; update to the latest release |
| `main` before v1.0.0 | Yes; there is no release yet |

Fixes ship as a new release. Recto does not patch older release lines.

## How to report a vulnerability

Use GitHub private vulnerability reporting: open the repository's **Security** tab and choose **Report a vulnerability**, or go straight to <https://github.com/rizonesoft/Recto/security/advisories/new>. Only the maintainers see the report.

Do not open a public issue, pull request or discussion for a vulnerability, and do not attach an exploit file anywhere public.

A useful report includes the Recto version and install type, the file or a minimal reproducer, the steps, and what an attacker gains.

## What happens next

- **First response within 3 working days**, confirming we received the report and who is handling it.
- We reproduce the problem, agree on its severity with you, and keep you updated in the private advisory thread until it is fixed.
- **Every security release ships with a GitHub security advisory** that names the affected versions, the fixed version and the credit you choose (or none).
- We ask you to keep the details private until the fixed release and its advisory are published.
