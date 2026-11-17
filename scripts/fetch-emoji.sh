#!/bin/sh
# Fetches Google's Noto Color Emoji (PLAN.md §7) into build/emoji/, pinned to a release commit
# and checked by sha256, then derives HyperBerry's font from it (scripts/build-emoji-font.py)
# into app/assets/fonts/, beside the font's OFL license, and one PNG per emoji into
# app/assets/emoji/ for the picker. Nothing here is committed. Does no work when the derived
# font is newer than its inputs and the PNGs exist.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# googlefonts/noto-emoji v2026-09-24-unicode18_0: font revision 2.057, Unicode 18.0.
URL=https://raw.githubusercontent.com/googlefonts/noto-emoji/e20cbc2bbec1926686be9f9bee7d1d2cfa1fea0e/2D/fonts
SRC=$ROOT/build/emoji
DIR=$ROOT/app/assets/fonts
mkdir -p "$SRC" "$DIR"
fetch() { # <name> <sha256> [base URL, default the noto-emoji one]
	# The hash is compared here, not by sha256sum -c: macOS's takes no --status, and fails -c on stdin.
	[ -f "$SRC/$1" ] && [ "$(sha256sum "$SRC/$1" | cut -d" " -f1)" = "$2" ] && return
	curl -fsSL -o "$SRC/$1.part" "${3:-$URL}/$1"
	[ "$(sha256sum "$SRC/$1.part" | cut -d" " -f1)" = "$2" ] || { rm -f "$SRC/$1.part"; echo "$1: sha256 mismatch" >&2; exit 1; }
	mv "$SRC/$1.part" "$SRC/$1"
}
fetch NotoColorEmoji.ttf 15671215ab769fdc7162a045d56fd7d7e477c51b04e6b3c761d914d8fdd6cc44
fetch LICENSE 6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2
# The picker's table (scripts/gen-emoji.py) comes from Unicode's list of the same version.
fetch emoji-test.txt 8f3735cda1f92a779d78af67cf86066bb1f07143dc22f2ac29394d9bc57ab21a https://www.unicode.org/Public/18.0.0/emoji
cp "$SRC/LICENSE" "$DIR/NotoColorEmoji-LICENSE.txt"

OUT=$DIR/HyperBerryEmoji.ttf
[ -f "$OUT" ] && [ -d "$ROOT/app/assets/emoji" ] && [ "$OUT" -nt "$SRC/NotoColorEmoji.ttf" ] && [ "$OUT" -nt "$ROOT/scripts/build-emoji-font.py" ] && exit 0
docker build -q -t hyperberry-font - >/dev/null <<'EOF'
FROM python:3.12-slim@sha256:aa943ce411423c7582288f18707ff66eddcbaeb510502d747a72aeb34f30d159
RUN pip install --no-cache-dir --root-user-action=ignore fonttools==4.68.0 pillow==13.0.0
EOF
docker run --rm -v "$ROOT:/src" hyperberry-font \
	python -I /src/scripts/build-emoji-font.py /src/build/emoji/NotoColorEmoji.ttf /src/app/assets/fonts/HyperBerryEmoji.ttf /src/app/assets/emoji
