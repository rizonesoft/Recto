# ISO 32000-1 Annex D encodings

`annex-d.txt` holds the character codes of the simple-font encodings of ISO 32000-1 Annex D, one `encoding code name` line per encoded code (codes in octal, as Annex D gives them): `StandardEncoding`, `MacRomanEncoding`, `WinAnsiEncoding` and `MacExpertEncoding` (Table D.2 and D.3), the built-in encodings of `Symbol` (D.5) and `ZapfDingbats` (D.6), and `MacOSRoman`, the Mac OS standard Roman character set that section 9.6.6.4 uses for a TrueType `(1,0)` subtable. `scripts/build-glyph-names.py` generates `src/pdf/glyph_names.inc` from it and the Adobe Glyph List (D01 T02 section 28).

## How the tables were checked

Annex D is not machine-readable, so the tables were taken on 2026-10-26 from two independent transcriptions, compared code by code, and every difference settled from Annex D itself:

- `StandardEncoding`, `MacExpertEncoding` and `Symbol` agree in both: pdf.js (`src/core/encodings.js` at `606bd6a4e138bbbc22fad10872d2ffa93365b6b1`) and Apache PDFBox (`pdmodel/font/encoding/` at `84492c427bbcd426f9eeceebb59087bc82f5e08c`).
- `StandardEncoding`, `Symbol` and `ZapfDingbats` equal the codes of Adobe's Core14 AFM files (`src/pdf/adobe-core14-afm/`), which the generator checks on every run. PDFBox lacks `ZapfDingbats` 0x80 to 0x8D (`a89` to `a88`), which the AFM encodes.
- `WinAnsiEncoding` follows Annex D's notes: `space` is also at 0240, `hyphen` also at 0255, and every unused code above 040 maps to `bullet` (0177, 0201, 0215, 0217, 0220, 0235). pdf.js does so; PDFBox writes `nbspace` and `sfthyphen` and leaves the unused codes empty.
- `MacRomanEncoding` follows Annex D: 208 codes, with `space` also at 0312 (PDFBox writes `nbspace`). pdf.js's table adds fifteen Mac OS characters Annex D's encoding does not have (`notequal` 0255 to `apple` 0360); with them, and with `Euro` in place of `currency` at 0333, it is `MacOSRoman`, as ISO 32000-1 Table 115 gives the Mac OS standard Roman set (the `Euro` from the Table, not from either transcription, both of which keep `currency`).

The generator also checks those notes on every run, so an edit that breaks one fails `--check`.

## Macintosh glyph names

`mac-glyph-names.txt` holds the 258 standard Macintosh glyph names a TrueType `post` table of format 1 or 2 refers to by index, from Apple's TrueType Reference Manual (the `post` table chapter, which Microsoft's OpenType specification points to), checked equal, name for name, to Apache PDFBox's `WGL4Names` at `84492c427bbcd426f9eeceebb59087bc82f5e08c`. The generator checks there are 258, unique, starting `.notdef`.
