---
trigger: always_on
---
# Design Contract Rule

## Contract
- Authoritative UI contract: `design/SPEC.md` and `design/README.md`, with approved mockups in `design/mockups/html/A-Home.html` and `design/mockups/html/A-Viewer.html`.

## Tokens and icons
- Token source: `design/tokens/colors.json`.
- Brand and app graphics are generated into `resources/` from `design/brand/github-banner.png` and icon sources.

## Never
- Never add raw `#RRGGBB` or `0xRRGGBB`, hardcoded `rgb()` or `ColorF`, or literal spacing, radius, or font size where a token in `design/tokens/colors.json` exists.
- Never add an inline unmanaged SVG, an emoji as an icon, or an unapproved backdrop.
- Never change a designed surface without a `design/SPEC.md` update first.

## Static lint
- No static design-lint script is installed yet (planned in Conclave-SDK `D00 T03 §7`, which will update this rule and `AGENTS.md` on install).

## Rendered check
- No built `recto-ui` window render command exists today; each surface section must build or name its own rendered check.
- Planned built-app render command: `D02 T01 §23` in `todo/02-viewer/TODO-01-app-shell.md`.
