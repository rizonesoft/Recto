## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T07:38:46Z
- Effort: medium
- Lines: 29
- Digest: 6b2e3fe9ab4e0a8a

**integration: needs-attention**

1. **`RectoChipStyle` can build before its font exists.** [App shell §19 and Implementation Order](R:/GitHub/RectoPDF/todo/02-viewer/TODO-01-app-shell.md) depends only on §1, but requires `RectoIcons` status glyphs and tests their presence. §18 is not a prerequisite. §2 depending on both does not order them relative to each other. Add §18 to §19’s dependencies.

2. **Later boards cannot add new glyphs using the prescribed workflow.** [Design boards §§2–27](R:/GitHub/RectoPDF/todo/06-quality/TODO-04-design-boards.md) instruct authors to add a SPEC glyph row and rebuild. However, app-shell §18 vendors only the initial subset and explicitly fails when an SVG is missing. No board step acquires additional pinned SVGs or updates their provenance and `NOTICE.md`. The boards also prescribe exclusively `fluent-svg/` inputs, although Redact and Squiggly live in `custom/`. Specify the complete extension workflow and both source directories.

3. **Custom layouts can put the status bar and filmstrip in the same slot without a composition rule.** [App shell §20](R:/GitHub/RectoPDF/todo/02-viewer/TODO-01-app-shell.md) shows the status bar whenever the toolbar uses the Classic docked placement. The independently configurable bottom filmstrip also occupies `BottomDockSlot`, as specified in [settings §3](R:/GitHub/RectoPDF/todo/02-viewer/TODO-04-settings-and-layout.md). A Custom combination can therefore require both simultaneously. Define their ordering and sizing, and test that combination; testing only the three presets misses it.

**testability: needs-attention**

4. **The proposed checker accepts an empty board and an incomplete control inventory.** [Design boards §1, checks (a)–(g)](R:/GitHub/RectoPDF/todo/06-quality/TODO-04-design-boards.md) checks control strings only against SPEC prose. It neither compares the manifest against consumer requirements nor checks rendered controls. A blank source, a correctly sized fresh blank PNG, and SPEC text containing the manifest labels satisfy every check. Empty `controls` or omitted controls also escape detection. Require an independently established consumer-control inventory, control identifiers linked to rendered elements, and visibility/bounds checks. Include negative fixtures for an omitted consumer control and a blank board.

5. **The style check does not enforce the claimed shape and colour rules.** [Design boards §1, check (f)](R:/GitHub/RectoPDF/todo/06-quality/TODO-04-design-boards.md) permits `999px` or `50%` on any element, including ordinary buttons and icon tiles. Checking only hex literals also misses non-token `rgb()` colours and styles supplied through rendering data. This can certify precisely the designs the owner prohibited. Validate rendered styles against semantic element roles, including permitted pill roles and glyph backgrounds.

**consistency: needs-attention**

6. **Compare PDFs remains without an owned board.** [Design boards Outcome and §9](R:/GitHub/RectoPDF/todo/06-quality/TODO-04-design-boards.md) promises coverage of every planned surface, but §9 omits `D03 T01 §10`. That consumer builds the comparison header, synchronized canvases, overlay mode and Changes panel specified in [SPEC §9.3.1](R:/GitHub/RectoPDF/design/SPEC.md). The existing `D-Search` source contains no comparison surface, and no new board section owns it. Schedule its board and connect the consumer.

7. **The new status-bar keyboard order contradicts its shell prerequisite.** [App shell §20](R:/GitHub/RectoPDF/todo/02-viewer/TODO-01-app-shell.md) correctly requires the status bar after the page, but §4 explicitly requires `BottomDockSlot -> CenterCanvasSlot` and tests that order. With the status bar in `BottomDockSlot`, both contracts cannot hold. Correct the owning shell focus-order contract alongside §20.

8. **Interactive chips are specified below the minimum hit-target size.** [App shell §19, build steps 1–2](R:/GitHub/RectoPDF/todo/02-viewer/TODO-01-app-shell.md) makes `Chip` and `FilterTag` 24 epx high and adds an embedded dismiss button, without a separate larger hit area. [SPEC §11](R:/GitHub/RectoPDF/design/SPEC.md) requires at least 32 × 32 epx. Define compliant interactive bounds independently of the pill’s visual height and test both chip activation and dismissal.

**source-defect: advisory**

[App shell §18](R:/GitHub/RectoPDF/todo/02-viewer/TODO-01-app-shell.md) leaves actual font consumption unproven. Its checks establish generated-file consistency, glyph membership, resource lookup and file copying, but never render the generated code points through `FontIcon`. Empty, incorrectly oriented or clipped outlines could satisfy the stated checkpoint. Add a rendered Regular/Filled glyph specimen check, including both custom glyphs, in packaged and unpackaged builds.

These findings are from direct file inspection and the supplied candidate. No files were modified, and no Git commands or tests were run.

OVERALL: needs-attention

## Response to round 1

1. Fixed: `todo/02-viewer/TODO-01-app-shell.md` Implementation Order now has §19 depend on `§1, §18`, so `RectoChipStyle` cannot build before the `RectoIcons` status glyphs exist; §18 also now depends on §1, whose app project its XAML merge and new specimen test need.
2. Fixed: app-shell §18 gains a repeatable `scripts/build-icon-font.py --vendor` step that copies a new table name's two SVGs from the same pinned package, appends their SHA-256 to `SOURCE.md` and the name to `NOTICE.md`, never touches existing files, and is tested on a fixture SPEC with one added row; custom rows map to `recto_<concept>` in `resources/fonts/custom/`, and a missing SVG names its fix. Every board section (`TODO-04-design-boards.md` §2 to §28) now states the full workflow (row, `--vendor` or a custom glyph under the SPEC section 6 acceptance rule, rebuild, commit with `SOURCE.md` and `NOTICE.md`) and draws from both `fluent-svg/` and `custom/`.
3. Fixed: app-shell §4 `ShellSlotManager::reparent_control` now takes a `SlotOccupant` with a stacking order and a focus group, and §20 records the composition rule for a Custom layout with the docked command bar and the bottom filmstrip (filmstrip above, status bar fixed at 32 epx below, canvas gives up the combined height), with `Shell.ClassicStatusBar` testing that combination's stacking, bounds, height and keyboard order.
4. Fixed: design-boards §1 rewritten: the manifest lists controls per consumer, check (a) requires each list to be non-empty and each label to appear in that consumer section's text, check (b) scans each consumer section for label-shaped backticked text missing from the manifest, check (e) rejects a blank PNG, and check (f) requires every control as a visible, in-canvas `data-control` element in a rendered element inventory that `render-mockups.mjs` now writes; negative fixtures cover a blank board, an empty consumer list, an omitted consumer control and a hidden or missing control element.
5. Fixed: design-boards §1 checks (g) and (h) now run on computed styles from the rendered inventory: radius by `data-role` (4 px controls, 8 px overlays, pill only on chip, filter-tag, keycap, badge and counter roles), every computed colour and every hex, `rgb()`, `hsl()` or named literal in the source and its `renderVals()` data must be a token, inlined glyph paths must equal the vendored or custom SVG, and a glyph on a tinted box at most twice its size fails unless it is a toggled control (SPEC section 6); fixtures cover each.
6. Fixed: new design-boards §28 draws `D-Compare.png` and completes SPEC section 9.3.1 for `D03 T01 §10` (Implementation Order row, Inputs XREF, Outcome counts updated); in `todo/03-text/TODO-01-selection-and-search.md` (frozen file, Freeze checks untouched) §10 gains the `D06 T04 §28` dependency, a Fidelity citation and the reverse XREF.
7. Fixed: app-shell §4 keyboard order now follows occupant focus groups (`BeforeCanvas` occupants of `BottomDockSlot` such as the filmstrip before `CenterCanvasSlot`, `AfterCanvas` occupants such as the status bar after it), matching SPEC section 11 and `D03 T02 §4`; `Shell.NamedSlotsAndReparenting` fails when the `AfterCanvas` occupant is reached before the page, and §20 places the bar in `AfterCanvas`.
8. Fixed: app-shell §19 separates the visual pill from the hit area: `Chip` and `FilterTag` get at least 32 x 32 epx hit bounds around the 24 epx pill, the dismiss button owns its own 32 x 32 epx hit box carved out of the chip body, `Keycap`, `Badge` and `Counter` are declared non-interactive, and `Shell.ChipStyleShape` tests activation (including a press in the hit margin, `Enter`, `Space`) and dismissal (dismiss box, `Delete`, `Backspace`) with no cross-firing.
Advisory (source-defect): Fixed: app-shell §18 adds `Shell.IconFontSpecimen`, rendering every Regular and Filled code point (custom glyphs included) through `FontIcon` at 16, 20 and 24 epx against a Direct2D raster of its SVG (non-empty ink, no clipping, alpha IoU at least 0.9) for the unpackaged output and the `-EnableMsix` layout, failing on emptied, mirrored and shifted fixture outlines; §18 Needs is now `windows-host, display-session`.
