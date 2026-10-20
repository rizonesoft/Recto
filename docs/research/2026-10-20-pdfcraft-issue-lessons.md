# Lessons from pdfcraft's issue tracker

Reviewed 2026-10-20 by Derick Payne. Source: every issue in [storytold/pdfcraft](https://github.com/storytold/pdfcraft/issues) on that date: 1,710 issues (968 open, 742 closed, #8 to #3751), exported with `gh issue list --state all` and read in eight batches. The inventory of every issue (number, state, dates, labels, reactions, comment count, title and link) is committed beside this file as `2026-10-20-pdfcraft-issues.csv`, so any issue number cited here can be checked against its source. pdfcraft is an open-source PDF reader and editor in Rust (egui, wgpu, a vendored hayro renderer) with about 16,600 stars, the closest public analogue to what Recto is building.

This record is the evidence behind the TODO work it feeds: the failure-class gates in `todo/06-quality/TODO-03-user-failure-gates.md`, the user feedback loop in `todo/07-release/TODO-03-listening-to-users.md`, and the new micro-steps and sections named in the owner column below. A section that copies a requirement from here cites this file and the issue numbers.

## How to read it

Reactions on pdfcraft issues are low (the maximum on any issue is 13, most have 0), so demand is measured by how many separate reports and duplicates a problem drew, not by votes. Issue numbers below are pdfcraft's. "Mistake" is the cause pdfcraft's own maintainers or contributors identified in the thread, never a guess. Coverage limits: each issue was read up to the first 2,200 characters of its body and the first six comments up to 500 characters each, so long threads were not read to the end; about 30% of the issues were relayed from Discord with one-line bodies; and many engine reports were written by AI agents from source reading. Causes were taken only where the visible thread stated them. Report counts such as "60+" are approximate totals from clustering related issues, including duplicates, not an exact tally; the cited issue numbers are representative, and the inventory lets anyone recount a theme.

## What users asked for most

| Rank | Request | Reports (representative pdfcraft issues) | Recto owner |
| ---- | ------- | ---------------------------------------- | ----------- |
| 1 | Printing that lists every printer and works on real drivers | 60+ (#160 with 13 reactions, #2192, #2430, #2563, #2684, #2922, #3472) | D07 T01 §2, D06 T03 §9 |
| 2 | Non-Latin text in Add text, Edit text, Fill and Sign and forms | 70+ (#125, #506, #1039, #1835, #2095, #2509, #3202, #3592) | D06 T03 §7 |
| 3 | UI and file names in CJK, Thai, Hebrew, Arabic without boxes | 40+ (#381 with 9, #728 with 10, #651, #826, #407, #586, #3082) | D06 T03 §8 |
| 4 | Editing existing text without destroying neighbours or layout | 50+ (#156, #1316, #1680, #2516, #2564, #3429, #3744) | D06 T03 §4, D03 T01 §9 |
| 5 | Redaction that removes data, and only the marked area of a scan | 35+ (#283, #389, #1141, #1844, #2448, #2874, #3646, #3647) | D06 T03 §6, D05 T01 §10 |
| 6 | Save that never loses or corrupts the file | 30+ (#1698, #1804, #2165, #2366, #2658, #2921, #3496) | D06 T03 §5, D04 T02 §1 |
| 7 | Smooth scrolling that follows Windows touchpad and wheel settings | 25+ (#444, #531, #759, #985, #1904 with 6, #3234) | D02 T02 §6 (existing), D06 T03 §8 |
| 8 | Remember page, zoom, layout and print settings | 25+ (#1178, #1618, #2001, #2156, #2455, #3463) | D02 T03 §1 (existing), D07 T01 §2 |
| 9 | Sharp scans and images (area-filtered downscaling) | 20+ (#730, #2393, #2535, #2868, #3250) | D06 T03 §10 |
| 10 | Windows shell integration: Combine and Convert verbs, print verb, thumbnails | 20+ (#308, #585, #1674, #1695, #2082, #2206, #3207, #3724) | D07 T01 §8, §9 |
| 11 | Cross-page and column-aware text selection and copy | 20+ (#527, #2110, #2387, #3513, #3682) | D06 T03 §11 |
| 12 | Forms that show their values in Acrobat and fill by keyboard | 20+ (#633, #1613, #2223, #2277, #3441, #3470) | D06 T03 §7 |
| 13 | Multiple saved signatures, image signatures with background removal | 20+ (#127, #220, #476, #1288, #1684, #3272) | D05 T02 §10 |
| 14 | OCR with accents and more languages, models shipped in every package | 15+ (#103, #489, #524, #1758, #2485, #3326) | D06 T03 §12 |
| 15 | Create PDF from a scanner | 8 (#312, #1774, #2010, #3256, #3384) | D04 T02 §13 |
| 16 | Tab reorder, tear-off and split view | 15+ (#1537, #1683, #2735, #3553) | ledger (`docs/user-demand.md`) |
| 17 | Know when a newer version exists | 10+ (#28, #99, #1682, #2814, #3197) | D07 T03 §7 |
| 18 | Recover unsaved edits after a crash | 6 (#815, #1803, #2663) | D02 T01 §12 |

## Failure classes and the requirement each one sets for Recto

| Class | pdfcraft evidence | Mistake | Recto requirement | Owner |
| ----- | ----------------- | ------- | ----------------- | ----- |
| Several content readers that disagree | #3080, #3107, #3108, #3177, #3216, #3504, #3568, #3569, #3578 | Renderer, editor, redactor and exporters each parsed content streams with different joins, `EI` rules and limits | One tokenizer and interpreter; a differential suite proves every consumer sees the same operators | D06 T03 §4 |
| Inline image `EI` found by heuristics | #2456, #3107, #3108, #3157, #3579 | Scan for `EI` instead of computing the unfiltered length | Length from `/W /H /BPC /CS` when unfiltered; heuristic only for filtered data | D06 T03 §1 |
| Silent truncation and success on failure | #623, #971, #3052, #3119, #3147, #3205, #3645, #3751 | Caps and watchdogs dropped content and still reported success or `complete: true` | Every cap returns a typed `Truncated` result that reaches the UI, Find and redaction | D06 T03 §2, §3 |
| Hostile numbers and graphs | #1313, #1337, #1761, #1769, #1796, #1799, #1815, #2177, #2423, #3309, #3581 | Unchecked casts and multiplies, recursion on document depth, panics | Checked arithmetic and iterative walks; each shape is a fixture that must return `PdfError` | D06 T03 §1 |
| One bad page-tree entry drops the rest | #2660, #2722, #2919 | Array reads stop at the first failure; viewer and editor disagree on page count | Skip, warn and count; page count matches a reference tool | D06 T03 §1 |
| Save replaced the file before verifying it | #1698, #1793, #1804, #2366, #2367, #3409, #3496 | Write before reopen check, no fsync, close errors ignored, dirty cleared on partial success | Temp, flush, verify by reopening, then replace; original byte-identical on any failure | D06 T03 §5 |
| Save drops metadata and permissions | #2658, #2751, #2921 | Rename-based replace lost ACLs, attributes, creation time; read-only overwritten | `ReplaceFileW` semantics, read-only prompt, access-denied falls back to Save As | D06 T03 §5 |
| Incremental save keeps deleted content | #2165, #3407, #3511 | Save As appended a revision, so deleted pages and images stayed in the file | Removal edits and redaction force a full rewrite; unreferenced-object scan finds nothing deleted | D06 T03 §5, §6 |
| Redaction leaves data in other channels | #1141, #1844, #2659, #2874, #3122, #3302, #3646, #3647 | Removal looked at glyphs and `/Annots` only | Remove from content, `/ActualText`, `/Alt`, struct tree, replies, outlines, XMP, OFF layers, patterns; raw-byte verifier | D06 T03 §6 |
| Partly covered scan blanks the page | #283, #1771, #2246, #3124, #3359 | Fail-closed removed the whole image | Decode, clear covered pixels, re-encode; outside pixels unchanged | D06 T03 §6 |
| Redaction audit wrong both ways | #1817, #1885, #2385, #2786, #3117, #3118 | Audit used its own visibility rules | Audit shares the renderer's visibility; positive and negative controls | D06 T03 §6 |
| Text written as `?` | #125, #222, #347, #506, #959, #2571 | Standard 14 fonts in WinAnsi, lossy encode reported success | Embed a Unicode subset or refuse at typing time; never `?` | D06 T03 §7 |
| Edit swapped the font or merged paragraphs | #1026, #1388, #2516, #2517, #2564, #3215, #3577 | Font coverage inferred from `/W`; paragraphs by index; state of the first run reused | Keep the original font for glyphs it has; stable paragraph identity; per-run state preserved | D06 T03 §4, §7 |
| UI fonts chosen by one probe character | #498, #651, #1531, #3082, #3592 | Fallback probed one Arabic character, Japanese-only bundle | Per-script DirectWrite fallback; every string and file name renders without `.notdef` | D06 T03 §8 |
| Print enumeration hangs on a dead printer | #2192, #2907 | Capabilities read one printer at a time on open, about 48 s per offline WSD printer | Dialog ready in under 1 s; per-printer timeout off the UI thread | D06 T03 §9 |
| Print capability XML parsed strictly | #2430, #2830 | `parse::<i32>()` without trim rejected the whole document | Tolerant parse; skip a bad feature, keep the job; driver XML corpus | D06 T03 §9 |
| Print settings forced or forgotten | #1103, #2001, #2422, #2826, #3096 | Fixed paper table, US Letter default, ticket always forced colour and duplex | Driver paper list, send only changed settings, persist per printer | D07 T01 §2 |
| Driver crash takes the app down | #2684, #2873, #2922, #3472 | Print ran in-process through a third-party path | Print worker isolated with watchdog; failure shows the error chain | D07 T01 §2, D06 T03 §9 |
| Nearest-neighbour image shrink | #2393, #2424, #2535, #2537, #3250 | A fix for one bug made every scan without `/Interpolate` jagged, shipped as a regression | Area filtering whenever scale is below 1; golden test against a reference | D06 T03 §10 |
| Blend and soft-mask errors | #2128, #2970, #2971, #2975, #3176, #3229 | Blend against white, no knockout, alpha bound late | 16 blend modes, knockout, soft masks in the golden suite | D06 T03 §10 |
| Text extraction gaps | #1467, #1468, #1699, #3086, #3169, #3170, #3173 | Glyph names outside AGL dropped, no ToUnicode fallback, fake-bold dedupe ate letters | AGL in full, cmap fallback, dedupe never drops real repeats | D06 T03 §11 |
| Reading order and selection | #527, #2387, #2586, #2667, #3513, #3682, #3711 | Top-left sort, per-page selection, bidi applied to output | Column-aware order, document-level selection, logical-order bidi | D06 T03 §11 |
| Shipped without its models or fonts | #103, #489, #1758, #3196 | Release packages lacked OCR models and a fallback font the code required | Smoke test the installed artifact, not the source tree | D06 T03 §12 |
| Startup dies silently | #1278, #2284, #2493, #3286, #3414, #3688 | GPU errors fatal, CPU-feature abort, single-instance handoff to a hung instance | Visible failure with log path, WARP safe mode, handoff liveness timeout | D02 T01 §7, D02 T02 §1, D06 T03 §12 |
| Default app taken without asking | #2363 | First run claimed the PDF handler | Only through the Windows Default Apps flow, never silently | D07 T01 §4, §9 |
| Upgrade loses pins, settings, signatures | #1058, #2185, #3606 | Installer recreated shortcuts and reset stores | Upgrade test keeps Start pin, settings and saved signatures | D06 T03 §12 |
| Feature exists but nobody finds it | #2231, #2721, #2843, #3188, #3290 | Buried settings, no search, no release notes | Every command in `Ctrl+K` with synonyms; What's new after update | D07 T03 §6, §7 |
| Keyboard layouts hijack shortcuts | #1980, #3655 | `Ctrl+Alt` treated as a command chord; physical keys matched for paste | AltGr is text input; text fields win `Ctrl+V/C/X/A` | D02 T04 §9 |
| IME breaks in the command box | #1252 | Focus re-requested every frame | Composition survives in `Ctrl+K` | D02 T03 §12 |
| Accent-sensitive Find only | #3460 | No diacritic folding | Diacritic-insensitive by default, toggle to exact | D03 T01 §5 |
| Reading colours repainted annotations | #1548, #2059 | Colour replacement applied to annotation appearances; white placeholder flash | Page content only, screen only; placeholder in the reading colour | D02 T02 §10 |

## How pdfcraft treated its users, and what Recto does instead

| Pattern | Evidence | Recto rule | Owner |
| ------- | -------- | ---------- | ----- |
| Fixed on main, closed, not released; users kept reporting | #160, #826, #1153, #1844, #2470, #2695, #3535 | An issue closes when the release containing its fix ships, with "Fixed in vX.Y.Z" | D07 T03 §4 |
| Security fixes sat unreleased, no advisory, private reporting off | #1114, #1844, #2380 | `SECURITY.md`, private reporting on, advisory with every security release | D07 T03 §1, §5 |
| No issue template; version, OS and file missing | #499, #500, #1860, #3046 | Required fields in issue forms, plus an in-app Copy diagnostics | D07 T03 §1, §2 |
| Discord relay with no reply path | #2078, #2098, #3352, about 40% of later issues | One intake: GitHub forms prefilled from the app; nothing relayed one way | D07 T03 §1, §2 |
| Needs-info dead ends for private files | #1628, #2423, #2722, #3159 | A structure-preserving scrubber so users can share a reproducer safely | D07 T03 §3 |
| Bot split reports and closed real requests as support | #2373, #2517, #2743, #3085, #3433, #3490 | Humans close; a duplicate close links the canonical issue's status; no auto-close of requests | D07 T03 §1 |
| Regression shipped with a subsystem swap | #2192, #2393, #2430, #2970 | Release-over-release render and print parity gates | D06 T03 §10, §12 |
| CI red or cancelled on main hid regressions | #671, #948, #1969, #2415 | A cancelled run is not green; non-admin test leg | D07 T03 §5 |
| Competing PRs and stale claims | #2800 (five PRs), #1683, #2395 | One owner per issue; superseded PRs closed | D07 T03 §1 |
| Requests with no visible answer | #493, #571, #2596 | A public scope page and a demand ledger reviewed every release | D07 T03 §6 |

## Already decided in Recto, confirmed by this review

- PDF JavaScript never runs (D9). pdfcraft's JavaScript host produced a steady stream of form bugs (#247, #269, #2618, #2842); Recto shows a banner on forms that need scripts or dynamic XFA instead.
- One `DocumentActor` per document and an immutable `DisplayList` shared by every consumer (foundation plan section 4) is the structural answer to pdfcraft's disagreeing readers.
- Temp-file-then-replace for every write (AGENTS.md) answers the save class; D06 T03 §5 proves it under fault injection.
- Local-only crash bundles and zero telemetry (D16) match what pdfcraft users praised (#348); any network feature stays opt-in.
