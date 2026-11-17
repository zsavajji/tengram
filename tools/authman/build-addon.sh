#!/bin/sh
# Builds build/hmem-addon.zip, an addon for the root installer (/var/rootdata/installer): the helper
# (tools/authman/authman-send.c) and the two headless memory classes. Layout as ../bb10-volte's.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
docker run --rm -v "$ROOT:/src" -w /src tengram-build sh -c \
	'bb10-clang -O2 -Wall tools/authman/authman-send.c -o tools/authman/addon/init/authman-send'
rm -f "$ROOT/build/hmem-addon.zip"
(cd "$ROOT/tools/authman/addon" && zip -q -r "$ROOT/build/hmem-addon.zip" MANIFEST init data)
unzip -l "$ROOT/build/hmem-addon.zip"
