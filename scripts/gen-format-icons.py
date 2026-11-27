"""Draws the icons of the composer's formatting actions (Composer.qml's context menu; the user, 2026-11-27: the menu showed
the system's gear for each): format_bold.png (a bold B), format_italic.png (an italic I), format_strike.png (an S struck
through), format_spoiler.png (an eye struck out), format_mono.png (</>) and format_link.png (two chain links). 81 x 81
white on transparent, the size of the other action icons (overflow_action.png). Drawn at 4x as a mask and scaled down, so
the edges are anti-aliased. The letters are the Mac's Georgia (/System/Library/Fonts/Supplemental).

    python3 -I scripts/gen-format-icons.py app/assets/images
"""
import sys

from PIL import Image, ImageDraw, ImageFont

SIDE = 81
S = 4
N = SIDE * S
U = N / 100  # the drawings are in a 100-unit box
W = round(8 * U)  # a stroke
FONTS = "/System/Library/Fonts/Supplemental/"


def p(x, y):
    return (x * U, y * U)


def blank():
    mask = Image.new("L", (N, N), 0)
    return mask, ImageDraw.Draw(mask)


def save(mask, out, name):
    img = Image.new("RGBA", (SIDE, SIDE), (255, 255, 255, 0))
    img.putalpha(mask.resize((SIDE, SIDE), Image.LANCZOS))
    img.save(f"{out}/format_{name}.png")


def letter(char, font):
    mask, d = blank()
    d.text(p(50, 52), char, fill=255, font=ImageFont.truetype(FONTS + font, round(78 * U)), anchor="mm")
    return mask, d


def main(out):
    mask, d = letter("B", "Georgia Bold.ttf")
    save(mask, out, "bold")

    mask, d = letter("I", "Georgia Italic.ttf")
    save(mask, out, "italic")

    # The line's own gap is cleared first, so it reads apart from the letter.
    mask, d = letter("S", "Georgia.ttf")
    d.rectangle([p(10, 44), p(90, 60)], fill=0)
    d.rectangle([p(14, 48), p(86, 56)], fill=255)
    save(mask, out, "strike")

    # An eye (two arcs and the pupil) struck out from the top left, the stroke's gap cleared under it.
    mask, d = blank()
    d.arc([p(8, 20), p(92, 104)], 200, 340, fill=255, width=W)
    d.arc([p(8, -4), p(92, 80)], 20, 160, fill=255, width=W)
    d.ellipse([p(38, 38), p(62, 62)], fill=255)
    d.line([p(26, 12), p(86, 84)], fill=0, width=W * 2)
    d.line([p(20, 16), p(80, 88)], fill=255, width=W)
    save(mask, out, "spoiler")

    mask, d = blank()
    d.line([p(32, 28), p(10, 50), p(32, 72)], fill=255, width=W, joint="curve")
    d.line([p(68, 28), p(90, 50), p(68, 72)], fill=255, width=W, joint="curve")
    d.line([p(58, 20), p(42, 80)], fill=255, width=W)
    save(mask, out, "mono")

    # Two links drawn lying flat, then turned 45 degrees; the right one's end is cleared where it passes under the left's.
    mask, d = blank()
    d.rounded_rectangle([p(6, 36), p(56, 64)], radius=14 * U, outline=255, width=W)
    d.rectangle([p(40, 30), p(52, 70)], fill=0)
    d.rounded_rectangle([p(44, 36), p(94, 64)], radius=14 * U, outline=255, width=W)
    d.rectangle([p(48, 30), p(60, 46)], fill=0)
    save(mask.rotate(45, resample=Image.BICUBIC), out, "link")


if __name__ == "__main__":
    main(sys.argv[1])
