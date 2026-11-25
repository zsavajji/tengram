"""Draws app/assets/images/discuss.png, a channel's Discuss button in the chat's bottom bar (the user's ask, 2026-11-25: the
"double bubble", as Telegram's): two speech bubbles, the one behind in outline at the top left, the one in front filled at
the bottom right with a clear gap around it, each with its tail at its outer bottom corner. 81 x 81 white on transparent,
the size of the bar's other icons (overflow_action.png), tinted in QML with ImageView.filterColor. Drawn at 4x as a mask
and scaled down, so the edges are anti-aliased.

    python3 -I scripts/gen-discuss-icon.py app/assets/images
"""
import sys

from PIL import Image, ImageDraw

SIDE = 81
S = 4
U = SIDE * S / 100  # the drawing is in a 100-unit box
W = round(6 * U)  # the outline's stroke
GAP = 6 * U  # the clear ring around the front bubble


def p(x, y):
    return (x * U, y * U)


def main(out):
    mask = Image.new("L", (SIDE * S, SIDE * S), 0)
    d = ImageDraw.Draw(mask)
    # Behind: an outlined bubble, its tail at the bottom left.
    d.rounded_rectangle([p(8, 12), p(64, 56)], radius=14 * U, outline=255, width=W)
    d.polygon([p(16, 50), p(30, 50), p(12, 68)], fill=255)
    # In front: its box grown by the gap is cleared first, so the two read apart where they overlap; then the bubble, and
    # its tail at the bottom right.
    box = (40, 42, 92, 80)
    g = GAP / U
    d.rounded_rectangle([p(box[0] - g, box[1] - g), p(box[2] + g, box[3] + g)], radius=(12 + g) * U, fill=0)
    d.rounded_rectangle([p(box[0], box[1]), p(box[2], box[3])], radius=12 * U, fill=255)
    d.polygon([p(70, 74), p(86, 74), p(90, 92)], fill=255)
    alpha = mask.resize((SIDE, SIDE), Image.LANCZOS)
    img = Image.new("RGBA", (SIDE, SIDE), (255, 255, 255, 0))
    img.putalpha(alpha)
    img.save(f"{out}/discuss.png")


if __name__ == "__main__":
    main(sys.argv[1])
