---
schema_version: 1
id: document-view
domain: 02-viewer
status: active
title: "TODO-02 -- Document View: Presentation, Tiles, Layouts, Scroll, Zoom, Toolbar and Links"
track: M2
---

# TODO-02 -- Document View: Presentation, Tiles, Layouts, Scroll, Zoom, Toolbar and Links

> **Goal:** Pages on screen at the foundation plan's performance targets: a per-view Direct3D 11 / Direct2D 1.3 presentation thread drawing into a composition swap chain hosted by `ui::SwapChainHost`, lock-free viewport state and immediate page-outline loading placeholders, worker-uploaded D3D11 textures with a 512 MB LRU cache, seam-free tile compositing, unified geometry for Continuous, Single-Page and Two-Page Spread layouts, 60 fps scroll and `Ctrl+G` Go-to-Page, zoom with in-frame stretched previews, the SPEC 8.2 Floating Page Toolbar with reading auto-fade, safe link handling (`"Open this link?"` dialog and launch-action refusal), dark-theme page colour compositing (`Original`, `Dimmed`, `Inverted`), and `F11` Full Screen mode.

> **Current state:** Measured 2026-10-14. No viewer code exists yet. `D01 T02` delivers `Tile` buffers (BGRA premultiplied, explicit stride) through `TileScheduler` callbacks; `D02 T01` delivers `ShellPage`, `DocumentTabSession` (`§3`), named slots (`§4`), and `ui::Dialog` (`§10`). The toolkit under it is `recto-ui` (`src/ui`, `todo/10-ui/`): `ui::SwapChainHost`, `ui::ScrollViewer`, `ui::Menu`, `ui::Dialog`, `ui::Motion` and the UI Automation providers. The architecture is fixed by foundation plan sections 4.5, 4.7, 4.8 and 8, and the interface by `design/SPEC.md` sections 4, 7, 8.2, 9.2, 9.9, 10, 11 and 12.

> 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); sections rewritten against `todo/10-ui/`

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 4.5 (thread map: presentation thread owns its D3D11/D2D device contexts), 4.7 (tile path and stretched zoom preview), 4.8 (presentation thread and 512 MB starting texture-cache budget), 8 (performance targets), and 9 (link and launch-action security rules).
- `design/SPEC.md` sections 4 (pages in dark theme: Original, Dimmed, Inverted), 7 (page shadow, canvas), 8.2 (floating page toolbar), 9.2 (viewer behavior), 9.9 (link dialog), 10 (keyboard shortcuts), 11 (accessibility), 12 (motion); boards `D-Focus-Light.png`, `D-Focus-Dark.png`, `D-Classic.png`, `D-ReadingRoom.png`, `D-Dialogs.png`.
- -> XREF: D10 T01 §18 -- the page canvas swap chain host (`ui::SwapChainHost`) that §1 and §2 present into, plus the shared device (`D10 T01 §5`), window DPI and full screen (`D10 T01 §4`), input (`D10 T01 §12`) and motion (`D10 T01 §14`) used throughout.
- -> XREF: D10 T02 §10 -- `ui::ScrollViewer` smooth wheel and DirectManipulation pan and pinch behind §6 and §7; the toolbar, menus, link dialog and slide navigator build on `ui::Button`, `ui::Menu`, `ui::Dialog` and `ui::ItemsView`.
- -> XREF: D10 T03 §1 -- the UI Automation provider tree that every control here exposes (names, announcements through `D10 T03 §4`).
- -> XREF: D01 T02 §7 -- the tile hand-off this file consumes: `TileKey`, BGRA premultiplied stride and completion callback are defined there (`§7`) and `TileScheduler` (`§8`) and uploaded to D3D11 textures in §3 here.
- -> XREF: D03 T01 §3 -- text selection (`§3`), search highlights (`§6`), and comparison overlays (`§9`) render over these pages using the page-to-viewport coordinate mapping defined in §5 here.
- -> XREF: D03 T03 §3 -- the `Annotate / Tools` contextual mode bars (`Markup`, `Form Designer`, `Redact & Sanitize`) attach to §8's `PageToolbar` toggle and `More` menu.
- -> XREF: D04 T01 §3 -- the `Convert` primary accent button on §8's `PageToolbar` navigates to `ConvertPage` (`D04 T01 §3`).
- -> XREF: D04 T02 §6 -- the `Extract Tables & Images` contextual mode bar attaches to §8's `PageToolbar` `More` menu.
- -> XREF: D05 T02 §1 -- the `Organize` mode switcher and page-surgery commands attach to §8's `PageToolbar` `More` menu.
- -> XREF: D06 T02 §1 -- the security audit (`Security.ActionRefusal` and link prompt audit) verifies §9's untrusted-action refusal boundary.
- -> XREF: D07 T01 §1 -- the `Print` button (`Ctrl+P`) on §8's `PageToolbar` invokes `D07 T01 §1`'s `PrintDialog`.
- -> XREF: D02 T05 §4 -- rich media references (D02 T05 §4) use this ask-first path.
- -> XREF: D02 T05 §8 -- the 3D view (D02 T05 §8) shares this device and thread.
- -> XREF: D03 T03 §13 -- links authored with the Link tool (D03 T03 §13) activate here.
- -> XREF: D02 T04 §5 -- tint and reading settings.
- -> XREF: D03 T03 §18 -- ViewSnapProvider.
- -> XREF: D03 T04 §13 -- ViewSnapProvider.
- -> XREF: D01 T02 §11 -- line-weight option joins the tile identity.
- -> XREF: D01 T02 §11 -- TileTextureCache keys by the full TileKey.
- -> XREF: D01 T02 §12 -- page transition parser and duration accessor.
- -> XREF: D01 T02 §12 -- transitions read through the actor query.
- -> XREF: D06 T04 §5 -- the `D-Presentation.png` board and `design/SPEC.md` section 9.10 that §13, §14, §15, §16, §17 here build against.
- -> XREF: D06 T04 §6 -- the `D-ViewTools.png` board and `design/SPEC.md` section 9.11 that §18, §20, §21, §22, §23, §24 here build against.

## Outcome

- A 100-page text PDF shows its first page within 300 ms on the reference machines (`D00 T01 §5`), scrolls at 60 fps with zero blank tiles at normal reading speed, and zooms with an immediate in-frame stretched preview and sharp tiles within 150 ms.
- Continuous, Single-Page, and Two-Page Spread (lone cover, then side-by-side pairs) layouts all work over one shared `PageLayoutGeometry` model with `0°`, `90°`, `180°`, and `270°` rotation.
- The SPEC 8.2 Floating Page Toolbar drives page navigation, zoom, rotation, tools, conversion, and the More menu, auto-fading to `40%` opacity after 3 seconds of idle reading (unless OS animations are disabled).
- Internal links navigate with back-history, external links prompt via the SPEC 9.9 `"Open this link?"` dialog, launch/script actions never execute, dark-theme page colour modes (`Original`, `Dimmed`, `Inverted`) composite on the GPU without re-rendering tiles, and `F11` Full Screen mode reveals chrome on screen-edge hover.
- Reading tints (sepia, eye-care green, custom), a presentation mode with page transitions, pen, laser, presenter view and slide navigator, hands-free auto-scroll, reverse page order, rulers, grid and guides, a line weights toggle, split view of one document and side-by-side view of two documents are planned in §11 to §24, all screen-only and none changing the file.

**Adjacency:** list=not-applicable (finding pages is the thumbnail and outline panels, D02 T03 §4 and §5); document=applicable @ D07 T01 §1; settings=applicable @ D02 T04 §5; reporting=not-applicable (no reports over viewing state); notifications=not-applicable (viewing produces no lifecycle notices); permissions=applicable @ D02 T02 §9; audit=not-applicable (reading position is session state, D02 T03 §1); exchange=not-applicable (copy and share are D03 T01 §3); reverse=not-applicable (view changes are stateless; nothing is created)

**Adjacency rationale:** Print (the document surface of viewing) is owned in `D07 T01 §1`. Permissions here means the document-level refusal boundary (external links ask before opening, `/Launch` and `/JavaScript` actions never execute) owned and exercised in §9. Reading settings (`Default zoom`, `Default page layout`, `Ask before opening links`, `Pages in dark theme`, `Page cache size`) live in `D02 T04 §§4-6` and are consumed here.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `ui::SwapChainHost`, Direct3D 11 / Direct2D 1.3 device creation and presentation thread | D02 T01 §3, D02 T01 §4, D10 T01 §3, D10 T01 §5, D10 T01 §18 | [ ] |
| 2 | §2 | Lock-free viewport state channel, DPI scaling and page-outline loading placeholders | §1, D01 T01 §10, D10 T01 §4 | [ ] |
| 3 | §3 | Worker D3D11 texture upload and LRU budgeted tile texture cache | §2, D01 T02 §7 | [ ] |
| 4 | §4 | Viewport-to-scheduler tile driver and seam-free page compositing | §3, D01 T02 §8 | [ ] |
| 5 | §5 | Page geometry engine for Continuous, Single-Page and Two-Page Spread layouts | §2 | [ ] |
| 6 | §6 | Continuous scrolling, inertia, keyboard page nav, rotation and Go-to-Page (`Ctrl+G`) | §4, §5, D10 T01 §12, D10 T02 §10 | [ ] |
| 7 | §7 | Zoom engine with in-frame stretched tile preview and anchor preservation | §4, §5, D10 T01 §12, D10 T02 §10 | [ ] |
| 8 | §8 | Floating Page Toolbar (`Focus`) and reading auto-fade behavior | §6, §7, D02 T01 §9, D10 T01 §9, D10 T01 §13, D10 T01 §14, D10 T02 §2, D10 T02 §3, D10 T02 §4, D10 T02 §14, D10 T02 §16, D10 T03 §1 | [ ] |
| 9 | §9 | Link hit-testing, internal navigation, `"Open this link?"` prompt and launch refusal | §5, §6, D02 T01 §10, D10 T01 §12, D10 T02 §3, D10 T02 §17 | [ ] |
| 10 | §10 | Dark-theme page colour modes (`Original`, `Dimmed`, `Inverted`) and `F11` Full Screen | §4, §8, D10 T01 §4, D10 T01 §8 | [ ] |
| 11 | §11 | Reading tints: sepia, eye-care green and a custom page colour | §10, D02 T04 §5, D02 T03 §11, D10 T01 §8, D10 T02 §14, D10 T02 §16, D10 T02 §20, D10 T02 §22, D10 T03 §5, D10 T02 §27 | [ ] |
| 12 | §12 | `recto-pdf` page transitions and timings: `/Trans` and `/Dur` | D01 T01 §10, D00 T03 §3 | [ ] |
| 13 | §13 | Presentation mode: full-screen slideshow, navigation and blank screen | §6, §9, §10, §12, D02 T03 §11, D06 T04 §5, D10 T01 §4, D10 T01 §12, D10 T02 §17 | [ ] |
| 14 | §14 | Render page transitions and honour automatic page timing | §12, §13, D02 T04 §5, D01 T02 §12, D06 T04 §5, D10 T01 §14 | [ ] |
| 15 | §15 | Presentation pen and laser pointer | §13, D06 T04 §5, D10 T01 §6, D10 T01 §9, D10 T01 §12, D10 T02 §16 | [ ] |
| 16 | §16 | Presenter view on a second display with page preview and timer | §3, §13, D02 T04 §5, D06 T04 §5, D10 T01 §3, D10 T01 §4, D10 T02 §2, D10 T02 §20, D10 T03 §1, D10 T03 §4 | [ ] |
| 17 | §17 | Slide navigator: jump to any page during a presentation | §3, §13, §16, D06 T04 §5, D10 T02 §11, D10 T03 §1 | [ ] |
| 18 | §18 | Hands-free auto-scroll with speed control | §2, §6, D02 T03 §11, D02 T04 §5, D06 T04 §6, D10 T01 §3, D10 T02 §20, D10 T03 §4 | [ ] |
| 19 | §19 | Reverse page order view and right-to-left spreads | §5, §6, D02 T03 §1, D10 T02 §14 | [ ] |
| 20 | §20 | Rulers and cursor coordinates in chosen units | §5, §7, D02 T01 §4, D02 T03 §11, D06 T04 §6, D10 T01 §6, D10 T01 §10, D10 T03 §1 | [ ] |
| 21 | §21 | Grid, guides and the view snap provider | §20, D02 T03 §1, D06 T04 §6, D10 T01 §6, D10 T01 §12 | [ ] |
| 22 | §22 | Line weights view toggle: true widths or uniform thin lines | §3, D02 T03 §7, D01 T02 §6, D01 T02 §11, D06 T04 §6, D10 T02 §14 | [ ] |
| 23 | §23 | Split view: two panes of one document with independent scroll and zoom | §6, §7, D02 T04 §6, D02 T01 §13, D06 T04 §6, D10 T01 §7, D10 T01 §12, D10 T01 §13, D10 T02 §26 | [ ] |
| 24 | §24 | Side-by-side view of two documents with synchronized scrolling | §6, D02 T01 §3, D02 T01 §13, D06 T04 §6, D10 T01 §7, D10 T01 §12, D10 T01 §13, D10 T02 §26 | [ ] |

## 1. `ui::SwapChainHost`, Direct3D 11 / Direct2D 1.3 device creation and presentation thread

Implement foundation plan section 4.8's presentation foundation in `src/app/document_view.h/.cpp` and `src/services/presentation.h/.cpp`: each open `DocumentView` owns a dedicated presentation thread that is the sole caller of its `ID3D11DeviceContext` and `ID2D1DeviceContext`, presents onto a DXGI flip-model swap chain (`DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`, `DXGI_FORMAT_B8G8R8A8_UNORM`) created for composition and hosted by `ui::SwapChainHost` (`D10 T01 §18`), handles GPU device-lost recovery (`DXGI_ERROR_DEVICE_REMOVED` / `DXGI_ERROR_DEVICE_RESET`), and registers into `Runtime`'s ordered shutdown sequence (`D01 T02 §4`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4 (`RectoCanvasBrush`: `#E6E7EA` light / `#141312` dark) and 7 (page canvas on the `OverlayCornerRadius` token, 8 epx; pages themselves stay square) with `design/screenshots/D-Focus-Light.png` and `D-Focus-Dark.png`.
**Job:** The user gets a tear-free, hardware-accelerated document canvas that never blocks the UI thread and recovers automatically if the graphics driver resets. Consumer: §2's viewport channel and §4's tile compositor draw onto this presentation surface.
**Treatment:** Dedicated presentation thread per `DocumentView` using the shared hardware `ID3D11Device` that `ui::GraphicsDevice` creates (`D3D11_CREATE_DEVICE_BGRA_SUPPORT`, WARP fallback only when no hardware adapter exists, `D10 T01 §5`) through a plain COM interface, its own multithreaded `ID2D1Factory1` device context, and a composition swap chain hosted by `ui::SwapChainHost`. Cheaper substitute that fails the checkpoint: drawing PDF pages into `ui::BitmapImage` elements on the UI thread.
**Chrome:** mount `DocumentView` inside `ShellPage`'s `CenterCanvasSlot` (`D02 T01 §4`) and clear the swap chain to `RectoCanvasBrush` resolved from the generated tokens for the active theme (`ui::ThemeManager`, `D10 T01 §8`). Do not hard-code hex clear colours.

**Build order:**

1. Author `src/services/presentation.h/.cpp` (`PresentationRenderer`): take the shared hardware `ID3D11Device` from `ui::GraphicsDevice` (`D3D11_CREATE_DEVICE_BGRA_SUPPORT`, `D10 T01 §5`) as a plain COM pointer, then derive `IDXGIDevice`, `IDXGIAdapter`, `IDXGIFactory2`, the flip-model `IDXGISwapChain1` created for composition, and `ID2D1DeviceContext` (`D2D1_FACTORY_TYPE_MULTI_THREADED`), and run the dedicated presentation loop thread waking on a condition variable / frame event and joining cleanly on stop. Done when: starting and stopping `PresentationRenderer` 50 times in a unit test leaks zero COM interfaces (`ID3D11Debug` live object report clean) and joins before `Runtime` shutdown completes.
2. Create `src/app/document_view.h/.cpp` hosting a `ui::SwapChainHost` (inside a bordered element on the `OverlayCornerRadius` token, 8 epx), hand the swap chain to the host on the UI thread (`D10 T01 §18`), clear to the theme-resolved `RectoCanvasBrush` colour, and implement `handle_device_lost()` so `DXGI_ERROR_DEVICE_REMOVED` or `DXGI_ERROR_DEVICE_RESET` recreates the D3D11/D2D devices and swap chain automatically. Done when: `DocumentView` renders the `RectoCanvasBrush` background in Light and Dark themes and recovers cleanly from a simulated device-lost signal in `recto-tests`.

- [ ] Author `src/services/presentation.h/.cpp` taking the shared `ID3D11Device` from `ui::GraphicsDevice` (hardware with automatic WARP fallback, `D10 T01 §5`) through a plain COM interface, and `ID2D1Factory1` / `ID2D1Device` / `ID2D1DeviceContext` initialization. Done when: device creation succeeds on both discrete/integrated GPUs and headless CI adapters.
- [ ] Create the `IDXGISwapChain1` through `CreateSwapChainForComposition` for `ui::SwapChainHost` (`D10 T01 §18`) with `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`, `DXGI_FORMAT_B8G8R8A8_UNORM`, `DXGI_ALPHA_MODE_IGNORE`, and 2 buffers for low-latency presentation. Done when: `Present(1, 0)` runs on the presentation thread with zero DXGI debug layer warnings.
- [ ] Author `src/app/document_view.h/.cpp` containing the `ui::SwapChainHost` and attach the swap chain when the element is added to `ShellPage`'s `CenterCanvasSlot`. Done when: `DocumentView` mounts inside active document tabs. Cheaper substitute that fails the checkpoint: rendering on the UI thread through a UI-thread `ID2D1DeviceContext` or `ui::BitmapImage` updates.
- [ ] Sample `RectoCanvasBrush` (`#E6E7EA` in Light, `#141312` in Dark) from the generated tokens header (`src/ui/generated/recto_tokens.g.h`) for the active theme and pass the resolved `D2D1_COLOR_F` to `PresentationRenderer` so the canvas background matches the active theme with zero hex literals in `src/app` or `src/ui` outside the generated tokens header. Done when: switching Light and Dark themes updates the swap-chain clear colour immediately.
- [ ] Implement `handle_device_lost()` in `PresentationRenderer` detecting `DXGI_ERROR_DEVICE_REMOVED` and `DXGI_ERROR_DEVICE_RESET` during `Present` or `ResizeBuffers`, releasing device-dependent resources, taking the rebuilt device from `ui::GraphicsDevice` and re-issuing the swap chain to `ui::SwapChainHost` (`D10 T01 §5`, `§18`), recreating the D2D device context, and notifying the tile cache (§3) to repopulate. Done when: injecting a synthetic device-lost flag in tests recovers presentation within one frame without crashing.
- [ ] Register `PresentationRenderer` lifetime with `DocumentTabSession` (`D02 T01 §3`) and `Runtime` (`D01 T02 §4`) so closing a tab stops and joins the presentation thread before its `DocumentActor` tears down. Done when: closing a tab mid-frame never accesses a destroyed swap chain or actor.
- [ ] Add `View.PresentationLifecycleAndDeviceLost` test in `tests/recto-tests/` exercising presentation thread startup/join, theme clear-colour updates, and synthetic device-lost recovery. Done when: `recto-tests.exe --gtest_filter=View.PresentationLifecycleAndDeviceLost` passes.
- [ ] Commit: `viewer: add composition swap chain host, D3D11/D2D1 presentation thread and device-lost recovery`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.PresentationLifecycleAndDeviceLost` passes on the display-session host with zero D3D11 debug layer leaks and verified device-lost recovery.

## 2. Lock-free viewport state channel, DPI scaling and page-outline loading placeholders

Build the non-blocking UI-to-presentation viewport channel (`ViewportState`) and the SPEC section 9.2 immediate loading state: the UI thread posts atomic viewport snapshots (`scroll_x`, `scroll_y`, `zoom`, `panel_width`, `panel_height`, `ui::Window::dpi_scale()` DPI scale, layout mode, page colour mode) without ever waiting on the presentation thread; the presentation thread resizes the swap chain and `IDXGISwapChain2::SetMatrixTransform` smoothly on DPI/size changes and draws true-size page outlines with the SPEC section 7 soft page shadow (`1 epx` key, `24 epx` ambient) from `DocumentDescription` page dimensions before the first tile arrives.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7 (page shadow: `1 epx` key, `24 epx` ambient, square page corners) and 9.2 ("Loading: show page outlines at the right size immediately and fill them as tiles arrive; never a spinner over the whole window") with `design/screenshots/D-Focus-Light.png` and `D-Focus-Dark.png`.
**Job:** The moment a PDF opens, the user sees properly proportioned pages with soft shadows on the canvas that resize and scroll at the monitor's refresh rate with zero full-window spinners or UI freezes. Consumer: `TileTextureCache` (§3) and `PageLayoutGeometry` (§5).
**Treatment:** Latest-only lock-free/mutex-coalesced `ViewportState` hand-off from UI thread to presentation thread, swap-chain `ResizeBuffers` + `SetMatrixTransform` inverse scale for crisp Per-Monitor V2 DPI rendering, and Direct2D page-placeholder rendering with cached shadow geometry. Cheaper substitute that fails the checkpoint: showing a `ui::ProgressRing` spinner while waiting for the first page to rasterize, or blocking the UI thread inside the resize handler while `ResizeBuffers` runs.
**Chrome:** consume `RectoCanvasBrush`, `RectoBorderBrush`, and `RectoRaisedBrush` for page outlines and shadows.

**Build order:**

1. Define `ViewportState` and `ViewportChannel` in `src/services/presentation.h/.cpp` where `post_viewport_state(ViewportState)` overwrites the pending snapshot and wakes the presentation thread, and wire the `ui::SwapChainHost` size event and the `ui::Window` DPI-scale event (`D10 T01 §18`, `§4`) in `DocumentView` to post updated dimensions and DPI scales. Done when: a synthetic storm of 200 rapid resize and DPI-scale updates completes in under `2 ms` on the UI thread while the presentation thread coalesces and resizes cleanly.
2. Feed `DocumentDescription::pages` (page widths, heights, and intrinsic rotations from `D01 T01 §4`) into `PresentationRenderer` as soon as `Runtime::open` succeeds, and draw each visible page's paper rect (`#FFFFFF` in Original, `#D9D5CE` in Dimmed, `#1B1A19` in Inverted), `1 epx` key border, and `24 epx` ambient drop shadow before any tiles are cached. Done when: opening a 100-page corpus file renders true-size shadowed page placeholders on the very first frame after `DocumentDescription` arrives.

- [ ] Define `ViewportState` (`scroll_x`, `scroll_y`, `zoom`, `width_epx`, `height_epx`, `dpi_scale_x`, `dpi_scale_y`, `layout_mode`, `page_color_mode`, `sequence_number`) and coalescing `ViewportChannel` in `src/services/presentation.h/.cpp`. Done when: posting 1,000 states from the UI thread never blocks and the consumer reads the latest `sequence_number`.
- [ ] Wire the `ui::SwapChainHost` size event and the `ui::Window` DPI-scale event in `src/app/document_view.cpp` to `ViewportChannel::post`, applying `IDXGISwapChain2::SetMatrixTransform` (`1.0f / dpi_scale_x`, `1.0f / dpi_scale_y`) and `ResizeBuffers` on the presentation thread. Done when: moving the window between a `100%` DPI monitor and a `200%` DPI monitor keeps the swap chain at native physical pixel resolution without flicker.
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

Wire interactive scrolling, view rotation, and page navigation in `DocumentView` (`src/app/document_view.h/.cpp`) over `PageLayoutGeometry` (§5): mouse wheel and precision touchpad scrolling with smooth inertia (`ui::ScrollViewer`, `D10 T02 §10`), vertical and horizontal scrollbars, keyboard navigation (`Page Down`, `Page Up`, `Space`, `Shift+Space`, `Home` first page, `End` last page, arrow keys), view rotation (`Ctrl+R` right `90°`, `Ctrl+Shift+R` left `90°`) keeping the current page anchored, and `Ctrl+G` Go-to-Page activation.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (previous/next page, `"3 / 9"` editable page box), 9.2 (continuous scroll, 16 epx gaps), and 10 (`Ctrl+R`, `Ctrl+Shift+R`, `Ctrl+G`, `Home`, `End`, `Page Up`, `Page Down`) with `design/screenshots/D-Focus-Light.png`.
**Job:** The user can scroll fluidly at 60 fps with mouse, touchpad, scrollbar, or keyboard, rotate sideways pages upright in one shortcut, and jump straight to any page number with `Ctrl+G`. Consumer: `PageToolbar` (§8) and `Pages` / `Outline` panels (`D02 T03 §4`, `§5`).
**Treatment:** `ui::ScrollViewer` manipulation (smooth wheel and DirectManipulation pan, `D10 T02 §10`) driving `ViewportChannel` (§2) at 60+ fps, rotation anchor preservation, and validated `1..page_count` `go_to_page` navigation. Cheaper substitute that fails the checkpoint: resetting scroll position to page 1 when rotating the view or changing layout mode.
**Chrome:** consume `PageLayoutGeometry` (§5) and `ViewportChannel` (§2); expose `current_page_changed` events to the toolbar and side panels.

**Build order:**

1. Wire pointer wheel, precision touchpad two-finger pan with inertia, and scrollbar thumb manipulation in `DocumentView` to update `ViewportState` scroll offsets clamped to `PageLayoutGeometry` bounds, and fire `CurrentPageChanged` whenever `PageLayoutGeometry::current_page_index` changes. Done when: continuous scrolling across a 100-page document sustains 60 fps (`<= 16.6 ms` frame time on the presentation thread) and updates `CurrentPageChanged` accurately.
2. Implement keyboard page navigation through `ui::Input` accelerators (`D10 T01 §12`), covering (`Home` -> page 1, `End` -> last page, `Page Down` / `Space` -> next viewport screen or next page in `SinglePage` mode, `Page Up` / `Shift+Space` -> previous screen/page, `Up`/`Down`/`Left`/`Right` arrow nudges), view rotation (`rotate_right()` `Ctrl+R`, `rotate_left()` `Ctrl+Shift+R`) preserving the current page and relative scroll offset, and `go_to_page(one_based_page)` (`Ctrl+G`) with out-of-range refusal. Done when: every SPEC section 10 navigation and rotation key moves the viewport to the expected page and coordinate.

- [ ] Wire mouse wheel, touchpad pan/inertia, and scrollbars through `ui::ScrollViewer` in `src/app/document_view.cpp` to post clamped scroll offsets to `ViewportChannel` (§2). Done when: scrolling never overshoots document bounds and sustains 60 fps on the reference machine.
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

Implement foundation plan section 4.7's two-phase zoom engine in `DocumentView` and `PresentationRenderer`: discrete zoom buckets (`25%` to `500%`), `Fit Width` (`Ctrl+0`), `Fit Page`, `Actual Size 100%` (`Ctrl+1`), the SPEC section 8.2 preset dropdown (`Fit width`, `Fit page`, `50%`, `75%`, `100%`, `125%`, `150%`, `200%`, `300%`, `400%`), `Ctrl+Plus` / `Ctrl+Minus`, `Ctrl+Wheel`, and touchpad pinch-to-zoom (DirectManipulation, `D10 T02 §10`). During any zoom change, `PresentationRenderer` scales the nearest already-cached zoom bucket's textures in the exact same frame (`0 ms` blanking) while `TileScheduler` renders sharp target-bucket tiles within `150 ms`, keeping the document point under the cursor (or viewport centre) stationary.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (zoom controls and preset list), 9.2 ("while zooming, stretch the nearest rendered tiles so the page never goes blank"), 10 (`Ctrl+Plus`, `Ctrl+Minus`, `Ctrl+0`, `Ctrl+1`), and 12 (zoom motion: animated scale of existing tiles, then sharp tiles replace them, no flash).
**Job:** The user can zoom in and out smoothly with `Ctrl+Wheel`, pinch, keyboard shortcuts, or toolbar presets without ever seeing a white flash or losing the paragraph they were looking at. Consumer: `PageToolbar` (§8) and `Classic` menu bar (`D02 T01 §9`).
**Treatment:** Two-level tile compositing in `PresentationRenderer` (fallback nearest cached bucket scaled with `D2D1_INTERPOLATION_MODE_LINEAR` underneath sharp target-bucket tiles as they arrive) plus exact focal-point anchor math. Cheaper substitute that fails the checkpoint: clearing the page to white while new zoom tiles rasterize, or shifting the scroll position to the top-left of the page on zoom.
**Chrome:** consume `TileTextureCache` (§3) and `PageLayoutGeometry` (§5).

**Build order:**

1. Implement the zoom model in `src/services/page_layout.h/.cpp` and `src/app/document_view.cpp`: `ZoomMode` (`FreePercent`, `FitWidth`, `FitPage`), discrete step presets (`25%`, `33%`, `50%`, `67%`, `75%`, `100%`, `125%`, `150%`, `200%`, `300%`, `400%`, `500%`), dynamic `FitWidth` and `FitPage` recalculation on window/panel resize, and focal-point anchor preservation (`page_point_under_anchor` stays at the same viewport pixel offset before and after zoom). Done when: zooming from `100%` to `200%` around cursor point `(x, y)` keeps the underlying PDF page coordinate within `<= 1.0 epx` of `(x, y)`.
2. Implement stretched fallback compositing in `PresentationRenderer::render_frame()`: when a target-bucket tile `(page, target_bucket, tx, ty)` is not yet in `TileTextureCache`, query `TileTextureCache::find_nearest_bucket_tiles(page, target_bucket, rect)` and draw the cached lower/higher bucket texture scaled over the tile region until the sharp target-bucket texture arrives. Done when: zooming on a corpus page produces zero blank/placeholder frames in the presentation trace and sharp tiles replace the stretched preview within `150 ms` on the reference machine (`D00 T01 §5`).

- [ ] Implement `ZoomMode` (`FitWidth` `Ctrl+0`, `FitPage`, `CustomFactor` including `ActualSize` `100%` `Ctrl+1`) and the SPEC 8.2 preset steps (`50%`, `75%`, `100%`, `125%`, `150%`, `200%`, `300%`, `400%`). Done when: `zoom_in()` (`Ctrl+Plus`) and `zoom_out()` (`Ctrl+Minus`) step cleanly through the preset ladder.
- [ ] Recompute effective zoom automatically when `ZoomMode::FitWidth` or `ZoomMode::FitPage` is active and the viewport or side panel resizes. Done when: opening/closing a docked side panel in `FitWidth` mode adjusts page scale smoothly to fill the remaining width.
- [ ] Wire `Ctrl`+wheel (`ui::Input`, `D10 T01 §12`) and DirectManipulation pinch (`ui::ScrollViewer`, `D10 T02 §10`) to focal-point zoom anchored at the pointer position (and anchor keyboard/toolbar zoom at the viewport centre). Done when: `Ctrl+Wheel` zoom over a word keeps that word directly under the cursor.
- [ ] Implement `TileTextureCache::find_fallback_textures(page_index, target_bucket, page_rect)` returning cached textures from the closest previously rendered zoom bucket for that page. Done when: pinning/keeping the previous zoom bucket's visible tiles until sharp replacement tiles arrive prevents premature LRU eviction during a zoom gesture.
- [ ] Composite fallback bucket textures scaled with `D2D1_INTERPOLATION_MODE_LINEAR` in the exact same frame as the zoom change, overdrawing each sub-region with `1:1` sharp target-bucket tiles as worker completions arrive. Done when: frame-by-frame capture during a `100% -> 200%` zoom step shows 0% blank paper frames (`no flash` per SPEC 12). Cheaper substitute that fails the checkpoint: showing blank white page rectangles while waiting for new tiles to render.
- [ ] Meet the foundation plan section 8 zoom latency target: stretched preview in the same frame (`<= 16.6 ms`) and sharp tiles replacing the visible viewport within `150 ms` on the reference machine. Done when: telemetry timestamps in `PresentationRenderer` record `< 150 ms` time-to-sharp on the 100-page text fixture.
- [ ] Add `View.ZoomStretchedPreviewAndAnchor` test in `tests/recto-tests/` asserting zero blank frames during zoom transitions, `< 1.0 epx` focal-point drift, `FitWidth`/`FitPage` resize reactivity, and `< 150 ms` sharp-tile convergence. Done when: `recto-tests.exe --gtest_filter=View.ZoomStretchedPreviewAndAnchor` passes.
- [ ] Commit: `viewer: implement zoom engine with in-frame stretched preview and anchor preservation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.ZoomStretchedPreviewAndAnchor` passes on the display-session host, with zero blank frames during zoom and sharp tiles arriving within `150 ms`.

## 8. Floating Page Toolbar (`Focus`) and reading auto-fade behavior

Build the SPEC section 8.2 `PageToolbar` control (`src/app/controls/page_toolbar.h/.cpp`) hosted in `BottomOverlaySlot` (`Focus` default, centred `22 epx` above the bottom of the canvas) and reparentable into `TopBarSlot` (`Classic` and `Reading room`): `RectoRaisedBrush`, the `OverlayCornerRadius` token (8 epx), `Padding=6 epx`, `ui::Painter` elevation Z=32, `36 x 36 epx` minimum button hit targets, exact SPEC 8.2 control order and `More` menu items, `RectoAccentTintBrush` toggled states, and the 3-second pointer-idle fade to `40%` opacity while reading (respecting the OS `"Show animations in Windows"` accessibility setting).

> **Deferred:** `Find` button activation -> XREF: D03 T01 §6
> **Deferred:** `Annotate / Tools` contextual mode bars (`Markup`, `Form Designer`, `Redact & Sanitize`, `Table/Image Extractor`, `Sign Document`, `Compare PDFs`, `Crop & Resize`, `Watermark & Header/Footer`, `Optimize PDF`) -> XREF: D03 T03 §3
> **Deferred:** `Convert` accent button navigation -> XREF: D04 T01 §3
> **Deferred:** `Print` button action -> XREF: D07 T01 §1

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 (`16 epx` toolbar icons), 7 (`OverlayCornerRadius` 8 epx for the bar, `ControlCornerRadius` 4 epx for its buttons including the accent Convert button, `ui::Painter` elevation Z=32), 8.2 (Floating page toolbar order, `More` menu items, 3 s fade to 40% opacity), and 11 (`36 x 36 epx` hit targets, `ui::Element` automation name and shortcut tooltips on every icon-only button, reduced-motion compliance) with `design/screenshots/D-Focus-Light.png`, `D-Focus-Dark.png` and `D-Classic.png`.
**Job:** The user has every primary reading action (`Side panel`, `Page X / Y`, `Zoom`, `Fit width`, `Find`, `Rotate`, `Annotate`, `Print`, `Convert`, and `More`) in one quiet bar that fades out of the way while reading and works identically when docked in `Classic` or `Reading room`. Consumer: `ShellPage` slots (`D02 T01 §4`), `CommandRegistry` (`D02 T03 §11`), and `LayoutPresetEngine` (`D02 T04 §3`).
**Treatment:** Single `PageToolbar` control instance shared across all three layout presets with `36 x 36 epx` hit targets, inline editable `"3 / 9"` page-number box (`Ctrl+G`), zoom preset `ui::Menu`, `More` `ui::Menu`, and idle timer gating opacity between `1.0` and `0.4`. Cheaper substitute that fails the checkpoint: building separate toolbar controls for `Focus` and `Classic`, hiding the toolbar completely (`Opacity = 0`), or omitting `ui::Element` automation name and shortcut tooltips.
**Chrome:** consume `RectoRaisedBrush`, `RectoBorderBrush`, `RectoAccentBrush`, `RectoAccentTintBrush`, and `BottomOverlaySlot` / `TopBarSlot` (`D02 T01 §4`).

**Build order:**

1. Author `src/app/controls/page_toolbar.h/.cpp` in the exact SPEC 8.2 left-to-right group order separated by `1 epx` vertical `RectoBorderBrush` dividers: Group 1 (`Side panel toggle` `Ctrl+B`, toggled state in `RectoAccentTintBrush`); Group 2 (`Previous page`, inline editable `"3 / 9"` page box where `Ctrl+G` focuses and selects the page number, `Enter` calls `DocumentView::go_to_page`, and `Esc` restores the current page label, `Next page`); Group 3 (`Zoom out` `Ctrl+Minus`, `Zoom %` button opening the preset menu with `Fit width`, `Fit page`, `50%`, `75%`, `100%`, `125%`, `150%`, `200%`, `300%`, `400%`, `Zoom in` `Ctrl+Plus`, `Fit width` `Ctrl+0` toggled state in `RectoAccentTintBrush`); Group 4 (`Find` `Ctrl+F`, `Rotate` `Ctrl+R`, `Annotate / Tools` toggle `Ctrl+Shift+A`, `Print` `Ctrl+P`); Group 5 (`Convert` primary accent button with `RectoAccentBrush` background and `RectoOnAccentBrush` text); and Group 6 (`More` `...` button opening a `ui::Menu` with `Save a copy (Ctrl+Shift+S)`, `Page layout` (`Continuous`, `Single page`, `Two-page view`), `Page colour` (`Original`, `Dimmed`, `Inverted`), `Form designer`, `Redact & sanitize (Ctrl+Shift+X)`, `Sign document (Ctrl+Shift+G)`, `Compare PDFs (Ctrl+Shift+D)`, `Extract tables/images`, `Crop & page setup`, `Watermark & header/footer`, `Optimize PDF`, and `Properties`). Done when: every button and `More` menu item exists with `36 x 36 epx` minimum hit target, `ui::Element` automation name, and shortcut tooltip.
2. Implement the 3-second reading auto-fade timer on `PageToolbar` when hosted in `BottomOverlaySlot`: after `3000 ms` without pointer movement over `DocumentView` and with no menu or text box focused inside the toolbar, animate `Opacity` to `0.4` over `167 ms` with `ui::Motion` (`D10 T01 §14`); restore `Opacity = 1.0` immediately on pointer movement, pointer hover, or keyboard focus inside the toolbar; and keep `Opacity = 1.0` permanently when docked in `TopBarSlot` or when Windows client-area animations are off (`SPI_GETCLIENTAREAANIMATION` through `ui::Motion`). Done when: driven tests verify the 3-second fade to `0.4`, hover/focus restoration to `1.0`, never hiding below `0.4`, and `1.0` opacity when animations are disabled.

- [ ] Build `src/app/controls/page_toolbar.h/.cpp` with `RectoRaisedBrush`, the `OverlayCornerRadius` token (8 epx), `Padding=6 epx`, buttons on the `ControlCornerRadius` token (4 epx) with no radius literal, and `36 x 36 epx` minimum hit targets per SPEC 7, 8.2, and 11. Done when: visual metrics match `D-Focus-Light.png` and `D-Focus-Dark.png`.
- [ ] Wire the `"3 / 9"` click-to-type page box (`Ctrl+G`): clicking the label or pressing `Ctrl+G` switches to a compact `ui::TextBox` with the current page number selected; pressing `Enter` navigates via `DocumentView::go_to_page` (§6) and returns focus to the canvas; pressing `Esc` cancels edit mode. Done when: typing `7` and pressing `Enter` jumps to page 7.
- [ ] Wire the zoom buttons and `Zoom %` menu (`Fit width`, `Fit page`, `50%` to `400%`) to `DocumentView`'s zoom engine (§7), updating the displayed percentage (`"100%"`, `"125%"`) and `Fit width` toggle highlight (`RectoAccentTintBrush`) live. Done when: zooming by wheel or menu updates the toolbar percentage and toggle state immediately.
- [ ] Populate the `More` (`...`) `ui::Menu` with every SPEC 8.2 command (`Save a copy`, `Page layout`, `Two-page view`, `Page colour`, `Form designer`, `Redact & sanitize`, `Sign document`, `Compare PDFs`, `Extract tables/images`, `Crop & page setup`, `Watermark & header/footer`, `Optimize PDF`, `Properties`). Done when: all 13 `More` items are present and bound to their live handlers or named deferred owners. Cheaper substitute that fails the checkpoint: omitting items from the SPEC 8.2 `More` list.
- [ ] Set the `ui::Element` automation name and a tooltip (including keyboard shortcut where applicable) on every icon-only button in `PageToolbar`. Done when: automated UI tree inspection finds zero unnamed buttons in `PageToolbar`.
- [ ] Implement the 3-second pointer-idle fade to `40%` opacity (`0.4`, never `0.0`) when floating in `BottomOverlaySlot`, restoring `100%` opacity on pointer hover or keyboard focus, and suppressing the fade while any toolbar menu or the page-number input is active. Done when: idle timer transitions `Opacity` to `0.4` at `3000 ms` and restores `1.0` on hover or focus.
- [ ] Honor Windows `"Show animations in Windows"` (`SPI_GETCLIENTAREAANIMATION` through `ui::Motion`): when false, disable the 3-second toolbar fade so `PageToolbar` remains at `100%` opacity at all times per SPEC 11. Done when: toggling the animation flag in tests disables the fade completely.
- [ ] Support seamless reparenting of `PageToolbar` between `BottomOverlaySlot` (`Focus` floating bar with `ui::Painter` elevation Z=32) and `TopBarSlot` (`Classic` and `Reading room` docked strip without shadow). Done when: moving `PageToolbar` to `TopBarSlot` matches `D-Classic.png` and `D-ReadingRoom.png`.
- [ ] Add `View.PageToolbarAndAutoFade` test in `tests/recto-tests/` verifying control order, `36 x 36 epx` hit targets, automation names, `"3 / 9"` inline page entry, `More` menu completeness, 3-second fade to `0.4`, and reduced-motion override. Done when: `recto-tests.exe --gtest_filter=View.PageToolbarAndAutoFade` passes.
- [ ] Commit: `viewer: build SPEC 8.2 Floating Page Toolbar with inline page box, More menu and 3s auto-fade`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.PageToolbarAndAutoFade` passes on the display-session host, and rendered toolbar screenshots match `D-Focus-Light.png`, `D-Focus-Dark.png`, and `D-Classic.png`.

## 9. Link hit-testing, internal navigation, `"Open this link?"` prompt and launch refusal

Implement the viewer's link interaction and untrusted-action security boundary (`design/SPEC.md` sections 2, 9.7, 9.9 and foundation plan section 9): extract link annotations per page from `DocumentActor` (`D01 T01 §4`), hit-test pointer hover and keyboard `Tab` focus through `PageLayoutGeometry` (§5), navigate internal `GoTo` page/coordinate links directly (recording back-history in §6), prompt before opening external `http://` / `https://` / `mailto:` / `file:` URIs via the SPEC 9.9 `"Open this link?"` `ui::Dialog` (`D10 T02 §17`) displaying the full unshortened URL in a monospace box (honoring the `"Ask before opening links"` setting for web links while always prompting for `file:` links), and unconditionally refuse PDF `/Launch`, `/JavaScript`, `/ImportData`, and `/SubmitForm` actions.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 2 ("Safe by default. Links and Office macros never run without the user's say-so"), 4 (`RectoAccentTextBrush` link hover/focus highlight), 9.7 (`Ask before opening links` setting), and 9.9 (`Link in a document` dialog: `"Open this link?"`, full URL in a monospace box, `Cancel`, `Open in browser`) with `design/screenshots/D-Dialogs.png`.
**Job:** The user can click internal table-of-contents and footnote links to jump around the document, inspect the true destination URL of any external link before it opens in their browser, and never be compromised by a malicious PDF `/Launch` or `/JavaScript` action. Consumer: `DocumentView`, `SettingsStore` (`D02 T04 §2`), and the security audit (`D06 T02 §1`).
**Treatment:** Page-space link quads transformed via `PageLayoutGeometry`, `OpenLinkDialog` (`src/app/dialogs/open_link_dialog.h/.cpp`) built on `ui::Dialog` (`D02 T01 §10`, `D10 T02 §17`) with monospace `Consolas`/`Cascadia Mono` URL display and Punycode/IDN visibility, and strict scheme allowlisting (`https`, `http`, `mailto`). Cheaper substitute that fails the checkpoint: passing link strings straight to `ShellExecuteW` or `Launcher::LaunchUriAsync` without confirmation or scheme validation.
**Chrome:** consume `ui::Dialog` (`D02 T01 §10`), `RectoAccentTintBrush` / `RectoAccentBrush` link hover rings, and `PageLayoutGeometry` (§5).

**Build order:**

1. Wire per-page `PdfLinkAnnotation` records from `DocumentActor` (`D01 T01 §4`) into `DocumentView`: transform link bounding rects via `PageLayoutGeometry`, change the pointer cursor to the system hand cursor on hover (`ui::Input`, `D10 T01 §12`), draw a subtle `RectoAccentTintBrush` hover/focus highlight, show a `ui::ToolTip` (`D10 T02 §3`) with `"Go to page N"` (internal) or the target domain/URI (external), and allow keyboard `Tab` and `Enter` activation across links on visible pages. Done when: hovering or tabbing onto internal and external links in the corpus link fixture highlights them and clicking an internal `GoTo` link scrolls to the target page and pushes the origin page onto §6's back-navigation stack (`Alt+Left`).
2. Build `OpenLinkDialog` (`src/app/dialogs/open_link_dialog.h/.cpp`) per SPEC 9.9 (`"Open this link?"`, full un-truncated URL in a read-only selectable monospace box, `Cancel` and `Open in browser` buttons): when an external `http://`, `https://`, or `mailto:` link is activated, show `OpenLinkDialog` if `"Ask before opening links"` is enabled (default true) and launch via `Windows::System::Launcher::LaunchUriAsync` only if confirmed; for `file://` or unknown schemes, always refuse or prompt with an explicit warning; and for `/Launch`, `/JavaScript`, `/JS`, `/Rendition`, or `/SubmitForm` actions, never execute anything and log a diagnostic refusal entry. Done when: clicking an external URL opens `OpenLinkDialog` matching `D-Dialogs.png`, clicking `Cancel` opens nothing, and clicking a crafted `/Launch` (`cmd.exe`) or `/JavaScript` link in the hostile test corpus executes zero processes or scripts.

- [ ] Cache per-page `PdfLinkAnnotation` lists in `DocumentView` and hit-test pointer coordinates through `PageLayoutGeometry::viewport_to_page_point` (§5). Done when: pointer hover over a link switches cursor to `Hand` even when the page is zoomed or rotated `90°`/`180°`/`270°`.
- [ ] Draw a subtle `RectoAccentTintBrush` fill and `1 epx` `RectoAccentBrush` outline on hovered or keyboard-focused links on the page canvas. Done when: keyboard `Tab` traversal visibly highlights each link on the current page.
- [ ] Execute internal `GoTo` links by scrolling `DocumentView` to the destination page and `(top, zoom)` target while pushing the prior viewport position onto §6's `Alt+Left` history stack. Done when: clicking a table-of-contents link jumps to the chapter and `Alt+Left` returns to the table of contents.
- [ ] Author `src/app/dialogs/open_link_dialog.h/.cpp` using `ui::Dialog` (`D02 T01 §10`) with title `"Open this link?"`, the complete URL in a monospace (`Cascadia Mono` / `Consolas`) box on `RectoSurfaceBrush` with `1 epx` `RectoBorderStrongBrush`, and `Cancel` / `Open in browser` buttons. Done when: the rendered dialog matches the `"Open this link?"` board in `D-Dialogs.png`.
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
**Treatment:** GPU Direct2D colour-matrix / blend compositing over cached tile textures (plus image-bbox mask preservation from `DisplayList` in `Inverted` mode so raster images remain true-colour) and `ui::Window` full-screen mode (`D10 T01 §4`) with top/bottom `8 epx` edge-hover reveal for chrome. Cheaper substitute that fails the checkpoint: re-rasterizing all PDF tiles on the CPU when switching page colour modes, or inverting embedded photos into colour negatives in `Inverted` mode.
**Chrome:** consume `ViewportChannel` (§2) and `ShellPage` slots (`D02 T01 §4`).

**Build order:**

1. Implement the three `PageColorMode` compositing paths in `PresentationRenderer` (`src/services/presentation.cpp`): `Original` (standard `1:1` premultiplied BGRA bitmap draw); `Dimmed` (Direct2D colour matrix / multiply blend toning white paper `#FFFFFF` to `#D9D5CE` `R=0.851, G=0.835, B=0.808` while preserving contrast); and `Inverted` (Direct2D luminance-preserving colour inversion mapping paper `#FFFFFF` to `#1B1A19` and `#EDEAE4` text, masking out raster image bounding rects reported by each page's `DisplayList` `D01 T02 §1` so photographs and figures render in their original un-inverted colours per SPEC section 4). Done when: switching between `Original`, `Dimmed`, and `Inverted` updates the screen in one frame (`0` new tile requests sent to `TileScheduler`), `Dimmed` paper samples at `#D9D5CE`, and `Inverted` preserves photo colours on the image corpus page.
2. Implement `F11` Full Screen mode in `ShellPage` and `DocumentView`: pressing `F11` switches `ui::Window` to full-screen mode (`D10 T01 §4`) and hides `TitleBarSlot`, `TopBarSlot`, and `BottomOverlaySlot`; moving the pointer within `8 epx` of the top screen edge slides down `TitleBarSlot`, and within `8 epx` of the bottom edge reveals `BottomOverlaySlot`; pressing `F11` again or `Esc` (when no higher overlay is open) restores the windowed placement and the active preset's chrome. Done when: `F11` enters full screen, top/bottom edge hover reveals the title bar and toolbar, and `F11` / `Esc` restores the window state cleanly.

- [ ] Implement `PageColorMode::Dimmed` in `PresentationRenderer` using a Direct2D colour matrix effect that maps `#FFFFFF` paper to `#D9D5CE` without re-rasterizing any `TileTextureCache` entries. Done when: switching `Original <-> Dimmed` generates zero `TileScheduler` requests and samples paper at `#D9D5CE`.
- [ ] Implement `PageColorMode::Inverted` in `PresentationRenderer` mapping `#FFFFFF` paper to `#1B1A19` and dark text to `#EDEAE4`, while clipping/restoring un-inverted regions for raster `DrawImage` bounding boxes recorded on the page's `DisplayList`. Done when: text and vector art invert to light-on-charcoal while embedded JPEG/PNG images retain their natural colours per SPEC section 4. Cheaper substitute that fails the checkpoint: blindly inverting the entire bitmap so photos turn into colour negatives, or re-rendering tiles on the CPU.
- [ ] Apply the selected `PageColorMode` automatically when in Dark theme (defaulting to `Dimmed` per SPEC 4) and expose live switching from `PageToolbar` (`More -> Page colour`), the `View` menu (`D02 T01 §9`), and `Settings` (`D02 T04 §4`). Done when: all three surfaces stay synchronized with the active `PageColorMode`.
- [ ] Implement `F11` Full Screen toggle on `ShellPage` using `ui::Window` full-screen mode (`D10 T01 §4`) and collapsing docked/title chrome so `DocumentView` fills `100%` of the monitor. Done when: pressing `F11` expands `DocumentView` to the full monitor bounds.
- [ ] Add `8 epx` top-edge and bottom-edge pointer proximity detection in Full Screen mode to reveal `TitleBarSlot` and `PageToolbar` on hover and hide them again when the pointer moves back onto the page canvas. Done when: moving the cursor to `Y <= 8` in Full Screen reveals the title bar and tabs.
- [ ] Exit Full Screen mode on `F11` or `Esc` (when no modal dialog, Command Box, Find card, or contextual mode bar is open), restoring the prior windowed placement and slot visibility. Done when: pressing `Esc` in Full Screen with no open popups exits Full Screen cleanly.
- [ ] Add `View.PageColorModesAndFullScreen` test in `tests/recto-tests/` asserting zero tile re-renders across `Original` / `Dimmed` / `Inverted` switches, `#D9D5CE` dimmed paper sampling, un-inverted image preservation in `Inverted` mode, and `F11` / edge-hover / `Esc` Full Screen transitions. Done when: `recto-tests.exe --gtest_filter=View.PageColorModesAndFullScreen` passes.
- [ ] Keep `Dimmed` and `Inverted` screen-only: printing (`D07 T01 §2`), export and saved files ignore `PageColorMode`, and highlight, underline and ink annotations keep at least 4.5:1 contrast against the text beneath them. Done when: `View.PageColorModesAndFullScreen` measures the contrast on a highlighted fixture in both modes and a print-to-PDF of the same page is byte-identical to `Original`.
- [ ] Paint page placeholders in the active mode's paper colour from the first frame, so switching pages in a dark mode never flashes white. Done when: `View.PageColorModesAndFullScreen` captures 100 rapid page flips in `Inverted` with zero white frames.
- [ ] Commit: `viewer: add GPU Dimmed/Inverted dark page colour modes and F11 Full Screen`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.PageColorModesAndFullScreen` passes on the display-session host, `Dimmed` page matches `D-Focus-Dark.png`, `Inverted` leaves corpus images un-inverted, and `F11` Full Screen edge-reveal and `Esc` restore work end to end.

## 11. Reading tints: sepia, eye-care green and a custom page colour

UPDF's and PDFelement's page background colours (sepia, eye-care green, custom) match a need Acrobat Pro only meets through its accessibility colour override. §10 gives dark-theme `Dimmed` and `Inverted`; this section adds three light-theme tints that work in any app theme. A tint is composited on the GPU over cached tiles, so switching re-renders nothing, and it is screen only: printing, export and saved files ignore it. Colours come from tokens; the custom colour is the user's own value, stored in settings, never a hex literal in a control.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4 (colour tokens, pages in dark theme) and 9.7 (Reading group) with `design/screenshots/D-Focus-Light.png` for the unchanged chrome.
**Job:** A user who reads for hours picks a warmer or greener paper and keeps it across documents. Consumer: `PresentationRenderer` (§1), `PageToolbar` (§8), `ShellMenuBar` (`D02 T01 §9`), `SettingsPage` (`D02 T04 §5`).
**Treatment:** GPU colour-matrix scale over cached tiles. Cheaper substitute that fails the checkpoint: re-rendering tiles with a new background, or hard-coding hex values in C++.
**Chrome:** generated `recto_tokens.g.h` tokens, `ui::Menu` radio items (`D10 T02 §14`), `ui::Flyout` (`D10 T02 §16`) and `ui::ColorSwatchPicker` (`D10 T02 §22`).

**Build order:**

1. Add the tokens `RectoPageSepiaBrush` (paper `#F4ECD8`) and `RectoPageEyeCareBrush` (paper `#CCE8CF`) to the Light, Dark and HighContrast sets of `design/tokens/colors.json` (the source of the generated `src/ui/generated/recto_tokens.g.h`, `D10 T01 §8`), and a row for each in `design/SPEC.md` section 4; in HighContrast both resolve to the system window colour so a tint never overrides the user's contrast theme. Done when: `ui::ThemeManager` resolves both brushes in every theme and the HighContrast value equals the `COLOR_WINDOW` system colour.
2. Add `PageTint` (`None`, `Sepia`, `EyeCare`, `Custom`; default `None`) and `CustomPageColor` (ARGB, default `#FFF4ECD8` as stored user data) to `RectoSettings`, and implement the tint in `PresentationRenderer` as a Direct2D colour-matrix scale that maps white paper to the tint colour after `Original` or `Dimmed`, and is ignored while `Inverted` is active. Done when: switching `None` to `Sepia` to `Custom` sends 0 requests to `TileScheduler` and samples paper at the token colour, then at the stored custom colour.

- [ ] Show `Page tint` as a radio group under `View -> Page Colour` (`D02 T01 §9`), in the `PageToolbar` `More -> Page colour` menu (`D02 T02 §8`) and as a row in the Reading group of Settings (`D02 T04 §5`), all bound to one `PageTint` value. Done when: the three surfaces stay in sync.
- [ ] Offer a `ui::Flyout` for `Custom` holding a `ui::ColorSwatchPicker` with the custom colour entry of `D10 T02 §27` for the user's own value, that shows the contrast ratio of black text on the chosen colour and a warning `ui::Chip` (`D10 T02 §20`) below 4.5:1 (icon plus text, never colour alone). Done when: choosing `#FF202020` shows the warning and choosing `#FFF4ECD8` does not.
- [ ] Register `Page tint: None`, `Sepia`, `Eye care` and `Custom colour...` in `CommandRegistry` (`D02 T03 §11`). Done when: all four are found by `Ctrl+K`.
- [ ] Keep printing, `recto-cli render` output and saved PDFs free of the tint. Done when: a print-to-PDF of a tinted page is byte-identical to the untinted one.
- [ ] Add `View.ReadingTints` in `tests/recto-tests/` covering token resolution, zero re-renders, custom colour persistence, `Inverted` precedence and HighContrast. Done when: `recto-tests.exe --gtest_filter=View.ReadingTints` passes. Cheaper substitute: re-rasterizing tiles on the CPU or painting a coloured rectangle behind transparent pages.
- [ ] Commit: `viewer: add sepia, eye-care and custom reading tints`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.ReadingTints` passes on the display-session host and a sepia screenshot shows paper at `#F4ECD8`.

## 12. `recto-pdf` page transitions and timings: `/Trans` and `/Dur`

PDFelement and UPDF play slide-style PDFs with their page transitions; Acrobat Pro does too. The engine must read the data safely before any surface draws it. This section parses a page's `/Trans` dictionary (ISO 32000-1 section 12.4.4 and its transition dictionary: `/S` style, `/D` duration, `/Dm`, `/M`, `/Di`, `/SS`, `/B`) and the page `/Dur` auto-advance time into a plain value type, and reads the catalog `/PageMode /FullScreen` hint. It never runs an action; unknown or hostile values degrade to a cut.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/pdf/page_transition.h/.cpp` with `PageTransition { style, duration_s, dimension, motion, direction_deg, scale, base_opaque }` `Document::page_transition(page_index) -> PdfResult<std::optional<PageTransition>>` and `Document::page_duration(page_index) -> PdfResult<std::optional<double>>` for the page's `/Dur` display time, covering the styles `Split`, `Blinds`, `Box`, `Wipe`, `Dissolve`, `Glitter`, `R`, `Fly`, `Push`, `Cover`, `Uncover` and `Fade`; map any other `/S` name to `R` and record one diagnostics line. Done when: a fixture page per style returns the matching enum and an unknown name returns `R`.
2. Clamp `/D` to 0 to 60 s (default 1 s), `/Di` to the allowed set (`0`, `90`, `180`, `270`, `315`, `None`), `/SS` to 0.1 to 10, and `/Dur` to 0 to 3600 s; reject non-numeric values as defaults; read `/PageMode` into the `DocumentDescription` extension as `opens_full_screen`. Done when: a hostile fixture with `/D -5`, `/D 1e9`, `/Di 77` and a nested `/Trans` array parses without a throw and with clamped values.

- [ ] Keep `PageTransition` and `page_duration` plain copyable values with no pointer into the `Document`, so the actor query of `D01 T02 §12` can return them across threads; that query and its thread test belong to `D01 T02 §12`, which builds after this section. Done when: `PdfTransition.ValuesAreCopyable` copies both values after the `Document` is destroyed and reads them under ASan.
- [ ] Add `tests/corpus/transitions.pdf` (12 pages, one per style, with `/Dur` on pages 2 and 3) to `scripts/build-corpus.py` and a `tests/corpus/PROVENANCE.md` row. Done when: `python scripts/build-corpus.py --check` regenerates it byte for byte.
- [ ] Add the hostile fixture and a libFuzzer seed so `fuzz_open` reaches the parser. Done when: `fuzz_open` runs 60 s with the seed and no crash.
- [ ] Add `PdfTransition.Parse`, `PdfTransition.ClampsHostileValues` and `PdfTransition.NoActionRuns` under ASan. Done when: `recto-tests.exe --gtest_filter=PdfTransition.*` passes. Cheaper substitute: reading transitions with a regex over raw page bytes.
- [ ] Commit: `pdf: parse page transitions, timings and the full-screen page mode`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfTransition.*` passes under ASan and the 60 s `fuzz_open` run is clean.

## 13. Presentation mode: full-screen slideshow, navigation and blank screen

UPDF's Presentation mode and Foxit's slide show view show one page at a time, edge to edge on a black backdrop with no chrome; Acrobat Pro has Full Screen mode. It differs from `F11` (§10), which keeps the reading layout. Recto keeps its rules: no script, no action runs, link clicks still ask first (§9), and entering is the user's choice. A document whose catalog asks for full screen gets one question, never an automatic switch.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 4 tokens, section 10 keys and the F11 behaviour of §10 here. Design board: `design/SPEC.md` section 9.10 with `design/screenshots/D-Presentation.png` (drawn by `D06 T04 §5`).
**Job:** A user presents a PDF edge to edge and drives it from the keyboard. Consumer: `DocumentView` (§6, §10), `CommandRegistry` (`D02 T03 §11`), `PresentationRenderer` (§1).
**Treatment:** One controller over the existing `SinglePage` layout and `ui::Window` full-screen mode (`D10 T01 §4`). Cheaper substitute that fails the checkpoint: a second renderer or window class for slideshows, or auto-entering full screen from the document.
**Chrome:** `RectoPresentationBackdropBrush`, `ui::Dialog` (`D02 T01 §10`).

**Build order:**

1. Add `PresentationController` (`src/app/presentation_controller.h/.cpp`): `F5` starts from the current page, `Ctrl+K` offers `Start presentation from first page`; it saves the tab's layout, zoom and slots, switches `ui::Window` to full-screen mode, selects `SinglePage` with fit-page, paints the new token `RectoPresentationBackdropBrush` (black in every theme, added to `design/tokens/colors.json` and SPEC section 4), and restores everything on exit. Done when: entering then leaving restores the previous layout, zoom, page and slot visibility exactly.
2. Handle keys while presenting: `Right`, `Down`, `PageDown`, `Space`, `Enter` and wheel-down next; `Left`, `Up`, `PageUp`, `Backspace` and wheel-up previous; `Home` and `End`; digits then `Enter` jump to a page; `B` and `W` toggle a black or white screen; `Esc` exits; a left click on a non-link area advances; hide the cursor after 3 s idle and show it on movement. Done when: a scripted key run lands on the expected page for every binding and the cursor hides after 3 s.

- [ ] If `opens_full_screen` (§12) is true show one `ui::Dialog` `This document asks to open full screen` with `Allow` and `Not now`, remembered for the session only. Done when: opening the fixture never switches to full screen without a click.
- [ ] Keep links working through §9's `Open this link?` prompt and never execute launch or script actions. Done when: `Security.ActionRefusal` still passes in presentation mode.
- [ ] Exit on `Esc` only when no dialog or card is open, on window deactivation never (the user may alt-tab), and when the document closes. Done when: alt-tab away and back keeps presenting.
- [ ] Register `Start presentation`, `Start presentation from first page` and `Exit presentation` in `CommandRegistry` (`D02 T03 §11`) and the `View` menu (`D02 T01 §9`). Done when: all are reachable from `Ctrl+K`.
- [ ] Add `View.PresentationModeNavigation` covering entry and exit state, every key binding, blank and white screens, cursor hiding and the full-screen question. Done when: `recto-tests.exe --gtest_filter=View.PresentationModeNavigation` passes. Cheaper substitute: reusing `F11` and hiding the toolbar only.
- [ ] Commit: `viewer: add presentation mode with slideshow navigation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.PresentationModeNavigation` passes on the display-session host.

## 14. Render page transitions and honour automatic page timing

PDFelement and UPDF play the transition a PDF page declares; Acrobat Pro does too. §12 reads `/Trans` and `/Dur`; this section draws the transition between two cached page textures on the presentation thread and optionally advances by `/Dur`. Windows animation settings win: when animations are off every transition is a cut. Page timing is off by default because a PDF that moves on by itself can skip content the reader has not read.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 12 (motion, reduced-motion rule) and §13 here. Design board: `design/SPEC.md` section 9.10 with `design/screenshots/D-Presentation.png` (drawn by `D06 T04 §5`).
**Job:** A user who opens a slide-style PDF sees its transitions, unless Windows animations are off, and can let its timings drive the show. Consumer: `PresentationController` (§13), `PageTransition` (§12).
**Treatment:** Direct2D effects on the presentation thread between two textures already in `TileTextureCache`. Cheaper substitute that fails the checkpoint: ignoring `/Trans`, or animating when Windows reduced motion is on.
**Chrome:** `RectoPresentationBackdropBrush`; no new chrome.

**Build order:**

1. In `PresentationRenderer` add `draw_transition(from_texture, to_texture, PageTransition, t)` for `Fade` and `Dissolve` (cross-fade), `Wipe` (moving clip edge by `/Di`), `Push` (both slide), `Cover` and `Fly` (new page slides over), `Uncover` (old page slides off), and `Split`, `Blinds`, `Box` (clip-geometry reveals: `Split` by `/Dm` and `/M`, `Blinds` by `/Dm`, `Box` by `/M`); `Glitter` falls back to a cross-fade, `R` is a cut, `/SS` scales `Fly` and `/B` is ignored; `docs/presentation-transitions.md` lists each `/S` and parameter as supported or as an intentional fallback. Done when: for every style the first frame equals the old page and the last frame equals the new page; for every style except `R` (a cut, whose frames before t=1 all equal the old page) intermediate frames at t=0.25, 0.5 and 0.75 (checked against golden images) differ from both pages and show the named effect: the `Wipe` and `Push` edge position follows `/Di` for 0, 90, 180 and 270 degrees, `Split` and `Blinds` follow `/Dm` horizontal or vertical, `/M` inward or outward reverses `Split`, `Box` and `Fly` (ISO 32000-1 Table 162; `Blinds` has no `/M` and a test proves `/M` leaves it unchanged), and `Cover` against `Uncover` moves a different page
2. Start the transition on page change in presentation mode (§13) using `/D`, never longer than 2 s, and skip it when Windows client-area animations are off (`ui::Motion`, `D10 T01 §14`) or the user turns off `Play page transitions` in Settings; add `PresentationAutoAdvance` (default off) that moves to the next page after `/Dur` seconds when set. Done when: with animations disabled a page change takes one frame, and with auto-advance on the fixture advances after its `/Dur`.

- [ ] Pause auto-advance on any key, click, wheel or when the presenter view (§16) is paused, and stop at the last page. Done when: a key press pauses the timer until the next explicit advance.
- [ ] Cancel an in-flight transition when the user navigates again so the target page appears at once. Done when: ten rapid presses end on the right page with no stuck frame.
- [ ] Add `PresentationAutoAdvance` and `Play page transitions` rows to the Reading group of Settings (`D02 T04 §5`). Done when: both rows change behaviour at the next page turn.
- [ ] Add `View.PageTransitionsRender` covering all 12 styles' endpoints and the intermediate-frame goldens above, a timing check that a 1 s `/D` takes 1 s on a fake clock (an old-page-until-last-frame implementation fails the intermediate frames), the cut when animations are off, cancel on navigation and auto-advance timing with a fake clock. Done when: `recto-tests.exe --gtest_filter=View.PageTransitionsRender` passes. Cheaper substitute: playing transitions on the UI thread with `ui::Motion` animations.
- [ ] Commit: `viewer: render page transitions and optional automatic page timing`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.PageTransitionsRender` passes on the display-session host.

## 15. Presentation pen and laser pointer

UPDF and Foxit let a presenter draw on the page and point with a laser; Acrobat Pro has no presentation pen. In Recto the pen writes ink to an overlay that is thrown away when the slideshow ends, so a presentation can never change the PDF (D18 keeps edits on a copy). Single-key commands apply only while presenting.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 4 tokens and section 11 (a text hint accompanies every colour cue). Design board: `design/SPEC.md` section 9.10 with `design/screenshots/D-Presentation.png` (drawn by `D06 T04 §5`).
**Job:** A presenter marks the page they are talking about and points without touching the file. Consumer: `PresentationController` (§13).
**Treatment:** A `ui::Element` ink layer that draws strokes with `ui::Painter` on an overlay with session-only storage. Cheaper substitute that fails the checkpoint: ink that is written to the document, or colour-only tool state.
**Chrome:** generated `recto_tokens.g.h` tokens, Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Add `PresentationInkLayer` (`src/app/presentation_ink_layer.h/.cpp`) as a `ui::Element` over the page that collects `WM_POINTER` mouse, touch and pen input (`D10 T01 §12`) and draws strokes with `ui::Painter` (`D10 T01 §9`): `P` pen, `H` highlighter, `L` laser, `E` eraser, `Ctrl+Z` undo last stroke, `Delete` clear page; colours from `RectoDangerBrush`, `RectoSuccessBrush`, `RectoWarningBrush` and `RectoTextBrush`, widths 2, 4 and 8 epx; strokes live per page for the session and are discarded on exit. Done when: drawing on page 2, moving to page 3 and back shows the stroke again, and exiting leaves the PDF byte-identical.
2. Draw the laser as a 12 epx `RectoDangerBrush` dot with a soft glow following the mouse, pen hover or touch contact, with no effect on the document, and hide it when the pointer leaves. Done when: the dot tracks the pointer within one frame and vanishes on leave.

- [ ] Show a one-time hint `Ink is not saved` the first time a pen tool is used in a session and list the keys on `?` in a dismissible card. Done when: the hint shows once per session.
- [ ] Map pen pressure (`WM_POINTER` pen pressure) to width for pen pointers and ignore pressure for the mouse. Done when: a pressure fixture varies the stroke width.
- [ ] Disable the tools when the user opens a link prompt or dialog, and restore the tool afterwards. Done when: a dialog opening mid-stroke ends the stroke cleanly.
- [ ] Register `Pen`, `Highlighter`, `Laser pointer`, `Eraser` and `Clear ink on this page` in `CommandRegistry`, enabled only while presenting. Done when: they appear in `Ctrl+K` only during a presentation.
- [ ] Add `View.PresentationInkAndLaser` covering strokes per page, undo, clear, discard on exit, laser tracking and the unchanged-file check. Done when: `recto-tests.exe --gtest_filter=View.PresentationInkAndLaser` passes. Cheaper substitute: writing the ink into the PDF as annotations.
- [ ] Commit: `viewer: add presentation pen, highlighter and laser pointer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.PresentationInkAndLaser` passes on the display-session host.

## 16. Presenter view on a second display with page preview and timer

PowerPoint-style presenter views are in UPDF and Foxit's presentation features: the audience sees the page on one display while the presenter sees the current page, the next page, the clock and an elapsed or countdown timer on another. Recto opens a second `ui::Window` on another monitor; with one display it opens as an ordinary window the user can place. It reads from the same document actor, never opens the file twice, and survives a display being unplugged.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 7 spacing, section 4 tokens and section 11. Design board: `design/SPEC.md` section 9.10 with `design/screenshots/D-Presentation.png` (drawn by `D06 T04 §5`).
**Job:** A presenter sees what is next and how long they have, while the audience sees only the page. Consumer: `PresentationController` (§13), `TileTextureCache` (§3).
**Treatment:** A second `ui::Window` bound to the same viewport state. Cheaper substitute that fails the checkpoint: duplicating the document in a second actor, or requiring two displays.
**Chrome:** generated `recto_tokens.g.h` tokens, Fluent UI System Icons (`RectoIcons`), `ui::Dialog`.

**Build order:**

1. Add `PresenterWindow` (`src/app/presenter_window.h/.cpp`) shown by `Presenter view` in the presentation controls or the key `T`: pick the audience display as the one that does not hold the main window (`EnumDisplayMonitors`), place the audience window full screen there and the presenter window on the other, with a `Swap displays` button; with a single display open the presenter view as a normal movable window. Done when: on a two-display rig the audience page and the presenter window appear on different displays, and on one display a movable window opens.
2. Show in the presenter window the current page, a next-page preview (a tile from `TileTextureCache`), `Page N of M`, the wall clock, and a timer: count up from zero by default, or count down from `PresentationCountdownMinutes` (setting, 0 means count up) with start, pause and reset buttons; turn the timer `RectoWarningBrush` with text `Under 1 minute` in the last minute and `RectoDangerBrush` with text `Over time` after zero. Done when: the timer advances with a fake clock, pauses and resets, and shows the state text at 60 s and 0 s.

- [ ] React to monitor changes (`WM_DISPLAYCHANGE`) by moving both windows to a remaining display and showing a `ui::Toast` (`D10 T02 §20`), never closing the show. Done when: unplugging the audience display keeps the presentation running on the remaining one.
- [ ] Mirror navigation both ways: keys in either window move both, and the presenter window shows the same page as the audience at all times. Done when: pressing `Right` in the presenter window advances the audience page within one frame.
- [ ] Give every control a `ui::Element` automation name, tooltip and keyboard access, and announce `Next page, N of M` politely through a UIA notification (`D10 T03 §4`). Done when: the UIA tree lists every control with a name.
- [ ] Add the `PresentationCountdownMinutes` row to Settings (`D02 T04 §5`). Done when: changing it changes the next presentation's timer.
- [ ] Add `View.PresenterViewAndTimer` covering display placement with fake displays, mirror navigation, timer states and display removal. Done when: `recto-tests.exe --gtest_filter=View.PresenterViewAndTimer` passes. Cheaper substitute: a second `DocumentActor` for the presenter window.
- [ ] Commit: `viewer: add presenter view with next-page preview and timer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.PresenterViewAndTimer` passes on the display-session host, and a two-display manual run is recorded in the stamp.

## 17. Slide navigator: jump to any page during a presentation

PowerPoint-style `See all slides` is in UPDF's presentation tools. A presenter who is asked a question needs to jump to a page by its picture. The navigator is a grid of page thumbnails from the existing cache, opened with `G` in presentation mode and permanently shown as a strip in the presenter view (§16).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 8.3 `Pages` thumbnail styling and section 11. Design board: `design/SPEC.md` section 9.10 with `design/screenshots/D-Presentation.png` (drawn by `D06 T04 §5`).
**Job:** A presenter jumps to any page by its picture. Consumer: `PresentationController` (§13), `PresenterWindow` (§16).
**Treatment:** Virtualized `ui::ItemsView` grid layout (`D10 T02 §11`) over cached thumbnails. Cheaper substitute that fails the checkpoint: a text list of page numbers.
**Chrome:** generated `recto_tokens.g.h` tokens.

**Build order:**

1. Build `SlideNavigator` (`src/app/controls/slide_navigator.h/.cpp`) as a virtualized `ui::ItemsView` grid of thumbnails from `TileTextureCache` (`D02 T02 §3`) with page numbers, the current page highlighted with `RectoAccentTintBrush` and a text `Current` label, keyboard arrows and `Enter` to jump, `Esc` to close; show it as an overlay on the audience display only when invoked, and as an always-visible strip in `PresenterWindow`. Done when: a 500-page document opens the grid in under 300 ms and jumping lands on the chosen page.
2. Request thumbnails at low priority so the live page never waits. Done when: the live page's tile latency is unchanged while the grid fills.

- [ ] Add `Show slide navigator` to `CommandRegistry` (while presenting) and bind `G`. Done when: `Ctrl+K` and `G` both open it.
- [ ] Make every thumbnail an automation element named `Page N`. Done when: Narrator reads the page numbers.
- [ ] Add `View.SlideNavigator` covering virtualization, jump, highlight text and the priority rule. Done when: `recto-tests.exe --gtest_filter=View.SlideNavigator` passes. Cheaper substitute: rendering full pages for the grid.
- [ ] Commit: `viewer: add the presentation slide navigator`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.SlideNavigator` passes on the display-session host.

## 18. Hands-free auto-scroll with speed control

UPDF and Foxit scroll a document by themselves for reading aloud or teleprompting, and Acrobat Pro has Automatically Scroll (`Ctrl+Shift+H`). The user starts it, steers speed and direction from the keyboard, and any manual input stops it. It moves only the viewport, so it never changes the file and honours the same bounds as manual scrolling.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 7 (floating card), section 10 and section 11. Design board: `design/SPEC.md` section 9.11 with `design/screenshots/D-ViewTools.png` (drawn by `D06 T04 §6`).
**Job:** A user reads, rehearses or follows along without touching the mouse. Consumer: `DocumentView` (§6), `ViewportChannel` (§2).
**Treatment:** Clock-driven viewport updates. Cheaper substitute that fails the checkpoint: a timer that nudges the scrollbar a fixed amount per tick.
**Chrome:** `RectoRaisedBrush`, `ui::Painter` elevation Z=32 floating card, Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Add `AutoScroller` (`src/app/auto_scroller.h/.cpp`) driving `ViewportChannel` (§2) from the presentation clock: speed levels 1 to 10 map to 12 to 240 pt per second at 100 percent zoom (scaled by zoom), `Ctrl+Shift+H` starts and stops, digits `1` to `9` and `0` set speed, `Up` and `Down` adjust it, `Left` reverses and `Right` goes forward, `Space` pauses and resumes, `Esc` stops; frame-rate independent using elapsed time. Done when: a 10 s run at level 5 scrolls the expected distance within 1 percent at 60 and at 30 fps.
2. Stop on wheel, drag, touch or scrollbar input, on tab switch, on window deactivation and at the last page; show a small `AutoScrollChip` in `BottomOverlaySlot` with the level and a pause button, announce `Auto scroll on, speed N` politely through a UIA notification (`D10 T03 §4`), and keep the chip text and not colour as the state cue. Done when: a wheel tick stops the scroll and removes the chip.

- [ ] Add the `AutoScrollDefaultSpeed` row (1 to 10, default 3) to the Reading group of Settings (`D02 T04 §5`). Done when: the next start uses the stored speed.
- [ ] In `SinglePage` and `TwoPageSpread` layouts scroll within the page and then advance to the next page. Done when: single-page mode crosses pages without a jump.
- [ ] Register `Start auto scroll`, `Stop auto scroll`, `Faster` and `Slower` in `CommandRegistry` (`D02 T03 §11`). Done when: all four are in `Ctrl+K`.
- [ ] Add `View.AutoScroll` covering speed maths, direction, pause, stop conditions and the layout cases with a fake clock. Done when: `recto-tests.exe --gtest_filter=View.AutoScroll` passes. Cheaper substitute: a fixed per-frame pixel step, which changes speed with frame rate.
- [ ] Commit: `viewer: add hands-free auto-scroll with speed control`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.AutoScroll` passes on the display-session host.

## 19. Reverse page order view and right-to-left spreads

Foxit and PDFelement can show pages in reverse order, which helps when scans arrive last page first; PDFs can also declare `/ViewerPreferences /Direction /R2L` for right-to-left reading. §5 lays pages out in numeric order only. This section adds a per-tab view option that reverses the display order without touching page numbers or the file, and, separately, arranges two-page spreads by the document's reading direction (`/Direction /R2L`). The two are independent flags: a right-to-left book still runs page 1 first.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.2 (layouts) with `design/screenshots/D-Focus-Light.png`; the toggle is a `ui::Menu` check item.
**Job:** A user reads scans that came in backwards, or a right-to-left book, in the right order without editing the file. Consumer: `DocumentView` (§6), `ShellMenuBar` (`D02 T01 §9`).
**Treatment:** A mapping inside `PageLayoutGeometry`. Cheaper substitute that fails the checkpoint: reordering the pages in the document, which is an Organize operation.
**Chrome:** `ui::Menu` check items (`D10 T02 §14`), tokens.

**Build order:**

1. Add `reverse_order` to `PageLayoutGeometry` (`src/services/page_layout.h/.cpp`) mapping display slot to page index for `Continuous`, `SinglePage` and `TwoPageSpread`; add `spread_direction` (`L2R`, `R2L`); with `R2L` the spread pairs mirror (page 2 on the right of page 3's slot) and the cover sits on the left, while the page sequence is unchanged; with `reverse_order` the sequence runs last page to first, and both flags combine; page numbers, coordinates and `current_page_index` keep their meaning. Done when: round-trip transforms stay within 0.01 epx in all layouts, rotations and all four combinations of `reverse_order` and `spread_direction`.
2. Add the `Reverse page order` toggle (View menu, `More` menu, `Ctrl+K`), default off, plus a `Reading direction` choice (`Left to right`, `Right to left`) defaulting to the document's `/Direction`; both are stored per file as `reverse_order` and `spread_direction` in the reading-position record (`D02 T03 §1`), and keep `Page Down` meaning the next screen in the display direction while `Home`, `End` and `Ctrl+G` keep SPEC meanings by page number. Done when: reverse view of a 10-page file shows page 10 first, `Home` goes to page 1 and `Ctrl+G` 3 lands on page 3.

- [ ] Keep thumbnails and the outline in numeric order and highlight the current page in both. Done when: reverse view changes neither panel's order.
- [ ] Preserve the current page when toggling. Done when: toggling on page 4 keeps page 4 in the viewport centre.
- [ ] Add the fixture `tests/corpus/direction-r2l.pdf` to `scripts/build-corpus.py`. Done when: `python scripts/build-corpus.py --check` passes.
- [ ] Add `View.ReversePageOrder` covering mapping for all four flag combinations, spreads, `/Direction` defaulting without reversing the sequence, persistence and key meanings. Done when: `recto-tests.exe --gtest_filter=View.ReversePageOrder` passes. Cheaper substitute: reversing only the thumbnail list.
- [ ] Commit: `viewer: add reverse page order view and R2L spreads`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.ReversePageOrder:View.PageLayoutGeometryAndTransforms` passes on the display-session host.

## 20. Rulers and cursor coordinates in chosen units

Foxit and PDFelement show rulers and live cursor coordinates, and Acrobat Pro has both (View, Show/Hide). Recto draws rulers along the canvas edges and a coordinate chip using §5's page transforms, with a zero at the top-left of the page under the pointer. They are screen furniture: never saved, never printed, and `Ctrl+R` stays rotate (so rulers use `Ctrl+Alt+R`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 5 caption type, section 7 spacing and section 4 tokens. Design board: `design/SPEC.md` section 9.11 with `design/screenshots/D-ViewTools.png` (drawn by `D06 T04 §6`).
**Job:** A user measures and aligns by eye in real units. Consumer: `PageLayoutGeometry` (§5), `GuideStore` (§21).
**Treatment:** Overlay drawn from page-space transforms. Cheaper substitute that fails the checkpoint: screen-pixel rulers that ignore zoom.
**Chrome:** `RectoSurfaceBrush`, `RectoBorderBrush`, `RectoTextSecondaryBrush`.

**Build order:**

1. Add `RulerOverlay` (`src/app/controls/ruler_overlay.h/.cpp`) with a horizontal and a vertical ruler (20 epx thick) in `CenterCanvasSlot`, ticks adapting to zoom (major, minor, label), units `pt`, `in`, `mm`, `cm`, `pc` from the `RulerUnits` setting (default from `GetLocaleInfoEx(LOCALE_IMEASURE)`: millimetres for metric, inches otherwise), and pointer marker lines; origin is the CropBox top-left of the page under the pointer. Done when: at 100 percent zoom an A4 page reads 210 mm across and 297 mm down within 0.5 mm, and the origin follows the page under the pointer in continuous mode.
2. Add `CursorCoordinates` chip in `LeftOverlaySlot` showing `Page N  x, y unit` while the pointer is over a page, toggled separately (`Show cursor coordinates`), text selectable for copy. Done when: moving over page 2 shows `Page 2` and coordinates matching `viewport_point_to_page`.

- [ ] Add `Rulers` (`Ctrl+Alt+R`), `Cursor coordinates` and `Ruler units` to the View menu, `More` menu, Settings (`D02 T04 §5`) and `CommandRegistry`. Done when: all are reachable from `Ctrl+K`.
- [ ] Hide rulers and the chip in presentation mode (§13) and full screen (§10), and keep ruler text at system colours in HighContrast. Done when: none show in presentation mode.
- [ ] Handle rotation and `Reverse page order` (§19) so the ruler origin and direction follow the displayed page. Done when: a 90 degree rotation swaps the ruler roles and keeps values correct.
- [ ] Make ruler marks non-focusable and expose the coordinate chip as a UIA name that updates on a 250 ms debounce, not per pixel. Done when: Narrator is not flooded while moving the mouse.
- [ ] Add `View.RulersAndCoordinates` covering unit conversion, tick spacing at 10 zoom levels, origin and rotation. Done when: `recto-tests.exe --gtest_filter=View.RulersAndCoordinates` passes. Cheaper substitute: pixel-based units that ignore page size.
- [ ] Commit: `viewer: add rulers and cursor coordinates`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.RulersAndCoordinates` passes on the display-session host.

## 21. Grid, guides and the view snap provider

Foxit and PDFelement give a grid and drag-out guides, and Acrobat Pro has a grid and guides (View, Show/Hide, Rulers & Grids). This section draws the grid and guides on the canvas and offers one `ViewSnapProvider` that annotation, form and edit tools can ask for a snapped point. Guides and the grid are view state stored beside the reading position, never written into the PDF.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 4 tokens and section 7. Design board: `design/SPEC.md` section 9.11 with `design/screenshots/D-ViewTools.png` (drawn by `D06 T04 §6`).
**Job:** A user aligns marks and form fields to a grid and guides that stay out of the file. Consumer: `RulerOverlay` (§20), annotation, form and edit tools through `ViewSnapProvider`.
**Treatment:** Overlay drawing and a pure snap function. Cheaper substitute that fails the checkpoint: guides stored in the PDF.
**Chrome:** `RectoBorderBrush`, `RectoAccentBrush`.

**Build order:**

1. Implement `src/services/view_snap.h/.cpp`: `GridSpec` (spacing in the ruler unit, default 0.25 in or 5 mm, subdivisions), `GuideStore` (per file, per page or all pages: axis and position in points, persisted in the reading-position record of `RecentFilesStore`, `D02 T03 §1`), and `ViewSnapProvider::snap(page_index, PagePoint, tolerance_epx) -> SnapResult` returning the nearest guide or grid line within the tolerance. Done when: a point 3 epx from a guide snaps to it and a point 20 epx away does not, at three zoom levels.
2. Draw the grid (`Ctrl+U`) and guides on the canvas; drag from a ruler (§20) to create a guide, drag it back to a ruler to remove it, double-click to type its position, `Clear all guides` command; colours `RectoBorderBrush` and `RectoAccentBrush` with a dash pattern so colour is not the only cue. Done when: creating, moving, typing and removing a guide each update the store and the drawing.

- [ ] Add `Snap to grid` and `Snap to guides` toggles (default on while shown) in the View menu, Settings and `CommandRegistry`. Done when: turning a toggle off makes `snap` return no result for that source.
- [ ] Make guides and grid screen-only: absent from print, export and saved files. Done when: a saved PDF has the same bytes as without guides.
- [ ] Publish the provider for consumers (request cross references to `D03 T03 §9`, `§18`, `D03 T04 §13`) without editing their files. Done when: the provider header is included by a test double standing in for a tool.
- [ ] Add `View.GridGuidesSnap` covering snapping maths, persistence, removal, rotation and the unchanged-file check. Done when: `recto-tests.exe --gtest_filter=View.GridGuidesSnap` passes. Cheaper substitute: storing guides as annotations in the PDF.
- [ ] Commit: `viewer: add grid, guides and the view snap provider`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.GridGuidesSnap` passes on the display-session host.

## 22. Line weights view toggle: true widths or uniform thin lines

Acrobat Pro and Foxit have a Line Weights view option; CAD and map PDFs are often easier to read when every stroke is drawn one device pixel wide. `Actual` keeps today's behaviour (specified width, with the rasterizer's minimum); `Uniform` draws every stroke as exactly one device pixel. This is a render option, so it must be part of the tile identity or stale tiles would show. The tile contract lives in frozen `D01 T02`: this section lists the request instead of editing it.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 8.2 `More` menu; a `ui::Menu` check item. Design board: `design/SPEC.md` section 9.11 with `design/screenshots/D-ViewTools.png` (drawn by `D06 T04 §6`).
**Job:** A user inspects dense line art with uniform thin lines and switches back. Consumer: `Rasterizer`, `TileTextureCache` (§3).
**Treatment:** A render option in the tile identity. Cheaper substitute that fails the checkpoint: a screen-space blur or filter over finished tiles.
**Chrome:** `ui::Menu` check items (`D10 T02 §14`).

**Build order:**

1. Add `RenderOptions::line_weight_mode` (`Actual`, `Uniform`) read by `Rasterizer` (`src/pdf/rasterizer.cpp`) for every stroke path, and carry it in the tile identity the same way `ocg_generation` is carried (`D02 T03 §7`), so toggling invalidates cached tiles. Done when: the same page rendered in both modes differs, and a `Uniform` stroke measures 1 pixel at 25 percent and 800 percent zoom.
2. Add the `Show line weights` toggle (default on) to the View menu, the `More` menu and `Ctrl+K`, per tab, resetting to default for new documents. Done when: toggling re-renders the visible pages within one tile cycle.

- [ ] Leave text, images and fills unchanged by the option. Done when: a text-only page is byte-identical in both modes.
- [ ] Ignore the mode for print, export and `recto-cli render`. Done when: print output equals the `Actual` render.
- [ ] Add `tests/corpus/line-weights.pdf` (hairline, 0.25 pt, 4 pt and dashed strokes) to `scripts/build-corpus.py`. Done when: `python scripts/build-corpus.py --check` passes.
- [ ] Add `View.LineWeightsToggle` and `Render.UniformStrokeWidth` covering the pixel widths and cache invalidation. Done when: `recto-tests.exe --gtest_filter=View.LineWeightsToggle:Render.UniformStrokeWidth` passes. Cheaper substitute: scaling the bitmap afterwards.
- [ ] Commit: `viewer: add the line weights view toggle`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.LineWeightsToggle:Render.UniformStrokeWidth` passes on the display-session host.

## 23. Split view: two panes of one document with independent scroll and zoom

Foxit, PDFelement and UPDF split the window so two places in one document stay visible (a contents page and its target, a figure and its text); Acrobat Pro has Split and Spreadsheet Split. Both panes read one `DocumentActor`, so there is one parse, one tile cache budget and no state drift. Today split view is only a request (`D02 T01 §3` area); it is now planned.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 7 (borders, no shadows on docked panes) and section 8 (named slots). Design board: `design/SPEC.md` section 9.11 with `design/screenshots/D-ViewTools.png` (drawn by `D06 T04 §6`).
**Job:** A user reads two places in one document together. Consumer: `ShellPage` `CenterCanvasSlot` (`D02 T01 §4`), `DocumentView` (§6).
**Treatment:** Two views over one actor and one cache. Cheaper substitute that fails the checkpoint: duplicating the document in a second tab.
**Chrome:** `RectoBorderBrush`, `RectoAccentBrush`, `ui::Splitter` (`D10 T02 §26`) between `ui::Grid` tracks (`D10 T01 §7`).

**Build order:**

1. Add `SplitDocumentView` (`src/app/split_document_view.h/.cpp`) hosting two `DocumentView` instances bound to the same `DocumentTabSession`, horizontal or vertical split with a draggable splitter (minimum pane 240 epx), each pane with its own `ViewportChannel`, zoom, layout and rotation, and the active pane marked with a `RectoAccentBrush` border plus the text `Active pane` for screen readers. Done when: scrolling pane 1 to page 40 leaves pane 2 on page 3, and both show the same annotation changes.
2. Add commands `Split view horizontally`, `Split view vertically`, `Remove split` (View menu, `More` menu, `Ctrl+K`), `Switch pane` (`F6`), and an optional `Link scrolling` toggle that moves both panes by the same offset. Done when: `F6` moves focus between panes and `Remove split` restores one pane at the active pane's position.

- [ ] Share one `TileTextureCache` budget between panes so memory stays within `TileCacheBudgetMb` (`D02 T04 §6`). Done when: two panes at different zooms stay under the budget in the stress run.
- [ ] Make text selection, find highlights and annotations render in both panes and act in the focused pane. Done when: a find match highlights in both panes.
- [ ] Save the split orientation and ratio in the session snapshot (§13). Done when: relaunch restores the split.
- [ ] Collapse to one pane in presentation mode (§13) and keep the Reading room inspector outside the panes. Done when: presentation mode shows one pane.
- [ ] Add `View.SplitViewSharedActor` covering one actor, independent viewports, budget, linked scrolling and session persistence. Done when: `recto-tests.exe --gtest_filter=View.SplitViewSharedActor` passes. Cheaper substitute: opening the file twice in two actors.
- [ ] Commit: `viewer: add split view of one document`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.SplitViewSharedActor` passes on the display-session host and the tile cache stays within budget.

## 24. Side-by-side view of two documents with synchronized scrolling

PDFelement, Foxit and UPDF show two PDFs side by side to compare versions; Recto already compares content (`D03 T01 §10`), and this is the plain reading view. Each side is its own tab and actor; the parallel view only arranges two tabs in `CenterCanvasSlot`. Tab tear-off to separate windows stays unplanned (one shell, D17).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 8.1 (tabs), section 7 and `D-Classic.png` pane borders. Design board: `design/SPEC.md` section 9.11 with `design/screenshots/D-ViewTools.png` (drawn by `D06 T04 §6`).
**Job:** A user reads two documents next to each other and scrolls them together when they choose. Consumer: `ShellPage` (`D02 T01 §4`), `DocumentTabSession` (`D02 T01 §3`).
**Treatment:** Two existing tabs arranged in one slot. Cheaper substitute that fails the checkpoint: a second shell window.
**Chrome:** `RectoBorderBrush`, `RectoAccentBrush`, `ui::Splitter` (`D10 T02 §26`) between `ui::Grid` tracks (`D10 T01 §7`).

**Build order:**

1. Add `ParallelView` (`src/app/parallel_view.h/.cpp`): `Compare side by side` on a tab's context menu and `Ctrl+K` picks a second open tab (or opens one with the picker) and shows both in `CenterCanvasSlot` with a splitter, each keeping its `DocumentTabSession`, viewport and zoom; `Close side by side` returns to single view without closing either tab. Done when: choosing a second tab shows both documents, and closing the view leaves both tabs open.
2. Add `Synchronized scrolling` (off by default): moving one side by a page-relative offset moves the other by the same fraction of its page, with `Align pages` to snap both to the current page numbers, and zoom linking as a separate toggle. Done when: with sync on, scrolling document A by 3 pages scrolls B by 3 pages, and pages of different sizes stay proportionally aligned.

- [ ] Route tab close, dirty prompts and `Ctrl+Tab` through §3 and §8 unchanged; closing one document ends the parallel view. Done when: closing a dirty side prompts and `Cancel` keeps both.
- [ ] Show the file name above each side and mark the active side with text and `RectoAccentBrush`. Done when: both names are visible and Narrator reads the active side.
- [ ] Save the pair in the session snapshot (§13) as two adjacent tabs flagged `parallel`. Done when: relaunch restores the side-by-side view.
- [ ] Keep memory within the shared tile budget when both sides are visible. Done when: the two-document stress run stays under `TileCacheBudgetMb`.
- [ ] Add `View.ParallelViewAndSyncScroll` covering pairing, sync maths, close behaviour and session restore. Done when: `recto-tests.exe --gtest_filter=View.ParallelViewAndSyncScroll` passes. Cheaper substitute: merging both documents into one actor.
- [ ] Commit: `viewer: add side-by-side view with synchronized scrolling`

**Test checkpoint:** `recto-tests.exe --gtest_filter=View.ParallelViewAndSyncScroll` passes on the display-session host.

## Verification

- Foundation plan section 8 targets owned here (first page `< 300 ms` on a 100-page text PDF, 60 fps scroll, in-frame stretched zoom preview with sharp tiles `< 150 ms`, one-document memory within the 512 MB texture cache budget) measured on the reference machines (`D00 T01 §5`) and quoted.
- Rendered surfaces match `D-Focus-Light.png`, `D-Focus-Dark.png`, `D-Classic.png`, `D-ReadingRoom.png`, and `D-Dialogs.png`; every SPEC section 10 view shortcut (`Ctrl+Plus`, `Ctrl+Minus`, `Ctrl+0`, `Ctrl+1`, `Ctrl+R`, `Ctrl+Shift+R`, `Ctrl+G`, `Home`, `End`, `Page Up`, `Page Down`, `F11`, `Esc`) is exercised and proven.
- Full `recto-tests` suite green; `python scripts/todo-graph.py validate` passes.
- The viewing features of §11 to §24 are proven by `View.ReadingTints`, `PdfTransition.*`, `View.PresentationModeNavigation`, `View.PageTransitionsRender`, `View.PresentationInkAndLaser`, `View.PresenterViewAndTimer`, `View.SlideNavigator`, `View.AutoScroll`, `View.ReversePageOrder`, `View.RulersAndCoordinates`, `View.GridGuidesSnap`, `View.LineWeightsToggle`, `View.SplitViewSharedActor` and `View.ParallelViewAndSyncScroll`.
