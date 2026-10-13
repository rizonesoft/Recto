---
schema_version: 1
id: pdf-conversions-a
domain: 04-convert
status: active
title: "TODO-02 -- Conversions A: Images, Images to PDF, Text"
track: M4
---

# TODO-02 -- Conversions A: Images, Images to PDF, Text

> **Goal:** The M4 MuPDF-native conversions ship as queue jobs: PDF to images at a chosen DPI and format, images assembled into a PDF at a chosen page size, and PDF to plain text, each through the plan's writer pattern, each honest about what it is good at, each round-tripping the corpus without errors.

> **Current state:** Measured 2026-10-13. No code exists. The mechanics are plan section 5's rows: whole-page renders encoded with WIC (PNG, JPEG with quality, multi-page TIFF option, BMP), images opened by MuPDF as one-page documents and written through the `pdf` writer (fit-to-image, A4, Letter with margins), and the `text` writer over the same extraction D03 T01 §1 builds. Surfaces are SPEC 9.5 step 3 forms (`D-Dialogs.png` top-right for images; `B-Convert.png` for the options shape) inside D04 T01 §2's frame.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` section 5 (writer pattern, the three conversion rows, the atomic saving rule), section 2 (WIC row).
- `design/SPEC.md` sections 9.5 (options per output, replace-existing rule), 9.9 (Replace original dialog where a conversion targets its source's name).
- -> XREF: D05 T01 §1 -- the graft-based page operations share this file's writer plumbing and atomic save helper (§1); the helper's contract is agreed here and consumed there.
- -> XREF: D04 T01 §1 -- the job contract these converters register against; isolation is re-proven here with a real writer.

## Outcome

- Any corpus PDF renders to a folder of images at the chosen DPI with page-range control, pixel-matching `recto-cli` reference renders at the same settings.
- A set of images becomes one PDF in order at the chosen page size, openable everywhere, with orientation and margins honored.
- A text PDF becomes a `.txt` equal to the extraction fixtures, and an image-only PDF produces an empty-but-honest result the UI names as scanned (OCR remains after v1).

**Adjacency:** list=not-applicable (outputs are files in Explorer, listed by the queue during the job per D04 T01 §2); document=applicable @ D04 T02 §2; settings=applicable @ D02 T04 §2; reporting=not-applicable (per-job results are the queue's, D04 T01 §3); notifications=applicable @ D04 T01 §3; permissions=not-applicable (no roles; encrypted inputs refuse through the job recovery in D04 T01 §1); audit=not-applicable (no conversion history kept, per D04 T01's stated boundary); exchange=applicable @ D04 T02 §3; reverse=not-applicable (converting back is another named conversion, images to PDF in section 3, not an undo)

**Adjacency rationale:** These sections are the exchange surface itself (formats in and out), with section 2 as the document user carry-away. Save-location and replace-existing defaults are settings values owned by D02 T04 §2 and read here. Nothing here needs its own list, report or audit beyond the queue's per-job truth.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Writer plumbing and atomic save helper | D04 T01 §1 | [ ] |
| 2 | §2 | PDF to images | §1 | [ ] |
| 3 | §3 | Images to PDF | §1 | [ ] |
| 4 | §4 | PDF to text | §1, D03 T01 §1 | [ ] |

## 1. Writer plumbing and atomic save helper

Plan section 5's pattern as shared bridge and services code: open a document writer for a format, run pages through it with the job's cookie, close, drop; plus the output-path helper implementing the saving rule (temp in destination, `ReplaceFileW` or `MoveFileExW`, name-collision policy from the job options: keep both with a suffix, or replace, asked per SPEC 9.9 when the target is the source itself). D05's graft operations reuse both halves.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/bridge/writer.c` (`rc_write_document` over the plan's loop for a format and options string, per-page progress through the cookie) and `src/services/output_path.cpp` (temp naming, collision policy, atomic replace, cleanup on failure). Done when: a copy-conversion of every good corpus PDF (pdf writer, identity) round-trips byte-comparable page counts and renderable output, and a killed mid-write test leaves the previous destination file intact.
2. Prove job isolation with a real writer: convert a file while it is open in the viewer path (services-level viewer session), scrolling simulated through the scheduler. Done when: D04 T01 §1's isolation test passes against this writer under ASan.

- [ ] Bridge writer loop plus atomic output helper. Done when: no converter in T02 to T04 or D05 needs its own save code (search proves one helper). Cheaper substitute: writing directly to the destination, which a crash turns into a corrupted only copy.
- [ ] Identity round-trip over the corpus as the plumbing proof. Done when: outputs re-open in `recto-cli info` with matching page counts for every good file.
- [ ] Commit: `convert: document-writer plumbing with atomic saves`

**Test checkpoint:** `recto-tests --gtest_filter=Convert.*` passes under ASan: round-trips, kill-mid-write integrity, isolation while viewing (quote file counts).

## 2. PDF to images

Each selected page rendered whole at the chosen DPI through the display-list path (one page-sized tile, plan section 5) and encoded with WIC: PNG, JPEG (quality), TIFF (one multi-page file or per-page), BMP. Options per SPEC 9.5: format, resolution 72, 150, 300, 600, page range, destination, naming pattern. Rotation is honored as viewed; transparent pages flatten onto white exactly like the tile path.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 step 3 with `design/screenshots/D-Dialogs.png` (conversion options dialog) inside `D-Convert.png`'s flow.
**Job:** The user can get faithful pictures of pages at print or screen resolution. Consumer: whatever opens the images next; goldens (D06 T01 §3) compare this output.
**Treatment:** Engine renders plus WIC encode, registered as a job card. Cheaper substitute that fails the checkpoint: screenshotting the viewer surface at window resolution.
**Chrome:** consume the Convert frame, options-dialog shape and job plumbing. Do not invent a second export dialog layout for later converters.

**Build order:**

1. Register the images converter: whole-page render service reusing D01 T02's display lists at render-job isolation, WIC encoders with format options, per-page output naming (`name-p3.png` pattern) through §1's helper. Done when: 150 and 300 dpi PNGs of a corpus page pixel-match `recto-cli render` at the same scale.
2. Options form per the board (format, DPI, pages, destination with the replace-existing choice) and the hint line ("Best for sharing or printing single pages"). Done when: the full SPEC option set is present and the surface account is clean.

- [ ] Images converter with WIC formats and DPI control. Done when: multi-page TIFF opens with all pages in order in a stock viewer. Cheaper substitute that fails the checkpoint: MuPDF's own PNG writer only, losing JPEG quality and TIFF (plan section 5 names WIC deliberately).
- [ ] Options form and page-range handling. Done when: an out-of-range page request refuses before the job starts, in the form, not as a job failure.
- [ ] Commit: `convert: PDF to images (PNG, JPEG, TIFF, BMP) via WIC`

**Test checkpoint:** `recto-tests --gtest_filter=Convert.Images` pixel-compares against CLI references at two DPIs (quoted), TIFF page count verified, screenshot of the options form against the board in findings.

## 3. Images to PDF

Images in the order given become pages: MuPDF opens each image (PNG, JPEG, TIFF and friends) as a one-page document and the `pdf` writer assembles them; page size options fit-to-image, A4 or Letter with margins, orientation following each image. EXIF orientation is honored so phone photos land upright; unreadable images fail the job per-file with names, not silently skipped.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (the "PDF from images" path through the same three steps) with `D-Convert.png`.
**Job:** The user can bind scans and photos into one PDF. Consumer: any PDF reader, including Recto reopening the result.
**Treatment:** MuPDF image documents through the pdf writer, not hand-built PDF objects. Cheaper substitute that fails the checkpoint: embedding images at fixed A4 regardless of aspect, cropped.
**Chrome:** consume the Convert frame and §1 helper. Do not invent a separate importer page.

**Build order:**

1. Register the converter: ordered image inputs (chips reorderable in step 1), page-size policy, per-image orientation from EXIF where present. Done when: the image corpus set assembles into a PDF whose page sizes and order match the options, verified by `recto-cli info` and a render comparison.
2. Failure honesty: unsupported or corrupt images listed by name in the job result, job succeeds only when every input is in. Done when: a planted corrupt image fails the job naming it, with no partial PDF left.

- [ ] Images-to-PDF converter with page-size policies. Done when: fit-to-image preserves pixel dimensions as page points per the stated scale. Cheaper substitute that fails the checkpoint: re-encoding every image to JPEG (lossy, plan says write through, not transcode by default).
- [ ] Per-file failure naming. Done when: mixed good and bad inputs produce no output and a complete bad list.
- [ ] Commit: `convert: images to PDF with page-size policies`

**Test checkpoint:** `recto-cli info` and render comparisons verify the assembled PDFs (quoted), corrupt-input case naming the file, screenshot of the flow in findings.

## 4. PDF to text

The `text` writer (or stext serialization where custom shaping is needed) over D03 T01 §1's extraction, page by page with page-break form feeds as the stated separator policy, written as UTF-8. The result equals the extraction fixtures for the text corpus; scanned files produce an empty file and a job result that says the document has no text layer (the honest-conversion rule, SPEC section 2), pointing at nothing it cannot do (OCR is after v1, plan section 2).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (Plain text card) with `D-Convert.png`; the output's fidelity is to D03 T01's fixtures, checked as bytes.
**Job:** The user can get a document's words as a text file. Consumer: editors and scripts downstream.
**Treatment:** The shared extraction, one serialization. Cheaper substitute that fails the checkpoint: a second ad-hoc extraction in the converter that disagrees with selection and find.
**Chrome:** consume the Convert frame; the options are pages and destination only. Do not invent encoding options v1 does not promise.

**Build order:**

1. Register the text converter over §1's writer plumbing with the separator and encoding policy stated in its hint line. Done when: output for every text-corpus file byte-matches the D03 T01 §1 fixtures under the stated policy.
2. No-text honesty: image-only input completes with the scanned-document result wording and an empty file only when the user confirms that is what they want at options time (the form detects and says so before running). Done when: the scanned corpus file's flow shows the warning before any job starts.

- [ ] Text converter byte-equal to extraction fixtures. Done when: multi-column reading order matches find's order for the same file. Cheaper substitute that fails the checkpoint: the MuPDF text writer with default options where they disagree with D03's recorded choices, unexamined.
- [ ] Scanned-document honesty before the job runs. Done when: no empty file is ever produced silently.
- [ ] Commit: `convert: PDF to plain text over the shared extraction`

**Test checkpoint:** `recto-tests --gtest_filter=Convert.Text` byte-compares the text corpus (quoted), the scanned-file pre-run warning captured in findings. M4's text done-when closes with §2 and §3 evidence in D06 T01 §3.

## Verification

- M4 round-trip rule (plan section 7) for the three converters: every good corpus file converts without error, outputs verify by re-opening or pixel and byte comparison.
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes.
