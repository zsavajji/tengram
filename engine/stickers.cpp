// The requests behind the sticker picker: stickers.sets, stickers.get, stickers.file, sticker.send and sticker.fave
// (docs/protocol.md, "Stickers"). Only stickers.sets, stickers.get and stickers.file work without a chat. Nothing is
// kept or fetched ahead here: each request asks TDLib when the UI asks, so the engine holds a sticker list only while
// it is being answered.

#include "engine.h"

#include <algorithm>

namespace {

using td_api::make_object;

void td_failed(std::uint64_t conn, const std::string &id, const td_api::error &e) {
  say("td error: %d %s", e.code_, e.message_.c_str());
  reply_td_error(conn, id, e);
}

// One sticker, as the grid takes it. `pic` is the picture the grid shows: the sticker's thumbnail (128 px, so a set
// of 120 stickers stays small) when it is a still one or a TGS (the UI draws its first frame); else the sticker's own
// file when that is a WebP or a TGS; a WebM sticker without a still thumbnail has none. TDLib's file ids last while
// this process runs, which is how long the UI may use them.
auto sticker_object(const td_api::sticker &s) {
  const std::int32_t format = s.format_ ? s.format_->get_id() : 0;
  const td_api::file *pic = nullptr;
  if (s.thumbnail_ && s.thumbnail_->file_ && s.thumbnail_->format_) {
    switch (s.thumbnail_->format_->get_id()) {
      case td_api::thumbnailFormatWebp::ID:
      case td_api::thumbnailFormatJpeg::ID:
      case td_api::thumbnailFormatPng::ID:
      case td_api::thumbnailFormatTgs::ID:
        pic = s.thumbnail_->file_.get();
    }
  }
  if (!pic && (format == td_api::stickerFormatWebp::ID || format == td_api::stickerFormatTgs::ID)) {
    pic = s.sticker_.get();
  }
  return td::json_object([&s, pic, format](auto &o) {
    o("file", std::to_string(s.sticker_->id_));
    o("emoji", s.emoji_);
    o("format", std::string(format == td_api::stickerFormatTgs::ID    ? "tgs"
                            : format == td_api::stickerFormatWebm::ID ? "webm"
                                                                      : "webp"));
    if (pic) {
      o("pic", std::to_string(pic->id_));
      if (pic->local_ && pic->local_->is_downloading_completed_) {
        o("picPath", pic->local_->path_);
      }
    }
  });
}

// The field "file", a TDLib file id (a whole number from 1) in a string. False after the bad_request reply.
bool file_field(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int32_t &file) {
  auto text = string_field(req, "file");
  std::int64_t n = 0;
  if (text.is_error() || !parse_i64(text.ok(), n) || n < 1 || n > INT32_MAX) {
    bad_request(conn, id, "need the field file, a string with a file id");
    return false;
  }
  file = static_cast<std::int32_t>(n);
  return true;
}

void sets_list(std::uint64_t conn, const std::string &id) {
  tdlib.send(make_object<td_api::getInstalledStickerSets>(make_object<td_api::stickerTypeRegular>()),
             [conn, id](Obj o) {
               if (auto e = as_error(o)) {
                 return td_failed(conn, id, *e);
               }
               const auto &list = static_cast<const td_api::stickerSets &>(*o);
               server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                                 r("sets", td::json_array([&](auto &sets) {
                                   for (const auto &s : list.sets_) {
                                     if (!s) {
                                       continue;
                                     }
                                     sets(td::json_object([&](auto &one) {
                                       one("set", std::to_string(s->id_));
                                       one("title", s->title_);
                                       one("count", s->size_);
                                       if (!s->covers_.empty() && s->covers_[0] && s->covers_[0]->sticker_) {
                                         one("cover", sticker_object(*s->covers_[0]));
                                       }
                                     }));
                                   }
                                 }));
                               })));
             });
}

void stickers_get(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto set = string_field(req, "set");
  std::int64_t set_id = 0;
  td_api::object_ptr<td_api::Function> ask;
  if (set.is_error()) {
    return bad_request(conn, id, "need the field set, a string: a set's id, \"recent\" or \"faves\"");
  } else if (set.ok() == "recent") {
    ask = make_object<td_api::getRecentStickers>(false);
  } else if (set.ok() == "faves") {
    ask = make_object<td_api::getFavoriteStickers>();
  } else if (parse_i64(set.ok(), set_id)) {
    ask = make_object<td_api::getStickerSet>(set_id);
  } else {
    return bad_request(conn, id, "set is a set's id, \"recent\" or \"faves\"");
  }
  tdlib.send(std::move(ask), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    // A stickerSet or a stickers: both hold the list as `stickers`, in the order TDLib gives it.
    const auto &list = o->get_id() == td_api::stickerSet::ID ? static_cast<const td_api::stickerSet &>(*o).stickers_
                                                             : static_cast<const td_api::stickers &>(*o).stickers_;
    server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                      r("stickers", td::json_array([&](auto &stickers) {
                        for (const auto &s : list) {
                          if (s && s->sticker_) {
                            stickers(sticker_object(*s));
                          }
                        }
                      }));
                    })));
  });
}

// getFile first: TDLib answers an id it doesn't know with 400, which downloadFile shares with a file it can't fetch.
void stickers_file(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  std::int32_t file = 0;
  if (!file_field(conn, id, req, file)) {
    return;
  }
  auto failed = [conn, id](const std::string &why) {
    say("stickers.file: %s", why.c_str());
    server.reply_error(conn, id, "download_failed", why);
  };
  tdlib.send(make_object<td_api::getFile>(file), [conn, id, file, failed](Obj o) {
    if (auto e = as_error(o)) {
      if (e->code_ == 400 || e->code_ == 404) {
        return server.reply_error(conn, id, "not_found", "no such file");
      }
      return failed(std::to_string(e->code_) + " " + e->message_);
    }
    const auto &here = static_cast<const td_api::file &>(*o);
    if (here.local_ && here.local_->is_downloading_completed_) {
      return server.reply_ok(conn, id, path_json(here.local_->path_));
    }
    // Priority 4, under media.get's 8 for a preview and 16 for a tap: a grid's pictures don't hold up what the user opened.
    tdlib.send(make_object<td_api::downloadFile>(file, 4, 0, 0, true), [conn, id, failed](Obj o2) {
      if (auto e = as_error(o2)) {
        return failed(std::to_string(e->code_) + " " + e->message_);
      }
      const auto &got = static_cast<const td_api::file &>(*o2);
      if (!got.local_ || !got.local_->is_downloading_completed_) {
        return failed("the download did not finish");
      }
      server.reply_ok(conn, id, path_json(got.local_->path_));
    });
  });
}

void sticker_send(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  std::int64_t chat = 0;
  std::int32_t file = 0, topic = -1;
  std::int64_t thread = 0;
  td_api::object_ptr<td_api::InputMessageReplyTo> reply;
  if (!request_chat(conn, id, req, chat) || !file_field(conn, id, req, file) || !quote_field(conn, id, req, reply) ||
      !topic_thread_fields(conn, id, req, topic, thread)) {
    return;
  }
  // The emoji is left empty: TDLib takes the sticker's own. It also puts the sent sticker first in Recent by itself.
  send_content(conn, id, chat, topic, thread, std::move(reply),
               make_object<td_api::inputMessageSticker>(
                   make_object<td_api::inputSticker>(make_object<td_api::inputFileId>(file), nullptr, 0, 0), ""));
}

void sticker_fave(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  std::int32_t file = 0;
  td::JsonValue on;
  if (!file_field(conn, id, req, file)) {
    return;
  }
  if (optional_field(req, "on", td::JsonValue::Type::Boolean, on) != 1) {
    return bad_request(conn, id, "need the field on, true or false");
  }
  td_api::object_ptr<td_api::Function> ask;
  if (on.get_boolean()) {
    ask = make_object<td_api::addFavoriteSticker>(make_object<td_api::inputFileId>(file));
  } else {
    ask = make_object<td_api::removeFavoriteSticker>(make_object<td_api::inputFileId>(file));
  }
  tdlib.send(std::move(ask), [conn, id](Obj o) { answer(conn, id, o); });
}

}  // namespace

bool stickers_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  static const char *const ours[] = {"stickers.sets", "stickers.get", "stickers.file", "sticker.send", "sticker.fave"};
  if (std::find(std::begin(ours), std::end(ours), cmd) == std::end(ours)) {
    return false;
  }
  if (cmd == "sticker.send") {
    sticker_send(conn, id, req);  // request_chat looks at the login
  } else if (!link_ready()) {
    server.reply_error(conn, id, "unlinked", "not logged in");
  } else if (cmd == "stickers.sets") {
    sets_list(conn, id);
  } else if (cmd == "stickers.get") {
    stickers_get(conn, id, req);
  } else if (cmd == "stickers.file") {
    stickers_file(conn, id, req);
  } else {
    sticker_fave(conn, id, req);
  }
  return true;
}
