// What a message says, as one piece of text: the body of a chat's preview now, and the text of a row of
// `messages` in the next step.

#include "engine.h"

#include <cstdio>

namespace {

using td_api::object_ptr;

// A caption, when there is one.
const std::string &caption(const object_ptr<td_api::formattedText> &t) {
  static const std::string none;
  return t ? t->text_ : none;
}

std::string or_default(const std::string &text, const char *fallback) {
  return text.empty() ? fallback : text;
}

// A timer as Telegram's apps word it: the largest unit that divides it, "5 seconds", "1 day", "1 month" (31 days: the
// longest auto-delete time).
std::string span(int secs) {
  static const struct {
    int length;
    const char *one, *many;
  } units[] = {{2678400, "month", "months"}, {604800, "week", "weeks"}, {86400, "day", "days"},
               {3600, "hour", "hours"},      {60, "minute", "minutes"}, {1, "second", "seconds"}};
  for (const auto &u : units) {
    if (secs % u.length == 0) {
      const int n = secs / u.length;
      return std::to_string(n) + " " + (n == 1 ? u.one : u.many);
    }
  }
  return std::to_string(secs) + " seconds";  // not reached: every time is a whole number of seconds
}

// A distance as the proximity alert words it: "800 m", "1 km", "1.5 km".
std::string distance_text(int metres) {
  if (metres < 1000) {
    return std::to_string(metres) + " m";
  }
  const int tenths = (metres + 50) / 100;  // kilometres in tenths, rounded
  return std::to_string(tenths / 10) + (tenths % 10 ? "." + std::to_string(tenths % 10) : "") + " km";
}

// The service messages: "Anna joined", "Anna removed Bob". Unknown people read "Someone".
std::string service_text(const td_api::message &m, const std::string &who, std::int64_t sender_id) {
  auto name_of = [](std::int64_t user_id) { return or_default(user_name(user_id, false), "Someone"); };
  switch (m.content_->get_id()) {
    case td_api::messageChatAddMembers::ID: {
      auto &ids = static_cast<const td_api::messageChatAddMembers &>(*m.content_).member_user_ids_;
      if (ids.size() == 1 && ids[0] == sender_id) {
        return who + " joined";
      }
      if (ids.size() == 1) {
        return who + " added " + name_of(ids[0]);
      }
      return who + " added " + std::to_string(ids.size()) + " people";
    }
    case td_api::messageChatJoinByLink::ID:
    case td_api::messageChatJoinByRequest::ID:
      return who + " joined";
    case td_api::messageChatDeleteMember::ID: {
      const std::int64_t gone = static_cast<const td_api::messageChatDeleteMember &>(*m.content_).user_id_;
      return gone == sender_id ? who + " left" : who + " removed " + name_of(gone);
    }
    case td_api::messageChatChangeTitle::ID:
      return who + " changed the name to " + static_cast<const td_api::messageChatChangeTitle &>(*m.content_).title_;
    case td_api::messageChatChangePhoto::ID:
      return who + " changed the photo";
    case td_api::messageChatDeletePhoto::ID:
      return who + " removed the photo";
    case td_api::messageBasicGroupChatCreate::ID:
    case td_api::messageSupergroupChatCreate::ID:
      return m.is_channel_post_ ? "Channel created" : who + " created the group";
    case td_api::messagePinMessage::ID:
      return who + " pinned a message";
    case td_api::messageContactRegistered::ID:
      return who + " joined Telegram";
    case td_api::messageScreenshotTaken::ID:  // a secret chat's
      return who + " took a screenshot!";
    case td_api::messageChatSetMessageAutoDeleteTime::ID: {  // a secret chat's timer is a self-destruct timer
      const auto &timer = static_cast<const td_api::messageChatSetMessageAutoDeleteTime &>(*m.content_);
      const int secs = timer.message_auto_delete_time_;
      ChatBrief chat = {};
      const bool secret = chat_brief(m.chat_id_, chat) && chat.secret != 0;
      if (secs <= 0) {
        return who + (secret ? " disabled the self-destruct timer" : " disabled the auto-delete timer");
      }
      return who + (secret ? " set the self-destruct timer to " : " set messages to auto-delete in ") + span(secs);
    }
    case td_api::messageProximityAlertTriggered::ID: {
      const auto &alert = static_cast<const td_api::messageProximityAlertTriggered &>(*m.content_);
      const std::int64_t me = link_me_id();
      // A sender is us when it is a user with our id; a chat's name is its title.
      auto is_me = [me](const td_api::MessageSender *s) {
        return s && s->get_id() == td_api::messageSenderUser::ID &&
               static_cast<const td_api::messageSenderUser &>(*s).user_id_ == me;
      };
      auto who_is = [&](const td_api::MessageSender *s) {
        if (s && s->get_id() == td_api::messageSenderUser::ID) {
          return name_of(static_cast<const td_api::messageSenderUser &>(*s).user_id_);
        }
        return s ? or_default(chat_title(static_cast<const td_api::messageSenderChat &>(*s).chat_id_), "Someone")
                 : std::string("Someone");
      };
      const std::string d = distance_text(alert.distance_);
      if (is_me(alert.watcher_id_.get())) {
        return who_is(alert.traveler_id_.get()) + " is within " + d + " of you";
      }
      if (is_me(alert.traveler_id_.get())) {
        return "You are within " + d + " of " + who_is(alert.watcher_id_.get());
      }
      return who_is(alert.traveler_id_.get()) + " is within " + d + " of " + who_is(alert.watcher_id_.get());
    }
    default:
      return "";
  }
}

}  // namespace

std::string sender_name(const td_api::message &m) {
  if (!m.sender_id_) {
    return "";
  }
  if (m.sender_id_->get_id() == td_api::messageSenderUser::ID) {
    return user_name(static_cast<const td_api::messageSenderUser &>(*m.sender_id_).user_id_, false);
  }
  return chat_title(static_cast<const td_api::messageSenderChat &>(*m.sender_id_).chat_id_);
}

bool call_missed(const td_api::message &m) {
  const auto &call = static_cast<const td_api::messageCall &>(*m.content_);
  const int why = call.discard_reason_ ? call.discard_reason_->get_id() : 0;
  return !m.is_outgoing_ &&
         (why == td_api::callDiscardReasonMissed::ID || why == td_api::callDiscardReasonDeclined::ID);
}

std::string message_body(const td_api::message &m, bool &is_service) {
  is_service = false;
  if (!m.content_) {
    return "Message";
  }
  const td_api::MessageContent &c = *m.content_;
  switch (c.get_id()) {
    case td_api::messageText::ID: {
      auto &t = static_cast<const td_api::messageText &>(c);
      return t.text_ ? t.text_->text_ : "";
    }
    case td_api::messagePhoto::ID:
      return or_default(caption(static_cast<const td_api::messagePhoto &>(c).caption_), "Photo");
    case td_api::messageVideo::ID:
      return or_default(caption(static_cast<const td_api::messageVideo &>(c).caption_), "Video");
    case td_api::messageAnimation::ID:
      return or_default(caption(static_cast<const td_api::messageAnimation &>(c).caption_), "GIF");
    case td_api::messageVideoNote::ID:
      return "Video message";
    case td_api::messageVoiceNote::ID: {
      auto &v = static_cast<const td_api::messageVoiceNote &>(c);
      const int secs = v.voice_note_ ? v.voice_note_->duration_ : 0;
      char buf[32];
      std::snprintf(buf, sizeof buf, "\xF0\x9F\x8E\xA4 %d:%02d", secs / 60, secs % 60);  // microphone
      return or_default(caption(v.caption_), buf);
    }
    case td_api::messageAudio::ID: {
      auto &a = static_cast<const td_api::messageAudio &>(c);
      if (!caption(a.caption_).empty()) {
        return caption(a.caption_);
      }
      if (a.audio_ && !a.audio_->performer_.empty() && !a.audio_->title_.empty()) {
        return a.audio_->performer_ + " \xE2\x80\x93 " + a.audio_->title_;  // en dash
      }
      return "Audio";
    }
    case td_api::messageDocument::ID: {
      auto &d = static_cast<const td_api::messageDocument &>(c);
      if (!caption(d.caption_).empty()) {
        return caption(d.caption_);
      }
      return d.document_ ? or_default(d.document_->file_name_, "Document") : "Document";
    }
    case td_api::messageSticker::ID: {
      auto &s = static_cast<const td_api::messageSticker &>(c);
      return s.sticker_ && !s.sticker_->emoji_.empty() ? s.sticker_->emoji_ + " Sticker" : "Sticker";
    }
    case td_api::messageAnimatedEmoji::ID:
      return static_cast<const td_api::messageAnimatedEmoji &>(c).emoji_;  // a text of one emoji, as it was written
    case td_api::messageLocation::ID:
      return "Location";
    case td_api::messageLiveLocation::ID:
      return "Live location";
    case td_api::messageVenue::ID: {
      auto &v = static_cast<const td_api::messageVenue &>(c);
      return or_default(v.venue_ ? v.venue_->title_ : "", "Location");
    }
    case td_api::messageContact::ID: {
      auto &k = static_cast<const td_api::messageContact &>(c);
      if (!k.contact_) {
        return "Contact";
      }
      const auto &name = *k.contact_;
      return "Contact: " + name.first_name_ + (name.last_name_.empty() ? "" : " " + name.last_name_);
    }
    case td_api::messagePoll::ID: {
      auto &p = static_cast<const td_api::messagePoll &>(c);
      return "Poll: " + (p.poll_ ? caption(p.poll_->question_) : std::string());
    }
    case td_api::messageCall::ID: {
      const bool video = static_cast<const td_api::messageCall &>(c).is_video_;
      is_service = true;  // no sender in front of it, like the service messages
      if (call_missed(m)) {
        return video ? "Missed video call" : "Missed call";
      }
      return video ? "Video call" : "Call";
    }
    default:
      break;
  }
  const std::int64_t sender_id =
      m.sender_id_ && m.sender_id_->get_id() == td_api::messageSenderUser::ID
          ? static_cast<const td_api::messageSenderUser &>(*m.sender_id_).user_id_
          : 0;
  const std::string name = m.is_outgoing_ ? "You" : or_default(sender_name(m), "Someone");
  std::string text = service_text(m, name, sender_id);
  if (text.empty()) {
    return "Message";
  }
  is_service = true;
  return text;
}
