#include "engine.h"

#include "td/db/DbKey.h"
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

// The version-10 schema: HyperBerry's migrations (engine/db.go) as one step, without the stories tables, and
// without its reactions and poll_votes tables (a message's reactions and a poll's counts are in messages.extra),
// and with the engine's own messages.sig, chats.read_inbox, chats.forum, chats.secret, chats.mentions, chats.mentions_off, chats.reactions, chats.translate, messages.topic, messages.tr and messages.tr_lang (and, in kTopics
// and kPeople below, its own tables).
// Column order is theirs, in case the UI reads by position; the engine's own come last, where ALTER TABLE
// puts them in a migrated file. The first line removes the table that SqliteDb::change_key leaves in a
// database it has just encrypted.
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
  archived INTEGER NOT NULL DEFAULT 0,
  read_inbox INTEGER NOT NULL DEFAULT 0,
  forum INTEGER NOT NULL DEFAULT 0,
  secret INTEGER NOT NULL DEFAULT 0,
  mentions INTEGER NOT NULL DEFAULT 0,
  mentions_off INTEGER NOT NULL DEFAULT 0,
  reactions INTEGER NOT NULL DEFAULT 0,
  translate TEXT NOT NULL DEFAULT '');
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
  topic INTEGER NOT NULL DEFAULT 0,
  tr TEXT, tr_lang TEXT,
  PRIMARY KEY(chat, id));
CREATE INDEX messages_by_time ON messages(chat, ts);
CREATE INDEX messages_by_kind ON messages(kind, ts);
CREATE TABLE engine_state(key TEXT PRIMARY KEY, value INTEGER NOT NULL);
)";

// What version 4 added besides the two columns: the forum topics and the index on messages.topic.
const char *const kTopics = R"(
CREATE TABLE topics(
  chat TEXT NOT NULL, topic INTEGER NOT NULL,
  name TEXT,
  color INTEGER,
  general INTEGER, pinned INTEGER, closed INTEGER, hidden INTEGER,
  ord TEXT,
  unread INTEGER, mentions INTEGER, read_inbox INTEGER,
  last_id TEXT, last_ts INTEGER, last_preview TEXT, last_out INTEGER,
  muted_until INTEGER,
  PRIMARY KEY(chat, topic));
CREATE INDEX messages_by_topic ON messages(chat, topic, ts);
)";

// What version 9 added to `topics`: an ALTER, not a column of kTopics, since migrate_3_to_4 makes the table from kTopics and
// migrate_8_to_9 then adds this.
const char *const kTopicReactions = "ALTER TABLE topics ADD COLUMN reactions INTEGER NOT NULL DEFAULT 0";

// What version 7 added: the people the UI draws beside a group's messages. messages.sender is a user's id in decimal, so is
// `user`; `color` is the index senders.get gives (0 to 6, user_color in chats.cpp); `photo` is the local path of the small
// picture, NULL until it is downloaded. The UI joins it to messages, so a group opens with the colours and pictures that
// are final, not the hash of the id that senders.get then replaces.
const char *const kPeople = R"(
CREATE TABLE people(user TEXT PRIMARY KEY, color INTEGER NOT NULL, photo TEXT);
)";

}  // namespace

// The key of both databases, 32 bytes: read from <dir>/db.key (64 hex characters, this user only), or made
// from /dev/random at the first launch. `fresh` says it was made now.
bool load_db_key(const std::string &dir, bool &fresh) {
  const std::string path = dir + "/db.key";
  fresh = false;
  db_key = read_key(path);
  if (!db_key.empty()) {
    return true;
  }
  db_key.resize(32);
  std::FILE *f = std::fopen("/dev/random", "r");
  const bool got = f && std::fread(&db_key[0], 1, db_key.size(), f) == db_key.size();
  if (f) {
    std::fclose(f);
  }
  if (!got || !write_file(path, td::hex_encode(db_key))) {
    say("cannot make the database key (%s from /dev/random, then %s)", got ? "32 bytes" : "nothing", path.c_str());
    db_key.clear();
    return false;
  }
  fresh = true;
  say("made the database key");
  return true;
}

bool Db::open(const std::string &dir, bool fresh_key) {
  path_ = dir + "/chats.db";
  if (fresh_key && access(path_.c_str(), F_OK) == 0) {
    say("chats.db is older than the key: deleting it");
    ok(td::SqliteDb::destroy(path_), "destroy");
  }
  if (try_open(db_key)) {
    return true;
  }
  say("chats.db: deleting it and starting again");
  ok(td::SqliteDb::destroy(path_), "destroy");
  return try_open(db_key);
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
  if (v.is_error() || (v.ok() != 0 && v.ok() != 2 && v.ok() != 3 && v.ok() != 4 && v.ok() != 5 && v.ok() != 6 &&
                         v.ok() != 7 && v.ok() != 8 && v.ok() != 9 && v.ok() != 10)) {
    say("chats.db: user_version %d is not ours", v.is_ok() ? v.ok() : -1);
    db_.close();
    return false;
  }
  if ((v.ok() == 0 && !create_schema()) || (v.ok() == 2 && !migrate_2_to_3()) ||
      ((v.ok() == 2 || v.ok() == 3) && !migrate_3_to_4()) ||
      ((v.ok() == 2 || v.ok() == 3 || v.ok() == 4) && !migrate_4_to_5()) ||
      ((v.ok() == 2 || v.ok() == 3 || v.ok() == 4 || v.ok() == 5) && !migrate_5_to_6()) ||
      ((v.ok() == 2 || v.ok() == 3 || v.ok() == 4 || v.ok() == 5 || v.ok() == 6) && !migrate_6_to_7()) ||
      ((v.ok() >= 2 && v.ok() <= 7) && !migrate_7_to_8()) || ((v.ok() >= 2 && v.ok() <= 8) && !migrate_8_to_9()) ||
      ((v.ok() >= 2 && v.ok() <= 9) && !migrate_9_to_10())) {
    db_.close();
    return false;
  }
  return true;
}

bool Db::create_schema() {
  // One transaction, user_version inside it: a crash leaves either nothing or all of it.
  return ok(db_.begin_write_transaction(), "begin") && exec(kSchema) && exec(kTopics) && exec(kTopicReactions) && exec(kPeople) &&
         ok(db_.set_user_version(10), "user_version") && ok(db_.commit_transaction(), "commit");
}

// Version 2 had no chats.read_inbox. One transaction with user_version inside it, as create_schema: a crash
// leaves version 2 or version 3. The rows keep 0 until the engine writes them again (chats.cpp). A failure
// closes the file and open() deletes it and starts again.
bool Db::migrate_2_to_3() {
  return ok(db_.begin_write_transaction(), "begin") &&
         exec("ALTER TABLE chats ADD COLUMN read_inbox INTEGER NOT NULL DEFAULT 0") &&
         ok(db_.set_user_version(3), "user_version") && ok(db_.commit_transaction(), "commit");
}

// Version 3 had no chats.forum, messages.topic or topics. Same shape as migrate_2_to_3. The messages go: their rows
// have no topic, and a forum's topic pages would never find them. They are a cache that TDLib refills when a chat
// opens (the UI's chat.open writes the newest page again). The tables of reactions and poll votes are still there
// for the next step to drop.
bool Db::migrate_3_to_4() {
  return ok(db_.begin_write_transaction(), "begin") &&
         exec("ALTER TABLE chats ADD COLUMN forum INTEGER NOT NULL DEFAULT 0") &&
         exec("ALTER TABLE messages ADD COLUMN topic INTEGER NOT NULL DEFAULT 0") && exec(kTopics) &&
         exec("DELETE FROM messages") &&
         ok(db_.set_user_version(4), "user_version") && ok(db_.commit_transaction(), "commit");
}

// Version 4 kept reactions and poll votes in tables of their own (always empty); version 5 has them in messages.extra
// (`reactions`, a poll's `votes`). Same shape as the others. The old rows get theirs when the engine writes them again
// (a chat's newest page on chat.open, or any update of the message): the extra changes their sig.
bool Db::migrate_4_to_5() {
  return ok(db_.begin_write_transaction(), "begin") &&
         exec("DROP TABLE IF EXISTS reactions; DROP TABLE IF EXISTS poll_votes") &&
         ok(db_.set_user_version(5), "user_version") && ok(db_.commit_transaction(), "commit");
}

// Version 5 had no chats.secret. Same shape as the others. Every row keeps 0, which is right: the engine ran with secret
// chats off until version 6, so no chat in the file is one.
bool Db::migrate_5_to_6() {
  return ok(db_.begin_write_transaction(), "begin") &&
         exec("ALTER TABLE chats ADD COLUMN secret INTEGER NOT NULL DEFAULT 0") &&
         ok(db_.set_user_version(6), "user_version") && ok(db_.commit_transaction(), "commit");
}

// Version 6 had no `people`. Same shape as the others. The table starts empty: the engine writes a user's row when TDLib
// first tells it of them (chats.cpp), and until then a group's messages from that user have the hash colour, as before.
bool Db::migrate_6_to_7() {
  return ok(db_.begin_write_transaction(), "begin") && exec(kPeople) && ok(db_.set_user_version(7), "user_version") &&
         ok(db_.commit_transaction(), "commit");
}

// Version 7 had no chats.mentions. Same shape as the others. Every row keeps 0 until the engine writes it again, which the
// next start does for every chat whose count is not 0 (its row differs from the one written).
bool Db::migrate_7_to_8() {
  return ok(db_.begin_write_transaction(), "begin") &&
         exec("ALTER TABLE chats ADD COLUMN mentions INTEGER NOT NULL DEFAULT 0") &&
         ok(db_.set_user_version(8), "user_version") && ok(db_.commit_transaction(), "commit");
}

// Version 8's chats.mentions was 0 while the chat's mention notifications were off; version 9 keeps the count whatever
// they are (the chat page's @ button) and says whether they are off in chats.mentions_off, and adds the unread reactions
// to our messages (chats.reactions, topics.reactions; the chat page's reactions button). The next start writes every
// chat row that differs, as for version 8; a topic's row, when the UI next lists the forum's topics.
bool Db::migrate_8_to_9() {
  return ok(db_.begin_write_transaction(), "begin") &&
         exec("ALTER TABLE chats ADD COLUMN mentions_off INTEGER NOT NULL DEFAULT 0") &&
         exec("ALTER TABLE chats ADD COLUMN reactions INTEGER NOT NULL DEFAULT 0") && exec(kTopicReactions) &&
         ok(db_.set_user_version(9), "user_version") && ok(db_.commit_transaction(), "commit");
}

// Version 9 had no translation: messages.tr and tr_lang (NULL until a message is translated) and chats.translate (the language
// the chat's incoming messages are shown in, '' off). Same shape as the others.
bool Db::migrate_9_to_10() {
  return ok(db_.begin_write_transaction(), "begin") && exec("ALTER TABLE messages ADD COLUMN tr TEXT") &&
         exec("ALTER TABLE messages ADD COLUMN tr_lang TEXT") &&
         exec("ALTER TABLE chats ADD COLUMN translate TEXT NOT NULL DEFAULT ''") &&
         ok(db_.set_user_version(10), "user_version") && ok(db_.commit_transaction(), "commit");
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
  exec("DELETE FROM chats; DELETE FROM messages; DELETE FROM topics; DELETE FROM people; DELETE FROM engine_state");
  commit();
}
