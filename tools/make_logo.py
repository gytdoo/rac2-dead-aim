#!/usr/bin/env python3
"""Draws the Dead Aim logo: a flat, angular emblem after Deadlocked's DreadZone mark (dark shards
cut apart by white on a dark rim, one glowing visor) in Going Commando's blues, with Going
Commando's own crosshair at its heart, over a heavy chamfered DEAD AIM. Every shape is drawn here.

Writes docs/images/logo.svg and logo.png (the emblem over the wordmark) and emblem.svg and
emblem.png (the emblem alone). Needs rsvg-convert (librsvg) for the PNGs.
Usage: python3 tools/make_logo.py"""
import math
import os
import subprocess

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "images")

EDGE = "#071a3a"    # the outer rim
DARK = "#0b2a5c"    # the shards
WHITE = "#ffffff"   # the cuts between them
FIELD = "#5fb0ea"   # the open ground: a lighter Going Commando blue (its HUD's #70bfee, deeper)

GRADIENTS = (
    # the crosshair: white at the foot to Going Commando cyan
    '<linearGradient id="glow" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#eaffff"/>'
    '<stop offset="0.45" stop-color="#9fe8ff"/><stop offset="1" stop-color="#2fb4ff"/></linearGradient>'
    # the visor: white-hot at its point, blue up its arms
    '<linearGradient id="visor" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#ffffff"/>'
    '<stop offset="0.35" stop-color="#bff3ff"/><stop offset="0.75" stop-color="#4cc3ff"/>'
    '<stop offset="1" stop-color="#1f8fe6"/></linearGradient>'
    # the letters: the emblem's ground at the top down to its dark
    f'<linearGradient id="face" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{FIELD}"/>'
    f'<stop offset="0.48" stop-color="#1d63bd"/><stop offset="1" stop-color="{DARK}"/></linearGradient>'
)

# The letters, cap height 100, y down: heavy blocks with chamfered corners.
LETTERS = {
    "D": (98, [[(6, 0), (64, 0), (98, 32), (98, 68), (64, 100), (6, 100), (0, 94), (0, 6)]],
          [[(31, 27), (54, 27), (67, 40), (67, 60), (54, 73), (31, 73)]]),
    "E": (82, [[(6, 0), (82, 0), (82, 27), (31, 27), (31, 39), (70, 39), (70, 61), (31, 61), (31, 73), (82, 73),
                (82, 100), (6, 100), (0, 94), (0, 6)]], []),
    "A": (104, [[(0, 100), (27, 0), (77, 0), (104, 100), (72, 100), (66, 79), (38, 79), (32, 100)]],
          [[(43, 56), (61, 56), (54, 27), (50, 27)]]),
    "I": (31, [[(5, 0), (26, 0), (31, 5), (31, 95), (26, 100), (5, 100), (0, 95), (0, 5)]], []),
    "M": (116, [[(0, 100), (0, 6), (6, 0), (36, 0), (58, 33), (80, 0), (110, 0), (116, 6), (116, 100), (85, 100),
                 (85, 47), (64, 77), (52, 77), (31, 47), (31, 100)]], []),
}
GAP, SPACE, LEAN = 9, 36, 0.13


def mirror(pts, cx=500):
    return [(2 * cx - x, y) for x, y in reversed(pts)]


def offset(pts, d):
    """A simple polygon grown by d (negative: shrunk), its corners mitred."""
    n = len(pts)
    area = sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n)) / 2
    sgn = -1 if area > 0 else 1
    lines = []
    for i in range(n):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        dx, dy = x1 - x0, y1 - y0
        length = math.hypot(dx, dy)
        nx, ny = dy / length * sgn, -dx / length * sgn
        lines.append(((x0 - nx * d, y0 - ny * d), (dx, dy)))
    out = []
    for i in range(n):
        (p, r), (q, s) = lines[i - 1], lines[i]
        den = r[0] * s[1] - r[1] * s[0]
        if abs(den) < 1e-9:
            out.append(q)
            continue
        t = ((q[0] - p[0]) * s[1] - (q[1] - p[1]) * s[0]) / den
        out.append((p[0] + r[0] * t, p[1] + r[1] * t))
    return out


def path(pts):
    return "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in pts) + " Z"


def shard(pts):
    return (f'<path d="{path(pts)}" fill="{DARK}" stroke="{WHITE}" stroke-width="12" stroke-linejoin="miter" '
            f'paint-order="stroke"/>')


def emblem(tx=0, ty=0):
    """The emblem in a 1000x760 box (centre x 500), moved by tx, ty."""
    right = [(500, 150), (566, 150), (610, 236),   # the crown and its shoulder
             (960, 196), (690, 418),               # a wing, swept up and out, then back in
             (612, 452), (500, 650)]               # down to the point
    sil = right + mirror(right[1:-1])
    crown = [(522, 182), (556, 182), (588, 246), (536, 262)]
    upper = [(624, 262), (888, 226), (742, 322), (650, 334)]
    lower = [(752, 346), (676, 404), (628, 420), (656, 356)]
    tip = [(500, 196), (514, 236), (500, 284), (486, 236)]
    visor = [(533.9, 448), (568.3, 448), (500, 568.8), (431.7, 448), (466.1, 448), (500, 507.8)]
    g = [f'<g transform="translate({tx},{ty})">',
         # the rim: dark, a white line, a dark band, a white line, then the blue ground
         f'<path d="{path(offset(sil, 10))}" fill="{EDGE}" stroke="{EDGE}" stroke-width="4"/>',
         f'<path d="{path(offset(sil, -4))}" fill="{WHITE}"/>',
         f'<path d="{path(offset(sil, -14))}" fill="{DARK}"/>',
         f'<path d="{path(offset(sil, -26))}" fill="{WHITE}"/>',
         f'<path d="{path(offset(sil, -32))}" fill="{FIELD}"/>']
    for p in (crown, upper, lower):
        g += [shard(p), shard(mirror(p))]
    g.append(shard(tip))
    # the visor: a wide V, cut out in white, glowing
    g += [shard(visor), f'<path d="{path(offset(visor, -6))}" fill="url(#visor)"/>']
    # Going Commando's crosshair (the ring most guns show: a thick inner ring, a thin outer ring, a
    # pair of short bars at each side) glowing on a dark scope, a diamond at its heart
    cx, cy = 500, 352
    g.append(f'<circle cx="{cx}" cy="{cy}" r="84" fill="{DARK}" stroke="{WHITE}" stroke-width="12" paint-order="stroke"/>')
    for r0, r1 in ((38, 52), (59, 64)):
        g.append(f'<circle cx="{cx}" cy="{cy}" r="{(r0 + r1) / 2}" fill="none" stroke="url(#glow)" stroke-width="{r1 - r0}"/>')
    for ang in (0, 90, 180, 270):
        ux, uy = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        for off in (-9.0, 3.0):
            a, b, w = 66, 80, 6.0
            pts = [(cx + ux * a - uy * off, cy + uy * a + ux * off), (cx + ux * b - uy * off, cy + uy * b + ux * off),
                   (cx + ux * b - uy * (off + w), cy + uy * b + ux * (off + w)),
                   (cx + ux * a - uy * (off + w), cy + uy * a + ux * (off + w))]
            g.append(f'<path d="{path(pts)}" fill="url(#glow)"/>')
    g.append(f'<path d="{path([(cx, cy - 13), (cx + 9, cy), (cx, cy + 13), (cx - 9, cy)])}" fill="url(#glow)"/>')
    g.append("</g>")
    return "".join(g)


def wordmark(tx, ty, unit):
    """DEAD AIM, leaning forward: a dark rim, a white line, the blue face. Returns (svg, width)."""
    parts, x = [], 0
    for c in "DEAD AIM":
        if c == " ":
            x += SPACE - GAP
            continue
        w, outs, holes = LETTERS[c]
        for poly in outs + holes:
            parts.append(path([((x + px + (100 - py) * LEAN) * unit, py * unit) for px, py in poly]))
        x += w + GAP
    d = " ".join(parts)
    width = (x - GAP + 100 * LEAN) * unit
    svg = (f'<g transform="translate({tx(width):.1f},{ty})">'
           f'<path d="{d}" fill="{EDGE}" fill-rule="evenodd" stroke="{EDGE}" stroke-width="34" stroke-miterlimit="3"/>'
           f'<path d="{d}" fill="{WHITE}" fill-rule="evenodd" stroke="{WHITE}" stroke-width="18" stroke-miterlimit="3"/>'
           f'<path d="{d}" fill="url(#face)" fill-rule="evenodd"/></g>')
    return svg, width


def svg(width, height, view, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="{view}">'
            f'<defs>{GRADIENTS}</defs>{body}</svg>')


def write(name, text, px):
    p = os.path.join(OUT, name)
    with open(p + ".svg", "w") as f:
        f.write(text)
    subprocess.run(["rsvg-convert", "-w", str(px), "-o", p + ".png", p + ".svg"], check=True)
    print(f"{p}.svg, {p}.png")


def main():
    # The logo: the emblem with the wordmark across its point, cropped to the art.
    word, _ = wordmark(lambda w: (1400 - w) / 2, 610, 1.7)
    write("logo", svg(1282, 671, "59 132 1282 671", emblem(200, 0) + word), 1282)
    # The emblem alone.
    write("emblem", svg(1010, 549, "-5 132 1010 549", emblem()), 1010)


if __name__ == "__main__":
    main()
