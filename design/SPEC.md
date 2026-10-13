# Recto design specification

Status: approved direction "D · Merged", 2026-10-13. Owner of decisions: the project owner. Review again after the first runnable shell (milestone M2).

This file is the source of truth for how Recto looks and behaves. When a screenshot and this text disagree, this text wins. When a screen has no D screenshot, build it from the reference listed for it and apply the tokens in section 4.

Architecture, threading and milestones live in `docs/plans/2026-10-13-recto-foundation.md`. This spec covers the interface only.

## 1. What is in this folder

| Path | What it is |
|---|---|
| `SPEC.md` | This specification |
| `screenshots/D-*.png` | The chosen design, one PNG per screen or state |
| `screenshots/explorations/` | Versions A, B and C that D was assembled from, plus `Main.png` comparing them |
| `tokens/colors.json` | Every colour as data |
| `tokens/RectoColors.xaml` | The same colours as a WinUI 3 `ResourceDictionary`, ready to merge into `App.xaml` |
| `brand/` | The README banner and the GitHub social preview |
| `../resources/` | Every icon and graphic the app ships: SVG masters, MSIX PNGs, ICO files (section 3) |
| `mockups/html/` | Static HTML of every screen; open in a browser to inspect sizes and colours with dev tools |
| `mockups/source/` | Original canvas sources (`.dc.html`) and `canvas.json`; only needed to re-render |
| `tools/render-mockups.mjs` | Regenerates `mockups/html` and `screenshots` from `mockups/source` (section 14) |
| `tools/build_resources.py` | Regenerates `resources/` from the tokens (section 14) |

Mockup sizes: app screens are 1440×900 px at 100% scale. Treat pixel values in the HTML as effective pixels (epx) in WinUI.

## 2. Principles

1. The page is the product. Chrome stays quiet, low-contrast and out of the page area; the accent appears only where the user acts or where they are.
2. One app shell. Every layout preset uses the same controls in different places (section 8).
3. Native first. Use stock WinUI 3 controls and Windows behaviours (Mica, caption buttons, snap layouts, system dialogs for files and printing) unless this spec says otherwise.
4. Honest conversion. Say what a conversion is good at and what it is not (PDF to Word works best on text documents).
5. Safe by default. Links and Office macros never run without the user's say-so.

## 3. Brand

### 3.1 App icon

A white page with a vermilion bookmark ribbon, on a charcoal rounded-square tile. The ribbon marks "where you are reading": the same idea as the name (the recto is the right-hand page).

All icon and graphic files are built and live in `resources/` at the repository root; `resources/README.md` lists every file, where it goes in `Package.appxmanifest`, and how to rebuild. `resources/preview.png` shows them all.

| File | Use |
|---|---|
| `resources/svg/app-icon.svg` | Master, 32 px and larger. Includes three text lines on the page. |
| `resources/svg/app-icon-small.svg` | 24 px and smaller (title bar, taskbar, tabs). Bigger page and ribbon, no text lines. |
| `resources/svg/app-icon-on-dark.svg`, `app-icon-small-on-dark.svg` | Dark backgrounds. Adds an edge so the charcoal tile does not disappear. |
| `resources/msix/Assets/` | Every package logo at every scale and target size, including the dark-taskbar `altform-unplated` set |
| `resources/ico/recto.ico` | Window icon (`AppWindow.SetIcon`) and unpackaged builds |

The icon colours are fixed and never follow the theme. Reference board: `screenshots/D-Brand.png`.

### 3.2 PDF file-type icon

What Explorer shows for `.pdf` files when Recto is the default PDF app: a white document with a folded top-right corner, the Recto ribbon hanging from the top edge, three text lines, and a vermilion "PDF" badge overlapping the bottom-left edge. At 24 px and below the lines and badge are dropped and the outline gets heavier. Files: `resources/svg/filetype-pdf.svg`, `resources/svg/filetype-pdf-small.svg`, `resources/msix/Assets/FileTypes/PdfFile.targetsize-*.png` (manifest `uap:Logo` of the `.pdf` file-type association) and `resources/ico/pdf-file.ico`.

### 3.3 Wordmark

"Recto" set in **Newsreader Medium** at the 72 pt optical size (Google Fonts, SIL Open Font License 1.1). Used only for the wordmark: the title bar name, About, the Store listing and the banner. Letter spacing -1 at 48 px and above. The app uses the outlined SVGs (`resources/svg/wordmark-*.svg`, `lockup-*.svg`), so it does not need to ship the font. Tagline, when used: "the right-hand page", Newsreader Italic.

### 3.4 In-app graphics

`resources/svg/` also holds the Settings layout preset wireframes (`layout-*-light.svg` and `-dark.svg`), the Home empty-state and drop-overlay illustration (`drop-files-*.svg`) and the current-page ribbon marker (`ribbon-marker*.svg`). Load them with `SvgImageSource` and switch light and dark files through theme resources.

### 3.5 GitHub banner and social preview

`brand/github-banner.png` (1280×320, for the top of the README) and `brand/github-social-preview.png` (1280×640, set in the repository settings). Both are dark: charcoal background, the ribbon hanging from the top edge, the Newsreader wordmark, and "Open-source PDF viewer and converter for Windows". Replace `[OWNER]` in the social preview with the real GitHub owner before uploading.

## 4. Colour

All colours come from `tokens/RectoColors.xaml`. No control sets a hex value directly. Text pairs meet WCAG AA (4.5:1) in both themes.

| Token (brush key) | Light | Dark | Used for |
|---|---|---|---|
| `RectoWindowBrush` | `#EEF1F6` | `#1B1A19` | Title bar and window background (over Mica Alt where available) |
| `RectoSurfaceBrush` | `#FAFAFA` | `#232120` | Docked panes, command bar, inspector |
| `RectoRaisedBrush` | `#FFFFFF` | `#2A2826` | Floating toolbar, floating panels, cards, menus, dialogs |
| `RectoCanvasBrush` | `#E6E7EA` | `#141312` | The area behind document pages |
| `RectoBorderBrush` | `#E5E5E5` | `#3A3734` | Dividers and card borders |
| `RectoBorderStrongBrush` | `#D6D9DF` | `#4A4642` | Segmented control tracks, input borders |
| `RectoTextBrush` | `#1B1B1B` | `#EDEAE4` | Primary text and icons |
| `RectoTextSecondaryBrush` | `#5C5C5C` | `#B8B2A8` | Captions, metadata, inactive tabs |
| `RectoAccentBrush` | `#C8431F` | `#F0603C` | Primary buttons, selection rings, focus, progress, current page |
| `RectoOnAccentBrush` | `#FFFFFF` | `#1B1A19` | Text and icons on the accent |
| `RectoAccentTextBrush` | `#A93717` | `#F48A6E` | Links and accent-coloured text |
| `RectoAccentTintBrush` | `#FBE4DC` | `#3A2620` | Selected list rows, selected thumbnails, toggled toolbar buttons |
| `RectoSearchHitBrush` | `#F6CDBF` | `#FFD8A8` | Search matches on the page |
| `RectoSearchHitCurrentBrush` | `#C8431F` | `#F0603C` | The current match (text on it uses `RectoOnAccentBrush`) |
| `RectoTextSelectionBrush` | accent at 18% | accent at 22% | Selected text on the page |
| `RectoSuccessBrush` / `Warning` / `Danger` | `#0F7B0F` / `#8A5300` / `#B3261E` | `#7DD3A0` / `#E2C27A` / `#FF9A85` | Status icons and messages |

The dictionary also replaces the Windows accent ramp (`SystemAccentColor` and its `Light1..3` and `Dark1..3` variants), so stock controls pick up vermilion. With WinUI's default mapping this gives accent fills of `#C8431F` in light and `#F0603C` in dark, and accent text of `#A93717` and `#F48A6E`. Check this against the Windows App SDK version in use when the shell is first built; if the mapping differs, adjust the ramp, not individual controls.

High contrast: the `HighContrast` dictionary maps every Recto brush to a system colour. Never hide information behind colour alone.

**Pages in dark theme** (a setting): Original (pages render as authored), Dimmed (default: pages are multiplied towards `#D9D5CE`, as in `D-Focus-Dark.png`) and Inverted (light text on `#1B1A19`; images are not inverted). This changes how tiles are composited, not the PDF.

The mockup boards for screens other than the viewers show light theme only. Build their dark versions by swapping tokens; `D-Focus-Dark.png` and `D-ReadingRoom.png` show how surfaces, borders and the accent behave in dark.

## 5. Typography

Interface: **Segoe UI Variable** through the stock WinUI type ramp. Documents are never restyled; page text is whatever the PDF contains.

| Role | WinUI style | Size / weight | Where |
|---|---|---|---|
| Caption | `CaptionTextBlockStyle` | 12 / Regular | Metadata, tab titles, labels above groups |
| Body | `BodyTextBlockStyle` | 14 / Regular | Default text |
| Body strong | `BodyStrongTextBlockStyle` | 14 / Semibold | Row titles, selected items |
| Subtitle | `SubtitleTextBlockStyle` | 20 / Semibold | Dialog titles, card headings |
| Title | `TitleTextBlockStyle` | 28 / Semibold | Page titles (Settings, Organize) |
| Title large | `TitleLargeTextBlockStyle` | 40 / Semibold | Home question ("What do you want to open?") |
| Wordmark | custom | Newsreader Medium | Section 3.2 only |

Group labels above settings groups are 12 px Semibold, all caps, letter spacing 0.6.

## 6. Icons

Use **Segoe Fluent Icons** (the system icon font) for every interface glyph, through `FontIcon` or `SymbolIcon`. The stroke icons in the mockups are placeholders that show which concept goes where; pick the matching Segoe Fluent glyph (home, open file, save, print, zoom in, zoom out, fit width, rotate, find, settings, more, page, pages grid, outline list, two-page view, moon for page dimming, convert, combine, split, lock, unlock, image, text, Word document, Office, warning, info, check, close, add, copy, link, undo, delete).

Icon sizes: 16 epx in toolbars and lists, 20 epx in the navigation rail, 24 epx in dialog headers.

## 7. Shape, spacing and elevation

- Spacing follows a 4 epx grid. Common gaps: 4, 8, 12, 16, 24, 32.
- Corner radius: 4 for small controls (stock), 8 for tabs, inputs and list rows, 10 for buttons inside the floating toolbar, 12 for the page canvas and segmented controls, 16 for floating panels, the floating toolbar and settings groups, 20 for dialogs and the command box.
- Elevation (shadows) only on floating things: the floating toolbar, floating panels, the command box, menus, dialogs and the queue card. Docked panes have borders, not shadows. Use `ThemeShadow` with `Translation` Z of 32 for the toolbar and panels and 64 for dialogs and the command box.
- Document pages sit on the canvas with a soft shadow (1 epx key, 24 epx ambient) and square corners.
- Window minimum size: 960 × 600 epx. Below 1100 epx wide, floating panels close when the user clicks the page.

## 8. App shell and layout presets

One `ShellPage` hosts the same controls in every preset. A `LayoutSettings` view model holds five settings; the shell moves controls between named slots when a setting changes. Changing any single setting turns the preset name to "Custom". Reference: `screenshots/D-Layout.png`.

| Setting | Focus (default) | Classic | Reading room |
|---|---|---|---|
| Page toolbar | Floating, bottom centre | Docked command bar, top | Docked strip under the tabs |
| Side panels | Floating cards over the canvas | Docked left pane | Docked right inspector |
| Page thumbnails | In the left panel | In the left pane | Filmstrip along the bottom |
| Search and outline | Left | Left | Right |
| Navigation (Home, Convert, Organize) | Title bar tabs plus command box | Left navigation rail | Mode switcher in the title bar |
| Screenshot | `D-Focus-Light.png`, `D-Focus-Dark.png` | `D-Classic.png` | `D-ReadingRoom.png` |

Slots: title bar, top bar, bottom overlay, left dock, right dock, left overlay, right overlay, bottom dock. `ThumbnailList` switches its `ItemsRepeater` layout between a vertical stack and a horizontal stack when it moves between the left panel and the filmstrip.

### 8.1 Title bar (all presets)

Left to right: small icon (20 epx), wordmark "Recto" (Newsreader, 17 epx), Home tab (icon only), one tab per open document (`TabView`, max 240 epx, close button on hover and on the active tab), new tab (+), the command box button centred in the remaining space ("Search, open or run a command", shows "Ctrl K"), settings, then the system caption buttons. The title bar is draggable everywhere except on interactive elements. In Reading room the mode switcher (Home, Read, Convert, Organize, underline indicator in accent) replaces the Home tab and document tabs move to the strip below.

### 8.2 Floating page toolbar (Focus)

A single row on `RectoRaisedBrush`, radius 16, 6 epx padding, centred 22 epx above the bottom of the canvas. Order: side panel toggle | previous page, "3 / 9" (click to type a page), next page | zoom out, zoom % (click for presets: fit width, fit page, 50 to 400%), zoom in, fit width | find, rotate, print | **Convert** (accent button) | more (save a copy, page layout, two-page view, page colour, properties). Toggled buttons use `RectoAccentTintBrush` with accent-coloured glyphs. The toolbar fades to 40% opacity after 3 s without pointer movement while reading and returns on hover or keyboard focus; it never hides completely.

### 8.3 Floating side panel (Focus)

A card 16 epx from the canvas edges, radius 16, `RectoRaisedBrush` at 90% opacity with acrylic where available. Header row: panel name, switch to the other panel (Pages, Outline), close. Pages shows thumbnails with page numbers; the current page has a 2 epx accent ring and the vermilion ribbon marker on its top-right edge. Outline is a `TreeView`; the current section is highlighted with `RectoAccentTintBrush` and a 3 epx accent bar on the left. Search opens as its own card (section 9.3).

## 9. Screens

### 9.1 Home (`D-Home.png`)

Purpose: open something fast. Centre column: the question "What do you want to open?", one line of help, and the command box expanded inline (720 epx wide). Typing filters actions and files together; arrow keys move, Enter chooses. Footer of the box: key hints and "Browse files" (accent). Below: "Recent" as six document covers with name and last-opened day, and "See all". Dropping files anywhere in the window opens them; a full-window drop overlay appears while dragging. Reading position is remembered, so a recent file opens at the page where the user stopped.

Empty state (no recent files): hide the Recent section and show "Drop a PDF anywhere in this window" under the box.

### 9.2 Viewer (`D-Focus-Light.png`, `D-Focus-Dark.png`, `D-Classic.png`, `D-ReadingRoom.png`)

Continuous vertical scroll by default; single page and two-page spread are options (two-page shows the cover alone, then pairs). Pages are centred on the canvas with 16 epx between them. Zoom with Ctrl+wheel, pinch or the toolbar; while zooming, stretch the nearest rendered tiles so the page never goes blank (see the plan, section 4.7). The Info panel (Reading room inspector, or "Properties" from More in other presets) lists title, author, pages, page size, PDF version, file size, created date, producer, security and fonts (`D-ReadingRoom.png`).

Loading: show page outlines at the right size immediately and fill them as tiles arrive; never a spinner over the whole window. Password-protected files show the password dialog before any page appears.

### 9.3 Search and selection (`D-Search.png`)

Ctrl+F opens a find card at the top right of the canvas: query field, "2 of 14", previous and next, close; a second row with Match case and Whole word toggles and "4 pages"; then up to six results with page numbers and the match in bold, and "Show all N results". Matches on the page use `RectoSearchHitBrush`; the current match uses `RectoSearchHitCurrentBrush` with `RectoOnAccentBrush` text. In Reading room the same content lives in the inspector's Search tab and the filmstrip marks pages with hits (`explorations/B-Search.png`).

Selecting text shows a small dark toolbar above the selection: Copy, Find, Link (copy a link to this page). Right-click shows the full context menu (copy, find, select all on page, copy page link). The status of the selection is announced to screen readers.

### 9.4 Command box (Ctrl+K) (`D-Dialogs.png`, top centre)

A 560 to 720 epx card, radius 20, centred near the top. Sections: "This document" (commands for the open file), "Go to" (pages and outline entries), "Files" (recent and indexed files), "Actions" (conversions and app commands). Each row: icon, label with the typed characters in bold, shortcut on the right. Esc closes. Every toolbar and menu command must be reachable here.

### 9.5 Convert (`D-Convert.png`)

A three-step flow in one page, 980 epx wide, centred:

1. **Files**: drop zone or file picker; files appear as removable chips. PDFs and Office files can be mixed.
2. **What to make**: a grid of large option cards (Word document, Images, Plain text, One combined PDF, Split files, Protected PDF, Unlocked PDF, PDF from Office). Options that don't apply to the chosen files are disabled with a reason ("Add Word, Excel or PowerPoint files to use this"). The selected card has a 1.5 epx accent border, a 3 epx accent-tint glow and a check badge.
3. **Options**: the options for the chosen output. PDF to images: format (PNG, JPEG, TIFF, BMP), resolution (72, 150, 300, 600 dpi), pages, save location (`D-Dialogs.png`, top right). PDF to Word: pages, save location, what to do if the file exists (`explorations/B-Convert.png`). Split: method and ranges (`explorations/B-Organize.png`).

A hint line next to Back and Next says what the chosen conversion is good at. The queue is a floating card in the bottom-right corner showing running jobs with a progress ring; it expands into the full list with cancel, open folder and error actions (`explorations/A-Convert.png` shows the full list and its states: running, waiting, done, failed with "Enter password").

If Office to PDF is chosen and neither Office nor LibreOffice is installed, show the "Install Office or LibreOffice" dialog (section 9.9).

### 9.6 Organize (`D-Organize.png`)

One page grid with three modes in a segmented control: Combine, Split and Reorder. Header: file name, a summary of unsaved changes ("2 pages moved, 1 rotated"), Undo, Save as copy, Save (accent).

- **Reorder** (`D-Organize.png`): drag pages to move them; click, Ctrl+click and Shift+click to select. A dark floating action bar appears at the bottom when pages are selected: "3 pages selected", Rotate, Move to, Extract to new PDF, Delete (danger colour), clear selection. Rotated pages show a "90°" badge.
- **Combine** (`explorations/A-Organize.png`): file chips with a colour per source file, Add files, pages tinted along their bottom edge by source, drop indicator while dragging, output file name and "Save combined PDF".
- **Split** (`explorations/B-Organize.png`): click between two pages to add or remove a split; each part gets a colour, and a side panel lists parts with page ranges and editable file names, plus split methods (by ranges, every N pages, one file per page).

Leaving Organize with unsaved changes shows the "Save your changes first?" dialog (Discard, Cancel, Save).

### 9.7 Settings (`D-Layout.png`, `D-Settings.png`)

A single centred column, 760 to 900 epx, with grouped cards. Order: Layout (preset cards with mini wireframes, then the five fine-tune settings), Theme (Light, Dark, Follow Windows; pages in dark theme), Reading (default zoom, page layout, ask before opening links, remember reading position), Converting (Office to PDF converter: Automatic, Microsoft Office, LibreOffice, showing what was found; where to save converted files), Performance and privacy (page cache 128 MB to 2 GB, default 512 MB; send crash reports, off by default), Default PDF app (opens the Windows Default apps page), About. A "Find a setting" box filters rows as the user types.

### 9.8 About (`D-About.png`)

Icon, wordmark, version and architecture, one sentence on what Recto is and its license (GNU Affero General Public License, version 3 or later), a details box (Windows App SDK version, MuPDF version, source link to the exact tag), buttons View source (accent), License, Copy version info, links to report a problem, contribute and keyboard shortcuts, and a "Built with" card listing third-party components and their licenses (MuPDF, Windows App SDK, FreeType, HarfBuzz, libjpeg, OpenJPEG, zlib, Little CMS, jbig2dec, extract, Newsreader). Version strings in the mockups are placeholders.

### 9.9 Dialogs and system states (`D-Dialogs.png`)

All dialogs are `ContentDialog` restyled to radius 20 with 28 epx padding. Alerts centre their icon, title and text and stack full-width buttons (primary on top); forms align left with Cancel and the primary button side by side.

| Dialog | Content | Buttons |
|---|---|---|
| Password | Lock icon, "Enter the password", "<file> is protected.", password box. Wrong password: red border and "That password didn't work." | Open, Cancel |
| Could not open | Warning icon, "Couldn't open this file", "<file> is damaged or isn't a PDF." | OK, Copy error details |
| Office converter missing | Office icon, "Install Office or LibreOffice", why it is needed | Get LibreOffice (free), Get Microsoft 365, Not now |
| Conversion options | Section 9.5 step 3 | Cancel, "Save 9 images" (the verb and count reflect the job) |
| Link in a document | "Open this link?", the full URL in a monospace box | Cancel, Open in browser |
| Replace original | "Replace the original?", explains the protected copy | Keep both, Replace |
| Unsaved Organize changes | "Save your changes first?" | Discard, Cancel, Save |

Finished conversions raise a Windows notification ("Thesis draft v3.docx is ready", Open file, Show in folder; `explorations/A-Dialogs.png`) when the window is not focused, and an in-app toast at the bottom of the window when it is (`explorations/B-Dialogs.png`).

## 10. Keyboard

| Keys | Action |
|---|---|
| Ctrl+O | Open file |
| Ctrl+K | Command box |
| Ctrl+F, F3, Shift+F3 | Find, next match, previous match |
| Ctrl+P | Print (system print dialog) |
| Ctrl+Shift+S | Save a copy |
| Ctrl+W, Ctrl+Tab, Ctrl+Shift+Tab | Close tab, next tab, previous tab |
| Ctrl+Plus, Ctrl+Minus | Zoom in, zoom out |
| Ctrl+0, Ctrl+1 | Fit width, actual size |
| Ctrl+R, Ctrl+Shift+R | Rotate view right, left |
| Ctrl+G | Go to page |
| Home, End, Page Up, Page Down | First page, last page, previous screen, next screen |
| Ctrl+B | Show or hide the side panel |
| Ctrl+, | Settings |
| F11 | Full screen (hides the title bar and toolbar until the pointer reaches the top or bottom edge) |
| Esc | Close the topmost card, menu or dialog |

## 11. Accessibility

- Every control is reachable by keyboard in a logical order: title bar, toolbar, side panel, page.
- Focus uses the stock WinUI focus visual in the accent colour.
- Icon-only buttons have `AutomationProperties.Name` and a tooltip with the shortcut.
- Page text is exposed through a UI Automation text provider (plan milestone M3) so screen readers can read and select it.
- Hit targets are at least 32 × 32 epx (36 in the floating toolbar).
- Respect "Show animations in Windows"; when off, the toolbar does not fade and panels appear without motion.
- Colour is never the only signal: selected pages also get a check badge, errors also get an icon and text.

## 12. Motion

Panels and cards: fade and translate 8 epx over 167 ms with the standard Fluent easing. Dialogs: stock `ContentDialog` motion. Page turns: none; scrolling is continuous. Zoom: animated scale of existing tiles, then sharp tiles replace them (no flash).

## 13. Explorations and what was taken from each

| Version | Look | Adopted into D |
|---|---|---|
| A · Fluent Classic (`explorations/A-*.png`) | Cobalt, light first, rail and command bar | Light theme neutrals; the Classic preset layout; queue states; combine mode; Windows notification |
| B · Ink (`explorations/B-*.png`) | Vermilion on charcoal, Newsreader | App icon, wordmark, dark theme, banner and social preview; the Reading room preset; split mode; info panel |
| C · Focus (`explorations/C-*.png`) | Violet, floating chrome, command box | The default Focus layout, Home, the command box, the Convert flow, Reorder mode, the Settings structure |

`explorations/Main.png` is the overview board that compared the three.

## 14. Regenerating the mockups

The screenshots are generated, not drawn. To change a screen, edit its source in `mockups/source/`, then run from the repository root:

```
node design/tools/render-mockups.mjs
```

This rewrites `mockups/html/` and `screenshots/` using headless Edge (or Chrome). Add `--no-png` to rebuild only the HTML. Fonts load from Google Fonts, so the machine needs internet access for the Newsreader wordmark to render.

To rebuild the icons and graphics in `resources/` after changing a token or a shape, run `python design/tools/build_resources.py --newsreader <path to Newsreader[opsz,wght].ttf>` (details in `resources/README.md`).
