# Parity ledger: Adobe Acrobat Pro, Foxit PDF Editor, PDFelement and UPDF

Foundation plan D18 (owner decisions, 2026-10-21): Recto matches Adobe Acrobat Pro tool by tool, and also every feature of Foxit PDF Editor Pro, Wondershare PDFelement and UPDF for Windows that Acrobat lacks, without removing or weakening any Recto improvement. This ledger maps every tool to the TODO sections that build it in Recto, or to a recorded exclusion with its reason. A tool with no owner here is a plan defect: file it through `add-todo` and add its row. Review this ledger with `docs/user-demand.md` before each release (`D07 T03 §6`).

Status: **Planned** means sections own it; **Partial** means part of the tool is owned and the rest is named; **Excluded** means a recorded decision not to build it, with the reason. "Recto keeps" names the improvement that parity must not remove.

Last reviewed: 2026-10-21, against the Acrobat Pro "All tools" list, the Foxit PDF Editor 2026.1 user manual and product pages, PDFelement's feature and guide pages, and UPDF's Windows tech specification and release notes.

## Create, combine and organize

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Create PDF from files (Office, images, HTML, text) | Planned | D04 T04 §1-§22, D04 T02 §5, §23 | Native engines, no Office or LibreOffice; legacy `.doc`, `.xls`, `.ppt`; OpenDocument, OFD, email, MHTML, Markdown, RTF, XPS, SVG |
| Create PDF from scanner | Planned | D04 T02 §13 (owner decision on timing) | Local WIA, optional local OCR |
| Create PDF from clipboard | Planned | D04 T02 §14-§16 | |
| Create a blank PDF | Planned | D04 T02 §24-§25 | Paper styles (lined, grid, dot, music) |
| Create PDF from a web page | Excluded | | Network fetches are off by design (plan section 9); a saved HTML or MHTML file converts through D04 T04 §3, §19 |
| Print to PDF (virtual printer) | Planned | D07 T01 §15-§20 (driver signing is third-party) | Full installer only; nothing leaves the machine |
| Combine files | Planned | D05 T01 §1-§2, §30, D05 T02 §3, §24, D07 T01 §9 | Explorer multi-select Combine; binder projects with a contents page |
| Organize pages (insert, delete, extract, rotate, reorder, split) | Planned | D05 T01 §2-§4, §15-§21, D05 T02 §1-§5, §16-§23 | Copy-based edits; never in place |
| Split by file size or by top-level bookmarks | Planned | D05 T01 §15, D05 T02 §16 | |
| Page labels | Planned | D05 T01 §19, D05 T02 §20 | |
| Replace pages | Planned | D05 T01 §14, D05 T02 §12 | |
| Crop and page boxes | Planned | D05 T01 §4, D05 T02 §6 | |
| PDF Portfolio | Planned | D02 T03 §8 | Attachments ask before opening |

## Edit and enrich

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Edit PDF: text with reflow, add text | Planned | D03 T04 §1-§7, §11-§12, §14-§24 | Edits on a copy; original font kept per glyph; never `?` |
| Edit PDF: images and vector objects | Planned | D03 T04 §8-§10, §13, §25-§30 | |
| Spell check | Planned | D03 T01 §14, D03 T04 §17, D03 T03 §4, §8 | Offline Windows spell checker |
| Find and replace text | Planned | D03 T01 §9 | Shares the Edit PDF rewrite engine |
| Links, destinations, create links from URLs | Planned | D03 T03 §11-§14, §30-§31 | Launch and JavaScript actions never written or run (D9) |
| Article threads | Planned | D03 T03 §15-§16 | |
| Bookmarks, including from styles, searches and tags | Planned | D02 T03 §5-§6, §14-§22 | Heading-based auto-generation |
| Headers, footers, watermarks, Bates numbering | Planned | D05 T01 §5-§6, D05 T02 §7-§8 | Managed marks can be listed and removed |
| Backgrounds | Planned | D05 T01 §13, D05 T02 §13 | |
| Attachments | Planned | D02 T03 §8 | |
| Layers (optional content), view and edit | Planned | D02 T03 §7, §23-§25 | Edits on a copy |
| Document properties, metadata, initial view | Planned | D02 T03 §9-§10 | |
| Rich media (audio, video), play and add | Planned | D02 T05 §1-§4, §16-§18 | Ask before play; no Flash, no scripts, no network |
| 3D models | Partial | D02 T05 §5-§10 | Bounded, fuzzed parsers; 3D JavaScript never runs. Not covered: PRC B-rep parts without stored tessellation (listed, drawn as bounding boxes), PRC compressed tessellation (poster fallback with the reason), and U3D block kinds outside the listed set (skipped and noted); B-rep tessellation needs its own section when asked for |
| Geospatial PDF | Planned | D02 T05 §11-§14 | |
| JavaScript (document, field, 3D) | Excluded | | Zero PDF JavaScript by construction (D9); forms that need scripts get a banner (D03 T03 §7) |

## Comment, fill and sign

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Comments and markup (all annotation types) | Planned | D03 T03 §1-§5, §45-§51 | |
| Review status, summarize comments, export comments | Planned | D03 T03 §23-§29 | |
| Stamps, custom and dynamic | Planned | D03 T03 §20-§21, §52-§55 | Dynamic tokens resolved without JavaScript |
| Measure (distance, perimeter, area) | Planned | D03 T03 §17-§19, §48, §51 | |
| Fill and Sign (flat forms) | Planned | D03 T03 §22, D05 T02 §10 | Unicode-safe typed text; Lock on save only on a copy |
| Prepare form, fill forms, automatic field detection | Planned | D03 T03 §6-§10, §32-§39 | XFA to AcroForm conversion; barcode fields filled by Recto's own code, never PDF JavaScript |
| Combine form data into a spreadsheet | Planned | D03 T03 §40-§41, D04 T07 §21 | |
| Digital signatures: sign, certify, validate, timestamp, LTV | Planned | D05 T01 §8-§9, §28-§29, D05 T02 §10-§11, §30-§32 | Windows certificate store; verdicts never "valid" when unknown |
| Request e-signatures (Acrobat Sign) | Planned, phase 98 | D09 T04, D09 T05 | Recto Cloud (D20): simple electronic signatures with a hash-chained audit trail and a PAdES seal; the desktop stays offline until the user signs in (D16) |
| Shared review, Send for comments | Planned, phase 98 | D09 T03, D09 T05 | Recto Cloud (D20) live reviews in Recto's annotation model; XFDF and JSON file exchange remains (D03 T03 §2, §5) |

## Protect and standards

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Password protection (AES-256) and permissions | Planned | D05 T01 §7, D06 T02 §7-§9 (FIPS policy) | |
| Certificate encryption | Planned | D05 T01 §11-§12, D05 T02 §14-§15 | |
| Redact, redaction profiles, whole pages | Planned | D05 T01 §10, §24-§26, D05 T02 §9, §27-§28 | True redaction proven on raw bytes (D06 T03 §6); whiteout is labelled as not redaction |
| Sanitize, Remove hidden information, inspect document | Planned | D05 T01 §10, §27, D05 T02 §9, §29 | |
| Compress, Optimize, audit space usage | Planned | D04 T02 §11-§12 | |
| Fast Web View (linearize) | Planned | D04 T02 §11 | |
| PDF/A validate and convert | Planned | D04 T05 §5-§6 | |
| PDF/UA check, autotag, PDF/UA-2 | Planned | D04 T05 §7, §23-§24 | Never "conformant" without a complete requirement inventory |
| PDF/X validate and convert | Planned | D04 T06 §4-§10 | |
| PDF/E and PDF/VT identify and preflight | Partial | D04 T06 §17 | Partial preflight profiles for PDF/E-1, PDF/VT-1 and PDF/VT-2 that report what was and was not checked, never "conformant"; PDF/VT-3 (on PDF/X-6) is identified only; conversion is not planned until requested (`docs/user-demand.md`) |
| E-invoices (ZUGFeRD, Factur-X) | Recto only | D04 T05 §8 | Beyond Acrobat |

## Accessibility

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Accessibility checker, autotag, colour contrast, WCAG report | Planned | D04 T05 §7, §22-§27 | |
| Tags panel editing, alternate text, formulas as MathML | Planned | D04 T05 §14, §16, §18-§21 | |
| Reading Order tool | Planned | D04 T05 §15, §17 | |
| Reflow view | Planned | D03 T02 §10-§12 | |
| Read Out Loud, including form fields | Planned | D03 T02 §7-§9 | Local Windows voices |
| Screen reader support of the app | Planned | D03 T02 §1-§6 | UIA text provider, keyboard audit |

## Scan, export and convert

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Scan and OCR, editable text from scans | Planned | D03 T01 §7-§8, §15-§19, D04 T02 §7, §19-§20 | Fully local OCR, no cloud |
| Enhance scans (deskew, cleanup) | Planned | D03 T01 §7 | |
| Export to Word and RTF | Planned | D04 T03 §1-§6 | Three layout modes |
| Export to Excel | Planned | D04 T05 §1-§2 | CSV and XML too |
| Export to PowerPoint | Planned | D04 T05 §10-§13 | |
| Export to images, text, HTML | Planned | D04 T02 §3-§6, §17-§18, §21-§22, D04 T05 §3 | Markdown, EPUB, LaTeX, SVG, XPS beyond Acrobat; one long image; image-only PDF |

## Print and print production

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Print, page ranges, scaling, duplex, print options | Planned | D07 T01 §1-§3, §11-§14 | Driver isolation, paper from the driver |
| Booklet, multiple pages per sheet, poster | Planned | D07 T01 §10, D05 T01 §17 (N-up as a file) | |
| Preflight and fixups, greyscale conversion | Planned | D04 T06 §2-§9, §13, §15, §18-§19 | Never compliant for unanalysed content |
| Output Preview, separations, ink manager, colour warnings | Planned | D04 T06 §11-§12, §14 | |

## Search, compare, automate

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Find in document | Planned | D03 T01 §5-§6 | Regex; accent-insensitive by default |
| Advanced Search across files, search index | Planned | D03 T05 §1-§9 | |
| Compare files | Planned | D03 T01 §10 | |
| Action Wizard | Planned | D04 T07 §1-§24 | Never overwrites inputs; no scripts |
| Custom tool sets, toolbar customization | Partial | D02 T04 §3, D02 T03 §12 | The `Ctrl+K` command box and the `Custom` layout preset stand in for Acrobat's custom tool sets |
| Command line and agent automation | Recto only | D01 T03 §1-§3 and every `recto-cli` verb, D04 T08 (`recto-mcp`) | Acrobat has none; no in-process plug-ins (D21) |

## Views and reading

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Page display, zoom, rotate view, full screen, read mode | Planned | D02 T02 §5-§10 | Three layout presets (D17), dark page modes |
| Full screen presentation with transitions | Planned | D02 T02 §12-§17 | Presenter view on a second display |
| Automatic scrolling | Planned | D02 T02 §18 | |
| Rulers, grids, guides, line weights | Planned | D02 T02 §20-§22 | |
| Snapshot tool | Planned | D03 T01 §11-§13 | |
| Tabs, multiple windows, split view | Planned | D02 T01 §3, §13-§16, D02 T02 §23-§24 | Tab tear-off into its own window stays recorded in `docs/user-demand.md` |
| Recent files, resume reading position | Planned | D02 T03 §1-§2 | |
| Share a file | Planned | D02 T01 §17 | Windows Share contract, nothing sent by Recto |

## Cloud and mobile

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| AI Assistant (summarize, ask, generative features) | Planned, opt-in | D08 T01 §1-§19, D08 T02 §1-§35 | Off by default; the user's own Anthropic, OpenAI, OpenRouter or Gemini key, or a local model; preview before every request (D19) |
| Document Cloud storage, sharing links | Planned, phase 98 | D09 T01, D09 T02, D09 T05 | Recto Cloud (D20), AGPL and self-hostable on `workerd`; opt-in from the desktop |
| Third-party cloud storage (OneDrive, SharePoint, Google Drive, Dropbox, Box) | Planned, opt-in | D02 T06 §1-§15 (provider registrations are third-party) | Off by default; lockable by Group Policy (D21) |
| Web app | Planned, phase 98 | D09 T02 | Scriptless `pdf.js` viewer (D9) |
| Mobile apps | Excluded | | Recto is a Windows desktop app (D15); the Recto Cloud web app works in mobile browsers |
| Admin deployment and managed settings | Planned | D07 T02 §8-§12 | Group Policy templates; no usage reporting (D16) |

## Beyond Acrobat: Foxit PDF Editor, PDFelement and UPDF

F = Foxit PDF Editor, W = Wondershare PDFelement, U = UPDF.

| Competitor feature | Products | Status | Recto sections |
| --- | --- | --- | --- |
| Reading tints (sepia, eye-care, custom page colour) | U, W | Planned | D02 T02 §11 |
| Reverse page order view, right-to-left spreads | F, W | Planned | D02 T02 §19 |
| Restore tab session, reopen closed tab, close tabs left or right, reorder tabs | U | Planned | D02 T01 §13-§16 |
| Word and character count | F, W | Planned | D02 T03 §13 |
| Bookmark import and export, contents page from bookmarks, bookmark styles | F, U | Planned | D02 T03 §18-§22 |
| Scrolling capture across pages | W | Planned | D03 T01 §13 |
| OCR suspects review and correction, zone OCR | F, W | Planned | D03 T01 §15-§19 |
| Vertical CJK text editing | W | Partial | D03 T04 §18-§20 (tate-chu-yoko, ruby and fonts without vertical data are refused with the reason) |
| Convert text to outlines | F, W | Planned | D03 T04 §21, D07 T01 §12 (when printing) |
| Linked text boxes, join and split text, move objects across pages | F, U | Planned | D03 T04 §22-§24, §27, §29 |
| Image filters, blend modes, shading objects | F | Planned | D03 T04 §25-§26, §28 |
| QR code, Data Matrix and PDF417 on pages and as form fields | W, F | Planned | D03 T03 §32-§35 |
| Image field and date picker | U, F, W | Planned | D03 T03 §38-§39 |
| Pen pressure and touch mode | F | Planned | D03 T03 §42-§44 |
| Cloud and arc markups, group markups, circle measurement | F, W | Planned | D03 T03 §45-§51 |
| Sticker packs | U, W | Planned | D03 T03 §52-§53 |
| Accounting tickmarks and calculator stamps | F | Planned | D03 T03 §54-§55 |
| PDF to one long image, image-only PDF | U, W | Planned | D04 T02 §3, §17-§18 |
| More image inputs (ICO, JFIF, DIB, HEIC, WebP, JPEG XL through installed codecs) | W | Planned | D04 T02 §23 |
| OpenDocument, OFD, email (`.eml`, `.msg`), MHTML and plain-text family to PDF | W, F | Planned | D04 T04 §8-§22 |
| Split one page into several, N-up as a new file | U, W, F | Planned | D05 T01 §16-§17, D05 T02 §17-§18 |
| Interleave scans, swap odd and even, reverse | U, W, F | Planned | D05 T01 §18, D05 T02 §19 |
| Detect and delete blank pages | W, U | Planned | D05 T01 §20, D05 T02 §21, D04 T07 §18 |
| Drag pages between documents and to Explorer, copy a page as an image | U | Planned | D05 T02 §22-§23 |
| Reorder pages by bookmarks | F | Planned | D05 T01 §21 |
| PDF Binder | F | Planned | D05 T01 §30, D05 T02 §24 |
| ID card copy | U | Planned | D05 T01 §23, D05 T02 §25 |
| Cross-page paging seal | W | Planned | D05 T01 §22, D05 T02 §26 |
| PII detection country packs | W, F | Planned | D05 T01 §25, D05 T02 §27 |
| Trust list updates (EU trusted lists) | F | Partial | D05 T01 §29, D05 T02 §31 (opt-in download or manual import; the list's XML signature is not verified in v1 and the UI says so) |
| USB signature pads | F | Partial | D05 T02 §32 (pen tablets and Windows Ink screens; vendor-SDK LCD pads are not supported) |
| Batch steps: print, crop, background, remove marks, insert a PDF, greyscale, PDF/UA | U, W, F | Planned | D04 T07 §15-§24 |
| FIPS mode | F | Planned | D06 T02 §7-§9 |
| Shipped interface translations | U, W, F | Planned | D07 T02 §13-§17 (first set of ten languages; reviewers are third-party) |
| AI: local models, whole-document translation, rewrite and proofread as edits, multi-document chat, semantic search, natural-language commands | U, W, F | Planned, opt-in | D08 T02 §1-§23 |
| AI: mind maps, flashcards and quizzes, contract review, AI-content estimate, image generation, audio summaries, PII suggestions | U, W, F | Planned, opt-in | D08 T02 §24-§35 |
| Signature and stamp sync across devices | U, W | Planned, phase 98 | D09 T05 §19-§20 |
| QR code for share links | U, W | Planned, phase 98 | D09 T02 §22 |
| E-sign templates, bulk send, web form links, in-person signing | U, W, F | Planned, phase 98 | D09 T04 §23-§29 |
| MCP or agent control of the app | W, F | Planned | D04 T08 §1-§12 |
| Page-flip 3D book view | W | Excluded | Presentation only; Recto's two-page and presentation views cover reading |
| Screen recording | W | Excluded | Not a PDF task; Windows ships a screen recorder |
| Visio, Project, Publisher, DWG and CAJ inputs | W, F, U | Excluded | Proprietary formats without a public specification or with a large engine; no Office or Visio runtime (D7, D10) |
| Password-protected OpenDocument | W | Excluded | Refused with a clear message (D04 T04 §8) until a user asks |
| Plug-in platform, COM and VBA automation | F | Excluded | No third-party code inside the app (D21); automation goes through `recto-cli` and `recto-mcp` |
| Document JavaScript, JS console, ODBC, page templates spawned by scripts | F | Excluded | Zero PDF JavaScript (D9) |
| Vendor-cloud AI, smart redact uploads, AI workspaces, support chatbots that run commands | U, W, F | Excluded | Recto sends nothing without the user's own key or a local model and a preview per request (D16, D19) |
| Cloud teams, SSO, licence consoles, usage analytics | U, W, F | Excluded | No telemetry (D16); Group Policy covers managed deployment (D21) |
| Microsoft Purview and AIP sensitivity labels | F | Excluded | Needs Microsoft cloud sign-in; not planned (D21) |
| Payments, notary and CRM connectors for e-sign | F | Excluded | Outside Recto Cloud's scope (D20) |
| Mobile-only and macOS-only features | U, W, F | Excluded | Recto is a Windows desktop app (D15) |
