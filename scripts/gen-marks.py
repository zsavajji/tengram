"""Draws the chat list's small marks into app/assets/images/: ticks, clock, mute, lock, pin and mic,
the action bar's record icon, the row menu's pin and mute, the voice bubble's play, pause and
retry discs, and the Hub item menu's tick_action, unread_action and delete_action.
The phone has only the trash can among the info page's and chat menu's icons (user's choice,
2026-11-01), so these are drawn as action icons too: contacts_action (two people), save_action
(a floppy disk), block_action (the "forbidden" sign), attach_action (a paperclip), plus_action
(a plus, which the phone has only beside a contact, a place or an event), megaphone_action and
help_action (a question mark in a ring, for About), back_action (a thin "<" chevron, the chat
title bar's Back) and overflow_action (three vertical dots, the composer's ⋮ column).
A forwarded card's marks are forward.png and forward_many.png.
The app's icon, the splash screens and the Hub's item and account glyphs (hub_read.png, hub_unread.png,
hub_account.png) are not drawn here: they are Tengram's own (scripts/gen-icon.py).

They're white on transparent, so QML tints them with ImageView.filterColor (grey, or the
primary colour for read). Each is drawn at 8x and scaled down to its size on the Q10: 3 du,
27 px tall (PLAN.md §7). Run by hand when a mark changes, in the image fetch-emoji.sh builds:
    docker run --rm -v "$PWD:/src" hyperberry-font python -I /src/scripts/gen-marks.py /src/app/assets/images
"""
import math
import sys

from PIL import Image, ImageDraw

S = 8  # supersampling
H = 27  # 3 du on the Q10


def canvas(w):
    img = Image.new("RGBA", (w * S, H * S), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def pts(*xy):
    return [(x * H * S, y * H * S) for x, y in xy]


def save(img, out, name):
    img.resize((img.width // S, img.height // S), Image.LANCZOS).save(f"{out}/{name}.png")


def action(img, out, name):
    """A mark as an action icon: 80% of an 81 px box, since the marks carry their own margin.
    The row menu showed the 27 px marks small beside the system's icons (Q10, 2026-11-01)."""
    B = 81
    box = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    m = img.resize((round(0.8 * B * img.width / img.height), round(0.8 * B)), Image.LANCZOS)
    box.paste(m, ((B - m.width) // 2, (B - m.height) // 2), m)
    box.save(f"{out}/{name}_action.png")


def main(out):
    white, w = (255, 255, 255, 255), round(0.11 * H * S)

    img, d = canvas(H)
    d.line(pts((0.14, 0.52), (0.38, 0.76), (0.86, 0.26)), white, w, joint="curve")
    save(img, out, "tick")
    action(img, out, "tick")  # the Hub item menu's Mark as read

    # A question mark in a ring: the swipe-down menu's About (the user's choice, 2026-11-07).
    img, d = canvas(H)
    d.ellipse(pts((0.06, 0.06), (0.94, 0.94)), None, white, w)
    d.arc(pts((0.35, 0.22), (0.65, 0.5)), 180, 70, white, w)
    d.line(pts((0.6, 0.47), (0.5, 0.55), (0.5, 0.64)), white, w, joint="curve")
    d.ellipse(pts((0.44, 0.71), (0.56, 0.83)), white)
    action(img, out, "help")

    # A forward's mark over its card (WhatsApp's "Forwarded"): a shaft curving up and right into a head,
    # 72 px; two heads for "Forwarded many times".
    for name, heads in (("forward", 1), ("forward_many", 2)):
        n = 72 * S
        img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        lw = round(0.09 * n)
        d.arc((0.12 * n, 0.4 * n, 1.04 * n, 1.32 * n), 180, 270, white, lw)
        d.line(((0.58 * n, 0.4 * n + lw / 2 - 1), (0.84 * n, 0.4 * n + lw / 2 - 1)), white, lw)
        for k in range(heads):
            x = 0.9 - 0.2 * k
            d.line(((x - 0.22) * n, 0.2 * n, x * n, 0.445 * n, (x - 0.22) * n, 0.69 * n), white, lw, joint="curve")
        img.resize((72, 72), Image.LANCZOS).save(f"{out}/{name}.png")

    # The second tick's short stroke starts past the first tick's long one, as WhatsApp draws it.
    img, d = canvas(round(H * 1.3))
    d.line(pts((0.10, 0.52), (0.34, 0.76), (0.82, 0.26)), white, w, joint="curve")
    d.line(pts((0.54, 0.66), (0.64, 0.76), (1.12, 0.26)), white, w, joint="curve")
    save(img, out, "ticks")

    img, d = canvas(H)
    d.ellipse(pts((0.12, 0.12), (0.88, 0.88)), outline=white, width=round(0.09 * H * S))
    d.line(pts((0.5, 0.26), (0.5, 0.52), (0.68, 0.62)), white, round(0.08 * H * S), joint="curve")
    save(img, out, "clock")

    # A speaker with a cross beside it.
    img, d = canvas(H)
    d.polygon(pts((0.06, 0.38), (0.22, 0.38), (0.46, 0.16), (0.46, 0.84), (0.22, 0.62), (0.06, 0.62)), white)
    x = round(0.08 * H * S)
    d.line(pts((0.6, 0.36), (0.9, 0.64)), white, x)
    d.line(pts((0.6, 0.64), (0.9, 0.36)), white, x)
    save(img, out, "mute")
    action(img, out, "mute")

    # A padlock: a body with a shackle over it. A closed forum topic's mark in the topics list.
    img, d = canvas(H)
    d.rectangle(pts((0.2, 0.46), (0.8, 0.9)), white)
    d.arc(pts((0.3, 0.12), (0.7, 0.56)), 180, 360, white, round(0.09 * H * S))
    d.line(pts((0.3, 0.34), (0.3, 0.5)), white, round(0.09 * H * S))
    d.line(pts((0.7, 0.34), (0.7, 0.5)), white, round(0.09 * H * S))
    save(img, out, "lock")

    # A filled disc: the Hub item menu's Mark as unread, as the unread dot of a chat.
    img, d = canvas(H)
    d.ellipse(pts((0.22, 0.22), (0.78, 0.78)), white)
    action(img, out, "unread")

    # A trash can: the Hub item menu's Delete. A lid line with a handle, a body that narrows
    # toward the bottom, and three slits.
    img, d = canvas(H)
    d.line(pts((0.14, 0.24), (0.86, 0.24)), white, round(0.08 * H * S))
    d.rectangle(pts((0.38, 0.12), (0.62, 0.20)), white)
    d.polygon(pts((0.22, 0.32), (0.78, 0.32), (0.72, 0.90), (0.28, 0.90)), white)
    for x in (0.38, 0.5, 0.62):
        d.line(pts((x, 0.42), (x, 0.80)), (0, 0, 0, 0), round(0.05 * H * S))
    action(img, out, "delete")

    # Two people, outlines: a head and a dome of shoulders each, the back one up and to the right.
    # The front one's silhouette is cleared first, so the back one's lines stop at a gap.
    img, d = canvas(H)
    o, clear = round(0.07 * H * S), (0, 0, 0, 0)

    def person(cx, top, r, half, v, base):
        """Head of radius r with its top at `top`, a dome half wide and v tall on the line y = base."""
        d.ellipse(pts((cx - r, top), (cx + r, top + 2 * r)), outline=white, width=o)
        d.arc(pts((cx - half, base - v), (cx + half, base + v)), 180, 360, white, o)
        d.line(pts((cx - half, base), (cx + half, base)), white, o)

    person(0.68, 0.10, 0.13, 0.24, 0.36, 0.86)
    g = 0.045  # the gap round the front person
    fx, fh, fr, fv = 0.34, 0.26, 0.14, 0.30
    d.ellipse(pts((fx - fr - g, 0.20 - g), (fx + fr + g, 0.20 + 2 * fr + g)), clear)
    d.pieslice(pts((fx - fh - g, 0.86 - fv - g), (fx + fh + g, 0.86 + fv + g)), 180, 360, clear)
    d.rectangle(pts((fx - fh - g, 0.86), (fx + fh + g, 0.92)), clear)
    person(fx, 0.20, fr, fh, fv, 0.86)
    action(img, out, "contacts")

    # A floppy disk: a square with the top right corner cut, the shutter at the top and the label
    # at the bottom.
    img, d = canvas(H)
    o = round(0.07 * H * S)
    d.line(pts((0.12, 0.12), (0.74, 0.12), (0.88, 0.26), (0.88, 0.88), (0.12, 0.88), (0.12, 0.12)),
           white, o, joint="curve")
    d.rectangle(pts((0.30, 0.12), (0.66, 0.36)), outline=white, width=o)
    d.rectangle(pts((0.26, 0.54), (0.74, 0.88)), outline=white, width=o)
    action(img, out, "save")

    # The "forbidden" sign: a circle with a bar from the top left to the bottom right.
    img, d = canvas(H)
    o = round(0.09 * H * S)
    d.ellipse(pts((0.10, 0.10), (0.90, 0.90)), outline=white, width=o)
    d.line(pts((0.22, 0.22), (0.78, 0.78)), white, o)
    action(img, out, "block")

    # A plus, as heavy as the "forbidden" sign.
    img, d = canvas(H)
    d.line(pts((0.5, 0.12), (0.5, 0.88)), white, o)
    d.line(pts((0.12, 0.5), (0.88, 0.5)), white, o)
    action(img, out, "plus")

    # A megaphone, for Channels (the user's, 2026-11-06): the horn, its grip and one sound wave.
    img, d = canvas(H)
    d.polygon(pts((0.08, 0.40), (0.26, 0.40), (0.70, 0.14), (0.70, 0.86), (0.26, 0.60), (0.08, 0.60)), fill=white)
    d.line(pts((0.22, 0.58), (0.31, 0.88)), white, o)
    d.line(pts((0.84, 0.34), (0.84, 0.66)), white, o)
    action(img, out, "megaphone")

    # A paperclip, drawn upright as one wire (outer left up, over the top, down the right, round
    # the bottom, up the inside, a small turn, down to the free end) and turned 45 degrees.
    def arc(cx, cy, r, a0, a1):
        return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / 24)),
                 cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / 24))) for i in range(25)]

    img, d = canvas(H)
    o = round(0.07 * H * S)
    wire = ([(0.28, 0.68), (0.28, 0.35)] + arc(0.50, 0.35, 0.22, 180, 360)  # up and over the top
            + [(0.72, 0.70)] + arc(0.56, 0.70, 0.16, 0, 180)  # down, round the bottom
            + [(0.40, 0.42)] + arc(0.50, 0.42, 0.10, 180, 360) + [(0.60, 0.64)])  # up, turn, down
    d.line(pts(*wire), white, o, joint="curve")
    for x, y in (wire[0], wire[-1]):  # round ends
        d.ellipse(pts((x - 0.035, y - 0.035), (x + 0.035, y + 0.035)), white)
    action(img.rotate(-45, Image.BICUBIC), out, "attach")

    # A thin "<", as the stock Back: the chat title bar's, now that the page's action bar is hidden.
    img, d = canvas(H)
    d.line(pts((0.62, 0.18), (0.30, 0.5), (0.62, 0.82)), white, w, joint="curve")
    action(img, out, "back")

    # Three vertical dots: the composer's ⋮ column, which opens the action menu.
    img, d = canvas(H)
    r = 0.07 * H * S
    for y in (0.22, 0.5, 0.78):
        d.ellipse((0.5 * H * S - r, y * H * S - r, 0.5 * H * S + r, y * H * S + r), white)
    action(img, out, "overflow")

    # A pushpin, drawn upright and turned 45 degrees.
    img, d = canvas(H)
    d.rectangle(pts((0.36, 0.08), (0.64, 0.16)), white)
    d.polygon(pts((0.40, 0.16), (0.60, 0.16), (0.62, 0.44), (0.76, 0.58), (0.24, 0.58), (0.38, 0.44)), white)
    d.line(pts((0.5, 0.58), (0.5, 0.94)), white, round(0.07 * H * S))
    save(img.rotate(-45, Image.BICUBIC), out, "pin")
    action(img.rotate(-45, Image.BICUBIC), out, "pin")

    # A microphone: capsule, cradle and stand.
    img, d = canvas(H)
    d.rounded_rectangle(pts((0.36, 0.06), (0.64, 0.56)), round(0.14 * H * S), white)
    d.arc(pts((0.24, 0.30), (0.76, 0.72)), 0, 180, white, round(0.07 * H * S))
    d.line(pts((0.5, 0.72), (0.5, 0.92)), white, round(0.07 * H * S))
    d.line(pts((0.36, 0.92), (0.64, 0.92)), white, round(0.07 * H * S))
    save(img, out, "mic")

    # The action bar's record icon: the same microphone, 60% of an 81 px box (the system's action
    # icons), white on transparent.
    B = 81
    img = Image.new("RGBA", (B * S, B * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    m = lambda *xy: [((0.2 + x * 0.6) * B * S, (0.2 + y * 0.6) * B * S) for x, y in xy]
    d.rounded_rectangle(m((0.36, 0.0), (0.64, 0.58)), round(0.14 * 0.6 * B * S), white)
    d.arc(m((0.22, 0.30), (0.78, 0.76)), 0, 180, white, round(0.07 * 0.6 * B * S))
    d.line(m((0.5, 0.76), (0.5, 1.0)), white, round(0.07 * 0.6 * B * S))
    d.line(m((0.34, 1.0), (0.66, 1.0)), white, round(0.07 * 0.6 * B * S))
    img.resize((B, B), Image.LANCZOS).save(f"{out}/record.png")

    # The chat list's ⋮ Search, Top and Bottom, in the same box: /usr/share/icons has none of
    # them, and an action with no icon shows a cog (Q10, 2026-10-31).
    w = round(0.09 * 0.6 * B * S)
    for name in ("search", "top", "bottom"):
        img = Image.new("RGBA", (B * S, B * S), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        if name == "search":
            d.ellipse(m((0.0, 0.0), (0.66, 0.66)), None, white, w)
            d.line(m((0.58, 0.58), (1.0, 1.0)), white, round(1.4 * w))
        else:
            bar, tip, tail = (0.04, 0.16, 0.96) if name == "top" else (0.96, 0.84, 0.04)
            d.line(m((0.1, bar), (0.9, bar)), white, w)
            d.line(m((0.5, tip), (0.5, tail)), white, w)
            wing = 0.3 if name == "top" else -0.3
            d.line(m((0.5, tip), (0.22, tip + wing)), white, w)
            d.line(m((0.5, tip), (0.78, tip + wing)), white, w)
        img.resize((B, B), Image.LANCZOS).save(f"{out}/{name}.png")

    # The bubble's buttons: a white disc 72 px across (8 du), a dark glyph. Not tinted.
    dark = (0x1a, 0x1a, 0x1a, 255)
    for name in ("play", "pause", "retry"):
        D = 72
        img = Image.new("RGBA", (D * S, D * S), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.ellipse((0, 0, D * S - 1, D * S - 1), white)
        p = lambda *xy: [(x * D * S, y * D * S) for x, y in xy]
        if name == "play":
            d.polygon(p((0.40, 0.30), (0.40, 0.70), (0.72, 0.50)), dark)
        elif name == "pause":
            d.rectangle(p((0.34, 0.30), (0.46, 0.70)), dark)
            d.rectangle(p((0.54, 0.30), (0.66, 0.70)), dark)
        else:
            d.arc(p((0.28, 0.28), (0.72, 0.72)), 40, 330, dark, round(0.08 * D * S))
            d.polygon(p((0.70, 0.22), (0.76, 0.46), (0.52, 0.40)), dark)
        img.resize((D, D), Image.LANCZOS).save(f"{out}/{name}.png")


if __name__ == "__main__":
    main(sys.argv[1])
