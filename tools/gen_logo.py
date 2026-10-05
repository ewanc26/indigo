#!/usr/bin/env python3
"""Generate Indigo's mark: docs/logo.svg and the 3DS icons in assets/.

The mark is an indigo bunting perched on a twig, side on, facing right. It is
described here as a handful of shapes (ellipses, a circle, polygons) and
rasterised, so the artwork is this file and nobody places a rectangle by hand.

- docs/logo.svg follows the Wolfram/MetalBear style: a 3x5 pixel grid in a
  viewBox 294 wide, horizontal runs as <rect>s, crispEdges, one .logo class
  coloured #15803d in light and #4ade80 in dark.
- assets/icon.png (48x48) and assets/icon-small.png (24x24) are the SMDH
  icons the Homebrew Menu shows. The same shapes are sampled on a square grid,
  the mark in #15803d on white, as Wolfram's docs/house-style.md requires. The small icon is drawn at its own
  size rather than left to smdhtool, whose fallback averages 2x2 blocks of the
  large one into a blur.

Standard library only. Usage: python3 tools/gen_logo.py [--check]
--check exits 1 if the committed files differ from what this would write.
"""
import math
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIDTH = 294          # viewBox width
CELL_W, CELL_H = 3, 5

# Design space: x 0..294, y 0..210.
DESIGN_H = 215


def rot(px, py, cx, cy, deg):
    a = math.radians(deg)
    dx, dy = px - cx, py - cy
    return cx + dx * math.cos(a) - dy * math.sin(a), cy + dx * math.sin(a) + dy * math.cos(a)


def in_ellipse(x, y, cx, cy, rx, ry, deg=0.0):
    # Undo the ellipse's rotation, then test the axis-aligned form.
    ux, uy = rot(x, y, cx, cy, -deg)
    return ((ux - cx) / rx) ** 2 + ((uy - cy) / ry) ** 2 <= 1.0


def in_poly(x, y, pts):
    inside = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


def in_capsule(x, y, ax, ay, bx, by, r):
    # Distance from the segment a-b.
    vx, vy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy)))
    return math.hypot(x - (ax + t * vx), y - (ay + t * vy)) <= r


def bunting(x, y, eye=7.0):
    """True where the mark is solid, in design coordinates."""
    # The eye is a hole, so the mark stays one colour. The 24px icon asks for
    # a larger one, or it vanishes below a pixel.
    if math.hypot(x - 214, y - 46) <= eye:
        return False
    body = in_ellipse(x, y, 146, 100, 74, 45, -24)
    head = math.hypot(x - 203, y - 52) <= 37
    # A finch's short conical beak.
    beak = in_poly(x, y, [(230, 38), (272, 54), (232, 68)])
    # The tail hangs behind the twig, as a perched bird's does.
    tail = in_poly(x, y, [(100, 110), (44, 200), (60, 206), (124, 134)])
    legs = in_capsule(x, y, 148, 136, 146, 182, 3.2) or in_capsule(x, y, 174, 130, 172, 182, 3.2)
    twig = in_capsule(x, y, 30, 186, 266, 178, 6.0)
    sprig = in_capsule(x, y, 238, 180, 262, 158, 3.4)
    return body or head or beak or tail or legs or twig or sprig


def svg():
    rows = DESIGN_H // CELL_H
    cols = WIDTH // CELL_W
    rects = []
    used_rows = []
    for r in range(rows):
        cy = r * CELL_H + CELL_H / 2
        c = 0
        run = []
        while c < cols:
            if bunting(c * CELL_W + CELL_W / 2, cy):
                s = c
                while c < cols and bunting(c * CELL_W + CELL_W / 2, cy):
                    c += 1
                run.append((s, c))
            else:
                c += 1
        if run:
            used_rows.append((r, run))
    top = used_rows[0][0]
    bottom = used_rows[-1][0]
    for r, runs in used_rows:
        for s, e in runs:
            rects.append('<rect x="%d" y="%d" width="%d" height="%d"/>'
                         % (s * CELL_W, (r - top) * CELL_H, (e - s) * CELL_W, CELL_H))
    height = (bottom - top + 1) * CELL_H
    style = ('@media (prefers-color-scheme: dark) { .logo { fill: #4ade80; } } '
             '@media (prefers-color-scheme: light), (prefers-color-scheme: no-preference) '
             '{ .logo { fill: #15803d; } }')
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" role="img" '
            'aria-label="Indigo logo"><style>%s</style><g class="logo" shape-rendering="crispEdges">%s</g></svg>\n'
            % (WIDTH, height, style, "".join(rects)))


def png(width, height, rgb_rows):
    raw = b"".join(b"\x00" + bytes(row) for row in rgb_rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def icon(size=48):
    # Wolfram docs/house-style.md: icons are the mark in #15803d on white,
    # never a new colour.
    bg = (0xFF, 0xFF, 0xFF)
    fg = (0x15, 0x80, 0x3D)
    # Fit the design box (0..294, 18..196 is the drawn extent) into the tile
    # with a margin, keeping it square-pixelled.
    x0, x1, y0, y1 = 22.0, 278.0, 12.0, 210.0
    margin = max(1, size // 12)
    scale = (size - 2 * margin) / max(x1 - x0, y1 - y0)
    ox = margin + ((size - 2 * margin) - (x1 - x0) * scale) / 2
    oy = margin + ((size - 2 * margin) - (y1 - y0) * scale) / 2
    rows = []
    for py in range(size):
        row = []
        for px in range(size):
            # 4x4 supersampling, thresholded: the icon is pixel art as well,
            # so a pixel is either mark or tile, never a blend.
            hits = 0
            for sy in range(4):
                for sx in range(4):
                    dx = x0 + (px + (sx + 0.5) / 4 - ox) / scale
                    dy = y0 + (py + (sy + 0.5) / 4 - oy) / scale
                    hits += bunting(dx, dy, 7.0 if size >= 48 else 12.0)
            row.extend(fg if hits >= 7 else bg)
        rows.append(row)
    return png(size, size, rows)


def main():
    outputs = {
        os.path.join(ROOT, "docs", "logo.svg"): svg().encode(),
        os.path.join(ROOT, "assets", "icon.png"): icon(48),
        os.path.join(ROOT, "assets", "icon-small.png"): icon(24),
    }
    check = "--check" in sys.argv[1:]
    stale = []
    for path, data in outputs.items():
        try:
            with open(path, "rb") as f:
                same = f.read() == data
        except FileNotFoundError:
            same = False
        if check:
            if not same:
                stale.append(os.path.relpath(path, ROOT))
        elif not same:
            with open(path, "wb") as f:
                f.write(data)
            print("wrote", os.path.relpath(path, ROOT))
    if stale:
        print("gen_logo: out of date, run python3 tools/gen_logo.py:", ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
