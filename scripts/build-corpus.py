#!/usr/bin/env python3
"""Deterministic PDF and OpenXML test corpus generator for Recto.

Uses Python standard library only. Every generated file uses fixed timestamps,
deterministic document IDs, and temp-file-then-replace atomic writes in the
destination folder.
"""

from __future__ import annotations

import argparse
import base64
import functools
import hashlib
import os
from pathlib import Path
import re
import stat
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
MAX_SEED_FILE_BYTES = 4_096
DEFLATE_BOMB_LIMIT_BYTES = 256 * 1024 * 1024
CORPUS_USER_PASSWORD = b"recto-test"
CORPUS_OWNER_PASSWORD = b"recto-owner"
CORPUS_PERMISSIONS_MASK = -1028  # 0xFFFFFBFC: every permission but bit 11, document assembly (ISO 32000-1 Table 22)
# The documented contract (tests/corpus/README.md), which the validators check fixtures against. These are separate
# literals on purpose: the generator's constants above are what a regression would change, so a validator reading them
# would follow the regression instead of catching it. test_the_documented_contract_matches_the_readme ties each one to
# the README.
DOCUMENTED_USER_PASSWORD = b"recto-test"
DOCUMENTED_OWNER_PASSWORD = b"recto-owner"
DOCUMENTED_PERMISSIONS = -1028
DOCUMENTED_PDF_DATE = "D:20261013000000Z"
DOCUMENTED_SEED_MAX_BYTES = 4_096
DOCUMENTED_STREAM_CEILING = 256 * 1024 * 1024


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


VALIDATOR_INFLATE_LIMIT = 4 * 1024 * 1024  # far above any corpus stream (the largest is under 1 KiB)


def inflate(data: bytes, what: str, limit: int = VALIDATOR_INFLATE_LIMIT) -> bytes:
    """data inflated as exactly one complete Flate stream, refusing as a ValueError naming what: a malformed or
    truncated stream, bytes after its end, or output past limit. One call with max_length produces at most limit + 1
    bytes, so validating never allocates more than that whatever the input claims."""
    d = zlib.decompressobj()
    try:
        out = d.decompress(data, limit + 1)
    except zlib.error as exc:
        raise ValueError(f"{what}: bad Flate data: {exc}") from exc
    if len(out) > limit or d.unconsumed_tail:
        raise ValueError(f"{what}: inflates past {limit} bytes")
    if not d.eof:
        raise ValueError(f"{what}: incomplete Flate stream")
    if d.unused_data:
        raise ValueError(f"{what}: bytes after the end of the Flate stream")
    return out


def flate_png_up_decompress(compressed: bytes, columns: int) -> bytes:
    raw = inflate(compressed, "a PNG-predicted Flate stream")
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
    # /W [1 2 2] -> 5 bytes per entry, /Size 11 (objects 0..10) so object 0 has gen 65535.
    xref_offset = len(out)
    offsets[10] = xref_offset

    obj_to_stm_idx = {obj_id: idx for idx, (obj_id, _) in enumerate(obj_stm_entries)}
    xref_raw = bytearray()
    for obj_id in range(11):
        if obj_id == 0:
            xref_raw.extend(struct.pack(">BHH", 0, 0, 65535))
        elif obj_id in obj_to_stm_idx:
            # Type 2: compressed in ObjStm 9 at index
            xref_raw.extend(struct.pack(">BHH", 2, 9, obj_to_stm_idx[obj_id]))
        else:
            # Type 1: uncompressed offset, gen 0
            xref_raw.extend(struct.pack(">BHH", 1, offsets[obj_id], 0))

    xref_comp = flate_png_up_compress(bytes(xref_raw), columns=5)
    id0, id1 = deterministic_id_hex(rel, rev=0)
    xref_dict = (
        f"<< /Type /XRef /Size 11 /Root 1 0 R /Info 8 0 R "
        f"/ID [<{id0}> <{id1}>] /W [1 2 2] "
        f"/Filter /FlateDecode /DecodeParms << /Predictor 12 /Columns 5 >> "
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
        b"/F1 10 Tf\n"
        b"0 -28 Td (Left col 1: Ef\x1Dcient work\x1Eow and \x1Dre\x1Ey render.) Tj\n"
        b"0 -16 Td (Left col 2: Super\xad) Tj\n"
        b"0 -16 Td (califragilistic soft-hyphenation in column 1.) Tj\n"
        b"0 -16 Td (Jump to Page 2 or https://example.com/recto-docs) Tj\n"
        b"260 48 Td (Right col 1: Second column starts at x=332 pt.) Tj\n"
        b"0 -16 Td (Right col 2: 1.1 Architecture & reading order.) Tj\n"
        b"0 -16 Td (Right col 3: Clean column-first text extraction.) Tj\n"
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

    # Font 7 with /Differences [29 /fi 30 /fl] and /ToUnicode CMap 8 expanding 0x1D -> "fi" and 0x1E -> "fl"
    pdf.set_obj(
        7,
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        "/Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding /Differences [29 /fi 30 /fl] >> "
        "/ToUnicode 8 0 R >>",
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
        "/AcroForm << /Fields [6 0 R 7 0 R 8 0 R] "
        "/DR << /Font << /F1 5 0 R /ZaDb 10 0 R >> >> "
        "/DA (/F1 11 Tf 0 g) >> >>",
    )
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Resources << /Font << /F1 5 0 R /ZaDb 10 0 R >> >> "
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
        b"/F1 12 Tf 108 -50 Td (Quarterly Hardware Order Schedule) Tj\n"
        b"/F1 11 Tf\n"
        b"-96 -30 Td (Item) Tj 160 0 Td (Qty) Tj 130 0 Td (Price) Tj\n"
        b"-290 -30 Td (Widget A) Tj 160 0 Td (10) Tj 130 0 Td ($25.00) Tj\n"
        b"-290 -30 Td (Widget B) Tj 160 0 Td (5) Tj 130 0 Td ($40.00) Tj\n"
        # Borderless whitespace-aligned table
        b"-290 -70 Td (Region) Tj 160 0 Td (Q1) Tj 130 0 Td (Q2) Tj\n"
        b"-290 -24 Td (North) Tj 160 0 Td (120) Tj 130 0 Td (145) Tj\n"
        b"-290 -24 Td (South) Tj 160 0 Td (98) Tj 130 0 Td (112) Tj\n"
        b"ET\n"
    )
    pdf.set_obj(4, make_stream_obj("", content, compress=True))
    pdf.set_obj(
        5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
    )
    # AcroForm Widget annotations: Tx, Btn, Ch with /AP Form XObject appearance streams
    pdf.set_obj(
        6,
        "<< /Type /Annot /Subtype /Widget /FT /Tx /T (customer_name) "
        "/DA (/F1 11 Tf 0 g) /V (Ada Lovelace) /Rect [72 340 280 364] "
        "/AP << /N 11 0 R >> /P 3 0 R >>",
    )
    pdf.set_obj(
        7,
        "<< /Type /Annot /Subtype /Widget /FT /Btn /Ff 0 /T (approve_terms) "
        "/DA (/ZaDb 10 Tf 0 g) /V /Yes /AS /Yes /Rect [72 300 92 320] "
        "/AP << /N << /Yes 12 0 R /Off 13 0 R >> >> /P 3 0 R >>",
    )
    pdf.set_obj(
        8,
        "<< /Type /Annot /Subtype /Widget /FT /Ch /Ff 131072 /T (delivery_tier) "
        "/DA (/F1 11 Tf 0 g) /Opt [(Standard) (Express)] /V (Express) "
        "/Rect [72 260 240 284] /AP << /N 14 0 R >> /P 3 0 R >>",
    )
    # Sample markup annotation
    pdf.set_obj(
        9,
        "<< /Type /Annot /Subtype /Highlight /Rect [72 726 340 746] "
        "/QuadPoints [72 746 340 746 72 726 340 726] "
        "/Contents (Highlighted heading annotation) /P 3 0 R >>",
    )
    pdf.set_obj(
        10, "<< /Type /Font /Subtype /Type1 /BaseFont /ZapfDingbats >>"
    )
    pdf.set_obj(
        11,
        make_stream_obj(
            "/Type /XObject /Subtype /Form /BBox [0 0 208 24] "
            "/Resources << /Font << /F1 5 0 R >> >>",
            b"/Tx BMC q BT /F1 11 Tf 4 6 Td (Ada Lovelace) Tj ET Q EMC\n",
            compress=True,
        ),
    )
    pdf.set_obj(
        12,
        make_stream_obj(
            "/Type /XObject /Subtype /Form /BBox [0 0 20 20] "
            "/Resources << /Font << /ZaDb 10 0 R >> >>",
            b"q 0 0 20 20 re S BT /ZaDb 12 Tf 4 4 Td (4) Tj ET Q\n",
            compress=True,
        ),
    )
    pdf.set_obj(
        13,
        make_stream_obj(
            "/Type /XObject /Subtype /Form /BBox [0 0 20 20]",
            b"q 0 0 20 20 re S Q\n",
            compress=True,
        ),
    )
    pdf.set_obj(
        14,
        make_stream_obj(
            "/Type /XObject /Subtype /Form /BBox [0 0 168 24] "
            "/Resources << /Font << /F1 5 0 R >> >>",
            b"/Tx BMC q BT /F1 11 Tf 4 6 Td (Express) Tj ET Q EMC\n",
            compress=True,
        ),
    )
    pdf.set_obj(
        15,
        f"<< /Title (Recto Tables and Forms) /CreationDate ({FIXED_PDF_DATE}) >>",
    )
    return pdf.build(root_obj=1, info_obj=15)


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


def validate_classic_xref_at(rel_path: str, data: bytes, xref_offset: int, visited: set[int],
                             located: dict | None = None, sizes: list[int] | None = None,
                             roots: list[bytes] | None = None) -> None:
    """located (newest section first): object number -> header offset of each in-use entry; sizes and roots: each
    section's /Size and /Root."""
    located = {} if located is None else located
    sizes = [] if sizes is None else sizes
    roots = [] if roots is None else roots
    if xref_offset in visited:
        raise ValueError(f"{rel_path}: cyclic /Prev xref offset {xref_offset}")
    visited.add(xref_offset)
    if not data[xref_offset:].startswith(b"xref\n"):
        raise ValueError(f"{rel_path}: startxref {xref_offset} does not point to 'xref\\n'")

    pos = xref_offset + len(b"xref\n")
    in_section: set[int] = set()  # one entry per object in a section (7.5.4); newer sections still override older ones
    while pos < len(data) and not data[pos:].startswith(b"trailer"):
        line_end = data.find(b"\n", pos)
        if line_end < 0:
            raise ValueError(f"{rel_path}: unexpected EOF in xref subsection header")
        # Exactly two non-negative integers and a single space between them, as the corpus writes it.
        header = re.fullmatch(rb"(\d+) (\d+)", data[pos:line_end])
        if header is None:
            raise ValueError(f"{rel_path}: invalid xref subsection header {data[pos:line_end]!r}")
        first_obj, count = int(header.group(1)), int(header.group(2))
        pos = line_end + 1
        for idx in range(count):
            entry = data[pos : pos + 20]
            # The one form ISO 32000-1 7.5.4 allows: 10-digit offset, 5-digit generation, n or f, 2-byte end of line.
            if re.fullmatch(rb"\d{10} \d{5} [nf](?: \r| \n|\r\n)", entry) is None:
                raise ValueError(f"{rel_path}: malformed xref entry {entry!r}")
            off_str = entry[0:10].decode("ascii")
            gen_str = entry[11:16].decode("ascii")
            kind = chr(entry[17])
            off = int(off_str)
            gen = int(gen_str)
            obj_num = first_obj + idx
            if obj_num in in_section:
                raise ValueError(f"{rel_path}: object {obj_num} appears twice in the xref section at {xref_offset}")
            in_section.add(obj_num)
            # Object 0 heads the free list (ISO 32000-1 7.5.4); the corpus frees nothing, so every free entry, object 0's
            # included, is exactly offset 0 and generation 65535.
            if (obj_num == 0 or kind == "f") and (kind, off, gen) != ("f", 0, 65535):
                raise ValueError(f"{rel_path}: xref entry for {obj_num} must be free with offset 0 and generation 65535, "
                                 f"got {entry!r}")
            if kind == "n" and gen != 0:
                raise ValueError(f"{rel_path}: xref entry for {obj_num} has generation {gen}; the corpus writes 0 only")
            if kind == "n":
                expected_prefix = f"{obj_num} {gen} obj\n".encode("ascii")
                if off not in object_header_offsets(rel_path, data) or not data[off:].startswith(expected_prefix):
                    raise ValueError(
                        f"{rel_path}: xref entry for {obj_num} {gen} at offset {off} "
                        f"does not start with {expected_prefix!r}"
                    )
                located.setdefault(obj_num, off)
            else:
                # A newer section's free entry wins over an older live one: the object is gone from that revision on.
                # Object 0's entry is recorded too, so a chain that never lists it refuses below.
                located.setdefault(obj_num, "free")
            pos += 20

    trailer_end = data.find(b"startxref\n", pos)
    if trailer_end < 0:
        raise ValueError(f"{rel_path}: missing startxref after trailer")
    if not data.startswith(b"trailer\n<<", pos):
        raise ValueError(f"{rel_path}: xref section at {xref_offset} is not followed by a trailer dictionary")
    trailer_dict_end = _skip_object(data, pos + len(b"trailer\n"))
    if trailer_dict_end + 1 != trailer_end or data[trailer_dict_end : trailer_end] != b"\n":
        raise ValueError(f"{rel_path}: the trailer dictionary at {pos} is not followed directly by startxref")
    trailer = dict_entries(data[pos + len(b"trailer\n") + 2 : trailer_dict_end - 2])
    allowed = {b"/Size", b"/Root", b"/Info", b"/ID", b"/Prev", b"/Encrypt"}
    if not set(trailer) <= allowed:
        raise ValueError(f"{rel_path}: trailer has keys the corpus never writes: {sorted(set(trailer) - allowed)}")
    size = re.fullmatch(rb"\d+", trailer.get(b"/Size", b""))
    if size is None:
        raise ValueError(f"{rel_path}: trailer needs a direct /Size")
    sizes.append(int(size.group(0)))
    roots.append(trailer.get(b"/Root", b""))
    if b"/Prev" in trailer:
        prev = re.fullmatch(rb"\d+", trailer[b"/Prev"])
        if prev is None:
            raise ValueError(f"{rel_path}: trailer /Prev {trailer[b'/Prev']!r} is not an offset")
        validate_classic_xref_at(rel_path, data, int(prev.group(0)), visited, located, sizes, roots)


def validate_xref_stream_at(rel_path: str, data: bytes, xref_offset: int, visited: set[int] | None = None,
                            located: dict | None = None, sizes: list[int] | None = None,
                            roots: list[bytes] | None = None) -> None:
    visited = set() if visited is None else visited
    located = {} if located is None else located
    sizes = [] if sizes is None else sizes
    roots = [] if roots is None else roots
    if xref_offset in visited:
        raise ValueError(f"{rel_path}: cyclic /Prev xref offset {xref_offset}")
    visited.add(xref_offset)
    try:
        entries, stream_start, stream_end = stream_object_at(rel_path, data, xref_offset)
    except ValueError:
        raise ValueError(f"{rel_path}: startxref {xref_offset} is not a valid XRef stream object") from None
    m_end = re.match(rb"\d+ \d+ obj\n", data[xref_offset:]).end()
    dict_bytes = data[xref_offset + m_end + 2 : _skip_object(data, xref_offset + m_end) - 2]
    allowed = {b"/Type", b"/Size", b"/Root", b"/Info", b"/ID", b"/W", b"/Filter", b"/DecodeParms", b"/Length",
               b"/Prev", b"/Index"}
    if not set(entries) <= allowed:
        raise ValueError(f"{rel_path}: XRef stream has keys the corpus never writes: {sorted(set(entries) - allowed)}")
    if entries.get(b"/Type") != b"/XRef":
        raise ValueError(f"{rel_path}: XRef stream missing /Type /XRef")
    size_m = re.fullmatch(rb"(\d+)", entries.get(b"/Size", b""))
    len_m = re.fullmatch(rb"(\d+)", entries.get(b"/Length", b""))
    w_m = re.fullmatch(rb"\[\s*(\d+)\s+(\d+)\s+(\d+)\s*\]", entries.get(b"/W", b""))
    if not (size_m and len_m and w_m):
        raise ValueError(f"{rel_path}: XRef stream missing /Size, /Length, or /W")
    size = int(size_m.group(1))
    w1, w2, w3 = int(w_m.group(1)), int(w_m.group(2)), int(w_m.group(3))
    if (w1, w2, w3) != (1, 2, 2):
        raise ValueError(f"{rel_path}: unexpected /W {(w1, w2, w3)}")
    # Decoded below as Flate with the PNG Up predictor over whole entries, numbered from object 0: the dictionary must
    # declare exactly that, or the entries would be read under rules the file does not state.
    filters = top_level_values(dict_bytes, b"/Filter")
    parms = top_level_values(dict_bytes, b"/DecodeParms")
    index = top_level_values(dict_bytes, b"/Index")
    if filters != [b"/FlateDecode"]:
        raise ValueError(f"{rel_path}: XRef stream /Filter must be /FlateDecode, found {filters}")
    # Exactly these two parameters: /Colors or /BitsPerComponent would change the row width the decoder uses.
    if len(parms) != 1 or not parms[0].startswith(b"<<") \
            or dict_entries(parms[0][2:-2]) != {b"/Predictor": b"12", b"/Columns": str(w1 + w2 + w3).encode()}:
        raise ValueError(f"{rel_path}: XRef stream /DecodeParms must be exactly /Predictor 12 /Columns {w1 + w2 + w3}")
    if index not in ([], [f"[0 {int(size_m.group(1))}]".encode()]):
        raise ValueError(f"{rel_path}: XRef stream /Index {index} is not the default [0 Size]")

    row_width = w1 + w2 + w3
    sizes.append(size)
    roots.append(entries.get(b"/Root", b""))
    stream_payload = data[stream_start:stream_end]
    unpacked = flate_png_up_decompress(stream_payload, columns=row_width)
    if len(unpacked) != size * row_width:
        raise ValueError(f"{rel_path}: unpacked XRef length {len(unpacked)} != {size * row_width}")

    type1_offsets: dict[int, int] = {}
    type2_refs: list[tuple[int, int, int]] = []
    for obj_id in range(size):
        etype, f2, f3 = struct.unpack(">BHH", unpacked[obj_id * row_width : (obj_id + 1) * row_width])
        if (obj_id == 0 or etype == 0) and (etype, f2, f3) != (0, 0, 65535):
            # Object 0 heads the free list, and the corpus frees nothing: every free entry, object 0's included, is
            # next-free 0, generation 65535.
            raise ValueError(f"{rel_path}: XRef entry for {obj_id} must be free with next 0 and generation 65535, "
                             f"got {(etype, f2, f3)}")
        if obj_id == 0:
            located.setdefault(0, "free")
        elif etype == 1 and f3 != 0:
            raise ValueError(f"{rel_path}: XRef type-1 entry for {obj_id} has generation {f3}; the corpus writes 0 only")
        elif etype == 1:
            expected_prefix = f"{obj_id} {f3} obj\n".encode("ascii")
            if f2 not in object_header_offsets(rel_path, data) or not data[f2:].startswith(expected_prefix):
                raise ValueError(
                    f"{rel_path}: XRef type-1 entry for {obj_id} at {f2} does not match object header"
                )
            type1_offsets[obj_id] = f2
            located.setdefault(obj_id, f2)
        elif etype == 2:
            type2_refs.append((obj_id, f2, f3))
            located.setdefault(obj_id, ("compressed", f2, f3))
        else:
            located.setdefault(obj_id, "free")

    for obj_id, stm_obj_id, idx in type2_refs:
        if stm_obj_id not in type1_offsets:
            raise ValueError(f"{rel_path}: ObjStm {stm_obj_id} not found in type-1 XRef entries")
        stm_off = type1_offsets[stm_obj_id]
        try:
            stm, s_start, s_end = stream_object_at(rel_path, data, stm_off)
        except ValueError:
            raise ValueError(f"{rel_path}: object {stm_obj_id} is not a valid /Type /ObjStm") from None
        allowed = {b"/Type", b"/N", b"/First", b"/Filter", b"/Length"}
        if stm.get(b"/Type") != b"/ObjStm" or not set(stm) <= allowed:
            raise ValueError(f"{rel_path}: object {stm_obj_id} is not a /Type /ObjStm the corpus writes: {sorted(stm)}")
        # Decoded below as plain Flate: the stream must declare exactly that.
        if stm.get(b"/Filter") != b"/FlateDecode":
            raise ValueError(f"{rel_path}: ObjStm {stm_obj_id} /Filter must be /FlateDecode")
        n_m = re.fullmatch(rb"(\d+)", stm.get(b"/N", b""))
        first_m = re.fullmatch(rb"(\d+)", stm.get(b"/First", b""))
        if not (n_m and first_m):
            raise ValueError(f"{rel_path}: ObjStm {stm_obj_id} missing /N or /First")
        n_val = int(n_m.group(1))
        if not (0 <= idx < n_val):
            raise ValueError(f"{rel_path}: ObjStm index {idx} out of range 0..{n_val - 1}")
        dec = inflate(data[s_start:s_end], rel_path)
        first_val = int(first_m.group(1))
        tokens = dec[:first_val].decode("ascii").split()
        if len(tokens) != n_val * 2:
            raise ValueError(f"{rel_path}: ObjStm header token count mismatch")
        if int(tokens[idx * 2]) != obj_id:
            raise ValueError(f"{rel_path}: ObjStm slot {idx} has id {tokens[idx * 2]} != {obj_id}")
        validate_objstm_offsets(rel_path, stm_obj_id, dec, first_val, [int(t) for t in tokens[1::2]])
    # Older revisions last: this (newer) section's entries, /Size and /Root are recorded first and take precedence.
    if b"/Prev" in entries:
        prev = re.fullmatch(rb"\d+", entries[b"/Prev"])
        if prev is None or int(prev.group(0)) >= len(data):
            raise ValueError(f"{rel_path}: XRef stream /Prev {entries[b'/Prev']!r} does not locate an xref section")
        prev_at = int(prev.group(0))
        if data.startswith(b"xref\n", prev_at):
            validate_classic_xref_at(rel_path, data, prev_at, visited, located, sizes, roots)
        else:
            validate_xref_stream_at(rel_path, data, prev_at, visited, located, sizes, roots)


def _complete_pdf_object(chunk: bytes) -> bool:
    """Whether chunk is exactly one PDF value, read by the same parser as every other object (dictionary and array
    delimiters are distinct, strings are data)."""
    try:
        return bool(chunk) and _skip_object(chunk, 0) == len(chunk)
    except ValueError:
        return False


def validate_objstm_offsets(rel_path: str, stm_obj_id: int, dec: bytes, first: int, offsets: list[int]) -> None:
    """Each offset in an object stream's header must locate its object: offsets start at 0, rise strictly, stay
    inside the stream, and each slot up to the next offset holds exactly one complete object (ISO 32000-1 7.5.7)."""
    body = dec[first:]
    if not offsets or offsets[0] != 0 or any(b <= a for a, b in zip(offsets, offsets[1:])) or offsets[-1] >= len(body):
        raise ValueError(f"{rel_path}: ObjStm {stm_obj_id} offsets {offsets} do not locate its objects")
    for k, start in enumerate(offsets):
        end = offsets[k + 1] if k + 1 < len(offsets) else len(body)
        if not _complete_pdf_object(body[start:end].strip()):
            raise ValueError(f"{rel_path}: ObjStm {stm_obj_id} slot {k} at offset {start} is not one complete object")


def validate_content_stream_syntax(rel_path: str, raw: bytes) -> None:
    """Verify balanced literal strings and in-page text positioning in content streams."""
    depth = 0
    escaped = False
    for b in raw:
        if escaped:
            escaped = False
            continue
        if b == 0x5C:  # backslash
            escaped = True
        elif b == 0x28:  # '('
            depth += 1
        elif b == 0x29:  # ')'
            depth -= 1
            if depth < 0:
                raise ValueError(f"{rel_path}: unmatched ')' in content stream")
    if depth != 0:
        raise ValueError(f"{rel_path}: unterminated '(' string literal in content stream")

    if b"BT" not in raw:
        return

    # Track text matrix translation (tx, ty) across BT..ET blocks
    tx, ty = 0.0, 0.0
    for line in raw.splitlines():
        for m in re.finditer(
            rb"(?:(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+Td)|"
            rb"(?:1\s+0\s+0\s+1\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+Tm)|"
            rb"\b(BT)\b",
            line,
        ):
            if m.group(5) is not None:
                tx, ty = 0.0, 0.0
            elif m.group(1) is not None:
                tx += float(m.group(1))
                ty += float(m.group(2))
            elif m.group(3) is not None:
                tx = float(m.group(3))
                ty = float(m.group(4))
            if m.group(5) is None and not (0.0 <= tx <= 842.0 and 0.0 <= ty <= 842.0):
                raise ValueError(
                    f"{rel_path}: text position ({tx}, {ty}) is outside page bounds [0..842]"
                )


_DELIMITERS = b"()<>[]{}/%"


_NUMBER = re.compile(rb"[+-]?(?:\d+\.?\d*|\.\d+)")
_INTEGER = re.compile(rb"\d+")
_NAME = re.compile(rb"/[A-Za-z0-9_.+-]*")
_KEYWORDS = (b"true", b"false", b"null")


def _skip_ws(body: bytes, i: int) -> int:
    while i < len(body) and body[i : i + 1].isspace():
        i += 1
    return i


def _skip_object(body: bytes, i: int) -> int:
    """The index just past the one PDF value starting at body[i], read by the grammar the corpus writes: a dictionary of
    name keys and values, an array of values, a literal or hex string, a plain name, a number, an indirect reference
    (N G R), true, false or null. Anything else refuses, so a malformed object is never skipped over."""
    if body.startswith(b"<<", i):
        i = _skip_ws(body, i + 2)
        keys: set[bytes] = set()
        while not body.startswith(b">>", i):
            if i >= len(body):
                raise ValueError("unterminated dictionary")
            key = _NAME.match(body, i)
            if key is None or key.end() == i + 1:
                raise ValueError(f"dictionary key at {i} is not a name")
            after = key.end()
            if after < len(body) and not body[after : after + 1].isspace() and body[after] not in _DELIMITERS:
                raise ValueError(f"dictionary key at {i} is not a plain name")
            if key.group(0) in keys:
                raise ValueError(f"dictionary key {key.group(0)!r} appears twice")
            keys.add(key.group(0))
            i = _skip_ws(body, key.end())
            if body.startswith(b">>", i):
                raise ValueError(f"dictionary key {key.group(0)!r} has no value")
            i = _skip_ws(body, _skip_object(body, i))
        return i + 2
    if body[i : i + 1] == b"[":
        i = _skip_ws(body, i + 1)
        while body[i : i + 1] != b"]":
            if i >= len(body):
                raise ValueError("unterminated array")
            i = _skip_ws(body, _skip_object(body, i))
        return i + 1
    if body[i : i + 1] == b"(":
        depth = 0
        while i < len(body):
            c = body[i : i + 1]
            if c == b"\\":  # an escaped byte never opens or closes the string
                i += 2
                continue
            depth += c == b"("
            depth -= c == b")"
            i += 1
            if depth == 0:
                return i
        raise ValueError("unterminated literal string")
    if body[i : i + 1] == b"<":
        end = body.find(b">", i)
        if end < 0 or re.fullmatch(rb"[0-9A-Fa-f\s]*", body[i + 1 : end]) is None:
            raise ValueError(f"malformed hex string at {i}")
        return end + 1
    if body[i : i + 1] == b"/":
        name = _NAME.match(body, i)
        end = name.end()
        if end < len(body) and not body[end : end + 1].isspace() and body[end] not in _DELIMITERS:
            raise ValueError(f"name at {i} is not a plain name")
        return end
    for word in _KEYWORDS:
        if body.startswith(word, i) and (i + len(word) == len(body) or body[i + len(word)] in _DELIMITERS
                                        or body[i + len(word) : i + len(word) + 1].isspace()):
            return i + len(word)
    number = _NUMBER.match(body, i)
    if number is None:
        raise ValueError(f"unexpected {body[i:i + 12]!r} where a PDF value belongs")
    end = number.end()
    if end < len(body) and not body[end : end + 1].isspace() and body[end] not in _DELIMITERS:
        raise ValueError(f"number at {i} runs into {body[end:end + 1]!r}")
    # An integer may open an indirect reference: N G R.
    ref = re.match(rb"\s+(\d+)\s+R(?![^\s()<>\[\]{}/%])", body[end:]) if _INTEGER.fullmatch(number.group(0)) else None
    return end + ref.end() if ref else end


def top_level_values(dict_body: bytes, key: bytes) -> list[bytes]:
    """The values of key among a dictionary body's own entries, never one inside a nested dictionary, array or string.
    A value written as an indirect reference ("12 0 R") is returned whole."""
    values, i, n = [], 0, len(dict_body)
    tokens: list[bytes] = []
    while i < n:
        if dict_body[i : i + 1].isspace():
            i += 1
            continue
        j = _skip_object(dict_body, i)
        tokens.append(dict_body[i:j])
        i = j
    k = 0
    while k < len(tokens):
        name = tokens[k]
        if not name.startswith(b"/") or k + 1 >= len(tokens):
            raise ValueError(f"dictionary entry {name!r} is not a name followed by a value")
        # The corpus writes plain key names only; a #xx escape would let one key pass for another (/Len#67th).
        if re.fullmatch(rb"/[A-Za-z0-9_.+-]+", name) is None:
            raise ValueError(f"dictionary key {name!r} is not a plain name")
        value = tokens[k + 1]
        k += 2
        if (re.fullmatch(rb"\d+", value) and k + 1 < len(tokens)
                and re.fullmatch(rb"\d+", tokens[k]) and tokens[k + 1] == b"R"):
            value = b" ".join([value, tokens[k], tokens[k + 1]])
            k += 2
        if name == key:
            values.append(value)
    return values


def dict_entries(dict_body: bytes) -> dict[bytes, bytes]:
    """A dictionary body's own entries, key -> value, each key once (a repeated key refuses)."""
    entries: dict[bytes, bytes] = {}
    i, n = 0, len(dict_body)
    names = []
    while i < n:
        if dict_body[i : i + 1].isspace():
            i += 1
            continue
        j = _skip_object(dict_body, i)
        if dict_body[i : i + 1] == b"/":
            names.append(dict_body[i:j])
        i = j
    for name in dict.fromkeys(names):
        values = top_level_values(dict_body, name)
        if len(values) > 1:
            raise ValueError(f"dictionary key {name!r} appears {len(values)} times")
        if values:
            entries[name] = values[0]
    return entries


_OBJ_HEADER = re.compile(rb"(?:\A|(?<=\n))\d+ \d+ obj\n")


def iter_objects(rel_path: str, data: bytes):
    """(object number, value start, value end, stream payload start or None, payload end) for every object, found by
    walking objects forward: each value is skipped with the string-aware parser and a stream's payload by its own
    /Length, so bytes inside strings or stream data are never read as structure."""
    pos = 0
    while True:
        m = _OBJ_HEADER.search(data, pos)
        if m is None:
            return
        num, gen = (int(v) for v in m.group(0).split()[:2])
        # The corpus writes generation 0 only, so every reference ("N 0 R") names exactly one object.
        if gen != 0:
            raise ValueError(f"{rel_path}: object {num} at {m.start()} has generation {gen}; the corpus writes 0 only")
        start = m.end()
        try:
            end = _skip_object(data, start)
        except ValueError as e:
            raise ValueError(f"{rel_path}: object at {m.start()} does not parse: {e}") from None
        stream_start = stream_end = None
        after = end
        if data.startswith(b"<<", start) and data.startswith(b"\nstream\n", end):
            stream_start = end + len(b"\nstream\n")
            lengths = top_level_values(data[start + 2 : end - 2], b"/Length")
            if len(lengths) != 1 or re.fullmatch(rb"\d+", lengths[0]) is None:
                raise ValueError(f"{rel_path}: stream dictionary needs exactly one direct /Length, found {lengths}")
            stream_end = stream_start + int(lengths[0])
            if not data.startswith(b"\nendstream", stream_end):
                raise ValueError(
                    f"{rel_path}: stream at {stream_start} with /Length {int(lengths[0])} "
                    f"is not followed by '\\nendstream'"
                )
            after = stream_end + len(b"\nendstream")
        # Every object ends with endobj right after its value (or its stream), so nothing is left unparsed in between.
        if not data.startswith(b"\nendobj\n", after):
            raise ValueError(f"{rel_path}: object at {m.start()} does not end with endobj after its value")
        yield num, start, end, stream_start, stream_end, m.start()
        pos = after + len(b"\nendobj\n")


@functools.lru_cache(maxsize=8)
def object_header_offsets(rel_path: str, data: bytes) -> frozenset[int]:
    """Where the forward object walk finds each object's header: the only offsets an xref entry may name, so an
    object header written inside a string or a stream payload can never be selected."""
    return frozenset(header for *_, header in iter_objects(rel_path, data))


def compressed_dict(rel_path: str, data: bytes, located: dict, where: tuple) -> dict[bytes, bytes] | None:
    """The dictionary stored in an object-stream slot ("compressed", stream number, index), or None when the slot
    holds no dictionary."""
    _, stm_num, idx = where
    stm_header = located.get(stm_num)
    if not isinstance(stm_header, int):
        raise ValueError(f"{rel_path}: object stream {stm_num} is not located by the cross-reference chain")
    entries, start, end = stream_object_at(rel_path, data, stm_header)
    dec = inflate(data[start:end], rel_path)
    first = int(entries[b"/First"])
    offsets = [int(v) for v in dec[:first].split()[1::2]]
    slot_end = first + offsets[idx + 1] if idx + 1 < len(offsets) else len(dec)
    slot = dec[first + offsets[idx] : slot_end].strip()
    return dict_entries(slot[2:-2]) if slot.startswith(b"<<") else None


def stream_object_at(rel_path: str, data: bytes, offset: int) -> tuple[dict[bytes, bytes], int, int]:
    """The stream object starting exactly at offset: (its dictionary's entries, payload start, payload end)."""
    m = re.match(rb"\d+ \d+ obj\n", data[offset:])
    if m is None or not data.startswith(b"<<", offset + m.end()):
        raise ValueError(f"{rel_path}: offset {offset} is not a stream object")
    start = offset + m.end()
    end = _skip_object(data, start)
    if not data.startswith(b"\nstream\n", end):
        raise ValueError(f"{rel_path}: object at {offset} is not a stream")
    entries = dict_entries(data[start + 2 : end - 2])
    length = re.fullmatch(rb"(\d+)", entries.get(b"/Length", b""))
    if length is None:
        raise ValueError(f"{rel_path}: stream at {offset} needs a direct /Length")
    payload = end + len(b"\nstream\n")
    return entries, payload, payload + int(length.group(1))


def validate_layout(rel_path: str, data: bytes) -> list[int]:
    """Account for every byte: the header, then objects and revision endings only. A classic revision ends with its
    xref table, `trailer`, its dictionary, `startxref` naming that table and `%%EOF`; an xref-stream revision ends with
    `startxref` naming its xref stream object and `%%EOF`. Nothing else may sit anywhere in the file, and every
    `startxref` names the section of its own revision, so no fragment can stand in for another."""
    header = re.match(rb"%PDF-[12]\.\d\n%[\x80-\xff]{4}\n", data)
    if header is None:
        raise ValueError(f"{rel_path}: the header is not %PDF-n.n and a binary comment line")
    pos = header.end()
    section = None  # where this revision's xref table or xref stream object starts
    revisions = 0
    ends: list[int] = []
    while pos < len(data):
        m = re.match(rb"\d+ \d+ obj\n", data[pos:])
        if m is not None:
            start = pos + m.end()
            end = _skip_object(data, start)
            if data.startswith(b"<<", start) and data.startswith(b"\nstream\n", end):
                if dict_entries(data[start + 2 : end - 2]).get(b"/Type") == b"/XRef":
                    section = pos
                lengths = top_level_values(data[start + 2 : end - 2], b"/Length")
                if len(lengths) != 1 or not lengths[0].isdigit():
                    raise ValueError(f"{rel_path}: stream dictionary needs exactly one direct /Length, found {lengths}")
                end += len(b"\nstream\n") + int(lengths[0])
                if not data.startswith(b"\nendstream", end):
                    raise ValueError(f"{rel_path}: stream at {pos} does not end at its /Length")
                end += len(b"\nendstream")
            if not data.startswith(b"\nendobj\n", end):
                raise ValueError(f"{rel_path}: object at {pos} does not end with endobj")
            pos = end + len(b"\nendobj\n")
            continue
        if data.startswith(b"xref\n", pos):
            section = pos
            trailer = data.find(b"trailer\n<<", pos)
            if trailer < 0:
                raise ValueError(f"{rel_path}: xref table at {pos} has no trailer")
            pos = _skip_object(data, trailer + len(b"trailer\n")) + 1
            if data[pos - 1 : pos] != b"\n":
                raise ValueError(f"{rel_path}: the trailer dictionary at {trailer} is not followed by a line break")
        ending = re.match(rb"startxref\n(\d+)\n%%EOF\n", data[pos:])
        if ending is None:
            raise ValueError(f"{rel_path}: unexpected bytes at {pos}: {data[pos:pos + 16]!r}")
        if section is None or int(ending.group(1)) != section:
            raise ValueError(f"{rel_path}: startxref at {pos} names {ending.group(1).decode()}, not its revision's "
                             f"cross-reference section ({section})")
        section = None
        revisions += 1
        pos += ending.end()
        ends.append(pos)
    if revisions == 0:
        raise ValueError(f"{rel_path}: no revision ends with startxref and %%EOF")
    return ends


def validate_pdf_structure(rel_path: str, data: bytes, earlier_revisions: bool = True, encrypted: bool = False) -> None:
    """Validate PDF header, stream /Length exactness, xref offsets, and content semantics. With encrypted, stream
    bodies are ciphertext, so their content is left to validate_encrypted_pdf_structure, which decrypts it; every
    other check (objects, xref coverage, /Size, the catalog, layout) is the same."""
    if not (data.startswith(b"%PDF-1.") or data.startswith(b"%PDF-2.")):
        raise ValueError(f"{rel_path}: missing %PDF- header")
    if not data.endswith(b"%%EOF\n"):
        raise ValueError(f"{rel_path}: missing trailing %%EOF")

    # Verify every stream's /Length matches the exact distance to \nendstream. Every stream keyword is found, whatever
    # its dictionary's layout (single-line or spread over lines), and each must be accounted for: an endstream no
    # dictionary was matched to refuses.
    # Every object, parsed once; a later revision's object replaces the earlier one (incremental updates).
    objects: dict[int, tuple[int, int, int | None, int | None]] = {}
    headers: dict[int, int] = {}
    for num, start, end, stream_start, end_pos, header in iter_objects(rel_path, data):
        objects[num] = (start, end, stream_start, end_pos)
        headers[num] = header
        if stream_start is None or encrypted:
            continue
        stream_entries = dict_entries(data[start + 2 : end - 2])
        # A plain Flate content stream (no predictor parameters, not an image, object or xref stream) is checked as
        # page content.
        if (
            stream_entries.get(b"/Filter") == b"/FlateDecode"
            and b"/DecodeParms" not in stream_entries
            and stream_entries.get(b"/Subtype") != b"/Image"
            and stream_entries.get(b"/Type") not in (b"/ObjStm", b"/XRef")
        ):
            dec = inflate(data[stream_start:end_pos], rel_path)
            if b"/CMapName" not in dec:
                validate_content_stream_syntax(rel_path, dec)


    def value_of(raw: bytes) -> bytes:
        """raw itself, or the value of the object it references ("12 0 R")."""
        ref = re.fullmatch(rb"(\d+) \d+ R", raw)
        if ref is None:
            return raw
        target = objects.get(int(ref.group(1)))
        if target is None:
            raise ValueError(f"{rel_path}: reference {raw!r} names no object")
        return data[target[0] : target[1]]

    def entries_of(raw: bytes) -> dict[bytes, bytes]:
        value = value_of(raw)
        if not (value.startswith(b"<<") and value.endswith(b">>")):
            raise ValueError(f"{rel_path}: {raw!r} is not a dictionary")
        return dict_entries(value[2:-2])

    def payload_of(num: int) -> bytes:
        start, _, stream_start, stream_end = objects[num]
        if stream_start is None:
            raise ValueError(f"{rel_path}: object {num} is not a stream")
        return data[stream_start:stream_end]

    eof_m = re.search(rb"startxref\n(\d+)\n%%EOF\n\Z", data)
    if eof_m is None:
        raise ValueError(f"{rel_path}: missing startxref before %%EOF")
    xref_offset = int(eof_m.group(1))
    located: dict = {}
    sizes: list[int] = []
    roots: list[bytes] = []
    if data[xref_offset:].startswith(b"xref\n"):
        validate_classic_xref_at(rel_path, data, xref_offset, set(), located, sizes, roots)
    else:
        validate_xref_stream_at(rel_path, data, xref_offset, None, located, sizes, roots)
    # The chain must locate every object at its newest definition, and the newest /Size must be one past the highest
    # object number (ISO 32000-1 7.5.4, 7.5.5): an empty or partial index refuses.
    if located.get(0) != "free":
        raise ValueError(f"{rel_path}: the cross-reference chain has no free entry for object 0")
    for num, header in headers.items():
        if located.get(num) != header:
            raise ValueError(f"{rel_path}: object {num} at {header} is not what the cross-reference chain locates "
                             f"({located.get(num)})")
    highest = max([*headers, *located], default=0)
    if not sizes or sizes[0] != highest + 1:
        raise ValueError(f"{rel_path}: newest /Size {sizes[0] if sizes else None} is not {highest + 1}")

    # The newest /Root must name an object the chain locates (a walked object or a live object-stream slot) whose
    # dictionary is the catalog.
    dicts = {num: entries_of(data[s:e]) for num, (s, e, _, _) in objects.items() if data.startswith(b"<<", s)}
    root_ref = re.fullmatch(rb"(\d+) 0 R", roots[0]) if roots else None
    if root_ref is None:
        raise ValueError(f"{rel_path}: the newest trailer has no /Root reference")
    root_num = int(root_ref.group(1))
    where = located.get(root_num)
    if isinstance(where, int):
        root = dicts.get(root_num)
    elif isinstance(where, tuple):
        root = compressed_dict(rel_path, data, located, where)
    else:
        raise ValueError(f"{rel_path}: /Root names object {root_num}, which the cross-reference chain does not locate")
    if root is None or root.get(b"/Type") != b"/Catalog":
        raise ValueError(f"{rel_path}: /Root object {root_num} is not a /Type /Catalog dictionary")
    acroform = entries_of(root[b"/AcroForm"]) if b"/AcroForm" in root else None
    if acroform is not None and b"/DA" in acroform:
        if b"/DR" not in acroform or b"/Font" not in entries_of(acroform[b"/DR"]):
            raise ValueError(f"{rel_path}: /AcroForm with /DA is missing /DR /Font resources")
        for w_obj_id, w_dict in dicts.items():
            if w_dict.get(b"/Subtype") != b"/Widget":
                continue
            if b"/AP" not in w_dict or b"/N" not in entries_of(w_dict[b"/AP"]):
                raise ValueError(f"{rel_path}: Widget object {w_obj_id} missing /AP << /N appearance")
            if b"/AS" not in w_dict:
                continue
            normal = entries_of(entries_of(w_dict[b"/AP"])[b"/N"])
            for req_state in (w_dict[b"/AS"], b"/Off"):
                ref = re.fullmatch(rb"(\d+) 0 R", normal.get(req_state, b""))
                if ref is None:
                    raise ValueError(f"{rel_path}: Widget {w_obj_id} missing {req_state.decode()} in /AP /N")
                if dicts.get(int(ref.group(1)), {}).get(b"/Subtype") != b"/Form":
                    raise ValueError(
                        f"{rel_path}: Widget {w_obj_id} {req_state.decode()} target {ref.group(1).decode()} "
                        f"is not a /Subtype /Form"
                    )


    if rel_path == "text/two-column-links-outline.pdf":
        s5_raw = inflate(payload_of(5), rel_path)
        if b"\x1d" not in s5_raw or b"\x1e" not in s5_raw or b"\xad" not in s5_raw:
            raise ValueError(
                "text/two-column-links-outline.pdf must exercise 0x1D (fi), 0x1E (fl), and 0xAD (soft hyphen)"
            )

    if rel_path == "text/scanned.pdf":
        if b"/Font" in data:
            raise ValueError("text/scanned.pdf must not declare any /Font resources")
        if 4 not in objects:
            raise ValueError("text/scanned.pdf missing page content stream 4 0 obj")
        s4_raw = inflate(payload_of(4), rel_path)
        if re.search(rb"\b(?:BT|ET|Tj|TJ|Tf)\b", s4_raw):
            raise ValueError("text/scanned.pdf content stream must contain 0 text operators")

    # Last, the backstop: every byte belongs to an object or a revision ending (the checks above say why first).
    ends = validate_layout(rel_path, data)
    # An incremental update leaves each earlier revision a complete PDF up to its own %%EOF: each is validated on its
    # own, so its xref, /Size and /Root must hold for what it contained, not only for the newest state.
    # Each is validated once, without validating its own earlier revisions again: linear, not exponential, in the
    # number of revisions.
    if earlier_revisions:
        for end in ends[:-1]:
            validate_pdf_structure(f"{rel_path} (revision ending at byte {end})", data[:end], earlier_revisions=False)


# ---------------------------------------------------------------------------
# Pure-stdlib RC4 and FIPS 197 AES-128 / AES-256 for PDF Standard Security
# ---------------------------------------------------------------------------

AES_SBOX = (
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
    0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
    0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
    0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
    0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
    0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
    0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
    0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
    0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
    0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
    0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16,
)
AES_INV_SBOX = tuple(AES_SBOX.index(i) for i in range(256))
AES_XTIME = tuple(((i << 1) ^ 0x11B) & 0xFF if (i & 0x80) else (i << 1) for i in range(256))
AES_RCON = (0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36)
PDF_PAD_BYTES = bytes((
    0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41,
    0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
    0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80,
    0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A,
))


def _gf_mul(a: int, b: int) -> int:
    res = 0
    for _ in range(8):
        if b & 1:
            res ^= a
        a = AES_XTIME[a]
        b >>= 1
    return res


def aes_expand_key(key: bytes) -> list[list[int]]:
    nk = len(key) // 4
    nr = nk + 6
    w = [list(key[4 * i : 4 * i + 4]) for i in range(nk)]
    for i in range(nk, 4 * (nr + 1)):
        temp = list(w[i - 1])
        if i % nk == 0:
            temp = [
                AES_SBOX[temp[1]] ^ AES_RCON[i // nk],
                AES_SBOX[temp[2]],
                AES_SBOX[temp[3]],
                AES_SBOX[temp[0]],
            ]
        elif nk > 6 and i % nk == 4:
            temp = [AES_SBOX[b] for b in temp]
        w.append([w[i - nk][j] ^ temp[j] for j in range(4)])
    return [[w[4 * r + c][row] for c in range(4) for row in range(4)] for r in range(nr + 1)]


def aes_encrypt_block(block: bytes, round_keys: list[list[int]]) -> bytes:
    s = [block[i] ^ round_keys[0][i] for i in range(16)]
    nr = len(round_keys) - 1
    for r in range(1, nr):
        u = [
            AES_SBOX[s[0]],  AES_SBOX[s[5]],  AES_SBOX[s[10]], AES_SBOX[s[15]],
            AES_SBOX[s[4]],  AES_SBOX[s[9]],  AES_SBOX[s[14]], AES_SBOX[s[3]],
            AES_SBOX[s[8]],  AES_SBOX[s[13]], AES_SBOX[s[2]],  AES_SBOX[s[7]],
            AES_SBOX[s[12]], AES_SBOX[s[1]],  AES_SBOX[s[6]],  AES_SBOX[s[11]],
        ]
        rk = round_keys[r]
        for c in range(4):
            i = 4 * c
            a0, a1, a2, a3 = u[i], u[i + 1], u[i + 2], u[i + 3]
            tm = a0 ^ a1 ^ a2 ^ a3
            s[i] = a0 ^ tm ^ AES_XTIME[a0 ^ a1] ^ rk[i]
            s[i + 1] = a1 ^ tm ^ AES_XTIME[a1 ^ a2] ^ rk[i + 1]
            s[i + 2] = a2 ^ tm ^ AES_XTIME[a2 ^ a3] ^ rk[i + 2]
            s[i + 3] = a3 ^ tm ^ AES_XTIME[a3 ^ a0] ^ rk[i + 3]
    rk = round_keys[nr]
    return bytes((
        AES_SBOX[s[0]] ^ rk[0],   AES_SBOX[s[5]] ^ rk[1],   AES_SBOX[s[10]] ^ rk[2],  AES_SBOX[s[15]] ^ rk[3],
        AES_SBOX[s[4]] ^ rk[4],   AES_SBOX[s[9]] ^ rk[5],   AES_SBOX[s[14]] ^ rk[6],  AES_SBOX[s[3]] ^ rk[7],
        AES_SBOX[s[8]] ^ rk[8],   AES_SBOX[s[13]] ^ rk[9],  AES_SBOX[s[2]] ^ rk[10],  AES_SBOX[s[7]] ^ rk[11],
        AES_SBOX[s[12]] ^ rk[12], AES_SBOX[s[1]] ^ rk[13],  AES_SBOX[s[6]] ^ rk[14],  AES_SBOX[s[11]] ^ rk[15],
    ))


def aes_decrypt_block(block: bytes, round_keys: list[list[int]]) -> bytes:
    nr = len(round_keys) - 1
    s = [block[i] ^ round_keys[nr][i] for i in range(16)]
    for r in range(nr - 1, 0, -1):
        u = [
            AES_INV_SBOX[s[0]],  AES_INV_SBOX[s[13]], AES_INV_SBOX[s[10]], AES_INV_SBOX[s[7]],
            AES_INV_SBOX[s[4]],  AES_INV_SBOX[s[1]],  AES_INV_SBOX[s[14]], AES_INV_SBOX[s[11]],
            AES_INV_SBOX[s[8]],  AES_INV_SBOX[s[5]],  AES_INV_SBOX[s[2]],  AES_INV_SBOX[s[15]],
            AES_INV_SBOX[s[12]], AES_INV_SBOX[s[9]],  AES_INV_SBOX[s[6]],  AES_INV_SBOX[s[3]],
        ]
        rk = round_keys[r]
        for c in range(4):
            i = 4 * c
            a0 = u[i] ^ rk[i]
            a1 = u[i + 1] ^ rk[i + 1]
            a2 = u[i + 2] ^ rk[i + 2]
            a3 = u[i + 3] ^ rk[i + 3]
            s[i] = _gf_mul(a0, 0x0E) ^ _gf_mul(a1, 0x0B) ^ _gf_mul(a2, 0x0D) ^ _gf_mul(a3, 0x09)
            s[i + 1] = _gf_mul(a0, 0x09) ^ _gf_mul(a1, 0x0E) ^ _gf_mul(a2, 0x0B) ^ _gf_mul(a3, 0x0D)
            s[i + 2] = _gf_mul(a0, 0x0D) ^ _gf_mul(a1, 0x09) ^ _gf_mul(a2, 0x0E) ^ _gf_mul(a3, 0x0B)
            s[i + 3] = _gf_mul(a0, 0x0B) ^ _gf_mul(a1, 0x0D) ^ _gf_mul(a2, 0x09) ^ _gf_mul(a3, 0x0E)
    rk = round_keys[0]
    return bytes((
        AES_INV_SBOX[s[0]] ^ rk[0],   AES_INV_SBOX[s[13]] ^ rk[1],  AES_INV_SBOX[s[10]] ^ rk[2],  AES_INV_SBOX[s[7]] ^ rk[3],
        AES_INV_SBOX[s[4]] ^ rk[4],   AES_INV_SBOX[s[1]] ^ rk[5],   AES_INV_SBOX[s[14]] ^ rk[6],  AES_INV_SBOX[s[11]] ^ rk[7],
        AES_INV_SBOX[s[8]] ^ rk[8],   AES_INV_SBOX[s[5]] ^ rk[9],   AES_INV_SBOX[s[2]] ^ rk[10],  AES_INV_SBOX[s[15]] ^ rk[11],
        AES_INV_SBOX[s[12]] ^ rk[12], AES_INV_SBOX[s[9]] ^ rk[13],  AES_INV_SBOX[s[6]] ^ rk[14],  AES_INV_SBOX[s[3]] ^ rk[15],
    ))


def aes_cbc_encrypt(key: bytes, iv: bytes, data: bytes, pad: bool = True) -> bytes:
    if pad:
        pad_len = 16 - (len(data) % 16)
        data = data + bytes([pad_len] * pad_len)
    rks = aes_expand_key(key)
    prev = iv
    out = bytearray()
    for off in range(0, len(data), 16):
        blk = bytes(data[off + i] ^ prev[i] for i in range(16))
        enc = aes_encrypt_block(blk, rks)
        out.extend(enc)
        prev = enc
    return bytes(out)


def aes_cbc_decrypt(key: bytes, iv: bytes, ciphertext: bytes, unpad: bool = True) -> bytes:
    if len(ciphertext) == 0 or len(ciphertext) % 16 != 0:
        raise ValueError(f"invalid AES-CBC ciphertext length {len(ciphertext)}")
    rks = aes_expand_key(key)
    prev = iv
    out = bytearray()
    for off in range(0, len(ciphertext), 16):
        cblk = ciphertext[off : off + 16]
        dblk = aes_decrypt_block(cblk, rks)
        out.extend(dblk[i] ^ prev[i] for i in range(16))
        prev = cblk
    raw = bytes(out)
    if unpad:
        pad_len = raw[-1]
        if not (1 <= pad_len <= 16) or raw[-pad_len:] != bytes([pad_len] * pad_len):
            raise ValueError("invalid PKCS#7 padding in AES-CBC payload")
        return raw[:-pad_len]
    return raw


def rc4_crypt(key: bytes, data: bytes) -> bytes:
    s = list(range(256))
    j = 0
    klen = len(key)
    for i in range(256):
        j = (j + s[i] + key[i % klen]) & 0xFF
        s[i], s[j] = s[j], s[i]
    i = 0
    j = 0
    out = bytearray(len(data))
    for idx, b in enumerate(data):
        i = (i + 1) & 0xFF
        j = (j + s[i]) & 0xFF
        s[i], s[j] = s[j], s[i]
        out[idx] = b ^ s[(s[i] + s[j]) & 0xFF]
    return bytes(out)


def _pdf_pad_password(pw: bytes) -> bytes:
    return (pw + PDF_PAD_BYTES)[:32]


def pdf_r34_compute_o(user_pw: bytes, owner_pw: bytes) -> bytes:
    digest = hashlib.md5(_pdf_pad_password(owner_pw or user_pw)).digest()
    for _ in range(50):
        digest = hashlib.md5(digest).digest()
    rc4_key = digest[:16]
    acc = rc4_crypt(rc4_key, _pdf_pad_password(user_pw))
    for i in range(1, 20):
        acc = rc4_crypt(bytes(b ^ i for b in rc4_key), acc)
    return acc


def pdf_r34_unwrap_owner(owner_pw: bytes, o_val: bytes) -> bytes:
    """Algorithm 7 (ISO 32000-1 7.6.3.4): recover padded user password from /O."""
    digest = hashlib.md5(_pdf_pad_password(owner_pw)).digest()
    for _ in range(50):
        digest = hashlib.md5(digest).digest()
    rc4_key = digest[:16]
    acc = o_val
    for i in range(19, 0, -1):
        acc = rc4_crypt(bytes(b ^ i for b in rc4_key), acc)
    return rc4_crypt(rc4_key, acc)


def pdf_r34_compute_file_key(
    user_pw_or_padded: bytes, o_val: bytes, p_val: int, id0_bytes: bytes
) -> bytes:
    h = hashlib.md5()
    h.update(_pdf_pad_password(user_pw_or_padded))
    h.update(o_val)
    h.update(struct.pack("<i", p_val))
    h.update(id0_bytes)
    digest = h.digest()
    for _ in range(50):
        digest = hashlib.md5(digest[:16]).digest()
    return digest[:16]


def pdf_r34_compute_u(file_key: bytes, id0_bytes: bytes) -> bytes:
    digest = hashlib.md5(PDF_PAD_BYTES + id0_bytes).digest()
    acc = rc4_crypt(file_key, digest)
    for i in range(1, 20):
        acc = rc4_crypt(bytes(b ^ i for b in file_key), acc)
    return acc + (b"\x00" * 16)


def pdf_r34_object_key(file_key: bytes, obj_num: int, gen_num: int, is_aes: bool) -> bytes:
    ext = struct.pack("<I", obj_num)[:3] + struct.pack("<H", gen_num)
    if is_aes:
        ext += b"sAlT"
    return hashlib.md5(file_key + ext).digest()[:16]


def pdf_r6_hash_2b(pw: bytes, salt: bytes, user_key: bytes = b"") -> bytes:
    """ISO 32000-2 section 7.6.4.3.4 Algorithm 2.B (64+ round iterative hash)."""
    pw = pw[:127]
    k = hashlib.sha256(pw + salt + user_key).digest()
    round_idx = 0
    while True:
        k1 = (pw + k + user_key) * 64
        e = aes_cbc_encrypt(k[:16], k[16:32], k1, pad=False)
        mod = sum(e[:16]) % 3
        if mod == 0:
            k = hashlib.sha256(e).digest()
        elif mod == 1:
            k = hashlib.sha384(e).digest()
        else:
            k = hashlib.sha512(e).digest()
        round_idx += 1
        if round_idx >= 64 and e[-1] <= (round_idx - 32):
            break
    return k[:32]


def _build_encrypted_pdf(mode: str) -> bytes:
    rel = f"render/encrypted-{mode}.pdf"
    id0_hex, id1_hex = deterministic_id_hex(rel, rev=0)
    id0_bytes = bytes.fromhex(id0_hex)
    user_pw = CORPUS_USER_PASSWORD
    owner_pw = CORPUS_OWNER_PASSWORD
    p_val = CORPUS_PERMISSIONS_MASK

    raw_content = (
        f"q 0.94 0.96 0.99 rg 36 700 523 80 re f Q\n"
        f"BT /F1 18 Tf 54 748 Td (Recto Encrypted PDF ({mode.upper()})) Tj "
        f"/F1 12 Tf 0 -24 Td (Unlocked with user password recto-test or owner password recto-owner.) Tj ET\n"
    ).encode("ascii")
    comp_stream = flate_compress(raw_content)

    def encrypt_obj_payload(obj_num: int, payload: bytes, file_key: bytes) -> bytes:
        if mode == "rc4-128":
            okey = pdf_r34_object_key(file_key, obj_num, 0, is_aes=False)
            return rc4_crypt(okey, payload)
        if mode == "aes-128":
            okey = pdf_r34_object_key(file_key, obj_num, 0, is_aes=True)
            iv = hashlib.sha256(f"recto-iv:{rel}:{obj_num}".encode("utf-8")).digest()[:16]
            return iv + aes_cbc_encrypt(okey, iv, payload, pad=True)
        iv = hashlib.sha256(f"recto-iv:{rel}:{obj_num}".encode("utf-8")).digest()[:16]
        return iv + aes_cbc_encrypt(file_key, iv, payload, pad=True)

    if mode in ("rc4-128", "aes-128"):
        o_val = pdf_r34_compute_o(user_pw, owner_pw)
        file_key = pdf_r34_compute_file_key(user_pw, o_val, p_val, id0_bytes)
        u_val = pdf_r34_compute_u(file_key, id0_bytes)
        if mode == "rc4-128":
            enc_dict = (
                f"<< /Filter /Standard /V 2 /R 3 /Length 128 /P {p_val} "
                f"/O <{o_val.hex().upper()}> /U <{u_val.hex().upper()}> >>"
            )
            ver = "1.7"
        else:
            enc_dict = (
                f"<< /Filter /Standard /V 4 /R 4 /Length 128 /P {p_val} /EncryptMetadata true "
                f"/CF << /StdCF << /Type /CryptFilter /CFM /AESV2 /AuthEvent /DocOpen /Length 16 >> >> "
                f"/StmF /StdCF /StrF /StdCF "
                f"/O <{o_val.hex().upper()}> /U <{u_val.hex().upper()}> >>"
            )
            ver = "1.7"
    elif mode == "aes-256":
        file_key = hashlib.sha256(f"recto-aes256-fkey:{rel}".encode("utf-8")).digest()
        u_vsalt = hashlib.sha256(f"recto-uvs:{rel}".encode("utf-8")).digest()[:8]
        u_ksalt = hashlib.sha256(f"recto-uks:{rel}".encode("utf-8")).digest()[:8]
        o_vsalt = hashlib.sha256(f"recto-ovs:{rel}".encode("utf-8")).digest()[:8]
        o_ksalt = hashlib.sha256(f"recto-oks:{rel}".encode("utf-8")).digest()[:8]
        u_val = pdf_r6_hash_2b(user_pw, u_vsalt, b"") + u_vsalt + u_ksalt
        u_ikey = pdf_r6_hash_2b(user_pw, u_ksalt, b"")
        ue_val = aes_cbc_encrypt(u_ikey, b"\x00" * 16, file_key, pad=False)
        o_val = pdf_r6_hash_2b(owner_pw, o_vsalt, u_val) + o_vsalt + o_ksalt
        o_ikey = pdf_r6_hash_2b(owner_pw, o_ksalt, u_val)
        oe_val = aes_cbc_encrypt(o_ikey, b"\x00" * 16, file_key, pad=False)
        perms_pt = struct.pack("<I", p_val & 0xFFFFFFFF) + b"\xFF\xFF\xFF\xFFTadbRcto"
        perms_val = aes_encrypt_block(perms_pt, aes_expand_key(file_key))
        enc_dict = (
            f"<< /Filter /Standard /V 5 /R 6 /Length 256 /P {p_val} /EncryptMetadata true "
            f"/CF << /StdCF << /Type /CryptFilter /CFM /AESV3 /AuthEvent /DocOpen /Length 32 >> >> "
            f"/StmF /StdCF /StrF /StdCF "
            f"/O <{o_val.hex().upper()}> /U <{u_val.hex().upper()}> "
            f"/OE <{oe_val.hex().upper()}> /UE <{ue_val.hex().upper()}> "
            f"/Perms <{perms_val.hex().upper()}> >>"
        )
        ver = "2.0"
    else:
        raise ValueError(f"unknown encryption mode {mode!r}")

    enc_stream = encrypt_obj_payload(4, comp_stream, file_key)
    enc_title = encrypt_obj_payload(6, f"Recto Encrypted {mode.upper()}".encode("ascii"), file_key).hex().upper()
    enc_author = encrypt_obj_payload(6, b"Recto Corpus Generator", file_key).hex().upper()
    enc_date = encrypt_obj_payload(6, FIXED_PDF_DATE.encode("ascii"), file_key).hex().upper()

    pdf = ClassicPdfBuilder(rel, ver)
    pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
    )
    pdf.set_obj(
        4,
        f"<< /Filter /FlateDecode /Length {len(enc_stream)} >>\nstream\n".encode("ascii")
        + enc_stream
        + b"\nendstream",
    )
    pdf.set_obj(5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    pdf.set_obj(6, f"<< /Title <{enc_title}> /Author <{enc_author}> /CreationDate <{enc_date}> >>")
    pdf.set_obj(7, enc_dict)
    raw = pdf.build(root_obj=1, info_obj=6)
    return raw.replace(b"/Info 6 0 R /ID", b"/Info 6 0 R /Encrypt 7 0 R /ID")


def build_encrypted_rc4_128() -> bytes:
    return _build_encrypted_pdf("rc4-128")


def build_encrypted_aes_128() -> bytes:
    return _build_encrypted_pdf("aes-128")


def build_encrypted_aes_256() -> bytes:
    return _build_encrypted_pdf("aes-256")


# ---------------------------------------------------------------------------
# Malformed & hostile fixtures under tests/corpus/broken/
# ---------------------------------------------------------------------------

def build_broken_empty_0b() -> bytes:
    return b""


def build_broken_non_pdf_header() -> bytes:
    return (
        b"NOT_A_PDF_DOCUMENT\r\n"
        b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR-hostile-non-pdf-binary-payload\n"
    )


def build_broken_truncated_header() -> bytes:
    return b"%PDF-1."


def build_broken_truncated_stream() -> bytes:
    payload = flate_compress(b"BT /F1 14 Tf 72 720 Td (Truncated stream fixture) Tj ET")
    out = bytearray()
    out.extend(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    out.extend(b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n")
    out.extend(b"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n")
    out.extend(
        b"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R >>\nendobj\n"
    )
    out.extend(b"4 0 obj\n<< /Filter /FlateDecode /Length 512 >>\nstream\n")
    out.extend(payload[:16])
    return bytes(out)


def build_broken_corrupt_xref_recoverable() -> bytes:
    rel = "broken/corrupt-xref-recoverable.pdf"
    content = b"BT /F1 16 Tf 54 750 Td (Recovered from damaged xref via linear scan.) Tj ET\n"
    out = bytearray()
    out.extend(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    objs = {
        1: b"<< /Type /Catalog /Pages 2 0 R >>",
        2: b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        3: (
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
            b"/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>"
        ),
        4: make_stream_obj("", content, compress=True),
        5: b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
        6: f"<< /Title (Recoverable Xref Fixture) /CreationDate ({FIXED_PDF_DATE}) >>".encode("ascii"),
    }
    for obj_num in sorted(objs.keys()):
        out.extend(f"{obj_num} 0 obj\n".encode("ascii"))
        out.extend(objs[obj_num])
        out.extend(b"\nendobj\n")

    id0, id1 = deterministic_id_hex(rel, rev=0)
    out.extend(
        (
            "xref\n0 7\n"
            "0000000000 65535 f \n"
            "9999999999 00000 n \n"
            "CORRUPT_XREF_TABLE_ROW\n"
            f"trailer\n<< /Size 7 /Root 1 0 R /Info 6 0 R /ID [<{id0}> <{id1}>] >>\n"
            "startxref\n999999\n%%EOF\n"
        ).encode("ascii")
    )
    return bytes(out)


def build_broken_cyclic_xref_prev() -> bytes:
    rel = "broken/cyclic-xref-prev.pdf"
    content = b"BT /F1 14 Tf 54 750 Td (Cyclic /Prev xref chain test.) Tj ET\n"
    base = bytearray()
    base.extend(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    objs = {
        1: b"<< /Type /Catalog /Pages 2 0 R >>",
        2: b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        3: (
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
            b"/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>"
        ),
        4: make_stream_obj("", content, compress=True),
        5: b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
    }
    offsets: dict[int, int] = {}
    for obj_num in sorted(objs.keys()):
        offsets[obj_num] = len(base)
        base.extend(f"{obj_num} 0 obj\n".encode("ascii"))
        base.extend(objs[obj_num])
        base.extend(b"\nendobj\n")

    xref_rows = bytearray(b"xref\n0 6\n0000000000 65535 f \n")
    for obj_num in range(1, 6):
        xref_rows.extend(f"{offsets[obj_num]:010d} 00000 n \n".encode("ascii"))
    xref_block = bytes(xref_rows)
    id0, id1 = deterministic_id_hex(rel, rev=0)

    xref_off_1 = len(base)
    xref_off_2 = xref_off_1 + len(xref_block) + 120
    for _ in range(8):
        sec1 = (
            xref_block
            + f"trailer\n<< /Size 6 /Root 1 0 R /Prev {xref_off_2} /ID [<{id0}> <{id1}>] >>\n"
            f"startxref\n{xref_off_1}\n%%EOF\n".encode("ascii")
        )
        xref_off_2 = xref_off_1 + len(sec1)
    sec2 = (
        xref_block
        + f"trailer\n<< /Size 6 /Root 1 0 R /Prev {xref_off_1} /ID [<{id0}> <{id1}>] >>\n"
        f"startxref\n{xref_off_2}\n%%EOF\n".encode("ascii")
    )
    return bytes(base) + sec1 + sec2


def build_broken_cyclic_pages_tree() -> bytes:
    rel = "broken/cyclic-pages-tree.pdf"
    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 595 842] >>")
    pdf.set_obj(3, "<< /Type /Pages /Parent 2 0 R /Kids [2 0 R] /Count 1 >>")
    pdf.set_obj(4, f"<< /Title (Cyclic Pages Tree) /CreationDate ({FIXED_PDF_DATE}) >>")
    return pdf.build(root_obj=1, info_obj=4)


def build_broken_deep_nested_array_70() -> bytes:
    rel = "broken/deep-nested-array-70.pdf"
    nested_70 = ("[" * 70) + "42" + ("]" * 70)
    content = b"BT /F1 14 Tf 54 750 Td (Deep nested array 70 levels.) Tj ET\n"
    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R /RectoDeepNested 6 0 R >>",
    )
    pdf.set_obj(4, make_stream_obj("", content, compress=True))
    pdf.set_obj(5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    pdf.set_obj(6, f"<< /Depth70 {nested_70} >>")
    return pdf.build(root_obj=1)


def build_broken_deflate_bomb_10kb() -> bytes:
    """Build a ~10 KB PDF whose stream expands to 272 MB (> 256 MB cap) of zeros.

    RFC 1951 caps a single-pass deflate back-reference at 258 bytes per 2 bits
    (1032:1 maximum ratio, so 256 MB requires >= 254 KB in one pass). Using a
    standard PDF 2-stage [/FlateDecode /FlateDecode] stream compresses 272 MB
    into a ~9.6 KB stream (total file size ~10.2 KB), where pass 1 yields the
    277 KB single-pass deflate stream and pass 2 expands to 272 MB inside FlateDecode.
    """
    rel = "broken/deflate-bomb-10kb.pdf"
    c1 = zlib.compressobj(level=9, method=zlib.DEFLATED, wbits=15)
    chunk_16mb = b"\x00" * (16 * 1024 * 1024)
    inner_parts = [c1.compress(chunk_16mb) for _ in range(17)]  # 17 * 16 MB = 272 MB
    inner_parts.append(c1.flush())
    inner_deflate = b"".join(inner_parts)

    c2 = zlib.compressobj(level=9, method=zlib.DEFLATED, wbits=15)
    step = len(inner_deflate) // 800
    outer_parts = [
        c2.compress(inner_deflate[i : i + step]) + c2.flush(zlib.Z_FULL_FLUSH)
        for i in range(0, len(inner_deflate), step)
    ]
    outer_parts.append(c2.flush())
    outer_deflate = b"".join(outer_parts)

    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R >>",
    )
    pdf.set_obj(
        4,
        f"<< /Filter [/FlateDecode /FlateDecode] /Length {len(outer_deflate)} >>\nstream\n".encode(
            "ascii"
        )
        + outer_deflate
        + b"\nendstream",
    )
    return pdf.build(root_obj=1)


# ---------------------------------------------------------------------------
# Compact (< 4 KB) libFuzzer seed fixtures under tests/corpus/seeds/
# ---------------------------------------------------------------------------

def ascii85_encode_pdf(raw: bytes) -> bytes:
    return base64.a85encode(raw) + b"~>"


def ascii85_decode_pdf(encoded: bytes) -> bytes:
    body = encoded.strip()
    if not body.endswith(b"~>"):
        raise ValueError("ASCII85 stream missing '~>' EOD marker")
    return base64.a85decode(body[:-2])


def rle_encode_pdf(raw: bytes) -> bytes:
    out = bytearray()
    for i in range(0, len(raw), 64):
        chunk = raw[i : i + 64]
        out.append(len(chunk) - 1)
        out.extend(chunk)
    out.append(128)  # EOD
    return bytes(out)


def rle_decode_pdf(encoded: bytes) -> bytes:
    out = bytearray()
    pos = 0
    while pos < len(encoded):
        length_byte = encoded[pos]
        pos += 1
        if length_byte == 128:
            return bytes(out)
        if length_byte < 128:
            count = length_byte + 1
            if pos + count > len(encoded):
                raise ValueError("truncated literal run in RunLengthDecode")
            out.extend(encoded[pos : pos + count])
            pos += count
        else:
            count = 257 - length_byte
            if pos >= len(encoded):
                raise ValueError("truncated repeat run in RunLengthDecode")
            out.extend([encoded[pos]] * count)
            pos += 1
    raise ValueError("RunLengthDecode stream missing EOD byte 128")


def lzw_encode_literals(raw: bytes) -> bytes:
    codes = [256]
    for idx, b in enumerate(raw):
        if idx > 0 and idx % 200 == 0:
            codes.append(256)
        codes.append(b)
    codes.append(257)
    bit_buf = 0
    bit_cnt = 0
    out = bytearray()
    for code in codes:
        bit_buf = (bit_buf << 9) | (code & 0x1FF)
        bit_cnt += 9
        while bit_cnt >= 8:
            bit_cnt -= 8
            out.append((bit_buf >> bit_cnt) & 0xFF)
    if bit_cnt > 0:
        out.append((bit_buf << (8 - bit_cnt)) & 0xFF)
    return bytes(out)


def lzw_decode_literals(encoded: bytes) -> bytes:
    codes: list[int] = []
    bit_buf = 0
    bit_cnt = 0
    for b in encoded:
        bit_buf = (bit_buf << 8) | b
        bit_cnt += 8
        while bit_cnt >= 9:
            bit_cnt -= 9
            code = (bit_buf >> bit_cnt) & 0x1FF
            codes.append(code)
            if code == 257:
                break
    if not codes or codes[0] != 256 or 257 not in codes:
        raise ValueError("invalid 9-bit LZW stream in seed validator")
    out = bytearray()
    for code in codes:
        if code == 256:
            continue
        if code == 257:
            break
        if 0 <= code <= 255:
            out.append(code)
        else:
            raise ValueError(f"unexpected LZW code {code} in literal-mode seed")
    return bytes(out)


def build_seed_classic_xref() -> bytes:
    rel = "seeds/seed-classic-xref.pdf"
    content = (
        b"q 0.2 0.4 0.7 rg 36 740 180 36 re f Q\n"
        b"BT /F1 12 Tf 48 754 Td (Seed 1: Classic XRef & Primitives) Tj ET\n"
    )
    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R /PageLayout /SinglePage >>")
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R "
        "/SeedPrimitives << /BoolT true /BoolF false /NullVal null /Hex <48656C6C6F> /Arr [1 -2 3.5 (lit)] >> >>",
    )
    pdf.set_obj(4, make_stream_obj("", content, compress=True))
    pdf.set_obj(5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    pdf.set_obj(6, f"<< /Title (Seed Classic XRef) /CreationDate ({FIXED_PDF_DATE}) >>")
    return pdf.build(root_obj=1, info_obj=6)


def build_seed_xref_objstm() -> bytes:
    rel = "seeds/seed-xref-objstm.pdf"
    out = bytearray()
    out.extend(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    obj_stm_entries = [
        (1, b"<< /Type /Catalog /Pages 2 0 R >>"),
        (2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>"),
        (
            3,
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
            b"/Resources << /Font << /F1 4 0 R >> >> /Contents 6 0 R >>",
        ),
        (4, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"),
    ]
    header_parts: list[str] = []
    body_parts: list[bytes] = []
    cur = 0
    for oid, raw in obj_stm_entries:
        header_parts.append(f"{oid} {cur}")
        body_parts.append(raw)
        cur += len(raw) + 1
    header_bytes = (" ".join(header_parts) + "\n").encode("ascii")
    obj_stm_raw = header_bytes + b"\n".join(body_parts)

    type1_offsets: dict[int, int] = {}
    type1_offsets[5] = len(out)
    out.extend(b"5 0 obj\n")
    out.extend(
        make_stream_obj(
            f"/Type /ObjStm /N {len(obj_stm_entries)} /First {len(header_bytes)}",
            obj_stm_raw,
            compress=True,
        )
    )
    out.extend(b"\nendobj\n")

    type1_offsets[6] = len(out)
    out.extend(b"6 0 obj\n")
    out.extend(make_stream_obj("", b"BT /F1 12 Tf 48 750 Td (Seed 2: XRef Stream & ObjStm) Tj ET\n"))
    out.extend(b"\nendobj\n")

    xref_obj_id = 7
    xref_offset = len(out)
    type1_offsets[xref_obj_id] = xref_offset
    size = xref_obj_id + 1
    xref_raw = bytearray()
    xref_raw.extend(struct.pack(">BHH", 0, 0, 65535))
    for idx, (oid, _) in enumerate(obj_stm_entries):
        xref_raw.extend(struct.pack(">BHH", 2, 5, idx))
    for oid in (5, 6, 7):
        xref_raw.extend(struct.pack(">BHH", 1, type1_offsets[oid], 0))
    xref_comp = flate_png_up_compress(bytes(xref_raw), columns=5)
    id0, id1 = deterministic_id_hex(rel, rev=0)
    out.extend(
        f"{xref_obj_id} 0 obj\n<< /Type /XRef /Size {size} /W [1 2 2] /Root 1 0 R "
        f"/ID [<{id0}> <{id1}>] /Filter /FlateDecode "
        f"/DecodeParms << /Predictor 12 /Columns 5 >> /Length {len(xref_comp)} >>\nstream\n".encode(
            "ascii"
        )
    )
    out.extend(xref_comp)
    out.extend(f"\nendstream\nendobj\nstartxref\n{xref_offset}\n%%EOF\n".encode("ascii"))
    return bytes(out)


def build_seed_filters_ascii_rle() -> bytes:
    rel = "seeds/seed-filters-ascii-rle.pdf"
    s_hex = b"BT /F1 11 Tf 48 760 Td (Seed 3A: ASCIIHexDecode) Tj ET\n".hex().upper().encode("ascii") + b">"
    s_a85 = ascii85_encode_pdf(b"BT /F1 11 Tf 48 740 Td (Seed 3B: ASCII85Decode) Tj ET\n")
    s_rle = rle_encode_pdf(b"BT /F1 11 Tf 48 720 Td (Seed 3C: RunLengthDecode) Tj ET\n")
    s_chain = ascii85_encode_pdf(
        flate_compress(b"BT /F1 11 Tf 48 700 Td (Seed 3D: Chained A85+Flate) Tj ET\n")
    )
    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
        "/Resources << /Font << /F1 8 0 R >> >> /Contents [4 0 R 5 0 R 6 0 R 7 0 R] >>",
    )
    pdf.set_obj(
        4,
        f"<< /Filter /ASCIIHexDecode /Length {len(s_hex)} >>\nstream\n".encode("ascii")
        + s_hex
        + b"\nendstream",
    )
    pdf.set_obj(
        5,
        f"<< /Filter /ASCII85Decode /Length {len(s_a85)} >>\nstream\n".encode("ascii")
        + s_a85
        + b"\nendstream",
    )
    pdf.set_obj(
        6,
        f"<< /Filter /RunLengthDecode /Length {len(s_rle)} >>\nstream\n".encode("ascii")
        + s_rle
        + b"\nendstream",
    )
    pdf.set_obj(
        7,
        f"<< /Filter [/ASCII85Decode /FlateDecode] /Length {len(s_chain)} >>\nstream\n".encode(
            "ascii"
        )
        + s_chain
        + b"\nendstream",
    )
    pdf.set_obj(8, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    return pdf.build(root_obj=1)


def build_seed_filters_lzw_ccitt() -> bytes:
    rel = "seeds/seed-filters-lzw-ccitt.pdf"
    s_lzw = lzw_encode_literals(
        b"q 48 680 16 16 re W n 16 0 0 16 48 680 cm /Im1 Do Q\n"
        b"BT /F1 11 Tf 48 750 Td (Seed 4: LZWDecode & CCITTFaxDecode) Tj ET\n"
    )
    ccitt_bytes = make_ccitt_group4_16x16()

    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
        "/Resources << /Font << /F1 6 0 R >> /XObject << /Im1 5 0 R >> >> /Contents 4 0 R >>",
    )
    pdf.set_obj(
        4,
        f"<< /Filter /LZWDecode /DecodeParms << /EarlyChange 1 >> /Length {len(s_lzw)} >>\nstream\n".encode(
            "ascii"
        )
        + s_lzw
        + b"\nendstream",
    )
    pdf.set_obj(
        5,
        f"<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceGray "
        f"/BitsPerComponent 1 /Filter /CCITTFaxDecode "
        f"/DecodeParms << /K -1 /Columns 16 /Rows 16 >> /Length {len(ccitt_bytes)} >>\nstream\n".encode(
            "ascii"
        )
        + ccitt_bytes
        + b"\nendstream",
    )
    pdf.set_obj(6, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    return pdf.build(root_obj=1)


def build_seed_outlines_links() -> bytes:
    rel = "seeds/seed-outlines-links.pdf"
    content = b"BT /F1 12 Tf 48 750 Td (Seed 5: Outlines, Named Dests & Link Actions) Tj ET\n"
    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(
        1,
        "<< /Type /Catalog /Pages 2 0 R /Outlines 6 0 R "
        "/Names << /Dests << /Names [(sec1) [3 0 R /XYZ 48 750 0]] >> >> >>",
    )
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R "
        "/Annots [8 0 R 9 0 R 10 0 R] >>",
    )
    pdf.set_obj(4, make_stream_obj("", content, compress=True))
    pdf.set_obj(5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    pdf.set_obj(6, "<< /Type /Outlines /First 7 0 R /Last 7 0 R /Count 1 >>")
    pdf.set_obj(7, "<< /Title (Section 1) /Parent 6 0 R /Dest (sec1) >>")
    pdf.set_obj(
        8,
        "<< /Type /Annot /Subtype /Link /Rect [48 720 180 740] /Border [0 0 0] "
        "/A << /S /URI /URI (https://example.com/recto) >> >>",
    )
    pdf.set_obj(
        9,
        "<< /Type /Annot /Subtype /Link /Rect [48 695 180 715] /Border [0 0 0] "
        "/A << /S /JavaScript /JS (app.alert('inert')) >> >>",
    )
    pdf.set_obj(
        10,
        "<< /Type /Annot /Subtype /Link /Rect [48 670 180 690] /Border [0 0 0] "
        "/A << /S /Launch /F (cmd.exe) >> >>",
    )
    return pdf.build(root_obj=1)


def build_seed_render_ops() -> bytes:
    rel = "seeds/seed-render-ops.pdf"
    content = (
        b"q /GS1 gs 0.1 0.3 0.6 rg 0.8 0.2 0.1 RG 1.5 w 1 J 1 j [3 1] 0 d "
        b"48 640 m 140 640 l 160 670 120 700 90 700 c 60 700 48 670 y h B "
        b"180 640 90 60 re W* n 0.2 0.6 0.4 0.1 k 185 645 80 50 re f* Q\n"
        b"q 1 0 0 1 300 640 cm /Fm1 Do Q\n"
        b"BT /F1 12 Tf 0.5 Tc 1.0 Tw 1 0 0 1 48 760 Tm "
        b"(Seed 6: Render Ops) Tj 0 -20 Td [(Vec) -40 (tor & Text)] TJ ET\n"
    )
    form_stm = b"q 0.85 0.45 0.15 rg 0 0 60 40 re f Q"
    pdf = ClassicPdfBuilder(rel, "1.7")
    pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
    pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    pdf.set_obj(
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
        "/Resources << /Font << /F1 5 0 R >> /ExtGState << /GS1 6 0 R >> "
        "/XObject << /Fm1 7 0 R >> >> /Contents 4 0 R >>",
    )
    pdf.set_obj(4, make_stream_obj("", content, compress=True))
    pdf.set_obj(5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    pdf.set_obj(6, "<< /Type /ExtGState /CA 0.85 /ca 0.75 /BM /Normal >>")
    pdf.set_obj(
        7,
        make_stream_obj(
            "/Type /XObject /Subtype /Form /BBox [0 0 60 40] /Matrix [1 0 0 1 0 0]",
            form_stm,
            compress=True,
        ),
    )
    return pdf.build(root_obj=1)


KNOWN_FILTER_PLAINTEXT = (
    b"Recto fuzz_filters deterministic plaintext vector 0123456789ABCD"
)  # 64 bytes (divisible by 8 for PNG Up predictor columns=8)


def make_minimal_jbig2_16x16() -> bytes:
    """Create a deterministic ISO/IEC 14492 embedded JBIG2 stream (segments 48, 38, 49)."""
    # Segment 0: Page Information (type 48), page 1, 19-byte body (width=16, height=16, 72x72 dpi)
    seg0_body = struct.pack(">IIIIBBB", 16, 16, 72, 72, 0, 0, 0)
    seg0_hdr = struct.pack(">IBBBI", 0, 48, 0, 1, len(seg0_body))

    # Segment 1: Immediate Lossless Generic Region (type 38), page 1, MMR=1 (bit 0 of flags)
    mmr_data = make_ccitt_group4_16x16()
    region_info = struct.pack(">IIIIB", 16, 16, 0, 0, 0)  # 17 bytes
    generic_flags = b"\x01"  # MMR = 1
    seg1_body = region_info + generic_flags + mmr_data
    seg1_hdr = struct.pack(">IBBBI", 1, 38, 0, 1, len(seg1_body))

    # Segment 2: End of Page (type 49), page 1, 0-byte body
    seg2_hdr = struct.pack(">IBBBI", 2, 49, 0, 1, 0)
    return seg0_hdr + seg0_body + seg1_hdr + seg1_body + seg2_hdr


def make_minimal_jpx_8x8_gray() -> bytes:
    """Create a deterministic ISO/IEC 15444-1 JPEG 2000 codestream (SOC..EOC) for 8x8 8-bit gray."""
    siz_body = (
        struct.pack(">HIIIIIIII", 0, 8, 8, 0, 0, 8, 8, 0, 0)
        + struct.pack(">HBBB", 1, 7, 1, 1)
    )
    siz = b"\xFF\x51" + struct.pack(">H", len(siz_body) + 2) + siz_body
    cod_body = bytes([0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x04, 0x04, 0x00, 0x01])
    cod = b"\xFF\x52" + struct.pack(">H", len(cod_body) + 2) + cod_body
    qcd_body = bytes([0x00, 0x40])
    qcd = b"\xFF\x5C" + struct.pack(">H", len(qcd_body) + 2) + qcd_body
    tile_bitstream = b"\x80\x00"
    psot = 12 + 2 + len(tile_bitstream)
    sot = b"\xFF\x90" + struct.pack(">HHIBB", 10, 0, psot, 0, 1)
    sod = b"\xFF\x93" + tile_bitstream
    return b"\xFF\x4F" + siz + cod + qcd + sot + sod + b"\xFF\xD9"


def build_seed_filter_00_flate() -> bytes:
    return b"\x00" + flate_compress(KNOWN_FILTER_PLAINTEXT)


def build_seed_filter_01_lzw() -> bytes:
    return b"\x01" + lzw_encode_literals(KNOWN_FILTER_PLAINTEXT)


def build_seed_filter_02_predictor() -> bytes:
    return b"\x02" + flate_png_up_compress(KNOWN_FILTER_PLAINTEXT, columns=8)


def build_seed_filter_03_ascii85() -> bytes:
    return b"\x03" + ascii85_encode_pdf(KNOWN_FILTER_PLAINTEXT)


def build_seed_filter_04_asciihex() -> bytes:
    return b"\x04" + KNOWN_FILTER_PLAINTEXT.hex().upper().encode("ascii") + b">"


def build_seed_filter_05_runlength() -> bytes:
    return b"\x05" + rle_encode_pdf(KNOWN_FILTER_PLAINTEXT)


def build_seed_filter_06_ccitt() -> bytes:
    return b"\x06" + make_ccitt_group4_16x16()


def build_seed_filter_07_jbig2() -> bytes:
    return b"\x07" + make_minimal_jbig2_16x16()


def build_seed_filter_08_dct() -> bytes:
    return b"\x08" + make_minimal_jpeg_8x8_rgb()


def build_seed_filter_09_jpx() -> bytes:
    return b"\x09" + make_minimal_jpx_8x8_gray()


def build_seed_filter_0a_chain_a85_flate() -> bytes:
    return b"\x0A" + ascii85_encode_pdf(flate_compress(KNOWN_FILTER_PLAINTEXT))


ONE_PAGE_FORMS = {
    1: {b"/Type": b"/Catalog", b"/Pages": b"2 0 R"},
    2: {b"/Type": b"/Pages", b"/Kids": b"[3 0 R]", b"/Count": b"1"},
    3: {b"/Type": b"/Page", b"/Parent": b"2 0 R", b"/MediaBox": b"[0 0 595 842]",
        b"/Resources": b"<< /Font << /F1 5 0 R >> >>", b"/Contents": b"4 0 R"},
    5: {b"/Type": b"/Font", b"/Subtype": b"/Type1", b"/BaseFont": b"/Helvetica", b"/Encoding": b"/WinAnsiEncoding"},
}
# The encryption each encrypted fixture's path promises: (/V, /R), and the mode its /Info title names.
ENCRYPTED_REVISIONS = {
    "render/encrypted-rc4-128.pdf": ((2, 3), "RC4-128"),
    "render/encrypted-aes-128.pdf": ((4, 4), "AES-128"),
    "render/encrypted-aes-256.pdf": ((5, 6), "AES-256"),
}


def one_page_objects(rel_path: str, data: bytes, objects: dict) -> None:
    """Objects 1, 2, 3 and 5 are the generator's exact one-page catalog, page tree, page and font dictionaries, and
    object 4 its only stream, exactly /Filter /FlateDecode with its own /Length."""
    for num, form in ONE_PAGE_FORMS.items():
        start, end, s0 = objects[num][:3] if num in objects else (0, 0, 0)
        if s0 is not None or not data.startswith(b"<<", start) or dict_entries(data[start + 2 : end - 2]) != form:
            raise ValueError(f"{rel_path}: object {num} is not the generator's exact dictionary")
    if 4 not in objects or objects[4][2] is None:
        raise ValueError(f"{rel_path}: object 4 must be the page's content stream")
    start = objects[4][0]
    stream_dict = dict_entries(data[start + 2 : data.index(b">>\nstream\n", start)])
    if stream_dict != {b"/Filter": b"/FlateDecode", b"/Length": str(objects[4][3] - objects[4][2]).encode()}:
        raise ValueError(f"{rel_path}: the content stream must be exactly /Filter /FlateDecode with its /Length")


def validate_encrypted_pdf_structure(rel_path: str, data: bytes) -> None:
    """Validate encrypted PDF structure and verify user/owner password decryption."""
    if not (data.startswith(b"%PDF-1.7\n") or data.startswith(b"%PDF-2.0\n")):
        raise ValueError(f"{rel_path}: missing valid %PDF- header")
    eof_m = re.search(rb"startxref\n(\d+)\n%%EOF\n\Z", data)
    if eof_m is None:
        raise ValueError(f"{rel_path}: missing startxref before %%EOF")
    validate_classic_xref_at(rel_path, data, int(eof_m.group(1)), set())

    # The same structure checks as every other fixture (objects, xref coverage and liveness, /Size, the catalog,
    # layout), with the stream content left to the decryption below.
    validate_pdf_structure(rel_path, data, encrypted=True)
    # Everything below is read from parsed objects: the trailer's own /Encrypt and /ID, the dictionary that /Encrypt
    # names, and the page content /Root reaches, never a text search for an object number.
    objects = {num: (start, end, s0, s1) for num, start, end, s0, s1, _ in iter_objects(rel_path, data)}

    def entries(num: int) -> dict[bytes, bytes]:
        if num not in objects or not data.startswith(b"<<", objects[num][0]):
            raise ValueError(f"{rel_path}: object {num} is not a dictionary")
        start, end = objects[num][:2]
        return dict_entries(data[start + 2 : end - 2])

    def ref(value: bytes, what: str) -> int:
        m = re.fullmatch(rb"(\d+) 0 R", value or b"")
        if m is None:
            raise ValueError(f"{rel_path}: {what} is not an indirect reference")
        return int(m.group(1))

    def whole(value: bytes, what: str, signed: bool = False) -> int:
        if re.fullmatch(rb"-?\d+" if signed else rb"\d+", value or b"") is None:
            raise ValueError(f"{rel_path}: {what} is not an integer")
        return int(value)

    def hex_string(value: bytes, what: str) -> bytes:
        m = re.fullmatch(rb"<([0-9A-F]+)>", value or b"")
        if m is None or len(m.group(1)) % 2:
            raise ValueError(f"{rel_path}: {what} is not a hex string")
        return bytes.fromhex(m.group(1).decode("ascii"))

    trailer_at = data.rindex(b"trailer\n<<")
    trailer = dict_entries(data[trailer_at + 10 : _skip_object(data, trailer_at + 8) - 2])
    id_m = re.fullmatch(rb"\[\s*<([0-9A-F]{32})>\s*<([0-9A-F]{32})>\s*\]", trailer.get(b"/ID", b""))
    if id_m is None:
        raise ValueError(f"{rel_path}: missing trailer /ID array")
    id0_bytes = bytes.fromhex(id_m.group(1).decode("ascii"))

    enc = entries(ref(trailer.get(b"/Encrypt", b""), "trailer /Encrypt"))
    if enc.get(b"/Filter") != b"/Standard":
        raise ValueError(f"{rel_path}: the /Encrypt dictionary is not /Filter /Standard")
    v_val = whole(enc.get(b"/V"), "/V")
    r_val = whole(enc.get(b"/R"), "/R")
    p_val = whole(enc.get(b"/P"), "/P", signed=True)
    # /P is a 32-bit two's-complement field whose reserved bits 7, 8 and 13 to 32 must be 1 and bits 1 and 2 must be 0
    # (ISO 32000-1 Table 22), and every encrypted fixture is documented (tests/corpus/README.md) as /P -1028. The value
    # is the documented contract's, not the generator's constant, so a changed constant is caught.
    bits = p_val & 0xFFFFFFFF
    if not -(2**31) <= p_val < 2**31 or bits & 0x3 or bits & 0xC0 != 0xC0 or bits & 0xFFFFF000 != 0xFFFFF000:
        raise ValueError(f"{rel_path}: /P {p_val} breaks the reserved permission bits")
    if p_val != DOCUMENTED_PERMISSIONS:
        raise ValueError(f"{rel_path}: /P {p_val} is not the documented {DOCUMENTED_PERMISSIONS}")
    o_bytes = hex_string(enc.get(b"/O"), "/O")
    u_bytes = hex_string(enc.get(b"/U"), "/U")
    # Each revision the corpus writes has exactly one form, and every entry that feeds key derivation or
    # authentication (ISO 32000-1 7.6.3, Algorithms 2, 2.A, 2.B) must be the value the decryption below assumes: a
    # 128-bit key for RC4 and AES-128, 256 for AES-256, metadata encrypted, and crypt filters as declared here.
    forms = {
        (2, 3): ({b"/Filter", b"/V", b"/R", b"/Length", b"/P", b"/O", b"/U"}, b"128", None, None),
        (4, 4): ({b"/Filter", b"/V", b"/R", b"/Length", b"/P", b"/EncryptMetadata", b"/CF", b"/StmF", b"/StrF", b"/O",
                  b"/U"}, b"128", b"/AESV2", b"16"),
        (5, 6): ({b"/Filter", b"/V", b"/R", b"/Length", b"/P", b"/EncryptMetadata", b"/CF", b"/StmF", b"/StrF", b"/O",
                  b"/U", b"/OE", b"/UE", b"/Perms"}, b"256", b"/AESV3", b"32"),
    }
    if (v_val, r_val) not in forms:
        raise ValueError(f"{rel_path}: unsupported (/V {v_val}, /R {r_val})")
    promised = ENCRYPTED_REVISIONS.get(rel_path)
    if promised is None or promised[0] != (v_val, r_val):
        raise ValueError(f"{rel_path}: (/V {v_val}, /R {r_val}) is not the revision this fixture's path promises")
    keys, key_bits, expected_cfm, cf_length = forms[(v_val, r_val)]
    if set(enc) != keys:
        raise ValueError(f"{rel_path}: the /V {v_val} /Encrypt dictionary must hold exactly {sorted(k.decode() for k in keys)}")
    if enc[b"/Length"] != key_bits:
        raise ValueError(f"{rel_path}: /Length {enc[b'/Length'].decode()} is not the {key_bits.decode()}-bit key the "
                         f"corpus derives")
    if expected_cfm is not None and enc[b"/EncryptMetadata"] != b"true":
        raise ValueError(f"{rel_path}: /EncryptMetadata must be true, which the key derivation and /Perms assume")
    if expected_cfm is None:
        if any(k in enc for k in (b"/CF", b"/StmF", b"/StrF")):
            raise ValueError(f"{rel_path}: an RC4 (/V 2) dictionary declares crypt filters it does not use")
    else:
        cf = enc.get(b"/CF", b"")
        if not cf.startswith(b"<<"):
            raise ValueError(f"{rel_path}: /V {v_val} needs a /CF dictionary")
        filters = dict_entries(cf[2:-2])
        std = filters.get(b"/StdCF", b"")
        std_entries = dict_entries(std[2:-2]) if std.startswith(b"<<") else {}
        if (set(filters) != {b"/StdCF"}
                or std_entries != {b"/Type": b"/CryptFilter", b"/CFM": expected_cfm, b"/AuthEvent": b"/DocOpen",
                                   b"/Length": cf_length}
                or enc.get(b"/StmF") != b"/StdCF" or enc.get(b"/StrF") != b"/StdCF"):
            raise ValueError(f"{rel_path}: /V {v_val} must declare /StdCF with /CFM {expected_cfm.decode()} as both "
                             f"/StmF and /StrF")

    if r_val in (3, 4):
        if len(o_bytes) != 32 or len(u_bytes) != 32:
            raise ValueError(f"{rel_path}: R={r_val} /O and /U must be 32 bytes")
        user_fkey = pdf_r34_compute_file_key(DOCUMENTED_USER_PASSWORD, o_bytes, p_val, id0_bytes)
        if pdf_r34_compute_u(user_fkey, id0_bytes)[:16] != u_bytes[:16]:
            raise ValueError(f"{rel_path}: R={r_val} user password authentication failed")
        recovered_padded_user = pdf_r34_unwrap_owner(DOCUMENTED_OWNER_PASSWORD, o_bytes)
        owner_fkey = pdf_r34_compute_file_key(recovered_padded_user, o_bytes, p_val, id0_bytes)
        if owner_fkey != user_fkey:
            raise ValueError(f"{rel_path}: R={r_val} owner password unwrapped wrong file key")
        wrong_fkey = pdf_r34_compute_file_key(b"wrong-password", o_bytes, p_val, id0_bytes)
        if pdf_r34_compute_u(wrong_fkey, id0_bytes)[:16] == u_bytes[:16]:
            raise ValueError(f"{rel_path}: R={r_val} unexpectedly accepted wrong password")
        file_key = user_fkey
    elif r_val == 6 and v_val == 5:
        if len(o_bytes) != 48 or len(u_bytes) != 48:
            raise ValueError(f"{rel_path}: R=6 /O and /U must be 48 bytes")
        oe_bytes = hex_string(enc.get(b"/OE"), "/OE")
        ue_bytes = hex_string(enc.get(b"/UE"), "/UE")
        perms_bytes = hex_string(enc.get(b"/Perms"), "/Perms")
        if (len(oe_bytes), len(ue_bytes), len(perms_bytes)) != (32, 32, 16):
            raise ValueError(f"{rel_path}: R=6 /OE, /UE and /Perms must be 32, 32 and 16 bytes")
        if pdf_r6_hash_2b(DOCUMENTED_USER_PASSWORD, u_bytes[32:40], b"") != u_bytes[:32]:
            raise ValueError(f"{rel_path}: R=6 user password /U verification failed")
        if pdf_r6_hash_2b(DOCUMENTED_OWNER_PASSWORD, o_bytes[32:40], u_bytes) != o_bytes[:32]:
            raise ValueError(f"{rel_path}: R=6 owner password /O verification failed")
        u_ikey = pdf_r6_hash_2b(DOCUMENTED_USER_PASSWORD, u_bytes[40:48], b"")
        o_ikey = pdf_r6_hash_2b(DOCUMENTED_OWNER_PASSWORD, o_bytes[40:48], u_bytes)
        user_fkey = aes_cbc_decrypt(u_ikey, b"\x00" * 16, ue_bytes, unpad=False)
        owner_fkey = aes_cbc_decrypt(o_ikey, b"\x00" * 16, oe_bytes, unpad=False)
        if user_fkey != owner_fkey:
            raise ValueError(f"{rel_path}: R=6 /UE and /OE unwrapped different file keys")
        perms_pt = aes_decrypt_block(perms_bytes, aes_expand_key(user_fkey))
        if perms_pt[9:12] != b"adb" or perms_pt[8:9] != b"T":
            raise ValueError(f"{rel_path}: R=6 /Perms signature verification failed")
        perms_p = struct.unpack("<i", perms_pt[:4])[0]
        if perms_p != p_val:
            raise ValueError(f"{rel_path}: R=6 /Perms permissions {perms_p} != /P {p_val}")
        file_key = user_fkey
    else:
        raise ValueError(f"{rel_path}: unsupported (/V {v_val}, /R {r_val})")

    # The objects around the encryption have one exact form each, as the generator writes them: a catalog, one page
    # tree with one page, its font, its Flate content stream (object 4) and the encrypted /Info strings. Anything else,
    # a changed /Type, a different /Filter or an extra entry, is not this corpus's file.
    trailer_keys = {b"/Size", b"/Root", b"/Info", b"/Encrypt", b"/ID"}
    if set(trailer) != trailer_keys or (trailer[b"/Root"], trailer[b"/Info"]) != (b"1 0 R", b"6 0 R"):
        raise ValueError(f"{rel_path}: the trailer must hold exactly /Size, /Root 1 0 R, /Info 6 0 R, /Encrypt, /ID")
    if sorted(objects) != [1, 2, 3, 4, 5, 6, 7] or objects[7][2] is not None:
        raise ValueError(f"{rel_path}: expected objects 1 to 7 with object 4 the only stream")
    one_page_objects(rel_path, data, objects)
    info = entries(6)
    if set(info) != {b"/Title", b"/Author", b"/CreationDate"}:
        raise ValueError(f"{rel_path}: /Info must hold exactly /Title, /Author and /CreationDate")
    enc_payload = data[objects[4][2] : objects[4][3]]
    def decrypt(obj_num: int, payload: bytes) -> bytes:
        if r_val == 3:
            return rc4_crypt(pdf_r34_object_key(file_key, obj_num, 0, is_aes=False), payload)
        key = pdf_r34_object_key(file_key, obj_num, 0, is_aes=True) if r_val == 4 else file_key
        if len(payload) < 32 or len(payload) % 16:
            raise ValueError(f"{rel_path}: object {obj_num}'s AES payload is not an IV and whole blocks")
        try:
            return aes_cbc_decrypt(key, payload[:16], payload[16:], unpad=True)
        except ValueError as exc:
            raise ValueError(f"{rel_path}: object {obj_num} does not decrypt: {exc}") from exc

    # Every encrypted string decrypts (AES padding included) to exactly what the generator wrote.
    expected_info = {b"/Title": f"Recto Encrypted {promised[1]}".encode("ascii"),
                     b"/Author": b"Recto Corpus Generator", b"/CreationDate": DOCUMENTED_PDF_DATE.encode("ascii")}
    for key, plain in expected_info.items():
        if decrypt(6, hex_string(info[key], f"/Info {key.decode()}")) != plain:
            raise ValueError(f"{rel_path}: /Info {key.decode()} does not decrypt to the generator's value")
    comp_payload = decrypt(4, enc_payload)

    raw_content = inflate(comp_payload, rel_path)
    validate_page_content(rel_path, raw_content, ONE_PAGE_FORMS[3][b"/Resources"])


def inflate_bounded(data: bytes, limit: int, keep: bool = True) -> tuple[int, bytes]:
    """(bytes produced, the bytes when keep) inflating data, stopping as soon as more than limit bytes have come out;
    output is produced 1 MiB at a time, so memory stays bounded whatever the input claims."""
    d = zlib.decompressobj()
    total, out, pending = 0, [], data
    while total <= limit:
        chunk = d.decompress(pending, 1 << 20)
        total += len(chunk)
        if keep and total <= limit:
            out.append(chunk)
        pending = d.unconsumed_tail
        if not chunk and not pending:
            break
    return total, b"".join(out)


def exact_objects(rel_path: str, data: bytes, forms: dict[int, dict[bytes, bytes]], streams: set[int]) -> dict:
    """The parsed objects of a fixture, refusing unless they are exactly the numbers in forms, each object's dictionary
    equal to its form (values as parsed, so a literal string can never stand in for an array), and exactly the objects
    in streams carrying a stream. Returns {num: (start, end, stream_start, stream_end)}."""
    objects = {num: (start, end, s0, s1) for num, start, end, s0, s1, _ in iter_objects(rel_path, data)}
    if sorted(objects) != sorted(forms):
        raise ValueError(f"{rel_path}: expected objects {sorted(forms)}, got {sorted(objects)}")
    for num, form in forms.items():
        start, end, s0, s1 = objects[num]
        if (s0 is not None) != (num in streams) or not data.startswith(b"<<", start):
            raise ValueError(f"{rel_path}: object {num} is not the generator's exact dictionary")
        body_end = data.index(b">>\nstream\n", start) + 2 if s0 is not None else end
        entries = dict_entries(data[start + 2 : body_end - 2])
        if s0 is not None and form.get(b"/Length") == b"*":
            form = {**form, b"/Length": str(s1 - s0).encode()}  # a stream's /Length is its own measured length
        if entries != form:
            raise ValueError(f"{rel_path}: object {num} is not the generator's exact dictionary")
    return objects


def validate_broken_pdf_structure(rel_path: str, data: bytes) -> None:
    """Validate that each broken/ fixture contains its exact expected fault."""
    if rel_path == "broken/empty-0b.pdf":
        if len(data) != 0:
            raise ValueError(f"{rel_path}: expected 0 bytes, got {len(data)}")
        return
    if rel_path == "broken/non-pdf-header.pdf":
        if len(data) == 0 or b"%PDF-" in data:
            raise ValueError(f"{rel_path}: must be non-empty and contain no %PDF- header")
        return
    if rel_path == "broken/truncated-header.pdf":
        if data != b"%PDF-1.":
            raise ValueError(f"{rel_path}: expected truncated b'%PDF-1.' header")
        return
    if rel_path == "broken/truncated-stream.pdf":
        # The generator's three-object page, then object 4 cut off inside a stream that declares 512 bytes.
        prefix = (b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
                  b"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
                  b"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R >>\nendobj\n"
                  b"4 0 obj\n<< /Filter /FlateDecode /Length 512 >>\nstream\n")
        rest = data[len(prefix) :]
        if not data.startswith(prefix) or not 0 < len(rest) < 512 or b"endstream" in rest or b"%%EOF" in rest:
            raise ValueError(f"{rel_path}: invalid truncated-stream structure")
        return
    if rel_path == "broken/corrupt-xref-recoverable.pdf":
        eof_m = re.search(rb"startxref\n(\d+)\n%%EOF\n\Z", data)
        if eof_m is None or int(eof_m.group(1)) < len(data):
            raise ValueError(f"{rel_path}: startxref must be deliberately out-of-bounds")
        # What a recovering reader finds by scanning: the generator's one-page document, objects 1 to 6, back to back
        # from the header, then the deliberately broken xref table and a trailer whose /Root and /Info name them.
        walked = list(iter_objects(rel_path, data))
        objects = {num: (start, end, s0, s1) for num, start, end, s0, s1, _ in walked}
        if [w[0] for w in walked] != [1, 2, 3, 4, 5, 6]:
            raise ValueError(f"{rel_path}: expected recoverable objects 1..6, got {[w[0] for w in walked]}")
        header = b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n"
        if not data.startswith(header) or walked[0][5] != len(header):
            raise ValueError(f"{rel_path}: must start with the exact %PDF-1.7 header and binary comment, then object 1")
        # Each object ends with its own endobj line, and the next begins right after it: no byte between them.
        ends = [data.index(b"endobj\n", s1 if s1 is not None else end) + len(b"endobj\n")
                for _, _, end, _, s1, _ in walked]
        if [w[5] for w in walked[1:]] != ends[:-1]:
            raise ValueError(f"{rel_path}: the recoverable objects must sit back to back")
        one_page_objects(rel_path, data, objects)
        info_start, info_end = objects[6][:2]
        if dict_entries(data[info_start + 2 : info_end - 2]) != {b"/Title": b"(Recoverable Xref Fixture)",
                                                                b"/CreationDate": f"({DOCUMENTED_PDF_DATE})".encode()}:
            raise ValueError(f"{rel_path}: object 6 is not the generator's /Info dictionary")
        tail = data[ends[-1] :]
        if re.fullmatch(rb"xref\n0 7\n0000000000 65535 f \n9999999999 00000 n \nCORRUPT_XREF_TABLE_ROW\ntrailer\n"
                        rb"<< /Size 7 /Root 1 0 R /Info 6 0 R /ID \[<([0-9A-F]{32})> <\1>\] >>\n"
                        rb"startxref\n999999\n%%EOF\n", tail) is None:
            raise ValueError(f"{rel_path}: the tail must be exactly the deliberately broken xref and its trailer")
        dec = inflate(data[objects[4][2] : objects[4][3]], rel_path)
        validate_page_content(rel_path, dec, ONE_PAGE_FORMS[3][b"/Resources"])
        return
    if rel_path == "broken/cyclic-xref-prev.pdf":
        eof_m = re.search(rb"startxref\n(\d+)\n%%EOF\n\Z", data)
        if eof_m is None:
            raise ValueError(f"{rel_path}: missing final startxref")
        try:
            validate_classic_xref_at(rel_path, data, int(eof_m.group(1)), set())
        except ValueError as exc:
            if "cyclic /Prev xref offset" in str(exc):
                return
            raise
        raise ValueError(f"{rel_path}: expected cyclic /Prev xref offset error")
    if rel_path == "broken/cyclic-pages-tree.pdf":
        validate_pdf_structure(rel_path, data)
        exact_objects(rel_path, data, {
            1: ONE_PAGE_FORMS[1],
            2: {b"/Type": b"/Pages", b"/Kids": b"[3 0 R]", b"/Count": b"1", b"/MediaBox": b"[0 0 595 842]"},
            3: {b"/Type": b"/Pages", b"/Parent": b"2 0 R", b"/Kids": b"[2 0 R]", b"/Count": b"1"},  # the 2 -> 3 -> 2 cycle
            4: {b"/Title": b"(Cyclic Pages Tree)", b"/CreationDate": f"({DOCUMENTED_PDF_DATE})".encode()},
        }, streams=set())
        return
    if rel_path == "broken/deep-nested-array-70.pdf":
        validate_pdf_structure(rel_path, data)
        exact_objects(rel_path, data, {
            1: ONE_PAGE_FORMS[1], 2: ONE_PAGE_FORMS[2], 5: ONE_PAGE_FORMS[5],
            3: {**ONE_PAGE_FORMS[3], b"/RectoDeepNested": b"6 0 R"},
            4: {b"/Filter": b"/FlateDecode", b"/Length": b"*"},
            6: {b"/Depth70": (b"[" * 70) + b"42" + (b"]" * 70)},  # parsed as a value: an array 70 deep, never a string
        }, streams={4})
        objects = {num: (s0, s1) for num, _, _, s0, s1, _ in iter_objects(rel_path, data)}
        validate_page_content(rel_path, inflate(data[objects[4][0] : objects[4][1]], rel_path),
                              ONE_PAGE_FORMS[3][b"/Resources"])
        return
    if rel_path == "broken/deflate-bomb-10kb.pdf":
        validate_pdf_structure(rel_path, data)
        if not (8_000 <= len(data) <= 16_384):
            raise ValueError(f"{rel_path}: expected ~10 KB file size, got {len(data)} bytes")
        objects = exact_objects(rel_path, data, {
            1: ONE_PAGE_FORMS[1], 2: ONE_PAGE_FORMS[2],
            3: {b"/Type": b"/Page", b"/Parent": b"2 0 R", b"/MediaBox": b"[0 0 595 842]", b"/Contents": b"4 0 R"},
            4: {b"/Filter": b"[/FlateDecode /FlateDecode]", b"/Length": b"*"},  # the two-stage bomb
        }, streams={4})
        # Each stage inflates in bounded steps (1 MiB of output at a time) and stops once past its limit, so checking the
        # bomb never allocates what it is checking for. (inflate_bounded only counts; unlike inflate() it does not
        # require a complete stream, since the inner stage is deliberately never inflated to its end.)
        inner_total, inner_deflate = inflate_bounded(data[objects[4][2] : objects[4][3]], 16 * 1024 * 1024)
        if inner_total > 16 * 1024 * 1024:
            raise ValueError(f"{rel_path}: the outer FlateDecode stage expands past 16 MiB")
        total_expanded, _ = inflate_bounded(inner_deflate, DOCUMENTED_STREAM_CEILING, keep=False)
        if total_expanded <= DOCUMENTED_STREAM_CEILING:
            raise ValueError(
                f"{rel_path}: expanded size {total_expanded} did not exceed 256 MB cap"
            )
        return
    raise ValueError(f"unknown broken fixture {rel_path}")


# What each seed exists to exercise, pinned as the parsed dictionary of every object it holds (a stream's /Length is its
# own measured length, "*"): the filters, outlines, link actions, image parameters and primitives a fuzzer starts from.
# A seed that lost its construct, or one seed standing in for another, no longer matches.
SEED_FORMS: dict[str, dict[int, dict[bytes, bytes]]] = {
    "seeds/seed-classic-xref.pdf": {
        1: {b"/Type": b"/Catalog", b"/Pages": b"2 0 R", b"/PageLayout": b"/SinglePage"},
        2: {b"/Type": b"/Pages", b"/Kids": b"[3 0 R]", b"/Count": b"1"},
        3: {
            b"/Type": b"/Page",
            b"/Parent": b"2 0 R",
            b"/MediaBox": b"[0 0 595 842]",
            b"/Resources": b"<< /Font << /F1 5 0 R >> >>",
            b"/Contents": b"4 0 R",
            b"/SeedPrimitives": b"<< /BoolT true /BoolF false /NullVal null /Hex <48656C6C6F> /Arr [1 -2 3.5 (lit)] >>",
        },
        4: {b"/Filter": b"/FlateDecode", b"/Length": b"*"},
        5: {b"/Type": b"/Font", b"/Subtype": b"/Type1", b"/BaseFont": b"/Helvetica", b"/Encoding": b"/WinAnsiEncoding"},
        6: {b"/Title": b"(Seed Classic XRef)", b"/CreationDate": b"(D:20261013000000Z)"},
    },
    "seeds/seed-filters-ascii-rle.pdf": {
        1: {b"/Type": b"/Catalog", b"/Pages": b"2 0 R"},
        2: {b"/Type": b"/Pages", b"/Kids": b"[3 0 R]", b"/Count": b"1"},
        3: {
            b"/Type": b"/Page",
            b"/Parent": b"2 0 R",
            b"/MediaBox": b"[0 0 595 842]",
            b"/Resources": b"<< /Font << /F1 8 0 R >> >>",
            b"/Contents": b"[4 0 R 5 0 R 6 0 R 7 0 R]",
        },
        4: {b"/Filter": b"/ASCIIHexDecode", b"/Length": b"*"},
        5: {b"/Filter": b"/ASCII85Decode", b"/Length": b"*"},
        6: {b"/Filter": b"/RunLengthDecode", b"/Length": b"*"},
        7: {b"/Filter": b"[/ASCII85Decode /FlateDecode]", b"/Length": b"*"},
        8: {b"/Type": b"/Font", b"/Subtype": b"/Type1", b"/BaseFont": b"/Helvetica", b"/Encoding": b"/WinAnsiEncoding"},
    },
    "seeds/seed-filters-lzw-ccitt.pdf": {
        1: {b"/Type": b"/Catalog", b"/Pages": b"2 0 R"},
        2: {b"/Type": b"/Pages", b"/Kids": b"[3 0 R]", b"/Count": b"1"},
        3: {
            b"/Type": b"/Page",
            b"/Parent": b"2 0 R",
            b"/MediaBox": b"[0 0 595 842]",
            b"/Resources": b"<< /Font << /F1 6 0 R >> /XObject << /Im1 5 0 R >> >>",
            b"/Contents": b"4 0 R",
        },
        4: {b"/Filter": b"/LZWDecode", b"/DecodeParms": b"<< /EarlyChange 1 >>", b"/Length": b"*"},
        5: {
            b"/Type": b"/XObject",
            b"/Subtype": b"/Image",
            b"/Width": b"16",
            b"/Height": b"16",
            b"/ColorSpace": b"/DeviceGray",
            b"/BitsPerComponent": b"1",
            b"/Filter": b"/CCITTFaxDecode",
            b"/DecodeParms": b"<< /K -1 /Columns 16 /Rows 16 >>",
            b"/Length": b"*",
        },
        6: {b"/Type": b"/Font", b"/Subtype": b"/Type1", b"/BaseFont": b"/Helvetica", b"/Encoding": b"/WinAnsiEncoding"},
    },
    "seeds/seed-outlines-links.pdf": {
        1: {
            b"/Type": b"/Catalog",
            b"/Pages": b"2 0 R",
            b"/Outlines": b"6 0 R",
            b"/Names": b"<< /Dests << /Names [(sec1) [3 0 R /XYZ 48 750 0]] >> >>",
        },
        2: {b"/Type": b"/Pages", b"/Kids": b"[3 0 R]", b"/Count": b"1"},
        3: {
            b"/Type": b"/Page",
            b"/Parent": b"2 0 R",
            b"/MediaBox": b"[0 0 595 842]",
            b"/Resources": b"<< /Font << /F1 5 0 R >> >>",
            b"/Contents": b"4 0 R",
            b"/Annots": b"[8 0 R 9 0 R 10 0 R]",
        },
        4: {b"/Filter": b"/FlateDecode", b"/Length": b"*"},
        5: {b"/Type": b"/Font", b"/Subtype": b"/Type1", b"/BaseFont": b"/Helvetica", b"/Encoding": b"/WinAnsiEncoding"},
        6: {b"/Type": b"/Outlines", b"/First": b"7 0 R", b"/Last": b"7 0 R", b"/Count": b"1"},
        7: {b"/Title": b"(Section 1)", b"/Parent": b"6 0 R", b"/Dest": b"(sec1)"},
        8: {
            b"/Type": b"/Annot",
            b"/Subtype": b"/Link",
            b"/Rect": b"[48 720 180 740]",
            b"/Border": b"[0 0 0]",
            b"/A": b"<< /S /URI /URI (https://example.com/recto) >>",
        },
        9: {
            b"/Type": b"/Annot",
            b"/Subtype": b"/Link",
            b"/Rect": b"[48 695 180 715]",
            b"/Border": b"[0 0 0]",
            b"/A": b"<< /S /JavaScript /JS (app.alert('inert')) >>",
        },
        10: {
            b"/Type": b"/Annot",
            b"/Subtype": b"/Link",
            b"/Rect": b"[48 670 180 690]",
            b"/Border": b"[0 0 0]",
            b"/A": b"<< /S /Launch /F (cmd.exe) >>",
        },
    },
    "seeds/seed-render-ops.pdf": {
        1: {b"/Type": b"/Catalog", b"/Pages": b"2 0 R"},
        2: {b"/Type": b"/Pages", b"/Kids": b"[3 0 R]", b"/Count": b"1"},
        3: {
            b"/Type": b"/Page",
            b"/Parent": b"2 0 R",
            b"/MediaBox": b"[0 0 595 842]",
            b"/Resources": b"<< /Font << /F1 5 0 R >> /ExtGState << /GS1 6 0 R >> /XObject << /Fm1 7 0 R >> >>",
            b"/Contents": b"4 0 R",
        },
        4: {b"/Filter": b"/FlateDecode", b"/Length": b"*"},
        5: {b"/Type": b"/Font", b"/Subtype": b"/Type1", b"/BaseFont": b"/Helvetica", b"/Encoding": b"/WinAnsiEncoding"},
        6: {b"/Type": b"/ExtGState", b"/CA": b"0.85", b"/ca": b"0.75", b"/BM": b"/Normal"},
        7: {
            b"/Type": b"/XObject",
            b"/Subtype": b"/Form",
            b"/BBox": b"[0 0 60 40]",
            b"/Matrix": b"[1 0 0 1 0 0]",
            b"/Filter": b"/FlateDecode",
            b"/Length": b"*",
        },
    },
    "seeds/seed-xref-objstm.pdf": {
        5: {b"/Type": b"/ObjStm", b"/N": b"4", b"/First": b"20", b"/Filter": b"/FlateDecode", b"/Length": b"*"},
        6: {b"/Filter": b"/FlateDecode", b"/Length": b"*"},
        7: {
            b"/Type": b"/XRef",
            b"/Size": b"8",
            b"/W": b"[1 2 2]",
            b"/Root": b"1 0 R",
            b"/ID": b"[<03BC1CB155BB474CEEE97428B5BB6A9F> <03BC1CB155BB474CEEE97428B5BB6A9F>]",
            b"/Filter": b"/FlateDecode",
            b"/DecodeParms": b"<< /Predictor 12 /Columns 5 >>",
            b"/Length": b"*",
        },
    },
}
# The cross-reference form each seed exercises: a classic table, or (the object-stream seed) an /XRef stream.
SEED_XREF_STREAM = {"seeds/seed-xref-objstm.pdf"}
# The objects the object-stream seed keeps inside its /ObjStm (object 5), as parsed dictionaries.
SEED_OBJSTM_FORMS: dict[int, dict[bytes, bytes]] = {
    1: {b"/Type": b"/Catalog", b"/Pages": b"2 0 R"},
    2: {b"/Type": b"/Pages", b"/Kids": b"[3 0 R]", b"/Count": b"1"},
    3: {b"/Type": b"/Page", b"/Parent": b"2 0 R", b"/MediaBox": b"[0 0 595 842]",
        b"/Resources": b"<< /Font << /F1 4 0 R >> >>", b"/Contents": b"6 0 R"},
    4: {b"/Type": b"/Font", b"/Subtype": b"/Type1", b"/BaseFont": b"/Helvetica", b"/Encoding": b"/WinAnsiEncoding"},
}
# The operators the render-ops seed's page content and form XObject use: path construction and painting, clipping,
# colour, graphics state, text and XObject operators (ISO 32000-1 Table 51).
SEED_RENDER_OPERATORS = {
    4: {b"q", b"Q", b"cm", b"gs", b"rg", b"RG", b"k", b"w", b"J", b"j", b"d", b"m", b"l", b"c", b"y", b"h", b"B",
        b"re", b"W*", b"n", b"f*", b"Do", b"BT", b"Tf", b"Tc", b"Tw", b"Tm", b"Tj", b"Td", b"TJ", b"ET"},
    7: {b"q", b"Q", b"rg", b"re", b"f"},
}
CONTENT_DELIMITERS = b"()<>[]{}/%"
CONTENT_WHITESPACE = b" \t\r\n\f\x00"
# Exactly the operators the content of the seed, encrypted and broken fixtures uses, each with the kinds of its operands in order (ISO 32000-1 Tables 51,
# 57, 59, 74, 105 and 107): n number, i integer, N name, s string, a an array of numbers (a dash pattern), A an array
# of strings and numbers (TJ). Any other operator refuses, so an operator the seeds never exercise (marked content,
# for one) cannot slip in unchecked.
CONTENT_OPERANDS: dict[bytes, str] = {
    b"q": "", b"Q": "", b"cm": "nnnnnn", b"w": "n", b"J": "i", b"j": "i", b"d": "an", b"gs": "N",
    b"m": "nn", b"l": "nn", b"c": "nnnnnn", b"y": "nnnn", b"h": "", b"re": "nnnn",
    b"f": "", b"f*": "", b"B": "", b"n": "", b"W": "", b"W*": "",
    b"rg": "nnn", b"RG": "nnn", b"k": "nnnn",
    b"BT": "", b"ET": "", b"Tc": "n", b"Tw": "n", b"Tf": "Nn", b"Td": "nn", b"Tm": "nnnnnn", b"Tj": "s", b"TJ": "A",
    b"Do": "N",
}
PATH_CONSTRUCTION = {b"m", b"l", b"c", b"y", b"h", b"re"}
PATH_PAINTING = {b"f", b"f*", b"B", b"n"}
TEXT_SHOWING = {b"Td", b"Tm", b"Tj", b"TJ"}  # only inside BT ... ET
# What a text object may contain (ISO 32000-1 Figure 9): general graphics state, colour, text state, and text
# positioning and showing; never q, Q, an XObject or a path.
TEXT_OBJECT_ALLOWED = {b"w", b"J", b"j", b"d", b"gs", b"rg", b"RG", b"k", b"Tc", b"Tw", b"Tf", b"ET"} | TEXT_SHOWING


def _content_operand(rel_path: str, raw: bytes, i: int) -> tuple[str, int]:
    """(kind, end) of the operand starting at raw[i]: s, a, d, N, n, i, or "k" for a bare keyword (an operator)."""
    b = raw[i]
    if b == 0x28:  # literal string, balanced, with escapes
        depth, i = 1, i + 1
        while i < len(raw) and depth:
            if raw[i] == 0x5C:
                i += 2
                continue
            depth += {0x28: 1, 0x29: -1}.get(raw[i], 0)
            i += 1
        if depth:
            raise ValueError(f"{rel_path}: unterminated string in content stream")
        return "s", i
    if raw.startswith(b"<<", i) or b == 0x5B:  # dictionary or array: its operands, to the matching close
        close, i = (b">>", i + 2) if b == 0x3C else (b"]", i + 1)
        elements = ""
        while True:
            while i < len(raw) and raw[i] in CONTENT_WHITESPACE:
                i += 1
            if i >= len(raw):
                raise ValueError(f"{rel_path}: unterminated {'dictionary' if close == b'>>' else 'array'}")
            if raw.startswith(close, i):
                # An array's kind lists its elements' kinds ("a:sns"), so an operator can require what they are.
                return ("d" if close == b">>" else "a:" + elements), i + len(close)
            kind, i = _content_operand(rel_path, raw, i)
            if kind == "k":
                raise ValueError(f"{rel_path}: an operator inside an array or dictionary operand")
            elements += kind[0]
    if b == 0x3C:  # hex string
        close = raw.find(b">", i)
        if close < 0 or re.fullmatch(rb"[0-9A-Fa-f\s]*", raw[i + 1 : close]) is None:
            raise ValueError(f"{rel_path}: bad hex string in content stream")
        return "s", close + 1
    j = i + 1 if b == 0x2F else i
    while j < len(raw) and raw[j] not in CONTENT_WHITESPACE and raw[j] not in CONTENT_DELIMITERS:
        j += 1
    if j == i:
        raise ValueError(f"{rel_path}: unexpected byte {b:#04x} in content stream")
    token = raw[i:j]
    if b == 0x2F:
        return "N", j
    if re.fullmatch(rb"[+-]?\d+", token):
        return "i", j
    if re.fullmatch(rb"[+-]?(?:\d+\.\d*|\.\d+)", token):
        return "n", j
    if token in (b"true", b"false", b"null"):
        return "o", j
    return "k", j


def content_instructions(rel_path: str, raw: bytes) -> list[tuple[bytes, list[tuple[str, bytes]]]]:
    """The instructions of a content stream, in order: each operator with its operands as (kind, bytes). Strings,
    arrays and dictionaries are read whole, so nothing inside an operand is ever taken for an operator."""
    out: list[tuple[bytes, list[tuple[str, bytes]]]] = []
    operands: list[tuple[str, bytes]] = []
    i = 0
    while i < len(raw):
        if raw[i] in CONTENT_WHITESPACE:
            i += 1
            continue
        if raw[i] == 0x25:  # % comment to end of line
            while i < len(raw) and raw[i] not in b"\r\n":
                i += 1
            continue
        kind, j = _content_operand(rel_path, raw, i)
        if kind == "k":
            out.append((raw[i:j], operands))
            operands = []
        else:
            operands.append((kind, raw[i:j]))
        i = j
    if operands:
        raise ValueError(f"{rel_path}: operands with no operator at the end of the content stream")
    return out


def content_operators(rel_path: str, raw: bytes) -> list[bytes]:
    """The operators of a content stream, in order (see content_instructions)."""
    return [op for op, _ in content_instructions(rel_path, raw)]


def validate_content_instructions(rel_path: str, raw: bytes, resources: dict[bytes, set[bytes]]) -> list[bytes]:
    """Refuse content that is not executable as written: an unknown operator, wrong operand count or kinds, q/Q or
    BT/ET out of balance, a text-showing operator outside BT ... ET, a path not ended by a painting operator (with
    nothing else between), or a font, XObject or graphics-state name that the stream's resources do not define
    (resources maps /Font, /XObject and /ExtGState to their names). Returns the operators."""
    # in_path: "" (no path), "building" (construction under way) or "clipped" (W or W* given; ISO 32000-1 8.5.4 puts
    # clipping after the last construction operator, so only a painting operator may follow).
    saves, in_text, in_path = 0, False, ""
    fonts: list[bytes | None] = [None]  # the text font is graphics state: q saves it, Q restores it, nothing sets a default
    ops = []
    for op, operands in content_instructions(rel_path, raw):
        shape = CONTENT_OPERANDS.get(op)
        if shape is None:
            raise ValueError(f"{rel_path}: operator {op.decode()} is not one the corpus content uses")
        kinds = "".join(k[0] for k, _ in operands)
        want = shape.replace("A", "a")
        fits = len(kinds) == len(want) and all(
            k == w or (w == "n" and k == "i") for k, w in zip(kinds, want))
        for (kind, _), w in zip(operands, shape):
            elements = kind[2:] if kind.startswith("a:") else ""
            if (w == "a" and set(elements) - set("ni")) or (w == "A" and (not elements or set(elements) - set("nis"))):
                fits = False
        if not fits:
            raise ValueError(f"{rel_path}: {op.decode()} takes operands {shape!r}, got "
                             f"{[k for k, _ in operands]!r}")
        _check_operand_values(rel_path, op, operands)
        if in_path == "building" and op not in PATH_CONSTRUCTION | PATH_PAINTING | {b"W", b"W*"}:
            raise ValueError(f"{rel_path}: {op.decode()} inside a path before its painting operator")
        if in_path == "clipped" and op not in PATH_PAINTING:
            raise ValueError(f"{rel_path}: {op.decode()} after W or W*, where only a painting operator may follow")
        if op in PATH_CONSTRUCTION:
            if op not in (b"m", b"re") and not in_path:
                raise ValueError(f"{rel_path}: {op.decode()} with no current path")
            in_path = "building"
        elif op in (b"W", b"W*"):
            if in_path != "building":
                raise ValueError(f"{rel_path}: {op.decode()} with no path to clip")
            in_path = "clipped"
        elif op in PATH_PAINTING:
            if not in_path:
                raise ValueError(f"{rel_path}: {op.decode()} with no path to paint")
            in_path = ""
        elif op == b"q":
            saves += 1
            fonts.append(fonts[-1])
        elif op == b"Q":
            saves -= 1
            if saves < 0:
                raise ValueError(f"{rel_path}: Q with no matching q")
            fonts.pop()
        elif op == b"BT":
            if in_text:
                raise ValueError(f"{rel_path}: BT inside a text object")
            in_text = True
        elif op == b"ET":
            if not in_text:
                raise ValueError(f"{rel_path}: ET with no text object")
            in_text = False
        elif op in TEXT_SHOWING and not in_text:
            raise ValueError(f"{rel_path}: {op.decode()} outside BT ... ET")
        if in_text and op != b"BT" and op not in TEXT_OBJECT_ALLOWED:  # BT itself just opened the object
            raise ValueError(f"{rel_path}: {op.decode()} inside BT ... ET")
        if op == b"Tf":
            fonts[-1] = operands[0][1]
        elif op in (b"Tj", b"TJ") and fonts[-1] is None:
            raise ValueError(f"{rel_path}: {op.decode()} shows text before any Tf selects a font")
        category = {b"Tf": b"/Font", b"Do": b"/XObject", b"gs": b"/ExtGState"}.get(op)
        if category is not None and operands[0][1] not in resources.get(category, set()):
            raise ValueError(f"{rel_path}: {op.decode()} names {operands[0][1].decode()}, which its resources lack")
        ops.append(op)
    if saves or in_text or in_path:
        raise ValueError(f"{rel_path}: content ends inside q, BT or a path")
    return ops


def _check_operand_values(rel_path: str, op: bytes, operands: list[tuple[str, bytes]]) -> None:
    """Refuse operand values outside what the operator accepts (ISO 32000-1 8.4.3 and 8.6.4): colour components from 0
    to 1, a line cap or join style from 0 to 2, a non-negative line width, and dash lengths that are non-negative and
    not all zero."""
    def numbers(values):
        return [float(v) for _, v in values]

    if op in (b"rg", b"RG", b"k") and not all(0 <= x <= 1 for x in numbers(operands)):
        raise ValueError(f"{rel_path}: {op.decode()} colour components must lie from 0 to 1")
    if op in (b"J", b"j") and int(operands[0][1]) not in (0, 1, 2):
        raise ValueError(f"{rel_path}: {op.decode()} style must be 0, 1 or 2")
    if op == b"w" and float(operands[0][1]) < 0:
        raise ValueError(f"{rel_path}: w line width must not be negative")
    if op == b"d":
        dashes = [float(x) for x in operands[0][1][1:-1].split()]
        if any(x < 0 for x in dashes) or (dashes and not any(dashes)):
            raise ValueError(f"{rel_path}: d dash lengths must be non-negative and not all zero")


def validate_page_content(rel_path: str, content: bytes, resources: bytes) -> list[bytes]:
    """Decoded page content checked as executable with the page's /Resources value (see
    validate_content_instructions), after the literal-string and text-position checks."""
    validate_content_stream_syntax(rel_path, content)
    return validate_content_instructions(rel_path, content, resource_names(resources))


def resource_names(resources: bytes) -> dict[bytes, set[bytes]]:
    """{/Font: names, /XObject: names, /ExtGState: names} from a /Resources dictionary value."""
    if not resources.startswith(b"<<"):
        return {}
    out = {}
    for category, value in dict_entries(resources[2:-2]).items():
        if value.startswith(b"<<"):
            out[category] = set(dict_entries(value[2:-2]))
    return out


def decode_seed_stream(rel_path: str, entries: dict[bytes, bytes], payload: bytes) -> bytes | None:
    """A seed stream's decoded content for the filters the seeds exercise, or None for a stream that is not page
    content (an image, an object or cross-reference stream)."""
    if entries.get(b"/Type") in (b"/ObjStm", b"/XRef") or entries.get(b"/Subtype") == b"/Image":
        return None
    flt = entries.get(b"/Filter")
    if flt == b"/FlateDecode":
        return inflate(payload, rel_path)
    if flt == b"/ASCIIHexDecode":
        if not payload.endswith(b">"):
            raise ValueError(f"{rel_path}: ASCIIHexDecode stream missing '>' EOD")
        return bytes.fromhex(payload[:-1].decode("ascii"))
    if flt == b"/ASCII85Decode":
        return ascii85_decode_pdf(payload)
    if flt == b"/RunLengthDecode":
        return rle_decode_pdf(payload)
    if flt == b"[/ASCII85Decode /FlateDecode]":
        return inflate(ascii85_decode_pdf(payload), rel_path)
    if flt == b"/LZWDecode":
        return lzw_decode_literals(payload)
    raise ValueError(f"{rel_path}: no seed content stream uses /Filter {flt!r}")


def validate_seed_pdf_structure(rel_path: str, data: bytes) -> None:
    if len(data) >= DOCUMENTED_SEED_MAX_BYTES:
        raise ValueError(
            f"{rel_path}: seed size {len(data)} must be < {DOCUMENTED_SEED_MAX_BYTES} bytes"
        )
    validate_pdf_structure(rel_path, data)
    if rel_path not in SEED_FORMS:
        raise ValueError(f"{rel_path}: not a known seed")
    forms = SEED_FORMS[rel_path]
    streams = {num for num, form in forms.items() if form.get(b"/Length") == b"*"}
    objects = exact_objects(rel_path, data, forms, streams)
    xref_at = int(re.search(rb"startxref\n(\d+)\n%%EOF\n\Z", data).group(1))
    if data.startswith(b"xref\n", xref_at) == (rel_path in SEED_XREF_STREAM):
        kind = "an /XRef stream" if rel_path in SEED_XREF_STREAM else "a classic xref table"
        raise ValueError(f"{rel_path}: the seed must use {kind}")
    # Which resources each content stream runs with: the page's for the streams its /Contents names, a form
    # XObject's own (none, when it has no /Resources) for the form.
    stream_resources: dict[int, dict[bytes, set[bytes]]] = {}
    for num, form in forms.items():
        if form.get(b"/Type") == b"/Page":
            for ref_m in re.finditer(rb"(\d+) 0 R", form[b"/Contents"]):
                stream_resources[int(ref_m.group(1))] = resource_names(form.get(b"/Resources", b""))
        elif form.get(b"/Subtype") == b"/Form":
            stream_resources[num] = resource_names(form.get(b"/Resources", b""))
    if rel_path == "seeds/seed-xref-objstm.pdf":
        stream_resources[6] = {b"/Font": {b"/F1"}}  # the page lives in the object stream (SEED_OBJSTM_FORMS[3])
    for num in sorted(streams):
        start, _, s0, s1 = objects[num]
        entries = {**forms[num], b"/Length": str(s1 - s0).encode()}
        content = decode_seed_stream(rel_path, entries, data[s0:s1])
        if content is None:
            continue
        validate_content_stream_syntax(rel_path, content)
        if num not in stream_resources:
            raise ValueError(f"{rel_path}: content stream {num} is neither page content nor a form XObject")
        ops = validate_content_instructions(rel_path, content, stream_resources[num])
        expected = SEED_RENDER_OPERATORS.get(num) if rel_path == "seeds/seed-render-ops.pdf" else None
        if expected is not None and set(ops) != expected:
            raise ValueError(f"{rel_path}: object {num} uses operators {sorted(set(ops))}, not {sorted(expected)}")
        if expected is None and not {b"BT", b"Tf", b"ET"} <= set(ops):
            raise ValueError(f"{rel_path}: object {num} decodes to no text block")
    if rel_path == "seeds/seed-xref-objstm.pdf":
        _, _, s0, s1 = objects[5]
        dec = inflate(data[s0:s1], rel_path)
        first = int(SEED_FORMS[rel_path][5][b"/First"])
        header = dec[:first].split()
        nums, offsets = [int(x) for x in header[0::2]], [int(x) for x in header[1::2]]
        if nums != sorted(SEED_OBJSTM_FORMS):
            raise ValueError(f"{rel_path}: the object stream must hold objects {sorted(SEED_OBJSTM_FORMS)}")
        for num, off in zip(nums, offsets):
            pos = first + off
            end = _skip_object(dec, pos)
            if not dec.startswith(b"<<", pos) or dict_entries(dec[pos + 2 : end - 2]) != SEED_OBJSTM_FORMS[num]:
                raise ValueError(f"{rel_path}: object {num} in the object stream is not the generator's exact dictionary")


SEED_FILTER_SELECTORS: dict[str, int] = {
    "seeds/seed-filter-00-flate.bin": 0x00,
    "seeds/seed-filter-01-lzw.bin": 0x01,
    "seeds/seed-filter-02-predictor.bin": 0x02,
    "seeds/seed-filter-03-ascii85.bin": 0x03,
    "seeds/seed-filter-04-asciihex.bin": 0x04,
    "seeds/seed-filter-05-runlength.bin": 0x05,
    "seeds/seed-filter-06-ccitt.bin": 0x06,
    "seeds/seed-filter-07-jbig2.bin": 0x07,
    "seeds/seed-filter-08-dct.bin": 0x08,
    "seeds/seed-filter-09-jpx.bin": 0x09,
    "seeds/seed-filter-0a-chain-a85-flate.bin": 0x0A,
}


def validate_seed_filter_bin_structure(rel_path: str, data: bytes) -> None:
    """Validate selector-prefixed raw filter seed files for fuzz_filters (D01 T03 §4)."""
    if len(data) < 2 or len(data) >= DOCUMENTED_SEED_MAX_BYTES:
        raise ValueError(
            f"{rel_path}: filter seed size {len(data)} must be in 2..{DOCUMENTED_SEED_MAX_BYTES - 1}"
        )
    expected_sel = SEED_FILTER_SELECTORS.get(rel_path)
    if expected_sel is None or data[0] != expected_sel:
        raise ValueError(
            f"{rel_path}: selector byte 0x{data[0]:02X} != expected {expected_sel!r}"
        )
    payload = data[1:]
    if expected_sel == 0x00:
        dec = inflate(payload, rel_path)
        if dec != KNOWN_FILTER_PLAINTEXT:
            raise ValueError(f"{rel_path}: FlateDecode output mismatch")
    elif expected_sel == 0x01:
        dec = lzw_decode_literals(payload)
        if dec != KNOWN_FILTER_PLAINTEXT:
            raise ValueError(f"{rel_path}: LZWDecode output mismatch")
    elif expected_sel == 0x02:
        dec = flate_png_up_decompress(payload, columns=8)
        if dec != KNOWN_FILTER_PLAINTEXT:
            raise ValueError(f"{rel_path}: PNG Up Predictor output mismatch")
    elif expected_sel == 0x03:
        dec = ascii85_decode_pdf(payload)
        if dec != KNOWN_FILTER_PLAINTEXT:
            raise ValueError(f"{rel_path}: ASCII85Decode output mismatch")
    elif expected_sel == 0x04:
        if not payload.endswith(b">"):
            raise ValueError(f"{rel_path}: ASCIIHexDecode missing '>' EOD")
        dec = bytes.fromhex(payload[:-1].decode("ascii"))
        if dec != KNOWN_FILTER_PLAINTEXT:
            raise ValueError(f"{rel_path}: ASCIIHexDecode output mismatch")
    elif expected_sel == 0x05:
        dec = rle_decode_pdf(payload)
        if dec != KNOWN_FILTER_PLAINTEXT:
            raise ValueError(f"{rel_path}: RunLengthDecode output mismatch")
    elif expected_sel == 0x06:
        if payload != make_ccitt_group4_16x16():
            raise ValueError(f"{rel_path}: CCITTFaxDecode Group 4 payload mismatch")
    elif expected_sel == 0x07:
        if payload != make_minimal_jbig2_16x16() or make_ccitt_group4_16x16() not in payload:
            raise ValueError(f"{rel_path}: JBIG2Decode payload mismatch")
    elif expected_sel == 0x08:
        if not (payload.startswith(b"\xFF\xD8") and payload.endswith(b"\xFF\xD9")):
            raise ValueError(f"{rel_path}: DCTDecode JPEG missing SOI/EOI markers")
    elif expected_sel == 0x09:
        if not (payload.startswith(b"\xFF\x4F\xFF\x51") and payload.endswith(b"\xFF\xD9")):
            raise ValueError(f"{rel_path}: JPXDecode codestream missing SOC/SIZ/EOC markers")
    elif expected_sel == 0x0A:
        dec = inflate(ascii85_decode_pdf(payload), rel_path)
        if dec != KNOWN_FILTER_PLAINTEXT:
            raise ValueError(f"{rel_path}: Chained A85+Flate output mismatch")


def validate_corpus_entry(rel_path: str, data: bytes) -> None:
    if rel_path.startswith("broken/"):
        validate_broken_pdf_structure(rel_path, data)
    elif rel_path.startswith("render/encrypted-"):
        validate_encrypted_pdf_structure(rel_path, data)
    elif rel_path.startswith("seeds/") and rel_path.endswith(".bin"):
        validate_seed_filter_bin_structure(rel_path, data)
    elif rel_path.startswith("seeds/"):
        validate_seed_pdf_structure(rel_path, data)
    elif rel_path.endswith(".pdf"):
        validate_pdf_structure(rel_path, data)
    elif rel_path.endswith((".docx", ".xlsx")):
        validate_openxml_structure(rel_path, data)
    else:
        raise ValueError(f"unrecognized corpus entry {rel_path}")


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


CORPUS_PURPOSES: dict[str, str] = {
    "render/simple-1p.pdf": "Single-page PDF 1.7 with Helvetica text and stroked/filled vector rectangle",
    "render/compressed-xref-2p.pdf": "Two-page PDF 1.7 using Type 2 ObjStm and Flate-compressed /XRef stream",
    "render/incremental-2rev.pdf": "Two-revision incremental PDF with /Prev xref chain overriding page 1 content",
    "render/rotated-pages.pdf": "Four-page PDF exercising /Rotate 0, 90, 180, and 270 with mixed /MediaBox and /CropBox",
    "render/images-and-vectors.pdf": "Vector Bézier paths, 8x8 RGB/Gray/1-bit XObjects, Form XObject, and /SMask alpha",
    "render/encrypted-rc4-128.pdf": "Standard Security Handler V=2 R=3 128-bit RC4 encrypted PDF with user/owner passwords",
    "render/encrypted-aes-128.pdf": "Standard Security Handler V=4 R=4 128-bit AES-CBC (/CF /StdCF /AESV2) encrypted PDF",
    "render/encrypted-aes-256.pdf": "PDF 2.0 Standard Security Handler V=5 R=6 256-bit AES-CBC (Algorithm 2.B + /Perms) PDF",
    "text/two-column-links-outline.pdf": "Two-column article layout with hyphenation, /Outlines tree, and /URI + /Dest links",
    "text/scanned.pdf": "Image-only scanned 300 DPI page without /Font resources for OCR pipeline testing",
    "convert/tables-and-forms.pdf": "Ruled 4-row, 3-column table with a merged header row, a borderless 3x3 table, and AcroForm /Tx, /Btn, and /Ch fields",
    "convert/sample.docx": "Minimal ECMA-376 WordprocessingML document with two plain paragraphs",
    "convert/sample.xlsx": "Minimal ECMA-376 SpreadsheetML workbook with inline-string and numeric cells",
    "broken/empty-0b.pdf": "Zero-byte file testing empty-input error handling",
    "broken/non-pdf-header.pdf": "Non-PDF plain-text header testing magic-header rejection",
    "broken/truncated-header.pdf": "Truncated 6-byte %PDF-1 header testing short-read rejection",
    "broken/truncated-stream.pdf": "Stream truncated mid-payload before endstream testing stream bounds enforcement",
    "broken/corrupt-xref-recoverable.pdf": "Corrupted startxref offset with intact indirect objects for linear scan recovery",
    "broken/cyclic-xref-prev.pdf": "Two-trailer /Prev cycle (offset A <-> offset B) testing xref loop detection",
    "broken/cyclic-pages-tree.pdf": "Pages tree /Kids cycle (2 0 R <-> 3 0 R) testing page-tree cycle rejection",
    "broken/deep-nested-array-70.pdf": "70-level nested PDF array exceeding the 64-level recursion guard",
    "broken/deflate-bomb-10kb.pdf": "10 KB 2-stage Flate stream expanding to 272 MB (> 256 MB decoded stream ceiling)",
    "seeds/seed-classic-xref.pdf": "Compact (< 4 KB) libFuzzer seed for classic uncompressed xref table parsing",
    "seeds/seed-xref-objstm.pdf": "Compact (< 4 KB) libFuzzer seed for /XRef stream and /ObjStm object streams",
    "seeds/seed-filters-ascii-rle.pdf": "Compact (< 4 KB) libFuzzer seed for ASCIIHexDecode, ASCII85Decode, and RunLengthDecode",
    "seeds/seed-filters-lzw-ccitt.pdf": "Compact (< 4 KB) libFuzzer seed for LZWDecode and CCITTFaxDecode Group 4 streams",
    "seeds/seed-outlines-links.pdf": "Compact (< 4 KB) libFuzzer seed for /Outlines bookmarks and /Link annotations",
    "seeds/seed-render-ops.pdf": "Compact (< 4 KB) libFuzzer seed for content-stream path, text, color, and state ops",
    "seeds/seed-filter-00-flate.bin": "Selector 0x00 raw libFuzzer seed for FlateDecode decompression",
    "seeds/seed-filter-01-lzw.bin": "Selector 0x01 raw libFuzzer seed for LZWDecode decompression",
    "seeds/seed-filter-02-predictor.bin": "Selector 0x02 raw libFuzzer seed for FlateDecode with PNG Up Predictor=12",
    "seeds/seed-filter-03-ascii85.bin": "Selector 0x03 raw libFuzzer seed for ASCII85Decode filter",
    "seeds/seed-filter-04-asciihex.bin": "Selector 0x04 raw libFuzzer seed for ASCIIHexDecode filter",
    "seeds/seed-filter-05-runlength.bin": "Selector 0x05 raw libFuzzer seed for RunLengthDecode filter",
    "seeds/seed-filter-06-ccitt.bin": "Selector 0x06 raw libFuzzer seed for CCITTFaxDecode Group 4 (K=-1) filter",
    "seeds/seed-filter-07-jbig2.bin": "Selector 0x07 raw libFuzzer seed for JBIG2Decode symbol-dictionary and page segments",
    "seeds/seed-filter-08-dct.bin": "Selector 0x08 raw libFuzzer seed for DCTDecode baseline JPEG 8x8 grayscale stream",
    "seeds/seed-filter-09-jpx.bin": "Selector 0x09 raw libFuzzer seed for JPXDecode minimal JPEG 2000 codestream",
    "seeds/seed-filter-0a-chain-a85-flate.bin": "Selector 0x0A raw libFuzzer seed for chained ASCII85Decode + FlateDecode pipeline",
}


def expected_corpus_files() -> dict[str, bytes]:
    """Return mapping of relative corpus path -> deterministic file bytes."""
    files: dict[str, bytes] = {
        "render/simple-1p.pdf": build_simple_1p(),
        "render/compressed-xref-2p.pdf": build_compressed_xref_2p(),
        "render/incremental-2rev.pdf": build_incremental_2rev(),
        "render/rotated-pages.pdf": build_rotated_pages(),
        "render/images-and-vectors.pdf": build_images_and_vectors(),
        "render/encrypted-rc4-128.pdf": build_encrypted_rc4_128(),
        "render/encrypted-aes-128.pdf": build_encrypted_aes_128(),
        "render/encrypted-aes-256.pdf": build_encrypted_aes_256(),
        "text/two-column-links-outline.pdf": build_two_column_links_outline(),
        "text/scanned.pdf": build_scanned(),
        "convert/tables-and-forms.pdf": build_tables_and_forms(),
        "convert/sample.docx": build_sample_docx(),
        "convert/sample.xlsx": build_sample_xlsx(),
        "broken/empty-0b.pdf": build_broken_empty_0b(),
        "broken/non-pdf-header.pdf": build_broken_non_pdf_header(),
        "broken/truncated-header.pdf": build_broken_truncated_header(),
        "broken/truncated-stream.pdf": build_broken_truncated_stream(),
        "broken/corrupt-xref-recoverable.pdf": build_broken_corrupt_xref_recoverable(),
        "broken/cyclic-xref-prev.pdf": build_broken_cyclic_xref_prev(),
        "broken/cyclic-pages-tree.pdf": build_broken_cyclic_pages_tree(),
        "broken/deep-nested-array-70.pdf": build_broken_deep_nested_array_70(),
        "broken/deflate-bomb-10kb.pdf": build_broken_deflate_bomb_10kb(),
        "seeds/seed-classic-xref.pdf": build_seed_classic_xref(),
        "seeds/seed-xref-objstm.pdf": build_seed_xref_objstm(),
        "seeds/seed-filters-ascii-rle.pdf": build_seed_filters_ascii_rle(),
        "seeds/seed-filters-lzw-ccitt.pdf": build_seed_filters_lzw_ccitt(),
        "seeds/seed-outlines-links.pdf": build_seed_outlines_links(),
        "seeds/seed-render-ops.pdf": build_seed_render_ops(),
        "seeds/seed-filter-00-flate.bin": build_seed_filter_00_flate(),
        "seeds/seed-filter-01-lzw.bin": build_seed_filter_01_lzw(),
        "seeds/seed-filter-02-predictor.bin": build_seed_filter_02_predictor(),
        "seeds/seed-filter-03-ascii85.bin": build_seed_filter_03_ascii85(),
        "seeds/seed-filter-04-asciihex.bin": build_seed_filter_04_asciihex(),
        "seeds/seed-filter-05-runlength.bin": build_seed_filter_05_runlength(),
        "seeds/seed-filter-06-ccitt.bin": build_seed_filter_06_ccitt(),
        "seeds/seed-filter-07-jbig2.bin": build_seed_filter_07_jbig2(),
        "seeds/seed-filter-08-dct.bin": build_seed_filter_08_dct(),
        "seeds/seed-filter-09-jpx.bin": build_seed_filter_09_jpx(),
        "seeds/seed-filter-0a-chain-a85-flate.bin": build_seed_filter_0a_chain_a85_flate(),
    }
    if set(files.keys()) != set(CORPUS_PURPOSES.keys()):
        missing = sorted(set(files.keys()) - set(CORPUS_PURPOSES.keys()))
        extra = sorted(set(CORPUS_PURPOSES.keys()) - set(files.keys()))
        raise ValueError(f"CORPUS_PURPOSES mismatch: missing={missing}, extra={extra}")

    total_bytes = 0
    for rel_path, content in files.items():
        total_bytes += len(content)
        validate_corpus_entry(rel_path, content)

    if total_bytes >= MAX_CORPUS_TOTAL_BYTES:
        raise ValueError(
            f"corpus total size {total_bytes} exceeds limit {MAX_CORPUS_TOTAL_BYTES}"
        )
    return files


def build_provenance_markdown(files: dict[str, bytes] | None = None) -> bytes:
    """Build deterministic UTF-8 bytes for tests/corpus/PROVENANCE.md."""
    if files is None:
        files = expected_corpus_files()
    lines: list[str] = [
        "# Test Corpus Provenance (`tests/corpus/PROVENANCE.md`)",
        "",
        "Every file under `tests/corpus/` (excluding `README.md` and `PROVENANCE.md`) is synthetically generated by `scripts/build-corpus.py` and dedicated to the public domain under `CC0-1.0`.",
        "",
        "`Corpus.Provenance` in `tests/recto-tests/corpus_tests.cpp` parses the table below and verifies that the set of files on disk matches 1-for-1 and that every file's Windows CNG SHA-256 digest matches.",
        "",
        "| Path | SHA-256 | License | Origin | Purpose |",
        "| ---- | ------- | ------- | ------ | ------- |",
    ]
    for rel_path in sorted(files.keys()):
        digest = hashlib.sha256(files[rel_path]).hexdigest()
        purpose = CORPUS_PURPOSES.get(rel_path, "").strip()
        if not purpose or "|" in purpose or "\n" in purpose:
            raise ValueError(f"invalid purpose description for {rel_path}")
        lines.append(
            f"| {rel_path} | {digest} | CC0-1.0 | scripts/build-corpus.py | {purpose} |"
        )
    lines.append("")
    return "\n".join(lines).encode("utf-8")


def generate_corpus(corpus_root: Path = CORPUS_ROOT) -> dict[str, str]:
    """Generate all corpus files and PROVENANCE.md atomically and return {rel_path: sha256_hex}."""
    files = expected_corpus_files()
    hashes: dict[str, str] = {}
    for rel_path, data in sorted(files.items()):
        dest = corpus_root / rel_path
        write_atomic(dest, data)
        hashes[rel_path] = hashlib.sha256(data).hexdigest()
    write_atomic(corpus_root / "PROVENANCE.md", build_provenance_markdown(files))
    return hashes


def _is_reparse_or_symlink_stat(st: os.stat_result) -> bool:
    if stat.S_ISLNK(st.st_mode):
        return True
    if getattr(st, "st_reparse_tag", 0) != 0:
        return True
    file_attrs = getattr(st, "st_file_attributes", 0)
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x0400)
    return bool(file_attrs & reparse_flag)


def check_corpus(corpus_root: Path = CORPUS_ROOT) -> tuple[bool, list[str]]:
    """Verify on-disk corpus files and PROVENANCE.md match deterministic generator output."""
    files = expected_corpus_files()
    errors: list[str] = []
    total_on_disk = 0

    try:
        root_lstat = os.lstat(corpus_root)
    except OSError as exc:
        errors.append(f"corpus directory stat error for {corpus_root}: {exc}")
        return (False, errors)

    if _is_reparse_or_symlink_stat(root_lstat):
        errors.append(f"corpus root must not be a symlink or reparse point: {corpus_root}")
        return (False, errors)
    if not stat.S_ISDIR(root_lstat.st_mode):
        errors.append(f"corpus root is not a directory: {corpus_root}")
        return (False, errors)

    for rel_path, expected_bytes in sorted(files.items()):
        dest = corpus_root / rel_path
        try:
            st = os.lstat(dest)
        except OSError:
            errors.append(f"missing corpus file: {rel_path}")
            continue
        if _is_reparse_or_symlink_stat(st) or not stat.S_ISREG(st.st_mode):
            errors.append(f"non-regular or symlink corpus file: {rel_path}")
            continue
        try:
            actual_bytes = dest.read_bytes()
        except OSError as exc:
            errors.append(f"read error for {rel_path}: {exc}")
            continue
        total_on_disk += len(actual_bytes)
        try:
            validate_corpus_entry(rel_path, actual_bytes)
        except ValueError as exc:
            errors.append(str(exc))
        if actual_bytes != expected_bytes:
            exp_hash = hashlib.sha256(expected_bytes).hexdigest()
            act_hash = hashlib.sha256(actual_bytes).hexdigest()
            errors.append(
                f"byte mismatch for {rel_path}: expected sha256 {exp_hash}, got {act_hash}"
            )

    provenance_path = corpus_root / "PROVENANCE.md"
    expected_provenance = build_provenance_markdown(files)
    try:
        prov_st = os.lstat(provenance_path)
        if _is_reparse_or_symlink_stat(prov_st) or not stat.S_ISREG(prov_st.st_mode):
            errors.append("PROVENANCE.md must be a regular non-symlink file")
        elif provenance_path.read_bytes() != expected_provenance:
            errors.append("PROVENANCE.md is out of date with generated corpus fixtures")
    except OSError:
        errors.append("missing provenance file: PROVENANCE.md")

    def _walk_corpus_dir(current_dir: Path, rel_prefix: str) -> None:
        try:
            with os.scandir(current_dir) as it:
                entries = sorted(list(it), key=lambda e: e.name)
        except OSError as exc:
            loc = rel_prefix if rel_prefix else "."
            errors.append(f"directory enumeration error in {loc}: {exc}")
            return

        for entry in entries:
            rel = f"{rel_prefix}/{entry.name}" if rel_prefix else entry.name
            try:
                entry_lstat = entry.stat(follow_symlinks=False)
                if entry.is_symlink() or _is_reparse_or_symlink_stat(entry_lstat):
                    errors.append(f"symlink or reparse point not allowed in corpus: {rel}")
                    continue
                if stat.S_ISDIR(entry_lstat.st_mode):
                    _walk_corpus_dir(Path(entry.path), rel)
                elif stat.S_ISREG(entry_lstat.st_mode):
                    if rel in ("README.md", "PROVENANCE.md"):
                        continue
                    if rel not in files:
                        errors.append(f"unexpected corpus file: {rel}")
                else:
                    errors.append(f"non-regular file entry in corpus: {rel}")
            except OSError as exc:
                errors.append(f"filesystem stat error for {rel}: {exc}")

    _walk_corpus_dir(corpus_root, "")

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
