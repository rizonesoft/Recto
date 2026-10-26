# Adobe Glyph List

Adobe's `glyphlist.txt` (the Adobe Glyph List 2.0: glyph names to Unicode) and `zapfdingbats.txt` (the ZapfDingbats glyph names to Unicode), byte for byte from `adobe-type-tools/agl-aglfn` at commit `4036a9ca80a62f64f9de4f7321a9a045ad0ecfd6`. `scripts/build-glyph-names.py` generates `src/pdf/glyph_names.inc` from them (D01 T02 section 28).

## Licence

BSD-3-Clause, Copyright 2002-2019 Adobe: the notice is the head of each file, which these copies keep unmodified, and `NOTICE.md` lists them.

## SHA-256

`scripts/build-glyph-names.py` refuses to generate from a file whose sum differs from this table, and `tests/scripts` runs it with `--check`.

| File | SHA-256 |
| ---- | ------- |
| `glyphlist.txt` | `a3b2f61ced9f3644cc0d4ecde5c59df34ca286c689d9484a43a710a81c466789` |
| `zapfdingbats.txt` | `f6394e3cb8a447e84a1dad75d4baaf2aa7f45dc104faf369f4720e1a774ef2dc` |
