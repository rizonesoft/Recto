---
schema_version: 1
id: attachment-pin
domain: 03-text
status: active
title: "TODO-06 -- Attachment Pin Markup Tool"
---

# TODO-06 -- Attachment Pin Markup Tool

> **Goal:** The `Attachment Pin` tool that SPEC 8.2 lists on the Markup & Annotation Bar works end to end: a reviewer places a pin, picks a file, the file is embedded in a copy-safe way, and a reader opens or extracts it only after being asked.

> **Current state:** Measured 2026-10-23. `D03 T03 §1` parses and writes `FileAttachment` annotations and `D02 T03 §8` lists embedded files and sanitizes their names, but no section creates a pin or puts an `Attachment Pin` button on the bar. `src/pdf` has no `annotations.*` or `portfolio.*` yet and `src/ui` is not built. `D03 T03` is at the 55-section cap, so this tool lives in its own file.

## Inputs

- `design/SPEC.md` sections 6 (`RectoIcons` `attach`), 8.2 (`Attachment Pin` on the Markup & Annotation Bar), 8.3 (Attachments), 11.
- `AGENTS.md` architecture rules: document links and portfolio attachments ask before opening, and every file write is temp-file-then-replace.
- `D02 T03 §8` (embedded file storage, filename sanitization, ask-before-open dialog) and `D06 T04 §10` (the `D-Markup.png` board) are consumed as built; neither is edited here.
- -> XREF: D03 T03 §3 -- the `Attachment Pin` button lives on the bar built there, and its undo stack and selection frame come from `D03 T03 §4`.

## Outcome

- A `FileAttachment` pin can be created from the bar, from `Ctrl+K` and from `recto-cli annot attach`, survives save and reopen, and shows in the Comments panel and the Attachments panel.
- Opening a pin asks first and refuses executable types; extraction writes a sanitized name.

**Adjacency:** list=not-applicable (pins appear in the lists of D03 T03 §5 and D02 T03 §8); document=applicable @ D03 T06 §1; settings=not-applicable (no setting; the size limit is a recorded constant); reporting=not-applicable (no report); notifications=not-applicable (failures use the dialog of the tool itself); permissions=applicable @ D03 T06 §1; audit=not-applicable (no audit log); exchange=applicable @ D03 T06 §1; reverse=applicable @ D03 T06 §1

**Adjacency rationale:** Document is the saved pin and embedded stream, permissions is the ask-before-open and executable refusal, exchange is `recto-cli annot attach` and `Save as...`, and reverse is undo of a placed pin.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `Attachment Pin` tool: embed a file, place the pin, open or extract it safely | D03 T03 §2, D03 T03 §3, D03 T03 §4, D02 T03 §8, D06 T04 §10, D10 T02 §2, D10 T02 §17 | [ ] |

## 1. `Attachment Pin` tool: embed a file, place the pin, and open or extract it safely

> **Groomed 2026-10-23:** new, filed from the SPEC 8.2 gap scan. SPEC 8.2 lists the `Attachment Pin` tool on the Markup & Annotation Bar; `D03 T03 §1` parses `FileAttachment` annotations and `D02 T03 §8` lists them in the Attachments panel, but nothing created one. Default taken: embedded files larger than `64 MiB` are refused with a plain message (reversal: raise the constant in `attachment_pin.cpp`).

Add the tool that creates a `FileAttachment` annotation: the user chooses `Attachment Pin` on the bar, clicks a page position, picks a file, and a pin icon appears whose `/FS` filespec embeds the file bytes. Opening a pin asks first, extraction sanitizes the name, and nothing is ever launched silently. The embedded-file storage, filename sanitization and open prompt are the ones `D02 T03 §8` builds; this section only adds creation and the canvas behaviour.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** `design/SPEC.md` sections 8.2 (`Attachment Pin` on the `Markup & Annotation Bar`), 8.3 (Attachments) and 11. Design board: `design/SPEC.md` section 8.2.1 with `design/screenshots/D-Markup.png` (drawn by `D06 T04 §10`).
**Job:** A reviewer drops a spreadsheet or photo onto a page as a pin, and a colleague clicks the pin, confirms, and opens or saves the file. Consumer: `MarkupAnnotationBar` (`D03 T03 §3`), the Comments panel (`D03 T03 §5`), the Attachments panel (`D02 T03 §8`) and any PDF reader.
**Treatment:** A bar tool that embeds the chosen file through the `D02 T03 §8` writer, synthesizes the pin `/AP` through `D03 T03 §2`, and routes opening through the shared ask-before-open dialog. Cheaper substitute that fails the checkpoint: an `Add File` button in the Attachments panel only, or a pin that launches the file on click.
**Chrome:** consume the `D03 T03 §3` bar buttons with the `RectoIcons` `attach` glyph, `recto_tokens.g.h` tokens, the `D03 T03 §4` selection frame and `ui::Dialog` chrome (`D10 T02 §17`).

**Build order:**

1. Implement `create_file_attachment(doc, page, point, path, icon)` in `src/pdf/attachment_pin.cpp` (embedded stream, `/Filespec` with `/UF`, `/Desc`, pin `/AP`). Done when: `PdfAttachmentPin.Create` round-trips a pin and the embedded bytes are byte-identical.
2. Add the bar button, placement, open and extract behaviour. Done when: `AttachmentPinUi.PlaceOpenExtract` passes.

- [ ] Implement `create_file_attachment` with icon names `PushPin`, `Graph`, `Paperclip` and `Tag`, embedding the file as a `FlateDecode` `/EmbeddedFile` stream with `/Params` size and modification date, refusing a file over `64 MiB` or unreadable with a typed error. Done when: `PdfAttachmentPin.Create` and `PdfAttachmentPin.TooLarge` pass under ASan. Cheaper substitute: storing only the path.
- [ ] Sanitize the stored `/UF` name with the `D02 T03 §8` rules (separators, `..`, reserved device names, trailing dots). Done when: `PdfAttachmentPin.HostileName` stores a safe name for `..\..\evil.exe`.
- [ ] Add the `Attachment Pin` button to `MarkupAnnotationBar` (`attach` glyph, automation name, tooltip) and the `annot.attachmentPin` command to `CommandRegistry` so `Ctrl+K` reaches it. Done when: `Keyboard.Audit` finds the command and `A11y.Inventory` finds the button name and tooltip.
- [ ] Place the pin on click and open `IFileOpenDialog`; `Cancel` creates nothing and leaves the document clean. Done when: `AttachmentPinUi.CancelCreatesNothing` finds no new annotation and no dirty flag.
- [ ] Make the pin undoable through the `D03 T03 §4` undo stack, selectable with the `D03 T03 §4` frame, and listed in the Comments panel with name and size. Done when: `AttachmentPinUi.Undo` removes the pin and its embedded stream together.
- [ ] Open a pin on double-click through the shared ask-before-open dialog of `D02 T03 §8` (never a silent launch; executable types refuse) and offer `Save as...` through `IFileSaveDialog`. Done when: `AttachmentPinUi.OpenAsks` shows the dialog before any launch and an `.exe` pin refuses to open.
- [ ] Add `recto-cli annot attach <file.pdf> --page <n> --at <x,y> --file <path> [--icon <name>] --out <out.pdf>` in `src/cli/cmd_annot.cpp`. Done when: `Cli.AnnotAttach` embeds a file and `recto-cli annot list` shows the pin.
- [ ] Commit: `app: add the Attachment Pin markup tool`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfAttachmentPin.*:AttachmentPinUi.*:Cli.AnnotAttach` passes, and making the pin launch the file on click makes `AttachmentPinUi.OpenAsks` fail.

## Verification

- A pin placed from the bar, saved and reopened still embeds the original bytes (`PdfAttachmentPin.Create`), opening it asks first (`AttachmentPinUi.OpenAsks`) and an executable pin refuses to open.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
