#!/usr/bin/env python3
"""Welcome animation: the classic tangram square springs open into the Tengram icon (the user, 2026-11-21: a Lottie of a
tangram becoming the icon, "do it with a spring animation"), for WelcomePage.qml's logo.

Writes app/assets/images/welcome.tgs (a gzipped Lottie, as Telegram's stickers are), which StickerPlayer plays through
rlottie. The icon's geometry is gen-icon.py's, so the last frame is icon(512): every piece lands at rotation 0 on its
place in the plane, its colour fades from the square's to the icon's, and the blue disc grows behind.
"""
import gzip
import importlib.util
import json
import math
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("gen_icon", HERE / "gen-icon.py")
gi = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gi)

FPS = 60
HOLD = 30        # frames on the square before it opens
STAGGER = 5      # frames between one piece's start and the next's
ZETA, OMEGA = 0.55, 12.0  # the spring: damping ratio, natural frequency (rad/s)
START = 1.3      # the square's scale against the icon's (1: 198 px of 512)
FRAMES = 150

# The square from the user's picture, side 2√2 tangram units, y down; colours sampled from it.
_S = 2 * math.sqrt(2)
_P = dict(a=(0, 0), b=(_S, 0), h=(0, _S), j=(_S, _S), d=(_S / 2, _S / 2), i=(_S / 2, _S), e=(_S, _S / 2),
          c=(3 * _S / 4, _S / 4), g=(3 * _S / 4, 3 * _S / 4), f=(_S / 4, 3 * _S / 4))
SQUARE = [(col, [_P[k] for k in keys]) for col, keys in (
    ("#c9e891", "abd"), ("#68c0d2", "adh"), ("#f8c82c", "eji"), ("#ef3d61", "begc"),
    ("#f0f223", "cdg"), ("#a796c2", "dgif"), ("#f195c8", "hfi"))]


def rgb(h):
    return [int(h[k:k + 2], 16) / 255 for k in (1, 3, 5)]


def spring(t):
    """0 at t=0, settling on 1 with an overshoot; t in seconds."""
    if t <= 0:
        return 0.0
    wd = OMEGA * math.sqrt(1 - ZETA ** 2)
    return 1 - math.exp(-ZETA * OMEGA * t) * (math.cos(wd * t) + ZETA * OMEGA / wd * math.sin(wd * t))


def ease(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3 - 2 * t)


def rot(p, deg, mirror=False):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    x, y = (-p[0] if mirror else p[0]), p[1]
    return (x * c - y * s, x * s + y * c)


def fit(src, dst):
    """(rotation in degrees, mirror) taking polygon src onto dst, both about their centroids; None if no rigid fit."""
    n = len(src)
    if n != len(dst):
        return None
    for mirror in (False, True):
        for k in range(n):
            for order in (dst, dst[::-1]):
                q = order[k:] + order[:k]
                p0 = (-src[0][0] if mirror else src[0][0], src[0][1])
                deg = math.degrees(math.atan2(q[0][1], q[0][0]) - math.atan2(p0[1], p0[0]))
                if all(math.dist(rot(p, deg, mirror), w) < 1e-6 for p, w in zip(src, q)):
                    return (deg + 180) % 360 - 180, mirror
    return None


def about(p):
    cx, cy = gi.centroid(p)
    return (cx, cy), [(x - cx, y - cy) for x, y in p]


def pieces():
    """Per icon piece: (end centre, path about it, end colour, start centre, start rotation, mirror, start colour)."""
    side = _S * gi.SCALE * START
    corner = gi.SIZE / 2 - side / 2
    square = [(col, [(corner + x * gi.SCALE * START, corner + y * gi.SCALE * START) for x, y in p]) for col, p in SQUARE]
    icon = gi.placed()
    out, free = [], list(range(len(square)))
    for fill, p in icon:
        end, shape = about(p)
        best = None
        for j in free:  # the same piece of the square; of a pair, the one turning least
            start, sq = about(square[j][1])
            f = fit([(x * START, y * START) for x, y in shape], sq)
            if f and (best is None or abs(f[0]) < abs(best[1][0])):
                best = (j, f, start)
        assert best, f"no piece of the square fits {fill}"
        j, (deg, mirror), start = best
        free.remove(j)
        path = [(x - end[0], y - end[1]) for x, y in gi.inset(p, gi.GAP / 2 + gi.ROUND)]
        out.append((end, path, fill, start, deg, mirror, square[j][0]))
    return out


def keys(values, frames):
    """Lottie keyframes, linear between the samples."""
    ks = [{"t": t, "s": v, "i": {"x": [1], "y": [1]}, "o": {"x": [0], "y": [0]}} for t, v in zip(frames, values)]
    ks[-1] = {"t": frames[-1], "s": values[-1]}
    return {"a": 1, "k": ks}


def static(v):
    return {"a": 0, "k": v}


def layer(ind, name, shapes, ks):
    return {"ddd": 0, "ind": ind, "ty": 4, "nm": name, "sr": 1, "ks": ks, "ao": 0, "shapes": shapes,
            "ip": 0, "op": FRAMES, "st": 0, "bm": 0}


def tr():
    return {"ty": "tr", "p": static([0, 0]), "a": static([0, 0]), "s": static([100, 100]), "r": static(0),
            "o": static(100)}


def build():
    frames = list(range(0, FRAMES, 2)) + [FRAMES - 1]
    layers = []
    for n, (end, path, fill, start, deg, mirror, col) in enumerate(pieces()):
        t0 = HOLD + n * STAGGER
        s = [spring((f - t0) / FPS) for f in frames]
        k = [START + (1 - START) * v for v in s]
        pos = [[start[0] + (end[0] - start[0]) * v, start[1] + (end[1] - start[1]) * v] for v in s]
        # A mirrored piece flips over as it flies: its x scale goes from -START to 1 with the spring.
        sx = [(-START + (1 + START) * v) if mirror else kk for v, kk in zip(s, k)]
        for f, p, a, kk, xx in zip(frames, pos, s, k, sx):  # no corner leaves the canvas, overshoot included
            for x, y in path:
                q = rot((x * abs(xx), y * kk), deg * (1 - a), xx < 0)
                assert 0 <= p[0] + q[0] <= gi.SIZE and 0 <= p[1] + q[1] <= gi.SIZE, (fill, f)
        c0, c1 = rgb(col), rgb(fill)
        cs = [[u + (w - u) * ease((f - t0) / 24) for u, w in zip(c0, c1)] + [1] for f in frames]
        shape = {"ty": "sh", "ks": static({"c": True, "v": [list(v) for v in path],
                                           "i": [[0, 0]] * len(path), "o": [[0, 0]] * len(path)})}
        group = {"ty": "gr", "it": [
            shape,
            {"ty": "st", "c": keys(cs, frames), "o": static(100), "w": static(2 * gi.ROUND), "lc": 2, "lj": 2},
            {"ty": "fl", "c": keys(cs, frames), "o": static(100), "r": 1},
            tr()]}
        layers.append(layer(n + 1, f"piece {n}", [group], {
            "o": static(100), "a": static([0, 0, 0]),
            "r": keys([[deg * (1 - v)] for v in s], frames),
            "p": keys([p + [0] for p in pos], frames),
            "s": keys([[100 * x, 100 * y, 100] for x, y in zip(sx, k)], frames)}))
    # The disc grows behind with no overshoot (radius 252 of 256 would clip): critically damped.
    r = gi.SIZE / 2 - 4
    d = [min(1.0, 1 - math.exp(-9 * max(0, f - HOLD) / FPS) * (1 + 9 * max(0, f - HOLD) / FPS)) for f in frames]
    d[-1] = 1.0
    disc = {"ty": "gr", "it": [
        {"ty": "el", "p": static([0, 0]), "s": static([2 * r, 2 * r]), "d": 1},
        {"ty": "gf", "o": static(100), "r": 1, "t": 1, "s": static([0, -gi.SIZE / 2]), "e": static([0, gi.SIZE / 2]),
         "g": {"p": 2, "k": static([0] + rgb("#2aabee") + [1] + rgb("#229ed9"))}},
        tr()]}
    layers.append(layer(len(layers) + 1, "disc", [disc], {
        "o": static(100), "a": static([0, 0, 0]), "r": static(0), "p": static([gi.SIZE / 2, gi.SIZE / 2, 0]),
        "s": keys([[100 * v, 100 * v, 100] for v in d], frames)}))
    return {"v": "5.5.2", "fr": FPS, "ip": 0, "op": FRAMES, "w": gi.SIZE, "h": gi.SIZE, "nm": "welcome", "ddd": 0,
            "assets": [], "layers": layers, "tgs": 1}


def main():
    gi.check_tangram()
    out = HERE.parent / "app" / "assets" / "images" / "welcome.tgs"
    data = json.dumps(build(), separators=(",", ":")).encode()
    out.write_bytes(gzip.compress(data, mtime=0))
    print(out, len(data), "bytes of JSON,", out.stat().st_size, "zipped")


if __name__ == "__main__":
    main()
