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
