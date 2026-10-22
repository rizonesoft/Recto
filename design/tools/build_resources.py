"""Builds every icon and graphic Recto ships into resources/.

Usage (from the repository root):
    python design/tools/build_resources.py --newsreader <path to Newsreader[opsz,wght].ttf>

Writes:
    resources/svg/            vector masters (app icon, file-type icon, wordmark, lockups, in-app graphics)
    resources/msix/Assets/    PNGs named and sized for Package.appxmanifest
    resources/ico/            recto.ico and pdf-file.ico (Inno Setup builds, window icon)
    resources/installer/      Inno Setup WizardImageFile and WizardSmallImageFile PNGs at every scale
    design/brand/             the GitHub README banner and social preview
    resources/preview.png     a contact sheet of everything above

Text in the wordmark and the "PDF" label is converted to outlines, so the app does not need to
ship Newsreader or Segoe UI. PNGs are rasterised by headless Edge (or Chrome) from one sprite sheet
and cut apart with Pillow; ICO files are assembled from those PNGs so every size is hand-picked,
not downscaled from 256 px.
"""

import argparse
import json
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "resources"
BRAND_OUT = ROOT / "design" / "brand"
TOKENS = json.loads((ROOT / "design" / "tokens" / "colors.json").read_text(encoding="utf-8"))
L, D, B = TOKENS["light"], TOKENS["dark"], TOKENS["brand"]
ILL, PAGE_DARK = TOKENS["illustration"], TOKENS["pageColourInDarkTheme"]
REPOSITORY = "github.com/rizonesoft/Recto"
# Inno Setup image areas at each display scale with the default font (WizardImageFile and WizardSmallImageFile help):
# the large area keeps a 164:314 aspect ratio, the small one is square, and both grow with font metrics, not DPI.
INSTALLER_SIZES = {100: ((202, 386), (58, 58)), 125: ((269, 515), (77, 77)), 150: ((336, 643), (97, 97)),
                   175: ((403, 772), (116, 116)), 200: ((430, 824), (124, 124)), 225: ((498, 953), (143, 143)),
                   250: ((534, 1022), (159, 159))}


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

def outline_text(font_path, text, location=None, tracking_em=0.0):
    """Returns (svg path data in font units with y pointing down, x_min, y_min, x_max, y_max, cap_height)."""
    font = TTFont(font_path)
    glyphs = font.getGlyphSet(location=location) if location else font.getGlyphSet()
    cmap = font.getBestCmap()
    upm = font["head"].unitsPerEm
    cap = getattr(font["OS/2"], "sCapHeight", 0) or int(upm * 0.7)
    pen = SVGPathPen(glyphs)
    bounds = BoundsPen(glyphs)
    x = 0.0
    for ch in text:
        name = cmap[ord(ch)]
        g = glyphs[name]
        g.draw(TransformPen(pen, (1, 0, 0, -1, x, 0)))
        g.draw(TransformPen(bounds, (1, 0, 0, -1, x, 0)))
        x += g.width + tracking_em * upm
    x0, y0, x1, y1 = bounds.bounds
    return pen.getCommands(), x0, y0, x1, y1, cap


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
            self.fonts[key] = TTFont(self.paths[key])
        return self.fonts[key]

    def width(self, key, text, size, location=None, tracking_em=0.0):
        font = self._font(key)
        glyphs = font.getGlyphSet(location=location) if location else font.getGlyphSet()
        cmap, upm = font.getBestCmap(), font["head"].unitsPerEm
        return sum(glyphs[cmap[ord(c)]].width + tracking_em * upm for c in text) * size / upm

    def path(self, key, text, size, x, baseline, fill, location=None, tracking_em=0.0):
        """Returns (svg path element, advance width in px). (x, baseline) is the start of the baseline."""
        font = self._font(key)
        glyphs = font.getGlyphSet(location=location) if location else font.getGlyphSet()
        cmap, upm = font.getBestCmap(), font["head"].unitsPerEm
        pen = SVGPathPen(glyphs)
        advance = 0.0
        for ch in text:
            g = glyphs[cmap[ord(ch)]]
            g.draw(TransformPen(pen, (1, 0, 0, -1, advance, 0)))
            advance += g.width + tracking_em * upm
        s = size / upm
        return (f'<path transform="translate({x:g} {baseline:g}) scale({s:g})" d="{pen.getCommands()}" {paint(fill)}/>',
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
    sub, _ = text.path("ui", "Free, offline by default. Native C++20 from engine to UI.", 20, 80, 420, D["textSecondary"])
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


# ---------------------------------------------------------------- rasterising

def find_browser():
    for p in (r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Google\Chrome\Application\chrome.exe"):
        if os.path.exists(p):
            return p
    raise SystemExit("Edge or Chrome is needed to rasterise the PNGs")


def wait_for_browser_exit(profile, timeout=15):
    """Edge keeps its profile locked for a moment after the screenshot; wait so the temp folder can be removed."""
    end = time.time() + timeout
    while (Path(profile) / "lockfile").exists() and time.time() < end:
        try:
            (Path(profile) / "lockfile").unlink()
        except OSError:
            time.sleep(0.25)


def rasterise(items, tmp):
    """items: list of (key, w, h, svg_markup). Packs them on one transparent sheet, screenshots it once, crops."""
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
    png = tmp / "sheet.png"
    profile = tmp / "profile"
    started = time.time()
    subprocess.run([find_browser(), "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                    "--default-background-color=00000000", f"--user-data-dir={profile}", f"--window-size={sheet_w},{sheet_h}",
                    f"--screenshot={png}", html.as_uri()], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # msedge.exe can return before the PNG is written; wait for a new, stable file.
    last = -1
    for _ in range(240):
        if png.exists() and png.stat().st_mtime >= started - 1:
            size = png.stat().st_size
            if size and size == last:
                break
            last = size
        time.sleep(0.25)
    else:
        raise SystemExit("the browser did not write the sprite sheet")
    sheet = Image.open(png).convert("RGBA")
    sheet.load()
    wait_for_browser_exit(profile)
    return {k: sheet.crop((pos[k][0], pos[k][1], pos[k][0] + w, pos[k][1] + h)) for k, w, h, _ in items}


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--newsreader", required=True, help="path to the Newsreader variable font (OFL)")
    ap.add_argument("--label-font", default=r"C:\Windows\Fonts\segoeuib.ttf", help="bold sans for the PDF label")
    ap.add_argument("--ui-font", default=r"C:\Windows\Fonts\segoeui.ttf", help="regular sans for brand graphic captions")
    args = ap.parse_args()

    if OUT.exists():
        for sub in ("svg", "msix", "ico", "installer"):
            shutil.rmtree(OUT / sub, ignore_errors=True)
    (OUT / "svg").mkdir(parents=True, exist_ok=True)
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
        (OUT / "svg" / name).write_text(text + "\n", encoding="utf-8")

    # ------------------------------------------------------------ MSIX assets
    scales = {100: 1, 125: 1.25, 150: 1.5, 200: 2, 400: 4}
    targets = [16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 256]
    items, files = [], {}

    def add(path, w, h, inner_vb_w, inner_vb_h, inner):
        key = str(path)
        items.append((key, w, h, svg(inner_vb_w, inner_vb_h, inner, w, h)))
        files[key] = path

    A = OUT / "msix" / "Assets"
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

    add_built(BRAND_OUT / "github-banner.png", github_banner)
    add_built(BRAND_OUT / "github-social-preview.png", github_social_preview)
    for sc, (large, small) in INSTALLER_SIZES.items():
        add_built(OUT / "installer" / f"wizard-image-{sc}.png", wizard_image, large)
        add_built(OUT / "installer" / f"wizard-small-{sc}.png", wizard_small, small)

    with tempfile.TemporaryDirectory(prefix="recto-res-", ignore_cleanup_errors=True) as tmp:
        crops = rasterise(items, Path(tmp))
    for key, img in crops.items():
        files[key].parent.mkdir(parents=True, exist_ok=True)
        img.save(files[key])

    # ------------------------------------------------------------ ICO files from the hand-picked sizes
    (OUT / "ico").mkdir(parents=True, exist_ok=True)
    def ico(out, pattern, sizes):
        frames = [Image.open(A / pattern.format(t)) for t in sizes]
        frames[-1].save(out, format="ICO", sizes=[f.size for f in frames], append_images=frames[:-1])
    ico_sizes = [16, 20, 24, 32, 40, 48, 64, 256]
    ico(OUT / "ico" / "recto.ico", "Square44x44Logo.targetsize-{}.png", ico_sizes)
    ico(OUT / "ico" / "pdf-file.ico", "FileTypes/PdfFile.targetsize-{}.png", ico_sizes)

    write_preview(masters)
    print(f"{len(masters)} SVG masters, {len(crops)} PNGs (including design/brand and the installer images), "
          f"2 ICO files and preview.png written")


def write_preview(masters):
    """resources/preview.png: every master on light and dark, plus the small PNG sizes at 1:1."""
    A = OUT / "msix" / "Assets"
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
            + f'<img src="{(OUT / "installer" / "wizard-image-100.png").as_uri()}" style="display:block">'
            + f'<img src="{(OUT / "installer" / "wizard-small-100.png").as_uri()}" style="display:block;background:#FFFFFF;border-radius:8px">'
            + f'<img src="{(BRAND_OUT / "github-banner.png").as_uri()}" style="display:block;width:640px;border-radius:8px"></div>'
            + '<h2 style="font:600 14px Segoe UI;margin:24px 0 10px">GitHub social preview (design/brand, shown at half size)</h2>'
            + f'<img src="{(BRAND_OUT / "github-social-preview.png").as_uri()}" style="display:block;width:640px;border-radius:8px">'
            + '</body></html>')
    with tempfile.TemporaryDirectory(prefix="recto-prev-", ignore_cleanup_errors=True) as tmp:
        page = Path(tmp) / "preview.html"
        page.write_text(html, encoding="utf-8")
        out = OUT / "preview.png"
        started = time.time()
        subprocess.run([find_browser(), "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                        f"--user-data-dir={Path(tmp) / 'profile'}", "--allow-file-access-from-files", "--window-size=1600,3200",
                        f"--screenshot={out}", page.as_uri()], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        last = -1
        for _ in range(240):
            if out.exists() and out.stat().st_mtime >= started - 1:
                if out.stat().st_size == last and last > 0:
                    wait_for_browser_exit(Path(tmp) / "profile")
                    return
                last = out.stat().st_size
            time.sleep(0.25)
        raise SystemExit("the browser did not write preview.png")


if __name__ == "__main__":
    main()
