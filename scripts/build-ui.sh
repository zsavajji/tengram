#!/bin/sh
# Builds the Cascades UI, build/ui/app/tengram, in the Native SDK 10.3.1 image: fetches what is not in git
# (emoji font, qrcodegen, OpenMAX headers, libwebp, the SQLCipher driver), checks that Qt 4.8 can parse every
# QML and JS file, turns the translations into .qm files, then qmake and make. It does not build the engine and
# it does not package: packaging comes later, once the UI runs.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=${BB10_NDK_IMAGE:-yamsergey/bb10-ndk:0.6}
"$ROOT/scripts/fetch-emoji.sh"
"$ROOT/scripts/fetch-qrcodegen.sh"
"$ROOT/scripts/fetch-omxil.sh"
"$ROOT/scripts/fetch-libwebp.sh"
"$ROOT/scripts/fetch-qtsqlcipher.sh"
# rlottie, built by clang in the tengram-build image: build/prefix/lib/libtgs.so, which the UI links (animated stickers).
"$ROOT/scripts/build-tgs.sh" >/dev/null
# Two functions of one name on a page's root object parse, and then the phone refuses the whole page ("Duplicate
# method name": ChatPage's picked, 0.1.0.29), which lupdate doesn't see.
dups=$(for f in "$ROOT"/app/assets/*.qml; do
	sed -n 's/^    function \([A-Za-z0-9_]*\)(.*/\1/p' "$f" | sort | uniq -d | sed "s|^|$(basename "$f"): |"
done)
if [ -n "$dups" ]; then
	echo "build-ui.sh: a function defined twice on a page's root object:" >&2
	echo "$dups" >&2
	exit 1
fi
docker run --rm -v "$ROOT:/src" -w /src "$IMAGE" bash -c '
	source /root/bbndk/bbndk-env_10_3_1_995.sh >/dev/null
	set -eu
	mkdir -p build/ui app/assets/translations
	# lupdate parses QML with Qt 4.8, as the phone does; the Qt 6 lint lets some syntax through
	# that fails a whole page there (Recorder.qml, 0.0.1.77).
	if lupdate app/assets/*.qml app/assets/*.js -ts /tmp/qml-check.ts 2>&1 | grep -A2 "error:"; then
		echo "build-ui.sh: QML that Qt 4.8 cannot parse" >&2
		exit 1
	fi
	for ts in app/translations/tengram_*.ts; do
		lrelease "$ts" -qm "app/assets/translations/$(basename "$ts" .ts).qm" >/dev/null
	done
	(cd build/ui && qmake -spec blackberry-armv7le-qcc CONFIG+=device ../../app/tengram.pro >/dev/null &&
		make -j"$(nproc)")
'
