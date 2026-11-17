// What the UI's open chat shows live: who is typing, and the other person's presence in a 1:1 chat. Only for the
// chats between their chat.open and their chat.close; a connection that closes closes what it opened.

#include "engine.h"

#include <ctime>
#include <map>
#include <set>
#include <unordered_map>

namespace {

// What presence needs of a user, from updateUser and updateUserStatus.
struct Status {
  std::int64_t online_until = 0;  // userStatusOnline: when it expires, unix seconds
  std::int64_t last_seen = 0;     // userStatusOffline: was_online_
  bool bot = false;
};
std::unordered_map<std::int64_t, Status> users;
std::map<std::int64_t, std::set<std::uint64_t>> open;  // chat -> the connections that have it open

void set_status(Status &s, const td_api::UserStatus *status) {
  s.online_until = s.last_seen = 0;
  if (status && status->get_id() == td_api::userStatusOnline::ID) {
    s.online_until = static_cast<const td_api::userStatusOnline &>(*status).expires_;
  } else if (status && status->get_id() == td_api::userStatusOffline::ID) {
    s.last_seen = static_cast<const td_api::userStatusOffline &>(*status).was_online_;
  }
}

// The presence event of a private chat that is open, unless it is our own Saved Messages or a bot's.
void send_presence(std::int64_t chat) {
  ChatBrief brief = {};
  if (!chat_brief(chat, brief) || brief.peer == 0 || brief.peer == link_me_id()) {
    return;
  }
  const Status s = users.count(brief.peer) ? users[brief.peer] : Status();
  if (s.bot) {
    return;
  }
  const bool online = s.online_until > std::time(nullptr);
  server.broadcast(td::json_encode<std::string>(td::json_object([&](auto &o) {
    o("ev", "presence");
    o("chat", std::to_string(chat));
    o("online", td::JsonBool(online));
    o("lastSeen", online ? std::int64_t{0} : s.last_seen);
  })));
}

void send_typing(const td_api::updateChatAction &u) {
  bool on = true, voice = false;
  switch (u.action_ ? u.action_->get_id() : 0) {
    case td_api::chatActionTyping::ID:
      break;
    case td_api::chatActionRecordingVoiceNote::ID:
      voice = true;
      break;
    case td_api::chatActionCancel::ID:
      on = false;
      break;
    default:  // uploading, choosing a sticker...: not shown
      return;
  }
  ChatBrief brief = {};
  std::string who;
  if (chat_brief(u.chat_id_, brief) && brief.kind == 1 && u.sender_id_ &&
      u.sender_id_->get_id() == td_api::messageSenderUser::ID) {
    who = user_name(static_cast<const td_api::messageSenderUser &>(*u.sender_id_).user_id_, false);
  }
  server.broadcast(td::json_encode<std::string>(td::json_object([&](auto &o) {
    o("ev", "typing");
    o("chat", std::to_string(u.chat_id_));
    o("who", who);
    o("on", td::JsonBool(on));
    o("voice", td::JsonBool(voice));
  })));
}

}  // namespace

void live_open(std::uint64_t conn, std::int64_t chat) {
  if (!server.alive(conn)) {  // it went away while TDLib was opening the chat
    tdlib.send(td_api::make_object<td_api::closeChat>(chat));
    return;
  }
  open[chat].insert(conn);
  send_presence(chat);
}

void live_close(std::uint64_t conn, std::int64_t chat) {
  auto it = open.find(chat);
  if (it != open.end() && it->second.erase(conn) && it->second.empty()) {
    open.erase(it);
  }
}

void live_conn_closed(std::uint64_t conn) {
  for (auto it = open.begin(); it != open.end();) {
    if (it->second.erase(conn) && it->second.empty()) {
      tdlib.send(td_api::make_object<td_api::closeChat>(it->first));  // the UI never said so
      it = open.erase(it);
    } else {
      ++it;
    }
  }
}

void live_reset() {
  open.clear();
  users.clear();
}

void live_update(td_api::Object &update) {
  switch (update.get_id()) {
    case td_api::updateUser::ID: {
      const auto &user = static_cast<td_api::updateUser &>(update).user_;
      if (user) {
        Status &s = users[user->id_];
        set_status(s, user->status_.get());
        s.bot = user->type_ && user->type_->get_id() == td_api::userTypeBot::ID;
      }
      break;
    }
    case td_api::updateUserStatus::ID: {
      auto &u = static_cast<td_api::updateUserStatus &>(update);
      set_status(users[u.user_id_], u.status_.get());
      for (const auto &entry : open) {
        ChatBrief brief = {};
        if (chat_brief(entry.first, brief) && brief.peer == u.user_id_) {
          send_presence(entry.first);
        }
      }
      break;
    }
    case td_api::updateChatAction::ID: {
      auto &u = static_cast<td_api::updateChatAction &>(update);
      if (open.count(u.chat_id_)) {
        send_typing(u);
      }
      break;
    }
    default:
      break;
  }
}
