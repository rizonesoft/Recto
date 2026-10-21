# Issue triage

How Recto's maintainers handle a new issue, from the first reply to closing it. The issue forms in `.github/ISSUE_TEMPLATE/` collect what we need; this page says what we do with it. Security reports never come through issues: `SECURITY.md` routes them to private reporting.

## Labels

Every open issue carries one primary kind label (`bug`, `feature` or `security`), plus `regression`, `data-loss` or `crash` on a bug when they apply, one area label once the area is known, and a state label when one applies. The labels live in `.github/labels.yml`.

| Label | Group | Use it when |
| ----- | ----- | ----------- |
| `engine` | Area | The problem is in parsing, rendering or saving a PDF. |
| `viewer` | Area | The app shell, page view, navigation or settings. |
| `text` | Area | Selection, search, OCR, annotations or forms. |
| `convert` | Area | Converting to or from Office, HTML, images or other formats. |
| `organize` | Area | Merging, splitting, reordering, rotating or other page operations. |
| `print` | Area | Printing and print production. |
| `install` | Area | The installer, portable mode, MSIX or updates. |
| `bug` | Kind | Recto does not behave as documented or expected. |
| `feature` | Kind | A request for something Recto does not do yet. |
| `regression` | Kind | It worked in an earlier release (added with `bug`). |
| `data-loss` | Kind | A file or user data was lost or damaged (added with `bug`). |
| `crash` | Kind | Recto closed or stopped responding (added with `bug`). |
| `security` | Kind | A security fix, labelled only once its advisory is drafted. |
| `needs-repro` | State | We cannot reproduce it yet and have asked for a file or steps. |
| `fixed-on-main` | State | The fix is merged; the issue stays open until it is released. |
| `released` | State | The release containing the fix is published. |

## Response targets

- A new bug or feature request gets a first human reply within 5 working days.
- A `data-loss`, `crash` or `regression` bug gets a first reply within 2 working days.
- A security report gets a first reply within 3 working days, as `SECURITY.md` promises.
- An issue in `needs-repro` with no reply from the reporter for 30 days gets one reminder; it is closed only by a person, with the reason, after another 30 days.

## A person closes every issue

No bot closes, locks or splits an issue. A maintainer closes it, says why in a comment, and the reporter can reply to reopen it. Automation may add labels or remind; it never decides.

## Duplicates link the canonical issue

Closing a duplicate links the canonical issue and states where it stands (open, `fixed-on-main` and waiting for a release, or released in a named version). Add the duplicate's details to the canonical issue when they add something new.

## Feature requests are never closed as support

A feature request is answered on its merits: it is planned (linked to the TODO section that owns it), decided, or not planned with the reason. It is never closed as a "support question" or "usage question". Record the answer in `docs/user-demand.md` so the next person finds it.

## Asking for a reproducer

When we cannot reproduce a bug, label it `needs-repro` and ask for one of these, in this order:

1. The original file, if the reporter can share it.
2. A reproducer made with `recto-cli scrub`, which keeps the file's structure and replaces its text, images and metadata (the command arrives with D07 T03 §3). The reporter checks the preview pages before sharing.
3. A synthetic file, following the guide below.

### Making a synthetic file

Recreate the problem in a file that holds nothing private: use the same producing app and the same steps (for example, the same Word template exported to PDF, or the same scanner settings), filled with placeholder text. Note the producing app and its version in the issue. If the synthetic file does not show the problem, say so: that tells us the trigger is in the content, and `recto-cli scrub` is the next step.

## One owner per issue

Each issue being worked on has exactly one assignee. Reassign rather than add a second assignee. An unassigned issue is not being worked on.

## Superseded pull requests

When a pull request replaces another, close the older one with a comment linking its replacement. Never leave two open pull requests for the same fix.

## Closing a fixed bug

A user-reported bug closes only when the release containing its fix is published. Until then it carries `fixed-on-main`. When the release ships, label it `released`, comment "Fixed in vX.Y.Z" with the release link, and close it.
