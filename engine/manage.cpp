// The requests that act on people, groups and invite links, the secret chats and the chat's timer, the chat's export
// to a text file and the order of the pinned chats: contact.block, group.participants, group.create, poll.create,
// invite.info, invite.accept, secret.start, chat.ttl, chat.export and chat.pinOrder (docs/protocol.md).
// What a chat is comes from pages.cpp (with_chat, person_of, basic_group_of, supergroup_of).

#include "engine.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

namespace {

using td_api::make_object;

constexpr std::size_t kGroupName = 128;  // Telegram's longest group name, in characters (TDLib cuts a longer one without a word)
// poll_answer_count_max in OptionManager.cpp: 12 until Telegram's config says otherwise, and then TDLib refuses.
constexpr std::size_t kPollOptions = 12;
// ponytail: an export keeps its lines in memory (the history comes newest first and the file is oldest first), so it
// stops at the newest 200 000 messages or 24 MiB of text, whichever comes first: the engine's cap is 128 MiB with the
// rlimit-patch addon, and TDLib also holds the messages a walk loads until it unloads them. A longer one would page into
// a temporary file and be reversed on the way out.
constexpr std::size_t kExportMessages = 200000;
constexpr std::size_t kExportBytes = std::size_t{24} << 20;

void td_failed(std::uint64_t conn, const std::string &id, const td_api::error &e) {
  say("td error: %d %s", e.code_, e.message_.c_str());
  reply_td_error(conn, id, e);
}

void bad_chat(std::uint64_t conn, const std::string &id, const char *what) {
  server.reply_error(conn, id, "bad_chat", what);
}

bool has(const td_api::error &e, const char *part) {
  return e.message_.find(part) != std::string::npos;
}

// An error of ours, worded as the td error is: TDLib's code and message.
void refuse(std::uint64_t conn, const std::string &id, const char *code, const td_api::error &e) {
  say("td error: %d %s", e.code_, e.message_.c_str());
  server.reply_error(conn, id, code, std::to_string(e.code_) + " " + e.message_);
}

// ---- contact.block

void contact_block(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  td::JsonValue block;
  if (optional_field(req, "block", td::JsonValue::Type::Boolean, block) != 1) {
    return bad_request(conn, id, "need the field block, true or false");
  }
  with_chat(conn, id, chat, [conn, id, on = block.get_boolean()](const td_api::chat &c) {
    const std::int64_t user = person_of(c);
    if (!user) {
      return bad_chat(conn, id, "not a chat with a person");
    }
    td_api::object_ptr<td_api::BlockList> list;  // null takes the user off the list
    if (on) {
      list = make_object<td_api::blockListMain>();
    }
    tdlib.send(make_object<td_api::setMessageSenderBlockList>(make_object<td_api::messageSenderUser>(user),
                                                              std::move(list)),
               [conn, id](Obj o) { answer(conn, id, o); });
  });
}

// ---- group.participants

// The refusals GroupInfoPage words (refused: 403 their privacy settings, 409 they are in already); the rest is `td`.
void participants_failed(std::uint64_t conn, const std::string &id, const td_api::error &e) {
  if (has(e, "USER_PRIVACY_RESTRICTED")) {
    say("td error: %d %s", e.code_, e.message_.c_str());
    server.reply_error(conn, id, "refused", "403");
  } else if (has(e, "USER_ALREADY_PARTICIPANT")) {
    say("td error: %d %s", e.code_, e.message_.c_str());
    server.reply_error(conn, id, "refused", "409");
  } else {
    td_failed(conn, id, e);
  }
}

// What Telegram's apps give a new administrator: the rights of a moderator, none to add administrators or stay hidden.
// (can_manage_chat is implied by any other, TDLib sets it: DialogParticipant.cpp.)
td_api::object_ptr<td_api::chatAdministratorRights> default_rights() {
  auto rights = make_object<td_api::chatAdministratorRights>();
  rights->can_change_info_ = rights->can_delete_messages_ = rights->can_invite_users_ = true;
  rights->can_restrict_members_ = rights->can_pin_messages_ = rights->can_manage_video_chats_ = true;
  rights->can_manage_topics_ = true;
  return rights;
}

void group_participants(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  auto jid = string_field(req, "jid");
  auto action = string_field(req, "action");
  std::int64_t user = 0;
  if (jid.is_error() || !parse_i64(jid.ok(), user) || user <= 0) {
    return bad_request(conn, id, "need the field jid, a string with a user id");
  }
  if (action.is_error() || (action.ok() != "add" && action.ok() != "remove" && action.ok() != "promote" &&
                            action.ok() != "demote")) {
    return bad_request(conn, id, "need the field action: add, remove, promote or demote");
  }
  with_chat(conn, id, chat, [conn, id, chat, user, what = action.ok()](const td_api::chat &c) {
    if (!basic_group_of(c) && !supergroup_of(c, false)) {
      return bad_chat(conn, id, "not a group");
    }
    if (what == "add") {
      // Telegram answers an add it refused for privacy as a list of who failed, not as an error.
      return tdlib.send(make_object<td_api::addChatMember>(chat, user, 100), [conn, id](Obj o) {
        if (auto e = as_error(o)) {
          return participants_failed(conn, id, *e);
        }
        if (!static_cast<const td_api::failedToAddMembers &>(*o).failed_to_add_members_.empty()) {
          return server.reply_error(conn, id, "refused", "403");
        }
        server.reply_ok(conn, id);
      });
    }
    auto done = [conn, id](Obj o) {
      if (auto e = as_error(o)) {
        return participants_failed(conn, id, *e);
      }
      server.reply_ok(conn, id);
    };
    auto member = make_object<td_api::messageSenderUser>(user);
    if (what == "remove") {
      // 0: for good, as Telegram's apps remove from a supergroup; a basic group just drops the member.
      tdlib.send(make_object<td_api::banChatMember>(chat, std::move(member), 0, false), done);
    } else if (what == "promote") {
      tdlib.send(make_object<td_api::setChatMemberStatus>(
                     chat, std::move(member), make_object<td_api::chatMemberStatusAdministrator>(true, default_rights())),
                 done);
    } else {
      tdlib.send(make_object<td_api::setChatMemberStatus>(chat, std::move(member),
                                                          make_object<td_api::chatMemberStatusMember>(0)),
                 done);
    }
  });
}

// ---- group.create

// The characters of a UTF-8 string: the bytes that do not continue one.
std::size_t characters(const std::string &s) {
  return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) { return (c & 0xC0) != 0x80; }));
}

void group_create(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto name = string_field(req, "name");
  td::JsonValue list;
  if (name.is_error() || optional_field(req, "participants", td::JsonValue::Type::Array, list) != 1) {
    return bad_request(conn, id, "need the fields name, a string, and participants, an array of user ids");
  }
  std::vector<std::int64_t> users;
  for (const td::JsonValue &item : list.get_array()) {
    std::int64_t user = 0;
    if (item.type() != td::JsonValue::Type::String || !parse_i64(item.get_string().str(), user) || user <= 0) {
      return bad_request(conn, id, "participants are strings with user ids");
    }
    users.push_back(user);
  }
  // TDLib trims the name and cuts it at 128 characters without an error, so the length is checked here.
  std::string title = name.move_as_ok();
  const std::size_t first = title.find_first_not_of(" \t\r\n");
  title = first == std::string::npos ? "" : title.substr(first, title.find_last_not_of(" \t\r\n") - first + 1);
  if (title.empty()) {
    return bad_request(conn, id, "name must not be empty");
  }
  if (characters(title) > kGroupName) {
    return server.reply_error(conn, id, "name_too_long", "a group name holds at most 128 characters");
  }
  tdlib.send(make_object<td_api::createNewBasicGroupChat>(std::move(users), title, 0), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    const auto &made = static_cast<const td_api::createdBasicGroupChat &>(*o);
    const std::size_t refused = made.failed_to_add_members_ ? made.failed_to_add_members_->failed_to_add_members_.size() : 0;
    // After the flush: the UI opens the chat from chats.db, and updateNewChat came before this answer.
    queue_reply(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                  r("jid", std::to_string(made.chat_id_));
                  r("refused", static_cast<std::int32_t>(refused));
                })));
  });
}

// ---- chat.pinOrder

// The main list's pinned chats in their new order, top first. TDLib answers with an updateChatPosition for each chat
// that moved (chats.pinned is the position's order), so the rows change with that, not with this reply.
void chat_pin_order(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  td::JsonValue list;
  if (optional_field(req, "ids", td::JsonValue::Type::Array, list) != 1) {
    return bad_request(conn, id, "need the field ids, an array of chat ids");
  }
  std::vector<std::int64_t> chats;
  for (const td::JsonValue &item : list.get_array()) {
    std::int64_t chat = 0;
    if (item.type() != td::JsonValue::Type::String || !parse_i64(item.get_string().str(), chat)) {
      return bad_request(conn, id, "ids are strings with chat ids");
    }
    chats.push_back(chat);
  }
  if (chats.empty()) {  // setPinnedChats with none would unpin them all
    return bad_request(conn, id, "ids holds at least one chat");
  }
  tdlib.send(make_object<td_api::setPinnedChats>(make_object<td_api::chatListMain>(), std::move(chats)),
             [conn, id](Obj o) {
               if (auto e = as_error(o)) {
                 return td_failed(conn, id, *e);
               }
               server.reply_ok(conn, id);
             });
}

// ---- poll.create

void poll_create(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  auto question = string_field(req, "question");
  td::JsonValue list, several;
  if (question.is_error() || question.ok().empty() ||
      optional_field(req, "options", td::JsonValue::Type::Array, list) != 1 ||
      optional_field(req, "multiple", td::JsonValue::Type::Boolean, several) < 0) {
    return bad_request(conn, id,
                       "need the fields question, a string that is not empty, options, an array of strings, and "
                       "multiple, true or false");
  }
  std::vector<std::string> options;
  for (td::JsonValue &item : list.get_array()) {
    if (item.type() != td::JsonValue::Type::String || item.get_string().str().empty()) {
      return bad_request(conn, id, "options are strings that are not empty");
    }
    options.push_back(item.get_string().str());
  }
  if (options.size() < 2 || options.size() > kPollOptions) {
    return bad_request(conn, id, "options holds 2 to 12 strings");
  }
  std::int32_t topic = -1;
  if (!topic_field(conn, id, req, topic)) {
    return;
  }
  const bool multiple = several.type() == td::JsonValue::Type::Boolean && several.get_boolean();
  with_chat(conn, id, chat, [conn, id, chat, topic, multiple, ask = question.ok(), options](const td_api::chat &c) {
    auto text = [](const std::string &s) {
      return make_object<td_api::formattedText>(s, std::vector<td_api::object_ptr<td_api::textEntity>>());
    };
    std::vector<td_api::object_ptr<td_api::inputPollOption>> choices;
    for (const std::string &option : options) {
      choices.push_back(make_object<td_api::inputPollOption>(text(option), nullptr));
    }
    // Anonymous as Telegram's apps make it in a group, and a channel takes no other (MessageContent.cpp: "Non-anonymous
    // polls can't be sent to channel chats"). In a chat with a person it is not: nothing is hidden between two people.
    // TDLib takes a poll there only in a chat with a bot or Saved Messages, and answers "Polls can't be sent to the
    // private chat" (a td error) in any other.
    auto content = make_object<td_api::inputMessagePoll>(
        text(ask), std::move(choices), nullptr, nullptr, !person_of(c), multiple, true, false,
        std::vector<std::string>(), false, false, make_object<td_api::inputPollTypeRegular>(false), 0, 0, false);
    send_content(conn, id, chat, topic, nullptr, std::move(content));
  });
}

// ---- invite.info and invite.accept

// A link Telegram does not have (any more): the server's INVITE_HASH_* or TDLib's own "Wrong invite link", which is
// what it says before asking when it can't read a hash out of the text (DialogInviteLinkManager.cpp).
bool invite_invalid(const td_api::error &e) {
  return has(e, "INVITE_HASH_") || has(e, "Wrong invite link");
}

void invite_failed(std::uint64_t conn, const std::string &id, const td_api::error &e, bool accepting) {
  if (invite_invalid(e)) {
    refuse(conn, id, "invite_invalid", e);
  } else if (accepting && (has(e, "USER_BANNED_IN_CHANNEL") || has(e, "CHANNEL_PRIVATE"))) {
    refuse(conn, id, "invite_not_allowed", e);
  } else {
    td_failed(conn, id, e);
  }
}

void invite_info(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto url = string_field(req, "url");
  if (url.is_error() || url.ok().empty()) {
    return bad_request(conn, id, "need the field url, a t.me invite link");
  }
  tdlib.send(make_object<td_api::checkChatInviteLink>(url.ok()), [conn, id, link = url.ok()](Obj o) {
    if (auto e = as_error(o)) {
      return invite_failed(conn, id, *e, false);
    }
    const auto &info = static_cast<const td_api::chatInviteLinkInfo &>(*o);
    // chat_id is set for a chat we can read already. accessible_for is set when that is only a look (an invite that
    // shows a channel for a while without joining it): not a member then (DialogInviteLinkManager.cpp).
    const bool member = info.chat_id_ != 0 && info.accessible_for_ == 0;
    // After the flush, as a chat the UI may open from chats.db.
    queue_reply(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                  r("member", td::JsonBool(member));
                  r("jid", info.chat_id_ ? std::to_string(info.chat_id_) : std::string());
                  if (!member) {
                    r("kind", info.type_ && info.type_->get_id() == td_api::inviteLinkChatTypeChannel::ID ? "channel"
                                                                                                           : "group");
                    r("code", link);
                    r("name", info.title_);
                    r("count", info.member_count_);
                  }
                })));
  });
}

void invite_accept(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto code = string_field(req, "code");  // kind, jid and name come back too (invite.info's); TDLib needs the link
  if (code.is_error() || code.ok().empty()) {
    return bad_request(conn, id, "need the field code, the link invite.info gave");
  }
  tdlib.send(make_object<td_api::joinChatByInviteLink>(code.move_as_ok()), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return invite_failed(conn, id, *e, true);
    }
    if (o->get_id() == td_api::chatJoinResultSuccess::ID) {
      return queue_reply(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                           r("jid", std::to_string(static_cast<const td_api::chatJoinResultSuccess &>(*o).chat_id_));
                         })));
    }
    if (o->get_id() == td_api::chatJoinResultRequestSent::ID) {
      return server.reply_error(conn, id, "request_sent", "an admin has to let the request in");
    }
    // Declined, or a guard bot's approval, which Tengram can't give.
    server.reply_error(conn, id, "invite_not_allowed", "the link doesn't let this account in");
  });
}

// ---- secret.start and chat.ttl

// A secret chat's timer: TDLib takes any time from 0 and sends it to the other side (set_dialog_message_ttl in
// MessagesManager.cpp checks nothing else); Telegram's apps offer up to a week.
constexpr std::int64_t kSecretTtlMax = 604800;
// Any other chat's auto-delete time: a whole number of days up to a year (td_api.tl, setChatMessageAutoDeleteTime); the
// server refuses the days it has no setting for.
constexpr std::int64_t kAutoDeleteMax = 365 * 86400;

void start_secret(std::uint64_t conn, const std::string &id, std::int64_t user) {
  tdlib.send(make_object<td_api::createNewSecretChat>(user), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      // A user TDLib has no access to (UserManager::get_input_user), or none it can ask: not one to start a chat with.
      if (has(*e, "User not found") || has(*e, "Have no access to the user") || has(*e, "Can't create secret chat")) {
        return refuse(conn, id, "bad_request", *e);
      }
      return td_failed(conn, id, *e);
    }
    // After the flush: the UI opens the chat from chats.db, and updateNewChat came before this answer. The chat is
    // pending (chats.secret 1) until the other side comes online and TDLib's updateSecretChat says ready.
    queue_reply(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                  r("jid", std::to_string(static_cast<const td_api::chat &>(*o).id_));
                })));
  });
}

// `user`, a user id, or `chat`, a chat with a person (whose peer is used); `user` wins when both are there.
void secret_start(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  td::JsonValue by_user, by_chat;
  const int has_user = optional_field(req, "user", td::JsonValue::Type::String, by_user);
  const int has_chat = optional_field(req, "chat", td::JsonValue::Type::String, by_chat);
  std::int64_t n = 0;
  if (has_user < 0 || has_chat < 0 || has_user + has_chat == 0 ||
      !parse_i64(has_user == 1 ? by_user.get_string().str() : by_chat.get_string().str(), n) ||
      (has_user == 1 && n <= 0)) {
    return bad_request(conn, id, "need the field user, a string with a user id, or chat, a chat with a person");
  }
  if (has_user == 1) {
    return start_secret(conn, id, n);
  }
  with_chat(conn, id, n, [conn, id](const td_api::chat &c) {
    const std::int64_t user = person_of(c);
    if (!user) {
      return bad_chat(conn, id, "not a chat with a person");
    }
    start_secret(conn, id, user);
  });
}

void chat_ttl(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  td::JsonValue v;
  std::int64_t secs = 0;
  if (optional_field(req, "secs", td::JsonValue::Type::Number, v) != 1 || !parse_i64(v.get_number().str(), secs) ||
      secs < 0) {
    return bad_request(conn, id, "need the field secs, a whole number of seconds, 0 for off");
  }
  with_chat(conn, id, chat, [conn, id, chat, secs](const td_api::chat &c) {
    const bool secret = c.type_ && c.type_->get_id() == td_api::chatTypeSecret::ID;
    if (secs > (secret ? kSecretTtlMax : kAutoDeleteMax) || (!secret && secs % 86400 != 0)) {
      return bad_request(conn, id, secret ? "secs is 0 to 604800 in a secret chat"
                                          : "secs is 0 or a whole number of days, up to 365");
    }
    tdlib.send(make_object<td_api::setChatMessageAutoDeleteTime>(chat, static_cast<std::int32_t>(secs)),
               [conn, id](Obj o) { answer(conn, id, o); });
  });
}

// ---- chat.export

struct Export {
  std::uint64_t conn;
  std::string id, title;
  std::int64_t chat;
  bool person;                     // a chat with a person: the other side is named by the chat's title
  std::vector<std::string> lines;  // finished lines, newest first as the history comes
  std::size_t bytes = 0;
};

// What a line says: the text or the caption; "<Media omitted>" for a media with neither; a service message as the chat
// list words it, which already holds the name, so `service` is set and the line gets no "Name: " in front.
std::string export_text(const td_api::message &m, bool &service) {
  const std::string body = message_body(m, service);
  if (service || !m.content_) {
    return body;
  }
  if (const td_api::formattedText *text = shown_text(*m.content_); text && !text->text_.empty()) {
    return text->text_;
  }
  switch (m.content_->get_id()) {
    case td_api::messagePhoto::ID:
    case td_api::messageVideo::ID:
    case td_api::messageAnimation::ID:
    case td_api::messageVideoNote::ID:
    case td_api::messageVoiceNote::ID:
    case td_api::messageAudio::ID:
    case td_api::messageDocument::ID:
    case td_api::messageSticker::ID:
      return "<Media omitted>";
    default:
      return body;  // a poll, a place, a contact: as the chat list words them
  }
}

std::string export_sender(const td_api::message &m, const Export &e) {
  if (m.is_outgoing_) {
    return "You";
  }
  if (e.person) {
    return e.title;
  }
  std::string name;
  if (m.sender_id_ && m.sender_id_->get_id() == td_api::messageSenderUser::ID) {
    name = user_name(static_cast<const td_api::messageSenderUser &>(*m.sender_id_).user_id_, true);
  } else if (m.sender_id_) {
    name = chat_title(static_cast<const td_api::messageSenderChat &>(*m.sender_id_).chat_id_);
  }
  return name.empty() ? "Someone" : name;
}

// "dd/mm/yy, HH:MM - Name: text" in local time; a multi-line text keeps its lines.
std::string export_line(const td_api::message &m, const Export &e) {
  bool service = false;
  const std::string text = export_text(m, service);
  char stamp[32] = "";
  const std::time_t at = m.date_;
  struct tm local;
  if (localtime_r(&at, &local)) {
    std::strftime(stamp, sizeof stamp, "%d/%m/%y, %H:%M - ", &local);
  }
  return std::string(stamp) + (service ? std::string() : export_sender(m, e) + ": ") + text + "\n";
}

// The phone's Downloads folder, else the engine's own.
std::string export_dir() {
  static const char *const shared = "/accounts/1000/shared/downloads";
  struct stat st;
  if (stat(shared, &st) == 0 && S_ISDIR(st.st_mode)) {
    return shared;
  }
  const std::string own = data_dir + "/downloads";
  mkdir(own.c_str(), 0700);  // there already is fine; a folder that can't be made fails the write below
  return own;
}

void export_done(const std::shared_ptr<Export> &e) {
  std::string name = e->title.empty() ? "chat" : e->title;
  for (char &c : name) {
    if (static_cast<unsigned char>(c) < 0x20 || std::strchr("/\\:*?\"<>|", c)) {
      c = '_';
    }
  }
  const std::string path = export_dir() + "/Telegram Chat with " + name + ".txt";
  // The oldest line first. Written line by line: one string of the whole chat would double the memory.
  std::FILE *f = std::fopen(path.c_str(), "w");
  bool ok = f != nullptr;
  for (auto line = e->lines.rbegin(); ok && line != e->lines.rend(); ++line) {
    ok = std::fwrite(line->data(), 1, line->size(), f) == line->size();
  }
  if (f && std::fclose(f) != 0) {
    ok = false;
  }
  if (!ok) {
    say("chat.export: could not write the file");
    return server.reply_error(e->conn, e->id, "write_failed", "could not write " + path);
  }
  chmod(path.c_str(), 0644);  // the engine's umask is 077; the file is for the user's other apps
  say("chat.export: %ld lines", static_cast<long>(e->lines.size()));
  server.reply_ok(e->conn, e->id, path_json(path));
}

// One page of the history, from the message before `from` (0: the newest) down.
void export_page(const std::shared_ptr<Export> &e, std::int64_t from) {
  tdlib.send(make_object<td_api::getChatHistory>(e->chat, from, 0, 100, false), [e, from](Obj o) {
    if (!server.alive(e->conn)) {  // the UI went away: no reason to walk the rest
      return;
    }
    if (auto err = as_error(o)) {
      return td_failed(e->conn, e->id, *err);
    }
    // TDLib may give fewer than 100 (often one at first), and may give `from` itself; only an empty page is the end.
    std::int64_t oldest = 0;
    for (const auto &m : static_cast<const td_api::messages &>(*o).messages_) {
      if (m && (from == 0 || m->id_ < from)) {
        e->lines.push_back(export_line(*m, *e));
        e->bytes += e->lines.back().size();
        oldest = m->id_;
      }
    }
    if (oldest == 0 || e->lines.size() >= kExportMessages || e->bytes >= kExportBytes) {
      return export_done(e);
    }
    export_page(e, oldest);
  });
}

void chat_export(std::uint64_t conn, const std::string &id, std::int64_t chat) {
  with_chat(conn, id, chat, [conn, id, chat](const td_api::chat &c) {
    auto e = std::make_shared<Export>();
    e->conn = conn;
    e->id = id;
    e->title = c.title_;
    e->chat = chat;
    e->person = person_of(c) != 0;
    export_page(e, 0);
  });
}

}  // namespace

bool manage_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  static const char *const ours[] = {"contact.block", "group.participants", "group.create", "poll.create",
                                     "invite.info",   "invite.accept",      "secret.start", "chat.ttl",
                                     "chat.export",   "chat.pinOrder"};
  if (std::find(std::begin(ours), std::end(ours), cmd) == std::end(ours)) {
    return false;
  }
  if (cmd == "group.create" || cmd == "invite.info" || cmd == "invite.accept" || cmd == "secret.start" ||
      cmd == "chat.pinOrder") {  // no chat
    if (!link_ready()) {
      server.reply_error(conn, id, "unlinked", "not logged in");
    } else if (cmd == "group.create") {
      group_create(conn, id, req);
    } else if (cmd == "invite.info") {
      invite_info(conn, id, req);
    } else if (cmd == "secret.start") {
      secret_start(conn, id, req);
    } else if (cmd == "chat.pinOrder") {
      chat_pin_order(conn, id, req);
    } else {
      invite_accept(conn, id, req);
    }
    return true;
  }
  std::int64_t chat = 0;
  if (!request_chat(conn, id, req, chat)) {
    return true;
  }
  if (cmd == "contact.block") {
    contact_block(conn, id, req, chat);
  } else if (cmd == "group.participants") {
    group_participants(conn, id, req, chat);
  } else if (cmd == "poll.create") {
    poll_create(conn, id, req, chat);
  } else if (cmd == "chat.ttl") {
    chat_ttl(conn, id, req, chat);
  } else {
    chat_export(conn, id, chat);
  }
  return true;
}
