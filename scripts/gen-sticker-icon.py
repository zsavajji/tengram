"""Draws app/assets/images/stickers.png, the emoji picker's Stickers button (the user's ask, 2026-11-20: Telegram's sticker
mark, a rounded square with its bottom right corner peeled, a face and a tongue, in outline). 108 x 108 white on transparent
(the button shows it at 54 px; 2x for the Classic's and the Q10's density), tinted in QML with ImageView.filterColor, as the
images of gen-round.py are. Drawn at 4x as a mask and scaled down, so the strokes are anti-aliased.

    python3 -I scripts/gen-sticker-icon.py app/assets/images
"""
import sys

from PIL import Image, ImageDraw

SIDE = 108
S = 4
U = SIDE * S / 100  # the drawing is in a 100-unit box
W = round(5.5 * U)  # stroke


def p(x, y):
    return (x * U, y * U)


def bezier(a, c, b, n=40):
    return [p((1 - t) ** 2 * a[0] + 2 * (1 - t) * t * c[0] + t * t * b[0],
              (1 - t) ** 2 * a[1] + 2 * (1 - t) * t * c[1] + t * t * b[1]) for t in (i / n for i in range(n + 1))]


def main(out):
    mask = Image.new("L", (SIDE * S, SIDE * S), 0)
    d = ImageDraw.Draw(mask)
    # The square, its bottom right corner taken off and closed by one inward curve: the peel.
    d.rounded_rectangle([p(12, 12), p(88, 88)], radius=22 * U, outline=255, width=W)
    d.polygon([p(43, 101), p(101, 101), p(101, 43)], fill=0)  # x + y > 144, the peel's ends
    d.line(bezier((88, 56), (64, 64), (56, 88)), fill=255, width=W, joint="curve")
    # The eyes, the smile, and the tongue hanging from its right half.
    d.ellipse([p(33, 32), p(41, 44)], fill=255)
    d.ellipse([p(55, 32), p(63, 44)], fill=255)
    d.line(bezier((29, 55), (46, 70), (66, 56)), fill=255, width=W, joint="curve")
    d.polygon(bezier((48, 61), (55, 82), (62, 59)), fill=255)  # the tongue, filled
    for x, y in ((88, 56), (56, 88), (29, 55), (66, 56)):  # round caps on the open ends
        r = W / 2
        d.ellipse([x * U - r, y * U - r, x * U + r, y * U + r], fill=255)
    # Tilted as Telegram's mark is.
    mask = mask.rotate(12, resample=Image.BICUBIC)
    alpha = mask.resize((SIDE, SIDE), Image.LANCZOS)
    img = Image.new("RGBA", (SIDE, SIDE), (255, 255, 255, 0))
    img.putalpha(alpha)
    img.save(f"{out}/stickers.png")


if __name__ == "__main__":
    main(sys.argv[1])
