#!/usr/bin/env python3
"""Deterministic PDF and OpenXML test corpus generator for Recto.

Uses Python standard library only. Every generated file uses fixed timestamps,
deterministic document IDs, and temp-file-then-replace atomic writes in the
destination folder.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import zipfile
import zlib

REPO_ROOT = Path(__file__).resolve().parent.parent
CORPUS_ROOT = REPO_ROOT / "tests" / "corpus"
FIXED_PDF_DATE = "D:20261013000000Z"
FIXED_ZIP_DT = (2026, 10, 13, 0, 0, 0)
MAX_CORPUS_TOTAL_BYTES = 5_000_000


def write_atomic(dest: Path, data: bytes) -> None:
    """Write bytes to a sibling temporary file and atomically replace dest."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(
        prefix=".recto-corpus-", suffix=".tmp", dir=str(dest.parent)
    )
    tmp_path = Path(tmp_name)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(tmp_path, dest)
    except BaseException:
        if tmp_path.exists():
            try:
                tmp_path.unlink()
            except OSError:
                pass
        raise


def deterministic_id_hex(rel_path: str, rev: int = 0) -> tuple[str, str]:
    h0 = hashlib.sha256(f"recto-corpus:{rel_path}:0".encode("utf-8")).digest()[:16].hex().upper()
    h1 = hashlib.sha256(f"recto-corpus:{rel_path}:{rev}".encode("utf-8")).digest()[:16].hex().upper()
    return h0, h1


def flate_compress(raw: bytes) -> bytes:
    comp = zlib.compressobj(level=9, method=zlib.DEFLATED, wbits=15)
    return comp.compress(raw) + comp.flush()


def flate_png_up_compress(raw: bytes, columns: int) -> bytes:
    """Apply PNG Up predictor (tag byte 2 per row) and zlib compress."""
    if len(raw) % columns != 0:
        raise ValueError(f"raw length {len(raw)} not divisible by columns {columns}")
    rows = len(raw) // columns
    out = bytearray()
    prev = bytes(columns)
    for r in range(rows):
        row = raw[r * columns : (r + 1) * columns]
        out.append(2)  # PNG Up filter
        for c in range(columns):
            out.append((row[c] - prev[c]) & 0xFF)
        prev = row
    return flate_compress(bytes(out))


def flate_png_up_decompress(compressed: bytes, columns: int) -> bytes:
    raw = zlib.decompress(compressed)
    stride = columns + 1
    if len(raw) % stride != 0:
        raise ValueError("invalid PNG predicted stream length")
    rows = len(raw) // stride
    out = bytearray()
    prev = bytearray(columns)
    for r in range(rows):
        tag = raw[r * stride]
        row_in = raw[r * stride + 1 : (r + 1) * stride]
        cur = bytearray(columns)
        if tag == 0:
            cur[:] = row_in
        elif tag == 2:
            for c in range(columns):
                cur[c] = (row_in[c] + prev[c]) & 0xFF
        else:
            raise ValueError(f"unsupported predictor tag {tag} in test validator")
        out.extend(cur)
        prev = cur
    return bytes(out)


def make_stream_obj(extra_dict: str, payload: bytes, compress: bool = True) -> bytes:
    body = flate_compress(payload) if compress else payload
    filter_entry = " /Filter /FlateDecode" if compress else ""
    header = f"<< {extra_dict}{filter_entry} /Length {len(body)} >>\nstream\n".encode("ascii")
    footer = b"\nendstream"
    return header + body + footer


class ClassicPdfBuilder:
    """Builds a deterministic PDF 1.7 file with a classic ASCII xref table."""

    def __init__(self, rel_path: str, version: str = "1.7") -> None:
        self.rel_path = rel_path
        self.version = version
        self.objects: dict[int, bytes] = {}

    def set_obj(self, obj_num: int, content: bytes | str) -> None:
        if isinstance(content, str):
            content = content.encode("latin1")
        self.objects[obj_num] = content

    def build(self, root_obj: int, info_obj: int | None = None) -> bytes:
        out = bytearray()
        out.extend(f"%PDF-{self.version}\n%\xe2\xe3\xcf\xd3\n".encode("latin1"))
        max_obj = max(self.objects.keys(), default=0)
        offsets: dict[int, int] = {}
        for obj_num in sorted(self.objects.keys()):
            offsets[obj_num] = len(out)
            out.extend(f"{obj_num} 0 obj\n".encode("ascii"))
            out.extend(self.objects[obj_num])
            out.extend(b"\nendobj\n")

        xref_offset = len(out)
        size = max_obj + 1
        out.extend(f"xref\n0 {size}\n".encode("ascii"))
        out.extend(b"0000000000 65535 f \n")
        for obj_num in range(1, size):
            if obj_num in offsets:
                out.extend(f"{offsets[obj_num]:010d} 00000 n \n".encode("ascii"))
            else:
                out.extend(b"0000000000 65535 f \n")

        id0, id1 = deterministic_id_hex(self.rel_path, rev=0)
        info_part = f" /Info {info_obj} 0 R" if info_obj is not None else ""
        trailer = (
            f"trailer\n<< /Size {size} /Root {root_obj} 0 R{info_part} "
            f"/ID [<{id0}> <{id1}>] >>\n"
            f"startxref\n{xref_offset}\n%%EOF\n"
        )
        out.extend(trailer.encode("ascii"))
        return bytes(out)


def build_simple_1p() -> bytes:
    rel = "render/simple-1p.pdf"
    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
    )
    content = (
        b"q\n"
        b"0.12 0.29 0.54 rg\n"
        b"72 680 468 56 re f\n"
        b"0.88 0.33 0.16 RG 2 w\n"
        b"72 520 468 130 re S\n"
        b"96 560 m 180 630 320 530 480 610 c S\n"
        b"Q\n"
        b"BT\n"
        b"1 1 1 rg\n"
        b"/F1 18 Tf\n"
        b"88 700 Td\n"
        b"(Recto Native PDF Engine - Simple 1P) Tj\n"
        b"0.1 0.1 0.1 rg\n"
        b"/F1 12 Tf\n"
        b"0 -64 Td\n"
        b"(Deterministic vector paths, cubic Beziers, and Standard 14 text.) Tj\n"
        b"ET\n"
    )
    pdf.set_obj(4, make_stream_obj("", content, compress=True))
    pdf.set_obj(
        5,
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
    )
    pdf.set_obj(
        6,
        f"<< /Title (Recto Simple 1P) /Author (Recto Corpus Generator) "
        f"/Creator (scripts/build-corpus.py) /CreationDate ({FIXED_PDF_DATE}) >>",
    )
    return pdf.build(root_obj=1, info_obj=6)


def build_compressed_xref_2p() -> bytes:
    """Build a PDF 1.6 file with /Type /ObjStm and /Type /XRef stream + PNG predictor."""
    rel = "render/compressed-xref-2p.pdf"
    out = bytearray(b"%PDF-1.6\n%\xe2\xe3\xcf\xd3\n")

    # Objects 1, 2, 3, 4, 7, 8 go inside ObjStm (object 9).
    # Objects 5 and 6 are page content streams (must be top-level uncompressed in XRef type 1).
    obj_stm_entries: list[tuple[int, bytes]] = [
        (1, b"<< /Type /Catalog /Pages 2 0 R >>"),
        (2, b"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>"),
        (
            3,
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
            b"/Resources << /Font << /F1 7 0 R >> >> /Contents 5 0 R >>",
        ),
        (
            4,
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
            b"/Resources << /Font << /F1 7 0 R >> >> /Contents 6 0 R >>",
        ),
        (7, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"),
        (
            8,
            f"<< /Title (Recto Compressed XRef 2P) /Author (Recto Corpus Generator) "
            f"/CreationDate ({FIXED_PDF_DATE}) >>".encode("ascii"),
        ),
    ]

    p1_stream = make_stream_obj(
        "",
        b"q 0.2 0.5 0.3 rg 72 680 468 48 re f Q "
        b"BT /F1 16 Tf 84 698 Td (Compressed XRef & ObjStm - Page 1 of 2) Tj ET\n",
        compress=True,
    )
    p2_stream = make_stream_obj(
        "",
        b"q 0.3 0.2 0.6 rg 72 680 468 48 re f Q "
        b"BT /F1 16 Tf 84 698 Td (Compressed XRef & ObjStm - Page 2 of 2) Tj ET\n",
        compress=True,
    )

    offsets: dict[int, int] = {}
    offsets[5] = len(out)
    out.extend(b"5 0 obj\n" + p1_stream + b"\nendobj\n")
    offsets[6] = len(out)
    out.extend(b"6 0 obj\n" + p2_stream + b"\nendobj\n")

    # Build ObjStm (object 9)
    header_parts: list[bytes] = []
    body_parts: list[bytes] = []
    cur_rel_offset = 0
    for obj_id, obj_bytes in obj_stm_entries:
        header_parts.append(f"{obj_id} {cur_rel_offset} ".encode("ascii"))
        body_parts.append(obj_bytes + b"\n")
        cur_rel_offset += len(obj_bytes) + 1
    header_bytes = b"".join(header_parts)
    first_offset = len(header_bytes)
    obj_stm_raw = header_bytes + b"".join(body_parts)
    obj_stm_obj = make_stream_obj(
        f"/Type /ObjStm /N {len(obj_stm_entries)} /First {first_offset}",
        obj_stm_raw,
        compress=True,
    )
    offsets[9] = len(out)
    out.extend(b"9 0 obj\n" + obj_stm_obj + b"\nendobj\n")

    # Object 10 is the /Type /XRef stream.
    # /W [1 2 1] -> 4 bytes per entry, /Size 11 (objects 0..10).
    xref_offset = len(out)
    offsets[10] = xref_offset

    obj_to_stm_idx = {obj_id: idx for idx, (obj_id, _) in enumerate(obj_stm_entries)}
    xref_raw = bytearray()
    for obj_id in range(11):
        if obj_id == 0:
            xref_raw.extend(struct.pack(">BHB", 0, 0, 255))
        elif obj_id in obj_to_stm_idx:
            # Type 2: compressed in ObjStm 9 at index
            xref_raw.extend(struct.pack(">BHB", 2, 9, obj_to_stm_idx[obj_id]))
        else:
            # Type 1: uncompressed offset, gen 0
            xref_raw.extend(struct.pack(">BHB", 1, offsets[obj_id], 0))

    xref_comp = flate_png_up_compress(bytes(xref_raw), columns=4)
    id0, id1 = deterministic_id_hex(rel, rev=0)
    xref_dict = (
        f"<< /Type /XRef /Size 11 /Root 1 0 R /Info 8 0 R "
        f"/ID [<{id0}> <{id1}>] /W [1 2 1] "
        f"/Filter /FlateDecode /DecodeParms << /Predictor 12 /Columns 4 >> "
        f"/Length {len(xref_comp)} >>\nstream\n"
    ).encode("ascii")
    out.extend(b"10 0 obj\n" + xref_dict + xref_comp + b"\nendstream\nendobj\n")
    out.extend(f"startxref\n{xref_offset}\n%%EOF\n".encode("ascii"))
    return bytes(out)


def build_incremental_2rev() -> bytes:
    """Build a 2-revision incrementally updated PDF with /Prev trailer chain."""
    rel = "render/incremental-2rev.pdf"
    base_builder = ClassicPdfBuilder(rel, "1.7")
    base_builder.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
    base_builder.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    base_builder.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
    )
    rev1_content = (
        b"BT /F1 16 Tf 72 720 Td (Incremental Document - Base Revision 1) Tj ET\n"
    )
    base_builder.set_obj(4, make_stream_obj("", rev1_content, compress=True))
    base_builder.set_obj(
        5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
    )
    base_builder.set_obj(
        6,
        f"<< /Title (Incremental Revision 1) /CreationDate ({FIXED_PDF_DATE}) >>",
    )
    rev1_bytes = base_builder.build(root_obj=1, info_obj=6)

    # Locate startxref from rev 1
    m = re.search(rb"startxref\n(\d+)\n%%EOF\n\Z", rev1_bytes)
    assert m is not None
    prev_xref = int(m.group(1))

    out = bytearray(rev1_bytes)
    # Revision 2 updates object 3 (Page 1 now has [4 0 R 7 0 R]), object 6 (Info), and adds object 7.
    off_3 = len(out)
    out.extend(
        b"3 0 obj\n"
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        b"/Resources << /Font << /F1 5 0 R >> >> /Contents [4 0 R 7 0 R] >>\n"
        b"endobj\n"
    )
    off_6 = len(out)
    out.extend(
        f"6 0 obj\n<< /Title (Incremental Revision 2) /CreationDate ({FIXED_PDF_DATE}) "
        f"/ModDate ({FIXED_PDF_DATE}) >>\nendobj\n".encode("ascii")
    )
    off_7 = len(out)
    rev2_overlay = (
        b"q 0.85 0.93 0.98 rg 72 640 468 44 re f Q "
        b"BT /F1 14 Tf 84 656 Td (Appended in Incremental Revision 2 via /Prev) Tj ET\n"
    )
    out.extend(b"7 0 obj\n" + make_stream_obj("", rev2_overlay, compress=True) + b"\nendobj\n")

    xref2_offset = len(out)
    id0, id1 = deterministic_id_hex(rel, rev=2)
    out.extend(
        f"xref\n"
        f"0 1\n0000000000 65535 f \n"
        f"3 1\n{off_3:010d} 00000 n \n"
        f"6 2\n{off_6:010d} 00000 n \n{off_7:010d} 00000 n \n"
        f"trailer\n<< /Size 8 /Root 1 0 R /Info 6 0 R /Prev {prev_xref} "
        f"/ID [<{id0}> <{id1}>] >>\n"
        f"startxref\n{xref2_offset}\n%%EOF\n".encode("ascii")
    )
    return bytes(out)


def build_rotated_pages() -> bytes:
    """Build a 4-page PDF with /Rotate 0, 90, 180, 270 and mixed MediaBox/CropBox."""
    rel = "render/rotated-pages.pdf"
    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R 6 0 R] /Count 4 >>")
    pdf.set_obj(
        11, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
    )
    pdf.set_obj(
        12,
        f"<< /Title (Recto Rotated Pages 0/90/180/270) /CreationDate ({FIXED_PDF_DATE}) >>",
    )

    specs = [
        (3, 7, 0, "[0 0 612 792]", "[0 0 612 792]", "Page 1 - Rotate 0"),
        (4, 8, 90, "[0 0 612 792]", "[36 36 576 756]", "Page 2 - Rotate 90"),
        (5, 9, 180, "[0 0 595 842]", "[18 18 577 824]", "Page 3 - Rotate 180"),
        (6, 10, 270, "[0 0 842 595]", "[24 24 818 571]", "Page 4 - Rotate 270"),
    ]
    for page_obj, content_obj, rot, mbox, cbox, label in specs:
        pdf.set_obj(
            page_obj,
            f"<< /Type /Page /Parent 2 0 R /Rotate {rot} "
            f"/MediaBox {mbox} /CropBox {cbox} "
            f"/Resources << /Font << /F1 11 0 R >> >> /Contents {content_obj} 0 R >>",
        )
        stream_data = (
            f"q 0.15 0.45 0.75 rg 48 48 160 36 re f Q "
            f"BT /F1 14 Tf 60 60 Td ({label}) Tj ET\n"
        ).encode("ascii")
        pdf.set_obj(content_obj, make_stream_obj("", stream_data, compress=True))

    return pdf.build(root_obj=1, info_obj=12)


def make_minimal_jpeg_8x8_rgb() -> bytes:
    """Create a valid, deterministic 8x8 baseline RGB JPEG (SOI..EOI) decodable by WIC."""
    # Quantization table of all 16s (64 bytes)
    dqt = bytes([0] + [16] * 64)
    # SOF0: 8-bit, height=8, width=8, 3 components (Y, Cb, Cr) 1x1 sampling
    sof0 = struct.pack(">BHHB", 8, 8, 8, 3) + bytes([
        1, 0x11, 0,
        2, 0x11, 0,
        3, 0x11, 0,
    ])
    # Minimal Huffman table: 1 code of length 1 (code '0' -> symbol 0x00)
    # Used for DC table 0, AC table 0, DC table 1, AC table 1.
    def one_symbol_dht(table_class_id: int) -> bytes:
        counts = bytes([1] + [0] * 15)
        symbols = bytes([0x00])
        return bytes([table_class_id]) + counts + symbols

    dht_payload = (
        one_symbol_dht(0x00)
        + one_symbol_dht(0x10)
        + one_symbol_dht(0x01)
        + one_symbol_dht(0x11)
    )
    # SOS: 3 components (1->DC0/AC0, 2->DC1/AC1, 3->DC1/AC1), Ss=0, Se=63, Ah/Al=0
    sos_hdr = bytes([3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0])
    # In our Huffman tables, symbol 0 (DC diff=0, or AC EOB) is 1-bit '0'.
    # For 1 MCU of 3 blocks (Y, Cb, Cr), each block needs DC=0 ('0') + AC=EOB ('0') = 2 bits '00'.
    # 3 blocks * 2 bits = 6 bits '000000' padded with 1s -> 0x03.
    scan_data = bytes([0x03])

    def segment(marker: int, payload: bytes) -> bytes:
        return bytes([0xFF, marker]) + struct.pack(">H", len(payload) + 2) + payload

    app0 = b"JFIF\x00\x01\x01\x01\x00H\x00H\x00\x00"
    return (
        b"\xFF\xD8"
        + segment(0xE0, app0)
        + segment(0xDB, dqt)
        + segment(0xC0, sof0)
        + segment(0xC4, dht_payload)
        + segment(0xDA, sos_hdr)
        + scan_data
        + b"\xFF\xD9"
    )


def make_ccitt_group4_16x16() -> bytes:
    """Create a valid ITU-T T.6 Group 4 2D bitstream for a 16x16 monochrome bitmap.

    Exercises Horizontal (H), Vertical V(0), VR(1), VL(1), Pass (P), and EOFB:
    - Row 0 (4W, 8B, 4W): H(001) + W4(1011) + B8(000101) + V0(1)
    - Row 1 (4W, 8B, 4W): V0(1) + V0(1) + V0(1)
    - Row 2 (5W, 8B, 3W): VR1(011) + VR1(011) + V0(1)
    - Row 3 (4W, 8B, 4W): VL1(010) + VL1(010) + V0(1)
    - Row 4 (16W):        P(0001) + V0(1)
    - Rows 5..15 (16W):   11 x V0(1)
    - EOFB:               000000000001000000000001
    """
    bits = (
        "001" + "1011" + "000101" + "1"
        + "111"
        + "011" + "011" + "1"
        + "010" + "010" + "1"
        + "0001" + "1"
        + ("1" * 11)
        + "000000000001000000000001"
    )
    while len(bits) % 8 != 0:
        bits += "0"
    return bytes(int(bits[i : i + 8], 2) for i in range(0, len(bits), 8))


def build_images_and_vectors() -> bytes:
    """Build render/images-and-vectors.pdf with DCTDecode, Flate+SMask, CCITTFaxDecode, clips, and Form XObject."""
    rel = "render/images-and-vectors.pdf"
    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Resources << "
        "/Font << /F1 5 0 R >> "
        "/ExtGState << /GS1 6 0 R >> "
        "/XObject << /ImJpeg 7 0 R /ImRgb 8 0 R /ImCcitt 10 0 R /Fm1 11 0 R >> "
        ">> /Contents 4 0 R >>",
    )

    content = (
        b"q\n"
        b"/GS1 gs\n"
        b"72 560 468 160 re W n\n"
        b"0.18 0.44 0.72 rg 72 560 240 160 re f\n"
        b"0.85 0.32 0.24 rg 220 580 280 120 re f*\n"
        b"0.10 0.10 0.10 RG 2 w 84 572 m 200 700 360 572 516 690 c S\n"
        b"Q\n"
        b"q 64 0 0 64 72 440 cm /ImJpeg Do Q\n"
        b"q 64 0 0 64 168 440 cm /ImRgb Do Q\n"
        b"q 64 0 0 64 264 440 cm /ImCcitt Do Q\n"
        b"q 1 0 0 1 360 440 cm /Fm1 Do Q\n"
        b"BT /F1 14 Tf 72 740 Td (Images, Clipping Paths, ExtGState & Form XObjects) Tj ET\n"
    )
    pdf.set_obj(4, make_stream_obj("", content, compress=True))
    pdf.set_obj(
        5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
    )
    pdf.set_obj(6, "<< /Type /ExtGState /CA 0.9 /ca 0.75 /BM /Normal >>")

    # 7: DCTDecode JPEG
    jpeg_bytes = make_minimal_jpeg_8x8_rgb()
    pdf.set_obj(
        7,
        make_stream_obj(
            "/Type /XObject /Subtype /Image /Width 8 /Height 8 "
            "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode",
            jpeg_bytes,
            compress=False,
        ),
    )

    # 8: FlateDecode RGB image + 9: SMask
    rgb_pixels = bytes(
        [(x * 32) & 0xFF for y in range(8) for x in range(8) for _ in range(3)]
    )
    smask_pixels = bytes([((x + y) * 16) & 0xFF for y in range(8) for x in range(8)])
    pdf.set_obj(
        8,
        make_stream_obj(
            "/Type /XObject /Subtype /Image /Width 8 /Height 8 "
            "/ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask 9 0 R",
            rgb_pixels,
            compress=True,
        ),
    )
    pdf.set_obj(
        9,
        make_stream_obj(
            "/Type /XObject /Subtype /Image /Width 8 /Height 8 "
            "/ColorSpace /DeviceGray /BitsPerComponent 8",
            smask_pixels,
            compress=True,
        ),
    )

    # 10: CCITTFaxDecode Group 4 monochrome bitmap
    ccitt_bytes = make_ccitt_group4_16x16()
    pdf.set_obj(
        10,
        make_stream_obj(
            "/Type /XObject /Subtype /Image /Width 16 /Height 16 "
            "/ColorSpace /DeviceGray /BitsPerComponent 1 "
            "/Filter /CCITTFaxDecode "
            "/DecodeParms << /K -1 /Columns 16 /Rows 16 /BlackIs1 false >>",
            ccitt_bytes,
            compress=False,
        ),
    )

    # 11: Form XObject
    form_stream = (
        b"q 0.92 0.88 0.72 rg 0 0 140 64 re f "
        b"0.3 0.2 0.1 RG 1.5 w 4 4 132 56 re S Q "
        b"BT /F1 11 Tf 12 28 Td (Form XObject Fm1) Tj ET\n"
    )
    pdf.set_obj(
        11,
        make_stream_obj(
            "/Type /XObject /Subtype /Form /BBox [0 0 140 64] /Matrix [1 0 0 1 0 0] "
            "/Resources << /Font << /F1 5 0 R >> >>",
            form_stream,
            compress=True,
        ),
    )
    pdf.set_obj(
        12,
        f"<< /Title (Recto Images and Vectors) /CreationDate ({FIXED_PDF_DATE}) >>",
    )
    return pdf.build(root_obj=1, info_obj=12)


def build_two_column_links_outline() -> bytes:
    """Build text/two-column-links-outline.pdf with 2 columns, ligatures, soft hyphens, outlines, and links."""
    rel = "text/two-column-links-outline.pdf"
    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(
        1,
        "<< /Type /Catalog /Pages 2 0 R /Outlines 9 0 R "
        "/Names << /Dests 13 0 R >> >>",
    )
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Resources << /Font << /F1 7 0 R >> >> "
        "/Contents 5 0 R /Annots [14 0 R 15 0 R] >>",
    )
    pdf.set_obj(
        4,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Resources << /Font << /F1 7 0 R >> >> "
        "/Contents 6 0 R >>",
    )

    # Note: \x1D and \x1E mapped via /ToUnicode to "fi" (<00660069>) and "fl" (<0066006C>),
    # and \xAD is soft hyphen in WinAnsiEncoding.
    p1_content = (
        b"BT\n"
        b"/F1 16 Tf 72 730 Td (1. Introduction to Two-Column Layout) Tj\n"
        b"/F1 11 Tf\n"
        b"0 -28 Td (Left column line 1: E\x1Dcient work\x1Dow and \x1Dre\x1Dy rendering.) Tj\n"
        b"0 -16 Td (Left column line 2: Super\xad\n"
        b"0 -16 Td (califragilistic soft-hyphenation across wrapped lines.) Tj\n"
        b"0 -16 Td (Jump to Page 2 or visit https://example.com/recto-docs.) Tj\n"
        b"260 48 Td (Right column line 1: Second column starts at x=332 pt.) Tj\n"
        b"0 -16 Td (Right column line 2: 1.1 Architecture & reading order.) Tj\n"
        b"0 -16 Td (Right column line 3: Clean column-first text extraction.) Tj\n"
        b"ET\n"
    )
    p2_content = (
        b"BT\n"
        b"/F1 16 Tf 72 730 Td (2. Second Page Details & Summary) Tj\n"
        b"/F1 11 Tf 0 -28 Td (Target page for internal GoTo links and outline bookmarks.) Tj\n"
        b"ET\n"
    )
    pdf.set_obj(5, make_stream_obj("", p1_content, compress=True))
    pdf.set_obj(6, make_stream_obj("", p2_content, compress=True))

    # Font 7 with /ToUnicode CMap 8 expanding 0x1D -> "fi" and 0x1E -> "fl"
    pdf.set_obj(
        7,
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        "/Encoding /WinAnsiEncoding /ToUnicode 8 0 R >>",
    )
    tounicode_cmap = (
        b"/CIDInit /ProcSet findresource begin\n"
        b"12 dict begin\n"
        b"begincmap\n"
        b"/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        b"/CMapName /Recto-Ligature-UCS def\n"
        b"/CMapType 2 def\n"
        b"1 begincodespacerange\n"
        b"<00> <FF>\n"
        b"endcodespacerange\n"
        b"2 beginbfchar\n"
        b"<1D> <00660069>\n"
        b"<1E> <0066006C>\n"
        b"endbfchar\n"
        b"1 beginbfrange\n"
        b"<20> <7E> <0020>\n"
        b"endbfrange\n"
        b"endcmap\n"
        b"CMapName currentdict /CMap defineresource pop\n"
        b"end\n"
        b"end\n"
    )
    pdf.set_obj(8, make_stream_obj("", tounicode_cmap, compress=True))

    # Outlines tree: 9 (root) -> 10 ("1. Introduction", child 11 "1.1 Architecture"), next 12 ("2. Second Page Details")
    pdf.set_obj(9, "<< /Type /Outlines /First 10 0 R /Last 12 0 R /Count 3 >>")
    pdf.set_obj(
        10,
        "<< /Title (1. Introduction) /Parent 9 0 R /Next 12 0 R "
        "/First 11 0 R /Last 11 0 R /Count 1 /Dest [3 0 R /XYZ 72 730 0] >>",
    )
    pdf.set_obj(
        11,
        "<< /Title (1.1 Architecture) /Parent 10 0 R /Dest [3 0 R /XYZ 332 686 0] >>",
    )
    pdf.set_obj(
        12,
        "<< /Title (2. Second Page Details) /Parent 9 0 R /Prev 10 0 R "
        "/Dest [4 0 R /XYZ 72 730 0] >>",
    )
    pdf.set_obj(
        13,
        "<< /Names [(sec-summary) [4 0 R /XYZ 72 730 0]] >>",
    )
    # Link annotations on Page 1
    pdf.set_obj(
        14,
        "<< /Type /Annot /Subtype /Link /Rect [72 648 210 664] "
        "/Border [0 0 0] /Dest [4 0 R /XYZ 72 730 0] >>",
    )
    pdf.set_obj(
        15,
        "<< /Type /Annot /Subtype /Link /Rect [215 648 390 664] "
        "/Border [0 0 0] /A << /Type /Action /S /URI /URI (https://example.com/recto-docs) >> >>",
    )
    pdf.set_obj(
        16,
        f"<< /Title (Recto Two-Column Links and Outlines) /CreationDate ({FIXED_PDF_DATE}) >>",
    )
    return pdf.build(root_obj=1, info_obj=16)


# 5x7 bitmap font for crisp 300 DPI raster OCR text in text/scanned.pdf
GLYPHS_5X7: dict[str, list[str]] = {
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01110"],
    "I": ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "11011", "10001"],
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "2": ["01110", "10001", "00001", "00110", "01000", "10000", "11111"],
    "6": ["01110", "10000", "10000", "11110", "10001", "10001", "01110"],
    " ": ["00000", "00000", "00000", "00000", "00000", "00000", "00000"],
}


def render_bitmap_banner(width: int, height: int, lines: list[tuple[str, int, int, int]]) -> bytes:
    """Render high-contrast black-on-white 8-bit grayscale bitmap text."""
    buf = bytearray([255] * (width * height))
    for text, x0, y0, scale in lines:
        cursor_x = x0
        for ch in text:
            rows = GLYPHS_5X7.get(ch, GLYPHS_5X7[" "])
            for gy, row_str in enumerate(rows):
                for gx, bit in enumerate(row_str):
                    if bit == "1":
                        for sy in range(scale):
                            py = y0 + gy * scale + sy
                            if 0 <= py < height:
                                row_off = py * width
                                for sx in range(scale):
                                    px = cursor_x + gx * scale + sx
                                    if 0 <= px < width:
                                        buf[row_off + px] = 0
            cursor_x += 6 * scale
    return bytes(buf)


def build_scanned() -> bytes:
    """Build text/scanned.pdf: 0 text operators, 300 DPI raster image of crisp English text."""
    rel = "text/scanned.pdf"
    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    # 300 DPI over a 360x120 pt (5" x 1.67") page = 1500 x 500 pixels
    img_w, img_h = 1500, 500
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 360 120] "
        "/Resources << /XObject << /ImScan 5 0 R >> >> /Contents 4 0 R >>",
    )
    # Strictly zero text operators (no BT, ET, Tj, TJ)
    content = b"q 360 0 0 120 0 0 cm /ImScan Do Q\n"
    pdf.set_obj(4, make_stream_obj("", content, compress=True))

    pixels = render_bitmap_banner(
        img_w,
        img_h,
        [
            ("RECTO OCR TEST PAGE 2026", 80, 110, 8),
            ("WINDOWS MEDIA OCR ENGINE", 80, 270, 8),
        ],
    )
    pdf.set_obj(
        5,
        make_stream_obj(
            f"/Type /XObject /Subtype /Image /Width {img_w} /Height {img_h} "
            f"/ColorSpace /DeviceGray /BitsPerComponent 8",
            pixels,
            compress=True,
        ),
    )
    pdf.set_obj(
        6,
        f"<< /Title (Recto Scanned Image-Only Page) /CreationDate ({FIXED_PDF_DATE}) >>",
    )
    return pdf.build(root_obj=1, info_obj=6)


def build_tables_and_forms() -> bytes:
    """Build convert/tables-and-forms.pdf with bordered/borderless tables, AcroForm, and annotations."""
    rel = "convert/tables-and-forms.pdf"
    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(
        1,
        "<< /Type /Catalog /Pages 2 0 R "
        "/AcroForm << /Fields [6 0 R 7 0 R 8 0 R] /DA (/F1 11 Tf 0 g) >> >>",
    )
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Resources << /Font << /F1 5 0 R >> >> "
        "/Contents 4 0 R /Annots [6 0 R 7 0 R 8 0 R 9 0 R] >>",
    )
    content = (
        b"q\n"
        b"0.15 0.15 0.15 RG 1 w\n"
        # Bordered table outer box & orthogonal grid lines (with merged top header row)
        b"72 580 420 120 re S\n"
        b"72 670 m 492 670 l S\n"
        b"72 640 m 492 640 l S\n"
        b"72 610 m 492 610 l S\n"
        b"232 580 m 232 670 l S\n"
        b"362 580 m 362 670 l S\n"
        b"Q\n"
        b"BT\n"
        b"/F1 14 Tf 72 730 Td (Tables, AcroForm Fields & Annotations) Tj\n"
        b"/F1 12 Tf 180 680 Td (Quarterly Hardware Order Schedule) Tj\n"
        b"/F1 11 Tf\n"
        b"-100 -30 Td (Item) Tj 160 0 Td (Qty) Tj 130 0 Td (Price) Tj\n"
        b"-290 -30 Td (Widget A) Tj 160 0 Td (10) Tj 130 0 Td ($25.00) Tj\n"
        b"-290 -30 Td (Widget B) Tj 160 0 Td (5) Tj 130 0 Td ($40.00) Tj\n"
        # Borderless whitespace-aligned table
        b"-290 -70 Td (Region) Tj 140 0 Td (Q1) Tj 100 0 Td (Q2) Tj\n"
        b"-240 -22 Td (North) Tj 140 0 Td (120) Tj 100 0 Td (145) Tj\n"
        b"-240 -22 Td (South) Tj 140 0 Td (98) Tj 100 0 Td (112) Tj\n"
        b"ET\n"
    )
    pdf.set_obj(4, make_stream_obj("", content, compress=True))
    pdf.set_obj(
        5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
    )
    # AcroForm Widget annotations: Tx, Btn, Ch
    pdf.set_obj(
        6,
        "<< /Type /Annot /Subtype /Widget /FT /Tx /T (customer_name) "
        "/V (Ada Lovelace) /Rect [72 340 280 364] /P 3 0 R >>",
    )
    pdf.set_obj(
        7,
        "<< /Type /Annot /Subtype /Widget /FT /Btn /Ff 0 /T (approve_terms) "
        "/V /Yes /AS /Yes /Rect [72 300 92 320] /P 3 0 R >>",
    )
    pdf.set_obj(
        8,
        "<< /Type /Annot /Subtype /Widget /FT /Ch /Ff 131072 /T (delivery_tier) "
        "/Opt [(Standard) (Express)] /V (Express) /Rect [72 260 240 284] /P 3 0 R >>",
    )
    # Sample markup annotation
    pdf.set_obj(
        9,
        "<< /Type /Annot /Subtype /Highlight /Rect [72 726 340 746] "
        "/QuadPoints [72 746 340 746 72 726 340 726] "
        "/Contents (Highlighted heading annotation) /P 3 0 R >>",
    )
    pdf.set_obj(
        10,
        f"<< /Title (Recto Tables and Forms) /CreationDate ({FIXED_PDF_DATE}) >>",
    )
    return pdf.build(root_obj=1, info_obj=10)


def build_deterministic_zip(entries: list[tuple[str, bytes]]) -> bytes:
    """Build a deterministic ZIP archive with fixed timestamps and permissions."""
    import io

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
        for arcname, payload in entries:
            zi = zipfile.ZipInfo(filename=arcname, date_time=FIXED_ZIP_DT)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.create_system = 0
            zi.external_attr = 0o644 << 16
            zf.writestr(zi, payload, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    return buf.getvalue()


def build_sample_docx() -> bytes:
    """Build a minimal valid ECMA-376 WordprocessingML .docx fixture."""
    content_types = (
        b'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        b'<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
        b'<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
        b'<Default Extension="xml" ContentType="application/xml"/>'
        b'<Override PartName="/word/document.xml" '
        b'ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
        b"</Types>"
    )
    rels = (
        b'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        b'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        b'<Relationship Id="rId1" '
        b'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" '
        b'Target="word/document.xml"/>'
        b"</Relationships>"
    )
    document_xml = (
        b'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        b'<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">'
        b"<w:body>"
        b"<w:p><w:r><w:t>Recto Sample Document (.docx)</w:t></w:r></w:p>"
        b"<w:p><w:r><w:t>Deterministic WordprocessingML fixture for Office-to-PDF conversion.</w:t></w:r></w:p>"
        b"</w:body>"
        b"</w:document>"
    )
    return build_deterministic_zip(
        [
            ("[Content_Types].xml", content_types),
            ("_rels/.rels", rels),
            ("word/document.xml", document_xml),
        ]
    )


def build_sample_xlsx() -> bytes:
    """Build a minimal valid ECMA-376 SpreadsheetML .xlsx fixture."""
    content_types = (
        b'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        b'<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
        b'<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
        b'<Default Extension="xml" ContentType="application/xml"/>'
        b'<Override PartName="/xl/workbook.xml" '
        b'ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
        b'<Override PartName="/xl/worksheets/sheet1.xml" '
        b'ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
        b"</Types>"
    )
    rels = (
        b'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        b'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        b'<Relationship Id="rId1" '
        b'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" '
        b'Target="xl/workbook.xml"/>'
        b"</Relationships>"
    )
    wb_rels = (
        b'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        b'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        b'<Relationship Id="rId1" '
        b'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" '
        b'Target="worksheets/sheet1.xml"/>'
        b"</Relationships>"
    )
    workbook_xml = (
        b'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        b'<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
        b'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
        b'<sheets><sheet name="Summary" sheetId="1" r:id="rId1"/></sheets>'
        b"</workbook>"
    )
    sheet1_xml = (
        b'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        b'<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
        b"<sheetData>"
        b'<row r="1"><c r="A1" t="inlineStr"><is><t>Item</t></is></c>'
        b'<c r="B1" t="inlineStr"><is><t>Amount</t></is></c></row>'
        b'<row r="2"><c r="A2" t="inlineStr"><is><t>Widget A</t></is></c>'
        b'<c r="B2"><v>250</v></c></row>'
        b"</sheetData>"
        b"</worksheet>"
    )
    return build_deterministic_zip(
        [
            ("[Content_Types].xml", content_types),
            ("_rels/.rels", rels),
            ("xl/_rels/workbook.xml.rels", wb_rels),
            ("xl/workbook.xml", workbook_xml),
            ("xl/worksheets/sheet1.xml", sheet1_xml),
        ]
    )


def validate_classic_xref_at(rel_path: str, data: bytes, xref_offset: int, visited: set[int]) -> None:
    if xref_offset in visited:
        raise ValueError(f"{rel_path}: cyclic /Prev xref offset {xref_offset}")
    visited.add(xref_offset)
    if not data[xref_offset:].startswith(b"xref\n"):
        raise ValueError(f"{rel_path}: startxref {xref_offset} does not point to 'xref\\n'")

    pos = xref_offset + len(b"xref\n")
    while pos < len(data) and not data[pos:].startswith(b"trailer"):
        line_end = data.find(b"\n", pos)
        if line_end < 0:
            raise ValueError(f"{rel_path}: unexpected EOF in xref subsection header")
        header_line = data[pos:line_end].decode("ascii").strip()
        parts = header_line.split()
        if len(parts) != 2:
            raise ValueError(f"{rel_path}: invalid xref subsection header '{header_line}'")
        first_obj, count = int(parts[0]), int(parts[1])
        pos = line_end + 1
        for idx in range(count):
            entry = data[pos : pos + 20]
            if len(entry) != 20:
                raise ValueError(f"{rel_path}: xref entry shorter than 20 bytes")
            off_str = entry[0:10].decode("ascii")
            gen_str = entry[11:16].decode("ascii")
            kind = chr(entry[17])
            off = int(off_str)
            gen = int(gen_str)
            obj_num = first_obj + idx
            if kind == "n":
                expected_prefix = f"{obj_num} {gen} obj\n".encode("ascii")
                if not data[off:].startswith(expected_prefix):
                    raise ValueError(
                        f"{rel_path}: xref entry for {obj_num} {gen} at offset {off} "
                        f"does not start with {expected_prefix!r}"
                    )
            pos += 20

    trailer_end = data.find(b"startxref\n", pos)
    if trailer_end < 0:
        raise ValueError(f"{rel_path}: missing startxref after trailer")
    trailer_slice = data[pos:trailer_end]
    prev_match = re.search(rb"/Prev\s+(\d+)", trailer_slice)
    if prev_match is not None:
        validate_classic_xref_at(rel_path, data, int(prev_match.group(1)), visited)


def validate_xref_stream_at(rel_path: str, data: bytes, xref_offset: int) -> None:
    obj_match = re.match(rb"(\d+)\s+(\d+)\s+obj\n<<(.*?)>>\nstream\n", data[xref_offset:], re.DOTALL)
    if obj_match is None:
        raise ValueError(f"{rel_path}: startxref {xref_offset} is not a valid XRef stream object")
    dict_bytes = obj_match.group(3)
    if b"/Type /XRef" not in dict_bytes:
        raise ValueError(f"{rel_path}: XRef stream missing /Type /XRef")
    size_m = re.search(rb"/Size\s+(\d+)", dict_bytes)
    len_m = re.search(rb"/Length\s+(\d+)", dict_bytes)
    w_m = re.search(rb"/W\s*\[\s*(\d+)\s+(\d+)\s+(\d+)\s*\]", dict_bytes)
    if not (size_m and len_m and w_m):
        raise ValueError(f"{rel_path}: XRef stream missing /Size, /Length, or /W")
    size = int(size_m.group(1))
    stream_len = int(len_m.group(1))
    w1, w2, w3 = int(w_m.group(1)), int(w_m.group(2)), int(w_m.group(3))
    if (w1, w2, w3) != (1, 2, 1):
        raise ValueError(f"{rel_path}: unexpected /W {(w1, w2, w3)}")

    stream_start = xref_offset + obj_match.end()
    stream_payload = data[stream_start : stream_start + stream_len]
    unpacked = flate_png_up_decompress(stream_payload, columns=w1 + w2 + w3)
    if len(unpacked) != size * 4:
        raise ValueError(f"{rel_path}: unpacked XRef length {len(unpacked)} != {size * 4}")

    type1_offsets: dict[int, int] = {}
    type2_refs: list[tuple[int, int, int]] = []
    for obj_id in range(size):
        etype, f2, f3 = struct.unpack(">BHB", unpacked[obj_id * 4 : (obj_id + 1) * 4])
        if etype == 1:
            expected_prefix = f"{obj_id} {f3} obj\n".encode("ascii")
            if not data[f2:].startswith(expected_prefix):
                raise ValueError(
                    f"{rel_path}: XRef type-1 entry for {obj_id} at {f2} does not match object header"
                )
            type1_offsets[obj_id] = f2
        elif etype == 2:
            type2_refs.append((obj_id, f2, f3))

    for obj_id, stm_obj_id, idx in type2_refs:
        if stm_obj_id not in type1_offsets:
            raise ValueError(f"{rel_path}: ObjStm {stm_obj_id} not found in type-1 XRef entries")
        stm_off = type1_offsets[stm_obj_id]
        stm_m = re.match(
            rb"\d+\s+0\s+obj\n<<(.*?)>>\nstream\n", data[stm_off:], re.DOTALL
        )
        if stm_m is None or b"/Type /ObjStm" not in stm_m.group(1):
            raise ValueError(f"{rel_path}: object {stm_obj_id} is not a valid /Type /ObjStm")
        n_m = re.search(rb"/N\s+(\d+)", stm_m.group(1))
        first_m = re.search(rb"/First\s+(\d+)", stm_m.group(1))
        slen_m = re.search(rb"/Length\s+(\d+)", stm_m.group(1))
        if not (n_m and first_m and slen_m):
            raise ValueError(f"{rel_path}: ObjStm {stm_obj_id} missing /N, /First, or /Length")
        n_val = int(n_m.group(1))
        if not (0 <= idx < n_val):
            raise ValueError(f"{rel_path}: ObjStm index {idx} out of range 0..{n_val - 1}")
        s_start = stm_off + stm_m.end()
        s_len = int(slen_m.group(1))
        dec = zlib.decompress(data[s_start : s_start + s_len])
        first_val = int(first_m.group(1))
        tokens = dec[:first_val].decode("ascii").split()
        if len(tokens) != n_val * 2:
            raise ValueError(f"{rel_path}: ObjStm header token count mismatch")
        if int(tokens[idx * 2]) != obj_id:
            raise ValueError(f"{rel_path}: ObjStm slot {idx} has id {tokens[idx * 2]} != {obj_id}")


def validate_pdf_structure(rel_path: str, data: bytes) -> None:
    """Validate PDF header, stream /Length exactness, and xref byte offsets."""
    if not (data.startswith(b"%PDF-1.") or data.startswith(b"%PDF-2.")):
        raise ValueError(f"{rel_path}: missing %PDF- header")
    if not data.endswith(b"%%EOF\n"):
        raise ValueError(f"{rel_path}: missing trailing %%EOF")

    # Verify every stream's /Length matches the exact distance to \nendstream
    for m in re.finditer(rb"<<([^\n]*?)>>\nstream\n", data):
        dict_body = m.group(1)
        len_m = re.search(rb"/Length\s+(\d+)", dict_body)
        if len_m is None:
            raise ValueError(f"{rel_path}: stream dictionary missing direct /Length")
        declared_len = int(len_m.group(1))
        stream_start = m.end()
        end_pos = stream_start + declared_len
        if data[end_pos : end_pos + 10] != b"\nendstream":
            raise ValueError(
                f"{rel_path}: stream at {stream_start} with /Length {declared_len} "
                f"is not followed by '\\nendstream'"
            )

    eof_m = re.search(rb"startxref\n(\d+)\n%%EOF\n\Z", data)
    if eof_m is None:
        raise ValueError(f"{rel_path}: missing startxref before %%EOF")
    xref_offset = int(eof_m.group(1))
    if data[xref_offset:].startswith(b"xref\n"):
        validate_classic_xref_at(rel_path, data, xref_offset, set())
    else:
        validate_xref_stream_at(rel_path, data, xref_offset)

    if rel_path == "text/scanned.pdf":
        if b"/Font" in data:
            raise ValueError("text/scanned.pdf must not declare any /Font resources")
        obj4_m = re.search(
            rb"4 0 obj\n<<([^\n]*?)>>\nstream\n", data
        )
        if obj4_m is None:
            raise ValueError("text/scanned.pdf missing page content stream 4 0 obj")
        len4_m = re.search(rb"/Length\s+(\d+)", obj4_m.group(1))
        assert len4_m is not None
        s4_start = obj4_m.end()
        s4_raw = zlib.decompress(data[s4_start : s4_start + int(len4_m.group(1))])
        if re.search(rb"\b(?:BT|ET|Tj|TJ|Tf)\b", s4_raw):
            raise ValueError("text/scanned.pdf content stream must contain 0 text operators")


def validate_openxml_structure(rel_path: str, data: bytes) -> None:
    import io

    try:
        with zipfile.ZipFile(io.BytesIO(data), "r") as zf:
            names = set(zf.namelist())
            if "[Content_Types].xml" not in names or "_rels/.rels" not in names:
                raise ValueError(f"{rel_path}: missing OpenXML package relationships")
            if rel_path.endswith(".docx") and "word/document.xml" not in names:
                raise ValueError(f"{rel_path}: missing word/document.xml")
            if rel_path.endswith(".xlsx") and "xl/workbook.xml" not in names:
                raise ValueError(f"{rel_path}: missing xl/workbook.xml")
    except zipfile.BadZipFile as exc:
        raise ValueError(f"{rel_path}: bad zip archive: {exc}") from exc


def expected_corpus_files() -> dict[str, bytes]:
    """Return mapping of relative corpus path -> deterministic file bytes."""
    files: dict[str, bytes] = {
        "render/simple-1p.pdf": build_simple_1p(),
        "render/compressed-xref-2p.pdf": build_compressed_xref_2p(),
        "render/incremental-2rev.pdf": build_incremental_2rev(),
        "render/rotated-pages.pdf": build_rotated_pages(),
        "render/images-and-vectors.pdf": build_images_and_vectors(),
        "text/two-column-links-outline.pdf": build_two_column_links_outline(),
        "text/scanned.pdf": build_scanned(),
        "convert/tables-and-forms.pdf": build_tables_and_forms(),
        "convert/sample.docx": build_sample_docx(),
        "convert/sample.xlsx": build_sample_xlsx(),
    }
    total_bytes = 0
    for rel_path, content in files.items():
        total_bytes += len(content)
        if rel_path.endswith(".pdf"):
            validate_pdf_structure(rel_path, content)
        elif rel_path.endswith((".docx", ".xlsx")):
            validate_openxml_structure(rel_path, content)

    if total_bytes >= MAX_CORPUS_TOTAL_BYTES:
        raise ValueError(
            f"corpus total size {total_bytes} exceeds limit {MAX_CORPUS_TOTAL_BYTES}"
        )
    return files


def generate_corpus(corpus_root: Path = CORPUS_ROOT) -> dict[str, str]:
    """Generate all corpus files atomically and return {rel_path: sha256_hex}."""
    files = expected_corpus_files()
    hashes: dict[str, str] = {}
    for rel_path, data in sorted(files.items()):
        dest = corpus_root / rel_path
        write_atomic(dest, data)
        hashes[rel_path] = hashlib.sha256(data).hexdigest()
    return hashes


def check_corpus(corpus_root: Path = CORPUS_ROOT) -> tuple[bool, list[str]]:
    """Verify on-disk corpus files match deterministic generator output and structural checks."""
    files = expected_corpus_files()
    errors: list[str] = []
    total_on_disk = 0
    for rel_path, expected_bytes in sorted(files.items()):
        dest = corpus_root / rel_path
        if not dest.is_file():
            errors.append(f"missing corpus file: {rel_path}")
            continue
        actual_bytes = dest.read_bytes()
        total_on_disk += len(actual_bytes)
        try:
            if rel_path.endswith(".pdf"):
                validate_pdf_structure(rel_path, actual_bytes)
            elif rel_path.endswith((".docx", ".xlsx")):
                validate_openxml_structure(rel_path, actual_bytes)
        except ValueError as exc:
            errors.append(str(exc))
        if actual_bytes != expected_bytes:
            exp_hash = hashlib.sha256(expected_bytes).hexdigest()
            act_hash = hashlib.sha256(actual_bytes).hexdigest()
            errors.append(
                f"byte mismatch for {rel_path}: expected sha256 {exp_hash}, got {act_hash}"
            )

    for subdir in ("render", "text", "convert"):
        sub_path = corpus_root / subdir
        if sub_path.is_dir():
            for child in sorted(sub_path.iterdir()):
                if child.is_file():
                    rel = f"{subdir}/{child.name}"
                    if rel not in files:
                        errors.append(f"unexpected corpus file: {rel}")

    if total_on_disk >= MAX_CORPUS_TOTAL_BYTES:
        errors.append(
            f"total corpus size {total_on_disk} exceeds ceiling {MAX_CORPUS_TOTAL_BYTES}"
        )
    return (len(errors) == 0, errors)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build or verify deterministic Recto test corpus fixtures."
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify on-disk corpus fixtures match deterministic output without writing.",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=CORPUS_ROOT,
        help="Target corpus root directory (default: tests/corpus).",
    )
    args = parser.parse_args(argv)

    if args.check:
        ok, errors = check_corpus(args.root)
        if not ok:
            for err in errors:
                print(f"error: {err}", file=sys.stderr)
            return 2
        print(
            f"verified: {len(expected_corpus_files())} corpus fixtures in {args.root} are current"
        )
        return 0

    hashes = generate_corpus(args.root)
    total_bytes = sum((args.root / rel).stat().st_size for rel in hashes)
    print(f"generated: {len(hashes)} corpus fixtures ({total_bytes} bytes) in {args.root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
