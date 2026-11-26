#!/usr/bin/env python3
"""The menu's controller art: a DualSense drawn as shapes (our own drawing, not Sony's art),
rasterised to runs of cells.

Each part is a mask drawn at SS times the cell size. Parts get a bevel (a light rim on the edges that
face up, a dark rim on those that face down), the body a soft glow and a top-to-bottom tone, the
buttons a highlight; the topmost layer at a cell names its colour slot. Every row of cells becomes
runs (x, length, slot), written as a C++ header that the mod draws with one overlay rectangle a run,
recolouring the part under the menu's cursor.

Usage: make_art.py OUT_HEADER [PREVIEW_PNG]
"""
import math
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

CELL = 0.5          # picture units (true length) a cell
W, H = 460, 320     # cells: 230 x 160 units
SS = 4              # supersampling

# Colour slots. The parts the menu lights have their own fill slot; the rest are shared looks.
SLOTS = [
    "none",
    # the body
    "glow", "edge", "body_top", "body_mid", "body_low", "rim_light", "rim_dark", "touchpad", "touch_rim", "lightbar",
    # parts the menu lights
    "l1", "l2", "r1", "r2", "dpad", "triangle", "circle", "cross", "square", "lstick", "rstick", "ps", "small",
    # shared looks
    "part_edge", "part_light", "part_dark", "well", "cap_ring", "sym_triangle", "sym_circle", "sym_cross", "sym_square",
    "mark",
]
S = {name: i for i, name in enumerate(SLOTS)}

layers = []  # (slot, mask), drawn in order: later on top


def new_mask():
    return Image.new("L", (W * SS, H * SS), 0)


def add(slot, mask):
    layers.append((S[slot], mask))


def P(x, y):
    """Design units (x right, y down, origin the controller's middle) to supersampled pixels."""
    return ((x / CELL + W / 2) * SS, (y / CELL + H / 2) * SS)


def px(units):
    return units / CELL * SS


def draw(mask):
    return ImageDraw.Draw(mask)


def ellipse(d, cx, cy, rx, ry, fill=255):
    x0, y0 = P(cx - rx, cy - ry)
    x1, y1 = P(cx + rx, cy + ry)
    d.ellipse([x0, y0, x1, y1], fill=fill)


def rrect(d, x0, y0, x1, y1, r, fill=255):
    a, b = P(x0, y0)
    c, e = P(x1, y1)
    d.rounded_rectangle([a, b, c, e], radius=px(r), fill=fill)


def rotated_ellipse_pts(cx, cy, rx, ry, angle_deg, n=96):
    a = math.radians(angle_deg)
    out = []
    for i in range(n):
        t = 2 * math.pi * i / n
        x, y = rx * math.cos(t), ry * math.sin(t)
        out.append((cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a)))
    return out


def grow(mask, units):
    k = int(px(abs(units)) * 2) | 1
    return mask.filter(ImageFilter.MaxFilter(k) if units > 0 else ImageFilter.MinFilter(k))


def shifted(mask, dx_units, dy_units):
    return ImageChops.offset(mask, int(px(dx_units)), int(px(dy_units)))


def minus(a, b):
    return ImageChops.subtract(a, b)


def both(a, b):
    return ImageChops.multiply(a, b)


def bevel(mask, width=1.2):
    """The rim of `mask` that faces up (light) and the one that faces down (dark)."""
    inner = grow(mask, -width)
    rim = minus(mask, inner)
    light = both(rim, minus(mask, shifted(mask, 0, width * 1.5)))   # where the shape ends above
    dark = both(rim, minus(mask, shifted(mask, 0, -width * 1.5)))   # where it ends below
    return light, dark


def part(slot, mask, edge=0.9, highlight=True):
    """A part: an edge round it, its fill, its bevel."""
    add("part_edge", grow(mask, edge))
    add(slot, mask)
    if highlight:
        light, dark = bevel(mask, 0.9)
        add("part_light", light)
        add("part_dark", dark)


# ---- The body ----
def body_shape(d):
    rrect(d, -70, -44, 70, 16, 18)                                  # the top
    ellipse(d, -60, -12, 40, 34)                                    # the lobes
    ellipse(d, 60, -12, 40, 34)
    d.polygon([P(*q) for q in rotated_ellipse_pts(-70, 20, 23, 36, 28)], fill=255)  # the grips
    d.polygon([P(*q) for q in rotated_ellipse_pts(70, 20, 23, 36, -28)], fill=255)
    rrect(d, -50, -10, 50, 36, 14)                                  # between the sticks
    ellipse(d, -72, 2, 30, 30)                                      # the sides, lobe into grip
    ellipse(d, 72, 2, 30, 30)


body = new_mask()
body_shape(draw(body))
draw(body).ellipse([*P(-56, 30), *P(56, 102)], fill=0)              # the curve between the grips
close = int(px(7.0)) | 1
body = body.filter(ImageFilter.MaxFilter(close)).filter(ImageFilter.MinFilter(close))

# The triggers and bumpers, behind the body: L2/R2 triggers with a rounded front, L1/R1 bumpers
# along the top.
for slot, side in (("l2", -1), ("r2", 1)):
    m = new_mask()
    x0, x1 = (-84, -46) if side < 0 else (46, 84)
    rrect(draw(m), x0, -64, x1, -44, 7)
    part(slot, m)
    hi = new_mask()
    rrect(draw(hi), x0 + 4, -61.5, x1 - 4, -59.5, 1)
    add("part_light", hi)
# The glow, then the body's edge, its tones, its bevel.
add("glow", grow(body, 3.2))
add("edge", grow(body, 1.4))
add("body_low", body)
mid = new_mask()
draw(mid).rectangle([0, 0, W * SS, P(0, 30)[1]], fill=255)
add("body_mid", both(body, mid))
top = new_mask()
draw(top).rectangle([0, 0, W * SS, P(0, -8)[1]], fill=255)
add("body_top", both(body, top))
light, dark = bevel(body, 1.6)
add("rim_light", light)
add("rim_dark", dark)

# L1 and R1: bumpers along the body's top edge, in front of it.
for slot, side in (("l1", -1), ("r1", 1)):
    m = new_mask()
    x0, x1 = (-88, -44) if side < 0 else (44, 88)
    rrect(draw(m), x0, -51, x1, -42, 4.5)
    part(slot, m)

# The touchpad, wider at the top, bevelled; the light bar's strips glowing beside it.
tp = new_mask()
draw(tp).polygon([P(-34.5, -44.5), P(34.5, -44.5), P(30, -13.5), P(-30, -13.5)], fill=255)
tp = tp.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))
add("touch_rim", grow(tp, 1.0))
add("touchpad", tp)
light, dark = bevel(tp, 0.8)
add("part_light", light)
add("part_dark", dark)
for side in (-1, 1):
    lb = new_mask()
    draw(lb).polygon([P(side * 39.0, -41), P(side * 36.6, -41), P(side * 31.6, -12.5), P(side * 34.0, -12.5)], fill=255)
    add("glow", grow(lb, 1.2))
    add("lightbar", lb)

# Create and Options: slanted pills, a mark each.
for side in (-1, 1):
    m = new_mask()
    draw(m).polygon([P(*q) for q in rotated_ellipse_pts(side * 43, -33, 2.2, 5.4, side * 18, 32)], fill=255)
    part("small", m, highlight=False)
    mk = new_mask()
    for k in (-1.6, 0, 1.6):
        draw(mk).line([P(side * 43 - 0.9, -33 + k + 0.15 * side), P(side * 43 + 0.9, -33 + k - 0.15 * side)], fill=255,
                      width=int(px(0.45)))
    add("mark", mk)

# The D-pad: a well, four bevelled arrows with a mark pointing out.
dx, dy = -60, -12
well = new_mask()
ellipse(draw(well), dx, dy, 16.5, 16.5)
add("well", well)
pad = new_mask()
for ang in (0, 90, 180, 270):
    a = math.radians(ang)
    ca, sa = math.cos(a), math.sin(a)
    shape = [(4.6, 4.6), (12.2, 4.6), (14.2, 2.6), (14.2, -2.6), (12.2, -4.6), (4.6, -4.6), (2.2, 0.0)]
    draw(pad).polygon([P(dx + x * ca - y * sa, dy + x * sa + y * ca) for x, y in shape], fill=255)
part("dpad", pad, edge=0.6)
mk = new_mask()
for ang in (0, 90, 180, 270):
    a = math.radians(ang)
    ca, sa = math.cos(a), math.sin(a)
    tri = [(11.6, 0.0), (9.2, 1.9), (9.2, -1.9)]
    draw(mk).polygon([P(dx + x * ca - y * sa, dy + x * sa + y * ca) for x, y in tri], fill=255)
add("mark", mk)

# The face buttons: raised discs in a well, their symbols in their colours.
fx, fy, sp = 60, -12, 11.6
face = {"triangle": (0, -sp), "circle": (sp, 0), "cross": (0, sp), "square": (-sp, 0)}
for name, (ox, oy) in face.items():
    w_ = new_mask()
    ellipse(draw(w_), fx + ox, fy + oy, 6.9, 6.9)
    add("well", w_)
for name, (ox, oy) in face.items():
    m = new_mask()
    ellipse(draw(m), fx + ox, fy + oy, 5.6, 5.6)
    part(name, m, edge=0.6)
    hi = new_mask()
    d = draw(hi)
    ellipse(d, fx + ox - 0.8, fy + oy - 1.4, 4.2, 3.4)
    ellipse(d, fx + ox - 0.2, fy + oy - 0.2, 4.4, 4.4, fill=0)
    add("part_light", hi)
sym = {k: new_mask() for k in face}
cx, cy = fx, fy - sp
draw(sym["triangle"]).polygon([P(cx, cy - 3.0), P(cx + 3.0, cy + 2.1), P(cx - 3.0, cy + 2.1)], fill=255)
draw(sym["triangle"]).polygon([P(cx, cy - 1.5), P(cx + 1.65, cy + 1.25), P(cx - 1.65, cy + 1.25)], fill=0)
cx, cy = fx + sp, fy
ellipse(draw(sym["circle"]), cx, cy, 3.0, 3.0)
ellipse(draw(sym["circle"]), cx, cy, 1.9, 1.9, fill=0)
cx, cy = fx, fy + sp
for a_, b_ in (((-2.4, -2.4), (2.4, 2.4)), ((-2.4, 2.4), (2.4, -2.4))):
    draw(sym["cross"]).line([P(cx + a_[0], cy + a_[1]), P(cx + b_[0], cy + b_[1])], fill=255, width=int(px(1.15)))
cx, cy = fx - sp, fy
rrect(draw(sym["square"]), cx - 2.6, cy - 2.6, cx + 2.6, cy + 2.6, 0.3)
rrect(draw(sym["square"]), cx - 1.55, cy - 1.55, cx + 1.55, cy + 1.55, 0.2, fill=0)
for k in face:
    add("sym_" + k, sym[k])

# The sticks: an edged well, the cap, a ring on the cap, its highlight.
for slot, sx in (("lstick", -26), ("rstick", 26)):
    w_ = new_mask()
    ellipse(draw(w_), sx, 12, 13.0, 13.0)
    add("part_edge", grow(w_, 1.0))
    add("well", w_)
    cap = new_mask()
    ellipse(draw(cap), sx, 12, 9.4, 9.4)
    part(slot, cap, edge=0.5)
    ring = new_mask()
    ellipse(draw(ring), sx, 12, 6.6, 6.6)
    ellipse(draw(ring), sx, 12, 5.6, 5.6, fill=0)
    add("cap_ring", ring)
    hi = new_mask()
    ellipse(draw(hi), sx - 2.2, 12 - 3.4, 4.6, 2.2)
    add("part_light", hi)

# The PS button (a plain disc: no logo) and the mute button.
m = new_mask()
ellipse(draw(m), 0, 9, 4.4, 4.4)
part("ps", m, edge=0.6)
m = new_mask()
rrect(draw(m), -3.6, 16.8, 3.6, 19.6, 1.3)
part("small", m, edge=0.5, highlight=False)
mk = new_mask()
draw(mk).line([P(-1.6, 18.2), P(1.6, 18.2)], fill=255, width=int(px(0.5)))
add("mark", mk)


def build():
    cells = [[0] * W for _ in range(H)]
    for slot, im in layers:
        small = im.resize((W, H), Image.BOX)
        p = small.load()
        for y in range(H):
            row = cells[y]
            for x in range(W):
                if p[x, y] >= 128:
                    row[x] = slot
    return cells


def anchors(cells):
    out = {}
    for name in ("l1", "l2", "r1", "r2", "dpad", "triangle", "circle", "cross", "square", "lstick", "rstick", "touchpad", "ps"):
        xs = ys = n = 0
        for y in range(H):
            for x in range(W):
                if cells[y][x] == S[name]:
                    xs += x
                    ys += y
                    n += 1
        out[name] = (xs / n, ys / n) if n else (0, 0)
    return out


def runs_of(cells):
    runs, row_start = [], []
    for y in range(H):
        row_start.append(len(runs))
        x, row = 0, cells[y]
        while x < W:
            if row[x] == 0:
                x += 1
                continue
            v, x0 = row[x], x
            while x < W and row[x] == v:
                x += 1
            runs.append((x0, x - x0, v))
    row_start.append(len(runs))
    return runs, row_start


def camel(name):
    return "".join(p.capitalize() for p in name.split("_"))


# The preview's colours (the mod's are in draw_dualsense).
PREVIEW = {
    "glow": (16, 60, 74), "edge": (143, 224, 255), "body_top": (18, 62, 76), "body_mid": (16, 57, 70), "body_low": (14, 51, 63),
    "rim_light": (60, 140, 165), "rim_dark": (8, 30, 40), "touchpad": (22, 74, 90), "touch_rim": (100, 190, 220),
    "lightbar": (170, 240, 255), "part_edge": (110, 200, 230), "part_light": (90, 175, 200), "part_dark": (12, 40, 52),
    "well": (6, 24, 34), "cap_ring": (14, 48, 60), "sym_triangle": (90, 220, 170), "sym_circle": (240, 110, 120),
    "sym_cross": (130, 175, 245), "sym_square": (230, 140, 200), "mark": (190, 235, 250),
}


def main():
    out = sys.argv[1]
    cells = build()
    runs, row_start = runs_of(cells)
    an = anchors(cells)
    with open(out, "w") as f:
        f.write("// Generated by tools/make_art.py: the menu's DualSense (our own drawing), as runs of cells.\n")
        f.write("// Do not edit; change the script and run it again.\n#pragma once\n#include <cstdint>\n\nnamespace art {\n\n")
        f.write(f"constexpr float kCell = {CELL}f;  // true units a cell\nconstexpr int kPadW = {W}, kPadH = {H};\n\n")
        f.write("enum Slot : std::uint8_t {\n" + "".join(f"    k{camel(n)} = {i},\n" for i, n in enumerate(SLOTS))
                + f"    kSlotCount = {len(SLOTS)},\n}};\n\n")
        f.write("struct Run {\n    std::uint16_t x;\n    std::uint16_t len;\n    std::uint8_t slot;\n};\n\n")
        f.write(f"constexpr Run kPadRuns[{len(runs)}] = {{\n")
        for i in range(0, len(runs), 6):
            f.write("    " + " ".join(f"{{{x}, {l}, {v}}}," for x, l, v in runs[i:i + 6]) + "\n")
        f.write("};\n\n")
        f.write(f"constexpr std::uint16_t kPadRowStart[{len(row_start)}] = {{\n")
        for i in range(0, len(row_start), 16):
            f.write("    " + " ".join(f"{v}," for v in row_start[i:i + 16]) + "\n")
        f.write("};\n\n// The parts' centres, in cells.\n")
        for name, (x, y) in an.items():
            f.write(f"constexpr float k{camel(name)}X = {x:.1f}f, k{camel(name)}Y = {y:.1f}f;\n")
        f.write("\n}  // namespace art\n")
    print(f"{len(runs)} runs, {W}x{H} cells")
    if len(sys.argv) > 2:
        im = Image.new("RGB", (W, H), (8, 30, 40))
        p = im.load()
        for y in range(H):
            for x in range(W):
                v = cells[y][x]
                if v == 0:
                    continue
                name = SLOTS[v]
                p[x, y] = PREVIEW.get(name, (122, 74, 16) if name == "l1" else (28, 90, 110))
        im.resize((W * 2, H * 2), Image.NEAREST).save(sys.argv[2])


if __name__ == "__main__":
    main()
