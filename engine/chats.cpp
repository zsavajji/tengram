// The mirror of Telegram's chat list in the `chats` table (docs/protocol.md). What TDLib says about each chat
// is kept here; a change marks the chat dirty, and flush() (main.cpp) writes the dirty ones at most every 50 ms
// and then names them in `chats` events.

#include "engine.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::int64_t kForever = std::int64_t{1} << 40;  // muted_until of a chat muted for good
constexpr std::int64_t kForeverFrom = 366 * 86400;       // a mute this long (seconds) counts as for good
constexpr std::size_t kEventJids = 200;                    // most jids in one `chats` event

// The columns of a row, as they are or will be in `chats`.
struct Row {
  int kind = 0;
  std::string name, photo;  // photo "" is NULL
  std::int64_t last_ts = 0;
  bool has_last = false;    // last_preview is NULL without a last message
  std::string preview;
  int last_out = 0, last_state = 0, unread = 0, archived = 0;
  std::int64_t pinned = 0;  // 0 not pinned, else the main list's position order (larger is higher)
  std::int64_t muted_until = 0;
  std::int64_t read_inbox = 0;
  int forum = 0;
  int secret = 0;  // 0 not a secret chat, 1 pending, 2 ready, 3 closed
  int mentions = 0;      // unread mentions and replies to us
  int mentions_off = 0;  // 1 while the chat's mention notifications are off
  int reactions = 0;     // unread reactions to our messages
  std::string draft;     // the draft's text, "" is NULL (none)
};

// muted_until is the time the mute was learned plus what was left of it, so the same mute learned again a
// second later differs by a second or so: that is not a change.
bool same(const Row &a, const Row &b) {
  const bool timed = a.muted_until > 0 && b.muted_until > 0 && a.muted_until < kForever && b.muted_until < kForever;
  const std::int64_t gap = std::max(a.muted_until, b.muted_until) - std::min(a.muted_until, b.muted_until);
  return a.kind == b.kind && a.name == b.name && a.photo == b.photo && a.last_ts == b.last_ts &&
         a.has_last == b.has_last && a.preview == b.preview && a.last_out == b.last_out &&
         a.last_state == b.last_state && a.unread == b.unread && a.pinned == b.pinned && a.archived == b.archived &&
         a.read_inbox == b.read_inbox && a.forum == b.forum && a.secret == b.secret &&
         a.mentions == b.mentions && a.mentions_off == b.mentions_off &&
         a.reactions == b.reactions && a.draft == b.draft &&
         (timed ? gap <= 2 : a.muted_until == b.muted_until);
}

struct Chat {
  std::int64_t id = 0;
  int kind = 0;  // chats.kind: 0 a person, 1 a group, 3 a channel
  std::int64_t supergroup = 0;  // the supergroup's id in a supergroup chat, else 0
  bool forum = false;           // a supergroup with topics (updateSupergroup)
  std::int64_t peer = 0;  // the other person's user id in a private or a secret chat, else 0
  std::int32_t secret_chat = 0;  // the secret chat's id in a secret chat (chatTypeSecret), else 0
  std::string title;
  int photo_file = 0;  // the small photo's file id, 0 for none
  std::string photo_path;
  bool photo_wanted = false;  // not here yet and downloadable: fetched once the chat has a row
  bool has_last = false;
  bool emptied = false;  // chat.clear took the history: no last message is really none, until the next one
  std::int64_t last_id = 0;
  int last_date = 0;
  bool last_out = false;
  int last_sending = 0;  // 0 sent, 1 pending, 2 failed
  std::string last_preview;
  int unread = 0;
  bool marked_unread = false;
  bool use_default_mute = true;  // the chat's own notification settings, and when they were learned
  int mute_for = 0;
  std::int64_t mute_learned = 0;
  int mentions = 0;                 // unread_mention_count_: mentions and replies to us
  int reactions = 0;                // unread_reaction_count_: reactions to our messages
  bool use_default_mentions = true;  // ... and whether they notify: the chat's setting or its scope's
  bool mentions_off = false;
  bool in_main = false, in_archive = false;
  std::int64_t pinned = 0;  // 0 not pinned, else the main list's chatPosition.order (a pinned chat's follows the pin order)
  std::int64_t read_inbox = 0;   // last_read_inbox_message_id_
  std::int64_t read_outbox = 0;
  std::string draft;  // the text of the chat's draft (draftMessageContentText), "" for none or another kind
  std::int64_t draft_date = 0;  // when the draft was written, 0 for none: the chat sorts by it when it is newer (TDLib's order)
  bool dirty = false;
  enum class Stored { Unknown, Present, Absent } stored = Stored::Unknown;  // is there a row, and what is in it
  Row written;                                                              // valid when Present
};

struct UserNames {
  std::string first, full;
  int photo_file = 0;  // the small profile photo's file id, 0 for none
  std::string photo_path;  // "" until it is downloaded
  int accent = 0;      // accent_color_id_: 0 to 6 are the built-in colours, a higher one is a server colour (accent_colors)
};

std::unordered_map<std::int64_t, Chat> chats;
std::unordered_map<std::int64_t, UserNames> users;
std::unordered_map<std::int64_t, bool> forums;    // supergroup id -> is_forum, from updateSupergroup
std::unordered_map<std::int32_t, int> secret_states;  // secret chat id -> chats.secret (1 to 3), from updateSecretChat
std::unordered_map<int, std::int64_t> downloads;  // photo file id -> chat id, for each download asked for
std::unordered_map<int, std::int64_t> user_downloads;  // the same for the users' small profile photos
std::unordered_map<int, int> accent_colors;       // a server accent colour's id -> the built-in one (0 to 6) to use for it
std::unordered_set<std::int64_t> unknown_chats;   // chats an update named that we have no record of (logged once)
std::vector<std::int64_t> dirty_ids;              // the chats with dirty set
std::vector<std::pair<std::int64_t, std::string>> translate_todo;  // chat.set's translate, written by chats_write
std::vector<std::int64_t> changed;                // written or deleted, their event is still to come
int scope_mute[3] = {0, 0, 0};                    // default mute_for of private chats, groups, channels
std::int64_t scope_learned[3] = {0, 0, 0};
bool scope_mentions_off[3] = {false, false, false};  // disable_mention_notifications of the three scopes
bool flag_pending = false;                        // chatlist_complete is to be written with the next flush
bool flag_written = false;                        // ... and was, in the flush that is committing now
Clock::time_point load_start;

// What the `people` table holds of a user, as the UI reads it (docs/protocol.md): the colour index senders.get gives and the
// small photo's path ("" is NULL).
struct Person {
  int color = 0;
  std::string photo;
};

std::unordered_map<std::int64_t, Person> people_written;  // what the table holds, or will once the queued rows are written
std::unordered_map<std::int64_t, Person> people_todo;     // the users whose row is to be written at the next flush
bool people_loaded = false;                               // people_written has what the table held when the engine started

td::SqliteStatement insert_row, delete_row, select_row, insert_person;

std::int64_t now() {
  return std::time(nullptr);
}

int scope_of(int kind) {
  return kind == 0 ? 0 : kind == 1 ? 1 : 2;
}

int kind_of(const td_api::ChatType &t) {
  switch (t.get_id()) {
    case td_api::chatTypeBasicGroup::ID:
      return 1;
    case td_api::chatTypeSupergroup::ID:
      return static_cast<const td_api::chatTypeSupergroup &>(t).is_channel_ ? 3 : 1;
    default:  // private, secret
      return 0;
  }
}

void touch(Chat &c) {
  if (!c.dirty) {
    c.dirty = true;
    dirty_ids.push_back(c.id);
  }
  mark_dirty();
}

}  // namespace

std::string draft_text(const td_api::draftMessage *d) {
  if (!d || !d->content_ || d->content_->get_id() != td_api::draftMessageContentText::ID) {
    return "";
  }
  const auto &t = static_cast<const td_api::draftMessageContentText &>(*d->content_);
  return t.text_ ? t.text_->text_ : "";
}

// Writes the chat's row again at the next flush: our own id has come (Saved Messages' name, chats announced before
// getMe answered were written with ours), or chat.clear emptied the history (`emptied`: the preview goes).
void chats_rewrite(std::int64_t chat, bool emptied) {
  auto it = chats.find(chat);
  if (it == chats.end()) {
    return;
  }
  it->second.emptied = it->second.emptied || emptied;
  touch(it->second);
}

namespace {

Chat *find(std::int64_t id) {
  auto it = chats.find(id);
  if (it != chats.end()) {
    return &it->second;
  }
  if (unknown_chats.insert(id).second) {
    say("update for chat %lld, which TDLib has not announced: ignored", static_cast<long long>(id));
  }
  return nullptr;
}

std::string preview_of(const Chat &c, const td_api::message &m) {
  bool service = false;
  std::string text = message_body(m, service);
  if (!service && c.kind != 3) {
    if (m.is_outgoing_) {
      text = "You: " + text;
    } else if (c.kind == 1) {
      const std::string who = sender_name(m);
      if (!who.empty()) {
        text = who + ": " + text;
      }
    }
  }
  return one_line(std::move(text));
}

void set_last(Chat &c, const td_api::message *m) {
  c.has_last = m != nullptr;
  if (!m) {
    return;
  }
  c.emptied = false;
  c.last_id = m->id_;
  c.last_date = m->date_;
  c.last_out = m->is_outgoing_;
  c.last_sending = 0;
  if (m->sending_state_) {
    c.last_sending = m->sending_state_->get_id() == td_api::messageSendingStatePending::ID ? 1 : 2;
  }
  c.last_preview = preview_of(c, *m);
}

// 0 for a position that is not a pin, else its order: the pins keep the order the user gave them, which is not the
// last message's.
std::int64_t pin_of(const td_api::chatPosition &p) {
  return p.is_pinned_ && p.order_ != 0 ? p.order_ : 0;
}

// Only the main list's position matters, and only for pinned.
void set_positions(Chat &c, const std::vector<td_api::object_ptr<td_api::chatPosition>> &positions) {
  c.pinned = 0;
  for (const auto &p : positions) {
    if (p && p->list_ && p->list_->get_id() == td_api::chatListMain::ID) {
      c.pinned = pin_of(*p);
    }
  }
}

void set_list(Chat &c, const td_api::ChatList &list, bool member) {
  if (list.get_id() == td_api::chatListMain::ID) {
    c.in_main = member;
  } else if (list.get_id() == td_api::chatListArchive::ID) {
    c.in_archive = member;  // folders are not ours
  }
}

void set_draft(Chat &c, const td_api::draftMessage *d) {
  c.draft = draft_text(d);
  c.draft_date = c.draft.empty() ? 0 : d->date_;
}

void set_mute(Chat &c, const td_api::chatNotificationSettings &n) {
  c.use_default_mute = n.use_default_mute_for_;
  c.mute_for = n.mute_for_;
  c.mute_learned = now();
  c.use_default_mentions = n.use_default_disable_mention_notifications_;
  c.mentions_off = n.disable_mention_notifications_;
}

// The small photo. A file that is not here yet is asked for once, and only for a chat in a list (TDLib
// announces many others: senders, linked chats); updateFile brings the path.
void set_photo(Chat &c, const td_api::chatPhotoInfo *info) {
  const td_api::file *f = info ? info->small_.get() : nullptr;
  c.photo_file = f ? f->id_ : 0;
  c.photo_path.clear();
  c.photo_wanted = false;
  if (!f || !f->local_) {
    return;
  }
  if (f->local_->is_downloading_completed_) {
    c.photo_path = f->local_->path_;
  } else {
    c.photo_wanted = f->local_->can_be_downloaded_;
  }
}

void fetch_photo(Chat &c) {
  c.photo_wanted = false;
  if (downloads.emplace(c.photo_file, c.id).second) {
    tdlib.send(td_api::make_object<td_api::downloadFile>(c.photo_file, 1, 0, 0, false));
  }
}

void new_chat(const td_api::chat &t) {
  Chat &c = chats[t.id_];
  c.id = t.id_;
  c.kind = t.type_ ? kind_of(*t.type_) : 0;
  c.supergroup = t.type_ && t.type_->get_id() == td_api::chatTypeSupergroup::ID
                     ? static_cast<const td_api::chatTypeSupergroup &>(*t.type_).supergroup_id_
                     : 0;
  c.forum = forums.count(c.supergroup) && forums[c.supergroup];
  c.peer = 0;
  c.secret_chat = 0;
  if (t.type_ && t.type_->get_id() == td_api::chatTypePrivate::ID) {
    c.peer = static_cast<const td_api::chatTypePrivate &>(*t.type_).user_id_;
  } else if (t.type_ && t.type_->get_id() == td_api::chatTypeSecret::ID) {
    c.peer = static_cast<const td_api::chatTypeSecret &>(*t.type_).user_id_;
    c.secret_chat = static_cast<const td_api::chatTypeSecret &>(*t.type_).secret_chat_id_;
  }
  c.title = t.title_;
  c.unread = t.unread_count_;
  c.marked_unread = t.is_marked_as_unread_;
  c.mentions = t.unread_mention_count_;
  c.reactions = t.unread_reaction_count_;
  c.read_inbox = t.last_read_inbox_message_id_;
  c.read_outbox = t.last_read_outbox_message_id_;
  if (t.notification_settings_) {
    set_mute(c, *t.notification_settings_);
  }
  c.in_main = c.in_archive = false;
  for (const auto &list : t.chat_lists_) {
    if (list) {
      set_list(c, *list, true);
    }
  }
  set_positions(c, t.positions_);
  set_last(c, t.last_message_.get());
  if (t.last_message_) {
    messages_queue(*t.last_message_);  // so a chat never opened still paints its newest message at once
  }
  set_photo(c, t.photo_.get());
  set_draft(c, t.draft_message_.get());
  touch(c);
}

void set_scope(const td_api::updateScopeNotificationSettings &u) {
  if (!u.scope_ || !u.notification_settings_) {
    return;
  }
  int s = 2;
  if (u.scope_->get_id() == td_api::notificationSettingsScopePrivateChats::ID) {
    s = 0;
  } else if (u.scope_->get_id() == td_api::notificationSettingsScopeGroupChats::ID) {
    s = 1;
  }
  const bool mute_same = scope_mute[s] == u.notification_settings_->mute_for_;
  const bool mentions_same = scope_mentions_off[s] == u.notification_settings_->disable_mention_notifications_;
  if (mute_same && mentions_same) {
    return;
  }
  if (!mute_same) {
    scope_mute[s] = u.notification_settings_->mute_for_;
    scope_learned[s] = now();
  }
  scope_mentions_off[s] = u.notification_settings_->disable_mention_notifications_;
  for (auto &entry : chats) {
    if (((!mute_same && entry.second.use_default_mute) || (!mentions_same && entry.second.use_default_mentions)) &&
        scope_of(entry.second.kind) == s) {
      touch(entry.second);
    }
  }
}

// people_written starts as the table: a user whose colour and picture are what an earlier run wrote costs no write, so
// the second start's thousands of updateUser write nothing. Read once, at the first user. ponytail: nothing prunes the
// table or this map (a user is about a hundred bytes with the picture's path, and the people TDLib tells of are the ones the account has seen).
void people_load() {
  if (people_loaded) {
    return;
  }
  people_loaded = true;
  auto s = db.prepare("SELECT user, color, photo FROM people");
  if (s.empty()) {
    return;
  }
  auto status = s.step();
  while (status.is_ok() && s.has_row()) {
    std::int64_t id = 0;
    if (parse_i64(s.view_string(0).str(), id)) {
      Person &p = people_written[id];
      p.color = s.view_int32(1);
      p.photo = s.view_datatype(2) == td::SqliteStatement::Datatype::Null ? "" : s.view_string(2).str();
    }
    status = s.step();
  }
  if (status.is_error()) {
    say("chats.db: reading people: %s", status.message().c_str());
  }
}

// The user's row in `people` is to be written, unless the table already has this colour and picture. Called with every
// updateUser (the first sync sends thousands, nearly all of them for a user whose row is as it should be) and when the user's
// small photo is here; marks dirty only for a change, so an update that changes nothing does not wake the flush.
void people_note(std::int64_t id) {
  Person p;
  if (!user_color(id, p.color)) {
    return;
  }
  p.photo = user_photo(id);
  people_load();
  const auto written = people_written.find(id);
  if (written != people_written.end() && written->second.color == p.color && written->second.photo == p.photo) {
    return;
  }
  people_written[id] = p;
  people_todo[id] = p;
  mark_dirty();
}

td::Status write_person(std::int64_t id, const Person &p) {
  const std::string user = std::to_string(id);  // bound by reference: it must outlive the step
  auto &s = insert_person;
  s.reset();
  TRY_STATUS(s.bind_string(1, user));
  TRY_STATUS(s.bind_int32(2, p.color));
  TRY_STATUS(p.photo.empty() ? s.bind_null(3) : s.bind_string(3, p.photo));
  auto status = s.step();
  s.reset();
  return status;
}

// flush() runs it with the chats, in their transaction. A row that can't be written is forgotten in people_written, so the
// next update of that user queues it again.
void people_write() {
  if (people_todo.empty()) {
    return;
  }
  if (insert_person.empty()) {
    insert_person = db.prepare("INSERT OR REPLACE INTO people(user, color, photo) VALUES(?1, ?2, ?3)");
  }
  auto todo = std::move(people_todo);
  people_todo.clear();
  for (const auto &entry : todo) {
    auto status = insert_person.empty() ? td::Status::Error("no statement") : write_person(entry.first, entry.second);
    if (status.is_error()) {
      say("chats.db: person %lld: %s", static_cast<long long>(entry.first), status.message().c_str());
      people_written.erase(entry.first);
    }
  }
}

void set_user(const td_api::user &u) {
  UserNames &n = users[u.id_];
  n.first = u.first_name_;
  n.full = u.last_name_.empty() ? u.first_name_ : u.first_name_ + " " + u.last_name_;
  const td_api::file *small = u.profile_photo_ ? u.profile_photo_->small_.get() : nullptr;
  n.photo_file = small ? small->id_ : 0;
  n.photo_path = small && small->local_ && small->local_->is_downloading_completed_ ? small->local_->path_ : "";
  n.accent = u.accent_color_id_;
}

// updateAccentColors: TDLib lists only the server colours, the built-in 0 to 6 always exist and are not in it.
void set_accent_colors(const td_api::updateAccentColors &u) {
  accent_colors.clear();
  for (const auto &c : u.colors_) {
    if (c) {
      accent_colors[c->id_] = c->built_in_accent_color_id_;
    }
  }
  // A user with a server colour that came before this list was given a guess (user_color): their row says the real one now.
  for (const auto &entry : users) {
    if (entry.second.accent > 6 || entry.second.accent < 0) {
      people_note(entry.first);
    }
  }
}

// A user's small photo is here: the UI is told, to every connection (a page that listed the user without it).
void user_photo_here(std::int64_t user_id, int file, const std::string &path) {
  auto it = users.find(user_id);
  if (it == users.end() || it->second.photo_file != file) {  // not a photo the user has since replaced
    return;
  }
  it->second.photo_path = path;
  people_note(user_id);
  server.broadcast(td::json_encode<std::string>(td::json_object([&](auto &o) {
    o("ev", "photo");
    o("jid", std::to_string(user_id));
    o("photo", path);
  })));
}

std::int64_t muted_until(const Chat &c) {
  const int s = scope_of(c.kind);
  const int mute = c.use_default_mute ? scope_mute[s] : c.mute_for;
  const std::int64_t learned = c.use_default_mute ? scope_learned[s] : c.mute_learned;
  if (mute <= 0) {
    return 0;
  }
  return mute >= kForeverFrom ? kForever : learned + mute;
}

// chats.secret of a chat: what updateSecretChat last said of its secret chat. TDLib sends that update before the chat, so
// 1 (pending) for a secret chat it has not named is only a guard.
int secret_of(const Chat &c) {
  if (!c.secret_chat) {
    return 0;
  }
  const auto it = secret_states.find(c.secret_chat);
  return it == secret_states.end() ? 1 : it->second;
}

Row row_of(const Chat &c) {
  Row r;
  r.kind = c.kind;
  // The chat with ourselves is titled with our own name by TDLib; Telegram's apps call it Saved Messages. English, as
  // the previews are.
  r.name = c.id != 0 && c.id == link_me_id() ? "Saved Messages" : c.title;
  r.photo = c.photo_path;
  r.has_last = c.has_last;
  if (c.has_last) {
    r.last_ts = c.last_date;
    r.preview = c.last_preview;
    r.last_out = c.last_out;
    if (c.last_out) {
      r.last_state = c.last_sending == 1 ? 0 : c.last_sending == 2 ? 4 : c.last_id <= c.read_outbox ? 3 : 1;
    }
  }
  r.unread = c.unread == 0 && c.marked_unread ? -1 : c.unread;
  r.muted_until = muted_until(c);
  r.pinned = c.pinned;
  r.archived = c.in_archive && !c.in_main;
  r.read_inbox = c.read_inbox;
  r.forum = c.forum;
  r.secret = secret_of(c);
  r.mentions = c.mentions;
  r.reactions = c.reactions;
  r.mentions_off = (c.use_default_mentions ? scope_mentions_off[scope_of(c.kind)] : c.mentions_off) ? 1 : 0;
  r.draft = c.draft;  // whole: the composer takes it back
  r.last_ts = std::max(r.last_ts, c.draft_date);  // a newer draft lifts the chat, as Telegram's list
  return r;
}

bool prepare() {
  if (insert_row.empty()) {
    insert_row = db.prepare(
        "INSERT OR REPLACE INTO chats(jid, kind, name, photo, last_ts, last_preview, last_out, last_state, unread, "
        "muted_until, pinned, archived, read_inbox, forum, secret, mentions, mentions_off, reactions, draft, translate) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, "
        "COALESCE((SELECT translate FROM chats WHERE jid = ?1), ''))");
  }
  if (delete_row.empty()) {
    delete_row = db.prepare("DELETE FROM chats WHERE jid = ?1");
  }
  if (select_row.empty()) {
    select_row = db.prepare(
        "SELECT kind, name, photo, last_ts, last_preview, last_out, last_state, unread, muted_until, pinned, "
        "archived, read_inbox, forum, secret, mentions, mentions_off, reactions, draft FROM chats WHERE jid = ?1");
  }
  return !insert_row.empty() && !delete_row.empty() && !select_row.empty();
}

// What the table holds for this chat, once, the first time the chat is to be written.
td::Status load_row(Chat &c) {
  const std::string jid = std::to_string(c.id);  // bound by reference: it must outlive the step
  select_row.reset();
  TRY_STATUS(select_row.bind_string(1, jid));
  TRY_STATUS(select_row.step());
  c.stored = Chat::Stored::Absent;
  if (select_row.has_row()) {
    auto &s = select_row;
    Row &r = c.written;
    r = Row();
    r.kind = s.view_int32(0);
    r.name = s.view_string(1).str();
    r.photo = s.view_datatype(2) == td::SqliteStatement::Datatype::Null ? "" : s.view_string(2).str();
    r.last_ts = s.view_int64(3);
    r.has_last = s.view_datatype(4) != td::SqliteStatement::Datatype::Null;
    r.preview = r.has_last ? s.view_string(4).str() : "";
    r.last_out = s.view_int32(5);
    r.last_state = s.view_int32(6);
    r.unread = s.view_int32(7);
    r.muted_until = s.view_int64(8);
    r.pinned = s.view_int64(9);
    r.archived = s.view_int32(10);
    r.read_inbox = s.view_int64(11);
    r.forum = s.view_int32(12);
    r.secret = s.view_int32(13);
    r.mentions = s.view_int32(14);
    r.mentions_off = s.view_int32(15);
    r.reactions = s.view_int32(16);
    r.draft = s.view_datatype(17) == td::SqliteStatement::Datatype::Null ? "" : s.view_string(17).str();
    c.stored = Chat::Stored::Present;
  }
  select_row.reset();
  return td::Status::OK();
}

td::Status write_row(const Chat &c, const Row &r) {
  const std::string jid = std::to_string(c.id);
  auto &s = insert_row;
  s.reset();
  TRY_STATUS(s.bind_string(1, jid));
  TRY_STATUS(s.bind_int32(2, r.kind));
  TRY_STATUS(s.bind_string(3, r.name));
  TRY_STATUS(r.photo.empty() ? s.bind_null(4) : s.bind_string(4, r.photo));
  TRY_STATUS(s.bind_int64(5, r.last_ts));
  TRY_STATUS(r.has_last ? s.bind_string(6, r.preview) : s.bind_null(6));
  TRY_STATUS(s.bind_int32(7, r.last_out));
  TRY_STATUS(s.bind_int32(8, r.last_state));
  TRY_STATUS(s.bind_int32(9, r.unread));
  TRY_STATUS(s.bind_int64(10, r.muted_until));
  TRY_STATUS(s.bind_int64(11, r.pinned));
  TRY_STATUS(s.bind_int32(12, r.archived));
  TRY_STATUS(s.bind_int64(13, r.read_inbox));
  TRY_STATUS(s.bind_int32(14, r.forum));
  TRY_STATUS(s.bind_int32(15, r.secret));
  TRY_STATUS(s.bind_int32(16, r.mentions));
  TRY_STATUS(s.bind_int32(17, r.mentions_off));
  TRY_STATUS(s.bind_int32(18, r.reactions));
  TRY_STATUS(r.draft.empty() ? s.bind_null(19) : s.bind_string(19, r.draft));
  auto status = s.step();
  s.reset();
  return status;
}

td::Status delete_chat(const Chat &c) {
  const std::string jid = std::to_string(c.id);
  delete_row.reset();
  TRY_STATUS(delete_row.bind_string(1, jid));
  auto status = delete_row.step();
  delete_row.reset();
  return status;
}

void load_list(bool main_list) {
  td_api::object_ptr<td_api::ChatList> list;
  if (main_list) {
    list = td_api::make_object<td_api::chatListMain>();
  } else {
    list = td_api::make_object<td_api::chatListArchive>();
  }
  tdlib.send(td_api::make_object<td_api::loadChats>(std::move(list), 100), [main_list](Obj o) {
    const td_api::error *e = as_error(o);
    if (!e) {
      return load_list(main_list);  // another page was loaded
    }
    if (e->code_ != 404) {  // 404 is "no more"; anything else is a load that failed: try again at the next start
      say("loadChats: %d %s; the chat list stays partly loaded", e->code_, e->message_.c_str());
      link_syncing(false);  // the UI goes on with what there is; the next start tries again
      return;
    }
    if (main_list) {
      return load_list(false);
    }
    std::size_t listed = 0;
    for (const auto &entry : chats) {
      listed += entry.second.in_main || entry.second.in_archive;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - load_start).count();
    say("chat list loaded: %zu chats in %lld ms", listed, static_cast<long long>(ms));
    flag_pending = true;  // goes in with the rows that are still waiting, in one commit
    mark_dirty();
  });
}

}  // namespace

// One line of at most 150 bytes, cut between characters.
std::string one_line(std::string s) {
  for (char &ch : s) {
    if (ch == '\n' || ch == '\r' || ch == '\t') {
      ch = ' ';
    }
  }
  if (s.size() > 150) {
    std::size_t n = 150;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) {  // s[n] would be mid-character
      --n;
    }
    s.resize(n);
  }
  return s;
}

std::int64_t topic_muted_until(const td_api::chatNotificationSettings &n) {
  if (n.use_default_mute_for_ || n.mute_for_ <= 0) {
    return 0;
  }
  return n.mute_for_ >= kForeverFrom ? kForever : now() + n.mute_for_;
}

std::string message_preview(const td_api::message &m) {
  auto it = chats.find(m.chat_id_);
  return it != chats.end() ? preview_of(it->second, m) : preview_of(Chat(), m);  // an unknown chat is a person's
}

bool chats_read(std::int64_t chat_id, std::function<void(const td_api::error *)> done, std::int64_t upto) {
  ChatBrief brief = {};
  if (!chat_brief(chat_id, brief)) {
    return false;
  }
  if (!brief.has_last) {
    done(nullptr);
  } else {
    const std::int64_t at = upto > 0 ? std::min(upto, brief.last_id) : brief.last_id;  // never past the newest
    tdlib.send(td_api::make_object<td_api::viewMessages>(chat_id, std::vector<std::int64_t>{at}, nullptr, true),
               [done](Obj o) { done(as_error(o)); });
  }
  return true;
}

bool chat_brief(std::int64_t chat_id, ChatBrief &out) {
  auto it = chats.find(chat_id);
  if (it == chats.end()) {
    return false;
  }
  out = {it->second.kind,  it->second.has_last, it->second.last_id, it->second.read_outbox,
         it->second.peer,  it->second.forum,    secret_of(it->second)};
  return true;
}

void chats_last_message(const td_api::message &m) {
  auto it = chats.find(m.chat_id_);
  if (it != chats.end()) {
    set_last(it->second, &m);
    touch(it->second);
  }
}

const std::string &user_name(std::int64_t user_id, bool full) {
  static const std::string none;
  auto it = users.find(user_id);
  return it == users.end() ? none : full ? it->second.full : it->second.first;
}

const std::string &chat_title(std::int64_t chat_id) {
  static const std::string none;
  auto it = chats.find(chat_id);
  return it == chats.end() ? none : it->second.title;
}

const std::string &user_photo(std::int64_t user_id) {
  static const std::string none;
  auto it = users.find(user_id);
  return it == users.end() ? none : it->second.photo_path;
}

bool user_color(std::int64_t user_id, int &color) {
  auto it = users.find(user_id);
  if (it == users.end()) {
    return false;
  }
  color = it->second.accent;
  if (color > 6 || color < 0) {  // a server colour: its built-in one, else a guess that at least spreads them
    auto server_color = accent_colors.find(color);
    color = server_color != accent_colors.end() ? server_color->second : color % 7;
  }
  if (color > 6 || color < 0) {  // TDLib promises 0 to 6; never hand the UI anything else
    color = 0;
  }
  return true;
}

bool user_photo_ask(std::int64_t user_id) {
  auto it = users.find(user_id);
  if (it == users.end() || it->second.photo_file == 0 || !it->second.photo_path.empty()) {
    return false;
  }
  const int file = it->second.photo_file;
  if (!user_downloads.emplace(file, user_id).second) {
    return false;
  }
  tdlib.send(td_api::make_object<td_api::downloadFile>(file, 1, 0, 0, false), [user_id, file](Obj o) {
    if (auto e = as_error(o)) {
      say("user photo: %d %s", e->code_, e->message_.c_str());
      user_downloads.erase(file);  // asked for again with the next page
      return;
    }
    // Not synchronous: this is the file as it is now. One that was here already has no update coming.
    const auto &f = static_cast<const td_api::file &>(*o);
    if (f.local_ && f.local_->is_downloading_completed_ && user_downloads.erase(file)) {
      user_photo_here(user_id, file, f.local_->path_);
    }
  });
  return true;
}

void chats_update(td_api::Object &update) {
  switch (update.get_id()) {
    case td_api::updateNewChat::ID: {
      auto &u = static_cast<td_api::updateNewChat &>(update);
      if (u.chat_) {
        new_chat(*u.chat_);
      }
      break;
    }
    case td_api::updateChatDraftMessage::ID: {
      auto &u = static_cast<td_api::updateChatDraftMessage &>(update);
      if (Chat *c = find(u.chat_id_)) {
        set_draft(*c, u.draft_message_.get());
        set_positions(*c, u.positions_);
        touch(*c);
      }
      break;
    }
    case td_api::updateChatTitle::ID: {
      auto &u = static_cast<td_api::updateChatTitle &>(update);
      if (Chat *c = find(u.chat_id_)) {
        c->title = u.title_;
        touch(*c);
      }
      break;
    }
    case td_api::updateChatPhoto::ID: {
      auto &u = static_cast<td_api::updateChatPhoto &>(update);
      if (Chat *c = find(u.chat_id_)) {
        set_photo(*c, u.photo_.get());
        touch(*c);
      }
      break;
    }
    case td_api::updateChatLastMessage::ID: {
      auto &u = static_cast<td_api::updateChatLastMessage &>(update);
      if (Chat *c = find(u.chat_id_)) {
        set_last(*c, u.last_message_.get());
        set_positions(*c, u.positions_);
        // Not ours still sending: the send's answer writes that row, tagged with the UI's local row (extra.local), as
        // messages_update's updateNewMessage leaves it to. TDLib gives this update before that answer, and an untagged row
        // that reaches the UI first can't be paired with the local row (a one-emoji text, which TDLib turns into a sticker).
        if (u.last_message_ && !u.last_message_->sending_state_) {
          messages_queue(*u.last_message_);
        }
        touch(*c);
      }
      break;
    }
    case td_api::updateChatPosition::ID: {
      auto &u = static_cast<td_api::updateChatPosition &>(update);
      if (Chat *c = find(u.chat_id_)) {
        if (u.position_ && u.position_->list_ && u.position_->list_->get_id() == td_api::chatListMain::ID) {
          c->pinned = pin_of(*u.position_);
          touch(*c);
        }
      }
      break;
    }
    case td_api::updateChatAddedToList::ID: {
      auto &u = static_cast<td_api::updateChatAddedToList &>(update);
      if (Chat *c = find(u.chat_id_)) {
        if (u.chat_list_) {
          set_list(*c, *u.chat_list_, true);
        }
        touch(*c);
      }
      break;
    }
    case td_api::updateChatRemovedFromList::ID: {
      auto &u = static_cast<td_api::updateChatRemovedFromList &>(update);
      if (Chat *c = find(u.chat_id_)) {
        if (u.chat_list_) {
          set_list(*c, *u.chat_list_, false);
        }
        touch(*c);
      }
      break;
    }
    case td_api::updateChatReadInbox::ID: {
      auto &u = static_cast<td_api::updateChatReadInbox &>(update);
      if (Chat *c = find(u.chat_id_)) {
        c->unread = u.unread_count_;
        c->read_inbox = u.last_read_inbox_message_id_;
        touch(*c);
      }
      break;
    }
    case td_api::updateChatUnreadMentionCount::ID: {
      auto &u = static_cast<td_api::updateChatUnreadMentionCount &>(update);
      if (Chat *c = find(u.chat_id_)) {
        c->mentions = u.unread_mention_count_;
        touch(*c);
      }
      break;
    }
    case td_api::updateMessageMentionRead::ID: {
      auto &u = static_cast<td_api::updateMessageMentionRead &>(update);
      if (Chat *c = find(u.chat_id_)) {
        c->mentions = u.unread_mention_count_;
        touch(*c);
      }
      break;
    }
    case td_api::updateChatUnreadReactionCount::ID: {
      auto &u = static_cast<td_api::updateChatUnreadReactionCount &>(update);
      if (Chat *c = find(u.chat_id_)) {
        c->reactions = u.unread_reaction_count_;
        touch(*c);
      }
      break;
    }
    case td_api::updateMessageUnreadReactions::ID: {
      auto &u = static_cast<td_api::updateMessageUnreadReactions &>(update);
      if (Chat *c = find(u.chat_id_)) {
        c->reactions = u.unread_reaction_count_;
        touch(*c);
      }
      break;
    }
    case td_api::updateChatReadOutbox::ID: {
      auto &u = static_cast<td_api::updateChatReadOutbox &>(update);
      if (Chat *c = find(u.chat_id_)) {
        c->read_outbox = u.last_read_outbox_message_id_;
        touch(*c);
      }
      break;
    }
    case td_api::updateChatIsMarkedAsUnread::ID: {
      auto &u = static_cast<td_api::updateChatIsMarkedAsUnread &>(update);
      if (Chat *c = find(u.chat_id_)) {
        c->marked_unread = u.is_marked_as_unread_;
        touch(*c);
      }
      break;
    }
    case td_api::updateChatNotificationSettings::ID: {
      auto &u = static_cast<td_api::updateChatNotificationSettings &>(update);
      if (Chat *c = find(u.chat_id_)) {
        if (u.notification_settings_) {
          set_mute(*c, *u.notification_settings_);
        }
        touch(*c);
      }
      break;
    }
    case td_api::updateScopeNotificationSettings::ID:
      set_scope(static_cast<td_api::updateScopeNotificationSettings &>(update));
      break;
    case td_api::updateUser::ID: {
      auto &u = static_cast<td_api::updateUser &>(update);
      if (u.user_) {
        set_user(*u.user_);
        people_note(u.user_->id_);
      }
      break;
    }
    case td_api::updateSupergroup::ID: {  // a group can become a forum, or stop being one
      const auto &g = static_cast<td_api::updateSupergroup &>(update).supergroup_;
      if (g && forums[g->id_] != g->is_forum_) {
        forums[g->id_] = g->is_forum_;
        for (auto &entry : chats) {
          if (entry.second.supergroup == g->id_) {
            entry.second.forum = g->is_forum_;
            touch(entry.second);
          }
        }
      }
      break;
    }
    case td_api::updateSecretChat::ID: {  // a secret chat's state: its chat's row says so (also one that comes later)
      const auto &s = static_cast<td_api::updateSecretChat &>(update).secret_chat_;
      if (!s) {
        break;
      }
      const std::int32_t state = s->state_ ? s->state_->get_id() : 0;
      secret_states[s->id_] =
          state == td_api::secretChatStateReady::ID ? 2 : state == td_api::secretChatStateClosed::ID ? 3 : 1;
      for (auto &entry : chats) {
        if (entry.second.secret_chat == s->id_) {
          touch(entry.second);
        }
      }
      break;
    }
    case td_api::updateAccentColors::ID:
      set_accent_colors(static_cast<td_api::updateAccentColors &>(update));
      break;
    case td_api::updateFile::ID: {
      auto &f = *static_cast<td_api::updateFile &>(update).file_;
      media_progress(f);
      if (!f.local_ || !f.local_->is_downloading_completed_) {
        break;
      }
      auto asked = downloads.find(f.id_);
      if (asked != downloads.end()) {
        Chat *c = find(asked->second);
        if (c && c->photo_file == f.id_) {  // not a photo the chat has since replaced
          c->photo_path = f.local_->path_;
          touch(*c);
        }
      }
      // The same file can be both: a person's chat photo is their profile photo.
      auto wanted = user_downloads.find(f.id_);
      if (wanted != user_downloads.end()) {
        const std::int64_t user = wanted->second;
        user_downloads.erase(wanted);
        user_photo_here(user, f.id_, f.local_->path_);
      }
      break;
    }
    default:
      break;
  }
}

void chats_start() {
  auto flag = db.prepare("SELECT value FROM engine_state WHERE key = 'chatlist_complete'");
  if (!flag.empty() && flag.step().is_ok() && flag.has_row()) {
    // The whole list was loaded before. Loading the first page is all: with the list not in memory TDLib
    // needs 9 MB instead of 47, and chats further down still send updates when something happens in them.
    tdlib.send(td_api::make_object<td_api::loadChats>(td_api::make_object<td_api::chatListMain>(), 20), [](Obj o) {
      if (auto e = as_error(o)) {
        if (e->code_ != 404) {
          say("loadChats: %d %s", e->code_, e->message_.c_str());
        }
      }
    });
    return;
  }
  say("chat list: loading all of it");
  link_syncing(true);  // until the flush that writes chatlist_complete has committed
  load_start = Clock::now();
  load_list(true);
}

void chats_write() {
  if (flag_pending) {
    flag_pending = false;
    flag_written = db.exec("INSERT OR REPLACE INTO engine_state VALUES('chatlist_complete', 1)");
  }
  people_write();
  if (!translate_todo.empty()) {
    auto set = db.prepare("UPDATE chats SET translate = ?2 WHERE jid = ?1");
    for (const auto &[chat, lang] : std::exchange(translate_todo, {})) {
      const std::string jid = std::to_string(chat);  // bound by reference: it must outlive the step
      td::Status status = set.empty() ? td::Status::Error("no statement") : set.bind_string(1, jid);
      if (status.is_ok()) {
        status = set.bind_string(2, lang);
      }
      if (status.is_ok()) {
        status = set.step();
      }
      if (!set.empty()) {
        set.reset();
      }
      if (status.is_error()) {
        say("chats.db: translate of chat %lld: %s", static_cast<long long>(chat), status.message().c_str());
      } else {
        changed.push_back(chat);
      }
    }
  }
  if (dirty_ids.empty()) {
    return;
  }
  if (!prepare()) {  // the changes are lost until the chats change again
    for (std::int64_t id : dirty_ids) {
      chats[id].dirty = false;
    }
    dirty_ids.clear();
    return;
  }
  for (std::int64_t id : dirty_ids) {
    auto it = chats.find(id);
    if (it == chats.end()) {
      continue;
    }
    Chat &c = it->second;
    c.dirty = false;
    if (c.stored == Chat::Stored::Unknown) {
      auto loaded = load_row(c);
      if (loaded.is_error()) {
        say("chats.db: reading chat %lld: %s", static_cast<long long>(id), loaded.message().c_str());
        c.stored = Chat::Stored::Unknown;
      }
    }
    td::Status status;
    if (c.in_main || c.in_archive) {
      if (c.photo_wanted) {
        fetch_photo(c);
      }
      Row r = row_of(c);
      if (!c.has_last && c.stored == Chat::Stored::Present && c.emptied) {
        r.last_ts = c.written.last_ts;  // cleared here: no preview, and the chat keeps its place in the list
      } else if (!c.has_last && c.stored == Chat::Stored::Present) {
        // TDLib announces a chat it is still loading with no last message, and sends the message a moment
        // later: the row keeps what it has, so the list does not flicker and the page is not written twice.
        // ponytail: a chat whose history became empty elsewhere (another device's clear) keeps its old preview until
        // its next message; chat.clear here says so (chats_rewrite).
        r.has_last = c.written.has_last;
        r.last_ts = c.written.last_ts;
        r.preview = c.written.preview;
        r.last_out = c.written.last_out;
        r.last_state = c.written.last_state;
      }
      if (c.stored == Chat::Stored::Present && same(r, c.written)) {
        continue;
      }
      status = write_row(c, r);
      if (status.is_ok()) {
        c.written = r;
        c.stored = Chat::Stored::Present;
        changed.push_back(id);
      }
    } else if (c.stored == Chat::Stored::Present) {  // not in a list: no row
      status = delete_chat(c);
      if (status.is_ok()) {
        c.stored = Chat::Stored::Absent;
        changed.push_back(id);
      }
    }
    if (status.is_error()) {
      say("chats.db: chat %lld: %s", static_cast<long long>(id), status.message().c_str());
      c.stored = Chat::Stored::Unknown;
    }
  }
  dirty_ids.clear();
}

void chats_set_translate(std::int64_t chat, const std::string &lang) {
  translate_todo.emplace_back(chat, lang);
  mark_dirty(true);
}

void chats_events() {
  for (std::size_t i = 0; i < changed.size(); i += kEventJids) {
    const std::vector<std::int64_t> part(changed.begin() + static_cast<std::ptrdiff_t>(i),
                                         changed.begin() + static_cast<std::ptrdiff_t>(
                                                               std::min(i + kEventJids, changed.size())));
    server.broadcast(td::json_encode<std::string>(td::json_object([&](auto &o) {
      o("ev", "chats");
      o("jids", td::json_array(part, [](std::int64_t id) { return std::to_string(id); }));
    })));
  }
  changed.clear();
  if (flag_written) {  // the whole list is in chats.db for the first time: the linking page can go
    flag_written = false;
    link_syncing(false);
  }
}

void chats_reset() {
  // db.wipe() has emptied the table: the UI is told about the rows that were there.
  for (const auto &entry : chats) {
    if (entry.second.stored == Chat::Stored::Present) {
      changed.push_back(entry.first);
    }
  }
  chats.clear();
  users.clear();
  people_written.clear();  // db.wipe() has emptied the table
  people_todo.clear();
  people_loaded = false;
  forums.clear();
  secret_states.clear();
  downloads.clear();
  user_downloads.clear();
  unknown_chats.clear();
  dirty_ids.clear();
  translate_todo.clear();
  for (int s = 0; s < 3; ++s) {
    scope_mute[s] = 0;
    scope_learned[s] = 0;
    scope_mentions_off[s] = false;
  }
  flag_pending = false;
  flag_written = false;
  mark_dirty();  // for the event
}

void chats_close() {
  insert_row = td::SqliteStatement();
  delete_row = td::SqliteStatement();
  select_row = td::SqliteStatement();
  insert_person = td::SqliteStatement();
}
