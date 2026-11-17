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
fetch() { # file sha256
	[ -f "$OUT/orig-$1" ] || curl -sSfL -o "$OUT/orig-$1" "https://raw.githubusercontent.com/qt/qt/v4.8.6/src/sql/drivers/sqlite/$1"
	[ "$(shasum -a 256 "$OUT/orig-$1" | cut -d' ' -f1)" = "$2" ] || { echo "sha256 mismatch for $1" >&2; exit 1; }
}
fetch qsql_sqlite.cpp 750478282f667844478d58a39c7eb0d729694ffe7365977849ff5c0984baf331
fetch qsql_sqlite.h 5fe9d1c08cae1e8fc5ce03af02f7e1409d435ecb314de6ab5a68d66ca3989ed0
for f in cpp h; do
	sed -e 's/QSQLite/QSQLCipher/g' -e 's/QSQL_SQLITE_H/QSQL_SQLCIPHER_H/g' -e 's/qsql_sqlite\.h/qsql_sqlcipher.h/' \
		-e 's/#include <sqlite3.h>/#include "tdsqlite_names.h"/' -e 's/^struct sqlite3;/#include "tdsqlite_names.h"/' \
		-e 's/define Q_EXPORT_SQLDRIVER_SQLITE Q_SQL_EXPORT/define Q_EXPORT_SQLDRIVER_SQLITE/' \
		"$OUT/orig-qsql_sqlite.$f" >"$OUT/qsql_sqlcipher.$f"
done
docker run --rm -e CRYPTO="$CRYPTO" -v "$ROOT:/src" -w /src "$IMAGE" bash -c '
	source /root/bbndk/bbndk-env_10_3_1_995.sh >/dev/null
	set -eu
	mkdir -p build/qtsqlcipher/obj && cd build/qtsqlcipher/obj
	qmake -spec blackberry-armv7le-qcc CONFIG+=device "CRYPTO=$CRYPTO" ../../../ui/sqlcipher/test.pro >/dev/null
	make -j"$(nproc)" 2>&1 | grep -E "error|warning: |undefined|Error" | head -40 || true
	cp sqlciphertest .. && ls -l ../sqlciphertest'
