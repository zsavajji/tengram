"""Draws the call screen's images (step 7.4) into app/assets/images/, in the stock BB10 Phone
app's look (measured from the user's screenshots, 720x720, 1 du = 9 px) but square, as Tengram's buttons
are: call_bg (the gradient with the default-contact silhouette), call_fade (a photo's fade to black),
call_track (the answer bar: a rectangle, a bracket at each end in the primary colour and in red, the red
handset, the chevrons; 665x148), call_knob and call_btn (the square knob and button) and the white
glyphs call_message, call_speaker, call_mute, call_unmute and call_end, which QML tints with
ImageView.filterColor. Everything is drawn at 8x and scaled down. The hint's (Hint.qml, the lock
screen's tutorial look) are call_hint_info (the white disc with the italic "i") and call_hint_chevrons
(two white chevrons pointing up, turned in QML). Run by hand when one changes:
    python3 -I scripts/gen-call-images.py [app/assets/images]
"""
import colorsys
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

S = 8
WHITE = (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)
PRIMARY = (0x24, 0xa1, 0xde)


def new(w, h):
    img = Image.new("RGBA", (round(w * S), round(h * S)), CLEAR)
    return img, ImageDraw.Draw(img)


def save(img, out, name):
    img.resize((img.width // S, img.height // S), Image.LANCZOS).save(f"{out}/{name}.png")


def hexa(c):
    return tuple(int(c[i:i + 2], 16) for i in (1, 3, 5)) + (255,)


def sc(*xy):
    return [v * S for v in xy]


def handset(w, h, color):
    """A phone handset lying flat: an arch with a pad at each end, hanging down (the hang-up
    one); returned at 8x. Rotated 135 degrees it is the answer one."""
    img, d = new(w, h)
    d.ellipse(sc(0, 0, w, 2 * h), color)
    d.rectangle(sc(0, 0.78 * h, w, 2 * h), CLEAR)
    d.ellipse(sc(0.17 * w, 0.34 * h, 0.83 * w, 2.4 * h), CLEAR)
    d.rounded_rectangle(sc(0, 0.38 * h, 0.27 * w, h), 0.1 * h * S, color)
    d.rounded_rectangle(sc(0.73 * w, 0.38 * h, w, h), 0.1 * h * S, color)
    return img


def put(img, part, cx, cy, angle=0):
    """Pastes a supersampled part centred on (cx, cy) in final px, turned by angle degrees clockwise."""
    if angle:
        part = part.rotate(-angle, Image.BICUBIC, expand=True)
    img.alpha_composite(part, (round(cx * S - part.width / 2), round(cy * S - part.height / 2)))


def lerp(table, y):
    """Cosine interpolation of (y, v...) rows: smooth, flat at each row."""
    import math
    if y <= table[0][0]:
        return table[0][1:]
    for a, b in zip(table, table[1:]):
        if y <= b[0]:
            t = (1 - math.cos(math.pi * (y - a[0]) / (b[0] - a[0]))) / 2
            return tuple(p + (q - p) * t for p, q in zip(a[1:], b[1:]))
    return table[-1][1:]


# The silhouette's colour (y, r, g, b), measured from the stock screen; its shape is in background().
SHADE = [(0, 0x45, 0x41, 0x52), (190, 0x45, 0x41, 0x52), (192, 0x43, 0x3f, 0x50), (224, 0x3f, 0x3b, 0x4a),
         (256, 0x38, 0x36, 0x43), (288, 0x32, 0x2f, 0x3a), (320, 0x2b, 0x28, 0x33), (352, 0x23, 0x22, 0x2a),
         (368, 0x1e, 0x1d, 0x23), (384, 0x1a, 0x19, 0x1f)]


def accent(rgb):
    """The stock screen's purple turned to Tengram's primary colour, #24A1DE (HyperBerry's rule, for its
    green, the user's ask of 2026-11-07): its hue, with the stock colour's own saturation and brightness
    kept."""
    h, s, v = colorsys.rgb_to_hsv(*[c / 255 for c in rgb])
    hue = colorsys.rgb_to_hsv(*[c / 255 for c in PRIMARY])[0]
    return tuple(c * 255 for c in colorsys.hsv_to_rgb(hue, s, v))


def gradient(y):
    t = min(1, max(0, (y - 300) / 210))
    return tuple(v * (1 - t) for v in accent((0x1b, 0x13, 0x2a)))


def background(out):
    """The stock #1b132a in the primary colour's hue, flat to y 300, then linear to black at 510; the
    default-contact silhouette fades into it."""
    W = 720
    bg = Image.new("RGB", (W, W))
    sil = Image.new("RGB", (W, W))
    for y in range(W):
        g = gradient(y)
        if y < 384:
            c = accent(lerp(SHADE, y))
        elif y < 400:  # into the background
            u = (y - 384) / 16
            c = tuple(a + (b - a) * u for a, b in zip(accent(lerp(SHADE, 384)), g))
        else:  # the shoulders are only a hint
            k = min((y - 400) / 50, 1) * min(1, max(0, (520 - y) / 70))
            c = tuple(a + k * h for a, h in zip(g, accent((13, 12, 16))))
        ImageDraw.Draw(bg).line((0, y, W, y), tuple(round(v) for v in g))
        ImageDraw.Draw(sil).line((0, y, W, y), tuple(round(v) for v in c))
    # The shape, fitted to the stock outline's rows: a flat-topped skull (190 px wide 9 px below
    # its top at 87; corners of 80, a dome as the stock one), the jaw rounding into the neck (418-578 at y 352, 432-563 at
    # 362: corners of 110), the ears, the neck, and the shoulders' wide curve (305-695 at y 465).
    m, d = new(W, W)
    d.rounded_rectangle(sc(380, 87, 617, 300), 80 * S, WHITE)
    d.rounded_rectangle(sc(380, 160, 617, 380), 110 * S, WHITE)
    d.rounded_rectangle(sc(368, 205, 392, 275), 12 * S, WHITE)  # the ears
    d.rounded_rectangle(sc(605, 205, 629, 275), 12 * S, WHITE)
    d.rectangle(sc(432, 340, 563, 440), WHITE)  # the neck
    d.ellipse(sc(140, 418, 860, 1018), WHITE)  # the shoulders
    m = m.resize((W, W), Image.LANCZOS).split()[3].filter(ImageFilter.GaussianBlur(1))
    bg.paste(sil, (0, 0), m)
    bg.save(f"{out}/call_bg.png")


def fade(out):
    """Transparent at the top, black at the bottom: over a photo's lower part."""
    img = Image.new("RGBA", (720, 260))
    px = img.load()
    for y in range(260):
        a = round(255 * (y / 259) ** 1.4)
        for x in range(720):
            px[x, y] = (0, 0, 0, a)
    img.save(f"{out}/call_fade.png")


def chevrons(d, x0, colors, left):
    for i, c in enumerate(colors):
        x = x0 + i * 31
        a, b = (x + 22, x) if left else (x, x + 22)
        d.line(sc(a, 57, b, 74, a, 91), hexa(c), 5 * S, joint="curve")


def shade(rgb, k):
    """rgb at k of its brightness, as #rrggbb."""
    return "#%02x%02x%02x" % tuple(round(c * k) for c in rgb)


def bracket(d, x0, color, left):
    """An end's marking, straight where HyperBerry's was an arc: a 4 px line along the end cell's top,
    bottom and outer side (the cell is 148 px wide, as the track is high)."""
    x1 = x0 + 148
    d.rectangle(sc(x0, 0, x1, 4), color)
    d.rectangle(sc(x0, 144, x1, 148), color)
    d.rectangle(sc(x0, 0, x0 + 4, 148) if left else sc(x1 - 4, 0, x1, 148), color)


def track(out):
    W, H = 665, 148
    img, d = new(W, H)
    d.rectangle(sc(0, 0, W, H), hexa("#1e1e1e"))
    answer, red = hexa("#%02x%02x%02x" % PRIMARY), hexa("#e83838")  # answer in the primary colour (the user's ask)
    bracket(d, 0, answer, True)
    bracket(d, W - 148, red, False)
    put(img, handset(85, 32, hexa("#f83030")), 583, 74)
    # Brighter toward the middle on the answer side, dimmer away from it on the red.
    chevrons(d, 168, [shade(PRIMARY, k) for k in (0.4, 0.6, 0.83)], True)  # the primary at 40, 60 and 83 %, as the stock's
    chevrons(d, 413, ["#983e3d", "#753132", "#592527"], False)
    save(img, out, "call_track")


def knob(out):
    """A square, in the three layers the round one had: light rim, dark ring, grey face."""
    img, d = new(110, 110)
    d.rectangle(sc(0, 0, 110, 110), hexa("#e8e8e8"))
    d.rectangle(sc(4, 4, 106, 106), hexa("#1e1e1e"))
    d.rectangle(sc(11, 11, 99, 99), hexa("#949494"))
    save(img, out, "call_knob")


def button(out):
    img, d = new(104, 104)
    d.rectangle(sc(0, 0, 104, 104), hexa("#2a2a2c"))
    d.rectangle(sc(2, 2, 102, 102), hexa("#040404"))
    save(img, out, "call_btn")


def message(out):
    img, d = new(50, 50)
    d.rounded_rectangle(sc(3, 7, 47, 37), 4 * S, WHITE)
    d.polygon(sc(9, 35, 23, 35, 12, 46), WHITE)
    for y, x1 in ((15, 40), (21, 40), (27, 30)):
        d.line(sc(10, y, x1, y), CLEAR, 3 * S)
    save(img, out, "call_message")


def speaker(out):
    img, d = new(50, 50)
    d.polygon(sc(3, 19, 12, 19, 25, 8, 25, 42, 12, 31, 3, 31), WHITE)
    d.arc(sc(16, 15, 34, 35), -55, 55, WHITE, 3 * S)
    d.arc(sc(14, 6, 46, 44), -55, 55, WHITE, 3 * S)
    save(img, out, "call_speaker")


def mic():
    img, d = new(50, 50)
    d.rounded_rectangle(sc(18, 4, 32, 30), 7 * S, WHITE)
    d.arc(sc(11, 14, 39, 38), 0, 180, WHITE, 3 * S)
    d.rectangle(sc(23.5, 38, 26.5, 44), WHITE)
    d.rectangle(sc(15, 43, 35, 46), WHITE)
    return img, d


def mute(out):
    img, d = mic()
    d.line(sc(6, 5, 44, 46), CLEAR, 8 * S)
    d.line(sc(6, 5, 44, 46), WHITE, 3 * S)
    save(img, out, "call_mute")


def unmute(out):
    """The mic without its slash: what the Mute toggle shows while muted, as the stock one does."""
    save(mic()[0], out, "call_unmute")


def end(out):
    save(handset(62, 22, WHITE), out, "call_end")


def brush(d, p0, p1, p2, w0, w1, color):
    """A stroke along the quadratic curve p0, p1, p2 (final px), its width going from w0 to w1: round
    dabs close together, the way a pen's thick and thin strokes are."""
    steps = 240
    for i in range(steps + 1):
        t = i / steps
        x = (1 - t) ** 2 * p0[0] + 2 * t * (1 - t) * p1[0] + t * t * p2[0]
        y = (1 - t) ** 2 * p0[1] + 2 * t * (1 - t) * p1[1] + t * t * p2[1]
        r = (w0 + (w1 - w0) * t) / 2
        d.ellipse(sc(x - r, y - r, x + r, y + r), color)


def hint_info(out):
    """The hint's badge, 96x96: a white disc with a serif italic lowercase i in #66661F. Drawn by
    hand (no font here): a slanted stem, the flag at its top left, the foot's hook to the right and
    the dot, each a brushstroke thick where a pen's would be and thin at its end."""
    img, d = new(96, 96)
    ink = hexa("#66661F")
    d.ellipse(sc(0, 0, 96, 96), WHITE)
    mid = lambda a, b: ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
    top, foot = (52, 40), (44.5, 68)
    brush(d, top, mid(top, foot), foot, 7.6, 7.6, ink)  # the stem
    brush(d, (52.5, 40.5), (43, 41), (38.5, 49), 5.5, 2.2, ink)  # the flag
    brush(d, foot, (43.5, 73.5), (56, 65), 6.5, 2.4, ink)  # the foot
    d.ellipse(sc(47.2, 22, 58.8, 33.6), ink)  # the dot
    save(img, out, "call_hint_info")


def hint_chevrons(out):
    """Two white chevrons stacked, pointing up, as the lock screen's tutorial hints have; 110x110, so
    that turned a quarter it keeps its box."""
    img, d = new(110, 110)
    for top in (9, 49):
        pts = sc(8, top + 41, 55, top, 102, top + 41)
        d.line(pts, WHITE, 9 * S, joint="curve")
        for x, y in ((8, top + 41), (102, top + 41)):
            d.ellipse(sc(x - 4.5, y - 4.5, x + 4.5, y + 4.5), WHITE)
    save(img, out, "call_hint_chevrons")


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "assets", "images")
    for f in (background, fade, track, knob, button, message, speaker, mute, unmute, end, hint_info, hint_chevrons):
        f(out)
