"""Draws the Round look's images into app/assets/images/ (Settings > Appearance > Shape, the user's ask, 2026-11-19):
Cascades 10.3 has no corner radius and no clipping mask, so every round shape is an image tinted with
ImageView.filterColor, as the cards' and the call screen's are.

  circle.png    a 256 x 256 white disc: the tiles' colour, the buttons, the carousel's pencil and cross.
  pill.png      a 42 x 42 white rounded square, radius 20, with pill.amd (sliceMargins 20): the toggles and chips,
                nine-sliced so the ends stay round at any width. 42 and not 40: the middle slice is 2 px, not none,
                so the stretch has something to repeat.
  card_round_l.png, card_round_r.png, and their .amd   a 48 x 48 white card with a 20 px radius on three corners and
                none on the fourth, the one at the tail (l: bottom left, theirs; r: bottom right, ours), sliceMargins 20.
                MessageCard puts them in place of card.png in Round mode.

They're white on transparent. Each is drawn at 4x as a mask and scaled down, so the edge is anti-aliased, and only the
alpha is scaled: the colour is white everywhere, so nothing bleeds into the edge. Run by hand when the look changes,
in the image fetch-emoji.sh builds:
    docker run --rm -v "$PWD:/src" hyperberry-font python -I /src/scripts/gen-round.py /src/app/assets/images
"""
import sys

from PIL import Image, ImageDraw

S = 4  # supersampling


def save(mask, out, name):
    """The mask (drawn at 4x) as a white image with its alpha."""
    size = (mask.width // S, mask.height // S)
    img = Image.new("RGBA", size, (255, 255, 255, 0))
    img.putalpha(mask.resize(size, Image.LANCZOS))
    img.save(f"{out}/{name}.png")


def amd(out, name, margins):
    with open(f"{out}/{name}.amd", "w") as f:
        f.write(f'#RimCascadesAssetMetaData version=1.0\nsource: "{name}.png"\nsliceMargins: {margins}\n')


def rounded(size, radius, corners=(True, True, True, True)):
    """A mask of a square with the radius on the corners that are True: top left, top right, bottom right, bottom left."""
    mask = Image.new("L", (size * S, size * S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size * S - 1, size * S - 1), radius * S, 255, corners=corners)
    return mask


def main(out):
    mask = Image.new("L", (256 * S, 256 * S), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, 256 * S - 1, 256 * S - 1), 255)
    save(mask, out, "circle")

    save(rounded(42, 20), out, "pill")
    amd(out, "pill", "20 20 20 20")

    save(rounded(48, 20, (True, True, True, False)), out, "card_round_l")
    amd(out, "card_round_l", "20 20 20 20")
    save(rounded(48, 20, (True, True, False, True)), out, "card_round_r")
    amd(out, "card_round_r", "20 20 20 20")


if __name__ == "__main__":
    main(sys.argv[1])
