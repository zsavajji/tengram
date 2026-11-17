// dbq: runs one SQL statement on the engine's chats.db and prints the rows, tab separated (NULL as NULL,
// a blob as <blob N>). The way to look into the encrypted database from a shell, and a second process
// reading it while the engine writes.
// Usage: dbq <chats.db> <db.key> "<sql>"      Exit 1 and the error on stderr if anything fails.

#include "sqlite/sqlite3.h"
#include "td/db/DbKey.h"
#include "td/db/SqliteDb.h"
#include "td/utils/misc.h"

#include <cstdio>
#include <cstring>
#include <string>

static void usage(std::FILE *to) {
  std::fprintf(to, "usage: dbq <chats.db> <db.key> \"<sql>\"\n");
}

int main(int argc, char **argv) {
  if (argc == 2 && std::strcmp(argv[1], "-h") == 0) {
    usage(stdout);
    return 0;
  }
  if (argc != 4) {
    usage(stderr);
    return 2;
  }

  char hex[80];
  std::FILE *f = std::fopen(argv[2], "r");
  std::size_t n = f ? std::fread(hex, 1, sizeof hex, f) : 0;
  if (f) {
    std::fclose(f);
  }
  while (n > 0 && (hex[n - 1] == '\n' || hex[n - 1] == '\r')) {
    --n;
  }
  auto raw = td::hex_decode(td::Slice(hex, n));
  if (n != 64 || raw.is_error()) {
    std::fprintf(stderr, "dbq: %s is not 64 hex characters\n", argv[2]);
    return 1;
  }

  auto r = td::SqliteDb::open_with_key(std::string(argv[1]), false, td::DbKey::raw_key(raw.move_as_ok()));
  if (r.is_error()) {
    std::fprintf(stderr, "dbq: %s\n", r.error().message().c_str());
    return 1;
  }
  auto db = r.move_as_ok();

  // SqliteStatement has no column count, so this uses SQLite's own interface.
  tdsqlite3_stmt *st = nullptr;
  if (tdsqlite3_prepare_v2(db.get_native(), argv[3], -1, &st, nullptr) != SQLITE_OK) {
    std::fprintf(stderr, "dbq: %s\n", tdsqlite3_errmsg(db.get_native()));
    return 1;
  }
  int rc;
  while ((rc = tdsqlite3_step(st)) == SQLITE_ROW) {
    for (int i = 0; i < tdsqlite3_column_count(st); ++i) {
      if (i) {
        std::putchar('\t');
      }
      switch (tdsqlite3_column_type(st, i)) {
        case SQLITE_NULL:
          std::fputs("NULL", stdout);
          break;
        case SQLITE_BLOB:
          std::printf("<blob %d>", tdsqlite3_column_bytes(st, i));
          break;
        default:
          std::fputs(reinterpret_cast<const char *>(tdsqlite3_column_text(st, i)), stdout);
      }
    }
    std::putchar('\n');
  }
  if (rc != SQLITE_DONE) {
    std::fprintf(stderr, "dbq: %s\n", tdsqlite3_errmsg(db.get_native()));
    tdsqlite3_finalize(st);
    return 1;
  }
  tdsqlite3_finalize(st);
  return 0;
}
