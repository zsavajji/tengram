// voice.start, voice.send, voice.cancel and voice.play (docs/protocol.md): recording a voice note and playing one. The
// recorder writes a WAV in the data dir; voice.send encodes it to an OGG Opus file and sends that. A note to play is
// decoded to a WAV, in a cache. The codec is opus_ogg.cpp's.

#include "engine.h"
#include "opus_ogg.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>

namespace {

using td_api::make_object;

// The one recording: the chat (and forum topic, -1 for none) voice.start named and the WAV the recorder is writing.
// 0 and "" when none.
std::int64_t rec_chat = 0;
std::int32_t rec_topic = -1;
std::string rec_path;
// Telegram shows "recording" for about 5 s, so it goes out again every 4 s until the note is sent or cancelled.
// A UI that died mid-recording sends neither: after 30 minutes the engine stops on its own.
constexpr auto kRecordingAgain = std::chrono::seconds(4);
constexpr auto kRecordingAtMost = std::chrono::minutes(30);
std::chrono::steady_clock::time_point rec_next, rec_until;

void send_recording(std::int64_t chat, std::int32_t topic, std::function<void(Obj)> done = nullptr) {
  tdlib.send(make_object<td_api::sendChatAction>(chat, topic_object(topic), "",
                                                 make_object<td_api::chatActionRecordingVoiceNote>()),
             std::move(done));
  rec_next = std::chrono::steady_clock::now() + kRecordingAgain;
}

// The `path` of a request: a .wav inside the data dir, no ".." in it. False after the bad_request reply.
bool wav_path(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::string &path) {
  auto text = string_field(req, "path");
  if (text.is_error()) {
    bad_request(conn, id, "need the field path, a string");
    return false;
  }
  path = text.move_as_ok();
  const bool ok = path.compare(0, data_dir.size() + 1, data_dir + "/") == 0 && path.size() > data_dir.size() + 5 &&
                  path.compare(path.size() - 4, 4, ".wav") == 0 && path.find("..") == std::string::npos;
  if (!ok) {
    bad_request(conn, id, "path is a .wav file inside the data folder");
  }
  return ok;
}

// The recording is over, however: deletes its WAV and, when it is the one we know, tells its chat we stopped.
void end_recording(const std::string &path) {
  unlink(path.c_str());
  if (path == rec_path) {
    tdlib.send(make_object<td_api::sendChatAction>(rec_chat, topic_object(rec_topic), "",
                                                   make_object<td_api::chatActionCancel>()));
    rec_chat = 0;
    rec_topic = -1;
    rec_path.clear();
  }
}

// The folder `dir` + "/" + `name` (dir exists); mkdir does nothing when it is already there.
std::string make_dir(const std::string &dir, const char *name) {
  const std::string path = dir + "/" + name;
  mkdir(path.c_str(), 0700);
  return path;
}

void voice_start(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::string path;
  std::int32_t topic = -1;
  if (!wav_path(conn, id, req, path) || !topic_field(conn, id, req, topic)) {
    return;
  }
  if (!rec_path.empty()) {
    end_recording(rec_path);  // a new start cancels the old one
  }
  rec_chat = chat;
  rec_topic = topic;
  rec_path = path;
  rec_until = std::chrono::steady_clock::now() + kRecordingAtMost;
  send_recording(chat, topic, [conn, id](Obj o) { answer(conn, id, o); });
}

void voice_send(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::string path, why;
  td_api::object_ptr<td_api::InputMessageReplyTo> reply;
  std::int32_t topic = -1;
  if (!wav_path(conn, id, req, path) || !quote_field(conn, id, req, reply) || !topic_field(conn, id, req, topic)) {
    return;
  }
  std::vector<std::int16_t> mono;
  int rate = 0;
  if (!wav_read(path, mono, rate, why)) {
    return bad_request(conn, id, why);
  }
  if (mono.size() < static_cast<std::size_t>(rate / 5)) {  // under 0.2 s
    end_recording(path);
    return bad_request(conn, id, "the recording is shorter than 0.2 s");
  }
  const std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch()).count();
  const std::string ogg = make_dir(data_dir, "voice") + "/" + std::to_string(now) + ".ogg";
  // ponytail: the whole note is encoded here, on the engine thread, which answers nothing else meanwhile: about a
  // second per minute of audio on the phone. Encode as the file grows (HyperBerry did) if notes get long.
  if (!ogg_opus_encode(mono, rate, ogg, why)) {
    say("voice.send: %s", why.c_str());
    end_recording(path);
    return bad_request(conn, id, why);
  }
  const auto seconds = static_cast<std::int32_t>(std::max<std::size_t>(1, (mono.size() + rate / 2) / rate));
  const std::string waveform = waveform_bits(mono);
  end_recording(path);
  send_content(conn, id, chat, topic, std::move(reply),
               make_object<td_api::inputMessageVoiceNote>(
                   make_object<td_api::inputVoiceNote>(make_object<td_api::inputFileLocal>(ogg), seconds, waveform),
                   nullptr, nullptr));
}

void voice_play(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  std::int64_t message = 0;
  if (!message_id(conn, id, req, "id", message)) {
    return;
  }
  tdlib.send(make_object<td_api::getMessage>(chat, message), [=](Obj o) {
    if (as_error(o) || !static_cast<td_api::message &>(*o).content_ ||
        static_cast<td_api::message &>(*o).content_->get_id() != td_api::messageVoiceNote::ID) {
      return server.reply_error(conn, id, "not_found", "no such voice note");
    }
    auto &m = static_cast<td_api::message &>(*o);
    const auto &note = static_cast<td_api::messageVoiceNote &>(*m.content_);
    const td_api::file *file = note.voice_note_ ? note.voice_note_->voice_.get() : nullptr;
    const std::string unique = file && file->remote_ ? file->remote_->unique_id_ : "";
    const bool incoming = !m.is_outgoing_;
    // The callback may run after this answer is gone: it keeps copies.
    download_path(file, "voice.play", [=](const std::string &ogg) {
      if (ogg.empty()) {
        return server.reply_error(conn, id, "download_failed", "the voice note did not download");
      }
      const std::string name = unique.empty() ? ogg.substr(ogg.rfind('/') + 1) : unique;
      const std::string wav = make_dir(make_dir(data_dir, "cache"), "voice") + "/" + name + ".wav";
      struct stat st;
      std::string why;
      if (stat(wav.c_str(), &st) != 0 && !ogg_opus_decode(ogg, wav, why)) {
        say("voice.play: %s", why.c_str());
        return server.reply_error(conn, id, "not_found", why);
      }
      if (incoming) {
        tdlib.send(make_object<td_api::openMessageContent>(chat, message));  // updateMessageContentOpened sets `played`
      }
      server.reply_ok(conn, id, path_json(wav));
    });
  });
}

}  // namespace

bool voice_deadline(std::chrono::steady_clock::time_point &due) {
  if (rec_path.empty()) {
    return false;
  }
  due = rec_next;
  return true;
}

void voice_tick() {
  if (std::chrono::steady_clock::now() >= rec_until) {
    say("voice: no send or cancel in 30 minutes, the recording state stops");
    end_recording(rec_path);
    return;
  }
  send_recording(rec_chat, rec_topic);
}

bool voice_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  if (cmd == "voice.cancel") {
    std::string path;
    if (wav_path(conn, id, req, path)) {
      end_recording(path);
      server.reply_ok(conn, id);
    }
    return true;
  }
  if (cmd != "voice.start" && cmd != "voice.send" && cmd != "voice.play") {
    return false;
  }
  std::int64_t chat = 0;
  if (!request_chat(conn, id, req, chat)) {
    return true;
  }
  if (cmd == "voice.start") {
    voice_start(conn, id, req, chat);
  } else if (cmd == "voice.send") {
    voice_send(conn, id, req, chat);
  } else {
    voice_play(conn, id, req, chat);
  }
  return true;
}
