// Data and storage (docs/protocol.md, "Data and storage"): the automatic download of new messages' media, the copy of
// downloaded photos and videos to the gallery, the cap on the media TDLib keeps on the phone, and the requests
// storage.get, storage.clear, network.get and network.reset. The preferences are mode.cpp's (DataPrefs).
//
// The only timer is data_deadline(): the next pruning, while the cap is on and the account is logged in.

#include "engine.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstring>
#include <unordered_map>

namespace {

using td_api::make_object;
using Clock = std::chrono::steady_clock;

void td_failed(std::uint64_t conn, const std::string &id, const td_api::error &e) {
  say("td error: %d %s", e.code_, e.message_.c_str());
  reply_td_error(conn, id, e);
}

// ---- automatic download

struct Started {
  std::int64_t chat, message;
};
std::unordered_map<std::int32_t, Started> started;  // the downloads we started, by file id, until they finish

// The file of an incoming message that the preferences let download by itself, or nullptr: a photo's full file when
// autoPhotos, a video or GIF up to autoVideoMB, a document or audio file up to autoFileMB, any voice note or round
// video. Stickers and the rest are the UI's to ask for.
const td_api::file *wanted(const td_api::message &m) {
  const td_api::file *f = message_file(m);
  if (!f || !m.content_) {
    return nullptr;
  }
  const DataPrefs &p = mode_data_prefs();
  int limit_mb = 0;
  switch (m.content_->get_id()) {
    case td_api::messagePhoto::ID:
      return p.auto_photos ? f : nullptr;
    case td_api::messageVoiceNote::ID:
    case td_api::messageVideoNote::ID:
      return f;
    case td_api::messageVideo::ID:
    case td_api::messageAnimation::ID:
      limit_mb = p.auto_video_mb;
      break;
    case td_api::messageDocument::ID:
    case td_api::messageAudio::ID:
      limit_mb = p.auto_file_mb;
      break;
    default:
      return nullptr;
  }
  const std::int64_t size = f->size_ ? f->size_ : f->expected_size_;
  return limit_mb > 0 && size > 0 && size <= (std::int64_t{limit_mb} << 20) ? f : nullptr;
}

// A file we started has been downloaded: its row shows it (media_state 2, the path), then the gallery copy.
void finished(const td_api::file &f) {
  const auto it = started.find(f.id_);
  if (it == started.end()) {
    return;
  }
  const Started s = it->second;
  started.erase(it);
  const std::string path = f.local_->path_;
  tdlib.send(make_object<td_api::getMessage>(s.chat, s.message), [path](Obj o) {
    if (as_error(o)) {  // deleted in the meantime
      return;
    }
    auto &m = static_cast<td_api::message &>(*o);
    messages_queue(m);
    gallery_save(m, path);
  });
}

void auto_download(const td_api::message &m) {
  if (m.is_outgoing_ || m.sending_state_ || m.self_destruct_type_ || !mode_auto_download_ok()) {
    return;
  }
  const td_api::file *f = wanted(m);
  if (!f || started.count(f->id_)) {
    return;
  }
  if (f->local_ && (f->local_->is_downloading_completed_ || f->local_->is_downloading_active_ ||
                    !f->local_->can_be_downloaded_)) {
    return;
  }
  const std::int32_t file_id = f->id_;
  started[file_id] = {m.chat_id_, m.id_};
  say("auto download: chat %lld message %lld, %lld bytes", static_cast<long long>(m.chat_id_),
      static_cast<long long>(m.id_), static_cast<long long>(f->size_ ? f->size_ : f->expected_size_));
  // Priority 1: below a preview's 8 and a tap's 16. Asynchronous: the answer is the file's state, the end comes as an
  // updateFile.
  tdlib.send(make_object<td_api::downloadFile>(file_id, 1, 0, 0, false), [file_id](Obj o) {
    if (auto e = as_error(o)) {
      say("auto download: file %d: %d %s", file_id, e->code_, e->message_.c_str());
      started.erase(file_id);
      return;
    }
    const auto &got = static_cast<const td_api::file &>(*o);
    if (got.local_ && got.local_->is_downloading_completed_) {  // it was here after all
      finished(got);
    }
  });
}

// ---- the gallery copy

std::string extension(const std::string &path) {
  const std::size_t dot = path.rfind('.'), slash = path.rfind('/');
  return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? "" : path.substr(dot);
}

bool make_folder(const std::string &dir) {
  if (mkdir(dir.c_str(), 0755) == 0) {
    chmod(dir.c_str(), 0755);  // the engine's umask is 077; the folder is for the user's other apps
    return true;
  }
  const int err = errno;
  struct stat st;
  return err == EEXIST && stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

}  // namespace

// ponytail: a blocking copy on the main thread, 64 KiB at a time: a video of a few hundred MB holds the engine for as
// long as the flash takes. A copy in slices from data_tick() if that is ever felt.
bool copy_file(const std::string &from, const std::string &to) {
  const std::string part = to + ".part";  // renamed when whole: a copy that was cut short is never taken for the file
  const int in = open(from.c_str(), O_RDONLY);
  const int out = open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  bool ok = in >= 0 && out >= 0;
  static char buf[1 << 16];
  for (ssize_t n; ok && (n = read(in, buf, sizeof buf)) != 0;) {
    ok = n > 0;
    for (ssize_t at = 0; ok && at < n;) {
      const ssize_t w = write(out, buf + at, static_cast<std::size_t>(n - at));
      ok = w > 0;
      at += w;
    }
  }
  if (in >= 0) {
    close(in);
  }
  if (out >= 0) {
    ok = fchmod(out, 0644) == 0 && ok;  // as the chat export's file
    ok = close(out) == 0 && ok;
  }
  ok = ok && rename(part.c_str(), to.c_str()) == 0;
  if (!ok) {
    unlink(part.c_str());
  }
  return ok;
}

namespace {

// ---- pruning

constexpr auto kFirstPrune = std::chrono::minutes(2);
constexpr auto kPruneEvery = std::chrono::hours(6);
Clock::time_point next_prune = Clock::now() + kFirstPrune;

// optimizeStorage for the media the chats show: photos, videos, GIFs, documents, audio files, voice notes and round
// videos. Not profile photos, stickers or thumbnails (chats.db rows point at them), and not wallpapers, which TDLib
// keeps by default too. `bytes` is the size the files may keep. Its ttl and count are INT32_MAX, no limit: TDLib's -1
// means its own defaults (23 hours and 40000 files), FileGcParameters.cpp:23-30. Files younger than `immunity` seconds
// stay (FileGcWorker.cpp:119: by modification time, which is the download's); the oldest-used of the others go first
// (:139, sorted by access time). The answer is the statistics of what was deleted.
td_api::object_ptr<td_api::optimizeStorage> optimize(std::int64_t bytes, std::int32_t immunity) {
  std::vector<td_api::object_ptr<td_api::FileType>> types;
  types.push_back(make_object<td_api::fileTypePhoto>());
  types.push_back(make_object<td_api::fileTypeVideo>());
  types.push_back(make_object<td_api::fileTypeAnimation>());
  types.push_back(make_object<td_api::fileTypeDocument>());
  types.push_back(make_object<td_api::fileTypeAudio>());
  types.push_back(make_object<td_api::fileTypeVoiceNote>());
  types.push_back(make_object<td_api::fileTypeVideoNote>());
  return make_object<td_api::optimizeStorage>(bytes, INT32_MAX, INT32_MAX, immunity, std::move(types),
                                              std::vector<std::int64_t>{}, std::vector<std::int64_t>{}, true, 0);
}

// ---- the requests

void storage_get(std::uint64_t conn, const std::string &id) {
  tdlib.send(make_object<td_api::getStorageStatisticsFast>(), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    const auto &s = static_cast<const td_api::storageStatisticsFast &>(*o);
    server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&s](auto &r) {
                      r("files", s.files_size_);
                      r("count", s.file_count_);
                      r("db", s.database_size_ + s.language_pack_database_size_);
                      r("logs", s.log_size_);
                    })));
  });
}

void storage_clear(std::uint64_t conn, const std::string &id) {
  tdlib.send(optimize(0, 0), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    const auto &s = static_cast<const td_api::storageStatistics &>(*o);
    say("storage: cleared %lld bytes in %d files", static_cast<long long>(s.size_), s.count_);
    server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&s](auto &r) { r("freed", s.size_); })));
  });
}

struct Bytes {
  std::int64_t sent = 0, received = 0;
};

void network_get(std::uint64_t conn, const std::string &id) {
  tdlib.send(make_object<td_api::getNetworkStatistics>(false), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    const auto &n = static_cast<const td_api::networkStatistics &>(*o);
    Bytes mobile, wifi, other;
    for (const auto &entry : n.entries_) {
      if (!entry) {
        continue;
      }
      const td_api::NetworkType *type = nullptr;
      Bytes add;
      if (entry->get_id() == td_api::networkStatisticsEntryFile::ID) {
        const auto &e = static_cast<const td_api::networkStatisticsEntryFile &>(*entry);
        type = e.network_type_.get();
        add = {e.sent_bytes_, e.received_bytes_};
      } else if (entry->get_id() == td_api::networkStatisticsEntryCall::ID) {
        const auto &e = static_cast<const td_api::networkStatisticsEntryCall &>(*entry);
        type = e.network_type_.get();
        add = {e.sent_bytes_, e.received_bytes_};
      } else {
        continue;
      }
      const std::int32_t kind = type ? type->get_id() : 0;
      Bytes &to = kind == td_api::networkTypeMobile::ID || kind == td_api::networkTypeMobileRoaming::ID ? mobile
                  : kind == td_api::networkTypeWiFi::ID                                                ? wifi
                                                                                                       : other;
      to.sent += add.sent;
      to.received += add.received;
    }
    auto part = [](const Bytes &b) {
      return td::json_object([b](auto &r) {
        r("sent", b.sent);
        r("received", b.received);
      });
    };
    server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                      r("since", n.since_date_);
                      r("mobile", part(mobile));
                      r("wifi", part(wifi));
                      r("other", part(other));
                    })));
  });
}

}  // namespace

bool data_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &) {
  if (cmd == "storage.get") {
    storage_get(conn, id);
  } else if (cmd == "storage.clear") {
    storage_clear(conn, id);
  } else if (cmd == "network.get") {
    network_get(conn, id);
  } else if (cmd == "network.reset") {
    tdlib.send(make_object<td_api::resetNetworkStatistics>(), [conn, id](Obj o) { answer(conn, id, o); });
  } else {
    return false;
  }
  return true;
}

void data_update(td_api::Object &update) {
  if (update.get_id() == td_api::updateNewMessage::ID) {
    const auto &u = static_cast<td_api::updateNewMessage &>(update);
    if (u.message_) {
      auto_download(*u.message_);
    }
  } else if (update.get_id() == td_api::updateFile::ID) {
    const auto &f = static_cast<td_api::updateFile &>(update).file_;
    if (f && f->local_ && f->local_->is_downloading_completed_) {
      finished(*f);
    }
  }
}

void data_reset() {
  started.clear();
}

// The network changed, or autoNet did: what was started on Wi-Fi stops once it is not allowed any more ("Wi-Fi only" on
// mobile data, or none). A tap's download of the same file (media.get) is TDLib's to keep: only ours are cancelled.
void data_network_changed() {
  if (started.empty() || mode_auto_download_ok()) {
    return;
  }
  say("auto download: %zu stopped, not allowed on this network", started.size());
  for (const auto &entry : started) {
    tdlib.send(td_api::make_object<td_api::cancelDownloadFile>(entry.first, false));
  }
  started.clear();
}

void gallery_save(const td_api::message &m, const std::string &path) {
  // Incoming photos and videos of messages the chat lets one save (not a protected chat's, not a self-destructing one).
  if (m.is_outgoing_ || !m.can_be_saved_ || m.self_destruct_type_ || !m.content_) {
    return;
  }
  bool video = false;
  if (m.content_->get_id() == td_api::messageVideo::ID) {
    video = true;
  } else if (m.content_->get_id() != td_api::messagePhoto::ID) {
    return;
  }
  ChatBrief chat = {};
  if (!chat_brief(m.chat_id_, chat)) {
    return;
  }
  const DataPrefs &p = mode_data_prefs();
  // Never a secret chat's: its media stay inside Tengram, as Telegram's apps keep them.
  if (chat.secret || !(chat.kind == 3 ? p.gallery_channels : chat.kind == 1 ? p.gallery_groups : p.gallery_private)) {
    return;
  }
  const std::string dir = std::string("/accounts/1000/shared/") + (video ? "videos" : "photos") + "/Tengram";
  if (!make_folder(dir)) {
    say("gallery: cannot make %s", dir.c_str());
    return;
  }
  const std::string to = dir + "/" + std::to_string(m.chat_id_) + "_" + std::to_string(m.id_) + extension(path);
  struct stat st;
  if (stat(to.c_str(), &st) == 0) {
    return;
  }
  say(copy_file(path, to) ? "gallery: %s" : "gallery: could not copy to %s", to.c_str());
}

// Another app opens a document or audio file only from where it can read, and File Manager shows only the shared
// folders: the engine's files are neither (the user, 2026-11-18: a file sent to Saved Messages couldn't be found or
// moved). So the menu's Download copies it (the user's ask, 2026-11-18: optional, not at every open). Ours too: in
// Saved Messages what the user sent from another device is outgoing. "" when it can't be saved.
std::string downloads_copy(const td_api::message &m, const std::string &path) {
  if (!m.content_ || !m.can_be_saved_ || m.self_destruct_type_) {
    return "";
  }
  std::string name;
  if (m.content_->get_id() == td_api::messageDocument::ID) {
    const auto &d = static_cast<const td_api::messageDocument &>(*m.content_).document_;
    name = d ? d->file_name_ : "";
  } else if (m.content_->get_id() == td_api::messageAudio::ID) {
    const auto &a = static_cast<const td_api::messageAudio &>(*m.content_).audio_;
    name = a ? a->file_name_ : "";
  } else {
    return "";
  }
  ChatBrief chat = {};
  if (!chat_brief(m.chat_id_, chat) || chat.secret) {  // a secret chat's files stay inside Tengram
    return "";
  }
  for (char &c : name) {
    if (static_cast<unsigned char>(c) < 0x20 || std::strchr("/\\:*?\"<>|", c)) {
      c = '_';
    }
  }
  if (name.empty() || name[0] == '.') {
    name = std::to_string(m.id_) + extension(path);
  }
  const std::string dir = "/accounts/1000/shared/downloads";
  struct stat from;
  if (!make_folder(dir) || stat(path.c_str(), &from) != 0) {
    return "";
  }
  // "name.ext", else "name (2).ext" and on: one of the same size is taken for this file, opened before.
  const std::string ext = extension(name), stem = name.substr(0, name.size() - ext.size());
  for (int n = 1; n < 100; ++n) {
    const std::string to = dir + "/" + (n == 1 ? stem : stem + " (" + std::to_string(n) + ")") + ext;
    struct stat st;
    if (stat(to.c_str(), &st) != 0) {
      if (!copy_file(path, to)) {
        say("downloads: could not copy to %s", to.c_str());
        return "";
      }
      say("downloads: %s", to.c_str());
      return to;
    }
    if (st.st_size == from.st_size) {
      return to;
    }
  }
  return "";
}

bool data_deadline(std::chrono::steady_clock::time_point &due) {
  if (mode_data_prefs().keep_mb == 0 || !link_ready()) {
    return false;
  }
  due = next_prune;
  return true;
}

void data_tick() {
  next_prune = Clock::now() + kPruneEvery;
  const int keep = mode_data_prefs().keep_mb;
  tdlib.send(optimize(std::int64_t{keep} << 20, 24 * 60 * 60), [keep](Obj o) {
    if (auto e = as_error(o)) {
      return say("storage: keep %d MB: %d %s", keep, e->code_, e->message_.c_str());
    }
    const auto &s = static_cast<const td_api::storageStatistics &>(*o);
    say("storage: keep %d MB: freed %lld bytes in %d files", keep, static_cast<long long>(s.size_), s.count_);
  });
}
