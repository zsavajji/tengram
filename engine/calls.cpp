// 1:1 voice calls: TDLib's call states and signalling on one side, Telegram's tgcalls library on the other
// (docs/protocol.md, "Calls"). One call at a time. tgcalls runs on threads of its own: its callbacks only post a
// closure to the main thread, as bb10.cpp does for the navigator, and every closure checks that its call is still
// the live one. The sound goes to and from the UI over a second connection (AudioLink).

#include "engine.h"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

#include "tgcalls/FakeAudioDeviceModule.h"
#include "tgcalls/Instance.h"
#include "tgcalls/v2/InstanceV2Impl.h"
#include "tgcalls/v2/InstanceV2ReferenceImpl.h"

namespace {

using td_api::make_object;

void td_failed(std::uint64_t conn, const std::string &id, const td_api::error &e) {
  say("td error: %d %s", e.code_, e.message_.c_str());
  reply_td_error(conn, id, e);
}

// The audio link to the UI (docs/protocol.md, "Audio link"): 16 kHz mono s16le on a socket, in frames of 60 ms. One per
// live call, shared by the call and by tgcalls' audio threads, which run Mic and Speaker every 10 ms.
constexpr int kRate = 16000;
constexpr std::size_t kTick = kRate / 100 * 2;  // 10 ms, in bytes
constexpr std::size_t kFrame = 1920;            // 60 ms
constexpr std::size_t kMicKeep = 3 * kFrame, kPeerKeep = 8 * kFrame;

struct AudioLink {
  explicit AudioLink(std::int32_t c) : call(c) {
  }
  void close_fd() {  // with m held
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
  }
  const std::int32_t call;
  std::mutex m;        // guards the rest
  int fd = -1;         // -1 when there is no link
  std::string mic;     // the microphone's bytes, waiting for tgcalls
  std::string gather;  // the peer's bytes, gathering into a frame
  std::string out;     // whole frames (the first may be half sent) waiting to be written to the UI
};

struct Live {
  std::int32_t id = 0;
  std::int64_t user = 0, chat = 0;
  bool out = false;
  std::string state;  // ringing, calling, connecting, active or ended: the `call` event's
  std::int64_t since_ms = 0;  // unix ms when it became active, 0 before
  std::unique_ptr<tgcalls::Instance> inst;
  std::shared_ptr<AudioLink> audio;
  bool hanging = false;                          // discardCall is sent
  std::vector<std::vector<std::uint8_t>> early;  // signalling that came before the instance, in order
};
std::optional<Live> live;

int wake_fd = -1;
std::mutex mu;
std::vector<std::function<void()>> queue;  // guarded by mu

// From any thread: runs on the main thread, in calls_drain().
void post(std::function<void()> f) {
  bool was_empty;
  {
    std::lock_guard<std::mutex> lock(mu);
    was_empty = queue.empty();
    queue.push_back(std::move(f));
  }
  if (was_empty) {
    ssize_t n = write(wake_fd, "c", 1);
    (void)n;
  }
}

bool is_live(std::int32_t id) {
  return live && live->id == id;
}

std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// ---- TDLib's side

td_api::object_ptr<td_api::callProtocol> protocol() {
  auto versions = tgcalls::Meta::Versions();
  std::reverse(versions.begin(), versions.end());  // Telegram Desktop sends them newest first
  return make_object<td_api::callProtocol>(true, true, 65, tgcalls::Meta::MaxLayer(), std::move(versions));
}

// The `call` event, to every connection.
void emit(const std::string &reason = "") {
  server.broadcast(td::json_encode<std::string>(td::json_object([&](auto &o) {
    o("ev", "call");
    o("id", std::to_string(live->id));
    o("chat", std::to_string(live->chat));
    o("out", td::JsonBool(live->out));
    o("state", live->state);
    if (!reason.empty()) {
      o("reason", reason);
    }
    o("since", live->since_ms);
  })));
}

void set_state(const char *s) {
  if (live->state != s) {
    const bool was_ringing = live->state == "ringing";
    live->state = s;
    emit();
    if (live->state == "ringing") {  // only an incoming call gets this state
      ring_start(live->chat);
    } else if (was_ringing) {
      ring_stop(live->chat);
    }
  }
}

// The call is over. The instance stops on its own threads: the completion logs the traffic and hands it back to the
// main thread to be deleted (never on tgcalls' thread).
void end(const char *reason) {
  say("call %d: ended (%s)", live->id, reason);
  if (live->state == "ringing") {
    ring_stop(live->chat);
  }
  live->state = "ended";
  emit(reason);
  {
    std::lock_guard<std::mutex> lock(live->audio->m);  // the instance's audio threads may run until it has stopped
    live->audio->close_fd();
  }
  if (tgcalls::Instance *raw = live->inst.release()) {
    raw->stop([raw](tgcalls::FinalState f) {
      const auto &t = f.trafficStats;
      say("call: traffic wifi %llu sent %llu received, mobile %llu sent %llu received",
          static_cast<unsigned long long>(t.bytesSentWifi), static_cast<unsigned long long>(t.bytesReceivedWifi),
          static_cast<unsigned long long>(t.bytesSentMobile), static_cast<unsigned long long>(t.bytesReceivedMobile));
      post([raw] { delete raw; });
    });
  }
  live.reset();
  mode_evaluate();
}

// discardCall, as call.reject and call.hangup send it; also what a failed connection does (is_disconnected).
void hang_up(bool disconnected, Td::Callback cb = nullptr) {
  const std::int64_t secs = live->since_ms ? (now_ms() - live->since_ms) / 1000 : 0;
  live->hanging = true;
  tdlib.send(make_object<td_api::discardCall>(live->id, disconnected, "", static_cast<std::int32_t>(secs), false,
                                              live->inst ? live->inst->getPreferredRelayId() : 0),
             std::move(cb));
}

// The UI is gone, or its audio link is: nothing can play or record. It gets 3 s to open a new one, then the call ends.
void audio_lost(std::int32_t id) {
  if (!is_live(id) || live->hanging) {
    return;
  }
  say("call %d: audio link lost", id);
  std::thread([id] {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    post([id] {
      if (!is_live(id) || live->hanging) {
        return;
      }
      {
        std::lock_guard<std::mutex> lock(live->audio->m);
        if (live->audio->fd >= 0) {
          return;
        }
      }
      say("call %d: no audio link for 3 s, hanging up", id);
      hang_up(true);
    });
  }).detach();
}

// With a.m held, from an audio thread: the link failed or the UI closed it.
void lose(AudioLink &a) {
  a.close_fd();
  const std::int32_t id = a.call;
  post([id] { audio_lost(id); });
}

// tgcalls' fake device sleeps a fixed 10 ms after each tick, so with the tick's own work and the sleep's overshoot it ran
// a few % slow: the peer reached the UI slower than real time and the Q10's speaker ran dry every ~0.4 s. This paces the
// ticks by a steady clock: each is due 10 ms after the last was due, whenever that ran. Over 100 ms behind (the thread
// was starved), it starts over from now rather than rushing out the backlog.
struct Pace {
  std::int32_t wait_us() {
    const auto now = std::chrono::steady_clock::now();
    next = now - next > std::chrono::milliseconds(100) ? now + std::chrono::milliseconds(10)
                                                       : next + std::chrono::milliseconds(10);
    return static_cast<std::int32_t>(std::max<long long>(
        0, std::chrono::duration_cast<std::chrono::microseconds>(next - now).count()));
  }
  std::chrono::steady_clock::time_point next{};
};

// The microphone: what the UI sent, 10 ms at a time; silence when it has not sent enough.
class Mic final : public tgcalls::FakeAudioDeviceModule::Recorder {
 public:
  explicit Mic(std::shared_ptr<AudioLink> a) : a_(std::move(a)) {
  }
  tgcalls::AudioFrame Record() override {  // tgcalls' audio thread
    std::lock_guard<std::mutex> lock(a_->m);
    char chunk[kFrame];
    while (a_->fd >= 0) {
      const ssize_t n = read(a_->fd, chunk, sizeof chunk);
      if (n > 0) {
        a_->mic.append(chunk, static_cast<std::size_t>(n));
        if (a_->mic.size() > kMicKeep) {  // the oldest go, in whole ticks so the samples stay aligned
          a_->mic.erase(0, (a_->mic.size() - kMicKeep + kTick - 1) / kTick * kTick);
        }
      } else if (n < 0 && errno == EINTR) {
        continue;
      } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break;
      } else {
        lose(*a_);
      }
    }
    if (a_->mic.size() >= kTick) {
      std::memcpy(buf_, a_->mic.data(), kTick);
      a_->mic.erase(0, kTick);
    } else {
      std::memset(buf_, 0, kTick);
    }
    return {buf_, kTick / 2, 2, 1, kRate, 0, 0};
  }

  std::int32_t WaitForUs() override {
    return pace_.wait_us();
  }

 private:
  std::shared_ptr<AudioLink> a_;
  std::int16_t buf_[kTick / 2];
  Pace pace_;
};

// The peer: gathered into frames and written to the UI without ever blocking; what the UI does not read is dropped.
class Speaker final : public tgcalls::FakeAudioDeviceModule::Renderer {
 public:
  explicit Speaker(std::shared_ptr<AudioLink> a) : a_(std::move(a)) {
  }
  bool Render(const tgcalls::AudioFrame &f) override {  // tgcalls' audio thread
    std::lock_guard<std::mutex> lock(a_->m);
    if (a_->fd < 0) {
      return true;
    }
    for (std::size_t i = 0; i < f.num_samples; ++i) {  // the left channel, if there are two
      a_->gather.append(reinterpret_cast<const char *>(f.audio_samples + i * f.num_channels), 2);
    }
    for (; a_->gather.size() >= kFrame; a_->gather.erase(0, kFrame)) {
      a_->out.append(a_->gather, 0, kFrame);
    }
    while (a_->out.size() > kPeerKeep) {
      // The oldest frame not yet started: one half sent is finished, so the UI's frames stay whole.
      a_->out.erase(a_->out.size() % kFrame, kFrame);
    }
    while (a_->fd >= 0 && !a_->out.empty()) {
      const ssize_t n = send(a_->fd, a_->out.data(), a_->out.size(), MSG_NOSIGNAL);
      if (n > 0) {
        a_->out.erase(0, static_cast<std::size_t>(n));
      } else if (n < 0 && errno == EINTR) {
        continue;
      } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break;
      } else {
        lose(*a_);
      }
    }
    return true;
  }
  std::int32_t WaitForUs() override {
    return pace_.wait_us();
  }

 private:
  std::shared_ptr<AudioLink> a_;
  Pace pace_;
};

std::string hex(const std::string &bytes) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (unsigned char b : bytes) {
    out += digits[b >> 4];
    out += digits[b & 15];
  }
  return out;
}

void on_state(std::int32_t id, tgcalls::State s) {
  if (!is_live(id)) {
    return;
  }
  static const char *const names[] = {"WaitInit", "WaitInitAck", "Established", "Failed", "Reconnecting"};
  say("call %d: tgcalls %s", id, names[static_cast<int>(s)]);
  if (s == tgcalls::State::Established && live->since_ms == 0) {
    live->since_ms = now_ms();
    set_state("active");
  } else if (s == tgcalls::State::Failed && !live->hanging) {
    hang_up(true);
  }
}

void send_signaling(std::int32_t id, const std::vector<std::uint8_t> &data) {
  if (is_live(id)) {
    say("call %d: signalling out, %zu bytes", id, data.size());
    tdlib.send(make_object<td_api::sendCallSignalingData>(id, std::string(data.begin(), data.end())));
  }
}

// The instance from callStateReady. False after ending the call.
bool make_instance(const td_api::call &c, const td_api::callStateReady &r) {
  if (!r.protocol_ || r.protocol_->library_versions_.empty() ||
      r.encryption_key_.size() != tgcalls::EncryptionKey::kSize) {
    say("call %d: no library version, or a key of %zu bytes", c.id_, r.encryption_key_.size());
    end("failed");
    return false;
  }
  const std::string &version = r.protocol_->library_versions_[0];
  auto key = std::make_shared<std::array<std::uint8_t, tgcalls::EncryptionKey::kSize>>();
  std::copy(r.encryption_key_.begin(), r.encryption_key_.end(), key->begin());

  // Telegram Desktop's mapping. The reflectors that speak UDP are endpoints; all of them are also RtcServers, numbered
  // by their place among the reflector ids; the WebRTC servers are STUN and TURN servers.
  std::vector<tgcalls::Endpoint> endpoints;
  std::vector<tgcalls::RtcServer> servers;
  std::set<std::int64_t> reflectors;
  for (const auto &s : r.servers_) {
    if (s && s->type_ && s->type_->get_id() == td_api::callServerTypeTelegramReflector::ID) {
      reflectors.insert(s->id_);
    }
  }
  for (const auto &s : r.servers_) {
    if (!s || !s->type_) {
      continue;
    }
    const auto port = static_cast<std::uint16_t>(s->port_);
    const std::string *const hosts[] = {&s->ip_address_, &s->ipv6_address_};
    if (s->type_->get_id() == td_api::callServerTypeTelegramReflector::ID) {
      const auto &t = static_cast<const td_api::callServerTypeTelegramReflector &>(*s->type_);
      if (!t.is_tcp_ && t.peer_tag_.size() == 16) {
        tgcalls::Endpoint e;
        e.endpointId = s->id_;
        e.host = {s->ip_address_, s->ipv6_address_};
        e.port = port;
        e.type = tgcalls::EndpointType::UdpRelay;
        std::memcpy(e.peerTag, t.peer_tag_.data(), 16);
        endpoints.push_back(e);
      }
      const auto id = static_cast<std::uint8_t>(std::distance(reflectors.begin(), reflectors.find(s->id_)) + 1);
      for (const std::string *host : hosts) {
        if (!host->empty()) {
          servers.push_back({id, *host, port, "reflector", hex(t.peer_tag_), true, t.is_tcp_});
        }
      }
    } else if (s->type_->get_id() == td_api::callServerTypeWebrtc::ID) {
      const auto &t = static_cast<const td_api::callServerTypeWebrtc &>(*s->type_);
      for (const bool turn : {false, true}) {
        if (turn ? !(t.supports_turn_ && !t.username_.empty() && !t.password_.empty()) : !t.supports_stun_) {
          continue;
        }
        for (const std::string *host : hosts) {
          if (!host->empty()) {
            servers.push_back({0, *host, port, turn ? t.username_ : "", turn ? t.password_ : "", turn, false});
          }
        }
      }
    }
  }

  tgcalls::Config config;
  config.initializationTimeout = 30;
  config.receiveTimeout = 20;
  config.dataSaving = tgcalls::DataSaving::Never;
  config.enableP2P = r.allow_p2p_;
  config.enableAEC = false;
  config.enableNS = true;
  config.enableAGC = true;
  config.enableVolumeControl = true;
  config.maxApiLayer = r.protocol_->max_layer_;
  config.customParameters = r.custom_parameters_;
  tgcalls::Descriptor d{.version = version,
                        .config = config,
                        .endpoints = std::move(endpoints),
                        .rtcServers = std::move(servers),
                        .encryptionKey = tgcalls::EncryptionKey(key, c.is_outgoing_)};
  const std::size_t n_endpoints = d.endpoints.size(), n_servers = d.rtcServers.size();
  const std::int32_t id = c.id_;
  d.stateUpdated = [id](tgcalls::State s) {
    post([id, s] { on_state(id, s); });
  };
  d.signalingDataEmitted = [id](const std::vector<std::uint8_t> &data) {
    post([id, data] { send_signaling(id, data); });
  };
  d.signalBarsUpdated = [id](int bars) {
    post([id, bars] {
      if (is_live(id)) {
        say("call %d: signal bars %d", id, bars);
      }
    });
  };
  tgcalls::FakeAudioDeviceModule::Options audio;
  audio.samples_per_sec = kRate;
  audio.num_channels = 1;
  d.createAudioDeviceModule = tgcalls::FakeAudioDeviceModule::Creator(std::make_shared<Speaker>(live->audio),
                                                                      std::make_shared<Mic>(live->audio), audio);

  say("call %d: version %s, p2p %d, %zu endpoints, %zu rtc servers", id, version.c_str(), d.config.enableP2P ? 1 : 0,
      n_endpoints, n_servers);
  live->inst = tgcalls::Meta::Create(version, std::move(d));
  if (!live->inst) {
    say("call %d: no instance for version %s", id, version.c_str());
    end("failed");
    return false;
  }
  for (const auto &packet : live->early) {
    live->inst->receiveSignalingData(packet);
  }
  live->early.clear();
  return true;
}

const char *reason_of(const td_api::CallDiscardReason *r, bool out) {
  switch (r ? r->get_id() : 0) {
    case td_api::callDiscardReasonMissed::ID:
      return out ? "no_answer" : "missed";
    case td_api::callDiscardReasonDeclined::ID:
      return "declined";
    case td_api::callDiscardReasonDisconnected::ID:
      return "failed";
    default:  // hung up, empty, upgraded to a group call
      return "hangup";
  }
}

void on_call(const td_api::call &c) {
  if (!c.state_) {
    return;
  }
  const int kind = c.state_->get_id();
  say("call %d: %s, %s", c.id_, type_name(*c.state_).c_str(), c.is_outgoing_ ? "outgoing" : "incoming");
  if (live && live->id != c.id_) {
    if (kind == td_api::callStatePending::ID && !c.is_outgoing_) {
      say("call %d: busy", c.id_);
      tdlib.send(make_object<td_api::discardCall>(c.id_, false, "", 0, false, 0));
    }
    return;
  }
  if (!live) {  // a call is ours from its first pending, exchanging or ready state; one already over is not
    if (kind != td_api::callStatePending::ID && kind != td_api::callStateExchangingKeys::ID &&
        kind != td_api::callStateReady::ID) {
      return;
    }
    live.emplace();
    live->id = c.id_;
    live->audio = std::make_shared<AudioLink>(c.id_);
    live->user = live->chat = c.user_id_;  // a private chat's id is the user's
    live->out = c.is_outgoing_;
    mode_evaluate();  // the network must be on
  }
  switch (kind) {
    case td_api::callStatePending::ID:
      set_state(c.is_outgoing_ ? "calling" : "ringing");
      break;
    case td_api::callStateExchangingKeys::ID:
      set_state("connecting");
      break;
    case td_api::callStateReady::ID:
      if (!live->inst && !make_instance(c, static_cast<const td_api::callStateReady &>(*c.state_))) {
        return;
      }
      if (live->state != "active") {
        set_state("connecting");
      }
      break;
    case td_api::callStateDiscarded::ID: {
      const auto *reason = static_cast<const td_api::callStateDiscarded &>(*c.state_).reason_.get();
      say("call %d: discard reason %s", c.id_, reason ? type_name(*reason).c_str() : "none");
      end(reason_of(reason, c.is_outgoing_));
      break;
    }
    case td_api::callStateError::ID: {
      const auto &e = static_cast<const td_api::callStateError &>(*c.state_).error_;
      const std::string message = e ? e->message_ : "";
      say("call %d: error %s", c.id_, message.c_str());
      end(message.find("USER_BUSY") != std::string::npos ? "busy" : "failed");
      break;
    }
    default:  // hanging up: the discarded state follows
      break;
  }
}

void on_signaling(std::int32_t id, const std::string &data) {
  if (!is_live(id)) {
    return;
  }
  say("call %d: signalling in, %zu bytes", id, data.size());
  std::vector<std::uint8_t> packet(data.begin(), data.end());
  if (live->inst) {
    live->inst->receiveSignalingData(packet);
  } else {
    live->early.push_back(std::move(packet));
  }
}

void call_start(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  std::int64_t chat = 0;
  if (!request_chat(conn, id, req, chat)) {
    return;
  }
  if (live) {
    return server.reply_error(conn, id, "busy", "a call is under way");
  }
  with_chat(conn, id, chat, [conn, id](const td_api::chat &c) {
    const std::int64_t user = person_of(c);
    if (user == 0) {
      return server.reply_error(conn, id, "bad_chat", "not a chat with a person");
    }
    if (live) {  // one came in while the chat was fetched
      return server.reply_error(conn, id, "busy", "a call is under way");
    }
    tdlib.send(make_object<td_api::createCall>(user, protocol(), false), [conn, id](Obj o) {
      if (auto e = as_error(o)) {
        say("td error: %d %s", e->code_, e->message_.c_str());
        return reply_td_error(conn, id, *e);
      }
      const std::int32_t call = static_cast<const td_api::callId &>(*o).id_;
      server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                        r("id", std::to_string(call));
                      })));
    });
  });
}

// A call message's discard reason, as calls.list words it: "" for none, or a call that became a group call.
const char *discard_word(const td_api::CallDiscardReason *r) {
  switch (r ? r->get_id() : 0) {
    case td_api::callDiscardReasonMissed::ID:
      return "missed";
    case td_api::callDiscardReasonDeclined::ID:
      return "declined";
    case td_api::callDiscardReasonDisconnected::ID:
      return "disconnected";
    case td_api::callDiscardReasonHungUp::ID:
      return "hungup";
    default:
      return "";
  }
}

// The call log: the newest 100 call messages of all chats, as TDLib orders them (newest first).
void calls_list(std::uint64_t conn, const std::string &id) {
  tdlib.send(make_object<td_api::searchCallMessages>("", 100, false), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    std::vector<const td_api::message *> rows;
    for (const auto &m : static_cast<const td_api::foundMessages &>(*o).messages_) {
      if (m && m->content_ && m->content_->get_id() == td_api::messageCall::ID) {
        rows.push_back(m.get());
      }
    }
    server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&rows](auto &r) {
                      r("calls", td::json_array(rows, [](const td_api::message *m) {
                        // m by value: the object is written out after this returns (by reference, every row read
                        // freed stack: chats, dates and names were garbage; Q10, 2026-11-19).
                        return td::json_object([m](auto &o) {
                          const auto &call = static_cast<const td_api::messageCall &>(*m->content_);
                          o("chat", std::to_string(m->chat_id_));
                          o("msg", std::to_string(m->id_));
                          o("date", m->date_);
                          o("out", td::JsonBool(m->is_outgoing_));
                          o("reason", std::string(discard_word(call.discard_reason_.get())));
                          o("duration", call.duration_);
                          o("video", td::JsonBool(call.is_video_));
                          o("name", chat_title(m->chat_id_));  // "" when unknown
                        });
                      }));
                    })));
  });
}

}  // namespace

void calls_start(int wake) {
  wake_fd = wake;
  // Not tgcalls v1 (2.7.7, 5.0.0, InstanceImpl): it crashes on QNX once media flows (tests/calls_check, 2026-11-19),
  // and every current Telegram app speaks 7.0.0 or newer.
  tgcalls::Register<tgcalls::InstanceV2Impl>();
  tgcalls::Register<tgcalls::InstanceV2ReferenceImpl>();
  std::string versions;
  for (const auto &v : tgcalls::Meta::Versions()) {
    versions += (versions.empty() ? "" : ", ") + v;
  }
  say("calls: versions %s, max layer %d", versions.c_str(), tgcalls::Meta::MaxLayer());
}

void calls_drain() {
  std::vector<std::function<void()>> jobs;
  {
    std::lock_guard<std::mutex> lock(mu);
    jobs.swap(queue);
  }
  for (auto &job : jobs) {
    job();
  }
}

void calls_update(td_api::Object &update) {
  if (update.get_id() == td_api::updateCall::ID) {
    if (auto &call = static_cast<td_api::updateCall &>(update).call_) {
      on_call(*call);
    }
  } else if (update.get_id() == td_api::updateNewCallSignalingData::ID) {
    const auto &u = static_cast<const td_api::updateNewCallSignalingData &>(update);
    on_signaling(u.call_id_, u.data_);
  }
}

bool calls_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  if (cmd == "calls.list") {
    if (link_ready()) {
      calls_list(conn, id);
    } else {
      server.reply_error(conn, id, "unlinked", "not logged in");
    }
    return true;
  }
  if (cmd == "call.start") {
    call_start(conn, id, req);
    return true;
  }
  if (cmd != "call.answer" && cmd != "call.reject" && cmd != "call.hangup") {
    return false;
  }
  auto text = string_field(req, "id");
  std::int64_t call = 0;
  if (text.is_error() || !parse_i64(text.ok(), call)) {
    bad_request(conn, id, "need the field id, a string with a call id");
  } else if (!live || live->id != call || (cmd == "call.answer" && live->state != "ringing")) {
    server.reply_error(conn, id, "not_found", "no such call, or it is not ringing");
  } else if (cmd == "call.answer") {
    tdlib.send(make_object<td_api::acceptCall>(live->id, protocol()), [conn, id](Obj o) { answer(conn, id, o); });
  } else {
    hang_up(false, [conn, id](Obj o) { answer(conn, id, o); });
  }
  return true;
}

bool calls_live() {
  return live.has_value();
}

void calls_hello() {
  if (live) {
    emit();  // the connections that know the call see its state again
  }
}

bool calls_audio_wanted(const std::string &call_id) {
  std::int64_t id = 0;
  return parse_i64(call_id, id) && live && live->id == id && live->state != "ended";
}

void calls_audio_attach(int fd) {
  if (!live || !set_nonblock(fd)) {
    ::close(fd);
    return;
  }
  AudioLink &a = *live->audio;
  {
    std::lock_guard<std::mutex> lock(a.m);
    a.close_fd();  // a new link replaces the old
    a.fd = fd;
    a.mic.clear();
    a.gather.clear();
    a.out.clear();
  }
  say("call %d: audio link attached", live->id);
}
