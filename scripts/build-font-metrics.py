#!/usr/bin/env python3
"""Generate src/pdf/standard14_metrics.inc from the Adobe Core14 AFM files (D01 T02 section 27).

The fourteen standard fonts (ISO 32000-1 section 9.6.2.2) need their widths and font boxes when a PDF does not embed
them. This reads the vendored AFM files (Adobe Technical Note 5004, AFM 4.1) in src/pdf/adobe-core14-afm, refuses
any whose SHA-256 differs from that directory's README, and writes one constexpr table per font: its font box, the
ascender, descender, cap height, italic angle and fixed pitch the AFM gives (a field the AFM lacks is none, never 0),
and per glyph its name, built-in code (-1 when unencoded), width and box, in AFM units of 1/1000 em. Glyphs are sorted
by name in byte order for lookup by name, and a 256-entry table gives each code's glyph.

    python scripts/build-font-metrics.py           # regenerate the table
    python scripts/build-font-metrics.py --check   # exit 1 if the table is not what the AFM files give
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
AFM_DIR = REPO_ROOT / "src" / "pdf" / "adobe-core14-afm"
OUT_PATH = REPO_ROOT / "src" / "pdf" / "standard14_metrics.inc"

# The fonts in the order of recto::pdf::Standard14Font, and each table's C++ suffix.
FONTS = [
    ("Courier", "Courier"),
    ("Courier-Bold", "CourierBold"),
    ("Courier-Oblique", "CourierOblique"),
    ("Courier-BoldOblique", "CourierBoldOblique"),
    ("Helvetica", "Helvetica"),
    ("Helvetica-Bold", "HelveticaBold"),
    ("Helvetica-Oblique", "HelveticaOblique"),
    ("Helvetica-BoldOblique", "HelveticaBoldOblique"),
    ("Times-Roman", "TimesRoman"),
    ("Times-Bold", "TimesBold"),
    ("Times-Italic", "TimesItalic"),
    ("Times-BoldItalic", "TimesBoldItalic"),
    ("Symbol", "Symbol"),
    ("ZapfDingbats", "ZapfDingbats"),
]

INT16 = (-32768, 32767)
NAME = re.compile(r"[A-Za-z0-9._]+")


class AfmError(ValueError):
    pass


@dataclass(frozen=True)
class Glyph:
    name: str
    code: int
    width: int
    box: tuple[int, int, int, int]


@dataclass(frozen=True)
class FontMetrics:
    name: str
    font_box: tuple[int, int, int, int]
    ascender: int | None
    descender: int | None
    cap_height: int | None
    italic_angle: str
    fixed_pitch: bool
    glyphs: list[Glyph]


def readme_sums(afm_dir: Path) -> dict[str, str]:
    """The `| `file` | `sha256` |` rows of the directory's README."""
    sums = {}
    for line in (afm_dir / "README.md").read_text(encoding="utf-8").splitlines():
        m = re.fullmatch(r"\| `([^`]+)` \| `([0-9a-f]{64})` \|", line.strip())
        if m:
            sums[m.group(1)] = m.group(2)
    return sums


def check_sums(afm_dir: Path) -> None:
    sums = readme_sums(afm_dir)
    expected = {f"{name}.afm" for name, _ in FONTS} | {"MustRead.html"}
    if set(sums) != expected:
        raise AfmError(f"README.md lists {sorted(sums)}, not the {len(expected)} files {sorted(expected)}")
    for file, want in sums.items():
        got = hashlib.sha256((afm_dir / file).read_bytes()).hexdigest()
        if got != want:
            raise AfmError(f"{file}: SHA-256 {got} differs from README.md's {want}")


def number(text: str, what: str) -> int:
    try:
        value = int(text)
    except ValueError:
        raise AfmError(f"{what} is not an integer: {text!r}") from None
    if not INT16[0] <= value <= INT16[1]:
        raise AfmError(f"{what} {value} does not fit 16 bits")
    return value


def parse_afm(path: Path, font_name: str) -> FontMetrics:
    """One AFM file, read strictly: a field read twice, a missing required one or a malformed glyph record refuses."""
    lines = path.read_bytes().decode("latin-1").replace("\r\n", "\n").split("\n")
    if not lines or lines[0].strip() != "StartFontMetrics 4.1":
        raise AfmError(f"{path.name}: does not start with StartFontMetrics 4.1")
    header: dict[str, str] = {}
    glyphs: list[Glyph] = []
    declared = None
    in_chars = False
    for raw in lines[1:]:
        line = raw.strip()
        if not line or line.startswith("Comment"):
            continue
        key, _, value = line.partition(" ")
        if in_chars:
            if line == "EndCharMetrics":
                in_chars = False
                continue
            fields = {}
            for part in line.split(";"):
                part = part.strip()
                if not part:
                    continue
                k, _, v = part.partition(" ")
                if k == "L":
                    continue  # ligatures: PDF text does not form them
                if k in fields:
                    raise AfmError(f"{path.name}: glyph record repeats {k}: {line}")
                fields[k] = v.strip()
            if set(fields) != {"C", "WX", "N", "B"}:
                raise AfmError(f"{path.name}: glyph record without exactly C, WX, N and B: {line}")
            name = fields["N"]
            if not NAME.fullmatch(name):
                raise AfmError(f"{path.name}: glyph name {name!r} is not a plain name")
            code = number(fields["C"], f"{path.name} {name} code")
            if not -1 <= code <= 255:
                raise AfmError(f"{path.name}: {name} code {code} is outside -1 to 255")
            box = fields["B"].split()
            if len(box) != 4:
                raise AfmError(f"{path.name}: {name} box is not four numbers")
            glyphs.append(Glyph(name, code, number(fields["WX"], f"{path.name} {name} width"),
                                tuple(number(b, f"{path.name} {name} box") for b in box)))
            continue
        if key == "StartCharMetrics":
            if declared is not None:
                raise AfmError(f"{path.name}: StartCharMetrics twice")
            declared = number(value, f"{path.name} StartCharMetrics")
            in_chars = True
        elif key in ("FontName", "FontBBox", "Ascender", "Descender", "CapHeight", "ItalicAngle", "IsFixedPitch"):
            if key in header:
                raise AfmError(f"{path.name}: {key} twice")
            header[key] = value.strip()
    if declared is None or in_chars:
        raise AfmError(f"{path.name}: no complete StartCharMetrics ... EndCharMetrics block")
    if len(glyphs) != declared:
        raise AfmError(f"{path.name}: StartCharMetrics says {declared} glyphs, the block holds {len(glyphs)}")
    if header.get("FontName") != font_name:
        raise AfmError(f"{path.name}: FontName {header.get('FontName')!r} is not {font_name!r}")
    for required in ("FontBBox", "ItalicAngle", "IsFixedPitch"):
        if required not in header:
            raise AfmError(f"{path.name}: no {required}")
    font_box = header["FontBBox"].split()
    if len(font_box) != 4:
        raise AfmError(f"{path.name}: FontBBox is not four numbers")
    if not re.fullmatch(r"-?\d+(\.\d+)?", header["ItalicAngle"]):
        raise AfmError(f"{path.name}: ItalicAngle {header['ItalicAngle']!r} is not a number")
    if header["IsFixedPitch"] not in ("true", "false"):
        raise AfmError(f"{path.name}: IsFixedPitch is neither true nor false")
    names = [g.name for g in glyphs]
    if len(set(names)) != len(names):
        raise AfmError(f"{path.name}: a glyph name repeats")
    codes = [g.code for g in glyphs if g.code >= 0]
    if len(set(codes)) != len(codes):
        raise AfmError(f"{path.name}: a code repeats")

    def optional(key: str) -> int | None:
        return number(header[key], f"{path.name} {key}") if key in header else None

    angle = header["ItalicAngle"]
    return FontMetrics(
        name=font_name,
        font_box=tuple(number(b, f"{path.name} FontBBox") for b in font_box),
        ascender=optional("Ascender"),
        descender=optional("Descender"),
        cap_height=optional("CapHeight"),
        italic_angle=angle if "." in angle else angle + ".0",
        fixed_pitch=header["IsFixedPitch"] == "true",
        glyphs=sorted(glyphs, key=lambda g: g.name.encode("ascii")),
    )


def render(fonts: list[FontMetrics]) -> str:
    def opt(v: int | None) -> str:
        return "std::nullopt" if v is None else f"int16_t{{{v}}}"

    def box(b: tuple[int, int, int, int]) -> str:
        return "{" + ", ".join(str(x) for x in b) + "}"

    out = [
        "// Generated by scripts/build-font-metrics.py from src/pdf/adobe-core14-afm (D01 T02 section 27): do not edit.",
        "// Per font, its glyphs sorted by name in byte order, each {name, built-in code or -1, width, {llx, lly, urx,",
        "// ury}} in AFM units of 1/1000 em; the index of each code's glyph, or -1; then the fourteen fonts in the order of",
        "// Standard14Font. Adobe's notice: src/pdf/adobe-core14-afm/MustRead.html.",
        "",
    ]
    for metrics, (_, suffix) in zip(fonts, FONTS):
        out.append(f"inline constexpr Standard14Glyph kGlyphs{suffix}[] = {{")
        for g in metrics.glyphs:
            out.append(f'\t{{"{g.name}", {g.code}, {g.width}, {box(g.box)}}},')
        out.append("};")
        index = [-1] * 256
        for i, g in enumerate(metrics.glyphs):
            if g.code >= 0:
                index[g.code] = i
        out.append(f"inline constexpr std::array<int16_t, 256> kCodes{suffix} = {{")
        for row in range(0, 256, 16):
            out.append("\t" + ", ".join(str(v) for v in index[row : row + 16]) + ",")
        out.append("};")
        out.append("")
    out.append("inline constexpr std::array<Standard14Metrics, 14> kStandard14Metrics = {{")
    for metrics, (_, suffix) in zip(fonts, FONTS):
        out.append(
            f'\t{{"{metrics.name}", {box(metrics.font_box)}, {opt(metrics.ascender)}, {opt(metrics.descender)}, '
            f"{opt(metrics.cap_height)}, {metrics.italic_angle}, {'true' if metrics.fixed_pitch else 'false'}, "
            f"kGlyphs{suffix}, kCodes{suffix}}},"
        )
    out.append("}};")
    return "\n".join(out) + "\n"


def generate(afm_dir: Path, skip_sums: bool = False) -> str:
    if not skip_sums:
        check_sums(afm_dir)
    return render([parse_afm(afm_dir / f"{name}.afm", name) for name, _ in FONTS])


def write_atomic(path: Path, text: str) -> None:
    tmp = path.with_name(path.name + ".tmp")
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)
    if path.read_text(encoding="utf-8") != text:
        raise OSError(f"{path}: read back differs from what was written")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true", help="exit 1 if the table is not what the AFM files give")
    parser.add_argument("--afm-dir", type=Path, default=AFM_DIR, help=argparse.SUPPRESS)
    parser.add_argument("--out", type=Path, default=OUT_PATH, help=argparse.SUPPRESS)
    # Tests only: read edited copies whose sums no longer match, to prove each field reaches the table.
    parser.add_argument("--skip-sums", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        text = generate(args.afm_dir, args.skip_sums)
    except (AfmError, OSError) as exc:
        print(f"build-font-metrics: {exc}", file=sys.stderr)
        return 1
    if args.check:
        current = args.out.read_text(encoding="utf-8") if args.out.exists() else None
        if current != text:
            print(f"build-font-metrics: {args.out} is out of date; run python scripts/build-font-metrics.py",
                  file=sys.stderr)
            return 1
        print(f"build-font-metrics: {args.out.name} is current ({len(FONTS)} fonts)")
        return 0
    write_atomic(args.out, text)
    print(f"build-font-metrics: wrote {args.out} ({len(FONTS)} fonts)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
