#!/bin/sh
# Fetches Qt 4.8.6's SQLite driver (LGPL) into lib/qtsqlcipher/ and renames its class to QSQLCipherDriver,
# since the phone's QtSql already has a QSQLiteDriver: the UI links this one on TDLib's SQLCipher amalgamation
# (ui/sqlcipher/, app/src/ChatsDb.cpp). The two files are pinned by sha256; the renamed copies are
# lib/qtsqlcipher/qsql_sqlcipher.cpp and .h. Not committed. Does no work when they are already there.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/lib/qtsqlcipher
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
		"$OUT/orig-qsql_sqlite.$f" >"$OUT/qsql_sqlcipher.$f.part"
	# Only a changed file replaces the old one, so make does not rebuild what did not change.
	if cmp -s "$OUT/qsql_sqlcipher.$f.part" "$OUT/qsql_sqlcipher.$f" 2>/dev/null; then
		rm -f "$OUT/qsql_sqlcipher.$f.part"
	else
		mv "$OUT/qsql_sqlcipher.$f.part" "$OUT/qsql_sqlcipher.$f"
	fi
done
