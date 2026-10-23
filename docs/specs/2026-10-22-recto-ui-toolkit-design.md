# recto-ui: Recto's own UI toolkit (design)

- **Date:** 2026-10-22
- **Status:** Approved by the owner 2026-10-22 (replaces WinUI 3, decision D4)
- **Owner decision:** "Drop WinUI 3 and plan for our own UI, still the same as the design." Toolkit base chosen by the owner: a Win32 window with DirectComposition, Direct2D and DirectWrite, Windows only.
- **Governs:** foundation plan D4, D5, D8 and sections 2, 3, 4.3, 4.6, 6, 7, 10 and 11 (`docs/plans/2026-10-13-recto-foundation.md`); `design/SPEC.md` principle 3 and every place it names a WinUI control; the TODO domain `todo/10-ui/`.
- **Amended 2026-10-23 (owner decisions D22 to D26):** the toolkit is Vellum, a shared Rizonesoft engine. See the amendment below; where this document says `recto-ui`, `src/ui` or `ui::`, read Vellum, `libs/vellum` and `vellum::` once `D10 T04 §1` lands.

## Amendment 2026-10-23: Vellum, a shared engine

- **Shared engine, not a shared look (D22).** Rizonesoft Office (Write, Grid, Stage, Flow, Plan, Mail) and Isotone (Stilus, Gesso, Albumen) will consume this toolkit instead of each building a C++ GUI toolkit. Shared: windowing, DPI and title-bar chrome; mouse, keyboard, touch and pen input; IME, text layout and rendering; UI Automation, high contrast and focus; composition, invalidation, layout, scrolling, animation and hit-testing; base controls themed by roles. Per consumer: theme and tokens, icon font, strings, shell and work canvas. Recto ships first and nothing is built speculatively for a consumer that has not adopted it.
- **Name and boundary (D23).** `recto-ui` becomes Vellum: `libs/vellum`, namespace `vellum::`, projects `vellum` and `vellum-tests`, headers under `include/vellum/`, test macro `VELLUM_TEST`, headless harness `vellum::test::render_to_bitmap`. No file under `libs/vellum` includes `src/**` or names Recto; `libs/vellum/scripts/check-boundary.py` proves both directions from the `/sourceDependencies` reports and adds a brand scan (`D10 T04 §1` and `§2`). Section 3's non-goal "no theming surface" still holds for end users; consumers theme Vellum through roles.
- **Theming split (D23).** Vellum owns the neutral role schema (`libs/vellum/tokens/schema.json`) and the generator (`libs/vellum/scripts/build-tokens.py`); Recto owns `design/tokens/colors.json`, the mapping `src/app/theme/recto_theme.json` and the generated `src/app/generated/recto_theme.g.h`. Vellum's own tests run against a neutral `libs/vellum/tests/themes/test-theme.json`; golden images of Recto's look are Recto's tests (`D10 T04 §3`).
- **Consumer identity (D23).** Window class, AUMID, registry and settings paths, icon set registration and string tables are supplied by the consumer at startup (`D10 T04 §4`); `RectoIcons` registers through `vellum::IconSet`.
- **Pen and ink in M2 (D25).** `vellum::Input` carries pressure, tilt, twist, coalesced history (`GetPointerPenInfoHistory`), eraser, barrel button and pen cursors (`D10 T01 §22`), and a low-latency wet-ink presenter (`D10 T01 §23`).
- **Extraction and licence (D24, D26).** Vellum stays in this repository until a second consumer adopts it, then leaves with history via `git filter-repo`, versioned by SemVer and consumed as a pinned vcpkg port (submodule fallback); it is `GPL-3.0-or-later` under `CLA.md` with SPDX headers until the licence is revisited at extraction (`D10 T04 §5` to `§7`). This document moves to `libs/vellum/docs/design.md` in `D10 T04 §1`.

## 1. Decision

Recto's interface is drawn by **`recto-ui`**, an in-tree C++23 static library (`src/ui`) that hosts a Win32 top-level window, composes it with DirectComposition, draws with Direct2D 1.3 and DirectWrite, and exposes itself to assistive technology through UI Automation providers it implements. WinUI 3, the Windows App SDK, XAML, MIDL3 and MRT Core leave the product. C++/WinRT stays only as the projection for the WinRT APIs Recto calls outside the UI (`Windows.Media.Ocr`, `Windows.UI.Notifications`, the Share contract, `PrintManager` interop); no `Microsoft.UI.*` type is referenced anywhere.

The look does not change. `design/SPEC.md` remains the source of truth: the same tokens, type ramp, 4 and 8 epx radii, pills, elevations, motion, icons, layouts and screens. `recto-ui` reproduces the Fluent (WinUI 3) look and behaviour the SPEC was written against, control by control, and the design boards are proven with golden images instead of eyeballed.

## 2. Why

- **One native stack from engine to pixels.** The page surface, tiles, thumbnails and the chrome share one D3D11 device and one Direct2D factory. No XAML island, no `SwapChainPanel` boundary, no WinRT activation in the UI path.
- **Ownership.** Same reasons as D3 for the engine: no framework release train to pin (the daily WinUI pin PRs end), no framework bug that has to wait for upstream, no Windows App SDK runtime to deploy self-contained, smaller install, faster cold launch (target < 400 ms in plan section 8 gets easier, not harder).
- **Testability.** Every element renders headless into a WIC bitmap, so layout, hit testing, focus order, UIA trees and pixels are asserted in `recto-tests` without a display session. Today the SPEC's fidelity checks are visual inspection.

## 3. Non-goals

- No cross-platform layer. `recto-ui` is Windows 11 only (D15). A platform-neutral core was offered and declined on 2026-10-22.
- No markup language, designer or reflection-based binding. Screens are built in C++ against a small element API.
- No third-party UI dependency (Skia, Dear ImGui, Qt, Sciter, CEF, WebView2). The toolkit uses only Windows SDK APIs.
- No plug-in or theming surface for users beyond the SPEC's Light, Dark, Follow Windows and high contrast.

## 4. Architecture

### 4.1 Module and dependency rule

```text
Recto (src/app, Win32 EXE, C++23)          screens, view models, commands
   |                 \
   v                  v
recto-services        recto-ui (src/ui, static lib, C++23)
(src/services)           window, compositor, elements, layout, tokens,
   |                     text, input, focus, motion, popups, controls, UIA
   v
recto-core -> recto-pdf
```

- `recto-ui` depends on the Windows SDK only. It never includes `recto-pdf`, `recto-core` or `recto-services` headers, and they never include it (the D13 boundary: engine libraries stay free of UI types).
- `src/app` composes the two: view models in `src/app` read `recto-services`, build `recto-ui` elements, and post engine work through the existing actor contract.
- `recto-services` keeps the presentation thread. It receives an `IDXGISwapChain1` created for composition and the shared `ID3D11Device` from `recto-ui`'s `GraphicsDevice` through plain COM interfaces, so it still has no `recto-ui` include.

### 4.2 Threads

- **UI thread:** one Win32 message loop per process (`ui::Application::run`). It owns every `ui::Window`, element tree, the DirectComposition device's commit and every UIA provider call (UIA calls arrive on this thread through `WM_GETOBJECT` and the provider's own marshalling). It never calls `recto-pdf` and never blocks on an engine thread, exactly as plan section 4.2 requires.
- **`ui::Dispatcher`:** replaces WinUI's `DispatcherQueue`. Any thread posts a closure with `Dispatcher::post`; the UI thread drains it on a private `WM_APP` message and through `MsgWaitForMultipleObjectsEx` so posted work and input interleave fairly. `DocumentActor` callbacks reach the UI through it.
- **Presentation thread (one per `DocumentView`):** unchanged ownership. It owns its `ID3D11DeviceContext` and `ID2D1DeviceContext`, renders into a composition swap chain hosted by a `ui::SwapChainHost` element, and presents on its own cadence; the UI thread only moves and clips the host visual.

### 4.3 Window host

- `ui::Window` creates a `WS_OVERLAPPEDWINDOW` HWND with `WS_EX_NOREDIRECTIONBITMAP`, extends the frame into the client area (`DwmExtendFrameIntoClientArea`, `WM_NCCALCSIZE` returning the full client), and draws the title bar itself while keeping the system resize borders and shadow.
- Backdrop: `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TABBEDWINDOW` (Mica Alt) under `RectoWindowBrush`, `DWMWA_USE_IMMERSIVE_DARK_MODE` following the theme, solid `RectoWindowBrush` when Mica is unavailable or transparency effects are off.
- Caption buttons are drawn by `recto-ui` with the Segoe Fluent Icons system glyphs and Windows 11 metrics (46 x 32 epx at rest), and `WM_NCHITTEST` returns `HTMINBUTTON`, `HTMAXBUTTON` and `HTCLOSE` over them so Windows shows the Snap Layouts flyout on the maximize button. Empty title bar space returns `HTCAPTION`; interactive elements return `HTCLIENT`. `Alt+Space` opens the system menu.
- Per-monitor DPI awareness v2 in `app.manifest`; `WM_DPICHANGED` relayouts at the new scale with the suggested rectangle. `WM_GETMINMAXINFO` enforces 960 x 600 epx at the current scale. `WM_SETICON` sets `resources/ico/recto.ico`.

### 4.4 Composition and drawing

- `ui::GraphicsDevice`: one `ID3D11Device` (`D3D11_CREATE_DEVICE_BGRA_SUPPORT`, hardware with WARP fallback), one multithreaded `ID2D1Factory7`, one `ID2D1Device6`, one `IDWriteFactory7`, one `IDCompositionDesktopDevice`. Device loss (`DXGI_ERROR_DEVICE_REMOVED`, `D2DERR_RECREATE_TARGET`) rebuilds every surface and re-issues every swap chain to its host without losing element state.
- The element tree maps to a small number of composition visuals: one per window root, one per popup, one per element that asks for its own layer (floating cards, the command box, dialogs, the page canvas host, anything animated in composition). Each layer draws into an `IDCompositionVirtualSurface` through `BeginDraw` with the dirty rectangle only.
- Primitives (`ui::Painter`): rounded rectangles at the token radii, 1 px hairlines snapped to device pixels, borders, solid and gradient brushes from tokens, opacity layers, clips, and elevation shadows. Elevation follows the SPEC (`ThemeShadow` with Z 32 for the toolbar and floating panels, Z 64 for dialogs and the command box): `recto-ui` reproduces the Fluent shadow (a key and an ambient blur, values taken from WinUI's ThemeShadow and recorded in the tokens) with the Direct2D Shadow effect, cached per size.
- Acrylic on the floating side panel (SPEC 8.3, "where available"): a backdrop blur of the layer beneath it with the Direct2D Gaussian blur effect plus the Fluent noise and tint, falling back to `RectoRaisedBrush` at 90 percent opacity when transparency effects are off or on battery saver.

### 4.5 Elements and layout

- `ui::Element`: parent and children, desired and arranged bounds in epx, margin, padding, alignment, min and max size, visibility, opacity, transform, clip, automation properties and a tooltip. `Element::render(ui::Painter&)` paints; `Element::hit_test` answers pointer queries.
- Layout is measure then arrange in effective pixels (epx). Final positions round to whole device pixels at the window's scale so 1 epx borders stay crisp at 100, 125, 150, 175 and 200 percent. Panels: `StackPanel`, `Grid` (pixel, auto and star tracks), `DockPanel`, `OverlayPanel` (named slots), `WrapPanel`.
- The SPEC's eight named slots are `OverlayPanel` and `DockPanel` children of the shell. Moving a control between slots reparents the same element instance; nothing is recreated (D17).
- `ui::Property<T>` holds observable values with change callbacks; `ui::Command` holds execute and can-execute. This is the whole binding story: view models in `src/app` expose properties and commands, and controls subscribe.

### 4.6 Tokens

- `design/tokens/colors.json` becomes the single colour source. `scripts/build-ui-tokens.py` (stdlib Python, run by the build and checked by `--check` in CI) generates `src/ui/generated/recto_tokens.g.h` with every `Recto*Brush` colour for Light and Dark, the HighContrast mapping of each brush to a system colour (`COLOR_WINDOW`, `COLOR_WINDOWTEXT`, `COLOR_HIGHLIGHT`, `COLOR_HIGHLIGHTTEXT`, `COLOR_HOTLIGHT`, `COLOR_GRAYTEXT`, `COLOR_BTNFACE`, `COLOR_BTNTEXT`), the radii (`ControlCornerRadius` 4, `OverlayCornerRadius` 8, pill), the 4 epx spacing grid, the elevations, the type ramp and the motion constants. The token names stay the SPEC's names.
- `design/tokens/RectoColors.xaml` is retired by the section that builds the generator (D10 T01 §8), and SPEC section 1 and 4 point at `colors.json`.
- `ui::ThemeManager` resolves Light, Dark or Follow Windows (registry `AppsUseLightTheme` with `WM_SETTINGCHANGE` `ImmersiveColorSet`), high contrast (`SPI_GETHIGHCONTRAST`), transparency effects and the animation setting, and repaints every window live.
- The accent ramp question in SPEC section 4 disappears: `recto-ui` controls read `RectoAccentBrush` and `RectoAccentHoverBrush` directly, so there is no system accent mapping to verify.

### 4.7 Text, icons and images

- DirectWrite with Segoe UI Variable and the SPEC section 5 ramp as named styles (`Caption` 12, `Body` 14, `BodyStrong` 14 Semibold, `Subtitle` 20 Semibold, `Title` 28 Semibold, `TitleLarge` 40 Semibold, `GroupLabel` 12 Semibold caps with 0.6 tracking), Segoe UI Variable's optical axis set from the size like WinUI does. Grayscale antialiasing on composition layers with alpha, ClearType where the layer is opaque. Windows text scaling (`UISettings.TextScaleFactor`) scales the ramp (D10 T03 §6).
- `ui::Icon` draws a glyph of `resources/fonts/RectoIcons.ttf` (D02 T01 §18), loaded as a private DirectWrite font set, by SPEC section 6 concept name, at 16, 20 or 24 epx, Regular or Filled, coloured by state. Built-in control glyphs (caption buttons, chevrons, check marks, spinner arrows, scrollbar arrows) use Segoe Fluent Icons as WinUI's stock controls do, which keeps SPEC section 6's "stock controls keep their system glyphs" rule true.
- `ui::SvgImage` renders `resources/svg/` files through `ID2D1DeviceContext5::CreateSvgDocument`, with a Light and Dark source switched by theme. `ui::BitmapImage` draws BGRA buffers (Home covers, thumbnails, previews) with an explicit stride, never assuming `width * 4`.

### 4.8 Input, focus and motion

- Pointer input through `WM_POINTER*` (`EnableMouseInPointer(TRUE)`) covering mouse, touch and pen with pressure; routing by hit test with capture, enter, leave and bubbling. Wheel and `Ctrl+wheel`, horizontal wheel, and DirectManipulation for precision touchpad and touch panning and pinch zoom in `ScrollViewer` and the page canvas.
- Keyboard through `WM_KEYDOWN`, `WM_SYSKEYDOWN` and `WM_CHAR` to the focused element, then up the tree; a window accelerator table maps SPEC section 10 shortcuts to `ui::Command`s; `Alt` shows access keys for the Classic `MenuBar`.
- `ui::FocusManager`: tab order follows the logical tree with explicit tab stops and focus scopes (dialogs and popups trap focus and return it on close), arrow-key groups for toolbars, lists and segmented controls, and the Fluent focus visual (2 epx outer ring in `RectoAccentBrush`, 1 epx inner ring in the surface colour) shown only for keyboard focus.
- `ui::Motion`: a frame clock driven by DirectComposition animations for opacity, translation and scale, with the Fluent standard easing and the SPEC section 12 timings (167 ms fade and 8 epx translate for panels and cards, the ContentDialog open and close curve for dialogs). `SPI_GETCLIENTAREAANIMATION` off means no motion and no toolbar fade.

### 4.9 Popups and overlays

- Menus, tooltips, combo box lists, suggestion lists and anchored flyouts can extend past the window, so they are owned `WS_POPUP` windows with their own composition target, placed inside the monitor work area, dismissed by outside click, `Esc` or window deactivation, and returning focus to their owner.
- Dialogs, the command box, floating side panels, the queue card, toasts and the drop overlay stay inside the window on the root overlay layer. Dialogs add the smoke layer and trap focus.

### 4.10 Accessibility

- `WM_GETOBJECT` with `UiaRootObjectId` returns the window's `IRawElementProviderFragmentRoot`; every element with an automation role is an `IRawElementProviderFragment` with navigation, bounding rectangles in screen pixels, name, help text, accelerator key, access key and automation id.
- Control patterns per control: Invoke, Toggle, Value, RangeValue, ExpandCollapse, Selection and SelectionItem, Scroll and ScrollItem, Grid and GridItem, Window, Text and TextRange.
- Events: focus changed, property changed, structure changed, notification (for "2 of 14", selection status and job completion announcements), and text selection changed.
- The document page text provider of D03 T02 implements the same `ITextProvider` and `ITextRangeProvider` through the reusable adapter of D10 T03 §3, replacing the planned custom `AutomationPeer`.

### 4.11 Text input

`ui::TextBox` implements editing itself (caret, selection by pointer and keyboard, word navigation, undo and redo, clipboard, placeholder) and the Text Services Framework through `ITextStoreACP2` so IME composition, dictation, the touch keyboard and handwriting panels work in every text field.

### 4.12 Strings

`.resw` and MRT Core leave with the Windows App SDK. `ui::Strings` loads per-language string tables compiled into `Recto.exe` as Win32 `STRINGTABLE` resources (one `.rc` per language under `src/app/strings/`), selects by the Language setting then the Windows display language, falls back to English, and supports a pseudo-locale for layout testing. Default chosen because it needs no runtime and is the Win32 standard; cost of changing later: one loader and a resource conversion script.

### 4.13 What stays exactly the same

`IFileOpenDialog` and `IFileSaveDialog` (system pickers), the Windows print dialog through `IPrintManagerInterop::GetForWindow` and `ID2D1PrintControl`, the Share contract through `IDataTransferManagerInterop::ShowShareUIForWindow`, Windows notifications through `Windows.UI.Notifications` with the AppUserModelID carried by the installer's Start menu shortcut (Portable mode writes no registry key, so it registers no AppUserModelID and shows the in-app toast instead; default taken 2026-10-22, reversing it costs an exception in the zero-registry proof of `D07 T01 §7`), `.pdf` file association, single-instance forwarding, Inno Setup and optional MSIX (now a full-trust Win32 desktop package with no Windows App SDK framework dependency).

## 5. WinUI to recto-ui mapping

Every TODO section that named a WinUI type is rewritten against this table. Section addresses are in `todo/10-ui/`.

| WinUI 3 or Windows App SDK | recto-ui replacement | Owner |
| --- | --- | --- |
| `Application`, `App.xaml`, `MainWindow.xaml`, `DispatcherQueue` | `ui::Application`, `ui::Window`, `ui::Dispatcher` | D10 T01 §2, §3, §4 |
| `AppWindow`, `AppWindowTitleBar`, `ExtendsContentIntoTitleBar`, `InputNonClientPointerSource`, `SetRegionRects`, `OverlappedPresenter` | `ui::Window` custom frame and non-client hit testing | D10 T01 §4, §19 |
| `XamlRoot.RasterizationScale`, `SizeChanged`, `Window::Activated` | `ui::Window::dpi_scale()`, `ui::Window` resize and activation events | D10 T01 §4 |
| `SwapChainPanel`, `ISwapChainPanelNative::SetSwapChain` | `ui::SwapChainHost` with `CreateSwapChainForComposition` | D10 T01 §18 |
| `Grid`, `StackPanel`, `Canvas`, `RelativePanel` | `ui::Grid`, `ui::StackPanel`, `ui::DockPanel`, `ui::OverlayPanel`, `ui::WrapPanel` | D10 T01 §7 |
| `ResourceDictionary`, `ThemeDictionaries`, `RectoColors.xaml`, `{ThemeResource}` | generated `recto_tokens.g.h` from `colors.json`, `ui::ThemeManager` | D10 T01 §8 |
| `ThemeShadow`, `Translation` Z, `ControlCornerRadius`, `OverlayCornerRadius` | `ui::Painter` elevation and token radii | D10 T01 §9 |
| `TextBlock`, type ramp styles | `ui::TextBlock`, `ui::TextStyle` | D10 T01 §10 |
| `FontIcon` + `RectoIconsFontFamily`, `SvgImageSource`, `Image` | `ui::Icon`, `ui::SvgImage`, `ui::BitmapImage` | D10 T01 §11 |
| pointer, keyboard, `KeyboardAccelerator`, `AccessKey` | `ui::Input`, window accelerator table, access keys | D10 T01 §12 |
| focus, `FocusVisual`, `TabIndex`, `XYFocus` | `ui::FocusManager` | D10 T01 §13 |
| `Storyboard`, implicit animations, connected animations | `ui::Motion` | D10 T01 §14 |
| `Popup`, light dismiss | `ui::PopupHost` and the overlay layer | D10 T01 §15 |
| `x:Bind`, `INotifyPropertyChanged`, `ICommand` | `ui::Property<T>`, `ui::Command` | D10 T01 §16 |
| `DragDrop`, `DataPackage`, `Clipboard` | OLE `IDropTarget`, `ui::Clipboard` | D10 T01 §17 |
| `Control`, `VisualStateManager`, `Style` | `ui::Control` with visual states and style structs | D10 T02 §1 |
| `Button`, `AccentButtonStyle`, `ToggleButton`, `AppBarButton`, `HyperlinkButton` | `ui::Button` family | D10 T02 §2 |
| `ToolTip`, `ToolTipService` | `ui::ToolTip` | D10 T02 §3 |
| `TextBox`, `RichEditBox` (plain) | `ui::TextBox` (+ TSF) | D10 T02 §4, §5 |
| `PasswordBox`, `NumberBox` | `ui::PasswordBox`, `ui::NumberBox` | D10 T02 §6 |
| `CheckBox`, `RadioButton`, `RadioButtons`, `ToggleSwitch` | same names in `ui::` | D10 T02 §7 |
| `Slider`, `ProgressBar`, `ProgressRing` | same names in `ui::` | D10 T02 §8 |
| `ComboBox`, `DropDownButton`, `SplitButton` | `ui::ComboBox`, `ui::DropDownButton` | D10 T02 §9 |
| `ScrollViewer`, `ScrollView` | `ui::ScrollViewer` | D10 T02 §10 |
| `ListView`, `GridView`, `ItemsRepeater`, `ItemsView` | `ui::ItemsView` | D10 T02 §11 |
| `TreeView` | `ui::TreeView` | D10 T02 §12 |
| `TabView` | `ui::TabStrip` | D10 T02 §13 |
| `MenuFlyout`, `ContextFlyout`, `CommandBarFlyout` | `ui::Menu` | D10 T02 §14 |
| `MenuBar` | `ui::MenuBar` | D10 T02 §15 |
| `Flyout`, `TeachingTip` | `ui::Flyout`, `ui::Card` | D10 T02 §16 |
| `ContentDialog`, `RectoDialogStyles.xaml` | `ui::Dialog` | D10 T02 §17 |
| `SelectorBar`, `Pivot`, segmented controls | `ui::SegmentedControl`, `ui::ModeSwitcher` | D10 T02 §18 |
| `NavigationView`, `Expander`, `SettingsCard` | `ui::NavigationRail`, `ui::Expander`, `ui::SettingsGroup`, `ui::SettingsCard` | D10 T02 §19 |
| `InfoBar`, in-app toast, `RectoChipStyle` base | `ui::InfoBar`, `ui::Toast`, `ui::Chip`, `ui::Badge`, `ui::Keycap` | D10 T02 §20 |
| `AutoSuggestBox` | `ui::SearchBox` | D10 T02 §21 |
| `ColorPicker` (swatches) | `ui::ColorSwatchPicker` | D10 T02 §22 |
| `ColorPicker` (custom colour) | `ui::ColourEntry` | D10 T02 §27 |
| `TextBox.IsSpellCheckEnabled` | `TextBox` spell checking (`ISpellChecker`) | D10 T02 §24 |
| `CalendarView`, `CalendarDatePicker`, `DatePicker` | `ui::CalendarFlyout`, `ui::DatePicker` | D10 T02 §25 |
| community `GridSplitter` | `ui::Splitter` | D10 T02 §26 |
| `AutomationProperties.Name`, `AutomationPeer`, `ITextProvider` | `ui::Element` automation properties, UIA providers | D10 T03 §1, §2, §3 |
| `.resw`, `ResourceLoader`, MRT Core | `ui::Strings` | D10 T03 §7 |
| `AppNotificationManager` (Windows App SDK) | `Windows.UI.Notifications` toast with a registered AUMID | D04 T01 §6 |
| `WindowsAppSDKSelfContained`, Windows App SDK runtime | nothing: no runtime ships | D10 T01 §2, D07 T01 |

## 6. The TODO domain `todo/10-ui/`

Domain number 10 because `09` was held by the withdrawn Recto Cloud domain (D20) and stays unused so old references cannot be misread. Phase 2 (M2) throughout; every M2 surface section depends on the controls it consumes.

### TODO-01 `ui-foundation`: window, composition, elements, layout, tokens, text and input

| § | Deliverable | Depends On |
| - | ----------- | ---------- |
| 1 | `recto-ui` static library, module boundary and headless render harness | D00 T01 §3 |
| 2 | Win32 `Recto.exe` host replaces the WinUI scaffold; Windows App SDK pins retired | §1 |
| 3 | UI thread `Dispatcher`, timers and engine callback marshalling | §1 |
| 4 | Top-level `Window`: custom frame, Mica Alt, dark mode, DPI and minimum size | §2, §3 |
| 5 | Compositor: shared D3D11 device, DirectComposition tree, D2D surfaces, device loss | §4 |
| 6 | `Element` tree: retained rendering, invalidation, hit testing and visual layers | §5 |
| 7 | Layout engine: epx measure and arrange, pixel snapping, Stack, Grid, Dock, Overlay | §6 |
| 8 | Design tokens generated from `design/tokens/colors.json` and `ThemeManager` | §1 |
| 9 | Drawing primitives: token radii, hairlines, borders, opacity, elevation shadows | §5, §8 |
| 10 | Text: DirectWrite type ramp, `TextBlock`, trimming and highlighted ranges | §7, §8 |
| 11 | Icon, SVG and bitmap image elements | §10, D02 T01 §18 |
| 12 | Pointer and keyboard input: routing, capture, wheel, accelerators, access keys | §6 |
| 13 | Focus manager: tab order, focus scopes, arrow groups and the Fluent focus visual | §9, §12 |
| 14 | Motion: frame clock, Fluent easing, panel motion and the Windows animation setting | §6 |
| 15 | Popup host: owned popup windows, placement, light dismiss, in-window overlay layer | §5, §13 |
| 16 | Observable properties, `Command` objects and view-model binding helpers | §3 |
| 17 | OLE drag-and-drop target and clipboard formats | §3, §12 |
| 18 | `SwapChainHost`: composition swap chain element for the page canvas | §5, §6 |
| 19 | Non-client hit testing: drag regions, resize edges, caption buttons, Snap Layouts | §4, §9, §10, §12 |
| 20 | Golden-image suite: headless renders at 100, 150 and 200 percent against SPEC boards | §1, §9, §10 |

### TODO-02 `ui-controls`: the Fluent control set the SPEC uses

| § | Deliverable | Depends On |
| - | ----------- | ---------- |
| 1 | `Control` base: visual states, tokens, automation name, tooltip and command hooks | D10 T01 §9, D10 T01 §10, D10 T01 §13, D10 T01 §16 |
| 2 | Buttons: standard, accent, danger, icon, toggle and hyperlink | §1, D10 T01 §11 |
| 3 | `ToolTip` with name and shortcut text | §1, D10 T01 §15 |
| 4 | `TextBox` editing core: caret, selection, undo, clipboard and placeholder | §1, D10 T01 §17 |
| 5 | Text Services Framework for `TextBox`: IME, dictation and touch keyboard | §4 |
| 6 | `PasswordBox` and `NumberBox` | §4 |
| 7 | `CheckBox`, `RadioButton` group and `ToggleSwitch` | §1 |
| 8 | `Slider`, `ProgressBar` and `ProgressRing` | §1, D10 T01 §14 |
| 9 | `ComboBox` and `DropDownButton` | §2, D10 T01 §15 |
| 10 | `ScrollViewer`: overlay scrollbars, smooth wheel, DirectManipulation pan and pinch | §1, D10 T01 §14 |
| 11 | Virtualized `ItemsView`: stack, filmstrip and grid layouts, selection, reorder hooks | §10 |
| 12 | `TreeView`: expand, collapse, keyboard, selection bar and drag reorder | §11 |
| 13 | `TabStrip`: title-bar document tabs, close, add, overflow and drag reorder | §2, §10 |
| 14 | `Menu` and context menu: glyphs, shortcuts, separators, submenus and check items | §2, D10 T01 §15 |
| 15 | `MenuBar` with access keys and Alt activation | §14 |
| 16 | `Flyout` popover and the floating `Card` container | §1, D10 T01 §14, D10 T01 §15 |
| 17 | `Dialog`: modal overlay, smoke, alert and form layouts and Fluent motion | §2, §16 |
| 18 | `SegmentedControl` and the underlined mode switcher | §2 |
| 19 | `NavigationRail`, `Expander`, `SettingsGroup` and `SettingsCard` | §2, §7 |
| 20 | `InfoBar`, in-app `Toast`, and the `Chip`, `Badge` and `Keycap` pills | §2, D10 T01 §14 |
| 21 | `SearchBox` with a suggestion list for the command box and Find a setting | §4, §11, D10 T01 §15 |
| 22 | `ColorSwatchPicker` from token swatches | §16 |
| 23 | Control gallery: every control in Light, Dark and HighContrast with golden images | §2 to §22, §24 to §27, D10 T01 §20 |
| 24 | Spell checking for `TextBox` through the Windows Spell Checking API | §4, §14 |
| 25 | `CalendarFlyout` and `DatePicker` with locale-aware weeks and formats | §11, §16 |
| 26 | `Splitter` for resizable panes with pointer, keyboard and limits | §1 |
| 27 | Custom colour entry: hex field, live preview and recent colours | §4, §22 |

### TODO-03 `ui-accessibility`: UI Automation, high contrast, text scale and strings

| § | Deliverable | Depends On |
| - | ----------- | ---------- |
| 1 | UI Automation provider tree: fragment root, navigation, properties, focus events | D10 T02 §1 |
| 2 | UIA control patterns for every recto-ui control | §1, D10 T02 §23 |
| 3 | UIA `TextPattern` for `TextBox` and a reusable text-range adapter | §1, D10 T02 §4 |
| 4 | UIA notification and live-region announcements | §1 |
| 5 | High contrast: system colour mapping, live switching and high-contrast focus visuals | D10 T01 §8, D10 T01 §13 |
| 6 | Windows text scaling applied to the type ramp and layout | D10 T01 §7, D10 T01 §10 |
| 7 | `Strings`: localized string tables, fallback and pseudo-localization | D10 T01 §1 |
| 8 | Accessibility verification: UIA tree walk, keyboard-only traversal, Narrator smoke | §2, §3, §4, §5, §6 |

## 7. Changes to the existing plan

- `D00 T01 §3` (stamped) built the WinUI scaffold. Its checklist stays as stamped; a dated correction note points at `D10 T01 §2`, which replaces the scaffold.
- `D02 T01 §1` keeps the manifest, assets and minimum window size, and depends on `D10 T01 §2`, `§4` and `§8` instead of configuring WinUI. `D02 T01 §2` builds the title bar on `D10 T01 §19`. `D02 T01 §18` still builds `RectoIcons.ttf`; `D10 T01 §11` consumes it. `D02 T01 §19` builds the `RectoChip` variants on `D10 T02 §20`.
- Every other section that names a WinUI type is rewritten against section 5 and gains `Depends On` edges to the `D10` sections it consumes.
- `D03 T02` builds the document text provider on `D10 T03 §1` and `§3`.
- `D04 T01 §6` uses `Windows.UI.Notifications` with a registered AUMID instead of `AppNotificationManager`.
- `D06 T04` design boards gain pixel comparison through `D10 T01 §20`.
- `D07 T01` and `D07 T02` package a Win32 desktop app: no `WindowsAppSDKSelfContained`, no Windows App SDK in the About "Built with" card or `NOTICE.md`; MSIX declares `runFullTrust` with no framework dependency.
- The `display-session` Need means a session that can show a `recto-ui` window.
- `scripts/check-winui-pin.py`, its tests and the `winui` job in `.github/workflows/pins.yml` are removed by `D10 T01 §2`, together with the `Microsoft.WindowsAppSDK` pin; `scripts/check-sdk-pins.py` keeps the `Microsoft.Windows.CppWinRT` and `Microsoft.Windows.SDK.BuildTools` pins.

## 8. Risks and how the plan answers them

| Risk | Answer |
| --- | --- |
| Accessibility parity with WinUI's built-in peers is a large surface | UIA is its own TODO file with a verification section that walks the tree and drives keyboard-only traversal; no control stamps without its pattern (D10 T03 §2 depends on the gallery). |
| IME and text input are easy to get subtly wrong | TSF is its own section (D10 T02 §5) with Japanese, Chinese and Korean IME composition tests and the touch keyboard. |
| Look drifts from Fluent over time | Golden images per control and per board at three scales and three themes (D10 T01 §20, D10 T02 §23, D06 T04). |
| Schedule: M2 now starts with a toolkit | The toolkit is built bottom-up in dependency order; the shell sections start as soon as the window, layout, tokens, text and buttons exist, not after the whole control set. |
| Smooth scrolling and zoom feel | DirectManipulation drives touchpad and touch; the page canvas keeps the presentation thread and stretched tiles (plan 4.5) unchanged. |

## 9. Cost of reversing

Before `D10 T01 §2` ships: low, revert the documents. After the control set exists: high, a second full UI rewrite, similar to the original D4 reversal cost. The engine, core and services are unaffected either way because `recto-ui` sits beside them, not under them.
