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

Use **Segoe Fluent Icons** (the system icon font) for every interface glyph, through `FontIcon` or `SymbolIcon`. The stroke icons in the mockups are placeholders that show which concept goes where; pick the matching Segoe Fluent glyph (home, open file, save, print, zoom in, zoom out, fit width, rotate, find, replace, settings, more, page, pages grid, outline list, bookmark add, two-page view, moon for page dimming, convert, combine, split, lock, unlock, certificate, verified badge, shield/sanitize, redact, highlight, underline, strikeout, sticky note, text box, callout, inking pen, shapes, stamp, form field, table grid, layers, paperclip attachment, portfolio folder, crop, resize, watermark, header/footer, compress/optimize, compare side-by-side, accessibility tag, invoice, image, text, Word document, Excel sheet, HTML code, Markdown, warning, info, check, close, add, copy, link, undo, redo, delete).

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

### 8.2 Floating page toolbar (Focus) and contextual mode bars

A single row on `RectoRaisedBrush`, radius 16, 6 epx padding, centred 22 epx above the bottom of the canvas. Order: side panel toggle | previous page, "3 / 9" (click to type a page), next page | zoom out, zoom % (click for presets: fit width, fit page, 50 to 400%), zoom in, fit width | find, rotate, annotate/tools toggle, print | **Convert** (accent button) | more (save a copy, page layout, two-page view, page colour, form designer, redact & sanitize, sign document, compare PDFs, extract tables/images, crop & page setup, watermark & header/footer, optimize PDF, properties). Toggled buttons use `RectoAccentTintBrush` with accent-coloured glyphs. The toolbar fades to 40% opacity after 3 s without pointer movement while reading and returns on hover or keyboard focus; it never hides completely.

When the user activates an authoring or inspection tool from the toolbar, side panel or `Ctrl+K`, a compact **contextual mode bar** (same `RectoRaisedBrush`, radius 16, 6 epx padding, docked directly above the floating toolbar in Focus or below the top command bar in Classic/Reading room; `Esc` closes it) appears:

1. **Markup & Annotation Bar (`Ctrl+Shift+A`):** Text markup tools (`Highlight`, `Underline`, `StrikeOut`, `Squiggly`), `Sticky Note`, `FreeText` (text box and callout with leader line), `Ink` (freehand pen and eraser), `Shapes` (rectangle, ellipse, line, arrow, polygon, polyline), `Stamp` picker (`Approved`, `Confidential`, `Draft`, `Final`, `Void`, custom text/image stamp), `Attachment Pin`, `Link` region tool, a style popover (colour swatch tokens, opacity slider, stroke width, font size), `Undo` / `Redo`, and `Flatten`. Selected annotations on the page show 8-point resize/move handles on the canvas.
2. **Form Fill & Form Designer Bar:** Interactive AcroForm (and XFA-converted) fields are fillable directly on the page canvas at all times (text boxes, checkboxes, radio buttons, combo boxes, list boxes, push buttons, signature fields, with tab-order keyboard navigation and required-field indicators). Toggling **Edit Form** opens the Form Designer bar: place new fields (`Text Field`, `Checkbox`, `Radio Button`, `Dropdown`, `List Box`, `Button`, `Signature Box`) by dragging on the page, align/resize handles, a Field Properties flyout (field name, tooltip, default value, font, read-only/required flags, calculation order), `Import Data` / `Export Data` (`JSON`, `XFDF`, `FDF`, `XML`), `Reset Form`, and `Flatten Form`.
3. **Redaction & Sanitize Bar (`Ctrl+Shift+X`):** `Mark Text / Area` tool (drag over text quads or rectangular regions to create redaction marks outlined in `RectoDangerBrush`), `Redaction Appearance` popover (fill colour and optional overlay code text such as `[REDACTED]` or FOIA exemption codes), `Preview Redacted` toggle, **`Apply Redactions`** (accent danger button that writes a new copy with underlying glyphs, vector paths, and clipped image pixels permanently stripped from content streams), and **`Sanitize Document`** (opens the Sanitize dialog to strip hidden metadata, XMP, embedded files, scripts, actions, and incremental history).
4. **Table & Image Extractor Bar:** `Select Table Region` tool (auto-detects tables on the page or lets the user draw/adjust a table bounding box and column/row dividers on the canvas), opens a live bottom preview drawer showing extracted cells, with `Export to Excel (.xlsx)`, `Export to CSV`, `Copy Table`, and `Extract Page Images`.
5. **Signature Banner & Sign Mode (`Ctrl+Shift+G`):** When a PDF contains digital signatures, a quiet status banner appears at the top of the canvas (`RectoSurfaceBrush` with `RectoSuccessBrush`, `RectoWarningBrush` or `RectoDangerBrush` badge: "Signed and all signatures are valid", "Signed with unsigned changes after signing", or "Signature verification failed") with `View Signatures` and `Sign`. Activating **Sign Document** prompts the user to drag a signature rectangle on the page (or choose an invisible signature) and opens the Sign Document dialog (section 9.9).

### 8.3 Floating side panel (Focus) and docked panels

A card 16 epx from the canvas edges, radius 16, `RectoRaisedBrush` at 90% opacity with acrylic where available. Header row: panel name, panel switcher menu, panel-specific action button, close (`Ctrl+B`). All panels share the same control instances across Focus (floating card), Classic (left dock) and Reading room (right inspector):

- **Pages:** Thumbnails with page numbers; the current page has a 2 epx accent ring and the vermilion ribbon marker on its top-right edge. Right-clicking a thumbnail offers `Insert Blank Page`, `Rotate Right/Left`, `Crop Page`, `Extract Page`, and `Delete Page`.
- **Outline (with Bookmark Editor):** Hierarchical `TreeView` of document bookmarks; the current section is highlighted with `RectoAccentTintBrush` and a 3 epx accent bar on the left. Header actions allow adding a bookmark at the current page/zoom, auto-generating an outline from document headings, renaming, nesting/reordering via drag-and-drop or keyboard, and deleting bookmarks (saved as a copy or queued job).
- **Comments (Annotations):** Lists all annotations and markup grouped by page, filterable by type, author or search text. Selecting an item scrolls to and highlights the annotation on the canvas; supports inline replies, status changes, delete, `Flatten All`, and `Import / Export XFDF or JSON`.
- **Layers (OCG):** Displays the document's Optional Content Groups (`/OCProperties`) with eye visibility toggles that re-composite the page immediately, `Reset to Default Visibility`, and `Flatten Visible Layers`.
- **Attachments & Portfolio:** Lists embedded files (`/EmbeddedFiles` and file-attachment annotations) and **PDF Portfolio (`/Collection`)** items with file icon, name, size, modified date, and description. Actions: `Open` (prompts before launching external types), `Save / Extract`, `Add File`, `Remove`, and `Save as PDF Portfolio`.
- **Signatures & Standards (PDF/A, PDF/UA, ZUGFeRD / Factur-X):**
  - *Signatures tab:* Lists every digital signature and timestamp in revision order: signer common name, issuer, signing time, reason, location, cryptographic digest status, certificate chain trust, LTV (`DSS`) status, and `View Signed Revision`.
  - *Standards & Accessibility tab:* Runs on-demand validation for **PDF/A** (`1a/1b/2a/2b/2u/3a/3b/3u`) and **PDF/UA-1 (ISO 14289)** accessibility rules, displaying a pass/fail checklist (clicking a failure highlights the page/element), an interactive **Structure Tag Tree (`/StructTreeRoot`)** inspector where users can inspect/auto-tag semantic elements (`H1`-`H6`, `P`, `Table`, `Figure`) and edit `/Alt` text, plus a one-click `Convert to PDF/A` action.
  - *E-Invoice (ZUGFeRD / Factur-X) tab:* Shown automatically (or via inspector) when a PDF/A-3 document contains an embedded `factur-x.xml` or `zugferd-invoice.xml` electronic invoice; renders the structured invoice data (Invoice #, Issue Date, Seller, Buyer, Currency, Line Items table, Tax breakdown, Grand Total) alongside the visual PDF page, with `Export XML` and `Validate EN 16931`.

## 9. Screens

### 9.1 Home (`D-Home.png`)

Purpose: open something fast. Centre column: the question "What do you want to open?", one line of help, and the command box expanded inline (720 epx wide). Typing filters actions and files together; arrow keys move, Enter chooses. Footer of the box: key hints and "Browse files" (accent). Below: "Recent" as six document covers with name and last-opened day, and "See all". Dropping files anywhere in the window opens them; a full-window drop overlay appears while dragging. Reading position is remembered, so a recent file opens at the page where the user stopped.

Empty state (no recent files): hide the Recent section and show "Drop a PDF anywhere in this window" under the box.

### 9.2 Viewer (`D-Focus-Light.png`, `D-Focus-Dark.png`, `D-Classic.png`, `D-ReadingRoom.png`)

Continuous vertical scroll by default; single page and two-page spread are options (two-page shows the cover alone, then pairs). Pages are centred on the canvas with 16 epx between them. Zoom with Ctrl+wheel, pinch or the toolbar; while zooming, stretch the nearest rendered tiles so the page never goes blank (see the plan, section 4.7).

The **Info, XMP & Viewer Preferences panel** (Reading room inspector, or "Properties" from More in other presets) lists title, author, subject, keywords, pages, page size (in mm, inches and pt), PDF version, PDF/A or PDF/UA conformance badge, file size, created and modified dates, creator, producer, linearized (Fast Web View) status, security/permissions summary, and embedded font inventory (`D-ReadingRoom.png`). Clicking **Edit Info & Preferences** lets the user edit `/Info` fields, custom XMP metadata properties, and `/ViewerPreferences` (`DisplayDocTitle`, `FitWindow`, `CenterWindow`, `HideToolbar`, `HideMenubar`, initial `PageMode` and `PageLayout`, initial open page) and save to a copy.

Loading: show page outlines at the right size immediately and fill them as tiles arrive; never a spinner over the whole window. Password-protected files show the password dialog before any page appears.

### 9.3 Search, Find & Replace, Selection, and Document Comparison (`D-Search.png`)

`Ctrl+F` opens a find card at the top right of the canvas: query field, "2 of 14", previous and next, close; a second row with `Match case`, `Whole word` and `Regex` (regular expression) toggles and "4 pages"; then up to six results with page numbers and the match in bold, and "Show all N results". Matches on the page use `RectoSearchHitBrush`; the current match uses `RectoSearchHitCurrentBrush` with `RectoOnAccentBrush` text. In Reading room the same content lives in the inspector's Search tab and the filmstrip marks pages with hits (`explorations/B-Search.png`).

Pressing `Ctrl+H` (or clicking the **Replace** chevron in the find card) expands a **Find & Replace** row beneath the query field: a `Replace with` text box, `Replace` (replaces the current match and advances) and `Replace All` (replaces every match across the page or document, refluxing glyph spacing in the content stream and saving via the atomic copy flow).

Selecting text shows a small dark toolbar above the selection: `Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link` (copy a link to this page). Right-click shows the full context menu (`Copy`, `Copy with Formatting`, `Highlight`, `Underline`, `Strikeout`, `Add Note`, `Mark for Redaction`, `Find`, `Select all on page`, `Copy page link`, `Export Selection as Table`). The status of the selection is announced to screen readers.

#### 9.3.1 Compare PDFs (`Ctrl+Shift+D`)

Choosing **Compare PDFs** opens a comparison view within the active tab (or a dedicated Compare tab): a header bar picks Document A (original) and Document B (revised), switches between **Side-by-Side Synchronized View** (two `DocumentView` canvases scrolling and zooming in lockstep) and **Visual Overlay Diff View** (blending both pages with additions in `RectoSuccessBrush` tint and deletions in `RectoDangerBrush` tint), and opens a **Changes panel** on the right listing every text insertion, deletion, replacement, and graphic change by page number. Clicking any change jumps both canvases to the exact quad.

### 9.4 Command box (Ctrl+K) (`D-Dialogs.png`, top centre)

A 560 to 720 epx card, radius 20, centred near the top. Sections: "This document" (commands for the open file, including annotate, form edit, sign, redact, sanitize, watermark, crop, optimize, compare, compliance check), "Go to" (pages, outline entries, comments, form fields), "Files" (recent and indexed files), "Actions" (conversions and app commands). Each row: icon, label with the typed characters in bold, shortcut on the right. Esc closes. Every toolbar and menu command must be reachable here.

### 9.5 Convert (`D-Convert.png`)

A three-step flow in one page, 980 epx wide, centred:

1. **Files**: drop zone or file picker; files appear as removable chips. PDFs, Office files (`.docx`, `.rtf`, `.doc`, `.xlsx`, `.csv`, `.xls`, `.pptx`, `.ppt`), web/markup files (`.html`, `.md`, `.xml`, `.tex`), `.xps`, `.svg`, and images (`.png`, `.jpg`, `.tiff`, `.bmp`, `.gif`) can be mixed.
2. **What to make**: a grid of large option cards:
   - **Word document (`.docx`, `.rtf`)**
   - **Excel & Data (`.xlsx`, `.csv`, `.xml`)**
   - **HTML5, Markdown, EPUB & LaTeX (`.html`, `.md`, `.epub`, `.tex`)**
   - **Images, Multipage TIFF, SVG & XPS (`.png`, `.jpg`, `.tiff`, `.bmp`, `.gif`, `.svg`, `.xps`)**
   - **Searchable PDF (OCR)**
   - **Plain text (`.txt`)**
   - **Extract Tables & Images**
   - **PDF/A & E-Invoice (ZUGFeRD / Factur-X)**
   - **Optimize & Compress PDF**
   - **One combined PDF**
   - **Split files**
   - **Protected & Signed PDF**
   - **Unlocked PDF**
   - **PDF from Office, HTML, Markdown & Images**
   Options that don't apply to the chosen files are disabled with a reason ("Add Word, Excel, PowerPoint, HTML or Markdown files to use this"). The selected card has a 1.5 epx accent border, a 3 epx accent-tint glow and a check badge.
3. **Options**: the options for the chosen output:
   - *PDF to Word (`.docx` / `.rtf`):* Output format (`.docx` or `.rtf`), Reconstruction mode (**Flowing** paragraphs/headings/lists/columns for natural editing, **Exact** positioned frames preserving fixed coordinates, or **Continuous** flow without page breaks), Reconstruct tables from vector ruling lines and aligned grids toggle, Image handling (`Keep all`, `Skip images`, target DPI), Font embedding/mapping, OCR mode (`Automatic on scanned pages`, `Force OCR on all pages`, `OCR vector-outlined text curves`, `Off`), pages, save location, file-exists rule (`explorations/B-Convert.png`).
   - *PDF to Excel (`.xlsx` / `.csv` / `.xml`):* Output format, Sheet layout (`One sheet per table`, `One sheet per page`, `Single combined sheet`), Detect borderless aligned tables toggle, OCR scanned tables toggle, pages, save location.
   - *PDF to HTML5 / Markdown / EPUB / LaTeX:* Output format (`.html`, `.md`, `.epub`, `.tex`), HTML layout (`Flowing responsive` or `Fixed exact`), Asset mode (`Single self-contained HTML` or `Folder with external CSS/images/fonts`), pages, save location.
   - *PDF to Images / Multipage TIFF / SVG / XPS:* Format (`PNG`, `JPEG`, `Single-page TIFF`, `Multipage TIFF`, `BMP`, `GIF`, `Animated GIF`, `SVG`, `XPS`), resolution (`72`, `150`, `300`, `600`, `1200` dpi), colour mode (`Colour`, `Grayscale`, `Monochrome 1-bit`), TIFF compression (`LZW`, `CCITT Group 4`, `JPEG`), pages, save location (`D-Dialogs.png`, top right).
   - *Extract Tables & Images:* What to extract (`All embedded images`, `Vector graphics as SVG`, `All detected tables as .xlsx/.csv`), minimum image dimension filter (`Skip icons smaller than 32 x 32 px`), pages, save folder.
   - *Searchable PDF (OCR):* OCR engine (`Hybrid High-Accuracy Neural + Windows OCR` default, `High-Accuracy Neural OCR`, `Windows Fast OCR`), Language (Installed Windows OCR and neural recognition packs), Deskew & enhance contrast toggle, **OCR vector-outlined text** toggle, pages, save location.
   - *PDF/A & E-Invoice (ZUGFeRD / Factur-X):* Conformance level (`PDF/A-1b`, `1a`, `2b`, `2u`, `2a`, `3b`, `3u`, `3a`), Embed ZUGFeRD / Factur-X EN 16931 XML toggle (pick existing XML or fill basic invoice header/line items), save location.
   - *Optimize & Compress PDF:* Preset (`High quality`, `Balanced`, `Smallest file`, `Custom`), Image downsampling DPI and JPEG/Flate quality, Subset fonts & deduplicate streams toggle, Strip metadata/annotations toggle, **Linearize for Fast Web View** toggle, live estimated size reduction preview, save location.
   - *Protected & Signed PDF:* Encrypt with `AES-256` passwords and permission flags (print, copy, edit, annotate, fill forms), and/or apply an X.509 Digital Signature (`.pfx` or Windows Certificate Store, PAdES, optional RFC 3161 timestamp server URL).
   - *PDF from Office, HTML, Markdown & Images:* Page size (`Original`, `A4`, `Letter`), orientation, margins, HTML/Markdown CSS print styling toggle, save location.
   - *Split:* method and ranges (`explorations/B-Organize.png`).

A hint line next to Back and Next says what the chosen conversion is good at. The queue is a floating card in the bottom-right corner showing running jobs with a progress ring; it expands into the full list with cancel, open folder and error actions (`explorations/A-Convert.png` shows the full list and its states: running, waiting, done, failed with "Enter password").

Office (`.docx`, `.docm`, `.xlsx`, `.xlsm`, `.pptx`, `.pptm`), legacy binary Office (`.doc`, `.xls`, `.ppt`), RTF, CSV, HTML, Markdown, XPS, SVG, and Image to PDF all run 100% natively in-tree via Recto's C++20 OpenXML, OLE2 Compound File (`[MS-CFB]`/`[MS-DOC]`/`[MS-XLS]`/`[MS-PPT]`), RTF, HTML/CSS, Markdown, XPS, and SVG layout engines with zero external dependencies (never requiring Microsoft Office or LibreOffice).

### 9.6 Organize (`D-Organize.png`)

One page grid with three primary modes in a segmented control (**Combine**, **Split** and **Reorder**) plus a suite of **Page & Document Tools** in the header bar (`Insert Blank Page`, `Crop & Resize`, `Header / Footer & Bates`, `Watermark`, `Sign`, `Sanitize`, `Flatten`). Header: file name, a summary of unsaved changes ("2 pages moved, 1 rotated, watermark added"), Undo, Redo, Save as copy, Save (accent).

- **Reorder** (`D-Organize.png`): drag pages to move them; click, Ctrl+click and Shift+click to select. A dark floating action bar appears at the bottom when pages are selected: "3 pages selected", `Rotate`, `Insert Blank`, `Crop / Resize`, `Move to`, `Extract to new PDF`, `Delete` (danger colour), clear selection. Rotated pages show a "90°" badge; cropped pages show a crop badge.
- **Combine** (`explorations/A-Organize.png`): file chips with a colour per source file, Add files, pages tinted along their bottom edge by source, drop indicator while dragging, output file name, bookmark generation option, and "Save combined PDF".
- **Split** (`explorations/B-Organize.png`): click between two pages to add or remove a split; each part gets a colour, and a side panel lists parts with page ranges and editable file names, plus split methods (by ranges, every N pages, one file per page, or by top-level bookmark).
- **Page & Document Tools (dialogs with live page preview over the working copy):**
  - *Insert Blank Page:* Pick page size (match selected page, `A4`, `Letter`, custom), orientation, count, and insertion position (before/after selection, start, end).
  - *Crop & Resize Pages:* Interactive crop handles over the page preview adjusting `/CropBox`, `/MediaBox`, `/BleedBox` or `/TrimBox` in `mm`/`in`/`pt`, auto-crop white margins button, or scale/resize page dimensions to a target paper size (`A4`, `Letter`, `Legal`, custom), applied to selected pages, odd/even pages, or all pages.
  - *Header, Footer, Page Numbers & Bates Numbering:* Six-zone editor (Top-Left, Top-Centre, Top-Right, Bottom-Left, Bottom-Centre, Bottom-Right) with live preview, one-click tokens (`Page X of Y`, Roman/Arabic numerals, Date, File Name), font/size/colour/margin controls, and a **Bates Numbering** tab (prefix, suffix, start number, digit padding across single or multiple files).
  - *Watermark & Stamp:* Text, image, or PDF-page watermark with live preview, rotation angle (`-90°` to `+90°`), opacity slider, scale, position grid, layer (`Behind page content` or `Over page content`), and page range.

Leaving Organize with unsaved changes shows the "Save your changes first?" dialog (Discard, Cancel, Save).

### 9.7 Settings (`D-Layout.png`, `D-Settings.png`)

A single centred column, 760 to 900 epx, with grouped cards. Order: Layout (preset cards with mini wireframes, then the five fine-tune settings), Theme (Light, Dark, Follow Windows; pages in dark theme), Reading (default zoom, page layout, ask before opening links, remember reading position, automatic OCR on scanned pages, highlight form fields), Converting (Default Office to PDF page setup: Match original document, A4, Letter; default PDF to Word reconstruction mode: Flowing, Exact, Continuous; OCR engine: Hybrid High-Accuracy Neural + Windows OCR, High-Accuracy Neural OCR, Windows Fast OCR; OCR language; where to save converted files), Signatures and security (verify signatures when opening, default timestamp server URL), Performance and privacy (Portable mode toggle: "Store settings, recent files, cache and temporary files in a data folder next to Recto.exe", migrating existing settings and recents when toggled; page cache 128 MB to 2 GB, default 512 MB; save local crash report bundles, off by default), Default PDF app (opens the Windows Default apps page), About. A "Find a setting" box filters rows as the user types.

### 9.8 About (`D-About.png`)

Icon, wordmark, version and architecture, one sentence on what Recto is and its license (GNU General Public License, version 3 or later), a details box (Windows App SDK version, `recto-pdf` engine version, source link to the exact tag), buttons View source (accent), License, Copy version info, links to report a problem, contribute and keyboard shortcuts, and a "Built with" card listing components and their licenses (`recto-pdf` native C++20 engine, `recto-core` High-Accuracy Neural OCR + OpenXML/OLE2 layout engines, Windows App SDK, Windows Platform Direct2D/DirectWrite/WIC/CNG/`Windows.Media.Ocr`, Newsreader). Version strings in the mockups are placeholders.

### 9.9 Dialogs and system states (`D-Dialogs.png`)

All dialogs are `ContentDialog` restyled to radius 20 with 28 epx padding. Alerts centre their icon, title and text and stack full-width buttons (primary on top); forms align left with Cancel and the primary button side by side.

| Dialog | Content | Buttons |
|---|---|---|
| Password | Lock icon, "Enter the password", "<file> is protected.", password box. Wrong password: red border and "That password didn't work." | Open, Cancel |
| Could not open | Warning icon, "Couldn't open this file", "<file> is damaged or isn't a PDF." | OK, Copy error details |
| Conversion options | Section 9.5 step 3 | Cancel, "Save 9 images" (the verb and count reflect the job) |
| Link in a document | "Open this link?", the full URL in a monospace box | Cancel, Open in browser |
| Sign document | Certificate picker (`.pfx`/`.p12` file or Windows Certificate Store), appearance preview (Type, Draw, or Image + signer details), Reason, Location, Timestamp server URL, PAdES level | Cancel, Sign and save copy |
| Apply redactions & sanitize | Warning icon, summary of marked redactions and hidden items to strip (metadata, XMP, embedded files, actions, incremental revisions) | Cancel, Apply and save copy |
| Watermark / Header & Footer / Bates | Two-column form: settings on the left (text/image, tokens, font, opacity, rotation, margins, page range) and a live rendered page preview on the right | Cancel, Apply |
| Crop & resize pages | Interactive crop box over live page preview, box selector (`CropBox`, `MediaBox`, `BleedBox`, `TrimBox`), margin inputs (`mm`/`in`/`pt`), auto-crop white margins, or target paper size | Cancel, Apply |
| Optimize & compress PDF | Preset selector, image DPI/quality controls, font subsetting, stream deduplication, Fast Web View linearization toggle, and estimated before/after size bar | Cancel, Save optimized copy |
| Replace original | "Replace the original?", explains the protected copy | Keep both, Replace |
| Unsaved Organize changes | "Save your changes first?" | Discard, Cancel, Save |

Finished conversions raise a Windows notification ("Thesis draft v3.docx is ready", Open file, Show in folder; `explorations/A-Dialogs.png`) when the window is not focused, and an in-app toast at the bottom of the window when it is (`explorations/B-Dialogs.png`).

## 10. Keyboard

| Keys | Action |
|---|---|
| Ctrl+O | Open file |
| Ctrl+K | Command box |
| Ctrl+F, F3, Shift+F3 | Find, next match, previous match |
| Ctrl+H | Find and replace text |
| Ctrl+P | Print (system print dialog) |
| Ctrl+S, Ctrl+Shift+S | Save changes (to copy or prompt), Save a copy |
| Ctrl+Z, Ctrl+Y | Undo, Redo (in Organize, Markup, Form Editor, Redaction) |
| Ctrl+Shift+A | Toggle Markup and Annotation bar |
| Ctrl+Shift+X | Toggle Redaction and Sanitize bar |
| Ctrl+Shift+G | Sign document |
| Ctrl+Shift+D | Compare PDFs |
| Ctrl+W, Ctrl+Tab, Ctrl+Shift+Tab | Close tab, next tab, previous tab |
| Ctrl+Plus, Ctrl+Minus | Zoom in, zoom out |
| Ctrl+0, Ctrl+1 | Fit width, actual size |
| Ctrl+R, Ctrl+Shift+R | Rotate view right, left |
| Ctrl+G | Go to page |
| Home, End, Page Up, Page Down | First page, last page, previous screen, next screen |
| Ctrl+B | Show or hide the side panel |
| Ctrl+, | Settings |
| F11 | Full screen (hides the title bar and toolbar until the pointer reaches the top or bottom edge) |
| Esc | Close the topmost card, mode bar, menu or dialog |

## 11. Accessibility

- Every control is reachable by keyboard in a logical order: title bar, toolbar, active mode bar, side panel, page (including interactive form fields and annotations in reading/tab order).
- Focus uses the stock WinUI focus visual in the accent colour.
- Icon-only buttons have `AutomationProperties.Name` and a tooltip with the shortcut.
- Page text, interactive form fields, and tagged PDF structure (`/StructTreeRoot` when present) are exposed through UI Automation providers (plan milestone M3) so screen readers can read, navigate headings/tables, fill forms, and select text.
- Hit targets are at least 32 × 32 epx (36 in the floating toolbar and mode bars).
- Respect "Show animations in Windows"; when off, the toolbar does not fade and panels appear without motion.
- Colour is never the only signal: selected pages also get a check badge, signature states and errors also get an icon and text.

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
