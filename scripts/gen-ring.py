"""Draws the download ring's 25 frames into app/assets/images/ring/ring_00.png to ring_24.png (frame k is k/24
filled), for MediaBubble: a photo, video or file being fetched shows a ring that fills, with a small cross
inside (a tap cancels). meegram2's paintProgress, scaled up: a track of white at alpha 0x60, an arc of white from
twelve o'clock clockwise, 3 px of a 32 px ring becoming 9 px of 88. The cross is a third of the ring wide, in the
same stroke.

They're white on transparent, so a file's bubble tints them with ImageView.filterColor (the theme's text colour).
Each is drawn at 4x and scaled down to 96 x 96. Run by hand when the look changes:
    python3 -I scripts/gen-ring.py app/assets/images/ring
"""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw

S = 4  # supersampling
SIZE = 96
OUTER = 88  # the ring's diameter
STROKE = 9
CROSS = 29  # the cross's width, a third of the ring
TRACK = 0x60
FRAMES = 25


def square(inset):
    """The box of the circle that is `inset` px inside the canvas, in supersampled pixels."""
    return (inset * S, inset * S, (SIZE - inset) * S, (SIZE - inset) * S)


def frame(k):
    big = SIZE * S
    ring = Image.new("L", (big, big), 0)
    d = ImageDraw.Draw(ring)
    margin = (SIZE - OUTER) / 2
    d.ellipse(square(margin), fill=255)
    d.ellipse(square(margin + STROKE), fill=0)
    alpha = ring.point(lambda v: v * TRACK // 255)
    if k:
        # pieslice's angles run clockwise from three o'clock: twelve is -90.
        pie = Image.new("L", (big, big), 0)
        ImageDraw.Draw(pie).pieslice(square(0), -90, -90 + 360 * k / (FRAMES - 1), fill=255)
        alpha = ImageChops.lighter(alpha, ImageChops.multiply(ring, pie))
    # The cross: two bars at 45 degrees, square ends. e is where a bar's centre line ends, so that the cross's
    # bounding box is CROSS wide.
    e = CROSS / 2 - STROKE / 2 / math.sqrt(2)
    cross = Image.new("L", (big, big), 0)
    d = ImageDraw.Draw(cross)
    c = SIZE / 2
    h = STROKE / 2 / math.sqrt(2)  # half the stroke along each axis
    for sign in (1, -1):
        x0, y0, x1, y1 = c - e, c - sign * e, c + e, c + sign * e
        d.polygon([((x0 - h) * S, (y0 + sign * h) * S), ((x0 + h) * S, (y0 - sign * h) * S),
                   ((x1 + h) * S, (y1 - sign * h) * S), ((x1 - h) * S, (y1 + sign * h) * S)], fill=255)
    alpha = ImageChops.lighter(alpha, cross)
    # Only the alpha is scaled: the colour is white everywhere, so nothing bleeds into the edges.
    out = Image.new("RGBA", (SIZE, SIZE), (255, 255, 255, 0))
    out.putalpha(alpha.resize((SIZE, SIZE), Image.LANCZOS))
    return out


if __name__ == "__main__":
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for k in range(FRAMES):
        frame(k).save(f"{out}/ring_{k:02d}.png")
