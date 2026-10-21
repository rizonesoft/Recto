# Adobe Acrobat Pro parity ledger

Foundation plan D18 (owner decision, 2026-10-21): Recto matches Adobe Acrobat Pro tool by tool, without removing or weakening any Recto improvement. This ledger maps every Acrobat Pro tool to the TODO sections that build it in Recto, or to a recorded exclusion with its reason. A tool with no owner here is a plan defect: file it through `add-todo` and add its row. Review this ledger with `docs/user-demand.md` before each release (`D07 T03 §6`).

Status: **Planned** means sections own it; **Partial** means part of the Acrobat tool is owned and the rest is named; **Excluded** means a recorded decision not to build it, with the reason. "Recto keeps" names the improvement that parity must not remove.

Last reviewed: 2026-10-21, against the Acrobat Pro "All tools" list.

## Create, combine and organize

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Create PDF from files (Office, images, HTML, text) | Planned | D04 T04 §1-§7, D04 T02 §5 | Native engines, no Office or LibreOffice; legacy `.doc`, `.xls`, `.ppt`; Markdown, RTF, XPS, SVG |
| Create PDF from scanner | Planned | D04 T02 §13 (owner decision on timing) | Local WIA, optional local OCR |
| Create PDF from clipboard | Planned | D04 T02 §14-§16 | |
| Create PDF from a web page | Excluded | | Network fetches are off by design (plan section 9); a saved HTML file converts through D04 T04 §3 |
| Combine files | Planned | D05 T01 §1-§2, D05 T02 §3, D07 T01 §9 | Explorer multi-select Combine |
| Organize pages (insert, delete, extract, rotate, reorder, split) | Planned | D05 T01 §2-§4, D05 T02 §1-§5 | Copy-based edits; never in place |
| Replace pages | Planned | D05 T01 §14, D05 T02 §12 | |
| Crop and page boxes | Planned | D05 T01 §4, D05 T02 §6 | |
| PDF Portfolio | Planned | D02 T03 §8 | Attachments ask before opening |

## Edit and enrich

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Edit PDF: text with reflow, add text | Planned | D03 T04 §1-§7, §11-§12, §14-§16 | Edits on a copy; original font kept per glyph; never `?` |
| Edit PDF: images and vector objects | Planned | D03 T04 §8-§10, §13 | |
| Find and replace text | Planned | D03 T01 §9 | Shares the Edit PDF rewrite engine |
| Links, destinations | Planned | D03 T03 §11-§14 | Launch and JavaScript actions never written or run (D9) |
| Article threads | Planned | D03 T03 §15-§16 | |
| Bookmarks | Planned | D02 T03 §5-§6 | Heading-based auto-generation |
| Headers, footers, watermarks, Bates numbering | Planned | D05 T01 §5-§6, D05 T02 §7-§8 | Managed marks can be listed and removed |
| Backgrounds | Planned | D05 T01 §13, D05 T02 §13 | |
| Attachments | Planned | D02 T03 §8 | |
| Layers (optional content) | Planned | D02 T03 §7 | |
| Document properties, metadata, initial view | Planned | D02 T03 §9-§10 | |
| Rich media (audio, video) | Planned | D02 T05 §1-§4 | Ask before play; no Flash, no scripts, no network |
| 3D models | Partial | D02 T05 §5-§10 | Bounded, fuzzed parsers; 3D JavaScript never runs. Not covered: PRC B-rep parts without stored tessellation (listed, drawn as bounding boxes), PRC compressed tessellation (poster fallback with the reason), and U3D block kinds outside the listed set (skipped and noted); B-rep tessellation needs its own section when asked for |
| Geospatial PDF | Planned | D02 T05 §11-§14 | |
| JavaScript (document, field, 3D) | Excluded | | Zero PDF JavaScript by construction (D9); forms that need scripts get a banner (D03 T03 §7) |

## Comment, fill and sign

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Comments and markup (all annotation types) | Planned | D03 T03 §1-§5 | |
| Stamps, custom and dynamic | Planned | D03 T03 §20-§21 | Dynamic tokens resolved without JavaScript |
| Measure (distance, perimeter, area) | Planned | D03 T03 §17-§19 | |
| Fill and Sign (flat forms) | Planned | D03 T03 §22, D05 T02 §10 | Unicode-safe typed text; Lock on save only on a copy |
| Prepare form, fill forms | Planned | D03 T03 §6-§10 | XFA to AcroForm conversion |
| Digital signatures: sign, certify, validate, timestamp, LTV | Planned | D05 T01 §8-§9, D05 T02 §10-§11 | Windows certificate store; verdicts never "valid" when unknown |
| Request e-signatures (Acrobat Sign) | Planned, phase 98 | D09 T04, D09 T05 | Recto Cloud (D20): simple electronic signatures with a hash-chained audit trail and a PAdES seal; the desktop stays offline until the user signs in (D16) |
| Shared review, Send for comments | Planned, phase 98 | D09 T03, D09 T05 | Recto Cloud (D20) live reviews in Recto's annotation model; XFDF and JSON file exchange remains (D03 T03 §2, §5) |

## Protect and standards

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Password protection (AES-256) and permissions | Planned | D05 T01 §7 | |
| Certificate encryption | Planned | D05 T01 §11-§12, D05 T02 §14-§15 | |
| Redact | Planned | D05 T01 §10, D05 T02 §9 | True redaction proven on raw bytes (D06 T03 §6) |
| Sanitize, Remove hidden information | Planned | D05 T01 §10, D05 T02 §9 | |
| Compress, Optimize, audit space usage | Planned | D04 T02 §11-§12 | |
| Fast Web View (linearize) | Planned | D04 T02 §11 | |
| PDF/A validate and convert | Planned | D04 T05 §5-§6 | |
| PDF/UA check, autotag | Planned | D04 T05 §7 | |
| PDF/X validate and convert | Planned | D04 T06 §4-§10 | |
| PDF/E and PDF/VT identify and preflight | Partial | D04 T06 §17 | Partial preflight profiles for PDF/E-1, PDF/VT-1 and PDF/VT-2 that report what was and was not checked, never "conformant"; PDF/VT-3 (on PDF/X-6) is identified only; conversion is not planned until requested (`docs/user-demand.md`) |
| E-invoices (ZUGFeRD, Factur-X) | Recto only | D04 T05 §8 | Beyond Acrobat |

## Accessibility

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Accessibility checker, autotag | Planned | D04 T05 §7 | |
| Tags panel editing, alternate text | Planned | D04 T05 §14, §16 | |
| Reading Order tool | Planned | D04 T05 §15, §17 | |
| Read Out Loud | Planned | D03 T02 §7-§9 | Local Windows voices |
| Screen reader support of the app | Planned | D03 T02 §1-§6 | UIA text provider, keyboard audit |

## Scan, export and convert

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Scan and OCR, editable text from scans | Planned | D03 T01 §7-§8, D04 T02 §7 | Fully local OCR, no cloud |
| Enhance scans (deskew, cleanup) | Planned | D03 T01 §7 | |
| Export to Word and RTF | Planned | D04 T03 §1-§6 | Three layout modes |
| Export to Excel | Planned | D04 T05 §1-§2 | CSV and XML too |
| Export to PowerPoint | Planned | D04 T05 §10-§13 | |
| Export to images, text, HTML | Planned | D04 T02 §3-§6, D04 T05 §3 | Markdown, EPUB, LaTeX, SVG, XPS beyond Acrobat |

## Print and print production

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Print, page ranges, scaling, duplex | Planned | D07 T01 §1-§3 | Driver isolation, paper from the driver |
| Booklet, multiple pages per sheet, poster | Planned | D07 T01 §10 | |
| Preflight and fixups | Planned | D04 T06 §2-§9, §13, §15 | Never compliant for unanalysed content |
| Output Preview, separations, ink manager, colour warnings | Planned | D04 T06 §11-§12, §14 | |

## Search, compare, automate

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Find in document | Planned | D03 T01 §5-§6 | Regex; accent-insensitive by default |
| Advanced Search across files, search index | Planned | D03 T05 §1-§9 | |
| Compare files | Planned | D03 T01 §10 | |
| Action Wizard | Planned | D04 T07 §1-§14 | Never overwrites inputs; no scripts |
| Custom tool sets, toolbar customization | Partial | D02 T04 §3, D02 T03 §12 | The `Ctrl+K` command box and the `Custom` layout preset stand in for Acrobat's custom tool sets |
| Command line | Recto only | D01 T03 §1-§3 and every `recto-cli` verb | Acrobat has none |

## Views and reading

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| Page display, zoom, rotate view, full screen, read mode | Planned | D02 T02 §5-§10 | Three layout presets (D17), dark page modes |
| Tabs, multiple windows | Partial | D02 T01 §3 | Tab tear-off and split view are recorded in `docs/user-demand.md` |
| Recent files, resume reading position | Planned | D02 T03 §1-§2 | |

## Cloud and mobile

| Acrobat Pro tool | Status | Recto sections | Recto keeps |
| --- | --- | --- | --- |
| AI Assistant (summarize, ask, generative features) | Planned, opt-in | D08 T01 §1-§19 | Off by default; the user's own Anthropic, OpenAI, OpenRouter or Gemini key; preview before every request (D19) |
| Document Cloud storage, sharing links | Planned, phase 98 | D09 T01, D09 T02, D09 T05 | Recto Cloud (D20), AGPL and self-hostable on `workerd`; opt-in from the desktop |
| Web app | Planned, phase 98 | D09 T02 | Scriptless `pdf.js` viewer (D9) |
| Mobile apps | Excluded | | Recto is a Windows desktop app (D15); the Recto Cloud web app works in mobile browsers |
