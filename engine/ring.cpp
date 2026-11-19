// What an incoming call does until it stops ringing: the phone's ringtone, and the Hub's call item (notifier_call).
// Ported from ../hyperberry/engine/calls_ring.go and ringtone_qnx.go, which are proven on the Q10. The tone plays
// through mm-renderer, the system's player service, so the engine holds no audio. It runs on a thread of its own: no
// call into mm-renderer is made on the main thread except by the join in ring_stop, and the thread's calls are fast.

#include "engine.h"

#include <mm/renderer.h>
#include <mm/renderer/events.h>
#include <sys/stat.h>

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::steady_clock;

// The notify service's status says whether the phone's mode lets a call ring. The user's own ringtone (cat.phone) and
// the mode's name (global) are in /pps/services/notify/settings, which an app can't read (an ACL; the Q10, 2026-11-19),
// so a call rings with the stock tone (the user: fine), and Phone only mode, whose status says sound off, doesn't ring.
const char *const kStatus = "/pps/services/notify/status";
const char *const kTone = "/usr/share/sounds/notification_phone_ringer.wav";
// The tone ends after this whatever happens: WhatsApp stops ringing sooner.
constexpr seconds kMax(60);

std::mutex mu;
std::condition_variable cv;
bool stopping = false;  // guarded by mu: the tone is to end
std::thread player;     // the main thread's; joinable from ring_start until the next ring_stop

// The value of key in a PPS object ("key:type:value" lines, the type often empty), "" when the object or the key is
// missing.
std::string pps_value(const char *path, const std::string &key) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  for (std::string line; std::getline(text, line);) {
    const std::size_t first = line.find(':');
    if (first == std::string::npos || line.compare(0, first, key) != 0) {
      continue;
    }
    const std::size_t second = line.find(':', first + 1);
    if (second != std::string::npos) {
      return line.substr(second + 1);
    }
  }
  return "";
}

// Whether the phone's notification mode lets a call ring (the user's call, 2026-11-07: Silent must not ring). The
// notify service's status says sound off in Silent, Vibrate, Alerts off and the like. Unreadable, it rings. The Hub
// item's own alert follows the mode by itself, vibration included.
bool ring_allowed() {
  return pps_value(kStatus, "sound") != "off";
}

// ---- mm-renderer

// One ringtone: the connection and the context it plays in.
struct Ring {
  mmr_connection_t *conn = nullptr;
  mmr_context_t *ctx = nullptr;
};

// Plays url, repeating. 0 on success, else the step that failed; close_ring cleans up after either.
int open_ring(Ring &r, const std::string &url) {
  strm_dict_t *d;
  int out;
  if (!(r.conn = mmr_connect(nullptr))) {
    return 1;
  }
  if (!(r.ctx = mmr_context_create(r.conn, "tengram_ring", 0, S_IRWXU))) {
    return 2;
  }
  if ((out = mmr_output_attach(r.ctx, "audio:default", "audio")) < 0) {
    return 3;
  }
  // The dictionaries become the API's, even when a call fails.
  if (!(d = strm_dict_new()) || !(d = strm_dict_set(d, "audio_type", "ringtone"))) {
    return 4;
  }
  if (mmr_output_parameters(r.ctx, out, d) != 0) {
    return 5;
  }
  if (mmr_input_attach(r.ctx, url.c_str(), "track") != 0) {
    return 6;
  }
  if (!(d = strm_dict_new()) || !(d = strm_dict_set(d, "repeat", "track"))) {
    return 7;
  }
  if (mmr_input_parameters(r.ctx, d) != 0) {
    return 8;
  }
  if (mmr_play(r.ctx) != 0) {
    return 9;
  }
  return 0;
}

// Starts the tone over once mm-renderer has stopped at its end: on the Q10 the repeat parameter didn't loop it
// (HyperBerry 0.0.1.185 rang once). It drains the pending events; the last, MMR_EVENT_NONE, still carries the
// context's state. True when it started over.
bool play_again(Ring &r) {
  const mmr_event_t *ev;
  do {
    ev = mmr_event_get(r.ctx);
  } while (ev && ev->type != MMR_EVENT_NONE);
  if (!ev || ev->state != MMR_STATE_STOPPED) {
    return false;
  }
  mmr_seek(r.ctx, "0");
  return mmr_play(r.ctx) == 0;
}

void close_ring(Ring &r) {
  if (r.ctx) {
    mmr_stop(r.ctx);
    mmr_context_destroy(r.ctx);
  }
  if (r.conn) {
    mmr_disconnect(r.conn);
  }
  r = Ring();
}

// ---- the thread

// False when the tone is to end, true after t of it.
bool ringing_after(steady_clock::duration t) {
  std::unique_lock<std::mutex> lock(mu);
  return !cv.wait_for(lock, t, [] { return stopping; });
}

// The player thread: the tone until ring_stop, or until kMax.
void play() {
  if (!ring_allowed()) {
    say("ringtone: off, the phone's notification mode has no sound");
    return;
  }
  Ring r;
  if (const int step = open_ring(r, std::string("file://") + kTone); step != 0) {
    close_ring(r);
    say("ringtone: mm-renderer failed at step %d", step);
    return;
  }
  say("ringtone %s", kTone);
  const auto end = steady_clock::now() + kMax;
  auto last = steady_clock::time_point();
  int quick = 0;
  // A look every half second whether the tone has ended, to start it over.
  while (ringing_after(milliseconds(500)) && steady_clock::now() < end) {
    if (!play_again(r)) {
      continue;
    }
    // A tone that stops again at once is failing, not ending: three times and it's left off.
    const auto now = steady_clock::now();
    quick = now - last < seconds(2) ? quick + 1 : 0;
    last = now;
    if (quick == 3) {
      say("ringtone: stops again at once, left off");
      break;
    }
  }
  close_ring(r);
}

// The tone ends and its thread with it. Nothing happens when none is playing.
void stop_tone() {
  if (!player.joinable()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mu);
    stopping = true;
  }
  cv.notify_all();
  player.join();
}

}  // namespace

void ring_start(std::int64_t chat) {
  stop_tone();
  notifier_call(chat, true);
  {
    std::lock_guard<std::mutex> lock(mu);
    stopping = false;
  }
  player = std::thread(play);
}

void ring_stop(std::int64_t chat) {
  stop_tone();
  notifier_call(chat, false);
}
