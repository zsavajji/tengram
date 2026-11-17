// The `messages` table (docs/protocol.md): a row from a TDLib message, the updates that change rows, and the
// writes. Nothing is written when an update arrives: it queues an operation, and flush() (main.cpp) runs the
// queue in one transaction, then sends the `msgs` events and the replies that waited for the commit.
//
// A row is written only when the table does not already hold the same one: `sig`, a hash of the row's other
// values, is compared first.

#include "engine.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <variant>

namespace {

using td_api::object_ptr;

constexpr std::size_t kEventIds = 200;  // most ids in one `msgs` event

// One column's value: NULL, an integer, text or a blob.
struct Blob {
  std::string data;
};
using Val = std::variant<std::monostate, std::int64_t, std::string, Blob>;

// The columns that are written, in table order (`deleted`, `media` and `starred` keep their defaults, `sig` is last).
enum Col {
  kChat, kId, kTs, kOut, kState, kSender, kSenderName, kKind, kText, kQuoteId, kQuotePreview, kEdited, kDuration,
  kWaveform, kMediaState, kPlayed, kThumb, kWidth, kHeight, kSize, kPath, kExtra, kCols
};

struct Row {
  std::int64_t chat = 0, id = 0;
  Val v[kCols];
};

// ---- the row of a message

void set_text(Row &r, Col c, const std::string &s) {  // "" is NULL
  if (!s.empty()) {
    r.v[c] = s;
  }
}

const std::string &caption(const object_ptr<td_api::formattedText> &t) {
  static const std::string none;
  return t ? t->text_ : none;
}

void set_file(Row &r, const td_api::file *f) {
  if (!f) {
    return;
  }
  if (f->expected_size_ > 0) {
    r.v[kSize] = f->expected_size_;
  }
  if (f->local_ && f->local_->is_downloading_completed_) {
    r.v[kMediaState] = 2;
    r.v[kPath] = f->local_->path_;
  } else {
    r.v[kMediaState] = 1;
  }
}

void set_thumb(Row &r, const td_api::minithumbnail *t) {
  if (t && !t->data_.empty()) {
    r.v[kThumb] = Blob{t->data_};
  }
}

// extra: a JSON object with what `fill` puts in it, or NULL when that is nothing.
template <class F>
Val extra_of(F &&fill) {
  std::string s = td::json_encode<std::string>(td::json_object(std::forward<F>(fill)));
  return s == "{}" ? Val() : Val(std::move(s));
}

// The code of an entity type we keep (docs/protocol.md, `extra.ent`) and its argument, if it has one. False for the
// types that are left out.
bool entity_code(const td_api::TextEntityType &t, const char *&code, std::string &argument, bool &has_argument) {
  has_argument = false;
  switch (t.get_id()) {
    case td_api::textEntityTypeBold::ID:
      code = "b";
      return true;
    case td_api::textEntityTypeItalic::ID:
      code = "i";
      return true;
    case td_api::textEntityTypeUnderline::ID:
      code = "u";
      return true;
    case td_api::textEntityTypeStrikethrough::ID:
      code = "s";
      return true;
    case td_api::textEntityTypeSpoiler::ID:
      code = "sp";
      return true;
    case td_api::textEntityTypeCode::ID:
      code = "c";
      return true;
    case td_api::textEntityTypePre::ID:
      code = "p";
      return true;
    case td_api::textEntityTypePreCode::ID:
      code = "p";
      argument = static_cast<const td_api::textEntityTypePreCode &>(t).language_;
      has_argument = !argument.empty();
      return true;
    case td_api::textEntityTypeBlockQuote::ID:
    case td_api::textEntityTypeExpandableBlockQuote::ID:
      code = "q";
      return true;
    case td_api::textEntityTypeTextUrl::ID:
      code = "a";
      argument = static_cast<const td_api::textEntityTypeTextUrl &>(t).url_;
      has_argument = true;
      return true;
    case td_api::textEntityTypeUrl::ID:
      code = "a";
      return true;
    case td_api::textEntityTypeMention::ID:
      code = "m";
      return true;
    case td_api::textEntityTypeMentionName::ID:
      code = "m";
      argument = std::to_string(static_cast<const td_api::textEntityTypeMentionName &>(t).user_id_);
      has_argument = true;
      return true;
    case td_api::textEntityTypeHashtag::ID:
      code = "h";
      return true;
    case td_api::textEntityTypeBotCommand::ID:
      code = "cmd";
      return true;
    case td_api::textEntityTypeEmailAddress::ID:
      code = "e";
      return true;
    case td_api::textEntityTypePhoneNumber::ID:
      code = "t";
      return true;
    default:
      return false;
  }
}

// The formatting of the text a row shows goes into extra as `ent`: [offset, length, code(, argument)] for each
// entity we keep, offsets and lengths as TDLib gives them (UTF-16 code units). extra is a JSON object this file has
// just encoded (never "{}": that is NULL), so `ent` is added before its closing brace.
void add_entities(Row &r, const td_api::formattedText *text) {
  if (!text) {
    return;
  }
  bool any = false;
  const std::string list = td::json_encode<std::string>(td::json_array([&](auto &all) {
    for (const auto &e : text->entities_) {
      const char *code = nullptr;
      std::string argument;
      bool has_argument = false;
      if (e && e->type_ && entity_code(*e->type_, code, argument, has_argument)) {
        any = true;
        all(td::json_array([&](auto &one) {
          one(e->offset_);
          one(e->length_);
          one(code);
          if (has_argument) {
            one(argument);
          }
        }));
      }
    }
  }));
  if (!any) {
    return;
  }
  if (auto *old = std::get_if<std::string>(&r.v[kExtra])) {
    old->pop_back();
    *old += ",\"ent\":" + list + "}";
  } else {
    r.v[kExtra] = "{\"ent\":" + list + "}";
  }
}

// The text, of those kinds of content that show a formatted one (a document's is its caption), or nullptr.
const td_api::formattedText *shown_text(const td_api::MessageContent &c) {
  switch (c.get_id()) {
    case td_api::messageText::ID:
      return static_cast<const td_api::messageText &>(c).text_.get();
    case td_api::messagePhoto::ID:
      return static_cast<const td_api::messagePhoto &>(c).caption_.get();
    case td_api::messageVideo::ID:
      return static_cast<const td_api::messageVideo &>(c).caption_.get();
    case td_api::messageAnimation::ID:
      return static_cast<const td_api::messageAnimation &>(c).caption_.get();
    case td_api::messageVoiceNote::ID:
      return static_cast<const td_api::messageVoiceNote &>(c).caption_.get();
    case td_api::messageAudio::ID:
      return static_cast<const td_api::messageAudio &>(c).caption_.get();
    case td_api::messageDocument::ID:
      return static_cast<const td_api::messageDocument &>(c).caption_.get();
    default:
      return nullptr;
  }
}

const td_api::photoSize *largest(const td_api::photo &p) {
  const td_api::photoSize *best = nullptr;
  for (const auto &s : p.sizes_) {
    if (s && (!best || std::int64_t{s->width_} * s->height_ > std::int64_t{best->width_} * best->height_)) {
      best = s.get();
    }
  }
  return best;
}

// TDLib's waveform is 5-bit samples packed least significant bit first (sample i is bits 5i to 5i+4 of the
// byte stream). The row has 64 bytes of 0 to 100: sample i*N/64 of the N there are, scaled. "" when empty.
std::string waveform64(const std::string &bits) {
  const std::size_t n = bits.size() * 8 / 5;
  if (n == 0) {
    return "";
  }
  std::string out(64, '\0');
  for (std::size_t i = 0; i < 64; ++i) {
    const std::size_t first = i * n / 64 * 5;
    int sample = 0;
    for (std::size_t bit = 0; bit < 5; ++bit) {
      const std::size_t k = first + bit;
      sample |= ((static_cast<unsigned char>(bits[k / 8]) >> (k % 8)) & 1) << bit;
    }
    out[i] = static_cast<char>(sample * 100 / 31);
  }
  return out;
}

void content(Row &r, const td_api::message &m) {
  r.v[kKind] = 7;
  r.v[kText] = std::string("Message");
  if (!m.content_) {
    return;
  }
  const td_api::MessageContent &c = *m.content_;
  switch (c.get_id()) {
    case td_api::messageText::ID: {
      auto &t = static_cast<const td_api::messageText &>(c);
      r.v[kKind] = 0;
      r.v[kText] = caption(t.text_);
      if (const auto *p = t.link_preview_.get()) {
        r.v[kExtra] = extra_of([&](auto &o) {
          if (!p->title_.empty()) {
            o("title", p->title_);
          }
          if (!caption(p->description_).empty()) {
            o("desc", caption(p->description_));
          }
          if (!p->url_.empty()) {
            o("url", p->url_);
          }
        });
      }
      break;
    }
    case td_api::messagePhoto::ID: {
      auto &p = static_cast<const td_api::messagePhoto &>(c);
      r.v[kKind] = 1;
      r.v[kText] = Val();
      set_text(r, kText, caption(p.caption_));
      if (p.photo_) {
        set_thumb(r, p.photo_->minithumbnail_.get());
        if (const auto *big = largest(*p.photo_)) {
          r.v[kWidth] = big->width_;
          r.v[kHeight] = big->height_;
        }
      }
      break;
    }
    case td_api::messageVideo::ID: {
      auto &p = static_cast<const td_api::messageVideo &>(c);
      r.v[kKind] = 2;
      r.v[kText] = Val();
      set_text(r, kText, caption(p.caption_));
      if (const auto *v = p.video_.get()) {
        r.v[kDuration] = v->duration_;
        r.v[kWidth] = v->width_;
        r.v[kHeight] = v->height_;
        set_thumb(r, v->minithumbnail_.get());
      }
      break;
    }
    case td_api::messageAnimation::ID: {
      auto &p = static_cast<const td_api::messageAnimation &>(c);
      r.v[kKind] = 2;
      r.v[kText] = Val();
      set_text(r, kText, caption(p.caption_));
      r.v[kExtra] = extra_of([](auto &o) { o("gif", td::JsonTrue()); });
      if (const auto *a = p.animation_.get()) {
        r.v[kDuration] = a->duration_;
        r.v[kWidth] = a->width_;
        r.v[kHeight] = a->height_;
        set_thumb(r, a->minithumbnail_.get());
      }
      break;
    }
    case td_api::messageVideoNote::ID: {
      auto &p = static_cast<const td_api::messageVideoNote &>(c);
      r.v[kKind] = 2;
      r.v[kText] = Val();
      r.v[kPlayed] = p.is_viewed_ ? 1 : 0;
      if (const auto *v = p.video_note_.get()) {
        r.v[kDuration] = v->duration_;
        r.v[kWidth] = v->length_;
        r.v[kHeight] = v->length_;
        set_thumb(r, v->minithumbnail_.get());
      }
      break;
    }
    case td_api::messageVoiceNote::ID: {
      auto &p = static_cast<const td_api::messageVoiceNote &>(c);
      r.v[kKind] = 3;
      r.v[kText] = Val();
      set_text(r, kText, caption(p.caption_));
      r.v[kPlayed] = p.is_listened_ ? 1 : 0;
      if (const auto *v = p.voice_note_.get()) {
        r.v[kDuration] = v->duration_;
        const std::string wave = waveform64(v->waveform_);
        if (!wave.empty()) {
          r.v[kWaveform] = Blob{wave};
        }
      }
      break;
    }
    case td_api::messageDocument::ID: {
      auto &p = static_cast<const td_api::messageDocument &>(c);
      r.v[kKind] = 4;
      r.v[kText] = Val();
      if (!caption(p.caption_).empty()) {
        r.v[kExtra] = extra_of([&](auto &o) { o("caption", caption(p.caption_)); });
      }
      if (const auto *d = p.document_.get()) {
        set_text(r, kText, d->file_name_);
        set_thumb(r, d->minithumbnail_.get());
      }
      break;
    }
    case td_api::messageSticker::ID: {
      auto &p = static_cast<const td_api::messageSticker &>(c);
      r.v[kKind] = 5;
      r.v[kText] = Val();
      if (const auto *s = p.sticker_.get()) {
        r.v[kWidth] = s->width_;
        r.v[kHeight] = s->height_;
        const bool animated = s->format_ && s->format_->get_id() != td_api::stickerFormatWebp::ID;
        r.v[kExtra] = extra_of([&](auto &o) {
          if (!s->emoji_.empty()) {
            o("emoji", s->emoji_);
          }
          if (animated) {
            o("animated", td::JsonTrue());
          }
        });
      }
      break;
    }
    case td_api::messageAudio::ID: {
      auto &p = static_cast<const td_api::messageAudio &>(c);
      r.v[kKind] = 8;
      r.v[kText] = Val();
      set_text(r, kText, caption(p.caption_));
      if (const auto *a = p.audio_.get()) {
        r.v[kDuration] = a->duration_;
        r.v[kExtra] = extra_of([&](auto &o) {
          if (!a->title_.empty()) {
            o("title", a->title_);
          }
          if (!a->performer_.empty()) {
            o("performer", a->performer_);
          }
        });
      }
      break;
    }
    case td_api::messageLocation::ID:
    case td_api::messageVenue::ID: {
      const td_api::location *where = nullptr;
      r.v[kKind] = 9;
      r.v[kText] = Val();
      if (c.get_id() == td_api::messageLocation::ID) {
        where = static_cast<const td_api::messageLocation &>(c).location_.get();
      } else if (const auto *v = static_cast<const td_api::messageVenue &>(c).venue_.get()) {
        where = v->location_.get();
        r.v[kText] = v->title_ + "\n" + v->address_;
      }
      if (where) {
        r.v[kExtra] = extra_of([&](auto &o) {
          o("lat", where->latitude_);
          o("lon", where->longitude_);
        });
      }
      break;
    }
    case td_api::messageContact::ID: {
      auto &p = static_cast<const td_api::messageContact &>(c);
      r.v[kKind] = 10;
      r.v[kText] = Val();
      if (const auto *k = p.contact_.get()) {
        const std::string name = k->first_name_ + (k->last_name_.empty() ? "" : " " + k->last_name_);
        set_text(r, kText, name);
        r.v[kExtra] = extra_of([&](auto &o) {
          o("contacts", td::json_array([&](auto &list) {
              list(td::json_object([&](auto &one) {
                one("name", name);
                one("tel", td::json_array([&](auto &tel) {
                      if (!k->phone_number_.empty()) {
                        tel(k->phone_number_);
                      }
                    }));
              }));
            }));
        });
      }
      break;
    }
    case td_api::messagePoll::ID: {
      auto &p = static_cast<const td_api::messagePoll &>(c);
      r.v[kKind] = 11;
      r.v[kText] = Val();
      if (const auto *poll = p.poll_.get()) {
        set_text(r, kText, caption(poll->question_));
        r.v[kExtra] = extra_of([&](auto &o) {
          o("options", td::json_array([&](auto &list) {
              for (const auto &option : poll->options_) {
                list(option ? caption(option->text_) : std::string());
              }
            }));
          o("max", poll->allows_multiple_answers_ ? 0 : 1);
        });
      }
      break;
    }
    case td_api::messageCall::ID: {
      auto &p = static_cast<const td_api::messageCall &>(c);
      r.v[kKind] = 13;
      const char *what = call_missed(m) ? (p.is_video_ ? "missed_video_call" : "missed_call")
                                        : (p.is_video_ ? "video_call" : "call");
      r.v[kText] = std::string(what);
      if (p.duration_ > 0) {
        r.v[kExtra] = extra_of([&](auto &o) { o("secs", p.duration_); });
      }
      break;
    }
    default: {
      bool service = false;
      const std::string body = message_body(m, service);
      if (service) {
        r.v[kKind] = 6;
        r.v[kText] = body;
      }
      break;
    }
  }
  set_file(r, message_file(m));
  add_entities(r, shown_text(c));
}

Row row_of(const td_api::message &m) {
  Row r;
  r.chat = m.chat_id_;
  r.id = m.id_;
  ChatBrief chat = {};  // a chat that is not mirrored: a person's, nothing read
  chat_brief(m.chat_id_, chat);

  r.v[kChat] = std::to_string(m.chat_id_);
  r.v[kId] = std::to_string(m.id_);
  r.v[kTs] = m.date_;
  r.v[kOut] = m.is_outgoing_ ? 1 : 0;
  r.v[kEdited] = m.edit_date_ > 0 ? 1 : 0;
  r.v[kMediaState] = 0;
  r.v[kPlayed] = 0;

  int state = 0;
  if (m.is_outgoing_) {
    if (m.sending_state_ && m.sending_state_->get_id() == td_api::messageSendingStatePending::ID) {
      state = 0;
    } else if (m.sending_state_) {
      state = 4;
    } else {
      state = m.id_ <= chat.read_outbox ? 3 : 1;
    }
  }
  r.v[kState] = state;

  if (chat.kind == 1 && m.sender_id_) {  // groups only
    if (m.sender_id_->get_id() == td_api::messageSenderUser::ID) {
      const std::int64_t user = static_cast<const td_api::messageSenderUser &>(*m.sender_id_).user_id_;
      r.v[kSender] = std::to_string(user);
      set_text(r, kSenderName, user_name(user, true));
    } else {
      const std::int64_t from = static_cast<const td_api::messageSenderChat &>(*m.sender_id_).chat_id_;
      r.v[kSender] = std::to_string(from);
      set_text(r, kSenderName, chat_title(from));
    }
  }

  if (m.reply_to_ && m.reply_to_->get_id() == td_api::messageReplyToMessage::ID) {
    auto &q = static_cast<const td_api::messageReplyToMessage &>(*m.reply_to_);
    if (q.chat_id_ == m.chat_id_ && q.message_id_ != 0) {  // same chat only
      r.v[kQuoteId] = std::to_string(q.message_id_);
    }
  }

  content(r, m);
  return r;
}

// ---- the table

enum Stmt {
  kInsert, kSelSig, kSelText, kSelKind, kSelPlayed, kDelete, kIdsInRange, kIdsRead, kSetRead, kSetPlayed, kStmts
};

const char *const kSql[kStmts] = {
    "INSERT OR REPLACE INTO messages(chat, id, ts, out, state, sender, sender_name, kind, text, quote_id, "
    "quote_preview, edited, duration, waveform, media_state, played, thumb, width, height, size, path, extra, sig) "
    "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23)",
    "SELECT sig FROM messages WHERE chat = ?1 AND id = ?2",
    "SELECT text FROM messages WHERE chat = ?1 AND id = ?2",
    "SELECT kind FROM messages WHERE chat = ?1 AND id = ?2",
    "SELECT played FROM messages WHERE chat = ?1 AND id = ?2",
    "DELETE FROM messages WHERE chat = ?1 AND id = ?2",
    "SELECT id FROM messages WHERE chat = ?1 AND CAST(id AS INTEGER) >= ?2 AND CAST(id AS INTEGER) < ?3",
    "SELECT id FROM messages WHERE chat = ?1 AND out = 1 AND state = 1 AND CAST(id AS INTEGER) <= ?2",
    "UPDATE messages SET state = 3, sig = NULL WHERE chat = ?1 AND out = 1 AND state = 1 AND CAST(id AS INTEGER) <= ?2",
    "UPDATE messages SET played = 1, sig = NULL WHERE chat = ?1 AND id = ?2",
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

std::vector<std::function<void()>> ops;                    // what flush() is to do, in order
std::set<std::pair<std::int64_t, std::int64_t>> refresh;   // (chat, id) edited: fetched again at the next flush
std::map<std::int64_t, std::vector<std::int64_t>> changed;  // per chat: the ids written or deleted, event to come
struct Reply {
  std::uint64_t conn;
  std::string id, result;
};
std::vector<Reply> replies;

void note(std::int64_t chat, std::int64_t id) {
  changed[chat].push_back(id);
}

void check(const td::Status &s, const char *what) {
  if (s.is_error()) {
    say("chats.db: messages: %s: %s", what, s.message().c_str());
  }
}

// FNV-1a over the row's values, each with its type, so no two different rows differ only by where a boundary is.
std::int64_t sig_of(const Row &r) {
  std::uint64_t h = 14695981039346656037ull;
  auto byte = [&](unsigned char b) {
    h ^= b;
    h *= 1099511628211ull;
  };
  auto number = [&](std::uint64_t n) {
    for (int i = 0; i < 8; ++i) {
      byte(static_cast<unsigned char>(n >> (8 * i)));
    }
  };
  auto bytes = [&](const std::string &s) {
    number(s.size());
    for (char ch : s) {
      byte(static_cast<unsigned char>(ch));
    }
  };
  for (const Val &v : r.v) {
    byte(static_cast<unsigned char>(v.index()));
    if (const auto *n = std::get_if<std::int64_t>(&v)) {
      number(static_cast<std::uint64_t>(*n));
    } else if (const auto *s = std::get_if<std::string>(&v)) {
      bytes(*s);
    } else if (const auto *b = std::get_if<Blob>(&v)) {
      bytes(b->data);
    }
  }
  return static_cast<std::int64_t>(h);
}

td::Status bind_val(td::SqliteStatement &s, int i, const Val &v) {
  if (const auto *n = std::get_if<std::int64_t>(&v)) {
    return s.bind_int64(i, *n);
  }
  if (const auto *t = std::get_if<std::string>(&v)) {
    return s.bind_string(i, *t);
  }
  if (const auto *b = std::get_if<Blob>(&v)) {
    return s.bind_blob(i, b->data);
  }
  return s.bind_null(i);
}

// The chat and the id, bound as the text the columns hold. The strings outlive the step: bind_string does not copy.
struct Key {
  std::string chat, id;
  Key(std::int64_t c, std::int64_t i) : chat(std::to_string(c)), id(std::to_string(i)) {
  }
  td::Status bind(td::SqliteStatement &s) const {
    TRY_STATUS(s.bind_string(1, chat));
    return s.bind_string(2, id);
  }
};

// A statement that is stepped once and reset; `row` tells whether it produced a row.
td::Status step_once(Stmt which, const Key &key, bool &row) {
  auto &s = stmts[which];
  s.reset();
  TRY_STATUS(key.bind(s));
  auto status = s.step();
  row = s.has_row();
  return status;
}

// The one-line text of the message this one replies to, when the table has it.
Val quote_preview(const Row &r) {
  const auto *quoted = std::get_if<std::string>(&r.v[kQuoteId]);
  if (!quoted) {
    return Val();
  }
  const Key key(r.chat, std::stoll(*quoted));
  bool row = false;
  auto status = step_once(kSelText, key, row);
  Val out;
  if (status.is_ok() && row && stmts[kSelText].view_datatype(0) == td::SqliteStatement::Datatype::Text) {
    out = one_line(stmts[kSelText].view_string(0).str());
  }
  stmts[kSelText].reset();
  return out;
}

void put(Row r) {
  r.v[kQuotePreview] = quote_preview(r);
  const std::int64_t sig = sig_of(r);
  const Key key(r.chat, r.id);
  bool row = false;
  check(step_once(kSelSig, key, row), "sig");
  auto &old = stmts[kSelSig];
  const bool same = row && old.view_datatype(0) == td::SqliteStatement::Datatype::Integer && old.view_int64(0) == sig;
  old.reset();
  if (same) {
    return;
  }
  auto &s = stmts[kInsert];
  s.reset();
  td::Status status;
  for (int i = 0; i < kCols && status.is_ok(); ++i) {
    status = bind_val(s, i + 1, r.v[i]);
  }
  if (status.is_ok()) {
    status = s.bind_int64(kCols + 1, sig);
  }
  if (status.is_ok()) {
    status = s.step();
  }
  s.reset();
  check(status, "insert");
  if (status.is_ok()) {
    note(r.chat, r.id);
  }
}

// Deletes the row. The id is named in the event when there was one, or when `always`.
void erase(std::int64_t chat, std::int64_t id, bool always) {
  const Key key(chat, id);
  bool existed = false, unused = false;
  check(step_once(kSelSig, key, existed), "exists");
  stmts[kSelSig].reset();
  if (existed) {
    check(step_once(kDelete, key, unused), "delete");
    stmts[kDelete].reset();
  }
  if (existed || always) {
    note(chat, id);
  }
}

// The ids in the first column of what the statement returns, once its parameters 2 and up are bound by `bind`.
template <class F>
std::vector<std::int64_t> ids_of(Stmt which, std::int64_t chat, F &&bind, const char *what) {
  std::vector<std::int64_t> ids;
  const std::string c = std::to_string(chat);
  auto &s = stmts[which];
  s.reset();
  td::Status status = s.bind_string(1, c);
  if (status.is_ok()) {
    status = bind(s);
  }
  if (status.is_ok()) {
    status = s.step();
  }
  while (status.is_ok() && s.has_row()) {
    ids.push_back(std::stoll(s.view_string(0).str()));
    status = s.step();
  }
  check(status, what);
  s.reset();
  return ids;
}

// The ids of the chat's rows from `oldest` up to, not including, `upper`.
std::vector<std::int64_t> ids_in_range(std::int64_t chat, std::int64_t oldest, std::int64_t upper) {
  return ids_of(kIdsInRange, chat, [&](td::SqliteStatement &s) {
    TRY_STATUS(s.bind_int64(2, oldest));
    return s.bind_int64(3, upper);
  }, "range");
}

void reconcile(std::int64_t chat, const std::vector<Row> &rows, std::int64_t oldest, std::int64_t upper) {
  std::set<std::int64_t> fetched;
  for (const Row &r : rows) {
    fetched.insert(r.id);
    put(r);
  }
  for (std::int64_t id : ids_in_range(chat, oldest, upper)) {
    if (!fetched.count(id)) {
      erase(chat, id, false);
    }
  }
}

// Our messages that were sent (state 1) up to `upto` are read now (state 3). sig becomes NULL for them, so
// that the next time the message comes from TDLib its row is compared with nothing and written again.
void read_outbox(std::int64_t chat, std::int64_t upto) {
  const std::vector<std::int64_t> ids =
      ids_of(kIdsRead, chat, [&](td::SqliteStatement &s) { return s.bind_int64(2, upto); }, "read");
  if (ids.empty()) {
    return;
  }
  ids_of(kSetRead, chat, [&](td::SqliteStatement &s) { return s.bind_int64(2, upto); }, "set read");
  for (std::int64_t id : ids) {
    note(chat, id);
  }
}

void opened(std::int64_t chat, std::int64_t id) {
  const Key key(chat, id);
  bool row = false;
  check(step_once(kSelPlayed, key, row), "played");
  const bool unplayed = row && stmts[kSelPlayed].view_int64(0) == 0;
  stmts[kSelPlayed].reset();
  if (unplayed) {
    check(step_once(kSetPlayed, key, row), "set played");  // sig NULL, as in read_outbox
    stmts[kSetPlayed].reset();
    note(chat, id);
  }
}

}  // namespace

// The file of a message that has one: a photo's largest size, a video, animation, video note, voice note,
// document, audio file or sticker. nullptr for the rest.
const td_api::file *message_file(const td_api::message &m) {
  if (!m.content_) {
    return nullptr;
  }
  const td_api::MessageContent &c = *m.content_;
  switch (c.get_id()) {
    case td_api::messagePhoto::ID: {
      const auto &p = static_cast<const td_api::messagePhoto &>(c);
      const td_api::photoSize *big = p.photo_ ? largest(*p.photo_) : nullptr;
      return big ? big->photo_.get() : nullptr;
    }
    case td_api::messageVideo::ID: {
      const auto &v = static_cast<const td_api::messageVideo &>(c).video_;
      return v ? v->video_.get() : nullptr;
    }
    case td_api::messageAnimation::ID: {
      const auto &a = static_cast<const td_api::messageAnimation &>(c).animation_;
      return a ? a->animation_.get() : nullptr;
    }
    case td_api::messageVideoNote::ID: {
      const auto &v = static_cast<const td_api::messageVideoNote &>(c).video_note_;
      return v ? v->video_.get() : nullptr;
    }
    case td_api::messageVoiceNote::ID: {
      const auto &v = static_cast<const td_api::messageVoiceNote &>(c).voice_note_;
      return v ? v->voice_.get() : nullptr;
    }
    case td_api::messageDocument::ID: {
      const auto &d = static_cast<const td_api::messageDocument &>(c).document_;
      return d ? d->document_.get() : nullptr;
    }
    case td_api::messageAudio::ID: {
      const auto &a = static_cast<const td_api::messageAudio &>(c).audio_;
      return a ? a->audio_.get() : nullptr;
    }
    case td_api::messageSticker::ID: {
      const auto &t = static_cast<const td_api::messageSticker &>(c).sticker_;
      return t ? t->sticker_.get() : nullptr;
    }
    default:
      return nullptr;
  }
}

int message_kind(std::int64_t chat, std::int64_t id) {
  if (!prepare()) {
    return -1;
  }
  bool row = false;
  auto status = step_once(kSelKind, Key(chat, id), row);
  const int kind = status.is_ok() && row ? stmts[kSelKind].view_int32(0) : -1;
  stmts[kSelKind].reset();
  return kind;
}

// ---- queueing

void messages_queue(const td_api::message &m) {
  ops.push_back([row = row_of(m)] { put(row); });
  mark_dirty();
}

void messages_reconcile(std::int64_t chat, const std::vector<td_api::object_ptr<td_api::message>> &page,
                        std::int64_t oldest, std::int64_t upper) {
  auto rows = std::make_shared<std::vector<Row>>();
  for (const auto &m : page) {
    rows->push_back(row_of(*m));
  }
  ops.push_back([=] { reconcile(chat, *rows, oldest, upper); });
  mark_dirty();
}

void queue_reply(std::uint64_t conn, const std::string &id, const std::string &result_json) {
  replies.push_back({conn, id, result_json});
  mark_dirty();
}

void messages_update(td_api::Object &update) {
  switch (update.get_id()) {
    case td_api::updateNewMessage::ID: {
      auto &u = static_cast<td_api::updateNewMessage &>(update);
      if (u.message_) {
        messages_queue(*u.message_);
      }
      break;
    }
    case td_api::updateMessageSendSucceeded::ID:
    case td_api::updateMessageSendFailed::ID: {  // the temporary id's row goes, the message's own is written
      const bool failed = update.get_id() == td_api::updateMessageSendFailed::ID;
      auto &sent = failed ? static_cast<td_api::updateMessageSendFailed &>(update).message_
                          : static_cast<td_api::updateMessageSendSucceeded &>(update).message_;
      const std::int64_t old_id = failed ? static_cast<td_api::updateMessageSendFailed &>(update).old_message_id_
                                         : static_cast<td_api::updateMessageSendSucceeded &>(update).old_message_id_;
      if (!sent) {
        break;
      }
      ops.push_back([chat = sent->chat_id_, old_id, row = row_of(*sent)] {
        erase(chat, old_id, true);
        put(row);
      });
      mark_dirty();
      break;
    }
    case td_api::updateMessageContent::ID: {
      auto &u = static_cast<td_api::updateMessageContent &>(update);
      refresh.insert({u.chat_id_, u.message_id_});
      mark_dirty();
      break;
    }
    case td_api::updateMessageEdited::ID: {
      auto &u = static_cast<td_api::updateMessageEdited &>(update);
      refresh.insert({u.chat_id_, u.message_id_});
      mark_dirty();
      break;
    }
    case td_api::updateDeleteMessages::ID: {
      auto &u = static_cast<td_api::updateDeleteMessages &>(update);
      if (u.is_permanent_) {  // not permanent: TDLib is only unloading its cache
        ops.push_back([chat = u.chat_id_, ids = u.message_ids_] {
          for (std::int64_t id : ids) {
            erase(chat, id, false);
          }
        });
        mark_dirty();
      }
      break;
    }
    case td_api::updateChatReadOutbox::ID: {
      auto &u = static_cast<td_api::updateChatReadOutbox &>(update);
      ops.push_back([chat = u.chat_id_, upto = u.last_read_outbox_message_id_] { read_outbox(chat, upto); });
      mark_dirty();
      break;
    }
    case td_api::updateMessageContentOpened::ID: {
      auto &u = static_cast<td_api::updateMessageContentOpened &>(update);
      ops.push_back([chat = u.chat_id_, id = u.message_id_] { opened(chat, id); });
      mark_dirty();
      break;
    }
    default:
      break;
  }
}

// ---- flush

void messages_write() {
  if (ops.empty() && refresh.empty()) {
    return;
  }
  if (!prepare()) {
    ops.clear();
    refresh.clear();
    return;
  }
  // ponytail: nothing prunes old rows, so every message of every chat is kept: chats.db grows with the history
  // the UI has opened and with every message that arrives. A prune of the oldest rows per chat goes here once
  // the file's size matters.
  auto todo = std::move(ops);
  ops.clear();
  for (auto &op : todo) {
    op();
  }
  // An edited message is fetched again, once for the content update and the edit date together, if we have it.
  for (const auto &[chat, id] : std::exchange(refresh, {})) {
    bool row = false;
    check(step_once(kSelSig, Key(chat, id), row), "exists");
    stmts[kSelSig].reset();
    if (row) {
      tdlib.send(td_api::make_object<td_api::getMessage>(chat, id), [](Obj o) {
        if (as_error(o)) {  // gone in the meantime
          return;
        }
        auto &m = static_cast<td_api::message &>(*o);
        ChatBrief brief = {};
        if (chat_brief(m.chat_id_, brief) && brief.has_last && brief.last_id == m.id_) {
          chats_last_message(m);
        }
        messages_queue(m);
      });
    }
  }
}

void messages_events() {
  for (auto &[chat, ids] : changed) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (std::size_t i = 0; i < ids.size(); i += kEventIds) {
      const auto last = static_cast<std::ptrdiff_t>(std::min(i + kEventIds, ids.size()));
      const std::vector<std::int64_t> part(ids.begin() + static_cast<std::ptrdiff_t>(i), ids.begin() + last);
      server.broadcast(td::json_encode<std::string>(td::json_object([&](auto &o) {
        o("ev", "msgs");
        o("chat", std::to_string(chat));
        o("ids", td::json_array(part, [](std::int64_t id) { return std::to_string(id); }));
      })));
    }
  }
  changed.clear();
}

void messages_replies() {
  for (const Reply &r : std::exchange(replies, {})) {
    server.reply_ok(r.conn, r.id, r.result);
  }
}

void messages_reset() {
  ops.clear();
  refresh.clear();
  changed.clear();
}

void messages_close() {
  for (auto &s : stmts) {
    s = td::SqliteStatement();
  }
}
