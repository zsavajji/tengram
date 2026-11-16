#!/usr/bin/env python3
"""Tengram icon: the Telegram paper plane as a real 7-piece tangram.

Writes assets/icon.svg. PNGs: see the commands at the bottom of this file.
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


def main():
    check_tangram()
    c, s = math.cos(math.radians(ANGLE)), math.sin(math.radians(ANGLE))
    total = sum(area(p) for _, p in PIECES)
    cx = sum(area(p) * centroid(p)[0] for _, p in PIECES) / total
    cy = sum(area(p) * centroid(p)[1] for _, p in PIECES) / total

    def place(q):
        x, y = (q[0] - cx) * SCALE, (q[1] - cy) * SCALE
        return (SIZE / 2 + OFFSET[0] + x * c - y * s, SIZE / 2 + OFFSET[1] + x * s + y * c)

    polys = []
    for fill, p in PIECES:
        pts = inset([place(q) for q in p], GAP / 2 + ROUND)
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


if __name__ == "__main__":
    main()

# PNGs (Passport 144, Z10/Z30 114, Q10 96, Z3 90). ImageMagick's own SVG
# renderer drops the gradient and the rounded joins, so rasterise with Chrome:
#   chrome --headless --default-background-color=00000000 --window-size=512,512 \
#     --screenshot=/tmp/icon.png file://$PWD/assets/icon.svg
#   for s in 144 114 96 90; do magick /tmp/icon.png -resize ${s}x${s} assets/icon-$s.png; done
