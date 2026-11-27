// location.get, map.get, location.send, location.live, location.stop, location.extend, location.alert and location.active,
// the `live` event, the phone's position and our live locations kept current while they run (docs/protocol.md,
// "Locations").
//
// The position comes from BPS' geolocation service, on a thread of its own while somebody wants it (BPS is per thread,
// as bb10.cpp explains): one fix for a location.get, a fix every 30 s while we share a live location. What the thread
// finds goes to the main thread through a queue and one byte on the wake socket, as bb10.cpp does it. Everything else
// here is the main thread's.

#include "engine.h"
#include "https.h"

#include "td/utils/misc.h"

#include <bps/bps.h>
#include <bps/geolocation.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using td_api::make_object;

// ---- the position thread

enum class Want { Off, Once, Live };

// What the thread found: a fix, or why there is none.
struct Posted {
  enum Kind { kFix, kOff, kNoFix } kind = kFix;  // kOff: the service is off or not allowed; kNoFix: no position in 30 s
  double lat = 0, lon = 0, accuracy = 0;         // accuracy 0 when the fix has none
  int heading = 0;                               // 0 when the fix has none, else 1 to 360
};

int wake_fd = -1;
std::mutex mu;
Want want = Want::Off;      // guarded by mu: what the main thread wants the thread to do
bool asked = false;         // guarded by mu: a location.get came since the thread last looked, and waits for a fix
bool running = false;       // guarded by mu: the thread exists
std::vector<Posted> queue;  // guarded by mu

// What a place search's thread hands back, to run on the main thread (location_drain), as translate.cpp's.
std::vector<std::function<void()>> jobs;  // guarded by mu

void post_job(std::function<void()> f) {
  {
    std::lock_guard<std::mutex> lock(mu);
    jobs.push_back(std::move(f));
  }
  ssize_t n = write(wake_fd, "l", 1);
  (void)n;
}

void post(Posted p) {
  bool was_empty;
  {
    std::lock_guard<std::mutex> lock(mu);
    was_empty = queue.empty();
    queue.push_back(p);
  }
  if (was_empty) {
    ssize_t n = write(wake_fd, "l", 1);
    (void)n;
  }
}

Posted failure(Posted::Kind kind) {
  Posted p;
  p.kind = kind;
  return p;
}

constexpr int kLiveSeconds = 30;                         // a live location's fixes come this often
constexpr auto kNoFixAfter = std::chrono::seconds(30);   // a location.get whose fix does not come
constexpr auto kRetryAfter = std::chrono::seconds(60);   // a request the service refused is made again

// The GPS' heading, in degrees, as Telegram's: 1 to 360, with 0 degrees as 360.
int heading_of(double degrees) {
  int h = static_cast<int>(std::lround(degrees)) % 360;
  if (h < 0) {
    h += 360;
  }
  return h == 0 ? 360 : h;
}

void run() {
  if (bps_initialize() != BPS_SUCCESS) {
    say("location: bps_initialize failed");
    post(failure(Posted::kOff));
    // ponytail: the thread stays 30 s before it lets a new one start, so that a phone that cannot start BPS is not asked
    // again at once by every `off` it answers. Nothing is better to do on a phone without BPS.
    std::this_thread::sleep_for(std::chrono::seconds(30));
    std::lock_guard<std::mutex> lock(mu);
    running = false;
    return;
  }
  // Satellites only. The default provider, hybrid, also asks BlackBerry's server where the Wi-Fi networks around are, and
  // that server is gone: on the Q10 (2026-11-27) rimlocp scanned Wi-Fi and failed to connect every 2 s for as long as a
  // request was out, and no position ever came from it.
  if (geolocation_set_provider(GEOLOCATION_PROVIDER_GNSS) != BPS_SUCCESS) {
    say("location: no GNSS provider");
  }
  bool requested = false;    // a request for events is out
  bool spent = false;        // a single fix's request has given its fix: it gives no other
  Want applied = Want::Off;  // what the last request was for
  Clock::time_point applied_at;
  bool waiting = false;  // a location.get waits for a fix, since waiting_at
  Clock::time_point waiting_at;
  for (;;) {
    Want w;
    bool fresh;
    {
      std::lock_guard<std::mutex> lock(mu);
      w = want;
      fresh = asked;
      asked = false;
      if (w == Want::Off) {
        running = false;  // in the lock that read Off, so set_want can never miss starting a new thread
      }
    }
    if (w == Want::Off) {
      if (requested) {
        geolocation_stop_events(0);
      }
      bps_shutdown();
      return;
    }
    const auto now = Clock::now();
    if (fresh) {
      waiting = true;
      waiting_at = now;
    }
    // A new request when what is wanted changed; when a location.get came and the last request can't answer it (a single
    // fix's that has given its fix, or one the service refused); and a minute after the service refused one, since it may
    // be on again (a live location goes on when the user turns Location Services back on). A single fix's request still
    // looking is left alone when another location.get comes: asked again, the GPS would start its search over.
    if (w != applied || (fresh && (!requested || (w == Want::Once && spent))) ||
        (!requested && now - applied_at >= kRetryAfter)) {
      if (requested) {
        geolocation_stop_events(0);
        requested = false;
      }
      geolocation_set_period(w == Want::Live ? kLiveSeconds : 0);  // 0: one fix
      geolocation_set_background(w == Want::Live);                 // a live location's fixes come with the screen off too
      if (geolocation_request_events(0) == BPS_SUCCESS) {
        requested = true;
      } else {
        say("location: geolocation_request_events failed");
        waiting = false;
        post(failure(Posted::kOff));
      }
      spent = false;
      applied = w;
      applied_at = now;
    }
    bps_event_t *ev = nullptr;
    // ponytail: the thread looks at what is wanted once a second while the GPS is on; a bps_channel_push_event from the main
    // thread would end the wait at once, if that second ever shows.
    if (bps_get_event(&ev, 1000) == BPS_SUCCESS && ev && bps_event_get_domain(ev) == geolocation_get_domain()) {
      if (bps_event_get_code(ev) == GEOLOCATION_INFO) {
        if (geolocation_event_is_latitude_valid(ev) && geolocation_event_is_longitude_valid(ev)) {
          Posted p;
          p.lat = geolocation_event_get_latitude(ev);
          p.lon = geolocation_event_get_longitude(ev);
          if (geolocation_event_is_accuracy_valid(ev)) {
            p.accuracy = geolocation_event_get_accuracy(ev);
          }
          if (geolocation_event_is_heading_valid(ev) && std::isfinite(geolocation_event_get_heading(ev))) {
            p.heading = heading_of(geolocation_event_get_heading(ev));
          }
          if (std::isfinite(p.lat) && std::isfinite(p.lon) && std::isfinite(p.accuracy)) {
            waiting = false;
            spent = applied == Want::Once;
            post(p);
          }
        }
      } else if (bps_event_get_code(ev) == GEOLOCATION_ERROR) {
        const int code = geolocation_event_get_error_code(ev);
        const char *message = geolocation_event_get_error_message(ev);
        say("location: error %d %s", code, message ? message : "");
        // The 0x1xxxx codes are warnings: the request goes on. After one of these three it is over.
        if (code == GEOLOCATION_ERROR_FATAL_DISABLED || code == GEOLOCATION_ERROR_FATAL_INSUFFICIENT_PROVIDERS ||
            code == GEOLOCATION_ERROR_FATAL_PERMISSION) {
          geolocation_stop_events(0);
          requested = false;
          waiting = false;
          post(failure(Posted::kOff));
        }
      }
    }
    if (waiting && Clock::now() - waiting_at >= kNoFixAfter) {
      waiting = false;  // told once; the request goes on looking, and the next location.get waits on it again
      post(failure(Posted::kNoFix));
    }
  }
}

// Main thread: what the thread is to do from now on. It is started when something is wanted and ends by itself when
// nothing is. ask: a location.get has just come and waits for a fix.
void set_want(Want w, bool ask = false) {
  std::lock_guard<std::mutex> lock(mu);
  want = w;
  asked = asked || ask;
  if (w != Want::Off && !running) {
    running = true;
    std::thread(run).detach();
  }
}

// ---- the main thread's state

struct Fix {
  double lat = 0, lon = 0, accuracy = 0;
  int heading = 0;
};
bool have_last = false;
Fix last;
Clock::time_point last_at;

struct Waiter {
  std::uint64_t conn;
  std::string id;
};
std::vector<Waiter> waiters;  // the location.get requests not answered yet

// One of our live locations still running, as TDLib lists it.
struct Live {
  std::int64_t chat, id;
  std::int32_t date, period, radius;  // period: seconds from date, INT32_MAX for one with no end; radius: the alert's metres
  double lat, lon;                    // what Telegram has for it now
  Clock::time_point sent_at;          // when we last sent a position
  bool due;                           // someone is looking: the next fix goes out whatever the distance
};
std::vector<Live> lives;

Want wanted() {
  return !lives.empty() ? Want::Live : !waiters.empty() ? Want::Once : Want::Off;
}

void settle() {
  set_want(wanted());
}

Live *find_live(std::int64_t chat, std::int64_t id) {
  for (Live &l : lives) {
    if (l.chat == chat && l.id == id) {
      return &l;
    }
  }
  return nullptr;
}

// The body of the `live` event, and the result of location.active (without `ev`).
std::string items_body(bool event) {
  return td::json_encode<std::string>(td::json_object([&](auto &o) {
    if (event) {
      o("ev", "live");
    }
    o("items", td::json_array([&](auto &items) {
      for (const Live &l : lives) {
        items(td::json_object([&](auto &one) {
          one("chat", std::to_string(l.chat));
          one("id", std::to_string(l.id));
          one("until", l.period == INT32_MAX ? std::int64_t{0} : std::int64_t{l.date} + l.period);
          one("alert", l.radius);
        }));
      }
    }));
  }));
}

void broadcast_live() {
  server.broadcast(items_body(true));
}

std::string fix_json(const Fix &f) {
  return td::json_encode<std::string>(td::json_object([&](auto &o) {
    o("lat", f.lat);
    o("lon", f.lon);
    o("accuracy", f.accuracy);
  }));
}

void answer_waiters(const char *code, const char *message) {
  std::vector<Waiter> now;
  now.swap(waiters);
  for (const Waiter &w : now) {
    server.reply_error(w.conn, w.id, code, message);
  }
}

// The great-circle distance in metres (haversine, a sphere of the earth's mean radius).
double metres_between(double lat1, double lon1, double lat2, double lon2) {
  constexpr double kRad = 3.14159265358979323846 / 180;
  const double a = std::sin((lat2 - lat1) * kRad / 2), b = std::sin((lon2 - lon1) * kRad / 2);
  const double h = a * a + std::cos(lat1 * kRad) * std::cos(lat2 * kRad) * b * b;
  return 2 * 6371000.0 * std::asin(std::min(1.0, std::sqrt(h)));
}

// The user's rule (2026-11-27): the fixes come at most every 30 s (the thread's period); one is sent when it moved 20 m
// or more, when someone just looked at it, and at least once every 5 minutes.
constexpr double kMoveMetres = 20;
constexpr auto kSendAtLeast = std::chrono::minutes(5);

void send_position(Live &l, const Fix &f) {
  l.lat = f.lat;
  l.lon = f.lon;
  l.sent_at = Clock::now();
  l.due = false;
  // live_period 0 leaves the period as it is. The radius is repeated in every edit: TDLib always sends it, so an edit
  // without it would switch the alert off.
  tdlib.send(make_object<td_api::editMessageLiveLocation>(
                 l.chat, l.id, nullptr,
                 make_object<td_api::liveLocation>(make_object<td_api::location>(f.lat, f.lon, f.accuracy), 0,
                                                   f.heading, l.radius)),
             [](Obj o) {
               const auto *e = as_error(o);
               if (e && e->message_.find("MESSAGE_NOT_MODIFIED") == std::string::npos) {
                 say("live location: %d %s", e->code_, e->message_.c_str());
               }
             });
}

void fix_arrived(const Fix &f) {
  const auto now = Clock::now();
  for (Live &l : lives) {
    if (l.due || now - l.sent_at >= kSendAtLeast || metres_between(l.lat, l.lon, f.lat, f.lon) >= kMoveMetres) {
      send_position(l, f);
    }
  }
}

// TDLib's list of our live locations, as its update gives it: the list is rebuilt from it. There is no request for the
// list: TDLib keeps it in its database, and sends the update by itself at start when it is not empty.
void set_lives(const std::vector<td_api::object_ptr<td_api::message>> &messages) {
  std::vector<Live> next;
  for (const auto &m : messages) {
    if (!m || !m->content_ || m->content_->get_id() != td_api::messageLiveLocation::ID) {
      continue;
    }
    const auto &c = static_cast<const td_api::messageLiveLocation &>(*m->content_);
    if (!c.location_ || !c.location_->location_) {
      continue;
    }
    Live n = {m->chat_id_, m->id_, m->date_, c.location_->live_period_, c.location_->proximity_alert_radius_,
              c.location_->location_->latitude_, c.location_->location_->longitude_, Clock::now(), false};
    if (const Live *old = find_live(n.chat, n.id)) {
      n.sent_at = old->sent_at;
      n.due = old->due;
    }
    next.push_back(n);
  }
  const bool was = !lives.empty();
  lives.swap(next);
  broadcast_live();
  settle();
  if (was != !lives.empty()) {
    say("live location: sharing in %zu chats", lives.size());
    mode_evaluate();
  }
}

// ---- the requests

// A number field in lo..hi, as a double. Optional: missing leaves `out` as it is. False after the bad_request reply.
bool number_field(std::uint64_t conn, const std::string &id, td::JsonObject &req, const char *name, double lo, double hi,
                  bool optional, double &out) {
  td::JsonValue v;
  const int given = optional_field(req, name, td::JsonValue::Type::Number, v);
  if (given == 0 && optional) {
    return true;
  }
  const double n = given == 1 ? td::to_double(v.get_number()) : 0;
  if (given != 1 || !std::isfinite(n) || n < lo || n > hi) {
    bad_request(conn, id, std::string(name) + " is a number from " + std::to_string(static_cast<long long>(lo)) + " to " +
                              std::to_string(static_cast<long long>(hi)));
    return false;
  }
  out = n;
  return true;
}

// A whole number field in lo..hi. Optional as number_field.
bool whole_field(std::uint64_t conn, const std::string &id, td::JsonObject &req, const char *name, std::int64_t lo,
                 std::int64_t hi, bool optional, std::int64_t &out) {
  td::JsonValue v;
  const int given = optional_field(req, name, td::JsonValue::Type::Number, v);
  if (given == 0 && optional) {
    return true;
  }
  std::int64_t n = 0;
  if (given != 1 || !parse_i64(v.get_number().str(), n) || n < lo || n > hi) {
    bad_request(conn, id, std::string(name) + " is a whole number from " + std::to_string(lo) + " to " + std::to_string(hi));
    return false;
  }
  out = n;
  return true;
}

// The point of a location request: lat and lon, in degrees.
bool point_fields(std::uint64_t conn, const std::string &id, td::JsonObject &req, double &lat, double &lon) {
  return number_field(conn, id, req, "lat", -90, 90, false, lat) && number_field(conn, id, req, "lon", -180, 180, false, lon);
}

void location_get(std::uint64_t conn, const std::string &id) {
  if (!lives.empty() && have_last && Clock::now() - last_at < std::chrono::seconds(60)) {
    return server.reply_ok(conn, id, fix_json(last));  // the thread is running already: this fix is fresh
  }
  waiters.push_back({conn, id});
  set_want(wanted(), true);
}

std::string point_json(double lat, double lon) {
  return td::json_encode<std::string>(td::json_object([&](auto &o) {
    o("lat", lat);
    o("lon", lon);
  }));
}

// Where the phone last was, as far as the engine knows, for a map to start from before any fix: its last fix of this run,
// else what it last sent of a live location that runs. Not the system's "last known position": asked through BPS
// (geolocation_set_last_known), the location manager answered nothing for 2 s and then refused the request as malformed
// ("missing 'dat' or 'period'", the Q10, 1.0.5.189), and that phone had no last position anyway, as its own log said to
// another asker. The UI keeps the last point it showed for the starts the engine has nothing for.
void location_last(std::uint64_t conn, const std::string &id) {
  if (have_last) {
    return server.reply_ok(conn, id, point_json(last.lat, last.lon));
  }
  if (!lives.empty()) {
    return server.reply_ok(conn, id, point_json(lives.front().lat, lives.front().lon));
  }
  server.reply_error(conn, id, "not_found", "no position yet");
}

// ---- location.search: an address or a place's name to points, by OpenStreetMap's Nominatim

constexpr int kSearchSecs = 15;
constexpr std::size_t kSearchBytes = 200;  // of query
constexpr int kSearchPlaces = 8;

// Everything but letters, digits and -._~ as %XX, as a URL's query takes it.
std::string url_encoded(const std::string &text) {
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : text) {
    if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

// A string member of an object; "" when it is missing or not a string.
std::string text_member(td::JsonValue &object, const char *name) {
  if (object.type() != td::JsonValue::Type::Object) {
    return "";
  }
  td::JsonValue v = object.get_object().extract_field(td::Slice(name));
  return v.type() == td::JsonValue::Type::String ? v.get_string().str() : std::string();
}

// Nominatim's answer (main thread): a JSON array of places, each with display_name and lat and lon as strings.
void search_answered(std::uint64_t conn, const std::string &id, const HttpsAnswer &a) {
  if (a.status == 0) {
    say("location.search: no answer");
    return server.reply_error(conn, id, "offline", "no answer from the place search");
  }
  std::string body = a.body;  // the parsed values point into it
  auto parsed = a.status == 200 ? td::json_decode(td::MutableSlice(body)) : td::Status::Error("not 200");
  if (parsed.is_error() || parsed.ok_ref().type() != td::JsonValue::Type::Array) {
    say("location.search: HTTP %d", a.status);
    return server.reply_error(conn, id, "search_failed", "the place search answered HTTP " + std::to_string(a.status));
  }
  server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &o) {
    o("places", td::json_array([&](auto &places) {
      for (td::JsonValue &item : parsed.ok_ref().get_array()) {
        const std::string name = text_member(item, "display_name");
        const std::string lat = text_member(item, "lat"), lon = text_member(item, "lon");
        const double y = td::to_double(lat), x = td::to_double(lon);
        if (name.empty() || lat.empty() || lon.empty() || !std::isfinite(y) || !std::isfinite(x) || y < -90 || y > 90 ||
            x < -180 || x > 180) {
          continue;
        }
        places(td::json_object([&](auto &one) {
          one("name", name);
          one("lat", y);
          one("lon", x);
        }));
      }
    }));
  })));
}

// `query` is what the user typed, an address or a place's name: it goes to nominatim.openstreetmap.org (the only thing
// sent, with the app's name as the User-Agent its usage policy asks for and the UI's language for the names), one request
// for each search the user submits. The phone's own geocoder is BlackBerry's and gone with its servers, and Telegram has
// none: its venue search wants a position first.
void location_search(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto query = string_field(req, "query");
  const std::string text = query.is_ok() ? trim(query.ok()) : std::string();
  if (text.empty() || text.size() > kSearchBytes) {
    return bad_request(conn, id, "query is a text of 1 to 200 bytes");
  }
  std::string path = "/search?format=jsonv2&limit=" + std::to_string(kSearchPlaces) + "&q=" + url_encoded(text);
  const std::string language = mode_language().substr(0, mode_language().find_first_of("-_"));
  if (language_code(language)) {
    path += "&accept-language=" + language;
  }
  // The thread touches only its own copies and post_job(); the answer is parsed and sent on the main thread.
  std::thread([conn, id, path] {
    HttpsAnswer a = https_get("nominatim.openstreetmap.org", path, {"User-Agent: Tengram (Telegram for BlackBerry 10)"},
                              kSearchSecs);
    post_job([conn, id, a = std::move(a)] { search_answered(conn, id, a); });
  }).detach();
}

void map_get(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  double lat = 0, lon = 0;
  std::int64_t zoom = 0, width = 0, height = 0, chat = 0;
  td::JsonValue v;
  if (!point_fields(conn, id, req, lat, lon) || !whole_field(conn, id, req, "zoom", 13, 20, false, zoom) ||
      !whole_field(conn, id, req, "width", 16, 1024, false, width) ||
      !whole_field(conn, id, req, "height", 16, 1024, false, height)) {
    return;
  }
  const int given = optional_field(req, "chat", td::JsonValue::Type::String, v);
  if (given < 0 || (given == 1 && !parse_i64(v.get_string().str(), chat))) {
    return bad_request(conn, id, "chat is a string with a number");
  }
  // Nothing is copied or kept: the page that asks shows the file where TDLib has it.
  tdlib.send(make_object<td_api::getMapThumbnailFile>(make_object<td_api::location>(lat, lon, 0), static_cast<int>(zoom),
                                                       static_cast<int>(width), static_cast<int>(height), 2, chat),
             [conn, id](Obj o) {
               if (auto e = as_error(o)) {
                 say("map.get: %d %s", e->code_, e->message_.c_str());
                 return reply_td_error(conn, id, *e);
               }
               download_path(static_cast<const td_api::file *>(o.get()), "map.get", [conn, id](const std::string &path) {
                 if (path.empty()) {
                   return server.reply_error(conn, id, "download_failed", "the map did not download");
                 }
                 server.reply_ok(conn, id, path_json(path));
               });
             });
}

void location_send(std::uint64_t conn, const std::string &id, td::JsonObject &req, bool live) {
  std::int64_t chat = 0, period = 0, heading = 0, thread = 0;
  std::int32_t topic = -1;
  double lat = 0, lon = 0, accuracy = 0;
  td_api::object_ptr<td_api::InputMessageReplyTo> reply;
  if (!request_chat(conn, id, req, chat) || !point_fields(conn, id, req, lat, lon) ||
      !number_field(conn, id, req, "accuracy", 0, 1500, true, accuracy)) {
    return;
  }
  if (live) {
    // 0 is "until I turn it off", which Telegram knows as 0x7FFFFFFF; else a minute to a day.
    if (!whole_field(conn, id, req, "period", 0, 86400, false, period)) {
      return;
    }
    if (period != 0 && period < 60) {
      return bad_request(conn, id, "period is 0, or a whole number from 60 to 86400");
    }
    if (!whole_field(conn, id, req, "heading", 0, 360, true, heading)) {
      return;
    }
  }
  if (!quote_field(conn, id, req, reply) || !topic_thread_fields(conn, id, req, topic, thread)) {
    return;
  }
  auto where = make_object<td_api::location>(lat, lon, accuracy);
  if (live) {
    // TDLib lists the message as an active live location when it is sent (updateActiveLiveLocationMessages), which starts
    // the position thread: nothing to do here.
    send_content(conn, id, chat, topic, thread, std::move(reply),
                 make_object<td_api::inputMessageLiveLocation>(make_object<td_api::liveLocation>(
                     std::move(where), period == 0 ? INT32_MAX : static_cast<std::int32_t>(period),
                     static_cast<std::int32_t>(heading), 0)),
                 "");
  } else {
    send_content(conn, id, chat, topic, thread, std::move(reply),
                 make_object<td_api::inputMessageLocation>(std::move(where)), "");
  }
}

void location_stop(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  std::int64_t chat = 0, message = 0;
  if (!request_chat(conn, id, req, chat) || !message_id(conn, id, req, "id", message)) {
    return;
  }
  // A null location stops it. TDLib then sends the new list of live locations.
  tdlib.send(make_object<td_api::editMessageLiveLocation>(chat, message, nullptr, nullptr),
             [conn, id](Obj o) { answer(conn, id, o); });
}

void location_extend(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  std::int64_t chat = 0, message = 0, add = 0;
  if (!request_chat(conn, id, req, chat) || !message_id(conn, id, req, "id", message) ||
      !whole_field(conn, id, req, "add", 60, 86400, false, add)) {
    return;
  }
  const Live *l = find_live(chat, message);
  if (!l) {
    return server.reply_error(conn, id, "not_found", "not a live location of ours that is running");
  }
  if (l->period == INT32_MAX || std::int64_t{l->period} + add >= INT32_MAX) {
    return bad_request(conn, id, "this live location has no end to move");
  }
  const std::int32_t period = static_cast<std::int32_t>(l->period + add);  // counted from the message's date
  tdlib.send(make_object<td_api::editMessageLiveLocation>(
                 chat, message, nullptr,
                 make_object<td_api::liveLocation>(make_object<td_api::location>(l->lat, l->lon, 0), period, 0, l->radius)),
             [conn, id, chat, message, period](Obj o) {
               if (auto e = as_error(o)) {
                 say("td error: %d %s", e->code_, e->message_.c_str());
                 return reply_td_error(conn, id, *e);
               }
               if (Live *now = find_live(chat, message)) {
                 now->period = period;
                 broadcast_live();
               }
               server.reply_ok(conn, id);
             });
}

// The proximity alert is a property of our live location in the chat: Telegram tells both sides when another member who
// shares theirs comes that close.
void location_alert(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  std::int64_t chat = 0, radius = 0;
  if (!request_chat(conn, id, req, chat) || !whole_field(conn, id, req, "radius", 0, 100000, false, radius)) {
    return;
  }
  const Live *found = nullptr;
  for (const Live &l : lives) {
    if (l.chat == chat) {
      found = &l;
      break;
    }
  }
  if (!found) {
    return server.reply_error(conn, id, "not_sharing", "share your live location in this chat first");
  }
  const std::int64_t message = found->id;
  tdlib.send(make_object<td_api::editMessageLiveLocation>(
                 chat, message, nullptr,
                 make_object<td_api::liveLocation>(make_object<td_api::location>(found->lat, found->lon, 0), 0, 0,
                                                   static_cast<std::int32_t>(radius))),
             [conn, id, chat, message, radius](Obj o) {
               if (auto e = as_error(o)) {
                 say("td error: %d %s", e->code_, e->message_.c_str());
                 return reply_td_error(conn, id, *e);
               }
               if (Live *now = find_live(chat, message)) {
                 now->radius = static_cast<std::int32_t>(radius);
                 broadcast_live();
               }
               server.reply_ok(conn, id);
             });
}

// ---- the map picture of a bubble

// zoom 15, 320 x 160 at scale 2: a 640 x 320 PNG, half as tall as wide, as the UI's bubble draws it.
constexpr int kMapZoom = 15, kMapWidth = 320, kMapHeight = 160, kMapScale = 2;

// The point of a location, venue or live location message; false for any other content.
bool point_of(const td_api::message &m, double &lat, double &lon) {
  if (!m.content_) {
    return false;
  }
  const td_api::location *where = nullptr;
  switch (m.content_->get_id()) {
    case td_api::messageLocation::ID:
      where = static_cast<const td_api::messageLocation &>(*m.content_).location_.get();
      break;
    case td_api::messageVenue::ID: {
      const auto *venue = static_cast<const td_api::messageVenue &>(*m.content_).venue_.get();
      where = venue ? venue->location_.get() : nullptr;
      break;
    }
    case td_api::messageLiveLocation::ID: {
      const auto *live = static_cast<const td_api::messageLiveLocation &>(*m.content_).location_.get();
      where = live ? live->location_.get() : nullptr;
      break;
    }
    default:
      break;
  }
  if (!where) {
    return false;
  }
  lat = where->latitude_;
  lon = where->longitude_;
  return true;
}

void map_failed(std::uint64_t conn, const std::string &id, const std::string &why) {
  say("media.get: map: %s", why.c_str());
  server.reply_error(conn, id, "download_failed", why);
}

}  // namespace

std::string map_path(double lat, double lon) {
  return data_dir + "/cache/maps/" + std::to_string(std::llround(lat * 1e6)) + "_" + std::to_string(std::llround(lon * 1e6)) +
         ".png";
}

void location_map(std::uint64_t conn, const std::string &id, const td_api::message &m) {
  double lat = 0, lon = 0;
  if (!point_of(m, lat, lon)) {
    return server.reply_error(conn, id, "not_found", "the message has no point");
  }
  const std::string path = map_path(lat, lon);
  if (access(path.c_str(), F_OK) == 0) {
    messages_queue(m);  // its row may not have extra.preview yet
    return queue_reply(conn, id, path_json(path));
  }
  const std::int64_t chat = m.chat_id_, message = m.id_;
  tdlib.send(make_object<td_api::getMapThumbnailFile>(make_object<td_api::location>(lat, lon, 0), kMapZoom, kMapWidth,
                                                       kMapHeight, kMapScale, chat),
             [=](Obj o) {
               if (auto e = as_error(o)) {
                 return map_failed(conn, id, std::to_string(e->code_) + " " + e->message_);
               }
               download_path(static_cast<const td_api::file *>(o.get()), "media.get map", [=](const std::string &from) {
                 if (from.empty()) {
                   return map_failed(conn, id, "the map did not download");
                 }
                 const std::string cache = data_dir + "/cache";
                 mkdir(cache.c_str(), 0700);
                 mkdir((cache + "/maps").c_str(), 0700);
                 if (!copy_file(from, path)) {
                   return map_failed(conn, id, "the map could not be kept");
                 }
                 tdlib.send(make_object<td_api::getMessage>(chat, message), [=](Obj o2) {
                   if (!as_error(o2)) {
                     messages_queue(static_cast<const td_api::message &>(*o2));  // now with extra.preview
                   }
                   queue_reply(conn, id, path_json(path));
                 });
               });
             });
}

void location_start(int wake) {
  wake_fd = wake;
}

void location_drain() {
  std::vector<Posted> got;
  std::vector<std::function<void()>> run_now;
  {
    std::lock_guard<std::mutex> lock(mu);
    got.swap(queue);
    run_now.swap(jobs);
  }
  for (auto &job : run_now) {
    job();
  }
  for (const Posted &p : got) {
    if (p.kind == Posted::kFix) {
      Fix f;
      f.lat = p.lat;
      f.lon = p.lon;
      f.accuracy = std::min(p.accuracy, 1500.0);  // what TDLib takes
      f.heading = p.heading;
      last = f;
      have_last = true;
      last_at = Clock::now();
      std::vector<Waiter> now;
      now.swap(waiters);
      for (const Waiter &w : now) {
        server.reply_ok(w.conn, w.id, fix_json(f));
      }
      fix_arrived(f);
    } else if (p.kind == Posted::kOff) {
      answer_waiters("location_off", "location services are off, or Tengram may not use them");
    } else {
      answer_waiters("no_fix", "no position in 30 s");
    }
  }
  settle();
}

void location_update(td_api::Object &update) {
  switch (update.get_id()) {
    case td_api::updateActiveLiveLocationMessages::ID:
      set_lives(static_cast<td_api::updateActiveLiveLocationMessages &>(update).messages_);
      break;
    case td_api::updateMessageContent::ID: {
      auto &u = static_cast<td_api::updateMessageContent &>(update);
      Live *l = find_live(u.chat_id_, u.message_id_);
      if (l && u.new_content_ && u.new_content_->get_id() == td_api::messageLiveLocation::ID) {
        const auto &c = static_cast<const td_api::messageLiveLocation &>(*u.new_content_);
        if (c.location_ && (c.location_->live_period_ != l->period || c.location_->proximity_alert_radius_ != l->radius)) {
          l->period = c.location_->live_period_;
          l->radius = c.location_->proximity_alert_radius_;
          broadcast_live();
        }
      }
      break;
    }
    case td_api::updateMessageLiveLocationViewed::ID: {
      auto &u = static_cast<td_api::updateMessageLiveLocationViewed &>(update);
      if (Live *l = find_live(u.chat_id_, u.message_id_)) {
        l->due = true;  // someone is looking: the next fix goes out whatever the distance
      }
      break;
    }
    default:
      break;
  }
}

bool location_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  static const char *const ours[] = {"location.get",  "location.last",   "location.search", "map.get",        "location.send",
                                     "location.live", "location.stop",   "location.extend", "location.alert", "location.active"};
  if (std::find(std::begin(ours), std::end(ours), cmd) == std::end(ours)) {
    return false;
  }
  if (!link_ready()) {
    server.reply_error(conn, id, "unlinked", "not logged in");
  } else if (cmd == "location.get") {
    location_get(conn, id);
  } else if (cmd == "location.last") {
    location_last(conn, id);
  } else if (cmd == "location.search") {
    location_search(conn, id, req);
  } else if (cmd == "map.get") {
    map_get(conn, id, req);
  } else if (cmd == "location.send" || cmd == "location.live") {
    location_send(conn, id, req, cmd == "location.live");
  } else if (cmd == "location.stop") {
    location_stop(conn, id, req);
  } else if (cmd == "location.extend") {
    location_extend(conn, id, req);
  } else if (cmd == "location.alert") {
    location_alert(conn, id, req);
  } else {
    server.reply_ok(conn, id, items_body(false));
  }
  return true;
}

void location_reset() {
  lives.clear();
  have_last = false;
  answer_waiters("location_off", "location services are off, or Tengram may not use them");
  set_want(Want::Off);
}

void location_hello() {
  if (!lives.empty()) {
    broadcast_live();
  }
}

bool location_live() {
  return !lives.empty();
}
