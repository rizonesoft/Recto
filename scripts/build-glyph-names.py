#!/usr/bin/env python3
"""Generate src/pdf/glyph_names.inc: the Annex D encodings and the Adobe Glyph List (D01 T02 section 28).

Reads src/pdf/pdf-encodings/annex-d.txt (ISO 32000-1 Annex D, transcribed; see its README) and the Adobe Glyph List
files in src/pdf/adobe-glyph-list (refused when a sum differs from that directory's README), checks the transcription
against Adobe's Core14 AFM codes and Annex D's own notes, and writes constexpr tables: each encoding's 256 glyph names
("" for none), the Glyph List's names in byte order with their Unicode scalars, and the ZapfDingbats names.

    python scripts/build-glyph-names.py           # regenerate the table
    python scripts/build-glyph-names.py --check   # exit 1 if the table is not what the sources give
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PDF_DIR = REPO_ROOT / "src" / "pdf"
OUT_PATH = PDF_DIR / "glyph_names.inc"

# The encodings in the order of recto::pdf::BaseEncoding, then the Mac OS Roman set.
ENCODINGS = ["StandardEncoding", "MacRomanEncoding", "WinAnsiEncoding", "MacExpertEncoding", "Symbol", "ZapfDingbats",
             "MacOSRoman"]
SUFFIX = {"StandardEncoding": "Standard", "MacRomanEncoding": "MacRoman", "WinAnsiEncoding": "WinAnsi",
          "MacExpertEncoding": "MacExpert", "Symbol": "Symbol", "ZapfDingbats": "ZapfDingbats",
          "MacOSRoman": "MacOSRoman"}
WIN_ANSI_UNUSED = [0o177, 0o201, 0o215, 0o217, 0o220, 0o235]
MAC_OS_EXTRAS = {0o255: "notequal", 0o260: "infinity", 0o262: "lessequal", 0o263: "greaterequal", 0o266: "partialdiff",
                 0o267: "summation", 0o270: "product", 0o271: "pi", 0o272: "integral", 0o275: "Omega",
                 0o303: "radical", 0o305: "approxequal", 0o306: "Delta", 0o327: "lozenge", 0o360: "apple"}
NAME = re.compile(r"[A-Za-z0-9._]+")


class SourceError(ValueError):
    pass


def check_sums(agl_dir: Path) -> None:
    sums = {}
    for line in (agl_dir / "README.md").read_text(encoding="utf-8").splitlines():
        m = re.fullmatch(r"\| `([^`]+)` \| `([0-9a-f]{64})` \|", line.strip())
        if m:
            sums[m.group(1)] = m.group(2)
    if set(sums) != {"glyphlist.txt", "zapfdingbats.txt"}:
        raise SourceError(f"{agl_dir / 'README.md'} lists {sorted(sums)}, not glyphlist.txt and zapfdingbats.txt")
    for file, want in sums.items():
        got = hashlib.sha256((agl_dir / file).read_bytes()).hexdigest()
        if got != want:
            raise SourceError(f"{file}: SHA-256 {got} differs from README.md's {want}")


def read_list(path: Path) -> list[tuple[str, list[int]]]:
    """A Glyph List file: `name;XXXX[ XXXX...]` rows after `#` comments."""
    rows = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        name, _, scalars = line.partition(";")
        if not NAME.fullmatch(name) or not re.fullmatch(r"[0-9A-F]{4,6}( [0-9A-F]{4,6})*", scalars):
            raise SourceError(f"{path.name}:{number}: not a `name;XXXX` row: {line!r}")
        values = [int(s, 16) for s in scalars.split()]
        if any(0xD800 <= v <= 0xDFFF or v > 0x10FFFF for v in values):
            raise SourceError(f"{path.name}:{number}: not a Unicode scalar: {line!r}")
        rows.append((name, values))
    names = [n for n, _ in rows]
    if len(set(names)) != len(names):
        raise SourceError(f"{path.name}: a glyph name repeats")
    return rows


def read_annex_d(path: Path) -> dict[str, dict[int, str]]:
    tables: dict[str, dict[int, str]] = {e: {} for e in ENCODINGS}
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        parts = line.split(" ")
        if len(parts) != 3 or parts[0] not in tables or not re.fullmatch(r"[0-3][0-7]{2}", parts[1]) or \
                not NAME.fullmatch(parts[2]):
            raise SourceError(f"{path.name}:{number}: not `encoding code name`: {line!r}")
        code = int(parts[1], 8)
        if code in tables[parts[0]]:
            raise SourceError(f"{path.name}:{number}: {parts[0]} code {parts[1]} twice")
        tables[parts[0]][code] = parts[2]
    return tables


def afm_codes(path: Path) -> dict[int, str]:
    text = path.read_bytes().decode("latin-1")
    return {int(c): n for c, n in re.findall(r"^C (\d+) ; WX \S+ ; N (\S+) ;", text, re.M)}


def check_annex_d(tables: dict[str, dict[int, str]], afm_dir: Path, names: set[str]) -> None:
    def same(enc: str, afm: str) -> None:
        if tables[enc] != afm_codes(afm_dir / afm):
            raise SourceError(f"annex-d.txt: {enc} differs from the codes of {afm}")

    same("StandardEncoding", "Times-Roman.afm")
    same("Symbol", "Symbol.afm")
    same("ZapfDingbats", "ZapfDingbats.afm")
    win, mac, mac_os = tables["WinAnsiEncoding"], tables["MacRomanEncoding"], tables["MacOSRoman"]
    if win.get(0o240) != "space" or win.get(0o255) != "hyphen":
        raise SourceError("annex-d.txt: WinAnsiEncoding must have space also at 0240 and hyphen also at 0255")
    if any(win.get(code) != "bullet" for code in WIN_ANSI_UNUSED):
        raise SourceError("annex-d.txt: WinAnsiEncoding must map its unused codes above 040 to bullet")
    if mac.get(0o312) != "space" or len(mac) != 208:
        raise SourceError("annex-d.txt: MacRomanEncoding must have 208 codes, space also at 0312")
    if mac_os != {**mac, **MAC_OS_EXTRAS}:
        raise SourceError("annex-d.txt: MacOSRoman must be MacRomanEncoding and the fifteen Mac OS characters")
    for enc, table in tables.items():
        unknown = sorted(n for n in table.values() if n not in names)
        if unknown:
            raise SourceError(f"annex-d.txt: {enc} names glyphs neither list has: {unknown[:5]}")


def read_mac_names(path: Path) -> list[str]:
    names = [l for l in path.read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    if len(names) != 258 or len(set(names)) != 258 or names[0] != ".notdef" or \
            not all(NAME.fullmatch(n) for n in names):
        raise SourceError(f"{path.name}: not 258 unique glyph names starting .notdef")
    return names


def render(tables: dict[str, dict[int, str]], agl: list[tuple[str, list[int]]],
           zapf: list[tuple[str, list[int]]], mac_names: list[str]) -> str:
    out = [
        "// Generated by scripts/build-glyph-names.py from src/pdf/pdf-encodings/annex-d.txt and src/pdf/adobe-glyph-list",
        "// and src/pdf/pdf-encodings/mac-glyph-names.txt (D01 T02 section 28): do not edit. The Adobe Glyph List and ZapfDingbats list are Copyright 2002-2019 Adobe,",
        "// BSD-3-Clause; the notice is the head of each list in src/pdf/adobe-glyph-list.",
        "",
    ]
    for enc in ENCODINGS:
        table = tables[enc]
        out.append(f"inline constexpr std::array<std::string_view, 256> kEncoding{SUFFIX[enc]} = {{")
        for row in range(0, 256, 8):
            out.append("\t" + " ".join(f'"{table.get(c, "")}",' for c in range(row, row + 8)))
        out.append("};")
        out.append("")
    flat: list[int] = []
    out.append("inline constexpr AglEntry kAglEntries[] = {")
    for name, values in agl:
        out.append(f'\t{{"{name}", {len(flat)}, {len(values)}}},')
        flat.extend(values)
    out.append("};")
    out.append("inline constexpr char32_t kAglScalars[] = {")
    for row in range(0, len(flat), 10):
        out.append("\t" + " ".join(f"0x{v:04X}," for v in flat[row : row + 10]))
    out.append("};")
    out.append("")
    out.append("inline constexpr std::array<std::string_view, 258> kMacGlyphNames = {")
    for row in range(0, 258, 8):
        out.append("\t" + " ".join(f'"{n}",' for n in mac_names[row : row + 8]))
    out.append("};")
    out.append("")
    out.append("inline constexpr ZapfEntry kZapfDingbatsEntries[] = {")
    for name, values in sorted(zapf, key=lambda r: r[0].encode("ascii")):
        out.append(f'\t{{"{name}", 0x{values[0]:04X}}},')
    out.append("};")
    return "\n".join(out) + "\n"


def generate(pdf_dir: Path, skip_sums: bool = False) -> str:
    agl_dir = pdf_dir / "adobe-glyph-list"
    if not skip_sums:
        check_sums(agl_dir)
    agl = read_list(agl_dir / "glyphlist.txt")
    zapf = read_list(agl_dir / "zapfdingbats.txt")
    if [n for n, _ in agl] != sorted((n for n, _ in agl), key=lambda s: s.encode("ascii")):
        raise SourceError("glyphlist.txt: names are not in byte order")
    if any(len(v) != 1 for _, v in zapf):
        raise SourceError("zapfdingbats.txt: a name maps to more than one scalar")
    tables = read_annex_d(pdf_dir / "pdf-encodings" / "annex-d.txt")
    check_annex_d(tables, pdf_dir / "adobe-core14-afm", {n for n, _ in agl} | {n for n, _ in zapf})
    return render(tables, agl, zapf, read_mac_names(pdf_dir / "pdf-encodings" / "mac-glyph-names.txt"))


def write_atomic(path: Path, text: str) -> None:
    tmp = path.with_name(path.name + ".tmp")
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)
    if path.read_text(encoding="utf-8") != text:
        raise OSError(f"{path}: read back differs from what was written")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true", help="exit 1 if the table is not what the sources give")
    parser.add_argument("--pdf-dir", type=Path, default=PDF_DIR, help=argparse.SUPPRESS)
    parser.add_argument("--out", type=Path, default=OUT_PATH, help=argparse.SUPPRESS)
    # Tests only: read edited copies whose sums no longer match, to prove each check and field reaches the table.
    parser.add_argument("--skip-sums", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        text = generate(args.pdf_dir, args.skip_sums)
    except (SourceError, OSError) as exc:
        print(f"build-glyph-names: {exc}", file=sys.stderr)
        return 1
    if args.check:
        current = args.out.read_text(encoding="utf-8") if args.out.exists() else None
        if current != text:
            print(f"build-glyph-names: {args.out} is out of date; run python scripts/build-glyph-names.py",
                  file=sys.stderr)
            return 1
        print(f"build-glyph-names: {args.out.name} is current")
        return 0
    write_atomic(args.out, text)
    print(f"build-glyph-names: wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
