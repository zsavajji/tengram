// The requests about chats and messages: chat.open, chat.history, chat.newer, chat.pins, chat.close, chat.read,
// chat.typing, chat.set, msg.send, media.send, msg.delete, msg.edit, msg.forward, media.get and media.cancel
// (docs/protocol.md). The rows they ask for are written by messages.cpp; the replies that tell the UI to read them
// come after the flush that commits them (queue_reply).

#include "engine.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
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
  std::int64_t upper;  // the table's rows from the oldest message up to here are the page's to reconcile
  std::vector<td_api::object_ptr<td_api::message>> got;  // newest first
  bool more = true;
  int calls = 0;
  bool newer = false;  // chat.newer: upper is the newest message's id + 1, and `more` asks about newer messages
  std::int32_t topic = -1;  // a forum topic's page (its history, its rows reconciled), or -1 for the chat's
  std::int64_t last = 0;    // chat.newer of a topic: the topic's newest message id when the request began
};

// The newest message the engine knows of in the page's chat, or topic; 0 when there is none.
std::int64_t newest_known(const Page &p) {
  ChatBrief brief = {};
  if (p.topic > 0) {
    return p.last;
  }
  return chat_brief(p.chat, brief) && brief.has_last ? brief.last_id : 0;
}

// The history call of the page. A topic's comes from Telegram every time: TDLib keeps no local list of it.
td_api::object_ptr<td_api::Function> history_call(const Page &p, std::int64_t from, int offset, int limit) {
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
    upper = newest + 1;
  }
  messages_reconcile(p->chat, p->topic, p->got, oldest, upper);
  queue_reply(p->conn, p->id, page_json(std::to_string(oldest), std::to_string(newest), p->got.size(), p->more));
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
// end of the page: for a topic it is the topic's newest message (Page::last), fetched when the request began.
void fetch_newer(const std::shared_ptr<Page> &p, std::int64_t from) {
  if (from >= newest_known(*p)) {
    return finish(p);
  }
  // offset -k with limit k: TDLib raises limit to k + 1, starts k newer than `from` and drops `from` itself, so
  // the answer is the k messages right above it, newest first (fewer near the newest; then `from` and older ones
  // can come back, which the filter drops).
  // A topic's history cuts its slice differently: with offset -k and limit k it answers `from` and the k - 1 messages
  // above it (and for k = 1 nothing but `from`). So a topic is asked for offset -(k + 1) and limit k + 1, which gives
  // `from` and k above it: -offset must stay under 100, so its k is at most 98.
  const int extra = p->topic > 0 ? 1 : 0;
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

void chat_open(std::uint64_t conn, const std::string &id, std::int64_t chat, std::size_t want, std::int32_t topic) {
  tdlib.send(make_object<td_api::getChat>(chat), [=](Obj o) {
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
  if (!parse_limit(conn, id, req, 100, limit) || !topic_field(conn, id, req, topic)) {
    return;
  }
  auto page = std::make_shared<Page>(Page{conn, id, chat, static_cast<std::size_t>(limit), before, {}});
  page->topic = topic;
  fetch(page, before);
}

void chat_newer(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t after = 0;
  long limit = 0;
  std::int32_t topic = -1;
  if (!message_id(conn, id, req, "after", after) || !parse_limit(conn, id, req, 100, limit) ||
      !topic_field(conn, id, req, topic)) {
    return;
  }
  ChatBrief brief = {};
  if (!chat_brief(chat, brief)) {
    return server.reply_error(conn, id, "bad_chat", "no such chat");
  }
  auto page = std::make_shared<Page>(Page{conn, id, chat, static_cast<std::size_t>(limit), 0, {}});
  page->newer = true;
  page->topic = topic;
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

// ---- chat.pins: the pinned messages, newest message first

struct Pinned {
  std::uint64_t conn;
  std::string id;
  std::int64_t chat;
  std::int32_t topic;
  std::vector<std::int64_t> found;
  int calls = 0;
};

// Telegram's own list: a pin never expires, and unpinning leaves no message, so it is asked for, not kept.
// Every message goes through messages_queue: the UI reads its row for the preview and the jump.
void fetch_pins(const std::shared_ptr<Pinned> &p, std::int64_t from) {
  tdlib.send(make_object<td_api::searchChatMessages>(p->chat, topic_object(p->topic), "", nullptr, from, 0, 100,
                                                     make_object<td_api::searchMessagesFilterPinned>()),
             [p](Obj o) {
               if (auto e = as_error(o)) {
                 say("td error: %d %s", e->code_, e->message_.c_str());
                 return reply_td_error(p->conn, p->id, *e);
               }
               auto &r = static_cast<td_api::foundChatMessages &>(*o);
               for (auto &m : r.messages_) {
                 if (m) {
                   messages_queue(*m);
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
    fetch_pins(std::make_shared<Pinned>(Pinned{conn, id, chat, topic, {}}), 0);
  }
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
  td::JsonValue pin, archive, unread, mute;
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
  if (has_pin < 0 || has_archive < 0 || has_unread < 0 || has_mute < 0) {
    return bad_request(conn, id, "pin, archive and unread are true or false, mute is a number");
  }
  if (has_pin + has_archive + has_unread + has_mute == 0) {
    return bad_request(conn, id, "at least one of pin, mute, archive, unread");
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

// A text as the UI typed it, with the entities it has (the mentions: none else).
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

void msg_send(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  auto text = string_field(req, "text");
  if (text.is_error() || text.ok().empty()) {
    return bad_request(conn, id, "need the field text, a string that is not empty");
  }
  td_api::object_ptr<td_api::InputMessageReplyTo> reply;
  std::int32_t topic = -1;
  if (!quote_field(conn, id, req, reply) || !topic_field(conn, id, req, topic)) {
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
  auto content = make_object<td_api::inputMessageText>(
      plain_text(text.ok(), mention_entities(text.ok(), mentions)), nullptr, true);
  send_content(conn, id, chat, topic, std::move(reply), std::move(content));
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

// media.send: the file's kind comes from its extension. Only a photo or a video carries a caption: the UI sends the
// text of any other kind as a msg.send of its own. `thumb` (a base64 JPEG HyperBerry needed for WhatsApp) is not
// read: TDLib makes its own thumbnails.
void media_send(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  auto path = string_field(req, "path");
  std::int64_t bytes = 0;
  if (path.is_error() || !regular_file(path.ok(), bytes) || bytes > kMaxFile) {
    return bad_request(conn, id, "need the field path, the absolute path of a file of at most 2000 MB");
  }
  td_api::object_ptr<td_api::InputMessageReplyTo> reply;
  std::int32_t topic = -1;
  if (!quote_field(conn, id, req, reply) || !topic_field(conn, id, req, topic)) {
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
  const std::string text = has_caption == 1 ? v.get_string().str() : "";
  auto caption = [&]() -> td_api::object_ptr<td_api::formattedText> {
    return text.empty() ? nullptr : plain_text(text);  // null is TDLib's empty caption
  };
  auto file = [&] { return make_object<td_api::inputFileLocal>(path.ok()); };

  static const char *const audio[] = {".mp3", ".m4a", ".aac", ".amr", ".ogg", ".wav"};
  const std::string ext = extension(path.ok());
  td_api::object_ptr<td_api::InputMessageContent> content;
  if (document) {
    content = make_object<td_api::inputMessageDocument>(make_object<td_api::inputDocument>(file(), nullptr, false),
                                                        nullptr);
  } else if (ext == ".jpg" || ext == ".jpeg") {
    content = make_object<td_api::inputMessagePhoto>(
        make_object<td_api::inputPhoto>(file(), nullptr, nullptr, std::vector<std::int32_t>(), width, height),
        caption(), false, nullptr, false);
  } else if (ext == ".mp4" || ext == ".3gp" || ext == ".mov") {
    // The request has no duration, so it is 0.
    content = make_object<td_api::inputMessageVideo>(
        make_object<td_api::inputVideo>(file(), nullptr, nullptr, 0, std::vector<std::int32_t>(), 0, width, height,
                                        true),
        caption(), false, nullptr, false);
  } else if (std::find(std::begin(audio), std::end(audio), ext) != std::end(audio)) {
    content = make_object<td_api::inputMessageAudio>(make_object<td_api::inputAudio>(file(), nullptr, 0, "", ""),
                                                     nullptr);
  } else {
    content = make_object<td_api::inputMessageDocument>(make_object<td_api::inputDocument>(file(), nullptr, false),
                                                        nullptr);
  }
  send_content(conn, id, chat, topic, std::move(reply), std::move(content));
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
    auto content = make_object<td_api::inputMessageText>(plain_text(text.ok()), nullptr, false);
    tdlib.send(make_object<td_api::editMessageText>(chat, message, nullptr, std::move(content)), done);
  } else if (kind == 1 || kind == 2) {
    tdlib.send(make_object<td_api::editMessageCaption>(chat, message, nullptr, plain_text(text.ok()), false), done);
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
               if (first.empty()) {
                 return reply_td_error(conn, id, td_api::error(400, "the message can't be forwarded"));
               }
               queue_reply(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) { r("id", first); })));
             });
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

void download_failed(std::uint64_t conn, const std::string &id, const std::string &why) {
  say("media.get: %s", why.c_str());
  server.reply_error(conn, id, "download_failed", why);
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
  tdlib.send(make_object<td_api::getMessage>(chat, message), [=](Obj o) {
    if (as_error(o)) {
      return server.reply_error(conn, id, "not_found", "no such message");
    }
    auto &m = static_cast<td_api::message &>(*o);
    const td_api::file *file = preview ? message_preview_file(m) : message_file(m);
    if (!file) {
      return server.reply_error(conn, id, "not_found",
                                preview ? "the message has no preview picture" : "the message has no file");
    }
    if (file->local_ && file->local_->is_downloading_completed_) {
      messages_queue(m);  // its row may still say the file is on the server (or have no extra.preview)
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
          messages_queue(static_cast<td_api::message &>(*o3));  // now with media_state 2 and the path, or extra.preview
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
  auto w = watched.find(f.id_);
  if (w == watched.end() || !f.local_ || f.local_->is_downloading_completed_) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now - w->second.sent < std::chrono::milliseconds(200)) {
    return;
  }
  w->second.sent = now;
  server.broadcast(td::json_encode<std::string>(td::json_object([&](auto &o) {
    o("ev", "media.progress");
    o("chat", std::to_string(w->second.chat));
    o("id", std::to_string(w->second.message));
    o("done", f.local_->downloaded_size_);
    o("size", f.size_ ? f.size_ : f.expected_size_);
  })));
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
void send_content(std::uint64_t conn, const std::string &id, std::int64_t chat, std::int32_t topic,
                  td_api::object_ptr<td_api::InputMessageReplyTo> reply,
                  td_api::object_ptr<td_api::InputMessageContent> content) {
  tdlib.send(make_object<td_api::sendMessage>(chat, topic_object(topic), std::move(reply), nullptr, nullptr,
                                              std::move(content)),
             [conn, id](Obj o) {
               if (auto e = as_error(o)) {
                 say("td error: %d %s", e->code_, e->message_.c_str());
                 return reply_td_error(conn, id, *e);
               }
               auto &m = static_cast<td_api::message &>(*o);
               messages_queue(m);  // the row, state 0, under its temporary id
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

td_api::object_ptr<td_api::MessageTopic> topic_object(std::int32_t topic) {
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

bool requests_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  static const char *const ours[] = {"chat.open", "chat.history", "chat.newer", "chat.pins", "chat.close", "chat.read",
                                     "chat.typing", "chat.set", "msg.send", "media.send", "msg.delete", "msg.edit",
                                     "msg.forward", "media.get", "media.cancel"};
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
    if (parse_limit(conn, id, req, 200, limit) && topic_field(conn, id, req, topic)) {
      chat_open(conn, id, chat, static_cast<std::size_t>(limit), topic);
    }
  } else if (cmd == "chat.history") {
    chat_history(conn, id, req, chat);
  } else if (cmd == "chat.newer") {
    chat_newer(conn, id, req, chat);
  } else if (cmd == "chat.pins") {
    chat_pins(conn, id, req, chat);
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
    if (!topic_field(conn, id, req, topic)) {
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
    if (!topic_field(conn, id, req, topic)) {
      return true;
    }
    td_api::object_ptr<td_api::ChatAction> action;
    if (on.get_boolean()) {
      action = make_object<td_api::chatActionTyping>();
    } else {
      action = make_object<td_api::chatActionCancel>();
    }
    tdlib.send(make_object<td_api::sendChatAction>(chat, topic_object(topic), "", std::move(action)),
               [conn, id](Obj o) { answer(conn, id, o); });
  } else if (cmd == "chat.set") {
    chat_set(conn, id, req, chat);
  } else if (cmd == "msg.send") {
    msg_send(conn, id, req, chat);
  } else if (cmd == "media.send") {
    media_send(conn, id, req, chat);
  } else if (cmd == "msg.delete") {
    msg_delete(conn, id, req, chat);
  } else if (cmd == "msg.edit") {
    msg_edit(conn, id, req, chat);
  } else if (cmd == "msg.forward") {
    msg_forward(conn, id, req, chat);
  } else if (cmd == "media.cancel") {
    media_cancel(conn, id, req, chat);
  } else {
    media_get(conn, id, req, chat);
  }
  return true;
}
