#!/bin/sh
# Fetches Nayuki's QR Code generator, C version (MIT), into lib/qrcodegen/, pinned to the commit
# of release v1.8.0 and checked by sha256. Not committed: qrcodegen.c is over the repo's
# 500-line file rule. Does no work when both files are already there and match.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
URL=https://raw.githubusercontent.com/nayuki/QR-Code-generator/720f62bddb7226106071d4728c292cb1df519ceb/c
DIR=$ROOT/lib/qrcodegen
mkdir -p "$DIR"
fetch() { # <name in the repo> <sha256>
	# The hash is compared here, not by sha256sum -c: macOS's takes no --status, and fails -c on stdin.
	[ -f "$DIR/$1" ] && [ "$(sha256sum "$DIR/$1" | cut -d" " -f1)" = "$2" ] && return
	curl -fsSL -o "$DIR/$1.part" "$URL/$1"
	[ "$(sha256sum "$DIR/$1.part" | cut -d" " -f1)" = "$2" ] || { rm -f "$DIR/$1.part"; echo "$1: sha256 mismatch" >&2; exit 1; }
	mv "$DIR/$1.part" "$DIR/$1"
}
fetch qrcodegen.c 300eff07ee25baaa7578f20284411638154716379437391e7e689c0e6ce81403
fetch qrcodegen.h e82df4bff37d18b5863b9e7486fe6bda1b6cda8c3b9ecebfec473907265cb589
