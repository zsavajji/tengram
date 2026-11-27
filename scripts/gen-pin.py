"""Draws the map pins of a location into app/assets/images/: map_pin.png (red, #E53935), shown over a location's map in the
chat (MediaBubble) and over the send-location page's map (LocationPage.qml), and map_pin_live.png (Tengram's blue, #24A1DE)
for a live location that is still running. And locate.png, the send-location page's "where am I" button: a ring with four
ticks and a dot, white on transparent, which QML tints as the jump buttons' images.

A pin is a teardrop about 60 px wide and 84 px tall with a white dot in its head and a thin white outline, so it reads on
any map, standing in the upper half of a 192 x 192 transparent square: the tip of its point (the outline's) is exactly the
square's centre, so an ImageView centred on a map puts the tip on the map's centre. Each image is drawn at 8x and scaled
down with LANCZOS. Run by hand when a pin changes:
    python3 scripts/gen-pin.py app/assets/images
"""
import math
import sys

from PIL import Image, ImageDraw

S = 8  # supersampling
N = 192  # the pin's square
R = 30  # the head's radius: the pin is 60 px wide
TALL = 84  # from the head's top to the tip
LINE = 3  # the white outline
DOT = 10  # the white dot's radius


def teardrop(d, cx, cy, r, tip, fill):
    """A circle of radius r at (cx, cy) and the wedge from it down to its point at (cx, tip), tangent to the circle."""
    a = math.asin(r / (tip - cy))  # the wedge's half angle at the point
    # The tangent points, relative to the centre: 90 - a degrees off straight down, (±r cos a, r sin a).
    sx, sy = r * math.cos(a), r * math.sin(a)
    d.polygon([(cx - sx, cy + sy), (cx, tip), (cx + sx, cy + sy)], fill)
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill)


def pin(out, name, color):
    n, r, line = N * S, R * S, LINE * S
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, tip = n / 2, n / 2
    cy = tip - TALL * S + r
    # The outline is the white pin; the coloured one is it inset by `line`, whose point sits line / sin(a) higher.
    teardrop(d, cx, cy, r, tip, (255, 255, 255, 255))
    a = math.asin(r / (tip - cy))
    teardrop(d, cx, cy, r - line, tip - line / math.sin(a), color)
    d.ellipse((cx - DOT * S, cy - DOT * S, cx + DOT * S, cy + DOT * S), (255, 255, 255, 255))
    img.resize((N, N), Image.LANCZOS).save(f"{out}/{name}.png")


def locate(out):
    b = 81 * S
    img = Image.new("RGBA", (b, b), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    white, w, c = (255, 255, 255, 255), round(0.07 * b), b / 2
    ring = 0.27 * b
    d.ellipse((c - ring, c - ring, c + ring, c + ring), None, white, w)
    for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
        d.line(((c + dx * (ring - w / 2), c + dy * (ring - w / 2)), (c + dx * 0.46 * b, c + dy * 0.46 * b)), white, w)
    dot = 0.1 * b
    d.ellipse((c - dot, c - dot, c + dot, c + dot), white)
    img.resize((81, 81), Image.LANCZOS).save(f"{out}/locate.png")


def main(out):
    pin(out, "map_pin", (0xE5, 0x39, 0x35, 255))
    pin(out, "map_pin_live", (0x24, 0xA1, 0xDE, 255))
    locate(out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "app/assets/images")
