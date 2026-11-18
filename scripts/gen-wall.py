"""Draws the chat page's wallpaper into app/assets/images/: wall_dark.png and wall_bright.png (the user's
ask, 2026-11-18: Telegram's doodle wallpaper behind the cards). Each is 512x512, opaque, and tiles without a
seam: ChatPage.qml repeats it with an ImagePaintDefinition, and Cascades repeats only power-of-two images.

The doodles are our own thin line drawings, not Telegram's artwork: speech bubbles, squares, stars, hearts,
envelopes, music notes, clouds, smiley faces, plus signs and dots, 40 to 70 px, 3 px lines with round joins,
each turned by up to 30 degrees. They sit where a fixed seed (random.Random(10)) puts them, no two closer than
MIN px (the wrap-around distance, so the tile's edges keep the gap too). A doodle that crosses an edge is
drawn again at +-512 px, so the other side of the tile has its other half. The lines are a step off the base
colour: the wallpaper is felt, not read. Both themes use the same places.

Drawn at 4x on a canvas padded by the filter's reach, scaled down with LANCZOS and cropped. Run by hand when
the wallpaper changes, in the image fetch-emoji.sh builds (or anywhere with Pillow):
    docker run --rm -v "$PWD:/src" hyperberry-font python -I /src/scripts/gen-wall.py /src/app/assets/images
"""
import math
import random
import sys

from PIL import Image, ImageDraw

N = 512  # the tile
S = 4  # supersampling
PAD = 16  # the canvas's margin in supersampled px: more than LANCZOS reaches (3 px * S), so the edge is filtered as the middle
MIN = 75  # the least distance between two doodles' centres
LINE = 3 * S  # line width
M = int(1.6 * 70 * S)  # a doodle's square, big enough for 70 px turned
THEMES = {  # base, line
    "dark": ("#0E1621", "#1A2938"),
    "bright": ("#C9D8E3", "#B3C6D3"),
}
# How many of each; 24 in all.
KINDS = ["bubble"] * 3 + ["square"] * 2 + ["star"] * 3 + ["heart"] * 2 + ["envelope"] * 2 + ["note"] * 3 \
    + ["cloud"] * 3 + ["smiley"] * 2 + ["plus"] * 2 + ["dot"] * 2


def rgb(hex):
    return tuple(int(hex[i:i + 2], 16) for i in (1, 3, 5))


def arc(cx, cy, r, a0=0, a1=360, rx=1.0):
    n = max(24, int(abs(a1 - a0) / 4))
    return [(cx + rx * r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def bez(p0, p1, p2, n=16):
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
             (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]) for t in (i / n for i in range(n + 1))]


def heart():
    pts = [(16 * math.sin(t) ** 3,
            -(13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)))
           for t in (2 * math.pi * i / 60 for i in range(60))]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    k = 0.95 / (max(xs) - min(xs))
    mx, my = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    return [((x - mx) * k, (y - my) * k) for x, y in pts]


def cloud():
    """Three bumps on a flat base: each circle's arc that no other circle covers and that faces out (the left
    one's left half, the middle one's top, the right one's right half), and the base."""
    circles = [(-0.27, 0.08, 0.17), (-0.02, -0.1, 0.27), (0.27, 0.05, 0.2)]
    faces = [lambda x, y: x <= -0.27, lambda x, y: y <= -0.1, lambda x, y: x >= 0.27]
    strokes = [([(-0.27, 0.25), (0.27, 0.25)], False)]
    for i, (cx, cy, r) in enumerate(circles):
        runs, run, kept = [], [], []
        for x, y in arc(cx, cy, r, 0, 360):
            ok = faces[i](x, y) and y <= 0.2501 and all((x - ox) ** 2 + (y - oy) ** 2 >= r2 ** 2 - 1e-6
                                     for j, (ox, oy, r2) in enumerate(circles) if j != i)
            kept.append(ok)
            if ok:
                run.append((x, y))
            elif run:
                runs.append(run)
                run = []
        if run:  # the arc starts at 0 degrees: a run at the end carries on into the first
            if kept[0] and runs:
                runs[0] = run + runs[0]
            else:
                runs.append(run)
        strokes += [(r_, False) for r_ in runs]
    return strokes


def shape(kind):
    """A doodle in units of its size, centred on (0, 0): its strokes as (points, closed), and its dots as
    (x, y, radius in final px)."""
    if kind == "bubble":
        return [([(-.5, -.38), (.5, -.38), (.5, .14), (-.02, .14), (-.26, .42), (-.22, .14), (-.5, .14)], True),
                ([(-.3, -.17), (.3, -.17)], False), ([(-.3, -.02), (.12, -.02)], False)], []
    if kind == "square":
        return [([(-.4, -.4), (.4, -.4), (.4, .4), (-.4, .4)], True)], []
    if kind == "star":
        return [([((.5 if i % 2 == 0 else .22) * math.cos(math.radians(-90 + 36 * i)),
                   (.5 if i % 2 == 0 else .22) * math.sin(math.radians(-90 + 36 * i))) for i in range(10)], True)], []
    if kind == "heart":
        return [(heart(), True)], []
    if kind == "envelope":
        return [([(-.5, -.34), (.5, -.34), (.5, .34), (-.5, .34)], True),
                ([(-.5, -.34), (0, .08), (.5, -.34)], False)], []
    if kind == "note":
        return [(arc(-.2, .32, .16, rx=1.25), True), ([(-.03, .32), (-.03, -.42)], False),
                (bez((-.03, -.42), (.1, -.3), (.3, -.28)) + bez((.3, -.28), (.34, -.12), (.2, 0)), False)], []
    if kind == "cloud":
        return cloud(), []
    if kind == "smiley":
        return [(arc(0, 0, .45), True), (arc(0, .02, .26, 30, 150), False)], [(-.16, -.1, 2.5), (.16, -.1, 2.5)]
    if kind == "plus":
        return [([(-.3, 0), (.3, 0)], False), ([(0, -.3), (0, .3)], False)], []
    return [], [(0, 0, 5)]  # dot


def doodle(kind, size, angle):
    """The doodle as a mask, M x M at S times the size, turned about its middle."""
    mask = Image.new("L", (M, M), 0)
    d = ImageDraw.Draw(mask)
    u, c = size * S, M / 2
    strokes, dots = shape(kind)
    for pts, closed in strokes:
        pts = [(c + x * u, c + y * u) for x, y in pts]
        if closed:
            pts = pts + [pts[0]]
        d.line(pts, 255, LINE, joint="curve")
        for x, y in pts:  # round ends and joins
            d.ellipse((x - LINE / 2, y - LINE / 2, x + LINE / 2, y + LINE / 2), 255)
    for x, y, r in dots:
        x, y, r = c + x * u, c + y * u, r * S
        d.ellipse((x - r, y - r, x + r, y + r), 255)
    return mask.rotate(angle, resample=Image.BICUBIC)


def places():
    rng = random.Random(10)
    kinds = KINDS[:]
    rng.shuffle(kinds)
    out = []
    for kind in kinds:
        size, angle = rng.uniform(40, 70), rng.uniform(-30, 30)
        for _ in range(5000):
            x, y = rng.uniform(0, N), rng.uniform(0, N)
            if all(min(abs(x - ox), N - abs(x - ox)) ** 2 + min(abs(y - oy), N - abs(y - oy)) ** 2 >= MIN ** 2
                   for _, ox, oy, _ in out):
                out.append((kind, x, y, doodle(kind, size, angle)))
                break
        else:
            print(f"gen-wall.py: no room for a {kind}", file=sys.stderr)
    return out


def main(out):
    doodles = places()
    for name, (base, line) in THEMES.items():
        canvas = Image.new("RGB", (N * S + 2 * PAD,) * 2, rgb(base))
        for _, x, y, mask in doodles:
            for ox in (-N, 0, N):  # also at +-512, so a doodle across an edge wraps
                for oy in (-N, 0, N):
                    canvas.paste(rgb(line), (round((x + ox) * S + PAD - M / 2), round((y + oy) * S + PAD - M / 2)), mask)
        p = PAD // S
        canvas.resize((canvas.width // S,) * 2, Image.LANCZOS).crop((p, p, p + N, p + N)).save(f"{out}/wall_{name}.png")


if __name__ == "__main__":
    main(sys.argv[1])
