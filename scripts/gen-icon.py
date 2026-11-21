#!/usr/bin/env python3
"""Tengram icon: the Telegram paper plane as a real 7-piece tangram.

Writes assets/icon.svg (the design, to look at), and into app/assets/images, with Pillow: the app icon in
every size (icon-<px>.png), the launch screens (splash-<w>x<h>.png), and the Hub's glyphs of the plane alone in
one colour: hub_read2.png (grey), hub_unread2.png (yellow) and hub_account2.png (white, also the lock screen's).
"""
import math
import pathlib
from collections import Counter

# Pieces on the unit grid (small-triangle leg = 1), y down, nose at (6,0).
# Upper wing = the strip y in [0,1]; main wing = the two large triangles;
# bottom tip = the medium triangle; the notch (1,1)-(2,2)-(3,1) is the fold.
PIECES = [  # (fill, points)
    ("#ffffff", [(4, 0), (6, 0), (4, 2)]),          # large (nose)
    ("#e3eff8", [(4, 0), (4, 2), (2, 2)]),          # large
    ("#c8daea", [(2, 2), (4, 2), (3, 3)]),          # medium
    ("#ffffff", [(0, 0), (1, 0), (2, 1), (1, 1)]),  # parallelogram (wing tip)
    ("#d5e6f3", [(1, 0), (2, 0), (2, 1)]),          # small
    ("#f1f7fc", [(2, 0), (3, 0), (3, 1), (2, 1)]),  # square
    ("#d5e6f3", [(3, 0), (4, 0), (3, 1)]),          # small
]

SIZE = 512
ANGLE = -24      # degrees; nose up and to the right, like Telegram's
SCALE = 70       # px per tangram unit
OFFSET = (10, 6) # nudge of the plane's centroid from the circle centre
GAP = 8          # px between pieces
ROUND = 4        # px corner radius


def area(p):
    return abs(sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(p, p[1:] + p[:1]))) / 2


def centroid(p):  # vertex mean == area centroid for triangles and parallelograms
    return sum(x for x, _ in p) / len(p), sum(y for _, y in p) / len(p)


def contains(p, q):  # strict interior of a convex polygon
    s = [(b[0] - a[0]) * (q[1] - a[1]) - (b[1] - a[1]) * (q[0] - a[0])
         for a, b in zip(p, p[1:] + p[:1])]
    return all(v > 1e-9 for v in s) or all(v < -1e-9 for v in s)


def check_tangram():
    """Fails if this is not the seven tangram pieces, or if any two overlap."""
    def sig(p):
        sides = sorted(round(math.dist(a, b), 3) for a, b in zip(p, p[1:] + p[:1]))
        return tuple(sides), area(p)
    r2 = round(math.sqrt(2), 3)
    want = Counter({
        ((2, 2, round(2 * math.sqrt(2), 3)), 2.0): 2,  # large triangles
        ((r2, r2, 2), 1.0): 1,                         # medium triangle
        ((1, 1, r2), 0.5): 2,                          # small triangles
        ((1, 1, 1, 1), 1.0): 1,                        # square
        ((1, 1, r2, r2), 1.0): 1,                      # parallelogram
    })
    assert Counter(sig(p) for _, p in PIECES) == want, "not a tangram set"
    for i in range(140):
        for j in range(80):
            q = (i * 0.05 + 0.013, j * 0.05 + 0.017)
            assert sum(contains(p, q) for _, p in PIECES) <= 1, f"overlap at {q}"


def inset(p, d):
    """Shrink a convex polygon by d on every side."""
    if sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(p, p[1:] + p[:1])) < 0:
        p = p[::-1]
    out = []
    for i, v in enumerate(p):
        a, b = p[i - 1], p[(i + 1) % len(p)]
        n = []
        for s, e in ((a, v), (v, b)):
            l = math.dist(s, e)
            n.append((-(e[1] - s[1]) / l, (e[0] - s[0]) / l))
        k = d / (1 + n[0][0] * n[1][0] + n[0][1] * n[1][1])
        out.append((v[0] + k * (n[0][0] + n[1][0]), v[1] + k * (n[0][1] + n[1][1])))
    return out


def placed():
    """The pieces on the 512 px icon, turned and centred: [(fill, points)], before the gap is cut."""
    c, s = math.cos(math.radians(ANGLE)), math.sin(math.radians(ANGLE))
    total = sum(area(p) for _, p in PIECES)
    cx = sum(area(p) * centroid(p)[0] for _, p in PIECES) / total
    cy = sum(area(p) * centroid(p)[1] for _, p in PIECES) / total

    def place(q):
        x, y = (q[0] - cx) * SCALE, (q[1] - cy) * SCALE
        return (SIZE / 2 + OFFSET[0] + x * c - y * s, SIZE / 2 + OFFSET[1] + x * s + y * c)

    return [(fill, [place(q) for q in p]) for fill, p in PIECES]


def glyph(px, top, foot=None, fill=0.7, gap=0.04, out=None):
    """The icon in one colour, for the Hub: the app icon's disc with the plane cut out of it (the user,
    2026-11-17: inverted, the plane a cutout of the circle), an RGBA image px square, the disc `fill` of the
    box. `top` is its colour, fading straight down to `foot` when given, as the system's glyphs do. `gap` is
    the cut between the pieces as a share of the plane's width: wider than the app icon's, or it closes up at
    this size. HyperBerry's recipe (../hyperberry/scripts/gen-icons.py, app_icon with mono): drawn large,
    scaled down, painted flat over the shape and pasted with no mask, since a masked paste darkens the edges."""
    from PIL import Image, ImageDraw
    k = 4  # 512 * 4: drawn large
    n = SIZE * k
    polys = [p for _, p in placed()]
    xs = [x for p in polys for x, _ in p]
    cut = gap * (max(xs) - min(xs)) / 2
    mask = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(mask)
    d.ellipse((4 * k, 4 * k, n - 4 * k - 1, n - 4 * k - 1), 255)  # the app icon's disc
    for p in polys:
        pts = [(x * k, y * k) for x, y in inset(p, cut + ROUND)]
        d.polygon(pts, 0)
        d.line(pts + pts[:2], 0, 2 * ROUND * k, joint="curve")  # the icon's rounded corners
    mask = mask.crop(mask.getbbox())
    r = fill * px / max(mask.size)
    mask = mask.resize((round(mask.width * r), round(mask.height * r)), Image.LANCZOS)
    fade = Image.linear_gradient("L").resize(mask.size)
    paint = Image.composite(Image.new("RGBA", mask.size, foot or top), Image.new("RGBA", mask.size, top), fade)
    paint.putalpha(mask)
    box = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    box.paste(paint, ((px - paint.width) // 2, (px - paint.height) // 2))
    if out:
        box.save(out)
    return box


def icon(px):
    """The app icon as an RGBA image px square: what assets/icon.svg draws, by Pillow (so no browser is needed
    to rasterise it), at four times the size and scaled down."""
    from PIL import Image, ImageDraw
    k = 4
    n = SIZE * k
    top, foot = (0x2A, 0xAB, 0xEE, 255), (0x22, 0x9E, 0xD9, 255)
    fade = Image.linear_gradient("L").resize((n, n))
    disc = Image.composite(Image.new("RGBA", (n, n), foot), Image.new("RGBA", (n, n), top), fade)
    mask = Image.new("L", (n, n), 0)
    ImageDraw.Draw(mask).ellipse((4 * k, 4 * k, n - 4 * k - 1, n - 4 * k - 1), 255)
    disc.putalpha(mask)
    d = ImageDraw.Draw(disc)
    for fill, p in placed():
        pts = [(x * k, y * k) for x, y in inset(p, GAP / 2 + ROUND)]
        d.polygon(pts, fill)
        d.line(pts + pts[:2], fill, 2 * ROUND * k, joint="curve")
    return disc.resize((px, px), Image.LANCZOS)


def app_images(folder):
    """The home screen's icon in every size BB10 picks from (the Q10 takes 90), and the launch screen for each
    screen a portrait app can meet (Q10 and Classic, Z30, Z10, Passport): the icon on black, 30 % of the width,
    as HyperBerry's (../hyperberry/scripts/gen-icons.py)."""
    from PIL import Image
    # 288 is not the home screen's: WelcomePage.qml's logo, 32 du on the Q10.
    for px in (86, 90, 96, 110, 144, 288):
        icon(px).save(folder / f"icon-{px}.png")
    for w, h in ((720, 720), (720, 1280), (768, 1280), (1440, 1440)):
        splash = Image.new("RGB", (w, h), (0, 0, 0))
        side = round(0.3 * w)
        tile = icon(side)
        splash.paste(tile, ((w - side) // 2, (h - side) // 2), tile)
        splash.save(folder / f"splash-{w}x{h}.png")


def hub_glyphs(folder):
    """What the engine's Hub account names (engine/hub_qnx.cpp), 81 px as the Hub draws them. The Hub tints
    nothing: the engine swaps the file. Colours as HyperBerry sampled them from the Q10's own glyphs."""
    glyph(81, (0x9E, 0x9E, 0x9E, 255), (0x81, 0x81, 0x81, 255), out=folder / "hub_read2.png")     # a read item
    glyph(81, (0xFF, 0xD9, 0x1C, 255), (0xE0, 0xB2, 0x17, 255), out=folder / "hub_unread2.png")   # an unread one
    # The account's own, which the lock screen and the instant preview draw: white, larger, no fade.
    glyph(81, (255, 255, 255, 255), fill=0.9, out=folder / "hub_account2.png")
    # The Active Frame's idle glyph, as HyperBerry's cover_glyph.png: white, 64 px, no fade; cover.qml tints it.
    glyph(64, (255, 255, 255, 255), fill=1.0, out=folder / "cover_glyph.png")


def main():
    check_tangram()
    polys = []
    for fill, p in placed():
        pts = inset(p, GAP / 2 + ROUND)
        d = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
        polys.append(f'  <polygon points="{d}" fill="{fill}" stroke="{fill}" '
                     f'stroke-width="{2 * ROUND}" stroke-linejoin="round"/>')
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {SIZE} {SIZE}">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#2aabee"/>
      <stop offset="1" stop-color="#229ed9"/>
    </linearGradient>
  </defs>
  <circle cx="{SIZE // 2}" cy="{SIZE // 2}" r="{SIZE // 2 - 4}" fill="url(#bg)"/>
{chr(10).join(polys)}
</svg>
'''
    out = pathlib.Path(__file__).resolve().parent.parent / "assets" / "icon.svg"
    out.parent.mkdir(exist_ok=True)
    out.write_text(svg)
    print(out)
    images = out.parent.parent / "app" / "assets" / "images"
    app_images(images)
    hub_glyphs(images)
    print(images, "icon-*.png splash-*.png hub_read2.png hub_unread2.png hub_account2.png cover_glyph.png")


if __name__ == "__main__":
    main()
