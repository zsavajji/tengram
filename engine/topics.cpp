// The `topics` table (docs/protocol.md): the topics of the forum chats the UI has asked for, `topics.list`, and what
// keeps their rows current. Nothing is written when something arrives: a row is queued, and flush() (main.cpp) writes
// the queue in its transaction, then sends the `topics` events and the replies that waited for the commit
// (queue_reply).
//
// Only a chat in `watched` is kept current: one the UI asked topics.list for, opened with a topic or read in one, or
// that already has rows from an earlier run. A message in any other forum chat costs nothing here. Within those, a
// topic is fetched at most once at a time (see fetch).
//
// TDLib answers every getForumTopic and getForumTopics with an updateForumTopic for each topic it carries (and an
// updateForumTopicInfo for one whose info changed), just before the answer, whether or not anything changed. Taken as
// news they would be fetched again, and so on without end: topics_update ignores them while our own request for the
// topic, or for the chat's list, is out.

#include "engine.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace {

using td_api::make_object;
using Key = std::pair<std::int64_t, std::int32_t>;  // chat, topic

constexpr std::size_t kEventIds = 200;  // most ids in one `topics` event
constexpr int kListLimit = 100;         // topics asked for in one getForumTopics call
constexpr int kListCalls = 20;          // most calls of one topics.list

struct Row {
  std::int64_t chat = 0;
  std::int32_t topic = 0;
  std::string name;
  std::int32_t color = 0;
  int general = 0, pinned = 0, closed = 0, hidden = 0;
  std::string ord;  // the decimal int64 of forumTopic.order
  std::int32_t unread = 0, mentions = 0, reactions = 0;
  std::int64_t read_inbox = 0;
  bool has_last = false;  // last_id, last_preview are NULL without a last message
  std::string last_id;
  std::int64_t last_ts = 0;
  std::string last_preview;
  int last_out = 0;
  std::int64_t muted_until = 0;
  bool operator==(const Row &) const = default;
};

// muted_until is the time the mute was learned plus what was left of it: the same mute learned a second later is not
// a change.
bool same(Row a, const Row &b) {
  if (std::abs(a.muted_until - b.muted_until) <= 2) {
    a.muted_until = b.muted_until;
  }
  return a == b;
}

// The row of a forumTopic; false for one that has no info.
bool row_of(std::int64_t chat, const td_api::forumTopic &t, Row &r) {
  const td_api::forumTopicInfo *info = t.info_.get();
  if (!info) {
    return false;
  }
  r = Row();
  r.chat = chat;
  r.topic = info->forum_topic_id_;
  r.name = info->name_;
  r.color = info->icon_ ? info->icon_->color_ : 0;
  r.general = info->is_general_;
  r.pinned = t.is_pinned_;
  r.closed = info->is_closed_;
  r.hidden = info->is_hidden_;
  r.ord = std::to_string(t.order_);
  r.unread = t.unread_count_;
  r.mentions = t.unread_mention_count_;
  r.reactions = t.unread_reaction_count_;
  r.read_inbox = t.last_read_inbox_message_id_;
  if (const auto *m = t.last_message_.get()) {
    r.has_last = true;
    r.last_id = std::to_string(m->id_);
    r.last_ts = m->date_;
    r.last_preview = message_preview(*m);
    r.last_out = m->is_outgoing_;
  }
  r.muted_until = t.notification_settings_ ? topic_muted_until(*t.notification_settings_) : 0;
  return true;
}

// ---- the table

enum Stmt { kInsert, kDelete, kIds, kChats, kStmts };

const char *const kSql[kStmts] = {
    "INSERT OR REPLACE INTO topics(chat, topic, name, color, general, pinned, closed, hidden, ord, unread, mentions, "
    "read_inbox, last_id, last_ts, last_preview, last_out, muted_until, reactions) "
    "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18)",
    "DELETE FROM topics WHERE chat = ?1 AND topic = ?2",
    "SELECT topic FROM topics WHERE chat = ?1",
    "SELECT DISTINCT chat FROM topics",
};

td::SqliteStatement stmts[kStmts];

bool prepare() {
  bool all = true;
  for (int i = 0; i < kStmts; ++i) {
    if (stmts[i].empty()) {
      stmts[i] = db.prepare(kSql[i]);
    }
    all = all && !stmts[i].empty();
  }
  return all;
}

std::vector<std::function<void()>> ops;                   // what flush() is to do, in order
std::map<Key, Row> written;  // the rows this run wrote, to skip a write that changes nothing
std::map<std::int64_t, std::set<std::int32_t>> changed;  // per chat: the topics written or deleted, event to come

void check(const td::Status &s, const char *what) {
  if (s.is_error()) {
    say("chats.db: topics: %s: %s", what, s.message().c_str());
  }
}

td::Status write_row(const Row &r) {
  const std::string chat = std::to_string(r.chat);  // bound by reference: it must outlive the step
  auto &s = stmts[kInsert];
  s.reset();
  TRY_STATUS(s.bind_string(1, chat));
  TRY_STATUS(s.bind_int32(2, r.topic));
  TRY_STATUS(s.bind_string(3, r.name));
  TRY_STATUS(s.bind_int32(4, r.color));
  TRY_STATUS(s.bind_int32(5, r.general));
  TRY_STATUS(s.bind_int32(6, r.pinned));
  TRY_STATUS(s.bind_int32(7, r.closed));
  TRY_STATUS(s.bind_int32(8, r.hidden));
  TRY_STATUS(s.bind_string(9, r.ord));
  TRY_STATUS(s.bind_int32(10, r.unread));
  TRY_STATUS(s.bind_int32(11, r.mentions));
  TRY_STATUS(s.bind_int64(12, r.read_inbox));
  TRY_STATUS(r.has_last ? s.bind_string(13, r.last_id) : s.bind_null(13));
  TRY_STATUS(s.bind_int64(14, r.last_ts));
  TRY_STATUS(r.has_last ? s.bind_string(15, r.last_preview) : s.bind_null(15));
  TRY_STATUS(s.bind_int32(16, r.last_out));
  TRY_STATUS(s.bind_int64(17, r.muted_until));
  TRY_STATUS(s.bind_int32(18, r.reactions));
  auto status = s.step();
  s.reset();
  return status;
}

void put(const Row &r) {
  auto it = written.find({r.chat, r.topic});
  if (it != written.end() && same(it->second, r)) {
    return;
  }
  auto status = write_row(r);
  check(status, "insert");
  if (status.is_ok()) {
    written[{r.chat, r.topic}] = r;
    changed[r.chat].insert(r.topic);
  }
}

void drop(std::int64_t chat, std::int32_t topic) {
  const std::string c = std::to_string(chat);
  auto &s = stmts[kDelete];
  s.reset();
  auto status = s.bind_string(1, c);
  if (status.is_ok()) {
    status = s.bind_int32(2, topic);
  }
  if (status.is_ok()) {
    status = s.step();
  }
  s.reset();
  check(status, "delete");
  written.erase({chat, topic});
  changed[chat].insert(topic);
}

// The topics the table holds for the chat.
std::vector<std::int32_t> ids_of(std::int64_t chat) {
  std::vector<std::int32_t> ids;
  const std::string c = std::to_string(chat);
  auto &s = stmts[kIds];
  s.reset();
  auto status = s.bind_string(1, c);
  if (status.is_ok()) {
    status = s.step();
  }
  while (status.is_ok() && s.has_row()) {
    ids.push_back(s.view_int32(0));
    status = s.step();
  }
  check(status, "ids");
  s.reset();
  return ids;
}

// ---- keeping a topic's row current

std::set<std::int64_t> watched;
bool seeded = false;  // `watched` has the chats that had rows when the engine started

bool is_watched(std::int64_t chat) {
  if (!seeded && prepare()) {
    seeded = true;
    auto &s = stmts[kChats];
    s.reset();
    auto status = s.step();
    while (status.is_ok() && s.has_row()) {
      std::int64_t id = 0;
      if (parse_i64(s.view_string(0).str(), id)) {
        watched.insert(id);
      }
      status = s.step();
    }
    check(status, "chats");
    s.reset();
  }
  return watched.count(chat) != 0;
}

using Done = std::function<void(const td_api::error *, std::int64_t)>;

// A topic being fetched. A request for it that comes meanwhile sets `again`: the fetch is repeated once, when this one
// is back, and the callbacks wait for that last answer (they asked after what this one may have missed).
struct Flight {
  bool again = false;
  std::vector<Done> waiting;
};
std::map<Key, Flight> flights;
std::multiset<std::int64_t> listing;  // the chats a topics.list is running for

void send_fetch(Key key) {
  tdlib.send(make_object<td_api::getForumTopic>(key.first, key.second), [key](Obj o) {
    const td_api::error *e = as_error(o);
    std::int64_t last = 0;
    if (!e) {
      const auto &t = static_cast<const td_api::forumTopic &>(*o);
      Row r;
      if (row_of(key.first, t, r)) {
        last = t.last_message_ ? t.last_message_->id_ : 0;
        ops.push_back([r] { put(r); });
        mark_dirty();
      }
    } else if (e->code_ == 404) {  // Telegram does not have the topic: it was deleted
      ops.push_back([key] { drop(key.first, key.second); });
      mark_dirty();
    } else {
      say("getForumTopic: %d %s", e->code_, e->message_.c_str());
    }
    auto it = flights.find(key);
    if (it != flights.end() && it->second.again) {
      it->second.again = false;
      return send_fetch(key);
    }
    std::vector<Done> waiting;
    if (it != flights.end()) {
      waiting = std::move(it->second.waiting);
      flights.erase(it);
    }
    for (auto &done : waiting) {
      done(e, last);
    }
  });
}

void fetch(Key key, Done done) {
  auto [it, fresh] = flights.try_emplace(key);
  if (done) {
    it->second.waiting.push_back(std::move(done));
  }
  if (fresh) {
    send_fetch(key);
  } else {
    it->second.again = true;
  }
}

// ---- topics.list

struct List {
  std::uint64_t conn;
  std::string id;
  std::int64_t chat;
  std::vector<Row> rows;
  int calls = 0;
  std::int32_t date = 0, topic = 0;  // where the next call goes on: getForumTopics' offsets
  std::int64_t message = 0;
};

void list_end(std::int64_t chat) {
  auto it = listing.find(chat);  // topics_reset may have cleared it
  if (it != listing.end()) {
    listing.erase(it);
  }
}

// The table becomes exactly Telegram's list for the chat: rows are added or changed, the others deleted.
void list_done(const std::shared_ptr<List> &p) {
  list_end(p->chat);
  ops.push_back([p] {
    std::set<std::int32_t> listed;
    for (const Row &r : p->rows) {
      listed.insert(r.topic);
      put(r);
    }
    for (std::int32_t topic : ids_of(p->chat)) {
      if (!listed.count(topic)) {
        drop(p->chat, topic);
      }
    }
  });
  queue_reply(p->conn, p->id, td::json_encode<std::string>(td::json_object([&](auto &o) {
                o("count", static_cast<std::int32_t>(p->rows.size()));
              })));
}

void list_fetch(const std::shared_ptr<List> &p) {
  tdlib.send(make_object<td_api::getForumTopics>(p->chat, "", p->date, p->message, p->topic, kListLimit), [p](Obj o) {
    if (auto e = as_error(o)) {
      say("td error: %d %s", e->code_, e->message_.c_str());
      list_end(p->chat);
      return reply_td_error(p->conn, p->id, *e);
    }
    const auto &got = static_cast<const td_api::forumTopics &>(*o);
    const std::size_t before = p->rows.size();
    for (const auto &t : got.topics_) {
      Row r;
      if (t && row_of(p->chat, *t, r)) {
        p->rows.push_back(std::move(r));
      }
    }
    // Until the total is reached or a call brings nothing; the calls are capped in case Telegram never says it is done.
    if (p->rows.size() == before || p->rows.size() >= static_cast<std::size_t>(std::max(got.total_count_, 0)) ||
        ++p->calls >= kListCalls) {
      return list_done(p);
    }
    p->date = got.next_offset_date_;
    p->message = got.next_offset_message_id_;
    p->topic = got.next_offset_forum_topic_id_;
    list_fetch(p);
  });
}

}  // namespace

bool topics_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  if (cmd != "topics.list") {
    return false;
  }
  std::int64_t chat = 0;
  if (!request_chat(conn, id, req, chat)) {
    return true;
  }
  ChatBrief brief = {};
  if (!chat_brief(chat, brief) || !brief.forum) {
    server.reply_error(conn, id, "bad_chat", "no such forum");
    return true;
  }
  watched.insert(chat);
  listing.insert(chat);
  list_fetch(std::make_shared<List>(List{conn, id, chat}));
  return true;
}

void topics_update(td_api::Object &update) {
  std::int64_t chat = 0;
  std::int32_t topic = 0;
  if (update.get_id() == td_api::updateForumTopicInfo::ID) {
    const auto &info = static_cast<td_api::updateForumTopicInfo &>(update).info_;
    chat = info ? info->chat_id_ : 0;
    topic = info ? info->forum_topic_id_ : 0;
  } else if (update.get_id() == td_api::updateForumTopic::ID) {
    chat = static_cast<td_api::updateForumTopic &>(update).chat_id_;
    topic = static_cast<td_api::updateForumTopic &>(update).forum_topic_id_;
  } else {
    return;
  }
  if (!flights.count({chat, topic}) && !listing.count(chat)) {  // not the echo of our own request (see the top)
    topics_note(chat, topic);
  }
}

void topics_note(std::int64_t chat, std::int32_t topic) {
  if (topic > 0 && is_watched(chat)) {
    fetch({chat, topic}, nullptr);
  }
}

void topics_watch(std::int64_t chat) {
  watched.insert(chat);
}

void topics_fetch(std::int64_t chat, std::int32_t topic, Done done) {
  watched.insert(chat);
  fetch({chat, topic}, std::move(done));
}

// viewMessages with the topic as its source reads the topic, not the chat. TDLib needs the message in memory and the
// topic known to it, which the fetch (getForumTopic) gives both.
bool topics_read(std::int64_t chat, std::int32_t topic, std::function<void(const td_api::error *)> done,
                 std::int64_t upto) {
  ChatBrief brief = {};
  if (!chat_brief(chat, brief) || !brief.forum) {
    return false;
  }
  topics_fetch(chat, topic, [=](const td_api::error *e, std::int64_t last) {
    if (e || last == 0) {
      return done(e);
    }
    const std::int64_t at = upto > 0 ? std::min(upto, last) : last;  // never past the newest
    tdlib.send(make_object<td_api::viewMessages>(chat, std::vector<std::int64_t>{at},
                                                 make_object<td_api::messageSourceForumTopicHistory>(), true),
               [=](Obj o) {
                 done(as_error(o));
                 fetch({chat, topic}, nullptr);  // the unread count after the read
               });
  });
  return true;
}

void topics_write() {
  if (ops.empty()) {
    return;
  }
  if (!prepare()) {
    ops.clear();
    return;
  }
  auto todo = std::move(ops);
  ops.clear();
  for (auto &op : todo) {
    op();
  }
}

void topics_events() {
  for (auto &[chat, ids] : changed) {
    const std::vector<std::int32_t> all(ids.begin(), ids.end());
    for (std::size_t i = 0; i < all.size(); i += kEventIds) {
      const auto last = static_cast<std::ptrdiff_t>(std::min(i + kEventIds, all.size()));
      const std::vector<std::int32_t> part(all.begin() + static_cast<std::ptrdiff_t>(i), all.begin() + last);
      server.broadcast(td::json_encode<std::string>(td::json_object([&](auto &o) {
        o("ev", "topics");
        o("chat", std::to_string(chat));
        o("topics", td::json_array(part, [](std::int32_t topic) { return topic; }));
      })));
    }
  }
  changed.clear();
}

void topics_reset() {
  ops.clear();
  written.clear();
  changed.clear();
  listing.clear();
  watched.clear();
  seeded = false;
}

void topics_close() {
  for (auto &s : stmts) {
    s = td::SqliteStatement();
  }
}
