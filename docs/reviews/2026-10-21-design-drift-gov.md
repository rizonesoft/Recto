## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T07:36:39Z
- Effort: medium
- Lines: 23
- Digest: a8e83a6c7e5072f4

Review is limited to the supplied diff. Repository reads failed because the execution helper could not start and the alternate file reader had no connected device. No files were modified; git and the generator were not run.

**consistency: needs-attention**

1. **The SVG master cannot deliver its documented size-dependent appearance.** `design/SPEC.md` §3.1 and `resources/README.md`, Contents, promise a firmer edge without a shadow at 32 to 47 px, and a shadow from 48 px. In `design/tools/build_resources.py`, `main()` always creates `app-icon.svg` with `app_icon(shadow=True)`. Scaling this SVG down retains its shadow and thin edge. Only the PNG generation path implements the threshold. Provide separate masters or explicitly restrict the size-dependent promise to raster assets and specify the correct SVG usage.

**source-defect: needs-attention**

2. **The installer image dimensions and DPI-selection explanation are incorrect.** `design/SPEC.md` §3.6, `resources/README.md`, Contents, and the generator’s `wizard_image()`, `wizard_small()` and `scaled()` use 164×314 and 55×58 multiplied by DPI percentage. Inno Setup’s small image area is square, including under its older defaults. Current default large-image dimensions start at 202×386; 164×314 describes older defaults. Both areas scale according to font metrics, not simple DPI multiplication. The small artwork can be distorted, and the advertised per-scale images will not match the actual areas. Specify the installer configuration and generate dimensions for that configuration. PNGs and comma-separated lists themselves are supported. Sources: [WizardImageFile](https://jrsoftware.org/ishelp/topic_setup_wizardimagefile.htm), [WizardSmallImageFile](https://jrsoftware.org/ishelp/topic_setup_wizardsmallimagefile.htm).

**integration: needs-attention**

3. **Generated brand graphics omit required content.** `design/SPEC.md` §3.5 requires both images to contain the Sheet app icon and three feature chips; the foundation plan’s “Brand and installer graphics” decision also requires the Sheet icon. In `design/tools/build_resources.py`, `github_banner()` renders neither the icon nor chips. `github_social_preview()` renders chips but no Sheet icon. Its window sketch and hanging ribbon do not implement the specified app icon.

4. **Shipping graphics still bypass colour tokens.** In `design/tools/build_resources.py`, `window_sketch()` hardcodes `#D3CEC5` for page lines, and `main()` retains `fill="#FFFFFF"` for the PDF badge lettering. The latter bypasses the newly introduced `brand.fileTypeBadgeText` token entirely. Changes to the authoritative tokens therefore cannot regenerate the corresponding artwork correctly. This violates `resources/README.md`’s generation contract and SPEC §3’s token ownership.

5. **The advertised complete contact sheet omits the social preview.** `resources/README.md` promises “everything on one sheet,” and SPEC §3.1 says the preview shows all graphics. `write_preview()` adds the banner and installer images but never adds `design/brand/github-social-preview.png`. The new social artwork consequently escapes the documented visual inspection surface.

**testability: advisory**

6. **Custom-glyph acceptance remains subjective.** `design/SPEC.md` §6 requires Redact and Squiggly underline to be drawn “in the Fluent style,” but supplies no reference artwork or measurable stroke, padding and alignment criteria. The 20 px grid alone cannot establish visual conformance. Define those criteria in the owning design-board section before accepting the custom glyphs.

**OVERALL: needs-attention**

## Response to round 1

1. Fixed: `build_resources.py` writes a third master, `resources/svg/app-icon-medium.svg` (no shadow, firmer edge) for 32 to 47 px, and SPEC 3.1 and `resources/README.md` now give `app-icon.svg` as the 48 px and up master.
2. Fixed: verified against the Inno Setup `WizardImageFile` and `WizardSmallImageFile` help (image areas follow font metrics; 164:314 aspect and a square small area). The generator now renders exact area sizes (202x386 to 534x1022, and square 58 to 159 px) from an `INSTALLER_SIZES` table, and SPEC 3.6 and the README list them.
3. Fixed: the banner now carries the Sheet icon and the three chips, and the social preview carries the Sheet icon, beside the hanging ribbon and wordmark SPEC 3.5 names.
4. Fixed: the window sketch's page lines use `brand.iconLines` and the PDF badge text uses `brand.fileTypeBadgeText`; the only remaining literal colours are the contact sheet's own presentation backgrounds, which are not shipped.
5. Fixed: `resources/preview.png` now shows the social preview too.
6. Fixed: SPEC 6's custom-glyph rule now has measurable acceptance (1 px strokes on the 20 px grid, round caps and joins, 1 to 2 px corners, a 2 to 18 px live area, whole-pixel edges, Filled keeps the Regular outline, and a side-by-side review at 16, 20 and 24 epx).

