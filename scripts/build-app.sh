#!/bin/sh
# Builds build/tengram.bar: the engine with the Telegram API id and hash compiled in (engine/build.sh), the UI
# (scripts/build-ui.sh), then the package, unsigned, with both binaries (app/bar-descriptor.xml). Each package
# gets the next build id from build/buildid.txt: the installer keeps the old app when Package-Version is the same.
# TG_API_ID and TG_API_HASH must be in the environment; they are in no file.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=${BB10_NDK_IMAGE:-yamsergey/bb10-ndk:0.6}
if [ -z "${TG_API_ID-}" ] || [ -z "${TG_API_HASH-}" ]; then
	echo "build-app.sh: set TG_API_ID and TG_API_HASH (my.telegram.org)" >&2
	exit 2
fi
"$ROOT/engine/build.sh" >/dev/null
"$ROOT/scripts/build-ui.sh" >/dev/null
[ -f "$ROOT/build/buildid.txt" ] || echo 1 >"$ROOT/build/buildid.txt"
docker run --rm -v "$ROOT:/src" -w /src "$IMAGE" bash -c '
	source /root/bbndk/bbndk-env_10_3_1_995.sh >/dev/null
	set -eu
	cd app && blackberry-nativepackager -package ../build/tengram.bar -buildIdFile ../build/buildid.txt \
		bar-descriptor.xml -configuration Device-Release
	rm -f assets/.assets.index
'
unzip -p "$ROOT/build/tengram.bar" META-INF/MANIFEST.MF | grep -E '^Package-(Id|Version):'
