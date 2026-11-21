// chats.db: what the UI shows. SQLCipher, WAL, a cache the engine rebuilds when it can't use it.
// Main thread only.
#pragma once

#include "td/db/SqliteDb.h"

#include <string>

class Db {
 public:
  // Opens <dir>/chats.db with db_key, making it when missing, and deleting and recreating it when it can't be
  // opened, has another user_version, or the key is new. False if even that fails.
  bool open(const std::string &dir, bool fresh_key);
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
  bool migrate_2_to_3();
  bool migrate_3_to_4();
  bool migrate_4_to_5();
  bool migrate_5_to_6();
  bool migrate_6_to_7();
  bool migrate_7_to_8();
  bool migrate_8_to_9();
  bool migrate_9_to_10();

  td::SqliteDb db_;
  std::string path_;
  bool in_tx_ = false;
};
