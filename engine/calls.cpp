// 1:1 voice calls: TDLib's call states and signalling on one side, Telegram's tgcalls library on the other
// (docs/protocol.md, "Calls"). One call at a time. tgcalls runs on threads of its own: its callbacks only post a
// closure to the main thread, as bb10.cpp does for the navigator, and every closure checks that its call is still
// the live one. Gate T0: no UI and no real audio yet, a test tone is the microphone (see Tone and Peer).

#include "engine.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <set>

#include "tgcalls/FakeAudioDeviceModule.h"
#include "tgcalls/Instance.h"
#include "tgcalls/InstanceImpl.h"
#include "tgcalls/v2/InstanceV2Impl.h"
#include "tgcalls/v2/InstanceV2ReferenceImpl.h"

namespace {

using td_api::make_object;

struct Live {
  std::int32_t id = 0;
  std::int64_t user = 0, chat = 0;
  bool out = false;
  std::string state;  // ringing, calling, connecting, active or ended: the `call` event's
  std::int64_t since_ms = 0;  // unix ms when it became active, 0 before
  std::unique_ptr<tgcalls::Instance> inst;
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

// ---- the test audio of gate T0, until the audio link to the UI: the microphone is a 440 Hz tone, and what the peer says
// is not played but measured, its level in the log.

constexpr int kRate = 48000, kFrame = kRate / 100;  // 10 ms of mono

class Tone final : public tgcalls::FakeAudioDeviceModule::Recorder {
 public:
  tgcalls::AudioFrame Record() override {
    for (int i = 0; i < kFrame; ++i, phase_ = (phase_ + 1) % kRate) {  // 440 whole cycles in kRate samples
      buf_[i] = static_cast<std::int16_t>(6000 * std::sin(6.283185307179586 * 440 * phase_ / kRate));
    }
    return {buf_, kFrame, 2, 1, kRate, 0, 0};
  }

 private:
  std::int16_t buf_[kFrame];
  int phase_ = 0;
};

class Peer final : public tgcalls::FakeAudioDeviceModule::Renderer {
 public:
  bool Render(const tgcalls::AudioFrame &f) override {  // tgcalls' audio thread
    const std::size_t n = f.num_samples * f.num_channels;
    for (std::size_t i = 0; i < n; ++i) {
      sum_ += static_cast<double>(f.audio_samples[i]) * f.audio_samples[i];
    }
    count_ += n;
    if (++frames_ == 200) {  // 2 s
      say("call audio: peer rms %ld", count_ ? std::lround(std::sqrt(sum_ / static_cast<double>(count_))) : 0L);
      sum_ = 0;
      count_ = 0;
      frames_ = 0;
    }
    return true;
  }

 private:
  double sum_ = 0;
  std::size_t count_ = 0;
  int frames_ = 0;
};

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
    live->state = s;
    emit();
  }
}

// The call is over. The instance stops on its own threads: the completion logs the traffic and hands it back to the
// main thread to be deleted (never on tgcalls' thread).
void end(const char *reason) {
  say("call %d: ended (%s)", live->id, reason);
  live->state = "ended";
  emit(reason);
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
  d.createAudioDeviceModule =
      tgcalls::FakeAudioDeviceModule::Creator(std::make_shared<Peer>(), std::make_shared<Tone>(), audio);

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

}  // namespace

void calls_start(int wake) {
  wake_fd = wake;
  tgcalls::Register<tgcalls::InstanceImpl>();
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
