## Round 1: gemini (`gemini-4-argon-medium` via `agy`)

- Writer: claude
- Run: 2026-10-21T07:45:45Z
- Skipped: astra (could not review: **consistency: needs-attention**)
- Lines: 35
- Digest: 87475899ef3dc104

### Lens: `consistency` — **needs-attention**

1. **[TODO-05-rich-media-3d-geo.md](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-05-rich-media-3d-geo.md#L467-L483) (`D02 T05 §17`) — Leftover "speaker tile" / "audio tile" violates the 2026-10-21 no-icon-tiles decision and contradicts `§4`:**
   - While `D02 T05 §4` ([line 146](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-05-rich-media-3d-geo.md#L146)) was updated to replace the default audio/video tile with `"the media rectangle outlined in RectoBorderBrush with a 24 epx RectoIcons video or speaker_2 glyph drawn directly on the overlay, no filled tile behind it"`, `D02 T05 §17` still requires a filled tile in two places:
     - [Line 469](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-05-rich-media-3d-geo.md#L469): `"chooses a poster (a frame from the video through the Windows thumbnail service, a chosen image, or a generic speaker tile for audio)"`
     - [Line 483](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-05-rich-media-3d-geo.md#L483) (Build order step 2): `"use a token-coloured speaker tile for audio; keep the poster at most 1024 px on the long side. Done when: a video poster, a custom poster and an audio tile each render in the page appearance."`
   - This contradicts `design/SPEC.md` section 6 (*"No icon tiles: no interface glyph sits inside a filled or tinted box, tile or circle, anywhere"*) and `D02 T05 §4`.

2. **[TODO-04-settings-and-layout.md](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-04-settings-and-layout.md#L176) (`D02 T04 §4`) — Selected preset card still specifies a `check` glyph inside a filled `RectoAccentBrush circle` instead of `RectoIcons` `checkmark` / `RectoChipStyle`:**
   - [Line 176](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-04-settings-and-layout.md#L176) states: `"and a visible check badge (FontIcon check glyph on RectoAccentBrush circle so selection is never conveyed by colour alone per SPEC 11)"`.
   - Under the 2026-10-21 decisions (`design/SPEC.md` sections 6 and 7, and `D02 T01 §19`), the canonical glyph name in `RectoIcons` is `checkmark` (not `check`), no glyph sits inside a filled circle (`SPEC` section 6), and status/selection badges use `RectoChipStyle` (`D02 T01 §19`, as `D05 T02 §1` was wired to do in `D02 T01` line 39).

3. **[TODO-04-settings-and-layout.md](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-04-settings-and-layout.md#L188-L209) (`D02 T04 §5`) — Internal contradiction on `Recto PDF virtual printer` and non-canonical group names in the section heading/deliverable:**
   - [Line 190](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-04-settings-and-layout.md#L190) says `Recto PDF virtual printer (placeholder card populated by D07 T01 §14)`, whereas Build order step 2 ([line 203](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-04-settings-and-layout.md#L203)) and checklist item 5 ([line 209](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-04-settings-and-layout.md#L209)) require `D02 T04 §5` itself to build the full `Recto PDF virtual printer` group card (`save mode Ask where to save / Save to folder, default folder picker, Open in Recto after saving toggle`, plus the Portable mode note) and leave only `Assistant` and `Language and updates` as empty group containers.
   - In addition, the `§5` Deliverable cell ([line 71](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-04-settings-and-layout.md#L71)) and heading ([line 188](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-04-settings-and-layout.md#L188)) still use the abbreviated pre-decision names (`Signatures & security`, `Performance & privacy`, `Printer`, `Default app`) and omit `Assistant` and `Language and updates` (and at 111 chars exceed `MAX_DELIVERABLE_LEN = 88`).

4. **[TODO-03-home-and-panels.md](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-03-home-and-panels.md#L249-L321) (`D02 T03 §6` and `§8`) — Unswept glyph descriptions in `Fidelity` / `Chrome` / checklist items:**
   - In `D02 T03 §6` ([lines 249–252](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-03-home-and-panels.md#L249-L252)) and `D02 T03 §8` ([lines 309–321](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-03-home-and-panels.md#L309-L321)), the sweep added the `D06 T04 §2` board citation but left the old prose glyph descriptions (`sections 6 (bookmark add, undo, redo, delete glyphs)` in `§6`; `sections 6 (paperclip attachment, portfolio folder glyphs)` and `file icon` / `file-type glyph` in `§8`) without naming `RectoIcons` (`bookmark_add`, `arrow_undo`, `arrow_redo`, `delete`, `attach`, `folder_zip`, `document`) or the no-tile rule on attachment file rows.

---

### Lens: `integration` — **needs-attention**

1. **[TODO-03-home-and-panels.md](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-03-home-and-panels.md#L663-L694) (`D02 T03 §22`) vs. [TODO-04-design-boards.md](file:///R:/GitHub/RectoPDF/todo/06-quality/TODO-04-design-boards.md#L125-L146) (`D06 T04 §2`) — Missing consumer wiring for `D-Panels.png` (`SPEC` section 8.3.1):**
   - `D06 T04 §2` ([lines 136–138](file:///R:/GitHub/RectoPDF/todo/06-quality/TODO-04-design-boards.md#L136-L138)) draws *"the Layers panel with eye toggles and the editing menu"* on `D-Panels.png` and requires `board-controls.json` for `D-Panels` (`SPEC` section 8.3.1) to include `"New layer"`, `"Merge layers"`, and `"Import as layer"` from its consumers' checklists.
   - Those controls are built in `D02 T03 §22` (*"Create, rename, reorder, merge and flatten layers, and import a PDF as a layer"*), not `D02 T03 §7`. However:
     - `D02 T03 §22` ([line 82](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-03-home-and-panels.md#L82)) does not list `D06 T04 §2` in `Depends On`.
     - `D02 T03 §22` ([line 676](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-03-home-and-panels.md#L676)) still says `**Fidelity:** Genuinely new controls inside the Layers panel of design/SPEC.md section 8.3.` without citing `D-Panels.png` or `section 8.3.1`.
     - `D02 T03` `Inputs` ([line 36](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-03-home-and-panels.md#L36)) and `D06 T04 §2` ([lines 23, 127](file:///R:/GitHub/RectoPDF/todo/06-quality/TODO-04-design-boards.md#L127)) omit `§22` from the consumer list (`§6, §7, §8, §10, §13, §25`).
   - Similarly, `D02 T03 §19` ([line 592](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-03-home-and-panels.md#L592), `"Genuinely new dialog"`) and `D02 T03 §20` ([line 620](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-03-home-and-panels.md#L620), `"Genuinely new dialog; uses the two-column preview dialog of design/SPEC.md section 9.9"`) build new Outline panel header buttons and dialogs (`Export...`, `Import...`, `Insert Table of Contents`) with no design-board dependency (`D06 T04 §2`).

2. **[TODO-02-document-view.md](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-02-document-view.md#L714-L737) (`D02 T02 §24`) vs. [TODO-04-design-boards.md](file:///R:/GitHub/RectoPDF/todo/06-quality/TODO-04-design-boards.md#L217-L238) (`D06 T04 §6`) — Unwired `Surface: yes` split-view sibling section:**
   - `D02 T02 §23` (*"Split view: two independent viewports on the same document"*) was wired to `D06 T04 §6` (`D-ViewTools.png`, `SPEC` section 9.11), but `D02 T02 §24` (*"Side-by-side view of two documents with synchronized scrolling"*, building `SplitWithTabControl` with `Sync scroll`, `Swap sides`, and `Close split`) still declares `**Fidelity:** Genuinely new surface; the divider and controls follow design/SPEC.md section 7.` ([line 720](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-02-document-view.md#L720)) with no dependency on `D06 T04 §6` ([line 74](file:///R:/GitHub/RectoPDF/todo/02-viewer/TODO-02-document-view.md#L74)).

OVERALL: needs-attention

## Response to round 1

Consistency lens:

1. Fixed: `D02 T05 §17` context and Build order step 2 now give audio the §4 default poster (an outlined rectangle with the Fluent `speaker_2` glyph converted to path operators, no filled tile, token colours resolved at write time), and `Overlays.AddMediaTool` checks that the only fills in the audio appearance are the glyph's own subpaths.
2. Fixed: `D02 T04 §4` check badge is now a 16 epx `RectoIcons` `checkmark` (Filled) in `RectoAccentBrush` drawn directly on the card with no circle or tile (SPEC 6 forbids a glyph-only filled pill too, so the badge is not a `RectoChip`), with a visual-tree and Narrator assertion in `Settings.LayoutAndThemeGroupsAndFilter`; a new checklist item renders the `Default` and `"Custom"` status badges as `RectoChip` `Kind=Badge`, so §4 now depends on `D02 T01 §19` (XREFs added in both files, and `D02 T04 §4` added to the `D02 T01 §19` consumer list).
3. Not changed: the cited text is not in the file; `D02 T04 §5` is the Reading and Converting groups, and `§6` already reads `## 6. Settings: Signatures and security, Performance and privacy, Default PDF app, About` (79 characters) with Build order step 2 fixing "the canonical 11-group order" and `kSettingsGroupOrder` naming `Assistant`, `Recto PDF virtual printer` and `Language and updates` as groups that arrive from `D08 T01 §15`, `D07 T01 §18`, `D07 T02 §14` and `D07 T03 §7`; no `D07 T01 §14` placeholder card exists.
4. Fixed: `D02 T03 §6` Fidelity and Chrome name `bookmark_add`, `arrow_undo`, `arrow_redo`, `delete`; `§7` Fidelity names `layer`, `eye`, `eye_off` (it said `layers`); `§8` context, Fidelity, Chrome, Build order step 2 and the panel checklist item replace "file icon" with a per-type `RectoIcons` glyph map drawn with no tile, tested in `Panels.AttachmentsAndPortfolio` by glyph name and a visual-tree walk.

Integration lens:

1. Partly fixed: the Layers editing controls are built by `D02 T03 §25` (not `§22`), which already cites `D-Panels.png` and depends on `D06 T04 §2`; the real gap was `§17` (`Create bookmarks`), `§19` (bookmark import and export), `§20` (`Bookmark style`) and `§22` (`Insert contents page`), which now cite section 8.3.1 and `D-Panels.png` and depend on `D06 T04 §2`, with the consumer lists, draw step, manifest labels and Outcome of `D06 T04 §2` and both XREF lines updated.
2. Fixed: `D02 T02 §24` now cites section 9.11 and `D-ViewTools.png` and depends on `D06 T04 §6`; `D06 T04 §6` lists `§24` as a consumer, draws the side-by-side frame, and its manifest gains `Compare side by side`, `Synchronized scrolling`, `Align pages` and `Close side by side` (and `Remove split`, the label `§23` actually uses, replaces `Close split`).
