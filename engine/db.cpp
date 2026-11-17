#include "engine.h"

#include "td/db/DbKey.h"
#include "td/utils/Random.h"
#include "td/utils/misc.h"

#include <unistd.h>

#include <cstdio>

namespace {

bool ok(td::Status s, const char *what) {
  if (s.is_error()) {
    say("chats.db: %s: %s", what, s.message().c_str());
    return false;
  }
  return true;
}

// The 32 key bytes from the 64 hex characters in the file, or "" when the file is missing or wrong.
std::string read_key(const std::string &path) {
  std::FILE *f = std::fopen(path.c_str(), "r");
  if (!f) {
    return "";
  }
  char buf[80];
  std::size_t n = std::fread(buf, 1, sizeof buf, f);
  std::fclose(f);
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
    --n;
  }
  auto raw = td::hex_decode(td::Slice(buf, n));
  return n == 64 && raw.is_ok() ? raw.move_as_ok() : "";
}

// The version-2 schema: HyperBerry's migrations (engine/db.go) as one step, without the stories tables, and
// with the engine's own messages.sig.
// Column order is theirs, in case the UI reads by position. The first line removes the table that
// SqliteDb::change_key leaves in a database it has just encrypted.
const char *const kSchema = R"(
DROP TABLE IF EXISTS encryption_dummy_table;
CREATE TABLE chats(
  jid TEXT PRIMARY KEY,
  kind INTEGER NOT NULL,
  name TEXT,
  photo TEXT,
  last_ts INTEGER, last_preview TEXT, last_out INTEGER, last_state INTEGER,
  unread INTEGER NOT NULL DEFAULT 0,
  muted_until INTEGER NOT NULL DEFAULT 0,
  pinned INTEGER NOT NULL DEFAULT 0,
  archived INTEGER NOT NULL DEFAULT 0);
CREATE TABLE messages(
  chat TEXT NOT NULL, id TEXT NOT NULL,
  ts INTEGER NOT NULL,
  out INTEGER NOT NULL, state INTEGER NOT NULL,
  sender TEXT, sender_name TEXT,
  kind INTEGER NOT NULL, text TEXT,
  quote_id TEXT, quote_preview TEXT,
  edited INTEGER NOT NULL DEFAULT 0,
  deleted INTEGER NOT NULL DEFAULT 0,
  duration INTEGER, waveform BLOB, media TEXT, media_state INTEGER NOT NULL DEFAULT 0,
  played INTEGER NOT NULL DEFAULT 0,
  thumb BLOB, width INTEGER, height INTEGER, size INTEGER, path TEXT,
  extra TEXT,
  starred INTEGER NOT NULL DEFAULT 0,
  sig INTEGER,
  PRIMARY KEY(chat, id));
CREATE INDEX messages_by_time ON messages(chat, ts);
CREATE INDEX messages_by_kind ON messages(kind, ts);
CREATE TABLE reactions(
  chat TEXT NOT NULL, msg TEXT NOT NULL,
  sender TEXT NOT NULL,
  sender_name TEXT,
  emoji TEXT NOT NULL,
  ts INTEGER NOT NULL,
  PRIMARY KEY(chat, msg, sender));
CREATE TABLE poll_votes(
  chat TEXT NOT NULL, msg TEXT NOT NULL,
  sender TEXT NOT NULL,
  sender_name TEXT,
  options TEXT NOT NULL,
  ts INTEGER NOT NULL,
  PRIMARY KEY(chat, msg, sender));
CREATE TABLE engine_state(key TEXT PRIMARY KEY, value INTEGER NOT NULL);
)";

}  // namespace

bool Db::open(const std::string &dir) {
  path_ = dir + "/chats.db";
  const std::string key_path = dir + "/chats.key";
  std::string raw = read_key(key_path);
  if (raw.empty()) {
    if (access(path_.c_str(), F_OK) == 0) {
      say("chats.db has no usable chats.key: deleting it");
      ok(td::SqliteDb::destroy(path_), "destroy");
    }
    raw.resize(32);
    td::Random::secure_bytes(raw);
    if (!write_file(key_path, td::hex_encode(raw))) {
      say("cannot write %s", key_path.c_str());
      return false;
    }
  }
  if (try_open(raw)) {
    return true;
  }
  say("chats.db: deleting it and starting again");
  ok(td::SqliteDb::destroy(path_), "destroy");
  return try_open(raw);
}

bool Db::try_open(const std::string &raw_key) {
  // change_key, not open_with_key: only it can create the file (as TDLib creates its own database).
  auto r = td::SqliteDb::change_key(path_, true, td::DbKey::raw_key(raw_key), td::DbKey::empty());
  if (r.is_error()) {
    say("chats.db: open: %s", r.error().message().c_str());
    return false;
  }
  db_ = r.move_as_ok();
  if (!exec("PRAGMA journal_mode=WAL") || !exec("PRAGMA synchronous=NORMAL") || !exec("PRAGMA cache_size=-1024")) {
    db_.close();
    return false;
  }
  auto v = db_.user_version();
  if (v.is_error() || (v.ok() != 0 && v.ok() != 2)) {
    say("chats.db: user_version %d is not ours", v.is_ok() ? v.ok() : -1);
    db_.close();
    return false;
  }
  if (v.ok() == 0 && !create_schema()) {
    db_.close();
    return false;
  }
  return true;
}

bool Db::create_schema() {
  // One transaction, user_version inside it: a crash leaves either nothing or all of it.
  return ok(db_.begin_write_transaction(), "begin") && exec(kSchema) &&
         ok(db_.set_user_version(2), "user_version") && ok(db_.commit_transaction(), "commit");
}

void Db::close() {
  in_tx_ = false;
  db_.close();
}

bool Db::exec(const char *sql) {
  return ok(db_.exec(td::CSlice(sql)), "exec");
}

td::SqliteStatement Db::prepare(const char *sql) {
  auto r = db_.get_statement(td::CSlice(sql));
  if (r.is_error()) {
    say("chats.db: prepare: %s", r.error().message().c_str());
    return {};
  }
  return r.move_as_ok();
}

void Db::begin() {
  if (!in_tx_ && ok(db_.begin_write_transaction(), "begin")) {
    in_tx_ = true;
  }
}

void Db::commit() {
  if (in_tx_) {
    in_tx_ = false;
    ok(db_.commit_transaction(), "commit");
  }
}

void Db::wipe() {
  begin();
  exec("DELETE FROM chats; DELETE FROM messages; DELETE FROM reactions; DELETE FROM poll_votes; "
       "DELETE FROM engine_state");
  commit();
}
