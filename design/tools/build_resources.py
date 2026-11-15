r"""Builds every icon and graphic Recto ships into resources/.

Usage (from the repository root):
    build\python.cmd design/tools/build_resources.py --newsreader <path to Newsreader[opsz,wght].ttf>
    ./scripts/run-python.sh design/tools/build_resources.py --newsreader <path to Newsreader[opsz,wght].ttf>  (Linux/WSL)

Writes:
    resources/svg/            vector masters (app icon, file-type icon, wordmark, lockups, in-app graphics)
    resources/msix/Assets/    PNGs named and sized for Package.appxmanifest
    resources/ico/            recto.ico and pdf-file.ico (Inno Setup builds, window icon)
    resources/installer/      Inno Setup WizardImageFile and WizardSmallImageFile PNGs at every scale
    design/brand/             the GitHub README banner and social preview
    resources/preview.png     a contact sheet of everything above

Text in the wordmark and the "PDF" label is converted to outlines, so the app does not need to
ship Newsreader or Segoe UI. PNGs are rasterised by headless Edge (or Chrome) from one sprite sheet
and cut apart using Python standard library PNG/ICO helpers; ICO files are assembled from those PNGs
so every size is hand-picked, not downscaled from 256 px.
"""

import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import tempfile
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "resources"
BRAND_OUT = ROOT / "design" / "brand"
CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
TOKENS = json.loads((ROOT / "design" / "tokens" / "colors.json").read_text(encoding="utf-8"))
L, D, B = TOKENS["light"], TOKENS["dark"], TOKENS["brand"]
ILL, PAGE_DARK = TOKENS["illustration"], TOKENS["pageColourInDarkTheme"]
REPOSITORY = "github.com/rizonesoft/Recto"
# Inno Setup image areas at each display scale with the default font (WizardImageFile and WizardSmallImageFile help):
# the large area keeps a 164:314 aspect ratio, the small one is square, and both grow with font metrics, not DPI.
INSTALLER_SIZES = {100: ((202, 386), (58, 58)), 125: ((269, 515), (77, 77)), 150: ((336, 643), (97, 97)),
                   175: ((403, 772), (116, 116)), 200: ((430, 824), (124, 124)), 225: ((498, 953), (143, 143)),
                   250: ((534, 1022), (159, 159))}


def write_bytes_atomically(path: Path, data: bytes) -> None:
    """Write `data` to `path` via a temp file in `path.parent` and atomic `os.replace`."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=f".tmp-{path.name}-", dir=path.parent)
    tmp_path = Path(tmp_name)
    try:
        with os.fdopen(fd, "wb") as fh:
            fh.write(data)
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp_path, path)
    finally:
        tmp_path.unlink(missing_ok=True)


def write_text_atomically(path: Path, text: str) -> None:
    """Atomically write UTF-8 text to `path`."""
    write_bytes_atomically(path, text.encode("utf-8"))


def paint(token):
    """SVG fill attributes for a token: '#RRGGBB' or 'rgba(r, g, b, a)'."""
    if token.startswith("rgba("):
        r, g, b, a = (p.strip() for p in token[5:-1].split(","))
        return f'fill="#{int(r):02X}{int(g):02X}{int(b):02X}" fill-opacity="{a}"'
    return f'fill="{token}"'


def stroke(token, width):
    if token.startswith("rgba("):
        r, g, b, a = (p.strip() for p in token[5:-1].split(","))
        return f'stroke="#{int(r):02X}{int(g):02X}{int(b):02X}" stroke-opacity="{a}" stroke-width="{width:g}"'
    return f'stroke="{token}" stroke-width="{width:g}"'

# ---------------------------------------------------------------- text to outlines

def _fmt_num(val: float | int) -> str:
    if isinstance(val, int) and not isinstance(val, bool):
        return str(val)
    rounded = round(float(val), 6)
    if rounded == 0.0:
        return "0"
    if rounded.is_integer():
        return str(int(rounded))
    return f"{rounded:.6f}".rstrip("0").rstrip(".")


def _iup_axis(orig: list[float], touched_indices: list[int], deltas: list[float | None]) -> list[float]:
    """Interpolate untouched point deltas along one axis per OpenType gvar IUP."""
    n = len(orig)
    out = [0.0] * n
    if not touched_indices:
        return out
    if len(touched_indices) == 1:
        val = float(deltas[touched_indices[0]] or 0.0)
        return [val] * n
    for idx in touched_indices:
        out[idx] = float(deltas[idx] or 0.0)
    m = len(touched_indices)
    for pos in range(m):
        i1 = touched_indices[pos]
        i2 = touched_indices[(pos + 1) % m]
        o1, o2 = orig[i1], orig[i2]
        d1, d2 = out[i1], out[i2]
        if o1 > o2:
            o1, o2 = o2, o1
            d1, d2 = d2, d1
        k = (i1 + 1) % n
        while k != i2:
            ok = orig[k]
            if ok <= o1:
                out[k] = d1
            elif ok >= o2:
                out[k] = d2
            elif o1 == o2:
                out[k] = d1
            else:
                out[k] = d1 + (d2 - d1) * (ok - o1) / (o2 - o1)
            k = (k + 1) % n
    return out


def _region_scalar(region: list[tuple[float, float, float]], coords: tuple[float, ...]) -> float:
    scalar = 1.0
    for axis_idx, (start, peak, end) in enumerate(region):
        if peak == 0.0:
            continue
        coord = coords[axis_idx] if axis_idx < len(coords) else 0.0
        if coord == peak:
            continue
        if coord < start or coord > end:
            return 0.0
        if coord < peak:
            if peak == start:
                return 0.0
            scalar *= (coord - start) / (peak - start)
        else:
            if end == peak:
                return 0.0
            scalar *= (end - coord) / (end - peak)
        if scalar <= 0.0:
            return 0.0
    return scalar


def _unpack_point_numbers(buf: bytes | memoryview, pos: int) -> tuple[list[int] | None, int]:
    b0 = buf[pos]
    pos += 1
    if b0 == 0:
        return None, pos
    if b0 & 0x80:
        count = ((b0 & 0x7F) << 8) | buf[pos]
        pos += 1
    else:
        count = b0
    points: list[int] = []
    prev = 0
    while len(points) < count:
        cb = buf[pos]
        pos += 1
        words = bool(cb & 0x80)
        run_len = (cb & 0x7F) + 1
        if words:
            for delta in struct.unpack_from(f">{run_len}H", buf, pos):
                prev += delta
                points.append(prev)
            pos += 2 * run_len
        else:
            for delta in buf[pos:pos + run_len]:
                prev += delta
                points.append(prev)
            pos += run_len
    return points[:count], pos


def _unpack_deltas(buf: bytes | memoryview, pos: int, count: int) -> tuple[list[int], int]:
    deltas: list[int] = []
    while len(deltas) < count:
        cb = buf[pos]
        pos += 1
        run_len = (cb & 0x3F) + 1
        if cb & 0x80:
            deltas.extend([0] * run_len)
        elif cb & 0x40:
            deltas.extend(struct.unpack_from(f">{run_len}h", buf, pos))
            pos += 2 * run_len
        else:
            deltas.extend(struct.unpack_from(f">{run_len}b", buf, pos))
            pos += run_len
    return deltas[:count], pos


def _update_quad_bounds(p0: tuple[float, float], p1: tuple[float, float], p2: tuple[float, float],
                        bounds: list[float]) -> None:
    for x, y in (p0, p2):
        bounds[0] = min(bounds[0], x)
        bounds[1] = min(bounds[1], y)
        bounds[2] = max(bounds[2], x)
        bounds[3] = max(bounds[3], y)
    for axis in (0, 1):
        v0, v1, v2 = p0[axis], p1[axis], p2[axis]
        denom = v0 - 2.0 * v1 + v2
        if abs(denom) > 1e-12:
            t = (v0 - v1) / denom
            if 0.0 < t < 1.0:
                val = ((1.0 - t) ** 2) * v0 + 2.0 * (1.0 - t) * t * v1 + (t ** 2) * v2
                if axis == 0:
                    bounds[0] = min(bounds[0], val)
                    bounds[2] = max(bounds[2], val)
                else:
                    bounds[1] = min(bounds[1], val)
                    bounds[3] = max(bounds[3], val)


class TrueTypeFont:
    """Pure-stdlib reader for static and variable OpenType/TrueType (glyf) fonts."""

    def __init__(self, path: str | os.PathLike):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        if len(self.data) < 12:
            raise ValueError(f"Font file truncated: {self.path}")
        sfnt = self.data[:4]
        if sfnt not in (b"\x00\x01\x00\x00", b"true"):
            raise ValueError(f"Unsupported SFNT signature {sfnt!r} in {self.path}; expected TrueType outlines")
        num_tables = struct.unpack_from(">H", self.data, 4)[0]
        self.tables: dict[str, tuple[int, int]] = {}
        for i in range(num_tables):
            tag_b, _chk, off, length = struct.unpack_from(">4sIII", self.data, 12 + 16 * i)
            self.tables[tag_b.decode("latin1")] = (off, length)
        for req in ("head", "maxp", "hhea", "hmtx", "cmap", "loca", "glyf"):
            if req not in self.tables:
                raise ValueError(f"TrueType table '{req}' missing from {self.path}")
        hd_off, _ = self.tables["head"]
        self.units_per_em = struct.unpack_from(">H", self.data, hd_off + 18)[0]
        self.loc_format = struct.unpack_from(">h", self.data, hd_off + 50)[0]
        mx_off, _ = self.tables["maxp"]
        self.num_glyphs = struct.unpack_from(">H", self.data, mx_off + 4)[0]
        hh_off, _ = self.tables["hhea"]
        self.num_hmetrics = struct.unpack_from(">H", self.data, hh_off + 34)[0]
        self.cap_height = self._read_cap_height()
        self.metrics = self._read_hmtx()
        self.cmap = self._read_cmap()
        self.loca = self._read_loca()
        self.axes = self._read_fvar()
        self.avar_maps = self._read_avar() if self.axes and "avar" in self.tables else []
        self.gvar = self._read_gvar() if self.axes and "gvar" in self.tables else None
        self.hvar = self._read_hvar() if self.axes and "HVAR" in self.tables else None

    def _read_cap_height(self) -> int:
        if "OS/2" not in self.tables:
            return int(self.units_per_em * 0.7)
        off, length = self.tables["OS/2"]
        ver = struct.unpack_from(">H", self.data, off)[0]
        cap = struct.unpack_from(">h", self.data, off + 88)[0] if ver >= 2 and length >= 90 else 0
        return cap or int(self.units_per_em * 0.7)

    def _read_hmtx(self) -> list[tuple[int, int]]:
        off, _ = self.tables["hmtx"]
        out: list[tuple[int, int]] = []
        last_adv = 0
        for i in range(self.num_hmetrics):
            adv, lsb = struct.unpack_from(">Hh", self.data, off + 4 * i)
            last_adv = adv
            out.append((adv, lsb))
        tail_off = off + 4 * self.num_hmetrics
        for i in range(self.num_hmetrics, self.num_glyphs):
            lsb = struct.unpack_from(">h", self.data, tail_off + 2 * (i - self.num_hmetrics))[0]
            out.append((last_adv, lsb))
        return out

    def _read_cmap(self) -> dict[int, int]:
        cm_off, _ = self.tables["cmap"]
        num_sub = struct.unpack_from(">H", self.data, cm_off + 2)[0]
        best_f12: int | None = None
        best_f4: int | None = None
        for i in range(num_sub):
            pid, eid, sub_rel = struct.unpack_from(">HHI", self.data, cm_off + 4 + 8 * i)
            sub = cm_off + sub_rel
            fmt = struct.unpack_from(">H", self.data, sub)[0]
            if fmt == 12 and (pid, eid) in ((3, 10), (0, 4), (0, 3)) and best_f12 is None:
                best_f12 = sub
            elif fmt == 4 and (pid, eid) in ((3, 1), (0, 3), (0, 1), (0, 0)) and best_f4 is None:
                best_f4 = sub
        mapping: dict[int, int] = {}
        if best_f4 is not None:
            seg_x2 = struct.unpack_from(">H", self.data, best_f4 + 6)[0]
            seg = seg_x2 // 2
            end_c = struct.unpack_from(f">{seg}H", self.data, best_f4 + 14)
            start_c = struct.unpack_from(f">{seg}H", self.data, best_f4 + 16 + 2 * seg)
            id_delta = struct.unpack_from(f">{seg}h", self.data, best_f4 + 16 + 4 * seg)
            ro_base = best_f4 + 16 + 6 * seg
            id_ro = struct.unpack_from(f">{seg}H", self.data, ro_base)
            for i in range(seg):
                sc, ec, delta, ro = start_c[i], end_c[i], id_delta[i], id_ro[i]
                if sc == 0xFFFF and ec == 0xFFFF:
                    continue
                for code in range(sc, ec + 1):
                    if ro == 0:
                        gid = (code + delta) & 0xFFFF
                    else:
                        glyph_off = ro_base + 2 * i + ro + 2 * (code - sc)
                        gid = struct.unpack_from(">H", self.data, glyph_off)[0]
                        if gid != 0:
                            gid = (gid + delta) & 0xFFFF
                    if gid != 0:
                        mapping[code] = gid
        if best_f12 is not None:
            num_groups = struct.unpack_from(">I", self.data, best_f12 + 12)[0]
            for i in range(num_groups):
                start_c, end_c, start_g = struct.unpack_from(">III", self.data, best_f12 + 16 + 12 * i)
                for code in range(start_c, end_c + 1):
                    gid = start_g + (code - start_c)
                    if gid != 0:
                        mapping[code] = gid
        return mapping

    def _read_loca(self) -> list[int]:
        off, _ = self.tables["loca"]
        count = self.num_glyphs + 1
        if self.loc_format == 0:
            return [v * 2 for v in struct.unpack_from(f">{count}H", self.data, off)]
        return list(struct.unpack_from(f">{count}I", self.data, off))

    def _read_fvar(self) -> list[tuple[str, float, float, float]]:
        if "fvar" not in self.tables:
            return []
        off, _ = self.tables["fvar"]
        _maj, _min, axes_off, _res, axis_cnt, axis_sz, _inst_cnt, _inst_sz = struct.unpack_from(">HHHHHHHH", self.data, off)
        axes: list[tuple[str, float, float, float]] = []
        for i in range(axis_cnt):
            tag_b, min_v, def_v, max_v, _fl, _nid = struct.unpack_from(">4siiiHH", self.data, off + axes_off + i * axis_sz)
            axes.append((tag_b.decode("latin1"), min_v / 65536.0, def_v / 65536.0, max_v / 65536.0))
        return axes

    def _read_avar(self) -> list[list[tuple[float, float]]]:
        off, _ = self.tables["avar"]
        _maj, _min, _res, axis_cnt = struct.unpack_from(">HHHH", self.data, off)
        pos = off + 8
        maps: list[list[tuple[float, float]]] = []
        for _ in range(axis_cnt):
            pair_cnt = struct.unpack_from(">H", self.data, pos)[0]
            pos += 2
            seg: list[tuple[float, float]] = []
            for _j in range(pair_cnt):
                fc, tc = struct.unpack_from(">hh", self.data, pos)
                pos += 4
                seg.append((fc / 16384.0, tc / 16384.0))
            maps.append(seg)
        return maps

    def _read_gvar(self) -> dict:
        off, _ = self.tables["gvar"]
        _maj, _min, axis_cnt, sh_cnt, sh_off, gl_cnt, flags, data_off = struct.unpack_from(">HHHHIHHI", self.data, off)
        shared_tuples: list[tuple[float, ...]] = []
        sh_base = off + sh_off
        for i in range(sh_cnt):
            vals = struct.unpack_from(f">{axis_cnt}h", self.data, sh_base + i * 2 * axis_cnt)
            shared_tuples.append(tuple(v / 16384.0 for v in vals))
        off_pos = off + 20
        if flags & 1:
            offsets = list(struct.unpack_from(f">{gl_cnt + 1}I", self.data, off_pos))
        else:
            offsets = [v * 2 for v in struct.unpack_from(f">{gl_cnt + 1}H", self.data, off_pos)]
        return {
            "axis_count": axis_cnt,
            "shared_tuples": shared_tuples,
            "offsets": offsets,
            "data_base": off + data_off,
        }

    def _read_hvar(self) -> dict | None:
        off, _ = self.tables["HVAR"]
        _maj, _min, ivs_off, adv_off, _lsb_off, _rsb_off = struct.unpack_from(">HHIIII", self.data, off)
        if ivs_off == 0:
            return None
        ivs_base = off + ivs_off
        _fmt, reg_off, data_cnt = struct.unpack_from(">HIH", self.data, ivs_base)
        data_offsets = list(struct.unpack_from(f">{data_cnt}I", self.data, ivs_base + 8))
        reg_base = ivs_base + reg_off
        reg_axes, reg_cnt = struct.unpack_from(">HH", self.data, reg_base)
        regions: list[list[tuple[float, float, float]]] = []
        pos = reg_base + 4
        for _ in range(reg_cnt):
            axes_r: list[tuple[float, float, float]] = []
            for _a in range(reg_axes):
                s, p, e = struct.unpack_from(">hhh", self.data, pos)
                pos += 6
                axes_r.append((s / 16384.0, p / 16384.0, e / 16384.0))
            regions.append(axes_r)
        return {
            "base": off,
            "ivs_base": ivs_base,
            "regions": regions,
            "data_offsets": data_offsets,
            "adv_map": (off + adv_off) if adv_off else 0,
        }

    def normalize_location(self, location: dict[str, float] | None) -> tuple[float, ...]:
        if not self.axes:
            return ()
        loc = location or {}
        norm: list[float] = []
        for idx, (tag, min_v, def_v, max_v) in enumerate(self.axes):
            val = float(loc.get(tag, def_v))
            if val < def_v:
                c = max(-1.0, (val - def_v) / (def_v - min_v)) if def_v > min_v else 0.0
            elif val > def_v:
                c = min(1.0, (val - def_v) / (max_v - def_v)) if max_v > def_v else 0.0
            else:
                c = 0.0
            if idx < len(self.avar_maps) and len(self.avar_maps[idx]) >= 2:
                seg = self.avar_maps[idx]
                if c <= seg[0][0]:
                    c = seg[0][1]
                elif c >= seg[-1][0]:
                    c = seg[-1][1]
                else:
                    for j in range(len(seg) - 1):
                        f0, t0 = seg[j]
                        f1, t1 = seg[j + 1]
                        if f0 <= c <= f1:
                            c = t0 if f1 == f0 else t0 + (c - f0) * (t1 - t0) / (f1 - f0)
                            break
            norm.append(c)
        return tuple(norm)

    def _hvar_delta(self, gid: int, norm_coords: tuple[float, ...]) -> float:
        hvar = self.hvar
        assert hvar is not None
        outer, inner = 0, gid
        map_off = hvar["adv_map"]
        if map_off:
            fmt = self.data[map_off]
            entry_fmt = self.data[map_off + 1]
            if fmt == 0:
                map_cnt = struct.unpack_from(">H", self.data, map_off + 2)[0]
                entries_off = map_off + 4
            else:
                map_cnt = struct.unpack_from(">I", self.data, map_off + 2)[0]
                entries_off = map_off + 6
            if map_cnt == 0:
                return 0.0
            inner_bits = (entry_fmt & 0x0F) + 1
            entry_sz = ((entry_fmt & 0x30) >> 4) + 1
            idx = min(gid, map_cnt - 1)
            raw = int.from_bytes(self.data[entries_off + idx * entry_sz:entries_off + (idx + 1) * entry_sz], "big")
            outer = raw >> inner_bits
            inner = raw & ((1 << inner_bits) - 1)
        data_offsets = hvar["data_offsets"]
        if outer >= len(data_offsets):
            return 0.0
        ivd_off = hvar["ivs_base"] + data_offsets[outer]
        item_cnt, word_delta_cnt, reg_idx_cnt = struct.unpack_from(">HHH", self.data, ivd_off)
        if inner >= item_cnt:
            return 0.0
        long_words = bool(word_delta_cnt & 0x8000)
        word_cnt = word_delta_cnt & 0x7FFF
        reg_indices = struct.unpack_from(f">{reg_idx_cnt}H", self.data, ivd_off + 6)
        short_cnt = reg_idx_cnt - word_cnt
        row_sz = word_cnt * (4 if long_words else 2) + short_cnt * (2 if long_words else 1)
        row_off = ivd_off + 6 + 2 * reg_idx_cnt + inner * row_sz
        total = 0.0
        pos = row_off
        for i in range(reg_idx_cnt):
            if long_words:
                if i < word_cnt:
                    delta = struct.unpack_from(">i", self.data, pos)[0]
                    pos += 4
                else:
                    delta = struct.unpack_from(">h", self.data, pos)[0]
                    pos += 2
            else:
                if i < word_cnt:
                    delta = struct.unpack_from(">h", self.data, pos)[0]
                    pos += 2
                else:
                    delta = struct.unpack_from(">b", self.data, pos)[0]
                    pos += 1
            if delta:
                reg = hvar["regions"][reg_indices[i]]
                total += _region_scalar(reg, norm_coords) * delta
        return total

    def _apply_gvar(self, gid: int, norm_coords: tuple[float, ...], contour_ends: list[int] | None,
                    xs: list[float], ys: list[float]) -> None:
        gvar = self.gvar
        if gvar is None or not any(norm_coords) or gid + 1 >= len(gvar["offsets"]):
            return
        start_rel, end_rel = gvar["offsets"][gid], gvar["offsets"][gid + 1]
        if start_rel == end_rel:
            return
        base = gvar["data_base"] + start_rel
        tv_count_raw, data_offset = struct.unpack_from(">HH", self.data, base)
        tv_count = tv_count_raw & 0x0FFF
        has_shared = bool(tv_count_raw & 0x8000)
        axis_cnt = gvar["axis_count"]
        shared_tuples = gvar["shared_tuples"]
        n_total = len(xs)
        n_glyph = n_total - 4
        hdr_pos = base + 4
        serial_pos = base + data_offset
        shared_pts: list[int] | None = None
        if has_shared:
            shared_pts, serial_pos = _unpack_point_numbers(self.data, serial_pos)
        acc_dx = [0.0] * n_total
        acc_dy = [0.0] * n_total
        orig_xs = list(xs)
        orig_ys = list(ys)
        for _ in range(tv_count):
            var_size, tuple_idx = struct.unpack_from(">HH", self.data, hdr_pos)
            hdr_pos += 4
            if tuple_idx & 0x8000:
                peak = tuple(v / 16384.0 for v in struct.unpack_from(f">{axis_cnt}h", self.data, hdr_pos))
                hdr_pos += 2 * axis_cnt
            else:
                peak = shared_tuples[tuple_idx & 0x0FFF]
            if tuple_idx & 0x4000:
                starts = tuple(v / 16384.0 for v in struct.unpack_from(f">{axis_cnt}h", self.data, hdr_pos))
                hdr_pos += 2 * axis_cnt
                ends = tuple(v / 16384.0 for v in struct.unpack_from(f">{axis_cnt}h", self.data, hdr_pos))
                hdr_pos += 2 * axis_cnt
            else:
                starts = tuple(min(0.0, p) for p in peak)
                ends = tuple(max(0.0, p) for p in peak)
            tv_end = serial_pos + var_size
            region = [(starts[a], peak[a], ends[a]) for a in range(axis_cnt)]
            scalar = _region_scalar(region, norm_coords)
            if scalar > 0.0 and var_size > 0:
                p = serial_pos
                if tuple_idx & 0x2000:
                    pts, p = _unpack_point_numbers(self.data, p)
                else:
                    pts = shared_pts
                delta_cnt = n_total if pts is None else len(pts)
                dx_vals, p = _unpack_deltas(self.data, p, delta_cnt)
                dy_vals, p = _unpack_deltas(self.data, p, delta_cnt)
                if pts is None:
                    for i in range(n_total):
                        acc_dx[i] += scalar * dx_vals[i]
                        acc_dy[i] += scalar * dy_vals[i]
                elif contour_ends is not None:
                    dx_sparse: list[float | None] = [None] * n_total
                    dy_sparse: list[float | None] = [None] * n_total
                    for pt_idx, xv, yv in zip(pts, dx_vals, dy_vals):
                        if pt_idx < n_total:
                            dx_sparse[pt_idx] = float(xv)
                            dy_sparse[pt_idx] = float(yv)
                    c_start = 0
                    for c_end in contour_ends:
                        c_len = c_end - c_start + 1
                        if c_len > 0:
                            touched = [k for k in range(c_len) if dx_sparse[c_start + k] is not None]
                            if touched:
                                iup_x = _iup_axis(orig_xs[c_start:c_end + 1], touched, dx_sparse[c_start:c_end + 1])
                                iup_y = _iup_axis(orig_ys[c_start:c_end + 1], touched, dy_sparse[c_start:c_end + 1])
                                for k in range(c_len):
                                    acc_dx[c_start + k] += scalar * iup_x[k]
                                    acc_dy[c_start + k] += scalar * iup_y[k]
                        c_start = c_end + 1
                    for pp_idx in range(n_glyph, n_total):
                        if dx_sparse[pp_idx] is not None:
                            acc_dx[pp_idx] += scalar * float(dx_sparse[pp_idx] or 0.0)
                            acc_dy[pp_idx] += scalar * float(dy_sparse[pp_idx] or 0.0)
                else:
                    for pt_idx, xv, yv in zip(pts, dx_vals, dy_vals):
                        if pt_idx < n_total:
                            acc_dx[pt_idx] += scalar * xv
                            acc_dy[pt_idx] += scalar * yv
            serial_pos = tv_end
        for i in range(n_total):
            xs[i] += acc_dx[i]
            ys[i] += acc_dy[i]

    def _glyph_data(self, gid: int, norm_coords: tuple[float, ...],
                    depth: int = 0) -> tuple[list[list[tuple[float | int, float | int, bool]]], list[tuple[float, float]]]:
        if depth > 16:
            raise ValueError(f"Composite glyph recursion depth exceeded at glyph {gid}")
        adv, lsb = self.metrics[gid]
        g0, g1 = self.loca[gid], self.loca[gid + 1]
        if g0 == g1:
            return [], [(0.0, 0.0), (float(adv), 0.0), (0.0, 0.0), (0.0, 0.0)]
        gl_base = self.tables["glyf"][0]
        pos = gl_base + g0
        num_contours, x_min, _y_min, _x_max, y_max = struct.unpack_from(">hhhhh", self.data, pos)
        pos += 10
        pp = [(float(x_min - lsb), 0.0), (float(x_min - lsb + adv), 0.0), (0.0, float(y_max)), (0.0, 0.0)]
        orig_pp0_x = pp[0][0]
        varied = bool(self.gvar is not None and any(norm_coords))
        if num_contours >= 0:
            if num_contours == 0:
                return [], pp
            end_pts = list(struct.unpack_from(f">{num_contours}H", self.data, pos))
            pos += 2 * num_contours
            n_pts = end_pts[-1] + 1
            inst_len = struct.unpack_from(">H", self.data, pos)[0]
            pos += 2 + inst_len
            flags: list[int] = []
            while len(flags) < n_pts:
                fl = self.data[pos]
                pos += 1
                flags.append(fl)
                if fl & 0x08:
                    rep = self.data[pos]
                    pos += 1
                    flags.extend([fl & ~0x08] * rep)
            flags = flags[:n_pts]
            xs_i: list[int] = []
            cur_x = 0
            for fl in flags:
                if fl & 0x02:
                    dx = self.data[pos]
                    pos += 1
                    cur_x += dx if (fl & 0x10) else -dx
                elif not (fl & 0x10):
                    cur_x += struct.unpack_from(">h", self.data, pos)[0]
                    pos += 2
                xs_i.append(cur_x)
            ys_i: list[int] = []
            cur_y = 0
            for fl in flags:
                if fl & 0x04:
                    dy = self.data[pos]
                    pos += 1
                    cur_y += dy if (fl & 0x20) else -dy
                elif not (fl & 0x20):
                    cur_y += struct.unpack_from(">h", self.data, pos)[0]
                    pos += 2
                ys_i.append(cur_y)
            if varied:
                xs: list[float] = [float(v) for v in xs_i] + [p[0] for p in pp]
                ys: list[float] = [float(v) for v in ys_i] + [p[1] for p in pp]
                self._apply_gvar(gid, norm_coords, end_pts, xs, ys)
                pp = [(xs[n_pts + k], ys[n_pts + k]) for k in range(4)]
                x_shift = pp[0][0] - orig_pp0_x
                contours: list[list[tuple[float | int, float | int, bool]]] = []
                c_start = 0
                for c_end in end_pts:
                    contours.append([(xs[i] - x_shift, ys[i], bool(flags[i] & 0x01)) for i in range(c_start, c_end + 1)])
                    c_start = c_end + 1
                return contours, pp
            contours = []
            c_start = 0
            for c_end in end_pts:
                contours.append([(xs_i[i], ys_i[i], bool(flags[i] & 0x01)) for i in range(c_start, c_end + 1)])
                c_start = c_end + 1
            return contours, pp

        # Composite glyph (num_contours < 0)
        comps = []
        more = True
        while more:
            c_flags, comp_gid = struct.unpack_from(">HH", self.data, pos)
            pos += 4
            if c_flags & 0x0001:  # ARG_1_AND_2_ARE_WORDS
                fmt = ">hh" if (c_flags & 0x0002) else ">HH"
                arg1, arg2 = struct.unpack_from(fmt, self.data, pos)
                pos += 4
            else:
                fmt = ">bb" if (c_flags & 0x0002) else ">BB"
                arg1, arg2 = struct.unpack_from(fmt, self.data, pos)
                pos += 2
            a, b, c, d = 1.0, 0.0, 0.0, 1.0
            if c_flags & 0x0008:
                a = d = struct.unpack_from(">h", self.data, pos)[0] / 16384.0
                pos += 2
            elif c_flags & 0x0040:
                a, d = (v / 16384.0 for v in struct.unpack_from(">hh", self.data, pos))
                pos += 4
            elif c_flags & 0x0080:
                a, b, c, d = (v / 16384.0 for v in struct.unpack_from(">hhhh", self.data, pos))
                pos += 8
            comps.append((c_flags, comp_gid, arg1, arg2, a, b, c, d))
            more = bool(c_flags & 0x0020)

        n_comp = len(comps)
        comp_pp = [(0.0, 0.0), (float(adv), 0.0), (0.0, 0.0), (0.0, 0.0)]
        c_xs = [float(comp[2]) if (comp[0] & 0x0002) else 0.0 for comp in comps] + [p[0] for p in comp_pp]
        c_ys = [float(comp[3]) if (comp[0] & 0x0002) else 0.0 for comp in comps] + [p[1] for p in comp_pp]
        if varied:
            self._apply_gvar(gid, norm_coords, None, c_xs, c_ys)
            comp_pp = [(c_xs[n_comp + k], c_ys[n_comp + k]) for k in range(4)]

        out_contours: list[list[tuple[float | int, float | int, bool]]] = []
        flat_points: list[tuple[float | int, float | int]] = []
        for idx, (c_flags, comp_gid, arg1, arg2, a, b, c, d) in enumerate(comps):
            sub_contours, _ = self._glyph_data(comp_gid, norm_coords, depth + 1)
            sub_flat = [pt for contour in sub_contours for pt in contour]
            if c_flags & 0x0002:
                dx = c_xs[idx]
                dy = c_ys[idx]
                if (c_flags & 0x0800) and not (c_flags & 0x1000):
                    sx = math.hypot(a, b)
                    sy = math.hypot(c, d)
                    dx *= sx
                    dy *= sy
            else:
                if arg1 < len(flat_points) and arg2 < len(sub_flat):
                    px_p, py_p = flat_points[arg1]
                    sx_p, sy_p = sub_flat[arg2][0], sub_flat[arg2][1]
                    tx_c = a * sx_p + c * sy_p
                    ty_c = b * sx_p + d * sy_p
                    dx = px_p - tx_c
                    dy = py_p - ty_c
                else:
                    dx, dy = 0.0, 0.0
            for contour in sub_contours:
                transformed: list[tuple[float | int, float | int, bool]] = []
                for px, py, on in contour:
                    nx = a * px + c * py + dx if (a != 1.0 or b != 0.0 or c != 0.0 or d != 1.0) else px + dx
                    ny = b * px + d * py + dy if (a != 1.0 or b != 0.0 or c != 0.0 or d != 1.0) else py + dy
                    transformed.append((nx, ny, on))
                    flat_points.append((nx, ny))
                out_contours.append(transformed)
        if varied and comp_pp[0][0] != 0.0:
            x_shift = comp_pp[0][0]
            out_contours = [[(px - x_shift, py, on) for px, py, on in contour] for contour in out_contours]
        return out_contours, comp_pp

    def advance_width(self, gid: int, norm_coords: tuple[float, ...],
                      pp: list[tuple[float, float]] | None = None) -> float:
        adv, _ = self.metrics[gid]
        if norm_coords and any(norm_coords):
            if self.hvar is not None:
                return float(adv) + self._hvar_delta(gid, norm_coords)
            if self.gvar is not None and pp is not None:
                return max(0.0, pp[1][0] - pp[0][0])
        return float(adv)

    def glyph_svg_and_bounds(self, codepoint: int, x_offset: float,
                             norm_coords: tuple[float, ...]) -> tuple[str, list[float] | None, float]:
        gid = self.cmap.get(codepoint, 0)
        contours, pp = self._glyph_data(gid, norm_coords)
        adv = self.advance_width(gid, norm_coords, pp)
        cmds: list[str] = []
        bounds = [math.inf, math.inf, -math.inf, -math.inf]
        has_pts = False
        for contour in contours:
            pts = [(gx + x_offset, -gy, on) for gx, gy, on in contour]
            if len(pts) < 2:
                continue
            has_pts = True
            if pts[0][2]:
                start = (pts[0][0], pts[0][1])
                seq = pts[1:] + [pts[0]]
            elif pts[-1][2]:
                start = (pts[-1][0], pts[-1][1])
                seq = pts[:-1] + [pts[-1]]
            else:
                start = ((pts[-1][0] + pts[0][0]) / 2.0, (pts[-1][1] + pts[0][1]) / 2.0)
                seq = pts + [(start[0], start[1], True)]
            cmds.append(f"M{_fmt_num(start[0])} {_fmt_num(start[1])}")
            cx, cy = start
            pending_off: tuple[float | int, float | int] | None = None
            for idx, (px, py, on) in enumerate(seq):
                is_last = idx == len(seq) - 1
                if on:
                    if pending_off is not None:
                        cmds.append(f"Q{_fmt_num(pending_off[0])} {_fmt_num(pending_off[1])} {_fmt_num(px)} {_fmt_num(py)}")
                        _update_quad_bounds((float(cx), float(cy)), (float(pending_off[0]), float(pending_off[1])),
                                            (float(px), float(py)), bounds)
                        cx, cy = px, py
                        pending_off = None
                    elif not is_last:
                        if px == cx and py != cy:
                            cmds.append(f"V{_fmt_num(py)}")
                        elif py == cy and px != cx:
                            cmds.append(f"H{_fmt_num(px)}")
                        elif px != cx or py != cy:
                            cmds.append(f"L{_fmt_num(px)} {_fmt_num(py)}")
                        for x_b, y_b in ((float(cx), float(cy)), (float(px), float(py))):
                            bounds[0] = min(bounds[0], x_b)
                            bounds[1] = min(bounds[1], y_b)
                            bounds[2] = max(bounds[2], x_b)
                            bounds[3] = max(bounds[3], y_b)
                        cx, cy = px, py
                else:
                    if pending_off is not None:
                        mx = (pending_off[0] + px) / 2.0
                        my = (pending_off[1] + py) / 2.0
                        cmds.append(f"Q{_fmt_num(pending_off[0])} {_fmt_num(pending_off[1])} {_fmt_num(mx)} {_fmt_num(my)}")
                        _update_quad_bounds((float(cx), float(cy)), (float(pending_off[0]), float(pending_off[1])),
                                            (float(mx), float(my)), bounds)
                        cx, cy = mx, my
                    pending_off = (px, py)
            cmds.append("Z")
        return "".join(cmds), (bounds if has_pts else None), adv


def outline_text(font_path, text, location=None, tracking_em=0.0):
    """Returns (svg path data in font units with y pointing down, x_min, y_min, x_max, y_max, cap_height)."""
    font = TrueTypeFont(font_path)
    norm_coords = font.normalize_location(location)
    upm = font.units_per_em
    cap = font.cap_height
    parts: list[str] = []
    bounds = [math.inf, math.inf, -math.inf, -math.inf]
    x = 0.0
    for ch in text:
        d, g_bounds, adv = font.glyph_svg_and_bounds(ord(ch), x, norm_coords)
        if d:
            parts.append(d)
        if g_bounds is not None:
            bounds[0] = min(bounds[0], g_bounds[0])
            bounds[1] = min(bounds[1], g_bounds[1])
            bounds[2] = max(bounds[2], g_bounds[2])
            bounds[3] = max(bounds[3], g_bounds[3])
        x += adv + tracking_em * upm
    if math.isinf(bounds[0]):
        bounds = [0.0, 0.0, 0.0, 0.0]
    return "".join(parts), bounds[0], bounds[1], bounds[2], bounds[3], cap


# ---------------------------------------------------------------- vector masters (64-unit grid)

def icon_defs():
    """Gradients and the lift shadow shared by the large icon. Identical in every copy, so repeated ids are harmless."""
    sh = B["iconShadow"][5:-1].split(",")
    return (f'<defs><linearGradient id="rp" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{B["iconPageTop"]}"/>'
            f'<stop offset="1" stop-color="{B["iconPageBottom"]}"/></linearGradient>'
            f'<linearGradient id="rr" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{B["iconRibbonTop"]}"/>'
            f'<stop offset="1" stop-color="{B["iconRibbonBottom"]}"/></linearGradient>'
            f'<filter id="rl" x="-25%" y="-20%" width="150%" height="150%"><feDropShadow dx="0" dy="1.5" stdDeviation="1.75" '
            f'flood-color="#{int(sh[0]):02X}{int(sh[1]):02X}{int(sh[2]):02X}" flood-opacity="{sh[3].strip()}"/></filter></defs>')


def app_icon(lines=True, shadow=False):
    """The app icon on the 64-unit grid: one page with the ribbon hanging from its top edge, no tile (SPEC 3.1).

    lines=False is the 16 to 24 px drawing: a darker outline, no text lines and a larger ribbon, so both survive at 16 px.
    shadow=True lifts the page off the background; used at 48 px and up, where it reads as depth rather than blur.
    Without the shadow (32 to 47 px) the page gets a firmer edge so it still holds on a light taskbar.
    """
    if not lines:
        return (f'<rect x="10" y="6" width="44" height="52" rx="6" fill="{B["iconPageTop"]}" {stroke(B["iconPageOutlineSmall"], 3.4)}/>'
                f'<path d="M34 2 H48 V32 L41 25.6 L34 32 Z" fill="{B["iconRibbonSmall"]}"/>')
    edge = stroke(B["iconPageEdge"], 0.5) if shadow else stroke(B["iconPageEdgeMedium"], 1.5)
    body = (f'<rect x="12.5" y="5.5" width="39" height="53" rx="4" fill="url(#rp)" {edge}/>'
            + "".join(f'<rect x="20.5" y="{y:g}" width="{w:g}" height="2.5" rx="1.25" {paint(B["iconLines"])}/>'
                      for y, w in ((26, 23), (32.5, 23), (39, 15)))
            + '<path d="M36.5 2.5 H45.5 V27.5 L41 23.5 L36.5 27.5 Z" fill="url(#rr)"/>')
    return icon_defs() + (f'<g filter="url(#rl)">{body}</g>' if shadow else body)


def filetype_icon(label_path=None, small=False):
    page, edge, fold, ribbon = B["fileTypePage"], B["fileTypeEdge"], B["fileTypeFold"], B["iconRibbonSmall"]
    if small:
        # 16 to 24 px: heavier strokes, no lines, no label.
        return (f'<path d="M13 3 H40 L53 16 V59 Q53 61 51 61 H13 Q11 61 11 59 V5 Q11 3 13 3 Z" fill="{page}" stroke="{edge}" stroke-width="2.5"/>'
                f'<path d="M40 3 V14 Q40 16 42 16 H53" fill="{fold}" stroke="{edge}" stroke-width="2.5" stroke-linejoin="round"/>'
                f'<path d="M19 1.5 H30 V27 L24.5 22 L19 27 Z" fill="{ribbon}"/>')
    body = (f'<path d="M14 4 H40 L52 16 V58 Q52 60 50 60 H14 Q12 60 12 58 V6 Q12 4 14 4 Z" fill="{page}" stroke="{edge}" stroke-width="1.2"/>'
            f'<path d="M40 4 V14 Q40 16 42 16 H52" fill="{fold}" stroke="{edge}" stroke-width="1.2" stroke-linejoin="round"/>'
            f'<path d="M20 2.5 H27.5 V22 L23.75 18.5 L20 22 Z" fill="{ribbon}"/>'
            + "".join(f'<rect x="{x}" y="{y}" width="{w}" height="2.4" rx="1.2" fill="{B["fileTypeLines"]}"/>'
                      for x, y, w in ((20, 26, 26), (20, 31, 24), (20, 36, 26))))
    if label_path:
        body += (f'<rect x="7" y="43" width="31" height="14" rx="3" fill="{B["fileTypeBadge"]}"/>' + label_path)
    return body


def svg(viewbox_w, viewbox_h, inner, w=None, h=None, title=None):
    size = f' width="{w}" height="{h}"' if w else ""
    t = f'<title>{title}</title>' if title else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {viewbox_w:g} {viewbox_h:g}"{size}'
            f'{" role=\"img\"" if title else ""}>{t}{inner}</svg>')


def placed(inner, x, y, scale):
    return f'<g transform="translate({x:g} {y:g}) scale({scale:g})">{inner}</g>'


# ---------------------------------------------------------------- in-app graphics

def layout_thumb(preset, dark):
    """240 x 150 wireframes for the layout preset cards in Settings."""
    w, h = 240, 150
    if dark:
        win, surf, raised, canvas, page, line, acc = (D["window"], D["surface"], D["raised"], D["canvas"], PAGE_DARK["dimmed"],
                                                      D["border"], D["accent"])
    else:
        win, surf, raised, canvas, page, line, acc = (ILL["wireframeWindowLight"], L["surface"], L["raised"], L["canvas"],
                                                      PAGE_DARK["original"], L["border"], L["accent"])
    s = [f'<rect width="{w}" height="{h}" rx="12" fill="{canvas}"/>',
         f'<path d="M12 0 H228 Q240 0 240 12 V14 H0 V12 Q0 0 12 0 Z" fill="{win}"/>']
    if preset == "focus":
        s += [f'<rect x="60" y="24" width="120" height="126" fill="{page}"/>',
              f'<rect x="8" y="24" width="36" height="86" rx="6" fill="{raised}" stroke="{line}"/>',
              f'<rect x="60" y="122" width="120" height="18" rx="7" fill="{raised}" stroke="{line}"/>',
              f'<rect x="150" y="128" width="22" height="6" rx="3" fill="{acc}"/>']
    elif preset == "classic":
        s += [f'<rect x="0" y="14" width="16" height="136" fill="{win}"/>',
              f'<rect x="16" y="14" width="224" height="14" fill="{surf}"/>',
              f'<rect x="16" y="28" width="38" height="122" fill="{surf}"/>',
              f'<path d="M16 28 H240 M54 28 V150" stroke="{line}"/>',
              f'<rect x="80" y="38" width="130" height="112" fill="{page}"/>',
              f'<rect x="206" y="17" width="26" height="8" rx="2" fill="{acc}"/>']
    else:  # reading room
        s += [f'<rect x="0" y="14" width="240" height="12" fill="{surf}"/>',
              f'<rect x="20" y="5" width="20" height="4" rx="2" fill="{acc}"/>',
              f'<rect x="190" y="26" width="50" height="124" fill="{surf}"/>',
              f'<rect x="0" y="122" width="190" height="28" fill="{win}"/>',
              f'<rect x="60" y="34" width="72" height="84" fill="{page}"/>']
        s += [f'<rect x="{18 + i * 22}" y="128" width="14" height="16" fill="{page}" opacity=".8"/>' for i in range(7)]
    return svg(w, h, "".join(s), w, h, f"{preset} layout preview")


def drop_illustration(dark):
    """240 x 160: pages dropping into a tray. Home empty state and the full-window drop overlay."""
    t = D if dark else L
    page = PAGE_DARK["dimmed"] if dark else PAGE_DARK["original"]
    inner = (f'<rect x="40" y="96" width="160" height="56" rx="14" fill="none" stroke="{t["borderStrong"]}" stroke-width="2" stroke-dasharray="6 6"/>'
             f'<g transform="rotate(-8 100 70)"><rect x="70" y="18" width="70" height="90" rx="4" fill="{page}" stroke="{t["border"]}"/></g>'
             f'<rect x="95" y="12" width="70" height="90" rx="4" fill="{page}" stroke="{t["border"]}"/>'
             f'<path d="M140 9 H150 V36 L145 31 L140 36 Z" fill="{t["accent"]}"/>'
             + "".join(f'<rect x="104" y="{y}" width="{w}" height="4" rx="2" fill="{t["borderStrong"]}"/>' for y, w in ((44, 44), (54, 50), (64, 36)))
             + f'<path d="M120 112 V140 M110 130 L120 140 L130 130" fill="none" stroke="{t["accent"]}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>')
    return svg(240, 160, inner, 240, 160, "Drop files to open them")


# ---------------------------------------------------------------- brand graphics and installer images

class TextOutliner:
    """Sets text as SVG outlines at a pixel size, so the PNGs never depend on installed or web fonts."""

    def __init__(self, newsreader, ui_font, ui_bold_font):
        self.paths = {"serif": newsreader, "ui": ui_font, "ui-bold": ui_bold_font}
        self.fonts = {}

    def _font(self, key):
        if key not in self.fonts:
            self.fonts[key] = TrueTypeFont(self.paths[key])
        return self.fonts[key]

    def width(self, key, text, size, location=None, tracking_em=0.0):
        font = self._font(key)
        norm_coords = font.normalize_location(location)
        upm = font.units_per_em
        advance = 0.0
        for ch in text:
            gid = font.cmap.get(ord(ch), 0)
            _, pp = font._glyph_data(gid, norm_coords)
            advance += font.advance_width(gid, norm_coords, pp) + tracking_em * upm
        return advance * size / upm

    def path(self, key, text, size, x, baseline, fill, location=None, tracking_em=0.0):
        """Returns (svg path element, advance width in px). (x, baseline) is the start of the baseline."""
        font = self._font(key)
        norm_coords = font.normalize_location(location)
        upm = font.units_per_em
        parts: list[str] = []
        advance = 0.0
        for ch in text:
            d, _, adv = font.glyph_svg_and_bounds(ord(ch), advance, norm_coords)
            if d:
                parts.append(d)
            advance += adv + tracking_em * upm
        s = size / upm
        return (f'<path transform="translate({x:g} {baseline:g}) scale({s:g})" d="{"".join(parts)}" {paint(fill)}/>',
                advance * s)


SERIF_DISPLAY = {"wght": 500, "opsz": 72}
SERIF_TEXT = {"wght": 400, "opsz": 36}


def ribbon(x, width, height, fill):
    notch = width * 0.42
    return f'<path d="M{x:g} 0 H{x + width:g} V{height:g} L{x + width / 2:g} {height - notch:g} L{x:g} {height:g} Z" fill="{fill}"/>'


def chip(text, label, x, y, size=17):
    w = text.width("ui", label, size)
    pad, h = 16, size + 18
    t, _ = text.path("ui", label, size, x + pad, y + h / 2 + size * 0.35, D["text"])
    return (f'<rect x="{x:g}" y="{y:g}" width="{w + 2 * pad:g}" height="{h:g}" rx="{h / 2:g}" fill="none" '
            f'stroke="{D["borderStrong"]}" stroke-width="1"/>' + t), w + 2 * pad


def window_sketch(x, y, w, h):
    """A dark Recto window with one page, a search hit and the side panel; the right edge is cut off by the canvas."""
    hit = D["searchHit"]
    lines = "".join(f'<rect x="{x + 112:g}" y="{y + 132 + i * 16:g}" width="{wd:g}" height="5" rx="2.5" {paint(B["iconLines"])}/>'
                    for i, wd in enumerate((288, 270, 278, 178)))
    lines += "".join(f'<rect x="{x + 112:g}" y="{y + 206 + i * 16:g}" width="{wd:g}" height="5" rx="2.5" {paint(B["iconLines"])}/>'
                     for i, wd in enumerate((288, 258)))
    return (f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" rx="8" fill="{D["window"]}" stroke="{D["border"]}"/>'
            f'<rect x="{x + 1:g}" y="{y + 36:g}" width="{w - 2:g}" height="{h - 37:g}" fill="{D["canvas"]}"/>'
            f'<rect x="{x + 14:g}" y="{y + 15:g}" width="44" height="6" rx="3" fill="{D["borderStrong"]}"/>'
            f'<rect x="{x + 76:g}" y="{y + 15:g}" width="36" height="6" rx="3" fill="{D["text"]}"/>'
            f'<rect x="{x + 78:g}" y="{y + 29:g}" width="32" height="3" rx="1.5" fill="{D["accent"]}"/>'
            f'<rect x="{x + 130:g}" y="{y + 15:g}" width="44" height="6" rx="3" fill="{D["borderStrong"]}"/>'
            f'<rect x="{x + 76:g}" y="{y + 62:g}" width="360" height="{h - 62:g}" fill="{PAGE_DARK["original"]}"/>'
            f'<rect x="{x + 112:g}" y="{y + 98:g}" width="174" height="12" rx="3" fill="{D["window"]}"/>' + lines
            + f'<rect x="{x + 112:g}" y="{y + 234:g}" width="230" height="9" rx="4.5" fill="{hit}"/>'
            f'<rect x="{x + 511:g}" y="{y + 36:g}" width="{w - 512:g}" height="{h - 37:g}" fill="{D["surface"]}"/>'
            f'<line x1="{x + 511:g}" y1="{y + 36:g}" x2="{x + 511:g}" y2="{y + h:g}" stroke="{D["border"]}"/>'
            f'<rect x="{x + 526:g}" y="{y + 54:g}" width="60" height="4" rx="2" fill="{D["borderStrong"]}"/>'
            f'<rect x="{x + 526:g}" y="{y + 72:g}" width="60" height="18" rx="4" fill="{D["accentTint"]}" stroke="{D["accent"]}"/>'
            f'<rect x="{x + 526:g}" y="{y + 104:g}" width="60" height="4" rx="2" fill="{D["borderStrong"]}"/>'
            f'<rect x="{x + 526:g}" y="{y + 122:g}" width="60" height="4" rx="2" fill="{D["borderStrong"]}"/>')


TAGLINE = "Open-source PDF viewer and converter for Windows"


def chips_row(text, x, y, size=17):
    out = ""
    for label in ("PDF to Word", "Merge and split", "Office to PDF"):
        c, cw = chip(text, label, x, y, size)
        out += c
        x += cw + 10
    return out


def github_banner(text):
    """1280 x 320, top of the README (SPEC 3.5): ribbon, Sheet icon and wordmark on the left; line, chips and link on the right."""
    w, h = 1280, 320
    icon = placed(app_icon(lines=True, shadow=True), 136, 96, 112 / 64)
    wm, ww = text.path("serif", "Recto", 104, 256, 196, D["text"], SERIF_DISPLAY, tracking_em=-1 / 48)
    right = 256 + ww + 56
    tag, _ = text.path("serif", TAGLINE, 26, right, 140, D["text"], SERIF_TEXT)
    link, _ = text.path("ui", REPOSITORY, 18, right, 246, D["accentText"])
    return w, h, (f'<rect width="{w}" height="{h}" fill="{D["window"]}"/>' + ribbon(64, 28, 92, D["accent"]) + icon + wm + tag
                  + chips_row(text, right, 162, 15) + link)


def github_social_preview(text):
    """1280 x 640, the repository's social preview (SPEC 3.5)."""
    w, h = 1280, 640
    icon = placed(app_icon(lines=True, shadow=True), 72, 138, 112 / 64)
    wm, _ = text.path("serif", "Recto", 132, 196, 236, D["text"], SERIF_DISPLAY, tracking_em=-1 / 48)
    l1, _ = text.path("serif", "Open-source PDF viewer and", 40, 80, 322, D["text"], SERIF_TEXT)
    l2, _ = text.path("serif", "converter for Windows.", 40, 80, 370, D["text"], SERIF_TEXT)
    sub, _ = text.path("ui", "Free, offline by default. Native C++ from engine to UI.", 20, 80, 420, D["textSecondary"])
    link, _ = text.path("ui", REPOSITORY, 20, 80, 536, D["accentText"])
    return w, h, (f'<rect width="{w}" height="{h}" fill="{D["window"]}"/>' + window_sketch(700, 96, 640, 600)
                  + ribbon(80, 36, 112, D["accent"]) + icon + wm + l1 + l2 + sub + chips_row(text, 80, 446) + link)


def wizard_image(text):
    """Inno Setup WizardImageFile, drawn on a 164 x 314 canvas (the area's fixed aspect ratio, SPEC 3.6)."""
    w, h = 164, 314
    icon = placed(app_icon(lines=True, shadow=True), 34, 92, 96 / 64)
    ww = text.width("serif", "Recto", 34, SERIF_DISPLAY, tracking_em=-1 / 48)
    wm, _ = text.path("serif", "Recto", 34, (w - ww) / 2, 236, D["text"], SERIF_DISPLAY, tracking_em=-1 / 48)
    return w, h, f'<rect width="{w}" height="{h}" fill="{D["window"]}"/>' + ribbon(22, 16, 52, D["accent"]) + icon + wm


def wizard_small(_text):
    """Inno Setup WizardSmallImageFile, drawn on a 58 x 58 canvas (the area is square): the icon, on transparent."""
    return 58, 58, placed(app_icon(lines=True), 4, 4, 50 / 64)


def scaled(builder, text, size=None):
    """Renders a builder's canvas at its own size, or at an exact output size with the same aspect ratio."""
    w, h, inner = builder(text)
    W, H = size or (w, h)
    return W, H, svg(w, h, inner, W, H)


# ---------------------------------------------------------------- PNG, ICO and rasterising

_PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
_PNG_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
_PNG_BIT_DEPTHS = {0: {1, 2, 4, 8, 16}, 2: {8, 16}, 3: {1, 2, 4, 8}, 4: {8, 16}, 6: {8, 16}}


def _png_chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def _decode_png_row_rgba(recon: bytearray, width: int, bit_depth: int, color_type: int,
                         palette: bytes, trns: bytes) -> bytes:
    if color_type == 6 and bit_depth == 8:
        return bytes(recon)
    if color_type == 2 and bit_depth == 8:
        t_rgb = struct.unpack(">HHH", trns) if len(trns) == 6 else None
        out = bytearray(width * 4)
        for x in range(width):
            r = recon[x * 3]
            g = recon[x * 3 + 1]
            b = recon[x * 3 + 2]
            a = 0 if (t_rgb is not None and (r, g, b) == t_rgb) else 255
            o = x * 4
            out[o] = r
            out[o + 1] = g
            out[o + 2] = b
            out[o + 3] = a
        return bytes(out)

    def _samples(count: int) -> list[int]:
        if bit_depth == 8:
            return list(recon)
        if bit_depth == 16:
            return [((recon[i * 2] << 8) | recon[i * 2 + 1]) for i in range(count)]
        mask = (1 << bit_depth) - 1
        vals: list[int] = []
        for byte in recon:
            for shift in range(8 - bit_depth, -1, -bit_depth):
                vals.append((byte >> shift) & mask)
                if len(vals) == count:
                    return vals
        return vals

    def _scale8(val: int) -> int:
        if bit_depth == 8:
            return val
        if bit_depth == 16:
            return val >> 8
        maxv = (1 << bit_depth) - 1
        return round(val * 255 / maxv)

    chans = _PNG_CHANNELS[color_type]
    vals = _samples(width * chans)
    out = bytearray(width * 4)
    if color_type == 0:
        t_gray = struct.unpack(">H", trns)[0] if len(trns) == 2 else None
        for x in range(width):
            v = vals[x]
            g = _scale8(v)
            a = 0 if (t_gray is not None and v == t_gray) else 255
            o = x * 4
            out[o] = out[o + 1] = out[o + 2] = g
            out[o + 3] = a
    elif color_type == 2:
        t_rgb = struct.unpack(">HHH", trns) if len(trns) == 6 else None
        for x in range(width):
            r0, g0, b0 = vals[x * 3], vals[x * 3 + 1], vals[x * 3 + 2]
            a = 0 if (t_rgb is not None and (r0, g0, b0) == t_rgb) else 255
            o = x * 4
            out[o] = _scale8(r0)
            out[o + 1] = _scale8(g0)
            out[o + 2] = _scale8(b0)
            out[o + 3] = a
    elif color_type == 3:
        for x in range(width):
            idx = vals[x]
            o = x * 4
            out[o] = palette[idx * 3]
            out[o + 1] = palette[idx * 3 + 1]
            out[o + 2] = palette[idx * 3 + 2]
            out[o + 3] = trns[idx] if idx < len(trns) else 255
    elif color_type == 4:
        for x in range(width):
            g = _scale8(vals[x * 2])
            a = _scale8(vals[x * 2 + 1])
            o = x * 4
            out[o] = out[o + 1] = out[o + 2] = g
            out[o + 3] = a
    elif color_type == 6:
        for x in range(width):
            o = x * 4
            out[o] = _scale8(vals[o])
            out[o + 1] = _scale8(vals[o + 1])
            out[o + 2] = _scale8(vals[o + 2])
            out[o + 3] = _scale8(vals[o + 3])
    return bytes(out)


def read_png(raw: bytes) -> tuple[int, int, bytes]:
    """Decode a non-interlaced PNG byte buffer into `(width, height, rgba_bytes)`."""
    if not raw.startswith(_PNG_SIGNATURE):
        raise ValueError("Invalid PNG signature")
    pos = len(_PNG_SIGNATURE)
    width = height = bit_depth = color_type = 0
    saw_ihdr = False
    palette = b""
    trns = b""
    idat_parts: list[bytes] = []
    while pos + 12 <= len(raw):
        length, ctype = struct.unpack_from(">I4s", raw, pos)
        pos += 8
        if pos + length + 4 > len(raw):
            raise ValueError("Truncated PNG chunk")
        chunk_data = raw[pos : pos + length]
        crc = struct.unpack_from(">I", raw, pos + length)[0]
        pos += length + 4
        if (zlib.crc32(ctype + chunk_data) & 0xFFFFFFFF) != crc:
            raise ValueError(f"CRC mismatch in PNG chunk {ctype!r}")
        if ctype == b"IHDR":
            if length != 13:
                raise ValueError("Invalid IHDR chunk length")
            width, height, bit_depth, color_type, comp, flt, interlace = struct.unpack(">IIBBBBB", chunk_data)
            if width <= 0 or height <= 0 or comp != 0 or flt != 0:
                raise ValueError("Unsupported PNG header parameters")
            if interlace != 0:
                raise ValueError("Interlaced PNGs are not supported")
            if color_type not in _PNG_CHANNELS or bit_depth not in _PNG_BIT_DEPTHS[color_type]:
                raise ValueError(f"Unsupported PNG color_type={color_type} bit_depth={bit_depth}")
            saw_ihdr = True
        elif ctype == b"PLTE":
            palette = chunk_data
        elif ctype == b"tRNS":
            trns = chunk_data
        elif ctype == b"IDAT":
            idat_parts.append(chunk_data)
        elif ctype == b"IEND":
            break
    if not saw_ihdr:
        raise ValueError("Missing IHDR chunk")
    if color_type == 3 and not palette:
        raise ValueError("Indexed PNG missing PLTE chunk")
    raw_pixels = zlib.decompress(b"".join(idat_parts))
    chans = _PNG_CHANNELS[color_type]
    bpp = max(1, (chans * bit_depth + 7) // 8)
    row_bytes = (width * chans * bit_depth + 7) // 8
    stride = row_bytes + 1
    if len(raw_pixels) != height * stride:
        raise ValueError(f"Unexpected PNG scanline buffer size {len(raw_pixels)} != {height * stride}")
    prev = bytearray(row_bytes)
    mv = memoryview(raw_pixels)
    rgba_rows: list[bytes] = []
    for y in range(height):
        start = y * stride
        ftype = mv[start]
        row = mv[start + 1 : start + stride]
        if ftype == 0:
            recon = bytearray(row)
        elif ftype == 1:
            recon = bytearray(row)
            for i in range(bpp, row_bytes):
                recon[i] = (recon[i] + recon[i - bpp]) & 0xFF
        elif ftype == 2:
            recon = bytearray((row[i] + prev[i]) & 0xFF for i in range(row_bytes))
        elif ftype == 3:
            recon = bytearray(row)
            for i in range(bpp):
                recon[i] = (recon[i] + (prev[i] >> 1)) & 0xFF
            for i in range(bpp, row_bytes):
                recon[i] = (recon[i] + ((recon[i - bpp] + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            recon = bytearray(row)
            for i in range(bpp):
                recon[i] = (recon[i] + prev[i]) & 0xFF
            for i in range(bpp, row_bytes):
                a = recon[i - bpp]
                b = prev[i]
                c = prev[i - bpp]
                p = a + b - c
                pa = p - a if p >= a else a - p
                pb = p - b if p >= b else b - p
                pc = p - c if p >= c else c - p
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                recon[i] = (recon[i] + pr) & 0xFF
        else:
            raise ValueError(f"Unknown PNG scanline filter {ftype}")
        rgba_rows.append(_decode_png_row_rgba(recon, width, bit_depth, color_type, palette, trns))
        prev = recon
    return width, height, b"".join(rgba_rows)


def crop_rgba(sheet_w: int, sheet_h: int, rgba: bytes, x: int, y: int, w: int, h: int) -> bytes:
    """Slice a `(w, h)` RGBA rectangle starting at `(x, y)` from a decoded image buffer."""
    if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > sheet_w or y + h > sheet_h:
        raise ValueError(f"Crop bounds ({x}, {y}, {w}, {h}) exceed image ({sheet_w}, {sheet_h})")
    stride = sheet_w * 4
    row_start = x * 4
    row_len = w * 4
    return b"".join(rgba[row * stride + row_start : row * stride + row_start + row_len] for row in range(y, y + h))


def write_png(width: int, height: int, rgba: bytes) -> bytes:
    """Encode an 8-bit RGBA byte buffer (`width * height * 4` bytes) as a PNG."""
    expected = width * height * 4
    if width <= 0 or height <= 0 or len(rgba) != expected:
        raise ValueError(f"Expected {expected} RGBA bytes for {width}x{height}, got {len(rgba)}")
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    stride = width * 4
    raw = b"".join(b"\x00" + rgba[row * stride : (row + 1) * stride] for row in range(height))
    idat = zlib.compress(raw, level=6)
    return _PNG_SIGNATURE + _png_chunk(b"IHDR", ihdr) + _png_chunk(b"IDAT", idat) + _png_chunk(b"IEND", b"")


def pack_ico(frames: list[tuple[int, int, bytes]]) -> bytes:
    """Assemble a Windows ICO container from `(width, height, png_bytes)` frames."""
    if not frames:
        raise ValueError("pack_ico requires at least one frame")
    header = struct.pack("<HHH", 0, 1, len(frames))
    entries: list[bytes] = []
    payloads: list[bytes] = []
    offset = 6 + 16 * len(frames)
    for width, height, data in frames:
        if width <= 0 or width > 256 or height <= 0 or height > 256:
            raise ValueError(f"Invalid ICO frame dimensions {width}x{height}")
        w_byte = width if width < 256 else 0
        h_byte = height if height < 256 else 0
        entries.append(struct.pack("<BBBBHHII", w_byte, h_byte, 0, 0, 0, 32, len(data), offset))
        payloads.append(data)
        offset += len(data)
    return header + b"".join(entries) + b"".join(payloads)


def find_browser():
    override = os.environ.get("RECTO_RENDER_BROWSER", "").strip()
    if override:
        override_path = Path(override)
        if override_path.exists():
            return str(override_path)
        resolved = shutil.which(override)
        if resolved:
            return resolved
        raise SystemExit(f"RECTO_RENDER_BROWSER points to missing executable: {override}")
    for p in (r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Google\Chrome\Application\chrome.exe"):
        if os.path.exists(p):
            return p
    for candidate in ("msedge", "chrome", "google-chrome", "chromium", "chromium-browser"):
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
    raise SystemExit("Edge or Chrome is needed to rasterise the PNGs")


def wait_for_browser_exit(profile, timeout=15):
    """Edge keeps its profile locked for a moment after the screenshot; wait so the temp folder can be removed."""
    end = time.time() + timeout
    while (Path(profile) / "lockfile").exists() and time.time() < end:
        try:
            (Path(profile) / "lockfile").unlink()
        except OSError:
            time.sleep(0.25)


def _run_browser_screenshot(url: str, png_path: Path, width: int, height: int,
                            profile: Path, label: str, timeout: float = 60.0) -> bytes:
    """Run headless Edge/Chrome screenshot to `png_path` and wait until its bytes settle."""
    started = time.time()
    cmd = [find_browser(), "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
           "--default-background-color=00000000", f"--user-data-dir={profile}",
           "--allow-file-access-from-files", f"--window-size={width},{height}",
           f"--screenshot={png_path}", url]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   creationflags=CREATE_NO_WINDOW)
    last = -1
    deadline = started + timeout
    while time.time() < deadline:
        if png_path.exists() and png_path.stat().st_mtime >= started - 1:
            size = png_path.stat().st_size
            if size and size == last:
                wait_for_browser_exit(profile)
                return png_path.read_bytes()
            last = size
        time.sleep(0.25)
    raise SystemExit(f"the browser did not write {label}")


def rasterise(items, tmp):
    """items: list of (key, w, h, svg_markup). Packs them on one transparent sheet, screenshots it once, crops."""
    tmp = Path(tmp).resolve()
    sheet_w, gap = 4096, 8
    x = y = row_h = 0
    pos = {}
    for key, w, h, _ in sorted(items, key=lambda i: -i[2]):
        if x + w > sheet_w:
            x, y, row_h = 0, y + row_h + gap, 0
        pos[key] = (x, y)
        x += w + gap
        row_h = max(row_h, h)
    sheet_h = y + row_h
    body = "".join(f'<div style="position:absolute;left:{pos[k][0]}px;top:{pos[k][1]}px;width:{w}px;height:{h}px">{s}</div>'
                   for k, w, h, s in items)
    html = tmp / "sheet.html"
    html.write_text(f'<!doctype html><html><head><meta charset="utf-8"><style>html,body{{margin:0;background:transparent}}svg{{display:block}}</style></head><body>{body}</body></html>', encoding="utf-8")
    png_bytes = _run_browser_screenshot(html.as_uri(), tmp / "sheet.png", sheet_w, sheet_h, tmp / "profile",
                                        "the sprite sheet")
    actual_w, actual_h, sheet_rgba = read_png(png_bytes)
    return {k: write_png(w, h, crop_rgba(actual_w, actual_h, sheet_rgba, pos[k][0], pos[k][1], w, h))
            for k, w, h, _ in items}


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--newsreader", required=True, help="path to the Newsreader variable font (OFL)")
    ap.add_argument("--label-font", default=r"C:\Windows\Fonts\segoeuib.ttf", help="bold sans for the PDF label")
    ap.add_argument("--ui-font", default=r"C:\Windows\Fonts\segoeui.ttf", help="regular sans for brand graphic captions")
    ap.add_argument("--out-dir", type=Path, default=OUT, help="output directory for SVG/MSIX/ICO/installer resources")
    ap.add_argument("--brand-out-dir", type=Path, default=BRAND_OUT, help="output directory for brand graphics")
    args = ap.parse_args()

    out_dir = args.out_dir.resolve()
    brand_out_dir = args.brand_out_dir.resolve()
    if out_dir.exists():
        for sub in ("svg", "msix", "ico", "installer"):
            shutil.rmtree(out_dir / sub, ignore_errors=True)
    (out_dir / "svg").mkdir(parents=True, exist_ok=True)
    outliner = TextOutliner(args.newsreader, args.ui_font, args.label_font)

    # Wordmark: Newsreader Medium at the 72 pt optical size, tracking -1/48 em.
    wm_d, wx0, wy0, wx1, wy1, cap = outline_text(args.newsreader, "Recto", {"wght": 500, "opsz": 72}, tracking_em=-1 / 48)
    wm_w, wm_h = wx1 - wx0, wy1 - wy0

    def wordmark(color, height):
        s = height / wm_h
        return f'<path transform="translate({-wx0 * s:g} {-wy0 * s:g}) scale({s:g})" d="{wm_d}" fill="{color}"/>', wm_w * s

    def lockup(dark, height=64):
        # Icon at full height; wordmark cap height = 58% of the icon, baseline at 82% of the icon height.
        s = height * 0.58 / cap
        baseline = height * 0.82
        icon = placed(app_icon(lines=True), 0, 0, height / 64)
        x = height * 1.28
        text = f'<path transform="translate({x - wx0 * s:g} {baseline:g}) scale({s:g})" d="{wm_d}" fill="{D["text"] if dark else L["text"]}"/>'
        return icon + text, x + wm_w * s

    # "PDF" label outlines, centred in the 31 x 14 badge at (7, 43).
    lb_d, lx0, ly0, lx1, ly1, lcap = outline_text(args.label_font, "PDF", tracking_em=0.02)
    ls = 8.2 / lcap
    label = (f'<path transform="translate({7 + (31 - (lx1 - lx0) * ls) / 2 - lx0 * ls:g} {43 + (14 + 8.2) / 2:g}) scale({ls:g})" '
             f'd="{lb_d}" {paint(B["fileTypeBadgeText"])}/>')

    masters = {
        "app-icon.svg": svg(64, 64, app_icon(shadow=True), 256, 256, "Recto"),
        "app-icon-medium.svg": svg(64, 64, app_icon(), 32, 32, "Recto"),
        "app-icon-small.svg": svg(64, 64, app_icon(lines=False), 24, 24, "Recto"),
        "filetype-pdf.svg": svg(64, 64, filetype_icon(label), 256, 256, "PDF document"),
        "filetype-pdf-small.svg": svg(64, 64, filetype_icon(small=True), 24, 24, "PDF document"),
        "ribbon-marker.svg": svg(10, 22, f'<path d="M0 0 H10 V22 L5 17.2 L0 22 Z" fill="{L["accent"]}"/>', 10, 22),
        "ribbon-marker-dark.svg": svg(10, 22, f'<path d="M0 0 H10 V22 L5 17.2 L0 22 Z" fill="{D["accent"]}"/>', 10, 22),
        "drop-files-light.svg": drop_illustration(False),
        "drop-files-dark.svg": drop_illustration(True),
    }
    for dark in (False, True):
        tag = "dark" if dark else "light"
        inner, w = wordmark(D["text"] if dark else L["text"], 48)
        masters[f"wordmark-{tag}.svg"] = svg(round(w, 2), 48, inner, round(w, 2), 48, "Recto")
        inner, w = lockup(dark)
        masters[f"lockup-{tag}.svg"] = svg(round(w, 2), 64, inner, round(w, 2), 64, "Recto")
        for preset in ("focus", "classic", "readingroom"):
            masters[f"layout-{preset}-{tag}.svg"] = layout_thumb(preset, dark)
    for name, text in masters.items():
        write_text_atomically(out_dir / "svg" / name, text + "\n")

    # ------------------------------------------------------------ MSIX assets
    scales = {100: 1, 125: 1.25, 150: 1.5, 200: 2, 400: 4}
    targets = [16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 256]
    items, files = [], {}

    def add(path, w, h, inner_vb_w, inner_vb_h, inner):
        key = str(path)
        items.append((key, w, h, svg(inner_vb_w, inner_vb_h, inner, w, h)))
        files[key] = path

    A = out_dir / "msix" / "Assets"
    def icon_for(px, dark=False):
        # The icon has no tile, so the dark-taskbar (altform-unplated) art is the same drawing; dark is kept for clarity.
        return app_icon(lines=px > 24, shadow=px >= 48)

    for sc, f in scales.items():
        px = round(44 * f)
        add(A / f"Square44x44Logo.scale-{sc}.png", px, px, 64, 64, icon_for(px))
        for name, base, frac in (("Square150x150Logo", 150, 0.5), ("Square71x71Logo", 71, 0.62), ("Square310x310Logo", 310, 0.5)):
            p = round(base * f)
            add(A / f"{name}.scale-{sc}.png", p, p, p, p, placed(icon_for(p), p * (1 - frac) / 2, p * (1 - frac) / 2, p * frac / 64))
        ww, wh = round(310 * f), round(150 * f)
        ic = wh * 0.5
        add(A / f"Wide310x150Logo.scale-{sc}.png", ww, wh, ww, wh, placed(icon_for(ic), (ww - ic) / 2, (wh - ic) / 2, ic / 64))
        sp = round(50 * f)
        add(A / f"StoreLogo.scale-{sc}.png", sp, sp, 64, 64, icon_for(sp))
        sw, sh = round(620 * f), round(300 * f)
        inner, lw = lockup(True, sh * 0.26)
        add(A / f"SplashScreen.scale-{sc}.png", sw, sh, sw, sh, f'<g transform="translate({(sw - lw) / 2:g} {sh * 0.37:g})">{inner}</g>')
    for t in targets:
        add(A / f"Square44x44Logo.targetsize-{t}.png", t, t, 64, 64, icon_for(t))
        add(A / f"Square44x44Logo.targetsize-{t}_altform-unplated.png", t, t, 64, 64, icon_for(t, dark=True))
        add(A / f"Square44x44Logo.targetsize-{t}_altform-lightunplated.png", t, t, 64, 64, icon_for(t))
        add(A / "FileTypes" / f"PdfFile.targetsize-{t}.png", t, t, 64, 64, filetype_icon(small=True) if t <= 24 else filetype_icon(label))

    # ------------------------------------------------------------ brand graphics (design/brand) and installer images
    def add_built(path, builder, size=None):
        W, H, markup = scaled(builder, outliner, size)
        items.append((str(path), W, H, markup))
        files[str(path)] = path

    add_built(brand_out_dir / "github-banner.png", github_banner)
    add_built(brand_out_dir / "github-social-preview.png", github_social_preview)
    for sc, (large, small) in INSTALLER_SIZES.items():
        add_built(out_dir / "installer" / f"wizard-image-{sc}.png", wizard_image, large)
        add_built(out_dir / "installer" / f"wizard-small-{sc}.png", wizard_small, small)

    with tempfile.TemporaryDirectory(prefix="recto-res-", ignore_cleanup_errors=True) as tmp:
        crops = rasterise(items, Path(tmp))
    for key, png_bytes in crops.items():
        write_bytes_atomically(files[key], png_bytes)

    # ------------------------------------------------------------ ICO files from the hand-picked sizes
    (out_dir / "ico").mkdir(parents=True, exist_ok=True)
    def ico(out, pattern, sizes):
        frames = [(t, t, crops[str(A / pattern.format(t))]) for t in sizes]
        write_bytes_atomically(out, pack_ico(frames))
    ico_sizes = [16, 20, 24, 32, 40, 48, 64, 256]
    ico(out_dir / "ico" / "recto.ico", "Square44x44Logo.targetsize-{}.png", ico_sizes)
    ico(out_dir / "ico" / "pdf-file.ico", "FileTypes/PdfFile.targetsize-{}.png", ico_sizes)

    write_preview(masters, out_dir=out_dir, brand_out_dir=brand_out_dir)
    print(f"{len(masters)} SVG masters, {len(crops)} PNGs (including design/brand and the installer images), "
          f"2 ICO files and preview.png written")


def write_preview(masters, out_dir=OUT, brand_out_dir=BRAND_OUT):
    """resources/preview.png: every master on light and dark, plus the small PNG sizes at 1:1."""
    out_dir = Path(out_dir).resolve()
    brand_out_dir = Path(brand_out_dir).resolve()
    A = out_dir / "msix" / "Assets"
    def cell(name, bg, fg, scale=1.0):
        s = masters[name]
        return (f'<figure style="margin:0;display:flex;flex-direction:column;align-items:center;gap:8px">'
                f'<div style="background:{bg};border-radius:12px;padding:16px;display:flex;align-items:center;justify-content:center;zoom:{scale}">{s}</div>'
                f'<figcaption style="font:12px Segoe UI;color:{fg}">{name}</figcaption></figure>')
    def row(title, names, scale=1.0):
        return (f'<h2 style="font:600 14px Segoe UI;margin:24px 0 10px">{title}</h2><div style="display:flex;gap:24px;flex-wrap:wrap;align-items:flex-end">'
                + "".join(cell(n, "#FFFFFF" if "dark" not in n else D["window"], "#5C5C5C", scale) for n in names) + "</div>")
    def pngs(title, pattern, bg):
        imgs = "".join(f'<figure style="margin:0;text-align:center"><img src="{(A / pattern.format(t)).as_uri()}" style="image-rendering:pixelated;display:block;margin:auto">'
                       f'<figcaption style="font:11px Segoe UI;color:#5C5C5C;margin-top:4px">{t}</figcaption></figure>'
                       for t in (16, 20, 24, 32, 48, 64, 96, 256))
        return f'<h2 style="font:600 14px Segoe UI;margin:24px 0 10px">{title}</h2><div style="display:flex;gap:20px;align-items:flex-end;background:{bg};padding:16px;border-radius:12px">{imgs}</div>'
    html = ('<!doctype html><html><head><meta charset="utf-8"></head><body style="margin:0;padding:32px;background:#F4F5F8;width:1536px;font-family:Segoe UI">'
            '<h1 style="font:600 24px Segoe UI;margin:0">Recto resources</h1>'
            + row("App icon", ["app-icon.svg", "app-icon-medium.svg", "app-icon-small.svg"], 0.5)
            + pngs("App icon PNGs at 1:1 on the light taskbar (Square44x44Logo targetsize)", "Square44x44Logo.targetsize-{}.png", "#F3F3F3")
            + pngs("App icon PNGs on the dark taskbar (altform-unplated)", "Square44x44Logo.targetsize-{}_altform-unplated.png", "#202020")
            + row("PDF file-type icon", ["filetype-pdf.svg", "filetype-pdf-small.svg"], 0.5)
            + pngs("PDF file-type PNGs at 1:1", "FileTypes/PdfFile.targetsize-{}.png", "#FFFFFF")
            + row("Wordmark and lockup", ["wordmark-light.svg", "lockup-light.svg", "wordmark-dark.svg", "lockup-dark.svg"])
            + row("Layout preset cards (Settings)", [f"layout-{p}-{t}.svg" for t in ("light", "dark") for p in ("focus", "classic", "readingroom")])
            + row("In-app graphics", ["drop-files-light.svg", "drop-files-dark.svg", "ribbon-marker.svg", "ribbon-marker-dark.svg"])
            + f'<h2 style="font:600 14px Segoe UI;margin:24px 0 10px">Splash screen (scale 100, on #1B1A19)</h2><img src="{(A / "SplashScreen.scale-100.png").as_uri()}" style="background:{D["window"]};border-radius:12px;display:block">'
            + '<h2 style="font:600 14px Segoe UI;margin:24px 0 10px">Inno Setup wizard images (scale 100)</h2><div style="display:flex;gap:24px;align-items:flex-start">'
            + f'<img src="{(out_dir / "installer" / "wizard-image-100.png").as_uri()}" style="display:block">'
            + f'<img src="{(out_dir / "installer" / "wizard-small-100.png").as_uri()}" style="display:block;background:#FFFFFF;border-radius:8px">'
            + f'<img src="{(brand_out_dir / "github-banner.png").as_uri()}" style="display:block;width:640px;border-radius:8px"></div>'
            + '<h2 style="font:600 14px Segoe UI;margin:24px 0 10px">GitHub social preview (design/brand, shown at half size)</h2>'
            + f'<img src="{(brand_out_dir / "github-social-preview.png").as_uri()}" style="display:block;width:640px;border-radius:8px">'
            + '</body></html>')
    with tempfile.TemporaryDirectory(prefix="recto-prev-", ignore_cleanup_errors=True) as tmp:
        tmp_dir = Path(tmp).resolve()
        page = tmp_dir / "preview.html"
        page.write_text(html, encoding="utf-8")
        png_bytes = _run_browser_screenshot(page.as_uri(), tmp_dir / "preview.png", 1600, 3200,
                                            tmp_dir / "profile", "preview.png")
        write_bytes_atomically(out_dir / "preview.png", png_bytes)


if __name__ == "__main__":
    main()
