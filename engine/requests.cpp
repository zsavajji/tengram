// The requests about chats and messages: chat.open, chat.history, chat.close, chat.read, chat.typing,
// chat.set and msg.send (docs/protocol.md). The rows they ask for are written by messages.cpp; the replies
// that tell the UI to read them come after the flush that commits them (queue_reply).

#include "engine.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <memory>

namespace {

using td_api::make_object;

// ---- reading a request

void bad_request(std::uint64_t conn, const std::string &id, const std::string &what) {
  server.reply_error(conn, id, "bad_request", what);
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
bool start(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t &chat) {
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

std::string page_json(const std::string &oldest, std::size_t count, bool more) {
  return td::json_encode<std::string>(td::json_object([&](auto &o) {
    o("oldest", oldest);
    o("count", static_cast<std::int32_t>(count));
    o("more", td::JsonBool(more));
  }));
}

// ---- chat.open and chat.history: a page of history

struct Page {
  std::uint64_t conn;
  std::string id;
  std::int64_t chat;
  std::size_t want;  // messages to collect
  std::int64_t upper;  // the table's rows from the oldest message up to here are the page's to reconcile
  std::vector<td_api::object_ptr<td_api::message>> got;  // newest first
  bool more = true;
  int calls = 0;
};

void finish(const std::shared_ptr<Page> &p) {
  if (p->got.empty()) {  // nothing to compare with, nothing written, so nothing to wait for
    server.reply_ok(p->conn, p->id, page_json("", 0, false));
    return;
  }
  const std::int64_t oldest = p->got.back()->id_;
  messages_reconcile(p->chat, p->got, oldest, p->upper);
  queue_reply(p->conn, p->id, page_json(std::to_string(oldest), p->got.size(), p->more));
}

// TDLib may answer with fewer messages than asked (often one, the first time), so this asks again from the
// oldest message so far. On the phone from_message_id itself does not come back with offset 0; the filter
// below costs nothing if a TDLib ever sends it.
void fetch(const std::shared_ptr<Page> &p, std::int64_t from) {
  const int limit = static_cast<int>(std::min<std::size_t>(100, p->want - p->got.size()));
  tdlib.send(make_object<td_api::getChatHistory>(p->chat, from, 0, limit, false), [p, from](Obj o) {
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
      p->more = false;
      return finish(p);
    }
    if (p->got.size() >= p->want || ++p->calls >= 20) {
      return finish(p);
    }
    fetch(p, p->got.back()->id_);
  });
}

void chat_open(std::uint64_t conn, const std::string &id, std::int64_t chat) {
  tdlib.send(make_object<td_api::getChat>(chat), [=](Obj o) {
    if (as_error(o)) {
      return server.reply_error(conn, id, "bad_chat", "no such chat");
    }
    tdlib.send(make_object<td_api::openChat>(chat), [=](Obj o2) {
      if (auto e = as_error(o2)) {
        say("td error: %d %s", e->code_, e->message_.c_str());
        return reply_td_error(conn, id, *e);
      }
      fetch(std::make_shared<Page>(Page{conn, id, chat, 50, INT64_MAX, {}}), 0);
    });
  });
}

void chat_history(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t before = 0;
  auto text = string_field(req, "before");
  if (text.is_error() || !parse_i64(text.ok(), before) || before <= 0) {
    return bad_request(conn, id, "need the field before, a string with a message id");
  }
  long limit = 50;
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
  if (given < 0 || limit < 1 || limit > 100) {
    return bad_request(conn, id, "limit is a number from 1 to 100");
  }
  fetch(std::make_shared<Page>(Page{conn, id, chat, static_cast<std::size_t>(limit), before, {}}), before);
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

// ---- msg.send

void msg_send(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  auto text = string_field(req, "text");
  if (text.is_error() || text.ok().empty()) {
    return bad_request(conn, id, "need the field text, a string that is not empty");
  }
  std::int64_t quote = 0;
  td::JsonValue v;
  const int has_quote = optional_field(req, "quote", td::JsonValue::Type::String, v);
  if (has_quote < 0 || (has_quote == 1 && (!parse_i64(v.get_string().str(), quote) || quote <= 0))) {
    return bad_request(conn, id, "quote is a string with a message id");
  }
  td_api::object_ptr<td_api::InputMessageReplyTo> reply;
  if (has_quote == 1) {
    reply = make_object<td_api::inputMessageReplyToMessage>(quote, nullptr, 0, "");
  }
  auto content = make_object<td_api::inputMessageText>(
      make_object<td_api::formattedText>(text.ok(), std::vector<td_api::object_ptr<td_api::textEntity>>()), nullptr,
      true);
  tdlib.send(make_object<td_api::sendMessage>(chat, nullptr, std::move(reply), nullptr, nullptr, std::move(content)),
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

}  // namespace

bool requests_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  static const char *const ours[] = {"chat.open", "chat.history", "chat.close", "chat.read",
                                     "chat.typing", "chat.set", "msg.send"};
  if (std::find(std::begin(ours), std::end(ours), cmd) == std::end(ours)) {
    return false;
  }
  std::int64_t chat = 0;
  if (!start(conn, id, req, chat)) {
    return true;
  }

  if (cmd == "chat.open") {
    chat_open(conn, id, chat);
  } else if (cmd == "chat.history") {
    chat_history(conn, id, req, chat);
  } else if (cmd == "chat.close") {
    tdlib.send(make_object<td_api::closeChat>(chat), [conn, id](Obj) { server.reply_ok(conn, id); });
  } else if (cmd == "chat.read") {
    ChatBrief brief = {};
    if (!chat_brief(chat, brief)) {
      server.reply_error(conn, id, "bad_chat", "no such chat");
    } else if (!brief.has_last) {
      server.reply_ok(conn, id);  // an empty chat has nothing to read
    } else {
      tdlib.send(make_object<td_api::viewMessages>(chat, std::vector<std::int64_t>{brief.last_id}, nullptr, true),
                 [conn, id](Obj o) { answer(conn, id, o); });
    }
  } else if (cmd == "chat.typing") {
    td::JsonValue on;
    if (optional_field(req, "on", td::JsonValue::Type::Boolean, on) != 1) {
      bad_request(conn, id, "need the field on, true or false");
      return true;
    }
    td_api::object_ptr<td_api::ChatAction> action;
    if (on.get_boolean()) {
      action = make_object<td_api::chatActionTyping>();
    } else {
      action = make_object<td_api::chatActionCancel>();
    }
    tdlib.send(make_object<td_api::sendChatAction>(chat, nullptr, "", std::move(action)),
               [conn, id](Obj o) { answer(conn, id, o); });
  } else if (cmd == "chat.set") {
    chat_set(conn, id, req, chat);
  } else {
    msg_send(conn, id, req, chat);
  }
  return true;
}
