#!/bin/sh
# Fetches Google's libwebp 1.4.0 (BSD) into lib/libwebp/, pinned by the release tarball's sha256.
# The app builds its decoder (sticker pictures, step 6.5b). Not committed: it is far over the
# repo's 500-line file rule. Does no work when lib/libwebp/.sha256 holds the pinned hash.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
URL=https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.4.0.tar.gz
SHA=61f873ec69e3be1b99535634340d5bde750b2e4447caa1db9f61be3fd49ab1e5
DIR=$ROOT/lib/libwebp
# The stamp is written last, so a half-extracted tree is fetched again.
[ -f "$DIR/.sha256" ] && [ "$(cat "$DIR/.sha256")" = "$SHA" ] && exit 0
rm -rf "$DIR"
mkdir -p "$DIR"
# The hash is compared here, not by sha256sum -c: macOS's takes no --status, and fails -c on stdin.
curl -fsSL -o "$DIR.tar.gz" "$URL"
[ "$(sha256sum "$DIR.tar.gz" | cut -d" " -f1)" = "$SHA" ] || { rm -rf "$DIR.tar.gz" "$DIR"; echo "libwebp: sha256 mismatch" >&2; exit 1; }
tar -xzf "$DIR.tar.gz" -C "$DIR" --strip-components=1
rm -f "$DIR.tar.gz"
echo "$SHA" >"$DIR/.sha256"
