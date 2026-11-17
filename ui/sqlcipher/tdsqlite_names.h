// Qt's SQLite driver calls SQLite by its usual names; TDLib's SQLCipher amalgamation
// (third_party/td/sqlite) has every name prefixed with "td". This maps the ones the driver uses.
#pragma once
#include "sqlite/sqlite3.h"

#define sqlite3 tdsqlite3
#define sqlite3_stmt tdsqlite3_stmt
#define sqlite3_bind_blob tdsqlite3_bind_blob
#define sqlite3_bind_double tdsqlite3_bind_double
#define sqlite3_bind_int tdsqlite3_bind_int
#define sqlite3_bind_int64 tdsqlite3_bind_int64
#define sqlite3_bind_null tdsqlite3_bind_null
#define sqlite3_bind_parameter_count tdsqlite3_bind_parameter_count
#define sqlite3_bind_text16 tdsqlite3_bind_text16
#define sqlite3_busy_timeout tdsqlite3_busy_timeout
#define sqlite3_changes tdsqlite3_changes
#define sqlite3_close tdsqlite3_close
#define sqlite3_column_blob tdsqlite3_column_blob
#define sqlite3_column_bytes tdsqlite3_column_bytes
#define sqlite3_column_bytes16 tdsqlite3_column_bytes16
#define sqlite3_column_count tdsqlite3_column_count
#define sqlite3_column_decltype16 tdsqlite3_column_decltype16
#define sqlite3_column_double tdsqlite3_column_double
#define sqlite3_column_int tdsqlite3_column_int
#define sqlite3_column_int64 tdsqlite3_column_int64
#define sqlite3_column_name16 tdsqlite3_column_name16
#define sqlite3_column_text16 tdsqlite3_column_text16
#define sqlite3_column_type tdsqlite3_column_type
#define sqlite3_enable_shared_cache tdsqlite3_enable_shared_cache
#define sqlite3_errmsg16 tdsqlite3_errmsg16
#define sqlite3_finalize tdsqlite3_finalize
#define sqlite3_last_insert_rowid tdsqlite3_last_insert_rowid
#define sqlite3_open_v2 tdsqlite3_open_v2
#define sqlite3_prepare16 tdsqlite3_prepare16
#define sqlite3_prepare16_v2 tdsqlite3_prepare16_v2
#define sqlite3_reset tdsqlite3_reset
#define sqlite3_step tdsqlite3_step
