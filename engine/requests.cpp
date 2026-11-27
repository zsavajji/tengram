// The requests about chats and messages: chat.open, chat.history, chat.newer, chat.pins, chat.find, chat.close, chat.read,
// chat.typing, chat.set, msg.send, media.send, media.album, msg.delete, msg.edit, msg.forward, msg.react, msg.reactions,
// msg.thread, poll.vote, media.get and media.cancel (docs/protocol.md). The rows they ask for are written by messages.cpp; the
// replies that tell the UI to read them come after the flush that commits them (queue_reply).

#include "engine.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace {

using td_api::make_object;

std::string page_json(const std::string &oldest, const std::string &newest, std::size_t count, bool more) {
  return td::json_encode<std::string>(td::json_object([&](auto &o) {
    o("oldest", oldest);
    o("newest", newest);
    o("count", static_cast<std::int32_t>(count));
    o("more", td::JsonBool(more));
  }));
}

// ---- chat.open, chat.history and chat.newer: a page of history

struct Page {
  std::uint64_t conn;
  std::string id;
  std::int64_t chat;
  std::size_t want;  // messages to collect
  std::int64_t upper;  // the table's rows from the oldest message up to here are the page's to reconcile (INT64_MAX: up to the newest, finish)
  std::vector<td_api::object_ptr<td_api::message>> got;  // newest first
  bool more = true;
  int calls = 0;
  bool newer = false;  // chat.newer: upper is the newest message's id + 1, and `more` asks about newer messages
  std::int32_t topic = -1;  // a forum topic's page (its history, its rows reconciled), or -1 for the chat's
  std::int64_t last = 0;    // chat.newer of a topic or a thread: its newest message id when the request began
  std::int64_t thread = 0;  // a comment thread's page: the thread's root id in the discussion group, else 0
  bool rooted = false;      // a thread's page has asked for the root post, once
};

// The newest message the engine knows of in the page's chat, or topic or thread; 0 when there is none.
std::int64_t newest_known(const Page &p) {
  ChatBrief brief = {};
  if (p.topic > 0 || p.thread > 0) {
    return p.last;
  }
  return chat_brief(p.chat, brief) && brief.has_last ? brief.last_id : 0;
}

// The history call of the page. A topic's or a thread's comes from Telegram every time: TDLib keeps no local list of it.
td_api::object_ptr<td_api::Function> history_call(const Page &p, std::int64_t from, int offset, int limit) {
  if (p.thread > 0) {
    return make_object<td_api::getMessageThreadHistory>(p.chat, p.thread, from, offset, limit);
  }
  if (p.topic > 0) {
    return make_object<td_api::getForumTopicHistory>(p.chat, p.topic, from, offset, limit);
  }
  return make_object<td_api::getChatHistory>(p.chat, from, offset, limit, false);
}

void finish(const std::shared_ptr<Page> &p) {
  if (p->got.empty()) {  // nothing to compare with, nothing written, so nothing to wait for
    server.reply_ok(p->conn, p->id, page_json("", "", 0, false));
    return;
  }
  const std::int64_t oldest = p->got.back()->id_;
  const std::int64_t newest = p->got.front()->id_;
  std::int64_t upper = p->upper;
  if (p->newer) {  // the newest message itself is part of the page
    p->more = newest < newest_known(*p);
  }
  // chat.open's page (no upper of its own) and chat.newer's end at their newest message: a row above it came from an update
  // after TDLib answered the first call (the later calls only page older), and the page can't speak for it. Up to
  // INT64_MAX, a message that arrived while a slow open paged on was erased until something wrote it again, the next open at the
  // latest (the user, 2026-11-25, a 1:1 chat on the Classic: chat.open took 8.6 s). Our own rows still sending (temporary ids
  // above the newest) are spared too. A message deleted above it still goes: TDLib sends updateDeleteMessages.
  if (p->newer || upper == INT64_MAX) {
    upper = newest + 1;
  }
  messages_reconcile(p->chat, p->thread > 0 ? p->thread : p->topic, p->got, oldest, upper);
  queue_reply(p->conn, p->id, page_json(std::to_string(oldest), std::to_string(newest), p->got.size(), p->more));
}

// The start of a comment thread's history: getMessageThreadHistory leaves the root out, so the post itself (the
// group's copy of it) is asked for and goes at the end of the page, below the oldest comment. A failure only costs the
// post: the comments are the page.
void add_root(const std::shared_ptr<Page> &p) {
  tdlib.send(make_object<td_api::getMessageThread>(p->chat, p->thread), [p](Obj o) {
    if (auto e = as_error(o)) {
      say("td error: %d %s", e->code_, e->message_.c_str());
    } else {
      const std::int64_t below = p->got.empty() ? INT64_MAX : p->got.back()->id_;
      for (auto &m : static_cast<td_api::messageThreadInfo &>(*o).messages_) {  // newest first, as the page
        if (m && m->chat_id_ == p->chat && m->id_ < below) {
          p->got.push_back(std::move(m));
        }
      }
    }
    finish(p);
  });
}

// TDLib may answer with fewer messages than asked (often one, the first time), so this asks again from the
// oldest message so far. On the phone from_message_id itself does not come back with offset 0; the filter
// below costs nothing if a TDLib ever sends it.
void fetch(const std::shared_ptr<Page> &p, std::int64_t from) {
  const int limit = static_cast<int>(std::min<std::size_t>(100, p->want - p->got.size()));
  tdlib.send(history_call(*p, from, 0, limit), [p, from](Obj o) {
    if (auto e = as_error(o)) {
      say("td error: %d %s", e->code_, e->message_.c_str());
      reply_td_error(p->conn, p->id, *e);
      return;
    }
    const std::size_t before = p->got.size();
    for (auto &m : static_cast<td_api::messages &>(*o).messages_) {
      if (m && (from == 0 || m->id_ < from)) {
        p->got.push_back(std::move(m));
      }
    }
    if (p->got.size() == before) {
      // TDLib answers empty only after its own tries from the database and the server (left_tries in
      // get_dialog_history), so this is the start of the history. meegram2 retried empty answers; if a
      // chat ever stops paging early, this line says where.
      if (from != 0) {
        say("history: nothing older than %lld in %lld", static_cast<long long>(from), static_cast<long long>(p->chat));
      }
      p->more = false;
      if (p->thread > 0 && !p->rooted) {
        p->rooted = true;
        return add_root(p);
      }
      return finish(p);
    }
    if (p->got.size() >= p->want || ++p->calls >= 20) {
      return finish(p);
    }
    fetch(p, p->got.back()->id_);
  });
}

// The page of chat.newer: the messages after `from`, collected newest first (each call's batch goes in front).
//
// THE ABORT THIS AVOIDS: with a negative offset, OrderedMessages::get_history has CHECK(!have_a_gap) once
// from_message_id >= the chat's last_message_id and that message is not in TDLib's loaded set (a closeChat unloads
// it): TDLib aborts the whole process. meegram2 hit it (docs/ui-fork.md, 3.6). So a negative offset is never sent
// with `from` >= the chat's last message id as the engine knows it. The engine learns that id from TDLib's own
// updates, so it is not ahead of TDLib's; it is read again right before every call, and when `from` has reached
// it the page ends here without asking.
//
// A topic's page (getForumTopicHistory) has no such abort: it never reads TDLib's loaded messages, only Telegram's
// answer (a search for the topic's messages), so no CHECK(!have_a_gap) is on its path. The bound is still used, as the
// end of the page: for a topic it is the topic's newest message (Page::last), fetched when the request began. A comment
// thread's page (getMessageThreadHistory) is the same: it is cut like a topic's, and its bound is the thread's last
// comment.
void fetch_newer(const std::shared_ptr<Page> &p, std::int64_t from) {
  if (from >= newest_known(*p)) {
    return finish(p);
  }
  // offset -k with limit k: TDLib raises limit to k + 1, starts k newer than `from` and drops `from` itself, so
  // the answer is the k messages right above it, newest first (fewer near the newest; then `from` and older ones
  // can come back, which the filter drops).
  // A topic's history cuts its slice differently: with offset -k and limit k it answers `from` and the k - 1 messages
  // above it (and for k = 1 nothing but `from`). So a topic is asked for offset -(k + 1) and limit k + 1, which gives
  // `from` and k above it: -offset must stay under 100, so its k is at most 98. A thread's history slices as a topic's.
  const int extra = p->topic > 0 || p->thread > 0 ? 1 : 0;
  const int k = static_cast<int>(std::min<std::size_t>(99 - extra, p->want - p->got.size()));
  tdlib.send(history_call(*p, from, -k - extra, k + extra), [p, from](Obj o) {
    if (auto e = as_error(o)) {
      say("td error: %d %s", e->code_, e->message_.c_str());
      reply_td_error(p->conn, p->id, *e);
      return;
    }
    std::vector<td_api::object_ptr<td_api::message>> batch;
    for (auto &m : static_cast<td_api::messages &>(*o).messages_) {
      if (m && m->id_ > from) {
        batch.push_back(std::move(m));
      }
    }
    if (batch.empty()) {  // TDLib has tried its database and the server: nothing newer, or nothing it can give
      say("history: nothing newer than %lld in %lld", static_cast<long long>(from), static_cast<long long>(p->chat));
      return finish(p);
    }
    p->got.insert(p->got.begin(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
    if (p->got.size() >= p->want || ++p->calls >= 20) {
      return finish(p);
    }
    fetch_newer(p, p->got.front()->id_);
  });
}

// The optional number field "limit", a whole number from 1 to `most`, else 50. False after the reply.
bool parse_limit(std::uint64_t conn, const std::string &id, td::JsonObject &req, long most, long &limit) {
  limit = 50;
  td::JsonValue v;
  const int given = optional_field(req, "limit", td::JsonValue::Type::Number, v);
  if (given == 1) {
    char *end = nullptr;
    const std::string number = v.get_number().str();
    limit = std::strtol(number.c_str(), &end, 10);
    if (*end) {
      limit = 0;
    }
  }
  if (given < 0 || limit < 1 || limit > most) {
    bad_request(conn, id, "limit is a number from 1 to " + std::to_string(most));
    return false;
  }
  return true;
}

void chat_open(std::uint64_t conn, const std::string &id, std::int64_t chat, std::size_t want, std::int32_t topic,
               std::int64_t thread) {
  tdlib.send(chat_lookup(chat), [=](Obj o) {
    if (as_error(o)) {
      return server.reply_error(conn, id, "bad_chat", "no such chat");
    }
    tdlib.send(make_object<td_api::openChat>(chat), [=](Obj o2) {
      if (auto e = as_error(o2)) {
        say("td error: %d %s", e->code_, e->message_.c_str());
        return reply_td_error(conn, id, *e);
      }
      live_open(conn, chat);  // typing and presence now flow for it
      auto page = std::make_shared<Page>(Page{conn, id, chat, want, INT64_MAX, {}});
      page->topic = topic;
      page->thread = thread;
      if (topic > 0) {
        topics_watch(chat);
      }
      fetch(page, 0);
    });
  });
}

void chat_history(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t before = 0;
  auto text = string_field(req, "before");
  if (text.is_error() || !parse_i64(text.ok(), before) || before <= 0) {
    return bad_request(conn, id, "need the field before, a string with a message id");
  }
  long limit = 0;
  std::int32_t topic = -1;
  std::int64_t thread = 0;
  if (!parse_limit(conn, id, req, 100, limit) || !topic_thread_fields(conn, id, req, topic, thread)) {
    return;
  }
  auto page = std::make_shared<Page>(Page{conn, id, chat, static_cast<std::size_t>(limit), before, {}});
  page->topic = topic;
  page->thread = thread;
  fetch(page, before);
}

void chat_newer(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t after = 0;
  long limit = 0;
  std::int32_t topic = -1;
  std::int64_t thread = 0;
  if (!message_id(conn, id, req, "after", after) || !parse_limit(conn, id, req, 100, limit) ||
      !topic_thread_fields(conn, id, req, topic, thread)) {
    return;
  }
  ChatBrief brief = {};
  if (!chat_brief(chat, brief)) {
    return server.reply_error(conn, id, "bad_chat", "no such chat");
  }
  auto page = std::make_shared<Page>(Page{conn, id, chat, static_cast<std::size_t>(limit), 0, {}});
  page->newer = true;
  page->topic = topic;
  page->thread = thread;
  if (thread > 0) {
    // The thread's newest comment is Telegram's to say, as a topic's: the group's own last message is not it.
    tdlib.send(make_object<td_api::getMessageThread>(chat, thread), [page, after](Obj o) {
      if (auto e = as_error(o)) {
        say("td error: %d %s", e->code_, e->message_.c_str());
        return reply_td_error(page->conn, page->id, *e);
      }
      const auto &info = static_cast<td_api::messageThreadInfo &>(*o);
      page->last = info.reply_info_ ? info.reply_info_->last_message_id_ : 0;
      fetch_newer(page, after);
    });
    return;
  }
  if (topic < 0) {
    // Replies at once with the empty page when `after` is the chat's last message or newer.
    return fetch_newer(page, after);
  }
  // The topic's newest message is Telegram's to say: the UI may never have asked for the chat's topics.
  topics_fetch(chat, topic, [page, after](const td_api::error *e, std::int64_t last) {
    if (e) {
      say("td error: %d %s", e->code_, e->message_.c_str());
      return reply_td_error(page->conn, page->id, *e);
    }
    page->last = last;
    fetch_newer(page, after);
  });
}

// ---- chat.pins and chat.find: the pinned messages, or the messages Telegram finds for a text, newest message first

struct Pinned {
  std::uint64_t conn;
  std::string id;
  std::int64_t chat;
  std::int32_t topic;
  std::int64_t thread;
  std::string query;  // "": the pinned messages; else the messages Telegram's search finds for it
  std::vector<std::int64_t> found;
  int calls = 0;
};

// Telegram's own list: a pin never expires, and unpinning leaves no message, so it is asked for, not kept. The same walk
// serves chat.find (query not empty): Telegram's search of the chat's whole history, fuzzy as its app's.
// A pinned message goes through messages_queue: the UI reads its row for the preview and the jump. A found one does
// not: the jump to it opens a window of history at it (chat.newer), which writes the rows it reads.
void fetch_pins(const std::shared_ptr<Pinned> &p, std::int64_t from) {
  const bool pinned = p->query.empty();
  tdlib.send(make_object<td_api::searchChatMessages>(
                 p->chat, topic_object(p->topic, p->thread), p->query, nullptr, from, 0, 100,
                 pinned ? td_api::object_ptr<td_api::SearchMessagesFilter>(make_object<td_api::searchMessagesFilterPinned>())
                        : nullptr),
             [p, pinned](Obj o) {
               if (auto e = as_error(o)) {
                 say("td error: %d %s", e->code_, e->message_.c_str());
                 return reply_td_error(p->conn, p->id, *e);
               }
               auto &r = static_cast<td_api::foundChatMessages &>(*o);
               for (auto &m : r.messages_) {
                 if (m) {
                   if (pinned) {
                     messages_queue(*m);
                   }
                   p->found.push_back(m->id_);
                 }
               }
               if (r.messages_.empty() || r.next_from_message_id_ == 0 || ++p->calls >= 5) {
                 std::sort(p->found.begin(), p->found.end(), std::greater<>());
                 // A page may repeat its `from`.
                 p->found.erase(std::unique(p->found.begin(), p->found.end()), p->found.end());
                 const auto text = [](std::int64_t m) { return std::to_string(m); };
                 return queue_reply(p->conn, p->id, td::json_encode<std::string>(td::json_object([&](auto &out) {
                                      out("ids", td::json_array(p->found, text));
                                    })));
               }
               fetch_pins(p, r.next_from_message_id_);
             });
}

void chat_pins(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int32_t topic = -1;
  if (topic_field(conn, id, req, topic)) {
    fetch_pins(std::make_shared<Pinned>(Pinned{conn, id, chat, topic, 0, "", {}}), 0);
  }
}

// `query` is the text to find, not empty; `topic` or `thread` narrows it to a forum topic or a comment thread.
void chat_find(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  auto text = string_field(req, "query");
  if (text.is_error() || text.ok().empty()) {
    return bad_request(conn, id, "need the field query, a string that is not empty");
  }
  std::int32_t topic = -1;
  std::int64_t thread = 0;
  if (topic_thread_fields(conn, id, req, topic, thread)) {
    fetch_pins(std::make_shared<Pinned>(Pinned{conn, id, chat, topic, thread, text.move_as_ok(), {}}), 0);
  }
}

// ---- chat.mention, chat.reaction: the oldest unread mention or reply to us, or the oldest of our messages with unread
// reactions, read now (the chat page's @ and reactions buttons)

// Telegram reads a mention or a message's reactions only when that message is viewed (chat.read views the newest
// message), so the button views the one it goes to. Its row is written first (messages_queue), so the UI can jump to it
// after the reply.
// ponytail: the oldest of the newest 100; a chat with more reaches the older ones once these are read.
void chat_mention(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat, bool reactions) {
  std::int32_t topic = -1;
  if (!topic_field(conn, id, req, topic)) {
    return;
  }
  tdlib.send(make_object<td_api::searchChatMessages>(chat, topic_object(topic), "", nullptr, 0, 0, 100,
                                                     reactions ? td_api::object_ptr<td_api::SearchMessagesFilter>(
                                                                     make_object<td_api::searchMessagesFilterUnreadReaction>())
                                                               : make_object<td_api::searchMessagesFilterUnreadMention>()),
             [conn, id, chat](Obj o) {
               if (auto e = as_error(o)) {
                 say("td error: %d %s", e->code_, e->message_.c_str());
                 return reply_td_error(conn, id, *e);
               }
               const td_api::message *oldest = nullptr;
               for (const auto &m : static_cast<td_api::foundChatMessages &>(*o).messages_) {
                 if (m && (!oldest || m->id_ < oldest->id_)) {
                   oldest = m.get();
                 }
               }
               if (!oldest) {
                 return server.reply_ok(conn, id, R"({"id":""})");
               }
               const std::int64_t found = oldest->id_;
               messages_queue(*oldest);
               tdlib.send(make_object<td_api::viewMessages>(chat, std::vector<std::int64_t>{found}, nullptr, true),
                          [](Obj o) {
                            if (auto e = as_error(o)) {
                              say("chat.mention/reaction: view: %d %s", e->code_, e->message_.c_str());
                            }
                          });
               queue_reply(conn, id, td::json_encode<std::string>(td::json_object([&](auto &out) {
                             out("id", std::to_string(found));
                           })));
             });
}

// ---- chat.set: several calls, one reply

struct Several {
  std::uint64_t conn;
  std::string id;
  int left = 0;
  bool failed = false, bad_chat = false;  // the first failure
  int code = 0;
  std::string message;
};

void part_done(const std::shared_ptr<Several> &s, const td_api::error *e, bool lookup) {
  if (e) {
    say("td error: %d %s", e->code_, e->message_.c_str());
    if (!s->failed) {
      s->failed = true;
      s->bad_chat = lookup;
      s->code = e->code_;
      s->message = e->message_;
    }
  }
  if (--s->left > 0) {
    return;
  }
  if (!s->failed) {
    server.reply_ok(s->conn, s->id);
  } else if (s->bad_chat) {
    server.reply_error(s->conn, s->id, "bad_chat", s->message);
  } else {
    reply_td_error(s->conn, s->id, td_api::error(s->code, s->message));
  }
}

void chat_set(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  td::JsonValue pin, archive, unread, mute, translate;
  const int has_pin = optional_field(req, "pin", td::JsonValue::Type::Boolean, pin);
  const int has_archive = optional_field(req, "archive", td::JsonValue::Type::Boolean, archive);
  const int has_unread = optional_field(req, "unread", td::JsonValue::Type::Boolean, unread);
  const int has_mute = optional_field(req, "mute", td::JsonValue::Type::Number, mute);
  double until = 0;  // unix seconds; 0 off; -1 always
  if (has_mute == 1) {
    char *end = nullptr;
    const std::string number = mute.get_number().str();
    until = std::strtod(number.c_str(), &end);
    if (*end || !std::isfinite(until) || until != std::floor(until) || until < -1) {
      return bad_request(conn, id, "mute is unix seconds, 0 or -1");
    }
  }
  const int has_translate = optional_field(req, "translate", td::JsonValue::Type::String, translate);
  const std::string lang = has_translate == 1 ? translate.get_string().str() : std::string();
  if (has_translate == 1 && !lang.empty() && !language_code(lang)) {
    return bad_request(conn, id, "translate is a language code or an empty string");
  }
  if (has_pin < 0 || has_archive < 0 || has_unread < 0 || has_mute < 0 || has_translate < 0) {
    return bad_request(conn, id, "pin, archive and unread are true or false, mute is a number, translate a string");
  }
  if (has_pin + has_archive + has_unread + has_mute + has_translate == 0) {
    return bad_request(conn, id, "at least one of pin, mute, archive, unread, translate");
  }
  if (has_translate) {
    chats_set_translate(chat, lang);  // local: no TDLib call
  }
  if (has_pin + has_archive + has_unread + has_mute == 0) {
    return server.reply_ok(conn, id);
  }

  auto several = std::make_shared<Several>();
  several->conn = conn;
  several->id = id;
  several->left = has_pin + has_archive + has_unread + has_mute;  // each is one part, answered once
  auto part = [several](Obj o) {
    part_done(several, as_error(o), false);
  };
  if (has_pin) {
    tdlib.send(make_object<td_api::toggleChatIsPinned>(make_object<td_api::chatListMain>(), chat,
                                                      pin.get_boolean()),
               part);
  }
  if (has_archive) {
    td_api::object_ptr<td_api::ChatList> list;
    if (archive.get_boolean()) {
      list = make_object<td_api::chatListArchive>();
    } else {
      list = make_object<td_api::chatListMain>();
    }
    tdlib.send(make_object<td_api::addChatToList>(chat, std::move(list)), part);
  }
  if (has_unread) {
    tdlib.send(make_object<td_api::toggleChatIsMarkedAsUnread>(chat, unread.get_boolean()), part);
  }
  if (has_mute) {
    // The chat's other notification settings stay as they are: ask for them, change the mute, send them back.
    const std::int64_t at = until;
    tdlib.send(make_object<td_api::getChat>(chat), [several, chat, at](Obj o) {
      if (auto e = as_error(o)) {
        return part_done(several, e, true);
      }
      auto settings = std::move(static_cast<td_api::chat &>(*o).notification_settings_);
      if (!settings) {
        settings = make_object<td_api::chatNotificationSettings>();
      }
      settings->use_default_mute_for_ = false;
      if (at == -1) {
        settings->mute_for_ = 0x7fffffff;
      } else if (at == 0) {
        settings->mute_for_ = 0;
      } else {
        settings->mute_for_ = static_cast<std::int32_t>(std::max<std::int64_t>(1, std::min<std::int64_t>(
                                  at - std::time(nullptr), 0x7fffffff)));
      }
      tdlib.send(make_object<td_api::setChatNotificationSettings>(chat, std::move(settings)),
                 [several](Obj o2) { part_done(several, as_error(o2), false); });
    });
  }
}

// ---- msg.send, msg.edit, msg.delete

// A text exactly as given, with the entities it has. A draft's: its marks stay as typed, so the composer gets them back.
td_api::object_ptr<td_api::formattedText> plain_text(
    const std::string &text, std::vector<td_api::object_ptr<td_api::textEntity>> entities = {}) {
  return make_object<td_api::formattedText>(text, std::move(entities));
}

// The UTF-16 code units in the first `bytes` bytes of a UTF-8 string, which is how TDLib counts: a character of four
// bytes is two units, any other one.
std::int32_t utf16_units(const std::string &s, std::size_t bytes) {
  std::int32_t units = 0;
  for (std::size_t i = 0; i < bytes; ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if ((c & 0xC0) != 0x80) {  // not the middle of a character
      units += c >= 0xF0 ? 2 : 1;
    }
  }
  return units;
}

// One textEntityTypeMentionName for each "@name" of the mentions that the text holds (the first place that no
// earlier mention has), sorted by offset. A mention whose "@name" is not there, or whose jid is not a user id, is
// skipped.
std::vector<td_api::object_ptr<td_api::textEntity>> mention_entities(
    const std::string &text, const std::vector<std::pair<std::string, std::string>> &mentions) {
  struct Found {
    std::size_t at, size;
    std::int64_t user;
  };
  std::vector<Found> found;
  for (const auto &mention : mentions) {
    std::int64_t user = 0;
    if (mention.second.empty() || !parse_i64(mention.first, user) || user <= 0) {
      continue;
    }
    const std::string needle = "@" + mention.second;
    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
      const bool taken = std::any_of(found.begin(), found.end(), [&](const Found &f) {
        return at < f.at + f.size && f.at < at + needle.size();
      });
      if (!taken) {
        found.push_back({at, needle.size(), user});
        break;
      }
    }
  }
  std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) { return a.at < b.at; });
  std::vector<td_api::object_ptr<td_api::textEntity>> entities;
  for (const Found &f : found) {
    entities.push_back(make_object<td_api::textEntity>(utf16_units(text, f.at),
                                                       utf16_units(text.substr(f.at, f.size), f.size),
                                                       make_object<td_api::textEntityTypeMentionName>(f.user)));
  }
  return entities;
}

// A text as the UI typed it, for a message or a caption: Telegram's Markdown in it (**bold**, __italic__, ~~strike~~,
// ||spoiler||, `code`, ```pre``` and [text](url)) becomes entities, by TDLib's own parser (parseMarkdown runs only
// synchronously). It moves the entities given (the mentions) to their new places, and marks that don't pair stay text.
td_api::object_ptr<td_api::formattedText> typed_text(
    const std::string &text, std::vector<td_api::object_ptr<td_api::textEntity>> entities = {}) {
  Obj parsed = td::ClientManager::execute(make_object<td_api::parseMarkdown>(plain_text(text, std::move(entities))));
  if (parsed && parsed->get_id() == td_api::formattedText::ID) {
    return td_api::move_object_as<td_api::formattedText>(parsed);
  }
  return plain_text(text);
}

void msg_send(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  auto text = string_field(req, "text");
  if (text.is_error() || text.ok().empty()) {
    return bad_request(conn, id, "need the field text, a string that is not empty");
  }
  td_api::object_ptr<td_api::InputMessageReplyTo> reply;
  std::int32_t topic = -1;
  std::int64_t thread = 0;
  if (!quote_field(conn, id, req, reply) || !topic_thread_fields(conn, id, req, topic, thread)) {
    return;
  }
  std::vector<std::pair<std::string, std::string>> mentions;  // jid, name
  td::JsonValue list;
  const int has_mentions = optional_field(req, "mentions", td::JsonValue::Type::Array, list);
  if (has_mentions == 1) {
    for (td::JsonValue &item : list.get_array()) {
      if (item.type() != td::JsonValue::Type::Object) {
        return bad_request(conn, id, "mentions are objects with jid and name");
      }
      auto jid = string_field(item.get_object(), "jid");
      auto name = string_field(item.get_object(), "name");
      if (jid.is_error() || name.is_error()) {
        return bad_request(conn, id, "mentions are objects with jid and name, both strings");
      }
      mentions.emplace_back(jid.move_as_ok(), name.move_as_ok());
    }
  } else if (has_mentions < 0) {
    return bad_request(conn, id, "mentions is an array of objects with jid and name");
  }
  // local: the UI's name for the row it shows already ("local-3"); it comes back on our row as extra.local, so the UI puts this
  // message in that row's place exactly (two quick sends of the same words can't swap). Our own letters only: it goes into JSON.
  std::string local;
  td::JsonValue lv;
  const int has_local = optional_field(req, "local", td::JsonValue::Type::String, lv);
  if (has_local == 1) {
    local = lv.get_string().str();
    const bool ok = !local.empty() && local.size() <= 32 &&
                    std::all_of(local.begin(), local.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
    if (!ok) {
      return bad_request(conn, id, "local is a short name of a-z, 0-9 and -");
    }
  } else if (has_local < 0) {
    return bad_request(conn, id, "local is a string");
  }
  auto content = make_object<td_api::inputMessageText>(
      typed_text(text.ok(), mention_entities(text.ok(), mentions)), nullptr, true);
  send_content(conn, id, chat, topic, thread, std::move(reply), std::move(content), local);
}

// The optional field `name`, a whole number from 0 up, into `out` (which stays as it was when the field is
// missing). False after the bad_request reply.
bool size_field(std::uint64_t conn, const std::string &id, td::JsonObject &req, const char *name, std::int32_t &out) {
  td::JsonValue v;
  int given = optional_field(req, name, td::JsonValue::Type::Number, v);
  if (given == 1) {
    char *end = nullptr;
    const std::string number = v.get_number().str();
    const long n = std::strtol(number.c_str(), &end, 10);
    if (*end || n < 0) {
      given = -1;
    } else {
      out = static_cast<std::int32_t>(std::min<long>(n, INT32_MAX));
    }
  }
  if (given < 0) {
    bad_request(conn, id, std::string(name) + " is a whole number, 0 or more");
    return false;
  }
  return true;
}

// A big-endian number of n bytes.
std::uint64_t be(const unsigned char *p, int n) {
  std::uint64_t v = 0;
  for (int i = 0; i < n; ++i) {
    v = v << 8 | p[i];
  }
  return v;
}

// An MP4's (also 3GP's and MOV's) length in whole seconds, rounded, from moov's mvhd box; 0 when it can't be read. The
// UI sends none, and Telegram shows the video as 0:00 without it. moov may come after the media (mdat), so the boxes
// at the top are skipped by their sizes, not read.
int video_seconds(const std::string &path) {
  std::FILE *f = std::fopen(path.c_str(), "rb");
  if (!f) {
    return 0;
  }
  int seconds = 0;
  long end = -1;  // the end of the box being looked into: the file, then moov
  if (std::fseek(f, 0, SEEK_END) == 0) {
    end = std::ftell(f);
  }
  long at = 0;
  unsigned char h[32];
  while (end > 0 && at + 8 <= end && std::fseek(f, at, SEEK_SET) == 0 && std::fread(h, 1, 8, f) == 8) {
    std::uint64_t size = be(h, 4);
    long head = 8;
    if (size == 1) {  // the size follows as 64 bits
      if (std::fread(h + 8, 1, 8, f) != 8) {
        break;
      }
      size = be(h + 8, 8);
      head = 16;
    } else if (size == 0) {  // to the end
      size = static_cast<std::uint64_t>(end - at);
    }
    if (size < static_cast<std::uint64_t>(head) || at + static_cast<long>(size) > end) {
      break;
    }
    if (std::memcmp(h + 4, "moov", 4) == 0) {  // look inside it
      end = at + static_cast<long>(size);
      at += head;
      continue;
    }
    if (std::memcmp(h + 4, "mvhd", 4) == 0) {
      // version, flags; then the times, timescale and duration: 4 bytes each in version 0, 8 (but timescale) in 1
      unsigned char b[32];
      if (std::fseek(f, at + head, SEEK_SET) == 0 && std::fread(b, 1, sizeof b, f) == sizeof b) {
        const bool wide = b[0] == 1;
        const std::uint64_t scale = be(b + (wide ? 20 : 12), 4);
        const std::uint64_t length = be(b + (wide ? 24 : 16), wide ? 8 : 4);
        if (scale > 0 && length != (wide ? ~std::uint64_t{0} : 0xFFFFFFFFu)) {
          seconds = static_cast<int>((length + scale / 2) / scale);
        }
      }
      break;
    }
    at += static_cast<long>(size);
  }
  std::fclose(f);
  return seconds;
}

// The extension of a path in lower case, with its dot (".jpg"); "" when the file name has none.
std::string extension(const std::string &path) {
  const std::size_t dot = path.find_last_of("./");
  if (dot == std::string::npos || path[dot] == '/') {
    return "";
  }
  std::string ext = path.substr(dot);
  for (char &c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext;
}

// What Telegram takes of a file (without a Premium account), in bytes.
constexpr std::int64_t kMaxFile = std::int64_t{2000} << 20;

// The content of a message for the file at `path`: its kind comes from the extension, `document` (a picture as it is,
// not as a compressed photo) makes it a document whatever it is. Only a photo or a video carries `text`, a caption;
// "" is none. `thumb` (a base64 JPEG HyperBerry needed for WhatsApp) is not read: TDLib makes its own thumbnails.
td_api::object_ptr<td_api::InputMessageContent> file_content(const std::string &path, bool document, std::int32_t width,
                                                             std::int32_t height, const std::string &text) {
  auto caption = [&]() -> td_api::object_ptr<td_api::formattedText> {
    return text.empty() ? nullptr : typed_text(text);  // null is TDLib's empty caption
  };
  auto file = [&] { return make_object<td_api::inputFileLocal>(path); };

  static const char *const audio[] = {".mp3", ".m4a", ".aac", ".amr", ".ogg", ".wav"};
  const std::string ext = extension(path);
  if (document) {
    return make_object<td_api::inputMessageDocument>(make_object<td_api::inputDocument>(file(), nullptr, false),
                                                     nullptr);
  }
  if (ext == ".jpg" || ext == ".jpeg") {
    return make_object<td_api::inputMessagePhoto>(
        make_object<td_api::inputPhoto>(file(), nullptr, nullptr, std::vector<std::int32_t>(), width, height),
        caption(), false, nullptr, false);
  }
  if (ext == ".mp4" || ext == ".3gp" || ext == ".mov") {
    return make_object<td_api::inputMessageVideo>(
        make_object<td_api::inputVideo>(file(), nullptr, nullptr, 0, std::vector<std::int32_t>(),
                                        video_seconds(path), width, height, true),
        caption(), false, nullptr, false);
  }
  if (std::find(std::begin(audio), std::end(audio), ext) != std::end(audio)) {
    return make_object<td_api::inputMessageAudio>(make_object<td_api::inputAudio>(file(), nullptr, 0, "", ""),
                                                  nullptr);
  }
  return make_object<td_api::inputMessageDocument>(make_object<td_api::inputDocument>(file(), nullptr, false),
                                                   nullptr);
}

// media.send: one file, by file_content. Only a photo or a video carries a caption: the UI sends the text of any other
// kind as a msg.send of its own.
void media_send(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  auto path = string_field(req, "path");
  std::int64_t bytes = 0;
  if (path.is_error() || !regular_file(path.ok(), bytes) || bytes > kMaxFile) {
    return bad_request(conn, id, "need the field path, the absolute path of a file of at most 2000 MB");
  }
  td_api::object_ptr<td_api::InputMessageReplyTo> reply;
  std::int32_t topic = -1;
  std::int64_t thread = 0;
  if (!quote_field(conn, id, req, reply) || !topic_thread_fields(conn, id, req, topic, thread)) {
    return;
  }
  td::JsonValue v;
  const int has_caption = optional_field(req, "caption", td::JsonValue::Type::String, v);
  if (has_caption < 0) {
    return bad_request(conn, id, "caption is a string");
  }
  std::int32_t width = 0, height = 0;
  if (!size_field(conn, id, req, "width", width) || !size_field(conn, id, req, "height", height)) {
    return;
  }
  td::JsonValue as_file;
  const int has_file = optional_field(req, "file", td::JsonValue::Type::Boolean, as_file);
  if (has_file < 0) {
    return bad_request(conn, id, "file is true or false");
  }
  const bool document = has_file == 1 && as_file.get_boolean();  // a picture as it is, not as a compressed photo
  send_content(conn, id, chat, topic, thread, std::move(reply),
               file_content(path.ok(), document, width, height, has_caption == 1 ? v.get_string().str() : ""));
}

// media.album: 2 to 10 files (`items`, each {path, width, height, file} as media.send's) sent as one album. TDLib's
// rules decide what can go together (photos and videos mix, documents only with documents, audio only with audio): what
// it refuses comes back as the td error. The caption is the first item's, and only a photo or a video carries it. The
// rows are queued as send_content's are, and `{ids}` are theirs, in order.
void media_album(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  td::JsonValue list;
  if (optional_field(req, "items", td::JsonValue::Type::Array, list) != 1 || list.get_array().size() < 2 ||
      list.get_array().size() > 10) {
    return bad_request(conn, id, "need the field items, an array of 2 to 10 objects with a path");
  }
  td_api::object_ptr<td_api::InputMessageReplyTo> reply;
  std::int32_t topic = -1;
  std::int64_t thread = 0;
  if (!quote_field(conn, id, req, reply) || !topic_thread_fields(conn, id, req, topic, thread)) {
    return;
  }
  td::JsonValue v;
  const int has_caption = optional_field(req, "caption", td::JsonValue::Type::String, v);
  if (has_caption < 0) {
    return bad_request(conn, id, "caption is a string");
  }
  std::vector<td_api::object_ptr<td_api::InputMessageContent>> contents;
  for (td::JsonValue &item : list.get_array()) {
    if (item.type() != td::JsonValue::Type::Object) {
      return bad_request(conn, id, "items are objects with a path, and optionally width, height and file");
    }
    td::JsonObject &o = item.get_object();
    auto path = string_field(o, "path");
    std::int64_t bytes = 0;
    if (path.is_error() || !regular_file(path.ok(), bytes) || bytes > kMaxFile) {
      return bad_request(conn, id, "every item needs a path, the absolute path of a file of at most 2000 MB");
    }
    std::int32_t width = 0, height = 0;
    if (!size_field(conn, id, o, "width", width) || !size_field(conn, id, o, "height", height)) {
      return;
    }
    td::JsonValue as_file;
    const int has_file = optional_field(o, "file", td::JsonValue::Type::Boolean, as_file);
    if (has_file < 0) {
      return bad_request(conn, id, "file is true or false");
    }
    const bool document = has_file == 1 && as_file.get_boolean();
    const bool first = contents.empty();
    contents.push_back(file_content(path.ok(), document, width, height,
                                    first && has_caption == 1 ? v.get_string().str() : ""));
  }
  tdlib.send(make_object<td_api::sendMessageAlbum>(chat, topic_object(topic, thread), std::move(reply), nullptr,
                                                   std::move(contents)),
             [conn, id](Obj o) {
               if (auto e = as_error(o)) {
                 say("td error: %d %s", e->code_, e->message_.c_str());
                 return reply_td_error(conn, id, *e);
               }
               std::vector<std::string> ids;
               for (auto &m : static_cast<td_api::messages &>(*o).messages_) {
                 if (m) {
                   messages_queue(*m);  // the rows, state 0, under their temporary ids
                   ids.push_back(std::to_string(m->id_));
                 }
               }
               mark_dirty(true);  // just sent here: the bubbles show without the 50 ms wait
               queue_reply(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                             r("ids", td::json_array(ids, [](const std::string &s) { return s; }));
                           })));
             });
}

void msg_delete(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  td::JsonValue list, everyone;
  if (optional_field(req, "ids", td::JsonValue::Type::Array, list) != 1 ||
      optional_field(req, "everyone", td::JsonValue::Type::Boolean, everyone) != 1) {
    return bad_request(conn, id, "need ids, an array of message ids, and everyone, true or false");
  }
  std::vector<std::int64_t> ids;
  for (const td::JsonValue &item : list.get_array()) {
    std::int64_t n = 0;
    if (item.type() != td::JsonValue::Type::String || !parse_i64(item.get_string().str(), n) || n <= 0) {
      return bad_request(conn, id, "ids are strings with message ids");
    }
    ids.push_back(n);
  }
  if (ids.empty() || ids.size() > 100) {
    return bad_request(conn, id, "ids holds 1 to 100 message ids");
  }
  // The rows go when Telegram's updateDeleteMessages comes.
  tdlib.send(make_object<td_api::deleteMessages>(chat, std::move(ids), everyone.get_boolean()),
             [conn, id](Obj o) { answer(conn, id, o); });
}

void msg_edit(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t message = 0;
  if (!message_id(conn, id, req, "id", message)) {
    return;
  }
  auto text = string_field(req, "text");
  if (text.is_error() || text.ok().empty()) {
    return bad_request(conn, id, "need the field text, a string that is not empty");
  }
  // The row's kind decides how the message is edited; the row itself changes with TDLib's edit updates.
  const int kind = message_kind(chat, message);
  if (kind < 0) {
    return server.reply_error(conn, id, "not_found", "no such message");
  }
  auto done = [conn, id](Obj o) { answer(conn, id, o); };
  if (kind == 0) {
    auto content = make_object<td_api::inputMessageText>(typed_text(text.ok()), nullptr, false);
    tdlib.send(make_object<td_api::editMessageText>(chat, message, nullptr, std::move(content)), done);
  } else if (kind == 1 || kind == 2) {
    tdlib.send(make_object<td_api::editMessageCaption>(chat, message, nullptr, typed_text(text.ok()), false), done);
  } else {
    bad_request(conn, id, "only a text, a photo or a video can be edited");
  }
}

// `chat` is the message's chat, `to` the one it goes to. TDLib decides what can be forwarded and answers the new
// message (a null one for what it left out): its row is queued as send_content's is, and `{id}` is the first's.
// `topic` is the target's: the forum topic of `to` the message goes to.
void msg_forward(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t message = 0;
  if (!message_id(conn, id, req, "id", message)) {
    return;
  }
  auto text = string_field(req, "to");
  std::int64_t to = 0;
  if (text.is_error() || !parse_i64(text.ok(), to)) {
    return bad_request(conn, id, "need the field to, a string with a chat number");
  }
  std::int32_t topic = -1;
  if (!topic_field(conn, id, req, topic)) {
    return;
  }
  tdlib.send(make_object<td_api::forwardMessages>(to, topic_object(topic), chat, std::vector<std::int64_t>{message},
                                                  nullptr, false, false),
             [conn, id](Obj o) {
               if (auto e = as_error(o)) {
                 say("td error: %d %s", e->code_, e->message_.c_str());
                 return reply_td_error(conn, id, *e);
               }
               std::string first;
               for (auto &m : static_cast<td_api::messages &>(*o).messages_) {
                 if (m) {
                   messages_queue(*m);
                   if (first.empty()) {
                     first = std::to_string(m->id_);
                   }
                 }
               }
               mark_dirty(true);  // just sent here: the bubble shows without the 50 ms wait
               if (first.empty()) {
                 return reply_td_error(conn, id, td_api::error(400, "the message can't be forwarded"));
               }
               queue_reply(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) { r("id", first); })));
             });
}

// One emoji reaction on or off. The row changes with TDLib's updateMessageInteractionInfo (messages.cpp), so the
// reply is TDLib's. Adding replaces our old reaction when the account allows only one.
// ponytail: no "emoji is empty: take all ours back" (setMessageReactions is for bots only); the UI removes the
// emoji it shows as ours. A premium account's custom emoji and the paid reaction are not handled.
void msg_react(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t message = 0;
  if (!message_id(conn, id, req, "id", message)) {
    return;
  }
  auto emoji = string_field(req, "emoji");
  td::JsonValue remove;
  if (emoji.is_error() || emoji.ok().empty() || optional_field(req, "remove", td::JsonValue::Type::Boolean, remove) < 0) {
    return bad_request(conn, id, "need the field emoji, a string that is not empty; remove is true or false");
  }
  // Telegram's reactions carry no emoji selector (U+FE0F): the heart is "❤", and TDLib compares the string exactly, so the
  // keyboard's and the picker's "❤️" was refused ("The reaction isn't available for the message", the user, 2026-11-22).
  std::string plain = emoji.ok();
  for (size_t at; (at = plain.find("\xEF\xB8\x8F")) != std::string::npos;) {
    plain.erase(at, 3);
  }
  if (plain.empty()) {
    return bad_request(conn, id, "need the field emoji, a string that is not empty; remove is true or false");
  }
  auto type = make_object<td_api::reactionTypeEmoji>(plain);
  auto done = [conn, id](Obj o) { answer(conn, id, o); };
  if (remove.type() == td::JsonValue::Type::Boolean && remove.get_boolean()) {
    tdlib.send(make_object<td_api::removeMessageReaction>(chat, message, std::move(type)), done);
  } else {
    tdlib.send(make_object<td_api::addMessageReaction>(chat, message, std::move(type), false, true), done);
  }
}

// The emoji reactions a message allows (getMessageAvailableReactions): the UI's bar shows only these, because TDLib
// refuses the rest ("The reaction isn't available for the message", the user, 2026-11-22). `top` is what Telegram's own bar
// shows, in order; `all` is the top, then the recent, then the popular ones, each emoji once. Both are empty when the
// message takes no reactions (off in the chat, a restricted group member, a guest of a supergroup).
// ponytail: premium-only and custom-emoji reactions are left out; a Premium account would see them in Telegram's own apps.
void msg_reactions(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t message = 0;
  if (!message_id(conn, id, req, "id", message)) {
    return;
  }
  tdlib.send(make_object<td_api::getMessageAvailableReactions>(chat, message, 8), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      say("td error: %d %s", e->code_, e->message_.c_str());
      return reply_td_error(conn, id, *e);
    }
    auto &found = static_cast<td_api::availableReactions &>(*o);
    std::vector<std::string> top, all;
    const auto add = [&all](const std::vector<td_api::object_ptr<td_api::availableReaction>> &list,
                            std::vector<std::string> *first) {
      for (const auto &r : list) {
        if (!r || !r->type_ || r->type_->get_id() != td_api::reactionTypeEmoji::ID || r->needs_premium_) {
          continue;
        }
        const std::string &emoji = static_cast<const td_api::reactionTypeEmoji &>(*r->type_).emoji_;
        if (first) {
          first->push_back(emoji);
        }
        if (std::find(all.begin(), all.end(), emoji) == all.end()) {
          all.push_back(emoji);
        }
      }
    };
    add(found.top_reactions_, &top);
    add(found.recent_reactions_, nullptr);
    add(found.popular_reactions_, nullptr);
    server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                      r("top", td::json_array(top, [](const std::string &s) { return s; }));
                      r("all", td::json_array(all, [](const std::string &s) { return s; }));
                    })));
  });
}

// msg.thread: the comments of a channel post (getMessageThread). The thread is in the channel's discussion group: the
// answer names that group and the thread's root there, which chat.open, chat.history, chat.newer, chat.read, chat.typing
// and the senders take as `chat` and `thread`. `count` is the comments there are (0 when TDLib has no reply info yet).
void msg_thread(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t message = 0;
  if (!message_id(conn, id, req, "id", message)) {
    return;
  }
  tdlib.send(make_object<td_api::getMessageThread>(chat, message), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      say("td error: %d %s", e->code_, e->message_.c_str());
      return reply_td_error(conn, id, *e);
    }
    auto &info = static_cast<td_api::messageThreadInfo &>(*o);
    server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                      r("chat", std::to_string(info.chat_id_));
                      r("thread", std::to_string(info.message_thread_id_));
                      r("name", chat_title(info.chat_id_));
                      r("count", info.reply_info_ ? info.reply_info_->reply_count_ : 0);
                    })));
  });
}

// chat.read of a comment thread: the thread's messages are viewed as the thread's history (Telegram counts the thread's
// unread comments by that), and the group's own read marker is left alone. Up to `upto`, or up to the thread's newest
// comment when it is 0; a thread with no comments has nothing to view.
void thread_read(std::int64_t chat, std::int64_t thread, std::int64_t upto,
                 std::function<void(const td_api::error *)> done) {
  const auto view = [chat, done](std::int64_t message) {
    tdlib.send(make_object<td_api::viewMessages>(chat, std::vector<std::int64_t>{message},
                                                 make_object<td_api::messageSourceMessageThreadHistory>(), true),
               [done](Obj o) { done(as_error(o)); });
  };
  if (upto > 0) {
    return view(upto);
  }
  tdlib.send(make_object<td_api::getMessageThread>(chat, thread), [done, view](Obj o) {
    if (auto e = as_error(o)) {
      return done(e);
    }
    const auto &info = static_cast<td_api::messageThreadInfo &>(*o);
    const std::int64_t last = info.reply_info_ ? info.reply_info_->last_message_id_ : 0;
    if (last <= 0) {
      return done(nullptr);
    }
    view(last);
  });
}

// Our answer to a poll: option positions (0-based, as extra.options lists them); none takes the vote back. TDLib
// refuses what the poll does not allow (several answers, revoting, a closed poll). The row changes with the
// updateMessageContent that follows.
void poll_vote(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t message = 0;
  if (!message_id(conn, id, req, "id", message)) {
    return;
  }
  auto list = req.extract_optional_field(td::Slice("options"), td::JsonValue::Type::Null);  // any type, null too
  std::vector<std::int32_t> picked;
  if (list.is_ok() && list.ok().type() == td::JsonValue::Type::Array) {
    for (const td::JsonValue &item : list.ok().get_array()) {
      std::int64_t n = 0;
      if (item.type() != td::JsonValue::Type::Number || !parse_i64(item.get_number().str(), n) || n < 0 || n > 1000) {
        return bad_request(conn, id, "options are the numbers of the options, 0 or more");
      }
      picked.push_back(static_cast<std::int32_t>(n));
    }
  } else if (list.is_error() || list.ok().type() != td::JsonValue::Type::Null) {
    return bad_request(conn, id, "options is an array of option numbers, or missing to take the vote back");
  }
  tdlib.send(make_object<td_api::setPollAnswer>(chat, message, std::move(picked)),
             [conn, id](Obj o) { answer(conn, id, o); });
}

// ---- media.get

// The files a media.get (not a preview) is downloading, for media.progress: the message each belongs to and when its
// last event went. And the files media.cancel stopped, for the media.get still waiting on each: it answers `cancelled`.
struct Watched {
  std::int64_t chat, message;
  std::chrono::steady_clock::time_point sent;
};
std::unordered_map<std::int32_t, Watched> watched;
std::unordered_set<std::int32_t> cancelled;
// The files of our messages still sending (upload_watch), for media.progress: the sending message and its last event.
std::unordered_map<std::int32_t, Watched> uploads;

void download_failed(std::uint64_t conn, const std::string &id, const std::string &why) {
  say("media.get: %s", why.c_str());
  server.reply_error(conn, id, "download_failed", why);
}

// media.get with save: the file's copy in the phone's Downloads, or why there is none.
void reply_saved(std::uint64_t conn, const std::string &id, const td_api::message &m, const std::string &path) {
  const std::string to = downloads_copy(m, path);
  if (to.empty()) {
    return server.reply_error(conn, id, "not_saved", "this file can't be saved to Downloads");
  }
  queue_reply(conn, id, path_json(to));
}

void media_get(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t message = 0;
  if (!message_id(conn, id, req, "id", message)) {
    return;
  }
  // preview: the picture a bubble shows before the file is opened (message_preview_file) instead of the file itself.
  td::JsonValue want;
  const int has_preview = optional_field(req, "preview", td::JsonValue::Type::Boolean, want);
  if (has_preview < 0) {
    return bad_request(conn, id, "preview is true or false");
  }
  const bool preview = has_preview == 1 && want.get_boolean();
  // save: also copy a document or audio file to the phone's Downloads, and answer with the copy (the menu's Download).
  const int has_save = optional_field(req, "save", td::JsonValue::Type::Boolean, want);
  if (has_save < 0) {
    return bad_request(conn, id, "save is true or false");
  }
  const bool save = !preview && has_save == 1 && want.get_boolean();
  tdlib.send(make_object<td_api::getMessage>(chat, message), [=](Obj o) {
    if (as_error(o)) {
      return server.reply_error(conn, id, "not_found", "no such message");
    }
    auto &m = static_cast<td_api::message &>(*o);
    // A tap on a self-destructing photo or video of theirs is its opening: TDLib starts the countdown only then
    // (updateMessageContentOpened rewrites the row with extra.expiresAt). A preview isn't an opening.
    if (!preview && m.self_destruct_type_ && !m.is_outgoing_) {
      tdlib.send(make_object<td_api::openMessageContent>(chat, message));
    }
    if (preview && m.content_ &&
        (m.content_->get_id() == td_api::messageLocation::ID || m.content_->get_id() == td_api::messageVenue::ID ||
         m.content_->get_id() == td_api::messageLiveLocation::ID)) {
      return location_map(conn, id, m);  // a map picture, not a file of the message
    }
    const td_api::file *file = preview ? message_preview_file(m) : message_file(m);
    if (!file) {
      return server.reply_error(conn, id, "not_found",
                                preview ? "the message has no preview picture" : "the message has no file");
    }
    if (file->local_ && file->local_->is_downloading_completed_) {
      messages_queue(m);  // its row may still say the file is on the server (or have no extra.preview)
      if (save) {
        return reply_saved(conn, id, m, file->local_->path_);
      }
      return queue_reply(conn, id, path_json(file->local_->path_));
    }
    // Synchronous: the answer is the file once the download is over. Another media.get for the same message
    // may be waiting on its own downloadFile at the same time: nothing here is shared between them.
    // A preview is asked for every picture a page shows: below a tap's priority, so the photo opened comes first.
    const std::int32_t file_id = file->id_;
    if (!preview) {
      cancelled.erase(file_id);
      watched[file_id] = {chat, message, {}};
    }
    tdlib.send(make_object<td_api::downloadFile>(file_id, preview ? 8 : 16, 0, 0, true), [=](Obj o2) {
      watched.erase(file_id);
      auto failed = [=](const std::string &why) {
        if (cancelled.erase(file_id)) {
          return server.reply_error(conn, id, "cancelled", "the download was cancelled");
        }
        download_failed(conn, id, why);
      };
      if (auto e = as_error(o2)) {
        return failed(std::to_string(e->code_) + " " + e->message_);
      }
      const auto &got = static_cast<const td_api::file &>(*o2);
      if (!got.local_ || !got.local_->is_downloading_completed_) {
        return failed("the download did not finish");
      }
      const std::string path = got.local_->path_;
      tdlib.send(make_object<td_api::getMessage>(chat, message), [=](Obj o3) {
        if (!as_error(o3)) {
          auto &m = static_cast<td_api::message &>(*o3);
          messages_queue(m);  // now with media_state 2 and the path, or extra.preview
          if (!preview) {
            gallery_save(m, path);
          }
          if (save) {
            return reply_saved(conn, id, m, path);
          }
        } else if (save) {
          return server.reply_error(conn, id, "not_saved", "this file can't be saved to Downloads");
        }
        queue_reply(conn, id, path_json(path));
      });
    });
  });
}

void media_cancel(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t message = 0;
  if (!message_id(conn, id, req, "id", message)) {
    return;
  }
  tdlib.send(make_object<td_api::getMessage>(chat, message), [=](Obj o) {
    if (as_error(o)) {
      return server.reply_error(conn, id, "not_found", "no such message");
    }
    const td_api::file *file = message_file(static_cast<td_api::message &>(*o));
    if (!file) {
      return server.reply_error(conn, id, "not_found", "the message has no file");
    }
    if (watched.erase(file->id_)) {  // a media.get is waiting on it
      cancelled.insert(file->id_);
    }
    tdlib.send(make_object<td_api::cancelDownloadFile>(file->id_, false), [conn, id](Obj o2) { answer(conn, id, o2); });
  });
}

}  // namespace

void media_progress(const td_api::file &f) {
  const auto now = std::chrono::steady_clock::now();
  const std::int64_t size = f.size_ ? f.size_ : f.expected_size_;
  auto event = [&](const Watched &w, std::int64_t done, bool up) {
    server.broadcast(td::json_encode<std::string>(td::json_object([&](auto &o) {
      o("ev", "media.progress");
      o("chat", std::to_string(w.chat));
      o("id", std::to_string(w.message));
      o("done", done);
      o("size", size);
      if (up) {
        o("up", td::JsonBool(true));
      }
    })));
  };
  auto w = watched.find(f.id_);
  if (w != watched.end() && f.local_ && !f.local_->is_downloading_completed_ &&
      now - w->second.sent >= std::chrono::milliseconds(200)) {
    w->second.sent = now;
    event(w->second, f.local_->downloaded_size_, false);
  }
  auto u = uploads.find(f.id_);
  if (u == uploads.end() || !f.remote_ || size == 0) {
    return;
  }
  if (f.remote_->is_uploading_completed_) {  // the last event is never throttled: the bar ends full
    event(u->second, size, true);
    uploads.erase(u);
  } else if (now - u->second.sent >= std::chrono::milliseconds(200)) {
    u->second.sent = now;
    event(u->second, f.remote_->uploaded_size_, true);
  }
}

void upload_watch(const td_api::message &m) {
  if (!m.sending_state_ || m.sending_state_->get_id() != td_api::messageSendingStatePending::ID) {
    return;
  }
  const td_api::file *file = message_file(m);
  if (file && file->remote_ && !file->remote_->is_uploading_completed_) {
    uploads[file->id_] = {m.chat_id_, m.id_, {}};
  }
}

void upload_forget(std::int64_t chat, std::int64_t message) {
  for (auto u = uploads.begin(); u != uploads.end();) {
    u = u->second.chat == chat && u->second.message == message ? uploads.erase(u) : std::next(u);
  }
}

void bad_request(std::uint64_t conn, const std::string &id, const std::string &what) {
  server.reply_error(conn, id, "bad_request", what);
}

// A message id in a string field of the request. False after the bad_request reply.
bool message_id(std::uint64_t conn, const std::string &id, td::JsonObject &req, const char *name, std::int64_t &out) {
  auto text = string_field(req, name);
  if (text.is_error() || !parse_i64(text.ok(), out) || out <= 0) {
    bad_request(conn, id, std::string("need the field ") + name + ", a string with a message id");
    return false;
  }
  return true;
}

// The optional field "quote", a message id in a string: `reply` becomes the message it replies to, or stays null.
// False after the bad_request reply.
bool quote_field(std::uint64_t conn, const std::string &id, td::JsonObject &req,
                 td_api::object_ptr<td_api::InputMessageReplyTo> &reply) {
  std::int64_t quote = 0;
  td::JsonValue v;
  const int has_quote = optional_field(req, "quote", td::JsonValue::Type::String, v);
  if (has_quote < 0 || (has_quote == 1 && (!parse_i64(v.get_string().str(), quote) || quote <= 0))) {
    bad_request(conn, id, "quote is a string with a message id");
    return false;
  }
  if (has_quote == 1) {
    reply = make_object<td_api::inputMessageReplyToMessage>(quote, nullptr, 0, "");
  }
  return true;
}

// Sends the content as a new message of the chat. TDLib's answer is the message: its row is queued, state 0 under
// a temporary id, and `{id}` is that id.
void send_content(std::uint64_t conn, const std::string &id, std::int64_t chat, std::int32_t topic, std::int64_t thread,
                  td_api::object_ptr<td_api::InputMessageReplyTo> reply,
                  td_api::object_ptr<td_api::InputMessageContent> content,
                  const std::string &local) {
  tdlib.send(make_object<td_api::sendMessage>(chat, topic_object(topic, thread), std::move(reply), nullptr, nullptr,
                                              std::move(content)),
             [conn, id, local](Obj o) {
               if (auto e = as_error(o)) {
                 say("td error: %d %s", e->code_, e->message_.c_str());
                 return reply_td_error(conn, id, *e);
               }
               auto &m = static_cast<td_api::message &>(*o);
               messages_queue(m, local);  // the row, state 0, under its temporary id, with the UI's name for it
               mark_dirty(true);   // written now, not with the next 50 ms batch: the user waits for this bubble
               queue_reply(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                             r("id", std::to_string(m.id_));
                           })));
             });
}

bool topic_field(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int32_t &topic) {
  topic = -1;
  td::JsonValue v;
  const int given = optional_field(req, "topic", td::JsonValue::Type::Number, v);
  if (given == 0) {
    return true;
  }
  long n = 0;
  if (given == 1) {
    char *end = nullptr;
    const std::string number = v.get_number().str();
    n = std::strtol(number.c_str(), &end, 10);
    if (*end) {
      n = 0;
    }
  }
  if (n < 1 || n > INT32_MAX) {
    bad_request(conn, id, "topic is a whole number, 1 or more");
    return false;
  }
  topic = static_cast<std::int32_t>(n);
  return true;
}

bool thread_field(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t &thread) {
  thread = 0;
  td::JsonValue v;
  const int given = optional_field(req, "thread", td::JsonValue::Type::String, v);
  if (given < 0 || (given == 1 && (!parse_i64(v.get_string().str(), thread) || thread <= 0))) {
    thread = 0;
    bad_request(conn, id, "thread is a string with a message id");
    return false;
  }
  return true;
}

bool topic_thread_fields(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int32_t &topic,
                         std::int64_t &thread) {
  if (!topic_field(conn, id, req, topic) || !thread_field(conn, id, req, thread)) {
    return false;
  }
  if (topic > 0 && thread > 0) {
    bad_request(conn, id, "topic and thread can't both be given");
    return false;
  }
  return true;
}

td_api::object_ptr<td_api::MessageTopic> topic_object(std::int32_t topic, std::int64_t thread) {
  if (thread > 0) {
    return make_object<td_api::messageTopicThread>(thread);
  }
  if (topic < 1) {
    return nullptr;
  }
  return make_object<td_api::messageTopicForum>(topic);
}

std::string path_json(const std::string &path) {
  return td::json_encode<std::string>(td::json_object([&](auto &o) { o("path", path); }));
}

// A whole decimal int64, as the keys travel: "-1001234567890".
bool parse_i64(const std::string &s, std::int64_t &out) {
  std::size_t digits = s.size() && s[0] == '-' ? 1 : 0;
  if (digits == s.size() || s.size() > 20) {
    return false;
  }
  for (std::size_t i = digits; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') {
      return false;
    }
  }
  errno = 0;
  const long long n = std::strtoll(s.c_str(), nullptr, 10);
  out = n;
  return errno == 0;
}

// The common start: the account is logged in and "chat" is a string holding an int64. False after the reply.
bool request_chat(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t &chat) {
  if (!link_ready()) {
    server.reply_error(conn, id, "unlinked", "not logged in");
    return false;
  }
  auto text = string_field(req, "chat");
  if (text.is_error() || !parse_i64(text.ok(), chat)) {
    bad_request(conn, id, "need the field chat, a string with a number");
    return false;
  }
  return true;
}

bool regular_file(const std::string &path, std::int64_t &size) {
  struct stat st;
  if (path.empty() || path[0] != '/' || stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
    return false;
  }
  size = st.st_size;
  return true;
}

// ---- draft.get and draft.set: Telegram's draft of a chat, a forum topic or a comment thread, kept for every client

// {text, reply?}: reply is the message the draft replies to as the reply bar shows it ({id, out, sender, text}), when it
// is in this chat and TDLib still has it.
void draft_answer(std::uint64_t conn, const std::string &id, std::int64_t chat, const td_api::draftMessage *d) {
  const std::string text = draft_text(d);
  std::int64_t quote = 0;
  if (!text.empty() && d->reply_to_ && d->reply_to_->get_id() == td_api::inputMessageReplyToMessage::ID) {
    quote = static_cast<const td_api::inputMessageReplyToMessage &>(*d->reply_to_).message_id_;
  }
  auto send = [conn, id, text](const td_api::message *m) {
    server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                      r("text", text);
                      if (m) {
                        bool service = false;
                        const std::string body = one_line(message_body(*m, service));
                        r("reply", td::json_object([&](auto &q) {
                            q("id", std::to_string(m->id_));
                            q("out", td::JsonBool(m->is_outgoing_));
                            q("sender", sender_name(*m));
                            q("text", body);
                          }));
                      }
                    })));
  };
  if (quote == 0) {
    return send(nullptr);
  }
  tdlib.send(make_object<td_api::getMessage>(chat, quote), [send](Obj o) {
    send(as_error(o) ? nullptr : static_cast<const td_api::message *>(o.get()));
  });
}

void draft_get(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int32_t topic = -1;
  std::int64_t thread = 0;
  if (!topic_thread_fields(conn, id, req, topic, thread)) {
    return;
  }
  auto got = [conn, id, chat, topic, thread](Obj o) {
    if (auto e = as_error(o)) {
      say("td error: %d %s", e->code_, e->message_.c_str());
      return reply_td_error(conn, id, *e);
    }
    const td_api::draftMessage *d = thread > 0  ? static_cast<td_api::messageThreadInfo &>(*o).draft_message_.get()
                                    : topic > 0 ? static_cast<td_api::forumTopic &>(*o).draft_message_.get()
                                                : static_cast<td_api::chat &>(*o).draft_message_.get();
    draft_answer(conn, id, chat, d);
  };
  if (thread > 0) {
    tdlib.send(make_object<td_api::getMessageThread>(chat, thread), got);
  } else if (topic > 0) {
    tdlib.send(make_object<td_api::getForumTopic>(chat, topic), got);
  } else {
    tdlib.send(make_object<td_api::getChat>(chat), got);
  }
}

// text '' removes the draft; quote, a message id, is the message it replies to. Plain text: formatting isn't kept.
void draft_set(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  auto text = string_field(req, "text");
  if (text.is_error()) {
    return bad_request(conn, id, "need the field text, a string ('' removes the draft)");
  }
  td_api::object_ptr<td_api::InputMessageReplyTo> reply;
  std::int32_t topic = -1;
  std::int64_t thread = 0;
  if (!quote_field(conn, id, req, reply) || !topic_thread_fields(conn, id, req, topic, thread)) {
    return;
  }
  td_api::object_ptr<td_api::draftMessage> draft;
  if (!text.ok().empty()) {
    draft = make_object<td_api::draftMessage>(
        std::move(reply), 0, make_object<td_api::draftMessageContentText>(plain_text(text.ok()), nullptr), 0, nullptr);
  }
  tdlib.send(make_object<td_api::setChatDraftMessage>(chat, topic_object(topic, thread), std::move(draft)),
             [conn, id](Obj o) { answer(conn, id, o); });
}

bool requests_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  static const char *const ours[] = {"chat.open", "chat.history", "chat.newer", "chat.pins", "chat.find", "chat.close",
                                     "chat.read", "chat.typing", "chat.set", "draft.get", "draft.set", "msg.send", "media.send", "media.album", "msg.delete",
                                     "msg.edit", "msg.forward", "msg.react", "msg.reactions", "msg.thread", "poll.vote",
                                     "media.get", "media.cancel", "chat.mention", "chat.reaction"};
  if (std::find(std::begin(ours), std::end(ours), cmd) == std::end(ours)) {
    return false;
  }
  std::int64_t chat = 0;
  if (!request_chat(conn, id, req, chat)) {
    return true;
  }

  if (cmd == "chat.open") {
    long limit = 0;
    std::int32_t topic = -1;
    std::int64_t thread = 0;
    if (parse_limit(conn, id, req, 200, limit) && topic_thread_fields(conn, id, req, topic, thread)) {
      chat_open(conn, id, chat, static_cast<std::size_t>(limit), topic, thread);
    }
  } else if (cmd == "chat.history") {
    chat_history(conn, id, req, chat);
  } else if (cmd == "chat.newer") {
    chat_newer(conn, id, req, chat);
  } else if (cmd == "chat.pins") {
    chat_pins(conn, id, req, chat);
  } else if (cmd == "chat.find") {
    chat_find(conn, id, req, chat);
  } else if (cmd == "chat.mention" || cmd == "chat.reaction") {
    chat_mention(conn, id, req, chat, cmd == "chat.reaction");
  } else if (cmd == "chat.close") {
    live_close(conn, chat);
    tdlib.send(make_object<td_api::closeChat>(chat), [conn, id](Obj) { server.reply_ok(conn, id); });
  } else if (cmd == "chat.read") {
    std::int64_t upto = 0;  // 0: up to the chat's newest message
    td::JsonValue v;
    const int has_upto = optional_field(req, "upto", td::JsonValue::Type::String, v);
    if (has_upto < 0 || (has_upto == 1 && (!parse_i64(v.get_string().str(), upto) || upto <= 0))) {
      bad_request(conn, id, "upto is a string with a message id");
      return true;
    }
    std::int32_t topic = -1;
    std::int64_t thread = 0;
    if (!topic_thread_fields(conn, id, req, topic, thread)) {
      return true;
    }
    auto done = [conn, id](const td_api::error *e) {
      if (e) {
        say("td error: %d %s", e->code_, e->message_.c_str());
        reply_td_error(conn, id, *e);
      } else {
        server.reply_ok(conn, id);
      }
    };
    if (thread > 0) {
      thread_read(chat, thread, upto, done);
      return true;
    }
    const bool known = topic > 0 ? topics_read(chat, topic, done, upto) : chats_read(chat, done, upto);
    if (!known) {
      server.reply_error(conn, id, "bad_chat", "no such chat");
    }
  } else if (cmd == "chat.typing") {
    td::JsonValue on;
    if (optional_field(req, "on", td::JsonValue::Type::Boolean, on) != 1) {
      bad_request(conn, id, "need the field on, true or false");
      return true;
    }
    std::int32_t topic = -1;
    std::int64_t thread = 0;
    if (!topic_thread_fields(conn, id, req, topic, thread)) {
      return true;
    }
    td_api::object_ptr<td_api::ChatAction> action;
    if (on.get_boolean()) {
      action = make_object<td_api::chatActionTyping>();
    } else {
      action = make_object<td_api::chatActionCancel>();
    }
    tdlib.send(make_object<td_api::sendChatAction>(chat, topic_object(topic, thread), "", std::move(action)),
               [conn, id](Obj o) { answer(conn, id, o); });
  } else if (cmd == "chat.set") {
    chat_set(conn, id, req, chat);
  } else if (cmd == "draft.get") {
    draft_get(conn, id, req, chat);
  } else if (cmd == "draft.set") {
    draft_set(conn, id, req, chat);
  } else if (cmd == "msg.send") {
    msg_send(conn, id, req, chat);
  } else if (cmd == "media.send") {
    media_send(conn, id, req, chat);
  } else if (cmd == "media.album") {
    media_album(conn, id, req, chat);
  } else if (cmd == "msg.delete") {
    msg_delete(conn, id, req, chat);
  } else if (cmd == "msg.edit") {
    msg_edit(conn, id, req, chat);
  } else if (cmd == "msg.forward") {
    msg_forward(conn, id, req, chat);
  } else if (cmd == "msg.react") {
    msg_react(conn, id, req, chat);
  } else if (cmd == "msg.reactions") {
    msg_reactions(conn, id, req, chat);
  } else if (cmd == "msg.thread") {
    msg_thread(conn, id, req, chat);
  } else if (cmd == "poll.vote") {
    poll_vote(conn, id, req, chat);
  } else if (cmd == "media.cancel") {
    media_cancel(conn, id, req, chat);
  } else {
    media_get(conn, id, req, chat);
  }
  return true;
}
