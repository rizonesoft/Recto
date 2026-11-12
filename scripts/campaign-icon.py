#!/usr/bin/env python3
"""Render a campaign launcher's icon: Recto's icon with its runner's badge in the corner.

    python scripts/campaign-icon.py <target.ico> <badge colour> [<mark.svg>]

Recto's icon comes from the committed `resources/ico/recto.ico`, each size from its own frame. The badge is a circle
in the runner's colour with a white outline, bottom right, the shape the Office and Isotone campaign shortcuts carry.
A runner's mark (the Claude symbol, the Gemini sparkle) is another company's trademark, so none is kept in this
repository: when the optional SVG file exists on this PC it is drawn in white inside the badge from 32 pixels up, and
without it the badge carries its colour alone. The icon is written beside the shortcut's other per-machine files,
never into the repository. Needs Pillow, and resvg_py only when a mark file is given.
"""

import io
import os
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

SOURCE = Path(__file__).resolve().parents[1] / "resources" / "ico" / "recto.ico"
MARK_FROM = 32  # below this the mark is a smudge: the colour alone tells the runners apart
OVERSAMPLE = 4  # the badge is drawn large and reduced, so its edge is smooth at every size


def mark_image(mark, side):
    """The mark as a white image `side` pixels square, or None when no usable mark file is on this PC."""
    if not mark or not Path(mark).is_file():
        return None
    import resvg_py
    data = bytes(resvg_py.svg_to_bytes(svg_path=str(mark), width=side, height=side))
    image = Image.open(io.BytesIO(data)).convert("RGBA")
    white = Image.new("RGBA", image.size, (255, 255, 255, 0))
    white.putalpha(image.getchannel("A"))  # whatever colour the file uses, the badge shows it in white
    return white


def frame(tile, fill, mark):
    size = tile.size[0]
    full = size * OVERSAMPLE
    big = Image.new("RGBA", (full, full), (0, 0, 0, 0))
    draw = ImageDraw.Draw(big)
    radius = round(full * 0.24)
    centre = full - radius - max(1, round(full * 0.01))
    ring = max(OVERSAMPLE, round(full * 0.025))
    draw.ellipse((centre - radius, centre - radius, centre + radius, centre + radius), fill="white")
    inner = radius - ring
    draw.ellipse((centre - inner, centre - inner, centre + inner, centre + inner), fill=fill)
    if size >= MARK_FROM:
        side = round(radius * 1.15)
        logo = mark_image(mark, side)
        if logo:
            big.alpha_composite(logo, (centre - side // 2, centre - side // 2))
    tile.alpha_composite(big.resize((size, size), Image.Resampling.LANCZOS))
    return tile


def main(argv):
    if len(argv) not in (2, 3) or not re.fullmatch(r"#[0-9A-Fa-f]{6}", argv[1]):
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    target, fill, mark = argv[0], argv[1], argv[2] if len(argv) == 3 else ""
    with Image.open(SOURCE) as icon:
        sizes = sorted(icon.ico.sizes())
        frames = [frame(icon.ico.getimage(size).convert("RGBA"), fill, mark) for size in sizes]
    partial = target + ".tmp"
    try:
        frames[-1].save(partial, format="ICO", sizes=sizes, append_images=frames[:-1])
        os.replace(partial, target)
    finally:
        if os.path.exists(partial):
            os.unlink(partial)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
