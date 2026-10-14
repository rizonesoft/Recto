---
schema_version: 1
id: document-view
domain: 02-viewer
status: active
title: "TODO-02 -- Document View: Presentation, Tiles, Layouts, Scroll, Zoom, Toolbar and Links"
track: M2
---

# TODO-02 -- Document View: Presentation, Tiles, Layouts, Scroll, Zoom, Toolbar and Links

> **Goal:** Pages on screen at the foundation plan's performance targets: a per-view Direct3D 11 / Direct2D 1.3 presentation thread drawing on a `SwapChainPanel`, lock-free viewport state and immediate page-outline loading placeholders, worker-uploaded D3D11 textures with a 512 MB LRU cache, seam-free tile compositing, unified geometry for Continuous, Single-Page and Two-Page Spread layouts, 60 fps scroll and `Ctrl+G` Go-to-Page, zoom with in-frame stretched previews, the SPEC 8.2 Floating Page Toolbar with reading auto-fade, safe link handling (`"Open this link?"` dialog and launch-action refusal), dark-theme page colour compositing (`Original`, `Dimmed`, `Inverted`), and `F11` Full Screen mode.

> **Current state:** Measured 2026-10-14. No viewer code exists yet. `D01 T02` delivers `Tile` buffers (BGRA premultiplied, explicit stride) through `TileScheduler` callbacks; `D02 T01` delivers `ShellPage`, `DocumentTabSession` (`§3`), named slots (`§4`), and `RectoDialogStyles.xaml` (`§10`). The architecture is fixed by foundation plan sections 4.5, 4.7, 4.8 and 8, and the interface by `design/SPEC.md` sections 4, 7, 8.2, 9.2, 9.9, 10, 11 and 12.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 4.5 (thread map: presentation thread owns its D3D11/D2D device contexts), 4.7 (tile path and stretched zoom preview), 4.8 (presentation thread and 512 MB starting texture-cache budget), 8 (performance targets), and 9 (link and launch-action security rules).
- `design/SPEC.md` sections 4 (pages in dark theme: Original, Dimmed, Inverted), 7 (page shadow, canvas), 8.2 (floating page toolbar), 9.2 (viewer behavior), 9.9 (link dialog), 10 (keyboard shortcuts), 11 (accessibility), 12 (motion); boards `D-Focus-Light.png`, `D-Focus-Dark.png`, `D-Classic.png`, `D-ReadingRoom.png`, `D-Dialogs.png`.
- -> XREF: D01 T02 §7 -- the tile hand-off this file consumes: `TileKey`, BGRA premultiplied stride and completion callback are defined there (`§7`) and `TileScheduler` (`§8`) and uploaded to D3D11 textures in §3 here.
- -> XREF: D03 T01 §3 -- text selection (`§3`), search highlights (`§6`), and comparison overlays (`§9`) render over these pages using the page-to-viewport coordinate mapping defined in §5 here.
- -> XREF: D03 T03 §3 -- the `Annotate / Tools` contextual mode bars (`Markup`, `Form Designer`, `Redact & Sanitize`) attach to §8's `PageToolbar` toggle and `More` menu.
- -> XREF: D04 T01 §3 -- the `Convert` primary accent button on §8's `PageToolbar` navigates to `ConvertPage` (`D04 T01 §3`).
- -> XREF: D04 T02 §6 -- the `Extract Tables & Images` contextual mode bar attaches to §8's `PageToolbar` `More` menu.
- -> XREF: D05 T02 §1 -- the `Organize` mode switcher and page-surgery commands attach to §8's `PageToolbar` `More` menu.
- -> XREF: D06 T02 §1 -- the security audit (`Security.ActionRefusal` and link prompt audit) verifies §9's untrusted-action refusal boundary.
- -> XREF: D07 T01 §1 -- the `Print` button (`Ctrl+P`) on §8's `PageToolbar` invokes `D07 T01 §1`'s `PrintDialog`.

## Outcome

- A 100-page text PDF shows its first page within 300 ms on the reference machines (`D00 T01 §5`), scrolls at 60 fps with zero blank tiles at normal reading speed, and zooms with an immediate in-frame stretched preview and sharp tiles within 150 ms.
- Continuous, Single-Page, and Two-Page Spread (lone cover, then side-by-side pairs) layouts all work over one shared `PageLayoutGeometry` model with `0°`, `90°`, `180°`, and `270°` rotation.
- The SPEC 8.2 Floating Page Toolbar drives page navigation, zoom, rotation, tools, conversion, and the More menu, auto-fading to `40%` opacity after 3 seconds of idle reading (unless OS animations are disabled).
- Internal links navigate with back-history, external links prompt via the SPEC 9.9 `"Open this link?"` dialog, launch/script actions never execute, dark-theme page colour modes (`Original`, `Dimmed`, `Inverted`) composite on the GPU without re-rendering tiles, and `F11` Full Screen mode reveals chrome on screen-edge hover.

**Adjacency:** list=not-applicable (finding pages is the thumbnail and outline panels, D02 T03 §4 and §5); document=applicable @ D07 T01 §1; settings=applicable @ D02 T04 §5; reporting=not-applicable (no reports over viewing state); notifications=not-applicable (viewing produces no lifecycle notices); permissions=applicable @ D02 T02 §9; audit=not-applicable (reading position is session state, D02 T03 §1); exchange=not-applicable (copy and share are D03 T01 §3); reverse=not-applicable (view changes are stateless; nothing is created)

**Adjacency rationale:** Print (the document surface of viewing) is owned in `D07 T01 §1`. Permissions here means the document-level refusal boundary (external links ask before opening, `/Launch` and `/JavaScript` actions never execute) owned and exercised in §9. Reading settings (`Default zoom`, `Default page layout`, `Ask before opening links`, `Pages in dark theme`, `Page cache size`) live in `D02 T04 §§4-6` and are consumed here.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `SwapChainPanel`, Direct3D 11 / Direct2D 1.3 device creation and presentation thread | D02 T01 §3, D02 T01 §4 | [ ] |
| 2 | §2 | Lock-free viewport state channel, DPI scaling and page-outline loading placeholders | §1, D01 T01 §10 | [ ] |
| 3 | §3 | Worker D3D11 texture upload and LRU budgeted tile texture cache | §2, D01 T02 §7 | [ ] |
| 4 | §4 | Viewport-to-scheduler tile driver and seam-free page compositing | §3, D01 T02 §8 | [ ] |
| 5 | §5 | Page geometry engine for Continuous, Single-Page and Two-Page Spread layouts | §2 | [ ] |
| 6 | §6 | Continuous scrolling, inertia, keyboard page nav, rotation and Go-to-Page (`Ctrl+G`) | §4, §5 | [ ] |
| 7 | §7 | Zoom engine with in-frame stretched tile preview and anchor preservation | §4, §5 | [ ] |
| 8 | §8 | Floating Page Toolbar (`Focus`) and reading auto-fade behavior | §6, §7, D02 T01 §9 | [ ] |
| 9 | §9 | Link hit-testing, internal navigation, `"Open this link?"` prompt and launch refusal | §5, §6, D02 T01 §10 | [ ] |
| 10 | §10 | Dark-theme page colour modes (`Original`, `Dimmed`, `Inverted`) and `F11` Full Screen | §4, §8 | [ ] |

## 1. `SwapChainPanel`, Direct3D 11 / Direct2D 1.3 device creation and presentation thread

Implement foundation plan section 4.8's presentation foundation in `src/app/DocumentView.xaml/.h/.cpp` and `src/services/presentation.h/.cpp`: each open `DocumentView` owns a dedicated presentation thread that is the sole caller of its `ID3D11DeviceContext` and `ID2D1DeviceContext`, presents onto a DXGI flip-model swap chain (`DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`, `DXGI_FORMAT_B8G8R8A8_UNORM`) bound to WinUI's `SwapChainPanel` via `ISwapChainPanelNative`, handles GPU device-lost recovery (`DXGI_ERROR_DEVICE_REMOVED` / `DXGI_ERROR_DEVICE_RESET`), and registers into `Runtime`'s ordered shutdown sequence (`D01 T02 §4`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4 (`RectoCanvasBrush`: `#E6E7EA` light / `#141312` dark) and 7 (page canvas corner radius 12) with `design/screenshots/D-Focus-Light.png` and `D-Focus-Dark.png`.
**Job:** The user gets a tear-free, hardware-accelerated document canvas that never blocks the UI thread and recovers automatically if the graphics driver resets. Consumer: §2's viewport channel and §4's tile compositor draw onto this presentation surface.
**Treatment:** Dedicated presentation thread per `DocumentView` with hardware `D3D11CreateDevice` (`D3D11_CREATE_DEVICE_BGRA_SUPPORT`), WARP fallback only when no hardware adapter exists, multithreaded `ID2D1Factory1`, and `ISwapChainPanelNative` binding. Cheaper substitute that fails the checkpoint: drawing PDF pages into XAML `Image` / `WriteableBitmap` controls on the UI thread.
**Chrome:** mount `DocumentView` inside `ShellPage`'s `CenterCanvasSlot` (`D02 T01 §4`) and clear the swap chain to `RectoCanvasBrush` sampled from the active theme dictionary. Do not hard-code hex clear colours.

**Build order:**

1. Author `src/services/presentation.h/.cpp` (`PresentationRenderer`): create the hardware `ID3D11Device` (`D3D11_CREATE_DEVICE_BGRA_SUPPORT`), `IDXGIDevice`, `IDXGIAdapter`, `IDXGIFactory2`, flip-model `IDXGISwapChain1`, and `ID2D1DeviceContext` (`D2D1_FACTORY_TYPE_MULTI_THREADED`), and run the dedicated presentation loop thread waking on a condition variable / frame event and joining cleanly on stop. Done when: starting and stopping `PresentationRenderer` 50 times in a unit test leaks zero COM interfaces (`ID3D11Debug` live object report clean) and joins before `Runtime` shutdown completes.
2. Create `src/app/DocumentView.xaml/.h/.cpp` hosting a `SwapChainPanel` (`CornerRadius=12`), bind the swap chain on the UI thread via `ISwapChainPanelNative::SetSwapChain`, clear to the theme-resolved `RectoCanvasBrush` colour, and implement `handle_device_lost()` so `DXGI_ERROR_DEVICE_REMOVED` or `DXGI_ERROR_DEVICE_RESET` recreates the D3D11/D2D devices and swap chain automatically. Done when: `DocumentView` renders the `RectoCanvasBrush` background in Light and Dark themes and recovers cleanly from a simulated device-lost signal in `recto-tests`.

- [ ] Author `src/services/presentation.h/.cpp` with hardware `D3D11CreateDevice` (`D3D11_CREATE_DEVICE_BGRA_SUPPORT`), automatic WARP fallback if hardware creation fails, and `ID2D1Factory1` / `ID2D1Device` / `ID2D1DeviceContext` initialization. Done when: device creation succeeds on both discrete/integrated GPUs and headless CI adapters.
- [ ] Create the `IDXGISwapChain1` with `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`, `DXGI_FORMAT_B8G8R8A8_UNORM`, `DXGI_ALPHA_MODE_IGNORE`, and 2 buffers for low-latency presentation. Done when: `Present(1, 0)` runs on the presentation thread with zero DXGI debug layer warnings.
- [ ] Author `src/app/DocumentView.xaml/.h/.cpp` containing the `SwapChainPanel` and bind `ISwapChainPanelNative::SetSwapChain` when the control loads into `ShellPage`'s `CenterCanvasSlot`. Done when: `DocumentView` mounts inside active document tabs. Cheaper substitute that fails the checkpoint: rendering on the UI thread via `SurfaceImageSource` or `WriteableBitmap`.
- [ ] Sample `RectoCanvasBrush` (`#E6E7EA` in Light, `#141312` in Dark) from the active XAML theme resources and pass the resolved `D2D1_COLOR_F` to `PresentationRenderer` so the canvas background matches the active theme with zero hex literals in C++. Done when: switching Light and Dark themes updates the swap-chain clear colour immediately.
- [ ] Implement `handle_device_lost()` in `PresentationRenderer` detecting `DXGI_ERROR_DEVICE_REMOVED` and `DXGI_ERROR_DEVICE_RESET` during `Present` or `ResizeBuffers`, releasing device-dependent resources, recreating the D3D11/D2D devices and swap chain, and notifying the tile cache (§3) to repopulate. Done when: injecting a synthetic device-lost flag in tests recovers presentation within one frame without crashing.
- [ ] Register `PresentationRenderer` lifetime with `DocumentTabSession` (`D02 T01 §3`) and `Runtime` (`D01 T02 §4`) so closing a tab stops and joins the presentation thread before its `DocumentActor` tears down. Done when: closing a tab mid-frame never accesses a destroyed swap chain or actor.
- [ ] Add `View.PresentationLifecycleAndDeviceLost` test in `tests/recto-tests/` exercising presentation thread startup/join, theme clear-colour updates, and synthetic device-lost recovery. Done when: `recto-tests.exe --gtest_filter=View.PresentationLifecycleAndDeviceLost` passes.
- [ ] Commit: `viewer: add SwapChainPanel, D3D11/D2D1 presentation thread and device-lost recovery`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.PresentationLifecycleAndDeviceLost` passes on the display-session host with zero D3D11 debug layer leaks and verified device-lost recovery.

## 2. Lock-free viewport state channel, DPI scaling and page-outline loading placeholders

Build the non-blocking UI-to-presentation viewport channel (`ViewportState`) and the SPEC section 9.2 immediate loading state: the UI thread posts atomic viewport snapshots (`scroll_x`, `scroll_y`, `zoom`, `panel_width`, `panel_height`, `CompositionScaleX/Y` DPI scale, layout mode, page colour mode) without ever waiting on the presentation thread; the presentation thread resizes the swap chain and `IDXGISwapChain2::SetMatrixTransform` smoothly on DPI/size changes and draws true-size page outlines with the SPEC section 7 soft page shadow (`1 epx` key, `24 epx` ambient) from `DocumentDescription` page dimensions before the first tile arrives.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7 (page shadow: `1 epx` key, `24 epx` ambient, square page corners) and 9.2 ("Loading: show page outlines at the right size immediately and fill them as tiles arrive; never a spinner over the whole window") with `design/screenshots/D-Focus-Light.png` and `D-Focus-Dark.png`.
**Job:** The moment a PDF opens, the user sees properly proportioned pages with soft shadows on the canvas that resize and scroll at the monitor's refresh rate with zero full-window spinners or UI freezes. Consumer: `TileTextureCache` (§3) and `PageLayoutGeometry` (§5).
**Treatment:** Latest-only lock-free/mutex-coalesced `ViewportState` hand-off from UI thread to presentation thread, swap-chain `ResizeBuffers` + `SetMatrixTransform` inverse scale for crisp Per-Monitor V2 DPI rendering, and Direct2D page-placeholder rendering with cached shadow geometry. Cheaper substitute that fails the checkpoint: showing a `ProgressRing` spinner while waiting for the first page to rasterize, or blocking the UI thread inside `SizeChanged` while `ResizeBuffers` runs.
**Chrome:** consume `RectoCanvasBrush`, `RectoBorderBrush`, and `RectoRaisedBrush` for page outlines and shadows.

**Build order:**

1. Define `ViewportState` and `ViewportChannel` in `src/services/presentation.h/.cpp` where `post_viewport_state(ViewportState)` overwrites the pending snapshot and wakes the presentation thread, and wire `SwapChainPanel::SizeChanged` and `SwapChainPanel::CompositionScaleChanged` in `DocumentView` to post updated dimensions and DPI scales. Done when: a synthetic storm of 200 rapid resize and DPI-scale updates completes in under `2 ms` on the UI thread while the presentation thread coalesces and resizes cleanly.
2. Feed `DocumentDescription::pages` (page widths, heights, and intrinsic rotations from `D01 T01 §4`) into `PresentationRenderer` as soon as `Runtime::open` succeeds, and draw each visible page's paper rect (`#FFFFFF` in Original, `#D9D5CE` in Dimmed, `#1B1A19` in Inverted), `1 epx` key border, and `24 epx` ambient drop shadow before any tiles are cached. Done when: opening a 100-page corpus file renders true-size shadowed page placeholders on the very first frame after `DocumentDescription` arrives.

- [ ] Define `ViewportState` (`scroll_x`, `scroll_y`, `zoom`, `width_epx`, `height_epx`, `dpi_scale_x`, `dpi_scale_y`, `layout_mode`, `page_color_mode`, `sequence_number`) and coalescing `ViewportChannel` in `src/services/presentation.h/.cpp`. Done when: posting 1,000 states from the UI thread never blocks and the consumer reads the latest `sequence_number`.
- [ ] Wire `SwapChainPanel::SizeChanged` and `SwapChainPanel::CompositionScaleChanged` in `src/app/DocumentView.xaml.cpp` to `ViewportChannel::post`, applying `IDXGISwapChain2::SetMatrixTransform` (`1.0f / dpi_scale_x`, `1.0f / dpi_scale_y`) and `ResizeBuffers` on the presentation thread. Done when: moving the window between a `100%` DPI monitor and a `200%` DPI monitor keeps the swap chain at native physical pixel resolution without flicker.
- [ ] Clamp `ResizeBuffers` pixel dimensions to `[1, D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION]` (`16384`) so minimized (`0 x 0`) or extreme multi-monitor windows never trigger `DXGI_ERROR_INVALID_CALL`. Done when: minimizing and restoring the window produces zero DXGI errors.
- [ ] Deliver `DocumentDescription` page dimensions (`width_pt`, `height_pt`, `rotation`) to `PresentationRenderer` immediately upon open completion. Done when: page placeholders have exact aspect ratios matching the PDF's `/MediaBox` and `/CropBox` before any tile renders.
- [ ] Render the SPEC section 7 page shadow (`1 epx` key shadow + `24 epx` soft ambient shadow, square page corners) and paper background rectangle for every visible page in the viewport. Done when: page outlines and shadows visually match `D-Focus-Light.png` and `D-Focus-Dark.png`. Cheaper substitute that fails the checkpoint: showing a loading spinner instead of true-size page outlines.
- [ ] Add `View.ViewportChannelAndPlaceholders` test in `tests/recto-tests/` verifying non-blocking resize/DPI storm coalescing, `0 x 0` minimize safety, and immediate page-outline placeholder geometry. Done when: `recto-tests.exe --gtest_filter=View.ViewportChannelAndPlaceholders` passes.
- [ ] Commit: `viewer: add coalesced ViewportChannel, DPI scaling and page-outline placeholders`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.ViewportChannelAndPlaceholders` passes on the display-session host, and rendered page outlines with soft shadows match `D-Focus-Light.png`.

## 3. Worker D3D11 texture upload and LRU budgeted tile texture cache

Implement foundation plan sections 4.7 and 4.8's GPU tile upload and caching pipeline in `src/services/tile_cache.h/.cpp`: `RenderPool` workers upload rasterized BGRA premultiplied `Tile` bitmaps directly into immutable `ID3D11Texture2D` GPU textures via the free-threaded `ID3D11Device` (honoring explicit row `stride`), and hand them to the presentation thread's `TileTextureCache`, which enforces a configurable byte budget (default `512 MB`, tunable `128 MB` to `2 GB` from Settings `D02 T04 §6`) with distance-from-viewport LRU eviction and test-visible telemetry.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Implement worker-thread `ID3D11Device::CreateTexture2D` upload in `src/services/tile_cache.cpp` (`D3D11_USAGE_IMMUTABLE`, `DXGI_FORMAT_B8G8R8A8_UNORM`, `D3D11_SUBRESOURCE_DATA::SysMemPitch = tile.stride`) so pixel-to-GPU upload happens on `RenderPool` worker threads off both the UI thread and the presentation thread's immediate context. Done when: concurrent worker threads creating textures for odd-width/padded-stride tiles succeed with zero D3D11 debug warnings or stride skew.
2. Build `TileTextureCache` (`src/services/tile_cache.h/.cpp`) keyed by `TileKey` (`page_index`, `zoom_bucket`, `rotation`, `tile_x`, `tile_y`, `ocg_generation`), tracking exact GPU texture byte size (`width * height * 4`), enforcing `max_budget_bytes` (default `512 * 1024 * 1024`), evicting least-recently-used tiles farthest from the active viewport page range first, and exposing `TileCacheStats` (`current_bytes`, `budget_bytes`, `tile_count`, `hit_count`, `eviction_count`). Done when: scrolling a 500-page document end to end keeps `current_bytes <= budget_bytes` at every frame and lowering the budget live immediately evicts excess textures.

- [ ] Implement free-threaded `ID3D11Device::CreateTexture2D` upload on `RenderPool` workers using `D3D11_SUBRESOURCE_DATA` with `SysMemPitch = tile.stride` (never assuming `stride == width * 4`). Done when: odd-width tiles (`width = 513`) with padded row strides upload without diagonal shearing. Cheaper substitute that fails the checkpoint: uploading pixel buffers on the presentation thread inside the frame loop, or counting tile count instead of byte size.
- [ ] Wrap each uploaded `ID3D11Texture2D` in an `ID2D1Bitmap1` (`CreateBitmapFromDxgiSurface` with `D2D1_ALPHA_MODE_PREMULTIPLIED`) on the presentation thread when inserting into `TileTextureCache`. Done when: cached tiles draw directly via `ID2D1DeviceContext::DrawBitmap`.
- [ ] Implement `TileTextureCache` keyed by `TileKey` (`page_index`, `zoom_bucket`, `rotation`, `tile_x`, `tile_y`, `ocg_generation`) with O(1) lookup and exact byte accounting (`width_px * height_px * 4`). Done when: `TileTextureCache` unit tests verify exact byte totals across insertions, replacements, and clears.
- [ ] Enforce the configurable `budget_bytes` ceiling (default `512 MB`, range `128 MB` to `2048 MB`) by evicting non-visible LRU entries ordered by page distance from the current viewport before inserting new tiles. Done when: `stats().current_bytes` never exceeds `budget_bytes` across a simulated 500-page scroll at `200%` zoom.
- [ ] Support dynamic `set_budget_bytes(size_t)` at runtime so changing the Settings cache slider (`D02 T04 §6`) immediately trims excess cached textures if the budget was lowered. Done when: reducing `budget_bytes` from `512 MB` to `128 MB` evicts textures immediately and brings `current_bytes <= 128 MB`.
- [ ] Provide `clear_all()` for device-lost recovery (§1) and `invalidate_page(page_index)` for live OCG layer toggles (`D02 T03 §7`) and annotation/form updates (`D03 T03`). Done when: invalidating a page removes only that page's tiles while preserving neighbouring pages.
- [ ] Expose `TileCacheStats` (`current_bytes`, `budget_bytes`, `tile_count`, `hit_count`, `miss_count`, `eviction_count`) for test and performance assertions. Done when: telemetry counters increment deterministically in unit tests.
- [ ] Add `View.TileTextureCacheBudgetAndStride` test in `tests/recto-tests/` testing concurrent worker texture creation, non-multiple-of-4 strides, 500-page LRU eviction within budget, and live budget shrinking. Done when: `recto-tests.exe --gtest_filter=View.TileTextureCacheBudgetAndStride` passes.
- [ ] Commit: `viewer: implement worker D3D11 texture upload and budgeted LRU TileTextureCache`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.TileTextureCacheBudgetAndStride` passes on the display-session host, proving concurrent worker uploads, padded stride fidelity, and strict adherence to the 512 MB / 128 MB byte budgets.

## 4. Viewport-to-scheduler tile driver and seam-free page compositing

Connect `PresentationRenderer` (§1), `ViewportChannel` (§2), and `TileTextureCache` (§3) to `TileScheduler` (`D01 T02 §3`): each frame, compute the set of `TileKey`s intersecting the viewport plus a 1-page prefetch margin, cancel stale in-flight requests via `CancelToken` when the viewport moves away, ingest completed worker textures, and composite tiles onto each page's paper placeholder with pixel-snapped alignment so tile seams are invisible at every zoom level and rotation.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7 and 9.2 (pages fill as tiles arrive, zero visible seams between `512 x 512` tiles) with `design/screenshots/D-Focus-Light.png`.
**Job:** As the user opens or scrolls a PDF, sharp page tiles fill in smoothly within the 300 ms first-page target with zero hairline gaps or seams between adjacent tiles. Consumer: §6's scrolling and §7's zoom engine drive this compositor.
**Treatment:** Viewport-driven `TileScheduler` request/cancel reconciliation per frame with pixel-aligned `D2D1_RECT_F` destination rects and clipped page bounds. Cheaper substitute that fails the checkpoint: rendering whole pages as single giant textures (which blows GPU texture limits at 400% zoom) or leaving 1 px fractional seams between tiles at non-integer DPI scales.
**Chrome:** consume `TileKey`, `CancelToken`, and `TileScheduler` from `D01 T02` unchanged.

**Build order:**

1. Implement `TileViewportDriver` in `src/services/presentation.cpp`: given the current `ViewportState` and visible page rects, compute the required `512 x 512` physical-pixel `TileKey` grid for visible pages (priority 0) and adjacent prefetch pages (priority 1), cancel any in-flight `TileKey` that is no longer in the visible+prefetch window, and submit missing keys to `TileScheduler`. Done when: scrolling past pages immediately aborts their in-flight worker rasterization (`CancelToken::is_cancelled() == true`) and prioritizes the newly landed viewport pages.
2. Composite cached `ID2D1Bitmap1` tiles onto each page in `PresentationRenderer::render_frame()`: transform to physical pixel coordinates, snap tile edges to integer device pixels, apply `PushAxisAlignedClip` to the page's paper rect, and handle `0°`, `90°`, `180°`, and `270°` page rotations. Done when: rendered pages across all 4 rotations pixel-match `recto-cli render` reference PNGs (`SSIM >= 0.999`) with zero visible seams at `100%`, `125%`, `150%`, and `200%` DPI/zoom scales.

- [ ] Compute the visible and 1-page-prefetch `TileKey` set each frame from `ViewportState` and page geometry. Done when: only tiles intersecting the viewport or prefetch band are requested from `TileScheduler`.
- [ ] Order `TileScheduler` requests by distance from the viewport centre so the tiles directly under the user's eyes rasterize first. Done when: centre-screen tiles complete before off-screen prefetch tiles.
- [ ] Cancel scrolled-past in-flight tile requests via their `CancelToken` handles and discard stale completions whose zoom bucket or page is outside the active window. Done when: rapid scrolling across a 100-page document does not queue a backlog of stale page renders. Cheaper substitute that fails the checkpoint: letting every scrolled-past page finish rendering before the destination page starts.
- [ ] Wake the presentation thread immediately when a visible `TileKey` completion arrives from a `RenderPool` worker so newly finished tiles present on the very next VSync. Done when: first-page presentation on the 100-page text corpus completes in `< 300 ms` on the reference machine (`D00 T01 §5`).
- [ ] Snap tile destination rectangles to integer physical device pixels (`std::round`) and clamp UV sampling so fractional DPI scales (`125%`, `150%`, `175%`) never show hairline white or dark seams between adjacent `512 x 512` tiles. Done when: automated seam-detection scan across `512 px` tile boundaries finds zero discontinuity steps at `125%` and `150%` scales.
- [ ] Composite tiles accurately for pages with intrinsic PDF `/Rotate` values (`0`, `90`, `180`, `270`) combined with user view rotation deltas. Done when: rotated corpus pages match `recto-cli render` golden outputs.
- [ ] Add `View.TileDriverAndSeamFreeComposite` test in `tests/recto-tests/` asserting `< 300 ms` first-page tile arrival on the 100-page text fixture, scroll-past `CancelToken` cancellation, rotation parity, and zero tile-boundary seam artifacts. Done when: `recto-tests.exe --gtest_filter=View.TileDriverAndSeamFreeComposite` passes.
- [ ] Commit: `viewer: connect TileScheduler viewport driver and seam-free D2D tile compositing`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.TileDriverAndSeamFreeComposite` passes on the display-session host, first page of a 100-page text PDF renders in `< 300 ms`, and rotated/fractional-DPI pages match reference renders with zero seams.

## 5. Page geometry engine for Continuous, Single-Page and Two-Page Spread layouts

Build the unified `PageLayoutGeometry` engine (`src/services/page_layout.h/.cpp`) that powers every layout mode in SPEC section 9.2 (`Continuous` vertical scroll, `SinglePage`, and `TwoPageSpread` where page 1 is shown alone as the cover on the right followed by side-by-side pairs `2-3`, `4-5`), with `16 epx` inter-page gaps, horizontal centring on the canvas, mixed page-size support, per-page rotation, the viewport-centre current-page rule, and bidirectional `page_to_viewport` / `viewport_to_page` coordinate transforms.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `PageLayoutGeometry` (`src/services/page_layout.h/.cpp`) taking the list of page dimensions (`width_pt`, `height_pt`, `intrinsic_rotation`), view rotation delta (`0`, `90`, `180`, `270`), active `PageLayoutMode` (`Continuous`, `SinglePage`, `TwoPageSpread`), zoom factor, and viewport size (`width_epx`, `height_epx`), and computing each page's canvas rectangle (`RectF` with `16 epx` gaps and horizontal centring) plus total scrollable extent (`total_width_epx`, `total_height_epx`). Done when: O(log N) binary search (`visible_pages_in_rect`) finds visible pages in a 10,000-page document in `< 5 microseconds`.
2. Implement the current-page rule (the page whose rect contains or is closest to the viewport centre point, preferring the left page on an exact two-page spread seam) and the bidirectional coordinate transforms (`viewport_point_to_page(PointF) -> Optional<PagePoint>` and `page_rect_to_viewport(page_index, RectF) -> RectF`) handling all 4 rotation angles (`0°`, `90°`, `180°`, `270°`). Done when: round-tripping points and rectangles between PDF page space (`72 dpi` points) and viewport `epx` space across all three layouts and four rotations is accurate to `< 0.01 epx`.

- [ ] Author `src/services/page_layout.h/.cpp` defining `PageLayoutMode` (`Continuous`, `SinglePage`, `TwoPageSpread`), `PagePlacement`, and `PageLayoutGeometry`. Done when: `PageLayoutGeometry` compiles in `recto-services` with zero UI-thread dependencies.
- [ ] Compute `Continuous` vertical layout with `16 epx` vertical gaps between pages and `24 epx` outer canvas margins, centring each page horizontally even when a document mixes portrait and landscape pages. Done when: mixed-size documents centre every page around the canvas midline.
- [ ] Compute `SinglePage` layout centring the active page both horizontally and vertically (when smaller than the viewport) or scrolling within the single active page (when zoomed larger than the viewport). Done when: `SinglePage` mode displays one page at a time with clean page-step transitions.
- [ ] Compute `TwoPageSpread` layout per SPEC 9.2: page 1 (index 0) sits alone as the right-hand recto cover, followed by pairs (`[2, 3]`, `[4, 5]`, ...) separated by a `16 epx` centre gutter, aligning paired pages of unequal height along their top edges. Done when: both odd-page-count and even-page-count documents lay out cover + spreads accurately. Cheaper substitute that fails the checkpoint: pairing `[1, 2]` together so left/right book spreads are off by one page.
- [ ] Support O(log N) prefix-sum lookup (`visible_pages_in_rect`) so 1,000+ page documents compute visible pages without linear O(N) scans per frame. Done when: querying visible pages on a 5,000-page synthetic document takes `< 10 microseconds`.
- [ ] Implement `current_page_index(ViewportState)` returning the 0-based page owning the viewport centre. Done when: scrolling across page boundaries updates `current_page_index` at the exact midline crossing.
- [ ] Implement bidirectional `viewport_to_page_point` and `page_to_viewport_rect` coordinate transforms accounting for PDF bottom-left vs. top-left origin, `/CropBox` offset, intrinsic `/Rotate`, and view rotation delta (`0°`, `90°`, `180°`, `270°`). Done when: round-trip transform tests pass across all 4 rotations and all 3 layout modes.
- [ ] Add `View.PageLayoutGeometryAndTransforms` unit tests in `tests/recto-tests/` covering `Continuous`, `SinglePage`, `TwoPageSpread` (cover alone + pairs), mixed portrait/landscape pages, all 4 rotations, current-page detection, and coordinate round-trips. Done when: `recto-tests.exe --gtest_filter=View.PageLayoutGeometryAndTransforms` passes.
- [ ] Commit: `viewer: add PageLayoutGeometry for Continuous, Single-Page and Two-Page Spread layouts`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.PageLayoutGeometryAndTransforms` passes, verifying cover-alone two-page spreads, mixed page sizes, rotation transforms, and O(log N) viewport queries.

## 6. Continuous scrolling, inertia, keyboard page nav, rotation and Go-to-Page (`Ctrl+G`)

Wire interactive scrolling, view rotation, and page navigation in `DocumentView` (`src/app/DocumentView.xaml/.h/.cpp`) over `PageLayoutGeometry` (§5): mouse wheel and precision touchpad scrolling with smooth inertia, vertical and horizontal scrollbars, keyboard navigation (`Page Down`, `Page Up`, `Space`, `Shift+Space`, `Home` first page, `End` last page, arrow keys), view rotation (`Ctrl+R` right `90°`, `Ctrl+Shift+R` left `90°`) keeping the current page anchored, and `Ctrl+G` Go-to-Page activation.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (previous/next page, `"3 / 9"` editable page box), 9.2 (continuous scroll, 16 epx gaps), and 10 (`Ctrl+R`, `Ctrl+Shift+R`, `Ctrl+G`, `Home`, `End`, `Page Up`, `Page Down`) with `design/screenshots/D-Focus-Light.png`.
**Job:** The user can scroll fluidly at 60 fps with mouse, touchpad, scrollbar, or keyboard, rotate sideways pages upright in one shortcut, and jump straight to any page number with `Ctrl+G`. Consumer: `PageToolbar` (§8) and `Pages` / `Outline` panels (`D02 T03 §4`, `§5`).
**Treatment:** Direct `InteractionTracker` / `ScrollViewer` manipulation driving `ViewportChannel` (§2) at 60+ fps, rotation anchor preservation, and validated `1..page_count` `go_to_page` navigation. Cheaper substitute that fails the checkpoint: resetting scroll position to page 1 when rotating the view or changing layout mode.
**Chrome:** consume `PageLayoutGeometry` (§5) and `ViewportChannel` (§2); expose `current_page_changed` events to the toolbar and side panels.

**Build order:**

1. Wire pointer wheel, precision touchpad two-finger pan with inertia, and scrollbar thumb manipulation in `DocumentView` to update `ViewportState` scroll offsets clamped to `PageLayoutGeometry` bounds, and fire `CurrentPageChanged` whenever `PageLayoutGeometry::current_page_index` changes. Done when: continuous scrolling across a 100-page document sustains 60 fps (`<= 16.6 ms` frame time on the presentation thread) and updates `CurrentPageChanged` accurately.
2. Implement keyboard page navigation (`Home` -> page 1, `End` -> last page, `Page Down` / `Space` -> next viewport screen or next page in `SinglePage` mode, `Page Up` / `Shift+Space` -> previous screen/page, `Up`/`Down`/`Left`/`Right` arrow nudges), view rotation (`rotate_right()` `Ctrl+R`, `rotate_left()` `Ctrl+Shift+R`) preserving the current page and relative scroll offset, and `go_to_page(one_based_page)` (`Ctrl+G`) with out-of-range refusal. Done when: every SPEC section 10 navigation and rotation key moves the viewport to the expected page and coordinate.

- [ ] Wire mouse wheel, touchpad pan/inertia, and scrollbars in `src/app/DocumentView.xaml.cpp` to post clamped scroll offsets to `ViewportChannel` (§2). Done when: scrolling never overshoots document bounds and sustains 60 fps on the reference machine.
- [ ] Emit `CurrentPageChanged(uint32_t zero_based_page, uint32_t page_count)` on the UI thread whenever the viewport-centre page changes during scrolling or programmatic jumps. Done when: scrolling from page 1 to page 10 fires `CurrentPageChanged` for each crossed page.
- [ ] Implement `Home` (first page), `End` (last page), `Page Up`, `Page Down`, `Space`, `Shift+Space`, and arrow-key scrolling in `DocumentView` per SPEC section 10. Done when: in `Continuous` mode `Page Down` advances by 85% of viewport height, and in `SinglePage` / `TwoPageSpread` modes `Page Down` advances by exactly one page/spread.
- [ ] Implement `rotate_clockwise()` (`Ctrl+R`) and `rotate_counterclockwise()` (`Ctrl+Shift+R`) updating the view rotation delta (`0° -> 90° -> 180° -> 270°`), recomputing `PageLayoutGeometry`, and re-anchoring scroll offset so the current page remains centred in the viewport. Done when: rotating on page 42 keeps page 42 in the centre of the viewport. Cheaper substitute that fails the checkpoint: jumping back to page 1 on rotation or layout change.
- [ ] Preserve the current page when switching `PageLayoutMode` between `Continuous`, `SinglePage`, and `TwoPageSpread`. Done when: switching from `Continuous` on page 15 to `TwoPageSpread` lands on the spread containing page 15.
- [ ] Implement `go_to_page(int32_t one_based_page)` (`Ctrl+G`, previous/next page buttons) that scrolls directly to the top of the target page and rejects `<= 0`, `> page_count`, or non-numeric input without moving the viewport. Done when: `go_to_page(5)` lands on page 5 and `go_to_page(999)` on a 10-page file leaves the current page unchanged.
- [ ] Maintain a navigation back/forward stack (`navigate_back` `Alt+Left`, `navigate_forward` `Alt+Right`) recording discrete jumps (`go_to_page`, `Home`/`End`, internal link clicks §9, outline clicks `D02 T03 §5`). Done when: jumping from page 2 to page 50 and pressing `Alt+Left` returns to page 2.
- [ ] Add `View.ScrollRotateAndNavigation` test in `tests/recto-tests/` verifying 60 fps scroll telemetry, `Home`/`End`/`PageUp`/`PageDown`, rotation anchor preservation, layout-switch page preservation, `go_to_page` bounds checking, and `Alt+Left` back-navigation. Done when: `recto-tests.exe --gtest_filter=View.ScrollRotateAndNavigation` passes.
- [ ] Commit: `viewer: implement scrolling, keyboard navigation, view rotation and Go-to-Page`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.ScrollRotateAndNavigation` passes on the display-session host, verifying page navigation keys, rotation anchor preservation, and 60 fps scroll frame times.

## 7. Zoom engine with in-frame stretched tile preview and anchor preservation

Implement foundation plan section 4.7's two-phase zoom engine in `DocumentView` and `PresentationRenderer`: discrete zoom buckets (`25%` to `500%`), `Fit Width` (`Ctrl+0`), `Fit Page`, `Actual Size 100%` (`Ctrl+1`), the SPEC section 8.2 preset dropdown (`Fit width`, `Fit page`, `50%`, `75%`, `100%`, `125%`, `150%`, `200%`, `300%`, `400%`), `Ctrl+Plus` / `Ctrl+Minus`, `Ctrl+Wheel`, and touchpad pinch-to-zoom. During any zoom change, `PresentationRenderer` scales the nearest already-cached zoom bucket's textures in the exact same frame (`0 ms` blanking) while `TileScheduler` renders sharp target-bucket tiles within `150 ms`, keeping the document point under the cursor (or viewport centre) stationary.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (zoom controls and preset list), 9.2 ("while zooming, stretch the nearest rendered tiles so the page never goes blank"), 10 (`Ctrl+Plus`, `Ctrl+Minus`, `Ctrl+0`, `Ctrl+1`), and 12 (zoom motion: animated scale of existing tiles, then sharp tiles replace them, no flash).
**Job:** The user can zoom in and out smoothly with `Ctrl+Wheel`, pinch, keyboard shortcuts, or toolbar presets without ever seeing a white flash or losing the paragraph they were looking at. Consumer: `PageToolbar` (§8) and `Classic` menu bar (`D02 T01 §9`).
**Treatment:** Two-level tile compositing in `PresentationRenderer` (fallback nearest cached bucket scaled with `D2D1_INTERPOLATION_MODE_LINEAR` underneath sharp target-bucket tiles as they arrive) plus exact focal-point anchor math. Cheaper substitute that fails the checkpoint: clearing the page to white while new zoom tiles rasterize, or shifting the scroll position to the top-left of the page on zoom.
**Chrome:** consume `TileTextureCache` (§3) and `PageLayoutGeometry` (§5).

**Build order:**

1. Implement the zoom model in `src/services/page_layout.h/.cpp` and `src/app/DocumentView.xaml.cpp`: `ZoomMode` (`FreePercent`, `FitWidth`, `FitPage`), discrete step presets (`25%`, `33%`, `50%`, `67%`, `75%`, `100%`, `125%`, `150%`, `200%`, `300%`, `400%`, `500%`), dynamic `FitWidth` and `FitPage` recalculation on window/panel resize, and focal-point anchor preservation (`page_point_under_anchor` stays at the same viewport pixel offset before and after zoom). Done when: zooming from `100%` to `200%` around cursor point `(x, y)` keeps the underlying PDF page coordinate within `<= 1.0 epx` of `(x, y)`.
2. Implement stretched fallback compositing in `PresentationRenderer::render_frame()`: when a target-bucket tile `(page, target_bucket, tx, ty)` is not yet in `TileTextureCache`, query `TileTextureCache::find_nearest_bucket_tiles(page, target_bucket, rect)` and draw the cached lower/higher bucket texture scaled over the tile region until the sharp target-bucket texture arrives. Done when: zooming on a corpus page produces zero blank/placeholder frames in the presentation trace and sharp tiles replace the stretched preview within `150 ms` on the reference machine (`D00 T01 §5`).

- [ ] Implement `ZoomMode` (`FitWidth` `Ctrl+0`, `FitPage`, `CustomFactor` including `ActualSize` `100%` `Ctrl+1`) and the SPEC 8.2 preset steps (`50%`, `75%`, `100%`, `125%`, `150%`, `200%`, `300%`, `400%`). Done when: `zoom_in()` (`Ctrl+Plus`) and `zoom_out()` (`Ctrl+Minus`) step cleanly through the preset ladder.
- [ ] Recompute effective zoom automatically when `ZoomMode::FitWidth` or `ZoomMode::FitPage` is active and the viewport or side panel resizes. Done when: opening/closing a docked side panel in `FitWidth` mode adjusts page scale smoothly to fill the remaining width.
- [ ] Wire `Ctrl+PointerWheelChanged` and touchpad pinch manipulation to focal-point zoom anchored at the pointer position (and anchor keyboard/toolbar zoom at the viewport centre). Done when: `Ctrl+Wheel` zoom over a word keeps that word directly under the cursor.
- [ ] Implement `TileTextureCache::find_fallback_textures(page_index, target_bucket, page_rect)` returning cached textures from the closest previously rendered zoom bucket for that page. Done when: pinning/keeping the previous zoom bucket's visible tiles until sharp replacement tiles arrive prevents premature LRU eviction during a zoom gesture.
- [ ] Composite fallback bucket textures scaled with `D2D1_INTERPOLATION_MODE_LINEAR` in the exact same frame as the zoom change, overdrawing each sub-region with `1:1` sharp target-bucket tiles as worker completions arrive. Done when: frame-by-frame capture during a `100% -> 200%` zoom step shows 0% blank paper frames (`no flash` per SPEC 12). Cheaper substitute that fails the checkpoint: showing blank white page rectangles while waiting for new tiles to render.
- [ ] Meet the foundation plan section 8 zoom latency target: stretched preview in the same frame (`<= 16.6 ms`) and sharp tiles replacing the visible viewport within `150 ms` on the reference machine. Done when: telemetry timestamps in `PresentationRenderer` record `< 150 ms` time-to-sharp on the 100-page text fixture.
- [ ] Add `View.ZoomStretchedPreviewAndAnchor` test in `tests/recto-tests/` asserting zero blank frames during zoom transitions, `< 1.0 epx` focal-point drift, `FitWidth`/`FitPage` resize reactivity, and `< 150 ms` sharp-tile convergence. Done when: `recto-tests.exe --gtest_filter=View.ZoomStretchedPreviewAndAnchor` passes.
- [ ] Commit: `viewer: implement zoom engine with in-frame stretched preview and anchor preservation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.ZoomStretchedPreviewAndAnchor` passes on the display-session host, with zero blank frames during zoom and sharp tiles arriving within `150 ms`.

## 8. Floating Page Toolbar (`Focus`) and reading auto-fade behavior

Build the SPEC section 8.2 `PageToolbar` control (`src/app/controls/PageToolbar.xaml/.h/.cpp`) hosted in `BottomOverlaySlot` (`Focus` default, centred `22 epx` above the bottom of the canvas) and reparentable into `TopBarSlot` (`Classic` and `Reading room`): `RectoRaisedBrush`, `CornerRadius=16`, `Padding=6 epx`, `ThemeShadow` Z=32, `36 x 36 epx` minimum button hit targets, exact SPEC 8.2 control order and `More` menu items, `RectoAccentTintBrush` toggled states, and the 3-second pointer-idle fade to `40%` opacity while reading (respecting the OS `"Show animations in Windows"` accessibility setting).

> **Deferred:** `Find` button activation -> XREF: D03 T01 §6
> **Deferred:** `Annotate / Tools` contextual mode bars (`Markup`, `Form Designer`, `Redact & Sanitize`, `Table/Image Extractor`, `Sign Document`, `Compare PDFs`, `Crop & Resize`, `Watermark & Header/Footer`, `Optimize PDF`) -> XREF: D03 T03 §3
> **Deferred:** `Convert` accent button navigation -> XREF: D04 T01 §3
> **Deferred:** `Print` button action -> XREF: D07 T01 §1

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 (`16 epx` toolbar icons), 7 (`CornerRadius=16`, `10 epx` inner button radius, `ThemeShadow` Z=32), 8.2 (Floating page toolbar order, `More` menu items, 3 s fade to 40% opacity), and 11 (`36 x 36 epx` hit targets, `AutomationProperties.Name` and shortcut tooltips on every icon-only button, reduced-motion compliance) with `design/screenshots/D-Focus-Light.png`, `D-Focus-Dark.png` and `D-Classic.png`.
**Job:** The user has every primary reading action (`Side panel`, `Page X / Y`, `Zoom`, `Fit width`, `Find`, `Rotate`, `Annotate`, `Print`, `Convert`, and `More`) in one quiet pill that fades out of the way while reading and works identically when docked in `Classic` or `Reading room`. Consumer: `ShellPage` slots (`D02 T01 §4`), `CommandRegistry` (`D02 T03 §11`), and `LayoutPresetEngine` (`D02 T04 §3`).
**Treatment:** Single `PageToolbar` control instance shared across all three layout presets with `36 x 36 epx` hit targets, inline editable `"3 / 9"` page-number box (`Ctrl+G`), zoom preset flyout, `More` menu flyout, and idle timer gating opacity between `1.0` and `0.4`. Cheaper substitute that fails the checkpoint: building separate toolbar controls for `Focus` and `Classic`, hiding the toolbar completely (`Opacity = 0`), or omitting `AutomationProperties.Name` and shortcut tooltips.
**Chrome:** consume `RectoRaisedBrush`, `RectoBorderBrush`, `RectoAccentBrush`, `RectoAccentTintBrush`, and `BottomOverlaySlot` / `TopBarSlot` (`D02 T01 §4`).

**Build order:**

1. Author `src/app/controls/PageToolbar.xaml/.h/.cpp` in the exact SPEC 8.2 left-to-right group order separated by `1 epx` vertical `RectoBorderBrush` dividers: Group 1 (`Side panel toggle` `Ctrl+B`, toggled state in `RectoAccentTintBrush`); Group 2 (`Previous page`, inline editable `"3 / 9"` page box where `Ctrl+G` focuses and selects the page number, `Enter` calls `DocumentView::go_to_page`, and `Esc` restores the current page label, `Next page`); Group 3 (`Zoom out` `Ctrl+Minus`, `Zoom %` button opening the preset flyout with `Fit width`, `Fit page`, `50%`, `75%`, `100%`, `125%`, `150%`, `200%`, `300%`, `400%`, `Zoom in` `Ctrl+Plus`, `Fit width` `Ctrl+0` toggled state in `RectoAccentTintBrush`); Group 4 (`Find` `Ctrl+F`, `Rotate` `Ctrl+R`, `Annotate / Tools` toggle `Ctrl+Shift+A`, `Print` `Ctrl+P`); Group 5 (`Convert` primary accent button with `RectoAccentBrush` background and `RectoOnAccentBrush` text); and Group 6 (`More` `...` button opening a `MenuFlyout` with `Save a copy (Ctrl+Shift+S)`, `Page layout` (`Continuous`, `Single page`, `Two-page view`), `Page colour` (`Original`, `Dimmed`, `Inverted`), `Form designer`, `Redact & sanitize (Ctrl+Shift+X)`, `Sign document (Ctrl+Shift+G)`, `Compare PDFs (Ctrl+Shift+D)`, `Extract tables/images`, `Crop & page setup`, `Watermark & header/footer`, `Optimize PDF`, and `Properties`). Done when: every button and `More` menu item exists with `36 x 36 epx` minimum hit target, `AutomationProperties.Name`, and shortcut tooltip.
2. Implement the 3-second reading auto-fade timer on `PageToolbar` when hosted in `BottomOverlaySlot`: after `3000 ms` without pointer movement over `DocumentView` and with no flyout or text box focused inside the toolbar, animate `Opacity` to `0.4` over `167 ms`; restore `Opacity = 1.0` immediately on pointer movement, pointer hover, or keyboard focus inside the toolbar; and keep `Opacity = 1.0` permanently when docked in `TopBarSlot` or when `UISettings().AnimationsEnabled()` is false. Done when: driven tests verify the 3-second fade to `0.4`, hover/focus restoration to `1.0`, never hiding below `0.4`, and `1.0` opacity when animations are disabled.

- [ ] Build `src/app/controls/PageToolbar.xaml/.h/.cpp` with `RectoRaisedBrush`, `CornerRadius=16`, `Padding=6 epx`, inner button `CornerRadius=10`, and `36 x 36 epx` minimum hit targets per SPEC 7, 8.2, and 11. Done when: visual metrics match `D-Focus-Light.png` and `D-Focus-Dark.png`.
- [ ] Wire the `"3 / 9"` click-to-type page box (`Ctrl+G`): clicking the label or pressing `Ctrl+G` switches to a compact `TextBox` with the current page number selected; pressing `Enter` navigates via `DocumentView::go_to_page` (§6) and returns focus to the canvas; pressing `Esc` cancels edit mode. Done when: typing `7` and pressing `Enter` jumps to page 7.
- [ ] Wire the zoom buttons and `Zoom %` flyout (`Fit width`, `Fit page`, `50%` to `400%`) to `DocumentView`'s zoom engine (§7), updating the displayed percentage (`"100%"`, `"125%"`) and `Fit width` toggle highlight (`RectoAccentTintBrush`) live. Done when: zooming by wheel or menu updates the toolbar percentage and toggle state immediately.
- [ ] Populate the `More` (`...`) `MenuFlyout` with every SPEC 8.2 command (`Save a copy`, `Page layout`, `Two-page view`, `Page colour`, `Form designer`, `Redact & sanitize`, `Sign document`, `Compare PDFs`, `Extract tables/images`, `Crop & page setup`, `Watermark & header/footer`, `Optimize PDF`, `Properties`). Done when: all 13 `More` items are present and bound to their live handlers or named deferred owners. Cheaper substitute that fails the checkpoint: omitting items from the SPEC 8.2 `More` list.
- [ ] Set `AutomationProperties.Name` and a tooltip (including keyboard shortcut where applicable) on every icon-only button in `PageToolbar`. Done when: automated UI tree inspection finds zero unnamed buttons in `PageToolbar`.
- [ ] Implement the 3-second pointer-idle fade to `40%` opacity (`0.4`, never `0.0`) when floating in `BottomOverlaySlot`, restoring `100%` opacity on pointer hover or keyboard focus, and suppressing the fade while any toolbar flyout or the page-number input is active. Done when: idle timer transitions `Opacity` to `0.4` at `3000 ms` and restores `1.0` on hover or focus.
- [ ] Honor Windows `"Show animations in Windows"` (`UISettings::AnimationsEnabled`): when false, disable the 3-second toolbar fade so `PageToolbar` remains at `100%` opacity at all times per SPEC 11. Done when: toggling the animation flag in tests disables the fade completely.
- [ ] Support seamless reparenting of `PageToolbar` between `BottomOverlaySlot` (`Focus` floating pill with `ThemeShadow` Z=32) and `TopBarSlot` (`Classic` and `Reading room` docked strip without shadow). Done when: moving `PageToolbar` to `TopBarSlot` matches `D-Classic.png` and `D-ReadingRoom.png`.
- [ ] Add `View.PageToolbarAndAutoFade` test in `tests/recto-tests/` verifying control order, `36 x 36 epx` hit targets, automation names, `"3 / 9"` inline page entry, `More` menu completeness, 3-second fade to `0.4`, and reduced-motion override. Done when: `recto-tests.exe --gtest_filter=View.PageToolbarAndAutoFade` passes.
- [ ] Commit: `viewer: build SPEC 8.2 Floating Page Toolbar with inline page box, More menu and 3s auto-fade`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.PageToolbarAndAutoFade` passes on the display-session host, and rendered toolbar screenshots match `D-Focus-Light.png`, `D-Focus-Dark.png`, and `D-Classic.png`.

## 9. Link hit-testing, internal navigation, `"Open this link?"` prompt and launch refusal

Implement the viewer's link interaction and untrusted-action security boundary (`design/SPEC.md` sections 2, 9.7, 9.9 and foundation plan section 9): extract link annotations per page from `DocumentActor` (`D01 T01 §4`), hit-test pointer hover and keyboard `Tab` focus through `PageLayoutGeometry` (§5), navigate internal `GoTo` page/coordinate links directly (recording back-history in §6), prompt before opening external `http://` / `https://` / `mailto:` / `file:` URIs via the SPEC 9.9 `"Open this link?"` `ContentDialog` displaying the full unshortened URL in a monospace box (honoring the `"Ask before opening links"` setting for web links while always prompting for `file:` links), and unconditionally refuse PDF `/Launch`, `/JavaScript`, `/ImportData`, and `/SubmitForm` actions.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 2 ("Safe by default. Links and Office macros never run without the user's say-so"), 4 (`RectoAccentTextBrush` link hover/focus highlight), 9.7 (`Ask before opening links` setting), and 9.9 (`Link in a document` dialog: `"Open this link?"`, full URL in a monospace box, `Cancel`, `Open in browser`) with `design/screenshots/D-Dialogs.png`.
**Job:** The user can click internal table-of-contents and footnote links to jump around the document, inspect the true destination URL of any external link before it opens in their browser, and never be compromised by a malicious PDF `/Launch` or `/JavaScript` action. Consumer: `DocumentView`, `SettingsStore` (`D02 T04 §2`), and the security audit (`D06 T02 §1`).
**Treatment:** Page-space link quads transformed via `PageLayoutGeometry`, `OpenLinkDialog` (`src/app/dialogs/OpenLinkDialog.xaml/.h/.cpp`) built on `RectoDialogStyles.xaml` (`D02 T01 §10`) with monospace `Consolas`/`Cascadia Mono` URL display and Punycode/IDN visibility, and strict scheme allowlisting (`https`, `http`, `mailto`). Cheaper substitute that fails the checkpoint: passing link strings straight to `ShellExecuteW` or `Launcher::LaunchUriAsync` without confirmation or scheme validation.
**Chrome:** consume `RectoDialogStyles.xaml` (`D02 T01 §10`), `RectoAccentTintBrush` / `RectoAccentBrush` link hover rings, and `PageLayoutGeometry` (§5).

**Build order:**

1. Wire per-page `PdfLinkAnnotation` records from `DocumentActor` (`D01 T01 §4`) into `DocumentView`: transform link bounding rects via `PageLayoutGeometry`, change the pointer cursor to `CoreCursorType::Hand` on hover, draw a subtle `RectoAccentTintBrush` hover/focus highlight, show a tooltip with `"Go to page N"` (internal) or the target domain/URI (external), and allow keyboard `Tab` and `Enter` activation across links on visible pages. Done when: hovering or tabbing onto internal and external links in the corpus link fixture highlights them and clicking an internal `GoTo` link scrolls to the target page and pushes the origin page onto §6's back-navigation stack (`Alt+Left`).
2. Build `OpenLinkDialog` (`src/app/dialogs/OpenLinkDialog.xaml/.h/.cpp`) per SPEC 9.9 (`"Open this link?"`, full un-truncated URL in a read-only selectable monospace box, `Cancel` and `Open in browser` buttons): when an external `http://`, `https://`, or `mailto:` link is activated, show `OpenLinkDialog` if `"Ask before opening links"` is enabled (default true) and launch via `Windows::System::Launcher::LaunchUriAsync` only if confirmed; for `file://` or unknown schemes, always refuse or prompt with an explicit warning; and for `/Launch`, `/JavaScript`, `/JS`, `/Rendition`, or `/SubmitForm` actions, never execute anything and log a diagnostic refusal entry. Done when: clicking an external URL opens `OpenLinkDialog` matching `D-Dialogs.png`, clicking `Cancel` opens nothing, and clicking a crafted `/Launch` (`cmd.exe`) or `/JavaScript` link in the hostile test corpus executes zero processes or scripts.

- [ ] Cache per-page `PdfLinkAnnotation` lists in `DocumentView` and hit-test pointer coordinates through `PageLayoutGeometry::viewport_to_page_point` (§5). Done when: pointer hover over a link switches cursor to `Hand` even when the page is zoomed or rotated `90°`/`180°`/`270°`.
- [ ] Draw a subtle `RectoAccentTintBrush` fill and `1 epx` `RectoAccentBrush` outline on hovered or keyboard-focused links on the page canvas. Done when: keyboard `Tab` traversal visibly highlights each link on the current page.
- [ ] Execute internal `GoTo` links by scrolling `DocumentView` to the destination page and `(top, zoom)` target while pushing the prior viewport position onto §6's `Alt+Left` history stack. Done when: clicking a table-of-contents link jumps to the chapter and `Alt+Left` returns to the table of contents.
- [ ] Author `src/app/dialogs/OpenLinkDialog.xaml/.h/.cpp` using `RectoDialogStyles.xaml` (`D02 T01 §10`) with title `"Open this link?"`, the complete URL in a monospace (`Cascadia Mono` / `Consolas`) box on `RectoSurfaceBrush` with `1 epx` `RectoBorderStrongBrush`, and `Cancel` / `Open in browser` buttons. Done when: the rendered dialog matches the `"Open this link?"` board in `D-Dialogs.png`.
- [ ] Honor the `"Ask before opening links"` setting (`D02 T04 §5`, default `true`): when `true`, always show `OpenLinkDialog` before calling `Launcher::LaunchUriAsync`; when `false`, allow only `https://` and `http://` web links to open directly while still prompting or refusing `file://` and custom URI schemes. Done when: disabling `"Ask before opening links"` still blocks silent `file://` or custom-protocol execution. Cheaper substitute that fails the checkpoint: calling `ShellExecuteW` on arbitrary URI strings or skipping the prompt for `file://` paths when the toggle is off.
- [ ] Unconditionally refuse `/Launch` (external program launch), `/JavaScript` / `/JS`, `/SubmitForm`, and `/ImportData` actions in both `recto-pdf` and `DocumentView`, recording the blocked action in `recto::pdf::diagnostics_tail()`. Done when: clicking a crafted `/Launch` (`calc.exe`) annotation does not spawn a process and logs the refusal.
- [ ] Add `View.SafeLinksAndLaunchRefusal` test in `tests/recto-tests/` testing internal `GoTo` navigation + `Alt+Left` return, external URL dialog confirmation and cancellation, setting toggle behavior, and `/Launch` / `/JavaScript` refusal. Done when: `recto-tests.exe --gtest_filter=View.SafeLinksAndLaunchRefusal` passes.
- [ ] Commit: `viewer: implement link hit-testing, OpenLinkDialog prompt and Launch/JS refusal`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.SafeLinksAndLaunchRefusal` passes on the display-session host, `OpenLinkDialog` screenshot matches `D-Dialogs.png`, and crafted `/Launch` / `/JavaScript` annotations are proven inert.

## 10. Dark-theme page colour modes (`Original`, `Dimmed`, `Inverted`) and `F11` Full Screen

Implement SPEC section 4's dark-theme page colour compositing modes (`Original`, `Dimmed` multiplying pages towards `#D9D5CE` as in `D-Focus-Dark.png`, and `Inverted` rendering light text on `#1B1A19` without inverting embedded raster images) directly on the presentation thread's Direct2D pipeline so switching modes re-renders zero tiles, and implement `F11` Full Screen mode (`SPEC` section 10) hiding the title bar and toolbars until the pointer reaches the top or bottom edge of the screen.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4 (`Pages in dark theme`: `Original`, `Dimmed` towards `#D9D5CE`, `Inverted` on `#1B1A19` with images un-inverted) and 10 (`F11` Full screen, `Esc` exit) with `design/screenshots/D-Focus-Dark.png` and `D-ReadingRoom.png`.
**Job:** The user can read comfortably at night using `Dimmed` or `Inverted` page modes without waiting for pages to re-render or seeing inverted photographic negatives, and can press `F11` to fill the entire monitor with the page. Consumer: `PageToolbar` `More -> Page colour` (§8), `Classic` `View -> Page Colour` menu (`D02 T01 §9`), and `Settings -> Theme` (`D02 T04 §4`).
**Treatment:** GPU Direct2D colour-matrix / blend compositing over cached tile textures (plus image-bbox mask preservation from `DisplayList` in `Inverted` mode so raster images remain true-colour) and `AppWindowPresenterKind::FullScreen` with top/bottom `8 epx` edge-hover reveal for chrome. Cheaper substitute that fails the checkpoint: re-rasterizing all PDF tiles on the CPU when switching page colour modes, or inverting embedded photos into colour negatives in `Inverted` mode.
**Chrome:** consume `ViewportChannel` (§2) and `ShellPage` slots (`D02 T01 §4`).

**Build order:**

1. Implement the three `PageColorMode` compositing paths in `PresentationRenderer` (`src/services/presentation.cpp`): `Original` (standard `1:1` premultiplied BGRA bitmap draw); `Dimmed` (Direct2D colour matrix / multiply blend toning white paper `#FFFFFF` to `#D9D5CE` `R=0.851, G=0.835, B=0.808` while preserving contrast); and `Inverted` (Direct2D luminance-preserving colour inversion mapping paper `#FFFFFF` to `#1B1A19` and `#EDEAE4` text, masking out raster image bounding rects reported by each page's `DisplayList` `D01 T02 §1` so photographs and figures render in their original un-inverted colours per SPEC section 4). Done when: switching between `Original`, `Dimmed`, and `Inverted` updates the screen in one frame (`0` new tile requests sent to `TileScheduler`), `Dimmed` paper samples at `#D9D5CE`, and `Inverted` preserves photo colours on the image corpus page.
2. Implement `F11` Full Screen mode in `ShellPage` and `DocumentView`: pressing `F11` switches `AppWindow::SetPresenter(AppWindowPresenterKind::FullScreen)` and hides `TitleBarSlot`, `TopBarSlot`, and `BottomOverlaySlot`; moving the pointer within `8 epx` of the top screen edge slides down `TitleBarSlot`, and within `8 epx` of the bottom edge reveals `BottomOverlaySlot`; pressing `F11` again or `Esc` (when no higher overlay is open) restores `AppWindowPresenterKind::Overlapped` and the active preset's chrome. Done when: `F11` enters full screen, top/bottom edge hover reveals the title bar and toolbar, and `F11` / `Esc` restores the window state cleanly.

- [ ] Implement `PageColorMode::Dimmed` in `PresentationRenderer` using a Direct2D colour matrix effect that maps `#FFFFFF` paper to `#D9D5CE` without re-rasterizing any `TileTextureCache` entries. Done when: switching `Original <-> Dimmed` generates zero `TileScheduler` requests and samples paper at `#D9D5CE`.
- [ ] Implement `PageColorMode::Inverted` in `PresentationRenderer` mapping `#FFFFFF` paper to `#1B1A19` and dark text to `#EDEAE4`, while clipping/restoring un-inverted regions for raster `DrawImage` bounding boxes recorded on the page's `DisplayList`. Done when: text and vector art invert to light-on-charcoal while embedded JPEG/PNG images retain their natural colours per SPEC section 4. Cheaper substitute that fails the checkpoint: blindly inverting the entire bitmap so photos turn into colour negatives, or re-rendering tiles on the CPU.
- [ ] Apply the selected `PageColorMode` automatically when in Dark theme (defaulting to `Dimmed` per SPEC 4) and expose live switching from `PageToolbar` (`More -> Page colour`), the `View` menu (`D02 T01 §9`), and `Settings` (`D02 T04 §4`). Done when: all three surfaces stay synchronized with the active `PageColorMode`.
- [ ] Implement `F11` Full Screen toggle on `ShellPage` using `AppWindow::SetPresenter(AppWindowPresenterKind::FullScreen)` and collapsing docked/title chrome so `DocumentView` fills `100%` of the monitor. Done when: pressing `F11` expands `DocumentView` to the full monitor bounds.
- [ ] Add `8 epx` top-edge and bottom-edge pointer proximity detection in Full Screen mode to reveal `TitleBarSlot` and `PageToolbar` on hover and hide them again when the pointer moves back onto the page canvas. Done when: moving the cursor to `Y <= 8` in Full Screen reveals the title bar and tabs.
- [ ] Exit Full Screen mode on `F11` or `Esc` (when no modal dialog, Command Box, Find card, or contextual mode bar is open), restoring the prior `OverlappedPresenter` window bounds and slot visibility. Done when: pressing `Esc` in Full Screen with no open popups exits Full Screen cleanly.
- [ ] Add `View.PageColorModesAndFullScreen` test in `tests/recto-tests/` asserting zero tile re-renders across `Original` / `Dimmed` / `Inverted` switches, `#D9D5CE` dimmed paper sampling, un-inverted image preservation in `Inverted` mode, and `F11` / edge-hover / `Esc` Full Screen transitions. Done when: `recto-tests.exe --gtest_filter=View.PageColorModesAndFullScreen` passes.
- [ ] Commit: `viewer: add GPU Dimmed/Inverted dark page colour modes and F11 Full Screen`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.PageColorModesAndFullScreen` passes on the display-session host, `Dimmed` page matches `D-Focus-Dark.png`, `Inverted` leaves corpus images un-inverted, and `F11` Full Screen edge-reveal and `Esc` restore work end to end.

## Verification

- Foundation plan section 8 targets owned here (first page `< 300 ms` on a 100-page text PDF, 60 fps scroll, in-frame stretched zoom preview with sharp tiles `< 150 ms`, one-document memory within the 512 MB texture cache budget) measured on the reference machines (`D00 T01 §5`) and quoted.
- Rendered surfaces match `D-Focus-Light.png`, `D-Focus-Dark.png`, `D-Classic.png`, `D-ReadingRoom.png`, and `D-Dialogs.png`; every SPEC section 10 view shortcut (`Ctrl+Plus`, `Ctrl+Minus`, `Ctrl+0`, `Ctrl+1`, `Ctrl+R`, `Ctrl+Shift+R`, `Ctrl+G`, `Home`, `End`, `Page Up`, `Page Down`, `F11`, `Esc`) is exercised and proven.
- Full `recto-tests` suite green; `python scripts/todo-graph.py validate` passes.
