#!/bin/sh
# Builds build/qtsqlcipher/sqlciphertest for BB10: Qt 4.8's SQLite driver on TDLib's SQLCipher, in the Native
# SDK image the UI is built with (qcc). The driver's two files come from Qt 4.8.6 (LGPL) with the class
# renamed to QSQLCipherDriver, since the phone's QtSql already has a QSQLiteDriver.
# CRYPTO: the libcrypto to link, as the container sees it (default: ours, static; "-lcrypto" is the phone's).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=${BB10_NDK_IMAGE:-yamsergey/bb10-ndk:0.6}
CRYPTO=${CRYPTO:-/src/build/prefix/lib/libcrypto.a}
OUT=$ROOT/build/qtsqlcipher
mkdir -p "$OUT"
# The renamed driver is fetched by scripts/fetch-qtsqlcipher.sh into lib/qtsqlcipher; ui/sqlcipher/test.pro reads it
# from build/qtsqlcipher.
"$ROOT/scripts/fetch-qtsqlcipher.sh"
cp "$ROOT/lib/qtsqlcipher/qsql_sqlcipher.cpp" "$ROOT/lib/qtsqlcipher/qsql_sqlcipher.h" "$OUT/"
docker run --rm -e CRYPTO="$CRYPTO" -v "$ROOT:/src" -w /src "$IMAGE" bash -c '
	source /root/bbndk/bbndk-env_10_3_1_995.sh >/dev/null
	set -eu
	mkdir -p build/qtsqlcipher/obj && cd build/qtsqlcipher/obj
	qmake -spec blackberry-armv7le-qcc CONFIG+=device "CRYPTO=$CRYPTO" ../../../ui/sqlcipher/test.pro >/dev/null
	make -j"$(nproc)" 2>&1 | grep -E "error|warning: |undefined|Error" | head -40 || true
	cp sqlciphertest .. && ls -l ../sqlciphertest'
