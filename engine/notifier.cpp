// The notifier. TDLib decides what deserves a notification (mutes, mentions in muted chats, silent messages,
// messages read on another device, the delay while another device is active). This mirrors its notification
// groups into the Hub, one item per chat, through hub.h. hub.json records what is already in the Hub, because
// the Hub keeps its items when the engine restarts.

#include "engine.h"
#include "hub.h"

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace {

struct ChatState {
  int total[2] = {0, 0};  // its Messages group and its Mentions group
  bool has_item = false;  // the Hub has an item for the chat
  int unread = 0;         // what that item says, with its title, body and time
  std::string title, body;
  std::int64_t ts_ms = 0;
};

std::map<std::int64_t, ChatState> chats;
bool usable = false;  // the Hub opened
bool account = false;
// The Hub keeps an account's icon from when it was added: a new glyph shows only on an account added again. hub.json
// says which glyph its account has; another number re-adds the account once (the inverted glyphs, 2026-11-17).
// Re-adding isn't enough: the Hub caches the picture by file name (the old grey one stayed, the lock screen showed
// the new), so a new glyph also needs a new file name (hub_account2.png, glyph 3; 2026-11-18).
constexpr int kGlyph = 3;
int glyph = 0;
std::set<std::string> other_types;  // group types we have logged and ignore

std::string path() {
  return data_dir + "/hub.json";
}

void save() {
  std::vector<std::int64_t> with_item;
  for (const auto &entry : chats) {
    if (entry.second.has_item) {
      with_item.push_back(entry.first);
    }
  }
  const std::string json = td::json_encode<std::string>(td::json_object([&](auto &o) {
    o("account", td::JsonBool(account));
    o("glyph", glyph);
    o("chats", td::json_array(with_item, [](std::int64_t id) { return std::to_string(id); }));
  }));
  const std::string tmp = path() + ".tmp";
  if (!write_file(tmp, json) || rename(tmp.c_str(), path().c_str()) < 0) {
    say("cannot write %s", path().c_str());
  }
}

void load() {
  std::ifstream in(path());
  std::stringstream text;
  text << in.rdbuf();
  std::string json = text.str();
  auto parsed = td::json_decode(td::MutableSlice(json));
  if (parsed.is_error() || parsed.ok_ref().type() != td::JsonValue::Type::Object) {
    return;
  }
  auto &obj = parsed.ok_ref().get_object();
  td::JsonValue has_account, list;
  account = optional_field(obj, "account", td::JsonValue::Type::Boolean, has_account) == 1 &&
            has_account.get_boolean();
  td::JsonValue has_glyph;
  glyph = optional_field(obj, "glyph", td::JsonValue::Type::Number, has_glyph) == 1
              ? std::atoi(has_glyph.get_number().str().c_str())
              : 0;
  if (optional_field(obj, "chats", td::JsonValue::Type::Array, list) == 1) {
    for (const td::JsonValue &item : list.get_array()) {
      if (item.type() == td::JsonValue::Type::String) {
        chats[std::strtoll(item.get_string().str().c_str(), nullptr, 10)].has_item = true;
      }
    }
  }
  say("hub: account %s, %zu items from the last run", account ? "known" : "not added", chats.size());
}

// 0 for a Messages group, 1 for Mentions, -1 for the others (a secret chat's, a call's), logged once each.
int group_index(const td_api::NotificationGroupType *type) {
  if (type && type->get_id() == td_api::notificationGroupTypeMessages::ID) {
    return 0;
  }
  if (type && type->get_id() == td_api::notificationGroupTypeMentions::ID) {
    return 1;
  }
  if (other_types.insert(type ? type_name(*type) : "none").second) {
    say("hub: notification groups of type %s are ignored", type ? type_name(*type).c_str() : "none");
  }
  return -1;
}

// The newest of some notifications, nullptr for none.
const td_api::notification *newest(const std::vector<td_api::object_ptr<td_api::notification>> &list) {
  const td_api::notification *best = nullptr;
  for (const auto &n : list) {
    if (n && (!best || n->date_ >= best->date_)) {
      best = n.get();
    }
  }
  return best;
}

// What the item says: the chat list's preview of the message, or a fixed line when the preview is not wanted.
std::string body_of(const td_api::notification *n) {
  if (n && n->type_ && n->type_->get_id() == td_api::notificationTypeNewMessage::ID && mode_notify_preview()) {
    const auto &t = static_cast<const td_api::notificationTypeNewMessage &>(*n->type_);
    if (t.show_preview_ && t.message_) {
      return message_preview(*t.message_);
    }
  }
  return "New message";
}

// A column of the chat's row in chats.db, "" when there is none. The mirror in memory is empty for the first seconds
// of a run (and holds only the chats TDLib has sent since), while the last run's items are rewritten at once: every
// one said "Telegram / New message" after an engine restart (the user, 2026-11-17).
std::string stored(std::int64_t chat, const char *sql) {
  const std::string jid = std::to_string(chat);  // bound by reference: it must outlive the step
  auto s = db.prepare(sql);
  if (s.empty() || s.bind_string(1, jid).is_error() || s.step().is_error() || !s.has_row() ||
      s.view_datatype(0) == td::SqliteStatement::Datatype::Null) {
    return "";
  }
  return s.view_string(0).str();
}

std::string title_of(std::int64_t chat) {
  std::string title = chat_title(chat);
  if (title.empty()) {
    title = stored(chat, "SELECT name FROM chats WHERE jid = ?1");
  }
  return title.empty() ? "Telegram" : title;
}

// What an item said before this run, which hub.json doesn't keep: the chat's last preview.
std::string last_body(std::int64_t chat, const ChatState &s) {
  std::string body = s.body.empty() ? stored(chat, "SELECT last_preview FROM chats WHERE jid = ?1") : s.body;
  return body.empty() || !mode_notify_preview() ? "New message" : body;
}

// Adds or updates the chat's item. The account goes in before the first item ever; hub.json is written when
// that, or the set of chats with an item, changes.
void put_item(std::int64_t chat, ChatState &s, const std::string &title, const std::string &body, int unread,
              std::int64_t ts_ms, bool alert) {
  bool changed = false;
  if (!account) {
    hub_account(true);
    account = changed = true;
    glyph = kGlyph;
  }
  const bool is_new = !s.has_item;
  hub_item(HubItem{chat, title, body, unread, ts_ms, alert}, is_new);
  s.has_item = true;
  s.unread = unread;
  s.title = title;
  s.body = body;
  s.ts_ms = ts_ms;
  if (is_new || changed) {
    save();
  }
}

void group_update(const td_api::updateNotificationGroup &u) {
  const int type = group_index(u.type_.get());
  if (type < 0) {
    return;
  }
  ChatState &s = chats[u.chat_id_];
  s.total[type] = u.total_count_;
  const int sum = s.total[0] + s.total[1];
  if (const td_api::notification *n = newest(u.added_notifications_)) {
    bool audible = false;
    for (const auto &added : u.added_notifications_) {
      audible = audible || (added && !added->is_silent_);
    }
    const bool alert = u.notification_sound_id_ != 0 && audible && !mode_ui_shows(u.chat_id_);
    put_item(u.chat_id_, s, title_of(u.chat_id_), body_of(n), std::max(1, sum), std::int64_t{n->date_} * 1000, alert);
  } else if (s.has_item && sum != s.unread) {
    // Read (sum 0) or partly read somewhere else: the item says so, with what it showed before.
    put_item(u.chat_id_, s, s.title, s.body, sum, s.ts_ms, false);
  }
}

// At the start: what TDLib still holds as notifications. They alerted before the restart, so none alerts now.
void active_update(const td_api::updateActiveNotifications &u) {
  std::map<std::int64_t, const td_api::notification *> shown;  // chats with a group of notifications
  std::set<std::int64_t> with_group;
  for (const auto &g : u.groups_) {
    const int type = g ? group_index(g->type_.get()) : -1;
    if (type < 0) {
      continue;
    }
    chats[g->chat_id_].total[type] = g->total_count_;
    with_group.insert(g->chat_id_);
    const td_api::notification *n = newest(g->notifications_);
    if (!shown.count(g->chat_id_) || (n && (!shown[g->chat_id_] || n->date_ >= shown[g->chat_id_]->date_))) {
      shown[g->chat_id_] = n;
    }
  }
  const std::int64_t now_ms = std::int64_t{std::time(nullptr)} * 1000;
  for (const auto &entry : shown) {
    ChatState &s = chats[entry.first];
    const int sum = s.total[0] + s.total[1];
    if (sum > 0) {
      put_item(entry.first, s, title_of(entry.first), body_of(entry.second), sum,
               entry.second ? std::int64_t{entry.second->date_} * 1000 : now_ms, false);
    }
  }
  // Items the last run left in the Hub for chats with nothing to show any more are read now, once.
  for (auto &entry : chats) {
    if (entry.second.has_item && !with_group.count(entry.first)) {
      ChatState &s = entry.second;
      put_item(entry.first, s, title_of(entry.first), last_body(entry.first, s), 0,
               s.ts_ms ? s.ts_ms : now_ms, false);
    }
  }
}

}  // namespace

void notifier_init() {
  if (!hub_open()) {
    say("hub: not available, no notifications");
    return;
  }
  usable = true;
  load();
  if (hub_new()) {  // the Hub holds nothing of ours, whatever hub.json says
    say("hub: new to the Hub, hub.json is forgotten");
    account = false;
    chats.clear();
    save();
  } else if (account && glyph != kGlyph) {  // its items go with it; the unread ones come back below
    say("hub: the account has glyph %d, re-added with %d", glyph, kGlyph);
    hub_account(false);
    account = false;
    chats.clear();
    save();
  }
}

bool notifier_active() {
  return usable;
}

void notifier_update(td_api::Object &update) {
  if (!usable) {
    return;
  }
  if (update.get_id() == td_api::updateNotificationGroup::ID) {
    group_update(static_cast<td_api::updateNotificationGroup &>(update));
  } else if (update.get_id() == td_api::updateActiveNotifications::ID) {
    active_update(static_cast<td_api::updateActiveNotifications &>(update));
  }
}

void notifier_reset() {
  if (!usable) {
    return;
  }
  if (account) {
    hub_account(false);
  }
  account = false;
  chats.clear();
  save();
}

void notifier_action(const std::string &action, std::int64_t chat) {
  if (!usable) {
    return;
  }
  say("hub: action %s on chat %lld", action.c_str(), static_cast<long long>(chat));
  if (action == "read") {
    chats_read(chat, [](const td_api::error *e) {
      if (e) {
        say("hub: read: %d %s", e->code_, e->message_.c_str());
      }
    });
  } else if (action == "unread") {
    tdlib.send(td_api::make_object<td_api::toggleChatIsMarkedAsUnread>(chat, true));
  } else if (action == "delete") {
    hub_remove(chat);
    auto it = chats.find(chat);
    if (it != chats.end() && it->second.has_item) {
      it->second.has_item = false;
      save();
    }
  }
}
