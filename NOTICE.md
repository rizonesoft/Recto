# Third-party notices

Recto itself is licensed under the [GNU Affero General Public License, version 3 or later](LICENSE). This file records the third-party components Recto uses and the license each one ships under. It is a skeleton until the first component is actually linked: entries appear when the component lands (the MuPDF pin in `D00 T01 §2` records its exact version and submodule commit here), and the complete, build-derived audit is owned by `D07 T02 §2`, which also feeds the About page's Built-with list. Nothing below claims a version that has not been pinned yet.

## Planned components (not yet linked)

| Component | Purpose | License | Lands in |
| --------- | ------- | ------- | -------- |
| MuPDF (Artifex) | PDF parsing, rendering, text, document writers. Built from a pinned release tag as a git submodule with Recto's own config, JavaScript disabled. MuPDF bundles its own third-party libraries (including FreeType, HarfBuzz, libjpeg, OpenJPEG, zlib, Little CMS, jbig2dec and extract); each is itemized with its license here when the pin lands. | GNU Affero General Public License, version 3 | D00 T01 §2 |
| Windows App SDK 2.4.0 (Microsoft) | WinUI 3 application framework for the packaged app. | MIT License | D00 T01 §3 |
| GoogleTest | Unit tests only; not shipped in the application. | BSD 3-Clause License | D00 T03 §1 |

## Assets

| Asset | Notes | License |
| ----- | ----- | ------- |
| Newsreader (Google Fonts) | The wordmark typeface. Only outlined letterforms ship (see `resources/README.md`); the font itself is not distributed with the app. | SIL Open Font License 1.1 |
| Segoe Fluent Icons | The system icon font, supplied by Windows at runtime; nothing is bundled. | Provided by Windows |

## How this file stays true

A component is added here in the same commit that first links or redistributes it, with its exact version. The pre-release audit in `D07 T02 §2` re-derives this list from the actual build and refuses a release whose notices and linked libraries disagree.
