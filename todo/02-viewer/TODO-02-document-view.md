---
schema_version: 1
id: document-view
domain: 02-viewer
status: active
title: "TODO-02 -- Document View: Presentation, Tiles, Scroll and Zoom"
track: M2
---

# TODO-02 -- Document View: Presentation, Tiles, Scroll and Zoom

> **Goal:** Pages on screen at the foundation plan's performance targets: a per-view presentation thread drawing scheduler tiles through Direct2D on a `SwapChainPanel`, continuous scroll (plus single-page and two-page options), zoom that never blanks, rotation, link handling that asks first, and dark-theme page colour modes, all matching the SPEC viewer boards.

> **Current state:** Measured 2026-10-13. No viewer code exists. D01 T02 will deliver `Tile`s (BGRA premultiplied, explicit stride) through a scheduler callback; D02 T01 delivers the shell slots this view docks into. The design is fixed: plan sections 4.7 (tile path), 4.8 (presentation thread, 512 MB starting texture-cache budget), 8 (targets: first page under 300 ms on a 100-page text PDF, 60 fps scroll, stretched zoom preview in-frame with sharp tiles within 150 ms), and SPEC sections 4 (page colour modes), 8.2 (floating toolbar contents), 9.2 (viewer behavior), 10 (keys), 12 (motion).

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 4.5 (thread map: presentation thread owns its device contexts), 4.7, 4.8, 8.
- `design/SPEC.md` sections 4 (pages in dark theme: Original, Dimmed, Inverted), 7 (page shadow, canvas), 8.2 (toolbar), 9.2, 10, 12; boards `D-Focus-Light.png`, `D-Focus-Dark.png`, `D-Classic.png`, `D-ReadingRoom.png`.
- -> XREF: D01 T02 §2 -- the tile hand-off this file consumes: `TileKey`, stride and completion callback are agreed there; the GPU upload strategy below feeds back into what the worker delivers (plan 4.7's worker-created textures).
- -> XREF: D03 T01 §2 -- text selection renders over these pages; the page coordinate mapping defined in §3 here is what selection and search highlights use.

## Outcome

- A 100-page text PDF shows its first page within the plan target on the reference machines (D00 T01 §5), scrolls at 60 fps with no blank tiles at normal speed, and zooms with an immediate stretched preview.
- Continuous, single-page and two-page (cover alone, then pairs) layouts all work with correct page geometry, rotation included.
- Links highlight and ask before opening (SPEC 9.9 dialog), launch actions never execute, and pages respect the dark-theme colour mode setting.

**Adjacency:** list=not-applicable (finding pages is the thumbnail and outline panels, D02 T03 §2); document=applicable @ D07 T01 §1; settings=applicable @ D02 T04 §2; reporting=not-applicable (no reports over viewing state); notifications=not-applicable (viewing produces no lifecycle notices); permissions=applicable @ D02 T02 §4; audit=not-applicable (reading position is session state, D02 T03 §1); exchange=not-applicable (copy and share are D03 T01 §2); reverse=not-applicable (view changes are stateless; nothing is created)

**Adjacency rationale:** Print (the document surface of viewing) is owned with packaging in M6 because its API choice is a named open item there. Permissions here means the document-level refusal boundary (links ask, launch actions never run) exercised in section 4, which is the viewer's one genuine permission surface. Reading position and recents are the Home file's state, not the view's.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | SwapChainPanel and presentation thread | D02 T01 §2 | [ ] |
| 2 | §2 | Tile integration and texture cache | §1, D01 T02 §3 | [ ] |
| 3 | §3 | Scroll, zoom, rotate and page layouts | §2 | [ ] |
| 4 | §4 | Links, page colour modes and view states | §3 | [ ] |

## 1. SwapChainPanel and presentation thread

Plan 4.8: each document view owns a presentation thread; the UI thread only sends viewport state (scroll offset, zoom, size, DPI scale). The presentation thread is the sole user of its `ID3D11DeviceContext` and `ID2D1DeviceContext` (multithreaded D2D factory), draws on a DXGI flip-model swap chain hosted by `SwapChainPanel`, and registers into `Runtime`'s shutdown sequence between actors and base-context drop (D01 T02 §4).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 7 and 9.2 (canvas, page shadow, loading outlines) with `D-Focus-Light.png` and `D-Focus-Dark.png`.
**Job:** The user sees pages, smoothly, from the first frame. Consumer: tiles from D01 T02 arrive here; scroll and zoom state originates here for the scheduler.
**Treatment:** Background-thread presentation exactly per plan 4.8. Cheaper substitute that fails the checkpoint: rendering pages into XAML `Image`s on the UI thread, which caps scroll performance and blocks input.
**Chrome:** consume the shell's centre slot and `RectoCanvasBrush`; the swap chain clears to the canvas token colour, sampled from theme resources, never a literal. Do not invent a second rendering surface for other modes.

**Build order:**

1. Create `DocumentView` (`src/app/DocumentView.xaml` plus presentation code in `src/services/presentation.cpp`): device creation with WARP-free hardware preference and device-lost recovery, swap chain sized to the panel, DPI changes handled. Done when: the canvas presents at the display refresh with the window resizing without flicker or device loss across monitor DPI changes.
2. Viewport state channel: UI thread posts size, scroll and zoom snapshots; presentation thread consumes the latest only. Done when: a synthetic resize storm never back-pressures the UI thread (input latency stays responsive in the trace).
3. Loading state: page outlines at true size drawn from the description (D01 T01 §3) before any tile arrives, per SPEC 9.2 (never a whole-window spinner). Done when: opening the largest corpus file shows correctly sized page placeholders in the first presented frames.

- [ ] Presentation thread with device-lost recovery and shutdown registration. Done when: closing a tab mid-present joins cleanly (D01 T02 §4 assert holds). Cheaper substitute that fails the checkpoint: presenting from the UI thread "until it is slow".
- [ ] Page-outline loading state from description data. Done when: scroll geometry is correct before the first tile lands.
- [ ] Commit: `viewer: SwapChainPanel presentation thread and viewport channel`

**Test checkpoint:** rendered frames on the display-session host: canvas and outlines match `D-Focus-Light.png` geometry, presentation sustains the display refresh during a scripted resize (frame times quoted in findings).

## 2. Tile integration and texture cache

Plan 4.7's hand-off completed: render workers create `ID3D11Texture2D`s from pixmap samples (device is free-threaded, immediate context is not) and post them; the presentation thread owns the texture cache with an LRU budget starting at 512 MB (plan 4.8), evicting by distance from the viewport. The scheduler interface from D01 T02 §3 is driven by the viewport: needed keys computed per frame, completions matched by `TileKey`, stale completions (view moved on) dropped without caching cost surprises.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.2 (pages fill as tiles arrive, no spinner) with `D-Focus-Light.png`; tile seams must be invisible at every zoom bucket.
**Job:** The user sees sharp, correct pages wherever they scroll or zoom, within the memory budget. Consumer: §3's layouts read tile availability from this cache.
**Treatment:** Worker-created textures plus presentation-owned LRU cache, keys per plan 3.3. Cheaper substitute that fails the checkpoint: uploading pixmaps on the presentation thread per frame, or an unbounded cache that grows past the 600 MB one-document target.
**Chrome:** consume `TileKey` and the scheduler callback from D01 T02 unchanged. Do not invent a second keying or caching scheme for thumbnails (D02 T03 §2 reuses this path at a thumbnail bucket).

**Build order:**

1. Tile texture cache with byte budget, LRU eviction and budget telemetry (current bytes, evictions) readable by tests. Done when: scrolling a 500-page corpus-class document end to end holds the cache at or under budget (telemetry quoted).
2. Viewport-to-scheduler driver: per-frame needed-key computation across visible pages and neighbours, request submission, completion uptake, page compositing with the soft shadow (SPEC section 7) beneath tiles. Done when: first page of a 100-page text file presents within 300 ms of open on the reference machine (D00 T01 §5), and fast scrolling converges to sharp tiles without permanent holes.

- [ ] Texture cache with enforceable 512 MB starting budget (the Settings cache tunable later sets this value, D02 T04 §2). Done when: the budget is one parameter with one consumer here. Cheaper substitute that fails the checkpoint: counting tiles instead of bytes, which lies at high zoom.
- [ ] Compositing with correct stride and premultiplication at every rotation. Done when: rotated corpus pages pixel-match `recto-cli` reference renders (D01 T03 §1) through the golden comparison (D06 T01 §1).
- [ ] Commit: `viewer: scheduler tiles on screen with budgeted texture cache`

**Test checkpoint:** the D06 T01 §4 harness (`tests/perf/`) measured on the reference machine: first-page time and scroll frame times quoted against plan section 8 targets, cache telemetry under budget, and pixel comparisons green for the render corpus at two zoom buckets.

## 3. Scroll, zoom, rotate and page layouts

The reading interactions (SPEC 9.2 and 10): continuous vertical scroll by default with 16 epx page gaps and centred pages; single-page and two-page spread (cover alone, then pairs); zoom by Ctrl+wheel, pinch and toolbar with discrete buckets and stretched-preview semantics (plan 4.7: nearest cached bucket scales in the same frame, sharp tiles replace within 150 ms); rotate view; page navigation keys (Home, End, Page Up, Page Down, Ctrl+G with D02 T03's surfaces); the floating page toolbar per SPEC 8.2 in the Focus slot arrangement (presets move it in D02 T04, contents fixed here).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 8.2 and 9.2 with `D-Focus-Light.png`, `D-Focus-Dark.png` and `D-Classic.png` (toolbar in docked form for slot verification).
**Job:** The user can read: move through pages, change size, turn the view, and always know which page they are on. Consumer: search and selection (D03) anchor to the page mapping defined here.
**Treatment:** One page-geometry model (page rects in view space for the active layout) that scrolling, zoom, hit-testing and later overlays all share. Cheaper substitute that fails the checkpoint: per-feature coordinate math that drifts apart at rotation and two-page boundaries.
**Chrome:** consume the page toolbar control built here in every preset slot; toggled states use `RectoAccentTintBrush`. Do not invent preset-specific toolbars.

**Build order:**

1. Page geometry model for the three layouts plus rotation, with the current-page rule (the page owning the viewport centre) feeding the toolbar's page box and later panels. Done when: scripted scroll and layout switches report the expected current page across the corpus, including mixed-size pages.
2. Zoom engine: bucket selection, stretch-preview compositing from the nearest cached bucket, anchor-point preservation (the point under the cursor or centre stays put), fit-width and fit-page and 50 to 400 percent presets (SPEC 8.2). Done when: a zoom step presents a stretched frame in the same frame budget and sharp tiles within 150 ms on the reference machine, with no blank frame in the trace.
3. The page toolbar (SPEC 8.2 order and contents, Convert accent button routing to D04's page when it lands, More menu with the listed commands) hosted in the bottom-overlay slot with the 3-second fade-to-40-percent reading behavior and its reduced-motion exception (SPEC 11). Done when: every toolbar command works or is deferred to a named section on the control itself (Convert to D04 T01 §2's page, Properties to D02 T03 §3, Print to D07 T01 §1).

- [ ] Three layouts over one geometry model. Done when: two-page spread pairs pages after a lone cover on odd and even page counts. Cheaper substitute that fails the checkpoint: continuous-only with spreads faked by zoom.
- [ ] Zoom with in-frame stretched preview and anchor preservation. Done when: Ctrl+wheel zoom on a text point keeps that point within a few epx across the sharp-tile swap.
- [ ] Page toolbar complete per SPEC 8.2, deferred commands naming their sections. Done when: the surface-completeness account in the review findings lists every SPEC 8.2 element as working or owned.
- [ ] Commit: `viewer: scroll, zoom, rotate, page layouts and the page toolbar`

**Test checkpoint:** `recto-tests --gtest_filter=View.Reading` drives a reading session on the display-session host against the three viewer boards (screenshots in findings), with zoom and scroll timings quoted against plan section 8 and the current-page script green.

## 4. Links, page colour modes and view states

The trust and comfort layer of viewing: link annotations hit-test and highlight, internal links navigate, external links open only through the SPEC 9.9 "Open this link?" dialog (setting-controlled per SPEC 9.7, ask by default), and launch actions never execute (plan section 9). Dark-theme page colour (SPEC section 4: Original, Dimmed towards `#D9D5CE`, Inverted without inverting images) is a compositing choice over tiles, never a PDF change. Full screen (F11) hides shell chrome until the pointer reaches an edge (SPEC section 10).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 4 and 9.9 (link dialog) with `D-Focus-Dark.png` (dimmed pages) and `D-Dialogs.png`.
**Job:** The user can follow a document's links safely and read it comfortably in the dark. Consumer: the Settings reading group (D02 T04 §2) owns the ask-before-links and page-colour values this section reads.
**Treatment:** Link data from the actor (annotation rects in page space), hit-tested through §3's geometry; colour modes applied in the tile shader or blend, not by re-rendering. Cheaper substitute that fails the checkpoint: shell-executing URLs directly, or inverting by re-rendering pages through `recto-pdf`.
**Chrome:** consume the §4 dialog style from D02 T01 §4 for the link prompt; page colour reads the one settings value. Do not invent a second prompt or per-document colour state.

**Build order:**

1. Link extraction on the actor (`recto-pdf` link rects, destinations and URIs per page from D01 T01 §4, cached with the display list's lifetime) and hover, cursor and keyboard activation in the view. Done when: the corpus link file's internal links navigate and its external link produces the dialog with the full URL in a monospace box, opening only on confirmation.
2. Launch-action and file-attachment refusal: actions other than internal go-to and confirmed URI are inert and, where the SPEC gives them a surface, explained. Done when: a crafted corpus file with a launch action (generated per D00 T03 §2 rules) does nothing on click beyond the refusal path.
3. Page colour modes in compositing plus F11 full screen with edge-reveal chrome. Done when: Dimmed matches `D-Focus-Dark.png` page tone, Inverted leaves an image's colours intact on the image corpus page, and full screen restores exactly on F11 or Esc.

- [ ] Links end to end with the ask-first dialog honoring the Reading setting. Done when: with "ask" off, hosted links open directly and file links still ask (SPEC 9.9 scope). Cheaper substitute that fails the checkpoint: trusting the URI scheme allowlist alone with no prompt.
- [ ] Dimmed and Inverted page modes as compositing only. Done when: switching modes re-renders zero tiles (scheduler telemetry flat).
- [ ] Commit: `viewer: safe links, dark page colour modes and full screen`

**Test checkpoint:** rendered proof in findings: link dialog against `D-Dialogs.png`, dimmed page against `D-Focus-Dark.png`, inverted image page visually intact, and the launch-action file inert.

## Verification

- Plan section 8 targets owned here (first page, scroll frame rate, zoom preview and sharp latency, one-document memory with cache budgets) measured on both reference machines and quoted.
- The three viewer boards matched on rendered surfaces; SPEC section 10 view keys all exercised.
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes.
