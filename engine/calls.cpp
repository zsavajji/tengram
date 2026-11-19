// 1:1 calls: TDLib's call states and signalling on one side, Telegram's tgcalls library on the other
// (docs/protocol.md, "Calls"). One call at a time. tgcalls runs on threads of its own: its callbacks only post a
// closure to the main thread, as bb10.cpp does for the navigator, and every closure checks that its call is still
// the live one. The sound goes to and from the UI over a second connection (AudioLink), the picture over a third
// (VideoLink).

#include "engine.h"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

#include "QnxVideo.h"
#include "tgcalls/FakeAudioDeviceModule.h"
#include "tgcalls/Instance.h"
#include "tgcalls/StaticThreads.h"
#include "tgcalls/VideoCaptureInterface.h"
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

// The video link to the UI (docs/protocol.md, "Video link"): records of a 16-byte little-endian header and a payload,
// both ways. One per live call, shared by the call, by its reader thread and by WebRTC's decoder and encoder threads,
// which write without ever blocking.
constexpr std::size_t kVideoHeader = 16, kVideoQueue = 30;
constexpr std::uint32_t kVideoMax = 1u << 20;  // the longest payload the UI may send
// kVideoTune: the target bit rate of WebRTC to the UI, the size of the camera's units from the UI.
constexpr int kVideoUnit = 1, kVideoKeyframe = 2, kVideoTune = 3;

void put_le(char *p, std::uint64_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    p[i] = static_cast<char>(v >> (8 * i));
  }
}

std::uint64_t get_le(const char *p, int bytes) {
  std::uint64_t v = 0;
  for (int i = bytes - 1; i >= 0; --i) {
    v = v << 8 | static_cast<unsigned char>(p[i]);
  }
  return v;
}

struct VideoLink {
  explicit VideoLink(std::int32_t c) : call(c) {
  }

  // Main thread. The new link replaces the old: that one is shut down, its reader joined and its fd closed.
  void attach(int new_fd) {
    stop();
    stopping = false;
    last_target = -1;  // the new link is told the target at WebRTC's next SetRates
    {
      std::lock_guard<std::mutex> lock(m);
      fd = new_fd;
    }
    reader = std::thread([this, new_fd] { read_loop(new_fd); });
  }

  // Main thread. No link afterwards, and no reader. The shutdown wakes the reader (or, if the stack did not, its poll
  // times out within 100 ms and sees `stopping`).
  void stop() {
    stopping = true;
    {
      std::lock_guard<std::mutex> lock(m);
      if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
      }
    }
    if (reader.joinable()) {
      reader.join();
    }
    std::lock_guard<std::mutex> lock(m);
    close_fd();
  }

  // From any thread, never blocking: one record to the UI. The queue keeps 30; when it is full the oldest record not
  // yet started goes (one half sent is finished, so the UI's records stay whole). Nothing without a link: false then.
  bool send_record(int type, int orientation, int flags, const std::uint8_t *data, std::size_t size) {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
    std::string record(kVideoHeader + size, '\0');
    put_le(&record[0], size, 4);
    record[4] = static_cast<char>(type);
    record[5] = static_cast<char>(orientation & 3);
    put_le(&record[6], static_cast<std::uint64_t>(flags), 2);
    put_le(&record[8], static_cast<std::uint64_t>(us.count()), 8);
    if (size) {
      std::memcpy(&record[kVideoHeader], data, size);
    }
    std::lock_guard<std::mutex> lock(m);
    if (fd < 0) {
      return false;
    }
    out.push_back(std::move(record));
    if (out.size() > kVideoQueue) {
      out.erase(out.begin() + (sent > 0 ? 1 : 0));
    }
    while (!out.empty()) {
      const std::string &r = out.front();
      const ssize_t n = ::send(fd, r.data() + sent, r.size() - sent, MSG_DONTWAIT | MSG_NOSIGNAL);
      if (n > 0) {
        sent += static_cast<std::size_t>(n);
        if (sent == r.size()) {
          out.pop_front();
          sent = 0;
        }
      } else if (n < 0 && errno == EINTR) {
        continue;
      } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break;
      } else {
        ::shutdown(fd, SHUT_RDWR);  // the reader sees it end and closes the link
        out.clear();
        sent = 0;
        break;
      }
    }
    return true;
  }

  // From WebRTC's encoder thread (its SetRates): the target bit rate to the UI's camera, a type-3 record of a u32 in
  // bit/s, when it differs by 10 % or more from the last one sent on this link, or none was sent on it yet.
  void send_target(std::uint32_t bps) {
    const std::int64_t last = last_target;
    if (last >= 0 && 10 * std::llabs(static_cast<std::int64_t>(bps) - last) < last) {
      return;
    }
    char payload[4];
    put_le(payload, bps, 4);
    if (send_record(kVideoTune, 0, 0, reinterpret_cast<const std::uint8_t *>(payload), sizeof payload)) {
      last_target = bps;  // without a link nothing was sent: the first estimate on the next one is
      say("call %d: video link: target %u kbit/s to the UI", call, static_cast<unsigned>(bps / 1000));
    }
  }

  // The reader thread: the UI's records until the link ends, then closes it. The call goes on without a picture.
  void read_loop(int f) {
    char head[kVideoHeader];
    std::vector<std::uint8_t> payload;
    while (read_all(f, head, kVideoHeader)) {
      const auto length = static_cast<std::uint32_t>(get_le(head, 4));
      if (length > kVideoMax) {
        say("call %d: video link: a record of %u bytes", call, static_cast<unsigned>(length));
        break;
      }
      payload.resize(length);
      if (length && !read_all(f, payload.data(), length)) {
        break;
      }
      const int type = static_cast<unsigned char>(head[4]);
      if (type == kVideoUnit) {
        const int quarters = static_cast<unsigned char>(head[5]) & 3;
        tgcalls::qnx::pushUnit(payload.data(), length, (get_le(head + 6, 2) & 1) != 0, quarters * 90);
      } else if (type == kVideoKeyframe) {
        tgcalls::qnx::requestPeerKeyframe();
      } else if (type == kVideoTune && length >= 4) {
        // The size of the units that follow: the camera started (again) at it. Before the first unit of that run, on this
        // thread, as the records came.
        const char *size = reinterpret_cast<const char *>(payload.data());
        const int w = static_cast<int>(get_le(size, 2)), h = static_cast<int>(get_le(size + 2, 2));
        if (w > 0 && h > 0) {
          tgcalls::qnx::setCameraSize(w, h);
          say("call %d: video link: the camera's size is %dx%d", call, w, h);
        }
      }  // another type: skipped
    }
    say("call %d: video link closed", call);
    std::lock_guard<std::mutex> lock(m);
    if (fd == f) {  // the main thread may have closed it, after this thread ended
      close_fd();
    }
  }

  // The reader's: n bytes from a non-blocking socket, waiting for them in poll. False at the end of the link or on an
  // error, and once the link is being stopped.
  bool read_all(int f, void *buf, std::size_t n) {
    char *p = static_cast<char *>(buf);
    while (n > 0) {
      if (stopping) {
        return false;
      }
      const ssize_t got = ::recv(f, p, n, 0);
      if (got > 0) {
        p += got;
        n -= static_cast<std::size_t>(got);
      } else if (got == 0) {
        return false;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        pollfd pf = {f, POLLIN, 0};
        poll(&pf, 1, 100);
      } else if (errno != EINTR) {
        return false;
      }
    }
    return true;
  }

  void close_fd() {  // with m held; the queue goes with the link
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
    out.clear();
    sent = 0;
  }

  const std::int32_t call;
  std::atomic<bool> stopping{false};
  std::atomic<std::int64_t> last_target{-1};  // the last target sent on this link in bit/s; -1 when none
  std::thread reader;  // the main thread starts and joins it
  std::mutex m;        // guards the rest
  int fd = -1;         // -1 when there is no link
  std::deque<std::string> out;  // whole records waiting to be written to the UI, the first maybe half sent
  std::size_t sent = 0;         // of out.front()
};

struct Live {
  std::int32_t id = 0;
  std::int64_t user = 0, chat = 0;
  bool out = false;
  bool video = false;  // TDLib's is_video: a video call
  std::string state;  // ringing, calling, connecting, active or ended: the `call` event's
  std::int64_t since_ms = 0;  // unix ms when it became active, 0 before
  std::unique_ptr<tgcalls::Instance> inst;
  std::shared_ptr<AudioLink> audio;
  std::shared_ptr<VideoLink> video_link;
  bool send_video = false;    // we want our camera out: call.start's video, call.answer's, call.video's
  bool video_theirs = false;  // the peer's video is active (tgcalls' remote media state)
  std::shared_ptr<tgcalls::VideoCaptureInterface> capture;  // our video, once we sent any; the instance holds it too
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
    o("video", td::JsonBool(live->video));
    o("videoOurs", td::JsonBool(live->send_video));
    o("videoTheirs", td::JsonBool(live->video_theirs));
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
      ring_start(live->chat, live->video);
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
  // Each returns when WebRTC's thread is out of its function: nothing writes to the link after it is closed.
  tgcalls::qnx::setReceivedUnits({});
  tgcalls::qnx::setKeyframeRequests({});
  tgcalls::qnx::setTargetRates({});
  live->video_link->stop();
  if (tgcalls::Instance *raw = live->inst.release()) {
    raw->stop([raw](tgcalls::FinalState f) {
      const auto &t = f.trafficStats;
      say("call: traffic wifi %llu sent %llu received, mobile %llu sent %llu received",
          static_cast<unsigned long long>(t.bytesSentWifi), static_cast<unsigned long long>(t.bytesReceivedWifi),
          static_cast<unsigned long long>(t.bytesSentMobile), static_cast<unsigned long long>(t.bytesReceivedMobile));
      post([raw] { delete raw; });
    });
  }
  live.reset();  // drops our share of the capture, after the instance's stop began: it holds its own
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

// Our camera: the capture device "camera" is the video link's units (calls/QnxInterface.cpp). Made when we first send
// video, on from now.
void start_capture() {
  if (!live->capture) {
    live->capture = tgcalls::VideoCaptureInterface::Create(tgcalls::StaticThreads::getThreads(), "camera");
  }
  live->capture->setState(tgcalls::VideoState::Active);
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
  d.remoteMediaStateUpdated = [id](tgcalls::AudioState a, tgcalls::VideoState v) {
    post([id, a, v] {
      static const char *const video_names[] = {"inactive", "paused", "active"};
      if (is_live(id)) {
        say("call %d: peer audio %s, video %s", id, a == tgcalls::AudioState::Muted ? "muted" : "active",
            video_names[static_cast<int>(v)]);
        const bool active = v == tgcalls::VideoState::Active;
        if (live->video_theirs != active) {
          live->video_theirs = active;
          emit();
        }
      }
    });
  };
  // The peer's video to the UI, and WebRTC's wish for a keyframe of ours (the UI's camera makes an IDR next).
  tgcalls::qnx::setReceivedUnits([link = live->video_link](const std::uint8_t *data, std::size_t size, bool key,
                                                           int rotation, int, int) {
    link->send_record(kVideoUnit, rotation / 90, key ? 1 : 0, data, size);
  });
  tgcalls::qnx::setKeyframeRequests([link = live->video_link] { link->send_record(kVideoKeyframe, 0, 0, nullptr, 0); });
  // WebRTC's target rate to the UI's camera (a type-3 record; the camera follows it).
  tgcalls::qnx::setTargetRates([link = live->video_link](int bps) { link->send_target(static_cast<std::uint32_t>(bps)); });
  if (live->send_video) {
    start_capture();
    d.videoCapture = live->capture;
  }

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
    live->video_link = std::make_shared<VideoLink>(c.id_);
    live->video = c.is_video_;
    live->send_video = c.is_outgoing_ && c.is_video_;  // call.start's video; an incoming call's is call.answer's
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
  td::JsonValue video;
  if (optional_field(req, "video", td::JsonValue::Type::Boolean, video) < 0) {
    return bad_request(conn, id, "video, if given, is true or false");
  }
  const bool is_video = video.type() == td::JsonValue::Type::Boolean && video.get_boolean();
  if (live) {
    return server.reply_error(conn, id, "busy", "a call is under way");
  }
  with_chat(conn, id, chat, [conn, id, is_video](const td_api::chat &c) {
    const std::int64_t user = person_of(c);
    if (user == 0) {
      return server.reply_error(conn, id, "bad_chat", "not a chat with a person");
    }
    if (live) {  // one came in while the chat was fetched
      return server.reply_error(conn, id, "busy", "a call is under way");
    }
    tdlib.send(make_object<td_api::createCall>(user, protocol(), is_video), [conn, id](Obj o) {
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

// call.video: our camera on or off, for the call under way.
void call_video(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto text = string_field(req, "id");
  std::int64_t call = 0;
  td::JsonValue on;
  if (text.is_error() || !parse_i64(text.ok(), call)) {
    return bad_request(conn, id, "need the field id, a string with a call id");
  }
  if (optional_field(req, "on", td::JsonValue::Type::Boolean, on) != 1) {
    return bad_request(conn, id, "need the field on, true or false");
  }
  if (!live || live->id != call) {
    return server.reply_error(conn, id, "not_found", "no such call");
  }
  live->send_video = on.get_boolean();
  if (live->inst) {  // before the instance, make_instance does it from send_video
    if (live->send_video) {
      start_capture();
      live->inst->setVideoCapture(live->capture);
    } else if (live->capture) {
      live->capture->setState(tgcalls::VideoState::Inactive);
      live->inst->setVideoCapture(nullptr);
    }
  }
  say("call %d: our video %s", live->id, live->send_video ? "on" : "off");
  emit();
  server.reply_ok(conn, id);
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
  // From the threads of tgcalls and WebRTC: say formats on the stack and writes a line to stderr in one call, so it is
  // safe from any thread (its clock is const).
  tgcalls::qnx::setLog([](const std::string &line) { say("video: %s", line.c_str()); });
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
  if (cmd == "call.video") {
    call_video(conn, id, req);
    return true;
  }
  if (cmd != "call.answer" && cmd != "call.reject" && cmd != "call.hangup") {
    return false;
  }
  auto text = string_field(req, "id");
  std::int64_t call = 0;
  td::JsonValue video;
  if (text.is_error() || !parse_i64(text.ok(), call)) {
    bad_request(conn, id, "need the field id, a string with a call id");
  } else if (cmd == "call.answer" && optional_field(req, "video", td::JsonValue::Type::Boolean, video) < 0) {
    bad_request(conn, id, "video, if given, is true or false");
  } else if (!live || live->id != call || (cmd == "call.answer" && live->state != "ringing")) {
    server.reply_error(conn, id, "not_found", "no such call, or it is not ringing");
  } else if (cmd == "call.answer") {
    // A video call is answered with our video, as offered, unless the UI says otherwise.
    const bool ours = video.type() == td::JsonValue::Type::Boolean ? video.get_boolean() : live->video;
    if (live->send_video != ours) {
      live->send_video = ours;
      emit();
    }
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

namespace {

// A link's hello names the live call.
bool call_wanted(const std::string &call_id) {
  std::int64_t id = 0;
  return parse_i64(call_id, id) && live && live->id == id && live->state != "ended";
}

}  // namespace

bool calls_audio_wanted(const std::string &call_id) {
  return call_wanted(call_id);
}

bool calls_video_wanted(const std::string &call_id) {
  return call_wanted(call_id);
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

void calls_video_attach(int fd) {
  if (!live || !set_nonblock(fd)) {
    ::close(fd);
    return;
  }
  live->video_link->attach(fd);  // a new link replaces the old
  // The UI's decoder starts with nothing: the peer's next keyframe, rather than its own interval (~5 s from Android).
  tgcalls::qnx::requestPeerKeyframe();
  say("call %d: video link attached", live->id);
}
