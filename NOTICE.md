# Third-Party Notices

Recto is distributed under the GNU General Public License, version 3 or later (`GPL-3.0-or-later`); see [`LICENSE`](./LICENSE) for the full text.

This file records the third-party components and assets that exist in the repository today and the components that will enter the build as the milestones land.
The full build-derived audit and compatibility record is owned by [`todo/07-release/TODO-02-store-and-compliance.md`](./todo/07-release/TODO-02-store-and-compliance.md) section 2 before version 1 ships, and the in-app **About -> Built with** list is generated from the same data so the two cannot drift.

## Present in the repository now (M0)

| Component | Path | License | Notes |
| --------- | ---- | ------- | ----- |
| Newsreader font outlines | `resources/svg/wordmark-*.svg`, `resources/svg/lockup-*.svg`, `design/brand/*.png` | SIL Open Font License 1.1 | Used only for the "Recto" wordmark and tagline, converted to SVG paths; the font file itself is not shipped in the app package. Copyright the Newsreader Project Authors. |

## Entering the build in M0 to M6 (none vendored in shipping code; tracked here so the roster is never implicit)

| Component | Enters at | Expected license | Shipped in app? |
| --------- | --------- | ---------------- | --------------- |
| Recto PDF Engine (`recto-pdf`) | `D00 T01 §3` (scaffold), `D01` (engine) | `GPL-3.0-or-later` | Yes (authored in-tree in C++20; zero MuPDF dependency; uses Windows 11 platform APIs: Direct2D, DirectWrite, WIC, Windows CNG `bcrypt.dll`, `Windows.Media.Ocr`) |
| Windows App SDK 2.5.1 and `Microsoft.Windows.CppWinRT` | `D00 T01 §3` (NuGet) | MIT | Yes |
| WebView2 SDK (`Microsoft.Web.WebView2` 1.0.3719.77) | `D00 T01 §3` (Windows App SDK dependency) | BSD-3-Clause | Yes (`Microsoft.Web.WebView2.Core.dll` beside `Recto.exe`; the WebView2 runtime is part of Windows 11) |
| GoogleTest | `D00 T01 §3` (manifest), `D00 T03 §1` (wiring) | BSD-3-Clause | No (test runner only, per decision D14) |
