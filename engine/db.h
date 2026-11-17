// chats.db: what the UI shows. SQLCipher, WAL, a cache the engine rebuilds when it can't use it.
// Main thread only.
#pragma once

#include "td/db/SqliteDb.h"

#include <string>

class Db {
 public:
  // Opens <dir>/chats.db with the key in <dir>/chats.key, making either when missing, and deleting
  // and recreating the database when it can't be opened or has another user_version. False if even that fails.
  bool open(const std::string &dir);
  void close();

  bool exec(const char *sql);                    // false, after a log line, on failure
  td::SqliteStatement prepare(const char *sql);  // empty() after a log line on failure

  // Writes batch in one transaction: begin() is a no-op while one is open, and the main loop commits
  // once per round (commit() is a no-op when none is open).
  void begin();
  void commit();

  // Deletes every row of every table (logout).
  void wipe();

 private:
  bool try_open(const std::string &raw_key);
  bool create_schema();

  td::SqliteDb db_;
  std::string path_;
  bool in_tx_ = false;
};
