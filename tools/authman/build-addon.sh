#!/bin/sh
# Builds build/headless-mem.zip, an addon for the root installer (/var/rootdata/installer): mempatch and the
# start.sh that raises _sys_headless_xlarge in authman's memory at every boot. Layout as ../bb10-volte's.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
docker run --rm -v "$ROOT:/src" -w /src tengram-build sh -c \
	'bb10-clang -O2 -Wall tools/authman/mempatch.c -o tools/authman/addon/init/mempatch'
rm -f "$ROOT/build/headless-mem.zip"
(cd "$ROOT/tools/authman/addon" && zip -q -r "$ROOT/build/headless-mem.zip" MANIFEST init)
unzip -l "$ROOT/build/headless-mem.zip"
