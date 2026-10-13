# Recto design

Everything needed to build Recto's interface, without access to the original design canvas.

Start with [`SPEC.md`](SPEC.md). It is the source of truth and points to a screenshot for every screen and state.

| Folder | Contents |
|---|---|
| [`screenshots/`](screenshots/) | The chosen design (`D-*.png`) and the explorations it came from (`explorations/`) |
| [`tokens/`](tokens/) | Colours as JSON and as a WinUI 3 `ResourceDictionary` |
| [`brand/`](brand/) | GitHub README banner and social preview |
| [`../resources/`](../resources/) | Every icon and graphic the app ships (app icon, PDF file-type icon, wordmark, MSIX PNGs, ICO files, in-app graphics) |
| [`mockups/html/`](mockups/html/) | Static HTML of every screen, for measuring in a browser |
| [`mockups/source/`](mockups/source/) | Canvas sources, used to regenerate the HTML and screenshots |
| [`tools/`](tools/) | `render-mockups.mjs` (screenshots) and `build_resources.py` (icons and graphics) |

## Chosen screens

| Screen | Screenshot |
|---|---|
| Viewer, Focus layout, light | `screenshots/D-Focus-Light.png` |
| Viewer, Focus layout, dark | `screenshots/D-Focus-Dark.png` |
| Viewer, Classic layout | `screenshots/D-Classic.png` |
| Viewer, Reading room layout, dark | `screenshots/D-ReadingRoom.png` |
| Home | `screenshots/D-Home.png` |
| Search and text selection | `screenshots/D-Search.png` |
| Convert | `screenshots/D-Convert.png` |
| Organize (reorder) | `screenshots/D-Organize.png` |
| Settings: layout presets | `screenshots/D-Layout.png` |
| Settings: other sections | `screenshots/D-Settings.png` |
| About and license | `screenshots/D-About.png` |
| Dialogs and command box | `screenshots/D-Dialogs.png` |
| Icon, wordmark and colour tokens | `screenshots/D-Brand.png` |

The application architecture is in [`docs/plans/2026-10-13-recto-foundation.md`](../docs/plans/2026-10-13-recto-foundation.md).
