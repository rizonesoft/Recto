"""Tests for design/tools/build_resources.py pure-stdlib TrueType/PNG/ICO and CLI behavior."""

from __future__ import annotations

import importlib.util
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
_SPEC = importlib.util.spec_from_file_location("build_resources", ROOT / "design" / "tools" / "build_resources.py")
assert _SPEC is not None and _SPEC.loader is not None
build_resources = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(build_resources)


CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def _sfnt_checksum(data: bytes) -> int:
    padded = data + b"\x00" * ((4 - (len(data) % 4)) % 4)
    return sum(struct.unpack(f">{len(padded) // 4}I", padded)) & 0xFFFFFFFF


def _pack_sfnt(tables: dict[bytes, bytes]) -> bytes:
    num = len(tables)
    entry_sel = num.bit_length() - 1
    search_range = (1 << entry_sel) * 16
    range_shift = num * 16 - search_range
    header = struct.pack(">IHHHH", 0x00010000, num, search_range, entry_sel, range_shift)
    offset = 12 + num * 16
    records: list[bytes] = []
    blobs: list[bytes] = []
    for tag in sorted(tables):
        blob = tables[tag]
        padded = blob + b"\x00" * ((4 - (len(blob) % 4)) % 4)
        records.append(struct.pack(">4sIII", tag, _sfnt_checksum(blob), offset, len(blob)))
        blobs.append(padded)
        offset += len(padded)
    return header + b"".join(records) + b"".join(blobs)


def _encode_simple_glyph(contours: list[list[tuple[int, int, bool]]]) -> bytes:
    xs = [x for contour in contours for x, _, _ in contour]
    ys = [y for contour in contours for _, y, _ in contour]
    xmin, ymin, xmax, ymax = min(xs), min(ys), max(xs), max(ys)
    out = bytearray(struct.pack(">hhhhh", len(contours), xmin, ymin, xmax, ymax))
    idx = -1
    for contour in contours:
        idx += len(contour)
        out.extend(struct.pack(">H", idx))
    out.extend(struct.pack(">H", 0))  # instructionLength = 0
    pts = [pt for contour in contours for pt in contour]
    for _, _, on in pts:
        out.append(1 if on else 0)
    cx = 0
    for x, _, _ in pts:
        out.extend(struct.pack(">h", x - cx))
        cx = x
    cy = 0
    for _, y, _ in pts:
        out.extend(struct.pack(">h", y - cy))
        cy = y
    if len(out) % 2:
        out.append(0)
    return bytes(out)


def _encode_composite_glyph(components: list[tuple[int, int, int]], bounds: tuple[int, int, int, int]) -> bytes:
    xmin, ymin, xmax, ymax = bounds
    out = bytearray(struct.pack(">hhhhh", -1, xmin, ymin, xmax, ymax))
    for i, (gid, dx, dy) in enumerate(components):
        # ARG_1_AND_2_ARE_WORDS (1) | ARGS_ARE_XY_VALUES (2) | MORE_COMPONENTS (32 if not last)
        flags = 0x0001 | 0x0002 | (0x0020 if i + 1 < len(components) else 0)
        out.extend(struct.pack(">HHhh", flags, gid, dx, dy))
    return bytes(out)


def _encode_gvar_packed_points(points: list[int]) -> bytes:
    if not points:
        return b"\x00"
    n = len(points)
    header = bytes([n]) if n < 128 else bytes([0x80 | (n >> 8), n & 0xFF])
    # Single word run for clarity
    run = bytearray([0x80 | (n - 1)])
    prev = 0
    for p in points:
        run.extend(struct.pack(">H", p - prev))
        prev = p
    return header + bytes(run)


def _encode_gvar_packed_deltas(deltas: list[int]) -> bytes:
    # Single i16 run (0x40 flag) for clarity
    n = len(deltas)
    out = bytearray([0x40 | (n - 1)])
    for d in deltas:
        out.extend(struct.pack(">h", d))
    return bytes(out)


def _build_synthetic_variable_ttf() -> bytes:
    """Build a valid OpenType TrueType font covering simple, quad, composite, Format 12 cmap, fvar/avar/gvar/HVAR."""
    # Glyphs: 0=.notdef (empty), 1='R'/'e'/'t'/'o'/'P'/'F', 2='c'/'D' (quad), 3='A' (composite of 1+2), 4=U+1F600
    g1 = _encode_simple_glyph([[(0, 0, True), (0, 700, True), (400, 700, True), (400, 0, True)]])
    g2 = _encode_simple_glyph([[(0, 0, True), (200, 700, False), (400, 0, True)]])
    g3 = _encode_composite_glyph([(1, 0, 0), (2, 100, 50)], (0, 0, 500, 750))
    # Glyph 4 uses touched points 0 and 2 so points 1 and 3 exercise IUP interpolation during gvar deltas!
    g4 = _encode_simple_glyph([[(0, 0, True), (0, 700, True), (400, 700, True), (400, 0, True)]])
    glyph_blobs = [b"", g1, g2, g3, g4]
    num_glyphs = len(glyph_blobs)

    glyf = bytearray()
    loca = bytearray()
    for blob in glyph_blobs:
        loca.extend(struct.pack(">I", len(glyf)))
        glyf.extend(blob)
    loca.extend(struct.pack(">I", len(glyf)))

    head = bytearray(54)
    struct.pack_into(">IIHH", head, 0, 0x00010000, 0x00010000, 0, 0)
    struct.pack_into(">IHH", head, 12, 0x5F0F3CF5, 0, 1000)  # magicNumber, flags, unitsPerEm=1000
    struct.pack_into(">hhhh", head, 36, 0, 0, 500, 750)
    struct.pack_into(">hh", head, 50, 1, 0)  # indexToLocFormat=1 (long)

    hhea = bytearray(36)
    struct.pack_into(">Ihhh", hhea, 0, 0x00010000, 800, -200, 0)
    struct.pack_into(">H", hhea, 34, num_glyphs)

    maxp = struct.pack(">IH", 0x00010000, num_glyphs) + b"\x00" * 26

    os2 = bytearray(96)
    struct.pack_into(">Hh", os2, 0, 2, 500)
    struct.pack_into(">h", os2, 88, 700)  # sCapHeight = 700

    hmtx = b"".join(struct.pack(">Hh", adv, 0) for adv in (500, 500, 480, 600, 520))

    # cmap Format 4 (BMP) + Format 12 (non-BMP U+1F600 -> glyph 4)
    # BMP mappings: P/F/R/e/t/o -> 1, D/c -> 2, A -> 3
    bmp = {ord("A"): 3, ord("D"): 2, ord("F"): 1, ord("P"): 1, ord("R"): 1, ord("c"): 2, ord("e"): 1, ord("o"): 1, ord("t"): 1}
    codes = sorted(bmp)
    seg_count = len(codes) + 1
    end_codes = codes + [0xFFFF]
    start_codes = codes + [0xFFFF]
    id_deltas = [((bmp[c] - c) & 0xFFFF) for c in codes] + [1]
    id_offsets = [0] * seg_count
    fmt4_body = bytearray()
    fmt4_body.extend(struct.pack(">HHHH", seg_count * 2, 2, 0, 0))
    for v in end_codes:
        fmt4_body.extend(struct.pack(">H", v))
    fmt4_body.extend(struct.pack(">H", 0))
    for v in start_codes:
        fmt4_body.extend(struct.pack(">H", v))
    for v in id_deltas:
        fmt4_body.extend(struct.pack(">H", v))
    for v in id_offsets:
        fmt4_body.extend(struct.pack(">H", v))
    fmt4 = struct.pack(">HHH", 4, 6 + len(fmt4_body), 0) + bytes(fmt4_body)

    fmt12 = struct.pack(">HHIII", 12, 0, 16 + 12, 0, 1) + struct.pack(">III", 0x1F600, 0x1F600, 4)

    cmap_hdr = struct.pack(">HH", 0, 2)
    off4 = 4 + 16
    off12 = off4 + len(fmt4)
    cmap = (
        cmap_hdr
        + struct.pack(">HHI", 3, 1, off4)
        + struct.pack(">HHI", 3, 10, off12)
        + fmt4
        + fmt12
    )

    # fvar: 2 axes ('wght' 400..500..700, 'opsz' 12..14..72)
    fvar_axes = (
        struct.pack(">4siiiHH", b"wght", 400 << 16, 500 << 16, 700 << 16, 0, 256)
        + struct.pack(">4siiiHH", b"opsz", 12 << 16, 14 << 16, 72 << 16, 0, 257)
    )
    fvar = struct.pack(">HHIIIIH", 1, 0, 16, 2, 2, 20, 0) + b"\x00\x00" + fvar_axes
    fvar = (
        struct.pack(">HH", 1, 0)
        + struct.pack(">HHHHHH", 16, 2, 2, 20, 0, 24)
        + fvar_axes
    )

    # avar: identity map for axis 0, non-linear segment on axis 1 (0.5 -> 0.25)
    def _avar_seg(pairs: list[tuple[int, int]]) -> bytes:
        return struct.pack(">H", len(pairs)) + b"".join(struct.pack(">hh", a, b) for a, b in pairs)

    avar = (
        struct.pack(">HHHH", 1, 0, 0, 2)
        + _avar_seg([(-16384, -16384), (0, 0), (16384, 16384)])
        + _avar_seg([(-16384, -16384), (0, 0), (8192, 4096), (16384, 16384)])
    )

    # gvar: apply delta +100 X at opsz=max (tuple=(0.0, 1.0)) on glyph 4 points 0 and 2 (points 1, 3 IUP'd!)
    # Note pointIndices includes all 4 glyph points + 4 phantom points: [0, 2]
    gvar_payload = (
        _encode_gvar_packed_points([0, 2])
        + _encode_gvar_packed_deltas([100, 200])  # dx for pt 0 and pt 2
        + _encode_gvar_packed_deltas([20, 40])    # dy for pt 0 and pt 2
    )
    # VariationDataHeader tupleVariationCount=1, dataOffset=8 (4 hdr + 4 tupleHeader with peak=4B)
    # peakTuple for axis 0=0, axis 1=1.0 (16384)
    t_hdr = struct.pack(">HHhh", len(gvar_payload), 0xA000, 0, 16384)
    g4_gvar = struct.pack(">HH", 1, 4 + len(t_hdr)) + t_hdr + gvar_payload

    # Build gvar offsets for num_glyphs + 1 (glyphs 0..3 empty, glyph 4 has g4_gvar)
    gvar_data_blobs = [b"", b"", b"", b"", g4_gvar]
    gvar_stream = bytearray()
    gvar_offsets: list[int] = []
    for b in gvar_data_blobs:
        gvar_offsets.append(len(gvar_stream))
        gvar_stream.extend(b)
    gvar_offsets.append(len(gvar_stream))
    gvar_header_len = 20
    gvar_offsets_len = len(gvar_offsets) * 4
    gvar_hdr = struct.pack(
        ">HHHHIHHI",
        1, 0, 2, 0,
        gvar_header_len + gvar_offsets_len,
        num_glyphs,
        1,  # flags=1 -> 32-bit offsets directly
        gvar_header_len + gvar_offsets_len,
    )
    gvar = gvar_hdr + b"".join(struct.pack(">I", o) for o in gvar_offsets) + bytes(gvar_stream)

    # HVAR: VarStore adding +60 advance width when opsz=max for glyph 1 (outer=0, inner=1) and +120 for glyph 4
    # VarRegionList: 1 region with 2 axes: axis0=(-1,0,1), axis1=(0,1,1)
    reg_list = struct.pack(">HH", 2, 1) + struct.pack(">hhh", -16384, 0, 16384) + struct.pack(">hhh", 0, 16384, 16384)
    # VarData: itemCount=3, shortDeltaCount=1, regionIndexCount=1, regions=[0], deltas=[0, 60, 120]
    var_data = struct.pack(">HHHH", 3, 1, 1, 0) + struct.pack(">hhh", 0, 60, 120)
    # VarStore format=1, regListOff=12, varDataCount=1, varDataOffs=[12 + len(reg_list)]
    var_store = struct.pack(">HIH", 1, 12, 1) + struct.pack(">I", 12 + len(reg_list)) + reg_list + var_data
    # AdvanceWidthMap: entryFormat=0x03 (4-bit inner mask, 1B entries): gids 0/2/3->0 (+0), gid 1->1 (+60), gid 4->2 (+120)
    adv_map = struct.pack(">BBH", 0, 0x03, num_glyphs) + bytes([0, 1, 0, 0, 2])
    hvar_store_off = 20
    hvar_map_off = hvar_store_off + len(var_store)
    hvar = struct.pack(">HHIIII", 1, 0, hvar_store_off, hvar_map_off, 0, 0) + var_store + adv_map

    return _pack_sfnt({
        b"head": bytes(head),
        b"hhea": bytes(hhea),
        b"maxp": maxp,
        b"OS/2": bytes(os2),
        b"hmtx": hmtx,
        b"cmap": cmap,
        b"loca": bytes(loca),
        b"glyf": bytes(glyf),
        b"fvar": fvar,
        b"avar": avar,
        b"gvar": gvar,
        b"HVAR": hvar,
    })


def _make_filtered_png(width: int, height: int, rgba: bytes, row_filters: list[int],
                       color_type: int = 6, bit_depth: int = 8,
                       palette: bytes = b"", trns: bytes = b"") -> bytes:
    """Build a test PNG using caller-chosen per-row filters (0..4) to exercise every read_png branch."""
    ihdr = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, 0)
    raw_rows: list[bytes] = []
    if color_type == 6 and bit_depth == 8:
        bpp, stride = 4, width * 4
        for y in range(height):
            raw_rows.append(rgba[y * stride : (y + 1) * stride])
    elif color_type == 6 and bit_depth == 16:
        bpp, stride = 8, width * 8
        for y in range(height):
            row = bytearray()
            for b in rgba[y * width * 4 : (y + 1) * width * 4]:
                row.extend(struct.pack(">H", (b << 8) | b))
            raw_rows.append(bytes(row))
    elif color_type == 2 and bit_depth == 8:
        bpp, stride = 3, width * 3
        for y in range(height):
            row = bytearray()
            for x in range(width):
                o = (y * width + x) * 4
                row.extend(rgba[o : o + 3])
            raw_rows.append(bytes(row))
    elif color_type == 0 and bit_depth == 8:
        bpp, stride = 1, width
        for y in range(height):
            raw_rows.append(bytes(rgba[(y * width + x) * 4] for x in range(width)))
    elif color_type == 4 and bit_depth == 8:
        bpp, stride = 2, width * 2
        for y in range(height):
            row = bytearray()
            for x in range(width):
                o = (y * width + x) * 4
                row.extend((rgba[o], rgba[o + 3]))
            raw_rows.append(bytes(row))
    elif color_type == 3 and bit_depth == 8:
        bpp, stride = 1, width
        for y in range(height):
            raw_rows.append(bytes(rgba[(y * width + x) * 4] for x in range(width)))
    else:
        raise ValueError("Unsupported test parameters")

    def paeth(a: int, b: int, c: int) -> int:
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        if pa <= pb and pa <= pc:
            return a
        return b if pb <= pc else c

    filtered = bytearray()
    prev = bytes(stride)
    for y, cur in enumerate(raw_rows):
        flt = row_filters[y % len(row_filters)]
        filtered.append(flt)
        for i in range(stride):
            left = cur[i - bpp] if i >= bpp else 0
            up = prev[i]
            ul = prev[i - bpp] if i >= bpp else 0
            if flt == 0:
                val = cur[i]
            elif flt == 1:
                val = (cur[i] - left) & 0xFF
            elif flt == 2:
                val = (cur[i] - up) & 0xFF
            elif flt == 3:
                val = (cur[i] - ((left + up) >> 1)) & 0xFF
            elif flt == 4:
                val = (cur[i] - paeth(left, up, ul)) & 0xFF
            else:
                raise ValueError(f"bad filter {flt}")
            filtered.append(val)
        prev = cur

    parts = [build_resources._PNG_SIGNATURE, build_resources._png_chunk(b"IHDR", ihdr)]
    if palette:
        parts.append(build_resources._png_chunk(b"PLTE", palette))
    if trns:
        parts.append(build_resources._png_chunk(b"tRNS", trns))
    parts.append(build_resources._png_chunk(b"IDAT", zlib.compress(bytes(filtered))))
    parts.append(build_resources._png_chunk(b"IEND", b""))
    return b"".join(parts)


class TrueTypeOutlineTests(unittest.TestCase):
    def test_synthetic_variable_font_outlines_avar_gvar_iup_hvar_and_non_bmp(self):
        ttf_bytes = _build_synthetic_variable_ttf()
        with tempfile.TemporaryDirectory() as tmp:
            font_path = Path(tmp) / "VarTest.ttf"
            font_path.write_bytes(ttf_bytes)
            font = build_resources.TrueTypeFont(font_path)

            # Verify avar piecewise normalization: opsz=43 is halfway between 14 and 72 (raw +0.5 -> avar +0.25)
            norm = font.normalize_location({"wght": 500, "opsz": 43})
            self.assertAlmostEqual(norm[0], 0.0, places=4)
            self.assertAlmostEqual(norm[1], 0.25, places=4)

            # At opsz=72 (norm=(0, 1)), glyph 4 gets +100/+200/+20/+40 deltas at pts 0,2 and IUP interpolates pts 1,3
            d, x0, y0, x1, y1, cap = build_resources.outline_text(font_path, "\U0001F600", {"wght": 500, "opsz": 72})
            self.assertEqual(cap, 700)
            self.assertAlmostEqual(x0, 100.0, places=3)
            self.assertAlmostEqual(x1, 600.0, places=3)
            # y0 is top (-yMax = -(700 + 40)), y1 is bottom (-yMin = -20)
            self.assertAlmostEqual(y0, -740.0, places=3)
            self.assertAlmostEqual(y1, -20.0, places=3)
            self.assertIn("M100 -20V-740H600V-20Z", d)

            # Composite glyph 'A' combines glyph 1 and shifted glyph 2 (with quadratic curve Q)
            comp_d, cx0, cy0, cx1, cy1, _ = build_resources.outline_text(font_path, "A")
            self.assertIn("Q300 -750 500 -50", comp_d)
            self.assertAlmostEqual(cx0, 0.0)
            self.assertAlmostEqual(cx1, 500.0)
            self.assertAlmostEqual(cy0, -700.0)
            self.assertAlmostEqual(cy1, 0.0)

            # TextOutliner width uses HVAR advance width delta (+60 for glyph 1 at opsz=72)
            outliner = build_resources.TextOutliner(font_path, font_path, font_path)
            w_default = outliner.width("serif", "R", 100.0)
            w_opsz72 = outliner.width("serif", "R", 100.0, location={"opsz": 72})
            self.assertAlmostEqual(w_default, 50.0, places=3)
            self.assertAlmostEqual(w_opsz72, 56.0, places=3)

            svg_el, px_adv = outliner.path("serif", "RA", 100.0, 10.0, 90.0, "#112233",
                                           location={"opsz": 72}, tracking_em=0.1)
            self.assertIn('transform="translate(10 90) scale(0.1)"', svg_el)
            self.assertAlmostEqual(px_adv, (560 + 100 + 600 + 100) * 0.1, places=3)

    def test_windows_segoeuib_matches_checked_in_filetype_pdf_svg(self):
        if sys.platform != "win32":
            return
        segoeuib = Path(r"C:\Windows\Fonts\segoeuib.ttf")
        self.assertTrue(segoeuib.exists(), f"Expected Windows label font at {segoeuib}")
        checked_in = (ROOT / "resources" / "svg" / "filetype-pdf.svg").read_text(encoding="utf-8")
        match = re.search(r'<path transform="(translate\([^"]+\) scale\([^"]+\))" d="([^"]+)" fill="#FFFFFF"/>',
                          checked_in)
        self.assertIsNotNone(match, checked_in)
        assert match is not None
        lb_d, lx0, _, lx1, _, lcap = build_resources.outline_text(segoeuib, "PDF", tracking_em=0.02)
        ls = 8.2 / lcap
        actual_transform = f"translate({7 + (31 - (lx1 - lx0) * ls) / 2 - lx0 * ls:g} {43 + (14 + 8.2) / 2:g}) scale({ls:g})"
        self.assertEqual(actual_transform, match.group(1))
        tok_re = re.compile(r"[A-Za-z]|[-+]?\d+(?:\.\d+)?")
        actual_tokens = tok_re.findall(lb_d)
        expected_tokens = tok_re.findall(match.group(2))
        self.assertEqual(len(actual_tokens), len(expected_tokens))
        for actual_tok, expected_tok in zip(actual_tokens, expected_tokens):
            if actual_tok.isalpha() or expected_tok.isalpha():
                self.assertEqual(actual_tok, expected_tok)
            else:
                self.assertAlmostEqual(float(actual_tok), float(expected_tok), places=5)

    def test_windows_variable_font_axes_change_outline_and_advance(self):
        if sys.platform != "win32":
            return
        seguivar = Path(r"C:\Windows\Fonts\SegUIVar.ttf")
        self.assertTrue(seguivar.exists(), f"Expected Windows variable font at {seguivar}")
        light_d, lx0, _, lx1, _, _ = build_resources.outline_text(
            seguivar, "Recto", {"wght": 300, "opsz": 12}
        )
        bold_d, bx0, _, bx1, _, _ = build_resources.outline_text(
            seguivar, "Recto", {"wght": 700, "opsz": 36}
        )
        self.assertTrue(light_d and bold_d)
        self.assertNotEqual(light_d, bold_d)
        self.assertGreater(bx1 - bx0, lx1 - lx0)


class StdlibPngAndIcoTests(unittest.TestCase):
    def test_png_roundtrip_crop_and_all_filters(self):
        w, h = 5, 6
        rgba = bytearray()
        for y in range(h):
            for x in range(w):
                rgba.extend(((x * 41 + y * 13) & 0xFF, (x * 17 + y * 53) & 0xFF, (x * 7 + y * 29) & 0xFF, (128 + x * 20) & 0xFF))
        raw_rgba = bytes(rgba)

        # Round-trip through write_png -> read_png -> crop_rgba
        encoded = build_resources.write_png(w, h, raw_rgba)
        dw, dh, decoded = build_resources.read_png(encoded)
        self.assertEqual((dw, dh), (w, h))
        self.assertEqual(decoded, raw_rgba)

        cropped = build_resources.crop_rgba(w, h, decoded, 1, 2, 3, 2)
        expected_crop = bytearray()
        for cy in range(2, 4):
            for cx in range(1, 4):
                o = (cy * w + cx) * 4
                expected_crop.extend(raw_rgba[o : o + 4])
        self.assertEqual(cropped, bytes(expected_crop))
        with self.assertRaises(ValueError):
            build_resources.crop_rgba(w, h, decoded, 3, 0, 3, 1)

        # Exercise all 5 scanline filters (0..4)
        filtered = _make_filtered_png(w, h, raw_rgba, [0, 1, 2, 3, 4], color_type=6, bit_depth=8)
        self.assertEqual(build_resources.read_png(filtered), (w, h, raw_rgba))

        # 16-bit RGBA
        rgba16 = _make_filtered_png(w, h, raw_rgba, [4, 1], color_type=6, bit_depth=16)
        self.assertEqual(build_resources.read_png(rgba16), (w, h, raw_rgba))

    def test_png_color_types_rgb_grayscale_indexed_and_error_handling(self):
        # RGB + tRNS color key
        rgb_src = bytes([10, 20, 30, 255, 40, 50, 60, 255])
        rgb_png = _make_filtered_png(2, 1, rgb_src, [0], color_type=2, trns=struct.pack(">HHH", 10, 20, 30))
        self.assertEqual(build_resources.read_png(rgb_png), (2, 1, bytes([10, 20, 30, 0, 40, 50, 60, 255])))

        # Indexed PLTE + tRNS
        idx_src = bytes([0, 0, 0, 255, 1, 0, 0, 255])
        plte = bytes([100, 150, 200, 210, 220, 230])
        trns = bytes([128, 255])
        pal_png = _make_filtered_png(2, 1, idx_src, [0], color_type=3, palette=plte, trns=trns)
        self.assertEqual(build_resources.read_png(pal_png), (2, 1, bytes([100, 150, 200, 128, 210, 220, 230, 255])))

        # Grayscale and Grayscale+Alpha
        gray_png = _make_filtered_png(2, 1, bytes([77, 0, 0, 255, 99, 0, 0, 255]), [0], color_type=0)
        self.assertEqual(build_resources.read_png(gray_png), (2, 1, bytes([77, 77, 77, 255, 99, 99, 99, 255])))
        ga_png = _make_filtered_png(2, 1, bytes([77, 0, 0, 64, 99, 0, 0, 255]), [0], color_type=4)
        self.assertEqual(build_resources.read_png(ga_png), (2, 1, bytes([77, 77, 77, 64, 99, 99, 99, 255])))

        # Bad signature / bad CRC
        with self.assertRaises(ValueError):
            build_resources.read_png(b"not-a-png")
        bad_crc = bytearray(build_resources.write_png(1, 1, b"\x01\x02\x03\x04"))
        bad_crc[-1] ^= 0xFF
        with self.assertRaises(ValueError):
            build_resources.read_png(bytes(bad_crc))

    def test_pack_ico_and_atomic_write(self):
        p16 = build_resources.write_png(16, 16, b"\x11\x22\x33\xff" * (16 * 16))
        p256 = build_resources.write_png(256, 256, b"\x44\x55\x66\xff" * (256 * 256))
        ico = build_resources.pack_ico([(16, 16, p16), (256, 256, p256)])
        reserved, icon_type, count = struct.unpack_from("<HHH", ico, 0)
        self.assertEqual((reserved, icon_type, count), (0, 1, 2))
        e0 = struct.unpack_from("<BBBBHHII", ico, 6)
        e1 = struct.unpack_from("<BBBBHHII", ico, 22)
        self.assertEqual(e0, (16, 16, 0, 0, 0, 32, len(p16), 38))
        self.assertEqual(e1, (0, 0, 0, 0, 0, 32, len(p256), 38 + len(p16)))
        self.assertEqual(ico[38 : 38 + len(p16)], p16)
        self.assertEqual(ico[38 + len(p16) :], p256)

        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "sample.bin"
            target.write_bytes(b"original")
            with mock.patch.object(build_resources.os, "fsync", side_effect=OSError("disk full")):
                with self.assertRaises(OSError):
                    build_resources.write_bytes_atomically(target, b"partial")
            self.assertEqual(target.read_bytes(), b"original")
            self.assertEqual([p.name for p in Path(tmp).iterdir()], ["sample.bin"])


def _strip_system_python_from_path(raw_path: str) -> str:
    """Return PATH with every directory containing a Python launcher removed."""
    kept: list[str] = []
    names = ("python.exe", "python3.exe", "python.cmd", "python.bat", "py.exe", "python", "python3")
    for entry in raw_path.split(os.pathsep):
        if not entry:
            continue
        folder = Path(entry)
        try:
            has_py = any((folder / name).exists() for name in names)
        except OSError:
            has_py = False
        if not has_py:
            kept.append(entry)
    return os.pathsep.join(kept)


class BuildResourcesEndToEndTests(unittest.TestCase):
    def test_find_browser_respects_env_override_and_missing_override_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            fake_exe = Path(tmp) / "fake-browser.exe"
            fake_exe.write_bytes(b"")
            with mock.patch.dict(os.environ, {"RECTO_RENDER_BROWSER": str(fake_exe)}):
                self.assertEqual(build_resources.find_browser(), str(fake_exe))
            with mock.patch.dict(os.environ, {"RECTO_RENDER_BROWSER": str(Path(tmp) / "missing.exe")}):
                with self.assertRaises(SystemExit):
                    build_resources.find_browser()

    def _run_cli_build(self, work_dir: Path, out_arg: str, brand_arg: str) -> None:
        ttf_bytes = _build_synthetic_variable_ttf()
        font_path = work_dir / "SynthVar.ttf"
        font_path.write_bytes(ttf_bytes)

        helper_py = work_dir / "fake_browser.py"
        helper_py.write_text(
            "\n".join([
                "import struct, sys, zlib",
                "from pathlib import Path",
                "shot = None",
                "size = (64, 64)",
                "for arg in sys.argv[1:]:",
                "    if arg.startswith('--screenshot='):",
                "        shot = Path(arg.split('=', 1)[1])",
                "    elif arg.startswith('--window-size='):",
                "        w, h = arg.split('=', 1)[1].split(',', 1)",
                "        size = (int(w), int(h))",
                "def chunk(tag, data):",
                "    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)",
                "w, h = size",
                "ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)",
                "row = b'\\x00' + (b'\\x10\\x20\\x30\\xff' * w)",
                "idat = zlib.compress(row * h, level=1)",
                "png = b'\\x89PNG\\r\\n\\x1a\\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', idat) + chunk(b'IEND', b'')",
                "shot.parent.mkdir(parents=True, exist_ok=True)",
                "shot.write_bytes(png)",
            ]) + "\n",
            encoding="utf-8",
        )

        if os.name == "nt":
            launcher = ROOT / "build" / "python.cmd"
            self.assertTrue(launcher.exists())
            browser_shim = work_dir / "fake-browser.cmd"
            browser_shim.write_text(
                f'@echo off\r\n"{launcher}" "{helper_py}" %*\r\n',
                encoding="utf-8",
            )
            cleaned_path = _strip_system_python_from_path(os.environ.get("PATH", ""))
            env = {
                "SystemRoot": os.environ.get("SystemRoot", r"C:\Windows"),
                "COMSPEC": os.environ.get("COMSPEC", r"C:\Windows\System32\cmd.exe"),
                "TEMP": str(work_dir),
                "TMP": str(work_dir),
                "PATH": f"{work_dir}{os.pathsep}{cleaned_path}" if cleaned_path else str(work_dir),
                "RECTO_RENDER_BROWSER": str(browser_shim),
            }
            if "RECTO_TOOLS_DIR" in os.environ:
                env["RECTO_TOOLS_DIR"] = os.environ["RECTO_TOOLS_DIR"]
            cmd_prefix = [str(launcher)]
        else:
            launcher = ROOT / "scripts" / "run-python.sh"
            self.assertTrue(launcher.exists())
            browser_shim = work_dir / "fake-browser.sh"
            browser_shim.write_text(
                f'#!/bin/sh\nexec "{sys.executable}" "{helper_py}" "$@"\n',
                encoding="utf-8",
            )
            browser_shim.chmod(0o755)
            env = {
                "PATH": os.pathsep.join([str(work_dir), str(Path(sys.executable).parent), "/usr/local/bin", "/usr/bin", "/bin"]),
                "HOME": os.environ.get("HOME", str(work_dir)),
                "TMPDIR": str(work_dir),
                "TEMP": str(work_dir),
                "TMP": str(work_dir),
                "RECTO_RENDER_BROWSER": str(browser_shim),
            }
            cmd_prefix = ["/bin/sh", str(launcher)]

        proc = subprocess.run(
            cmd_prefix + [
                str(ROOT / "design" / "tools" / "build_resources.py"),
                "--newsreader", str(font_path),
                "--label-font", str(font_path),
                "--ui-font", str(font_path),
                "--out-dir", out_arg,
                "--brand-out-dir", brand_arg,
            ],
            cwd=str(work_dir),
            env=env,
            capture_output=True,
            text=True,
            timeout=120,
            creationflags=CREATE_NO_WINDOW,
        )
        self.assertEqual(proc.returncode, 0, f"stdout={proc.stdout}\nstderr={proc.stderr}")
        self.assertIn("SVG masters", proc.stdout)

        out_dir = (work_dir / out_arg).resolve()
        brand_dir = (work_dir / brand_arg).resolve()
        self.assertTrue((out_dir / "svg" / "wordmark-light.svg").exists())
        self.assertTrue((out_dir / "svg" / "filetype-pdf.svg").exists())
        self.assertTrue((out_dir / "ico" / "recto.ico").exists())
        self.assertTrue((out_dir / "ico" / "pdf-file.ico").exists())
        self.assertTrue((out_dir / "preview.png").exists())
        self.assertTrue((brand_dir / "github-banner.png").exists())
        self.assertTrue((out_dir / "installer" / "wizard-image-100.png").exists())
        self.assertTrue((out_dir / "msix" / "Assets" / "Square44x44Logo.targetsize-256.png").exists())

    def test_cli_runs_with_platform_launcher_and_without_third_party_packages(self):
        with tempfile.TemporaryDirectory() as tmp:
            work_dir = Path(tmp)
            self._run_cli_build(work_dir, str(work_dir / "resources"), str(work_dir / "brand"))

    def test_cli_accepts_relative_output_directories(self):
        with tempfile.TemporaryDirectory() as tmp:
            work_dir = Path(tmp)
            self._run_cli_build(work_dir, "rel_out/resources", "rel_out/brand")


if __name__ == "__main__":
    unittest.main()
