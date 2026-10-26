# Adobe Core14 AFM files

The Adobe Font Metrics of the fourteen standard PDF fonts (ISO 32000-1 section 9.6.2.2): Courier, Helvetica and Times in four styles each, Symbol and ZapfDingbats. `scripts/build-font-metrics.py` generates `src/pdf/standard14_metrics.inc` from them (D01 T02 section 27).

## Licence

Adobe's notice in `MustRead.html` applies to every file here and travels with them: the files may be used, copied and distributed for any purpose and without charge, with or without modification, provided the copyright notices are kept, the AFM files are not distributed without `MustRead.html`, modifications are prominently noted, and that paragraph is not modified. These files are unmodified. The notice is compatible with Recto's `GPL-3.0-or-later`.

## Source

Adobe published the set as `Core14_AFMs.zip` (and earlier `Core14_AFMs.tar`) on its developer site; every Adobe URL for it answered 404 on 2026-10-26. The files were taken that day from two independent mirrors of Adobe's set, which agree line for line on all fourteen AFMs:

- `tecnickcom/tc-font-core14-afms` at commit `0675784d24b28a55c607cad6b74596ce19ce333c`: the AFM files here, byte for byte, with their CRLF line ends.
- Apache PDFBox at commit `84492c427bbcd426f9eeceebb59087bc82f5e08c` (`pdfbox/src/main/resources/org/apache/pdfbox/resources/afm/`): `MustRead.html` here, byte for byte; its AFM copies have every line end doubled, and with that undone match the files here.

`.gitattributes` keeps this directory's AFM and HTML files byte-exact (`-text`).

## SHA-256

`scripts/build-font-metrics.py` refuses to generate from a file whose sum differs from this table, and `tests/scripts` runs it with `--check`.

| File | SHA-256 |
| ---- | ------- |
| `Courier-Bold.afm` | `ad0150d4bedcc8877742bf94251fcec13e348dd599d4603f679d92027d1e6e99` |
| `Courier-BoldOblique.afm` | `cb82e69ef5f6d421e8f404fe00bb0d993425aae79725331d0ebf847e94e97e92` |
| `Courier-Oblique.afm` | `b27103b2a2ef6030c110626597e2ab47bb8279075a039ab9170facb0aa1f70e1` |
| `Courier.afm` | `521e0d7c7521efd4be78a5a9c5398e4c67d0771e396115b0346bc4ef74ada53d` |
| `Helvetica-Bold.afm` | `b880d96baf56d0cc059f258f60b4d764ef49b555ab9db294b959c0016dee41f2` |
| `Helvetica-BoldOblique.afm` | `69984a35ca26973a39f261cf83e0d367ea2e6517c590b0b030d4d4a219d9c269` |
| `Helvetica-Oblique.afm` | `b4609b71b660a392ac09df35060271a876c2ce66617dd83bf826f742bb9d9721` |
| `Helvetica.afm` | `da33f1870474c8e68bfe3e2353ff107ab6c6eea1f9836ce2aaf1e1a07b17982f` |
| `MustRead.html` | `b226bfc00e1b8b8a80c7b3cfbbc322d13b4b0401f94cdeafdd93b2210ad802eb` |
| `Symbol.afm` | `3d2128a820375a10de9bc8bf6cfb15ded482c01ca0f95cc0b3277f37ec8bde66` |
| `Times-Bold.afm` | `b4a000ed85cb22c6cdd985aa0fd3f6f78ed5079b7c6860dc4f0234e0d0e3c522` |
| `Times-BoldItalic.afm` | `93c4744ba955215de02c4aae0b777133442ada2f7ab5a2af30a040b792b3c55d` |
| `Times-Italic.afm` | `ed37fa2e6a67b5b17dfd47f36fc7e90df32891a4408860dbc8d4cbbe9959e242` |
| `Times-Roman.afm` | `768e1cabea085d489a63da3e80b96bc5abf0ec98d3073c9b4d6ba76e7bccba64` |
| `ZapfDingbats.afm` | `a32565c90afd1b57a7008fc567b78d95cf1c22adff5e086094d666d88b039859` |
