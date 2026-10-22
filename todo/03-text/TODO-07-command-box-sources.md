---
schema_version: 1
id: command-box-sources
domain: 03-text
status: active
title: "TODO-07 -- Command Box Rows for Comments, Form Fields and Indexed Files"
---

# TODO-07 -- Command Box Rows for Comments, Form Fields and Indexed Files

> **Goal:** The `Ctrl+K` command box (SPEC 9.4) answers every row kind the SPEC names: `Go to` lists comments and form fields beside pages and outline entries, and `Files` lists indexed files beside recent files.

> **Current state:** Measured 2026-10-23. SPEC 9.4 says `Go to` covers "pages, outline entries, comments, form fields" and `Files` covers "recent and indexed files". `D02 T03 §12` builds `CommandBoxFilter` with pages and outline entries for `Go to` and `RecentFilesStore` entries for `Files`, so comments, form fields and indexed files have no source. `src/app` holds only the retired WinUI scaffold and `src/ui` is not built yet.

## Inputs

- `design/SPEC.md` section 9.4 (command box sections and rows), 10 (`Ctrl+K`) and 11.
- `D02 T03 §12` (`CommandBoxFilter`, `CommandBoxControl`) is extended through a source interface and is otherwise unchanged.
- -> XREF: D03 T03 §5 -- the Comments panel view model supplies the comment rows.
- -> XREF: D03 T03 §8 -- `FormViewModel` supplies the form-field rows.
- -> XREF: D03 T05 §4 -- the local `SearchIndex` supplies the indexed-file rows.

## Outcome

- Typing a comment's text or author, or a form field's name or tooltip, offers a `Go to` row that scrolls to and selects it.
- Typing part of an indexed file's name offers a `Files` row that opens it, after the recent files.
- With no index and no comments or fields, the rows are simply absent and nothing errors.

**Adjacency:** list=applicable @ D03 T07 §1; document=not-applicable (no document is written); settings=not-applicable (indexed rows follow the Search index setting of D03 T05 §9); reporting=not-applicable (no report); notifications=not-applicable (no announcements beyond the command box itself); permissions=not-applicable (opening goes through the normal open pipeline); audit=not-applicable (no audit log); exchange=not-applicable (nothing is imported or exported); reverse=not-applicable (the box is dismissed with Esc, which D02 T03 §12 owns)

**Adjacency rationale:** The only surface is the ranked row list of the command box; every other key is owned by the sections that build the data being searched.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `CommandBoxSource` interface and registration in `CommandBoxFilter` | D02 T03 §12 | [ ] |
| 2 | §2 | `Go to` rows for comments and form fields | §1, D03 T03 §5, D03 T03 §8 | [ ] |
| 3 | §3 | `Files` rows for indexed files | §1, D03 T05 §4, D03 T05 §9 | [ ] |

## 1. `CommandBoxSource` interface and registration in `CommandBoxFilter`

Give `CommandBoxFilter` (`D02 T03 §12`) a small extension point so a feature adds a row kind without editing the filter: a source returns ranked, labelled rows for a query and the row's action. Pages, outline entries and recent files can stay as they are; this section only adds the interface and proves it with a fake source.

**Phase:** 3
**Needs:** windows-host
**Surface:** yes
**Fidelity:** `design/SPEC.md` section 9.4 with `design/screenshots/D-Dialogs.png` (top centre): a new row kind uses the existing row layout, glyph and bold-match treatment.
**Job:** A developer adds a row kind with one registration and a user sees it ranked among the others. Consumer: `CommandBoxControl` and the sections `§2` and `§3`.
**Treatment:** `CommandBoxSource` with `query(text, context, CancelToken)` returning rows `{ section, label, match ranges, glyph, action }`, polled off the UI thread with a `5 ms` budget per source. Cheaper substitute that fails the checkpoint: hard-coding comment and field lookups inside `CommandBoxFilter`.
**Chrome:** consume the `D02 T03 §12` row style, `RectoIcons` glyphs and `recto_tokens.g.h` tokens; no new colours.

**Build order:**

1. Define `CommandBoxSource`, `CommandBoxRow` and `CommandBoxFilter::register_source` in `src/app/controls/command_box_source.h` and wire the filter to merge source rows into the matching SPEC 9.4 section. Done when: `CommandBox.SourceMerge` shows a fake source row ranked with built-in rows.
2. Add budgeting and failure isolation. Done when: `CommandBox.SlowSource` and `CommandBox.FailingSource` pass.

- [ ] Define the interface and row type with `std::vector<TextRange>` match spans in `src/app/controls/command_box_source.h`. Done when: the header compiles in `recto-tests` with a fake source.
- [ ] Merge source rows into `Go to` or `Files` by the row's declared section and rank them with the existing prefix and substring rules. Done when: `CommandBox.SourceMerge` asserts section and order.
- [ ] Run each source on a worker with a `5 ms` budget and drop rows from a source that overruns, without blocking typing. Done when: `CommandBox.SlowSource` finishes within `20 ms` of the keystroke and shows the other sources' rows. Cheaper substitute: awaiting every source on the UI thread.
- [ ] Contain a source that returns an error: log it, show no rows from it, keep the box usable. Done when: `CommandBox.FailingSource` keeps the other rows and the box open.
- [ ] Treat row labels as untrusted text: truncate to `120` characters with an ellipsis and strip control characters. Done when: `CommandBox.HostileLabel` shows a `10000`-character label containing a newline safely.
- [ ] Add `CommandBox.*` tests in `tests/recto-tests/command_box_source_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=CommandBox.*` passes.
- [ ] Commit: `app: add the command box source interface`

**Test checkpoint:** `recto-tests.exe --gtest_filter=CommandBox.*` passes, and making `CommandBoxFilter` wait for a slow source on the UI thread makes `CommandBox.SlowSource` fail.

## 2. `Go to` rows for comments and form fields

Register two sources: one over the Comments view model of `D03 T03 §5` (comment text, author, page) and one over `FormViewModel` of `D03 T03 §8` (field name, tooltip, page). Choosing a row scrolls the page to the item and selects it on the canvas, exactly as clicking the card or tabbing to the field would.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** `design/SPEC.md` section 9.4 (`Go to` rows with a 16 epx glyph drawn directly on the row) and 11. Design board: `design/screenshots/D-Dialogs.png`.
**Job:** A reviewer types a colleague's name or a field label and jumps to the comment or field. Consumer: `CommandBoxControl`, `DocumentView` and the Comments panel selection.
**Treatment:** Two `CommandBoxSource`s reading the existing view models (no second copy of the data), capped at `8` rows each. Cheaper substitute that fails the checkpoint: opening the Comments panel and asking the user to search again.
**Chrome:** consume the `§1` row layout, the `RectoIcons` subtype glyphs of `D03 T03 §5` and `recto_tokens.g.h` tokens.

**Build order:**

1. Implement `CommentsGoToSource` and `FormFieldsGoToSource` in `src/app/commands/goto_sources.cpp`. Done when: `CommandBox.GoToComments` and `CommandBox.GoToFields` find rows by text, author, name and tooltip.
2. Execute the row action. Done when: `AppTests.CommandBoxGoToComment` scrolls and selects the annotation and `AppTests.CommandBoxGoToField` focuses the field.

- [ ] Implement `CommentsGoToSource` over `CommentsPanelViewModel` matching `/Contents` and `/T`, returning at most `8` rows with the subtype glyph and `Page N`. Done when: `CommandBox.GoToComments` returns the expected rows for a 100-annotation fixture in under `5 ms`.
- [ ] Implement `FormFieldsGoToSource` over `FormViewModel` matching the field name and `/TU`, returning at most `8` rows. Done when: `CommandBox.GoToFields` returns the expected rows on `tests/corpus/convert/tables-and-forms.pdf`.
- [ ] Run the row action on the UI thread: scroll `DocumentView` to the page, select the annotation or focus the field overlay, and open the panel only if it is already docked. Done when: the two `AppTests` above pass. Cheaper substitute: changing page only.
- [ ] Show no rows, and no error, when the document has no comments or no fields. Done when: `CommandBox.GoToEmpty` passes on a plain PDF.
- [ ] Announce the chosen target through `AnnouncementService` (`D03 T02 §5`) as `Comment by <author>, page N` or `Field <name>`. Done when: `A11y.CommandBoxGoToAnnouncement` captures the notification.
- [ ] Commit: `app: add comment and form field rows to the command box Go to section`

**Test checkpoint:** `recto-tests.exe --gtest_filter=CommandBox.GoTo*:AppTests.CommandBoxGoTo*:A11y.CommandBoxGoToAnnouncement` passes, and a source that returns rows for a deleted annotation makes `AppTests.CommandBoxGoToComment` fail.

## 3. `Files` rows for indexed files

Register a `CommandBoxSource` over the local `SearchIndex` (`D03 T05 §4`) that lists indexed files whose name matches the typed text, after the recent files. Choosing a row opens the file through the shared open pipeline; a file that no longer exists is shown dimmed with the reason and removed from the index on the next update.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** `design/SPEC.md` section 9.4 (`Files` rows: "recent and indexed files", glyph drawn directly on the row). Design board: `design/screenshots/D-Dialogs.png`.
**Job:** A user who indexed a folder types part of a file name and opens it from `Ctrl+K`. Consumer: `CommandBoxControl` and the open pipeline (`D02 T01 §5`).
**Treatment:** A name-only query over the index file table (never the text), `8` rows maximum, skipped entirely when the user has not turned the index on. Cheaper substitute that fails the checkpoint: scanning the folders on every keystroke.
**Chrome:** consume the `§1` row layout and the `D02 T03 §12` file row glyph treatment.

**Build order:**

1. Implement `IndexedFilesSource` in `src/app/commands/indexed_files_source.cpp` querying `SearchIndex::list_files_by_name`. Done when: `CommandBox.IndexedFiles` returns the expected rows after recent files.
2. Handle off, empty, corrupt and missing-file cases. Done when: `CommandBox.IndexedFilesOff` and `CommandBox.IndexedMissing` pass.

- [ ] Add `SearchIndex::list_files_by_name(query, limit)` reading only the file table in `src/core/search_index.cpp`, bounded to `limit` rows and returning nothing for a corrupt index (never trusting it). Done when: `SearchIndex.ListByName` passes and a corrupt index returns an empty list.
- [ ] Implement `IndexedFilesSource` ranking prefix matches first, deduplicating files already shown as recent. Done when: `CommandBox.IndexedFiles` shows each file once, recent first.
- [ ] Return no rows when `Use index` is off or the index is empty. Done when: `CommandBox.IndexedFilesOff` passes and no index file is opened.
- [ ] Open the file through `OpenDocumentPipeline`; for a missing file show the row dimmed with `File not found` and schedule its removal at the next incremental update. Done when: `AppTests.CommandBoxOpenIndexed` opens a file and `CommandBox.IndexedMissing` dims a deleted one.
- [ ] Commit: `app: add indexed files to the command box Files section`

**Test checkpoint:** `recto-tests.exe --gtest_filter=CommandBox.IndexedFiles*:CommandBox.IndexedMissing:SearchIndex.ListByName:AppTests.CommandBoxOpenIndexed` passes, and scanning folders instead of the index makes `CommandBox.IndexedFilesOff` fail.

## Verification

- `Ctrl+K` offers comment, form field and indexed file rows exactly as SPEC 9.4 lists, each proven by its `CommandBox.*` test.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
