"""Derives HyperBerry's emoji font from Google's Noto Color Emoji (PLAN.md §7, step 0.7).

Cascades draws text with Monotype iType, which loads a font through a FontFaceRule but
applies none of its GSUB. On the Q10 the original took 15 s and 177 MB to draw. So:
  - keep only the glyphs the cmap reaches: the sequence glyphs need GSUB (user, 2026-10-29:
    no substitutions), and so does nothing else
  - drop the cmap below U+0100 (digits, #, *, space, (c), (R)), so text never takes them from
    here instead of Slate Pro; the keycap's combining frame U+20E3 draws nothing
  - write each code point's bitmap as a CELL px PNG for the picker, from the original size
  - scale every colour bitmap from the font's 109 ppem down to PPEM, near the drawn size

    build-emoji-font.py <NotoColorEmoji.ttf> <HyperBerryEmoji.ttf> <png folder>
"""
import io
import os
import sys

from fontTools import subset
from fontTools.ttLib import TTFont
from PIL import Image

CELL = 54  # the picker's cell image: 6 du on the Q10

PPEM = 32  # the size Cascades draws emoji in body text on the Q10 (measured 2026-10-29)


def main(src, dst, pngs):
    font = TTFont(src)
    options = subset.Options()
    options.layout_features = []
    options.drop_tables += ["GSUB", "GPOS", "GDEF"]
    options.name_IDs = ["*"]
    sub = subset.Subsetter(options)
    sub.populate(unicodes=[u for u in font.getBestCmap() if u >= 0x100])
    sub.subset(font)
    # U+20E3, the keycap's frame, is a combining mark: with no GSUB to join it to its digit it
    # drew over the text before it, and the system has no glyph for it (a "?" box; Q10,
    # 2026-10-31). It takes ZWJ's glyph instead, empty and zero-wide, so "1️⃣" draws as "1".
    for table in font["cmap"].tables:
        table.cmap[0x20E3] = table.cmap[0x200D]

    os.makedirs(pngs, exist_ok=True)
    glyphs = font["CBDT"].strikeData[0]
    for cp, name in font.getBestCmap().items():
        if name not in glyphs:
            continue
        img = Image.open(io.BytesIO(glyphs[name].imageData)).convert("RGBA")
        k = min(CELL / img.width, CELL / img.height)
        img = img.resize((max(1, round(img.width * k)), max(1, round(img.height * k))), Image.LANCZOS)
        cell = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
        cell.paste(img, ((CELL - img.width) // 2, (CELL - img.height) // 2))
        cell.save(os.path.join(pngs, "%x.png" % cp), optimize=True)

    (strike,) = font["CBLC"].strikes
    size = strike.bitmapSizeTable
    scale = PPEM / size.ppemX
    for bitmap in font["CBDT"].strikeData[0].values():  # format 17: small metrics + PNG
        m = bitmap.metrics
        w, h = max(1, round(m.width * scale)), max(1, round(m.height * scale))
        out = io.BytesIO()
        Image.open(io.BytesIO(bitmap.imageData)).resize((w, h), Image.LANCZOS).save(
            out, "PNG", optimize=True)
        bitmap.imageData = out.getvalue()
        m.width, m.height = w, h
        m.BearingX, m.BearingY = round(m.BearingX * scale), round(m.BearingY * scale)
        m.Advance = round(m.Advance * scale)
    size.ppemX = size.ppemY = PPEM
    for lines in (size.hori, size.vert):
        for name in ("ascender", "descender", "widthMax", "minOriginSB", "minAdvanceSB",
                     "maxBeforeBL", "minAfterBL"):
            setattr(lines, name, round(getattr(lines, name) * scale))
    font.save(dst)
    print("%s: %d glyphs, %d code points, %d ppem" % (
        dst, len(font.getGlyphOrder()), len(font.getBestCmap()), PPEM))


if __name__ == "__main__":
    main(*sys.argv[1:])
