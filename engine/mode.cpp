// The delivery mode (docs/protocol.md): how long the connection to Telegram stays open while the UI is not
// in front. Instant keeps it open. Batched closes it with setNetworkType(None) and opens it every `batchSecs`
// for a sync: until TDLib is ready and has been quiet for 2 s (60 s at most), then closes it again. This is
// the probe's --batch cycle (probe/tdprobe.cpp). Also here: ui.state, prefs.set and prefs.json.
//
// The only timer is mode_deadline(): the next sync, or the end of the running one. With the network on and no
// sync running there is none, and the engine sleeps in poll().

#include "engine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

namespace {

using Clock = std::chrono::steady_clock;

// The test build (-DMODE_TEST_FAST) shortens these so that a test does not take minutes. The shipped build never
// defines it, and then the limits are the protocol's.
#ifdef MODE_TEST_FAST
constexpr int kBatchMin = 1;
constexpr auto kQuiet = std::chrono::milliseconds(300);
constexpr auto kSyncMax = std::chrono::seconds(3);
constexpr auto kBusy = std::chrono::milliseconds(300);  // how long after a request the engine counts as busy
#else
constexpr int kBatchMin = 60;
constexpr auto kQuiet = std::chrono::seconds(2);
constexpr auto kSyncMax = std::chrono::seconds(60);
constexpr auto kBusy = std::chrono::seconds(10);
#endif
constexpr int kBatchMax = 3600;

struct Prefs {
  std::string mode = "auto";  // auto, instant or batched
  int batch_secs = 300;
  bool notify_preview = true;  // the Hub's items say what the message says
  // HyperBerry's two, which the UI sends on every connect. The engine keeps them and does nothing with them yet:
  // previews are worded in English (language), and TDLib's file cache is not pruned by it (cacheMB; the pruning is
  // data.keepMB).
  std::string language;
  int cache_mb = 64;
  DataPrefs data;  // automatic download, the gallery copy, the media kept (data.cpp)
} prefs;

struct Ui {
  bool fullscreen = false;
  std::string open_chat;  // the chat the UI has open, "" for none
};
std::map<std::uint64_t, Ui> ui;  // by connection: the authenticated ones that sent ui.state
bool front_seen = false;         // whether the UI was in front at the last look

bool online_elsewhere = false;  // our own account is online on another device
bool network_on = true;         // what we last told TDLib: it starts with the network on
bool syncing = false;
bool conn_ready = false;  // TDLib's connection is ready now
bool sync_ready = false;  // it was ready since the sync started
std::size_t sync_updates = 0;
Clock::time_point next_sync, sync_start, last_update;

bool front() {
  for (const auto &entry : ui) {
    if (entry.second.fullscreen) {
      return true;
    }
  }
  return false;
}

// The engine is doing something a client asked for: a request came in the last 10 s, or TDLib still owes an
// answer (a download, a history page). The network stays on meanwhile, whoever asked (a UI that is not in
// front, a Hub action), and our own account showing as online is our own doing (sending marks it online).
bool requested = false;
Clock::time_point last_request;

bool busy() {
  return tdlib.pending() > 0 || (requested && Clock::now() - last_request < kBusy);
}

// What the mode asks for, leaving aside whether the engine is busy.
bool mode_batched() {
  if (!link_ready() || front() || calls_live() || prefs.mode == "instant") {
    return false;
  }
  return prefs.mode == "batched" || online_elsewhere;
}

bool wants_batched() {
  return mode_batched() && !busy();
}

// Why the network is on (or goes on).
const char *why_online() {
  if (!link_ready()) {
    return "not logged in";
  }
  if (calls_live()) {
    return "call";
  }
  if (front()) {
    return "ui";
  }
  return prefs.mode == "instant" ? "instant" : "not online elsewhere";
}

// What TDLib is told about the network: none when the mode wants it off or the system has none, else the type.
enum class Net { None, WiFi, Mobile };
Net sent = Net::WiFi;  // what TDLib was last told. A new client starts with the network on: as good as Wi-Fi
bool net_available = true, net_wifi = true;  // the system's word (--service); a plain run keeps these forever

Net effective() {
  if (!network_on || !net_available) {
    return Net::None;
  }
  return net_wifi ? Net::WiFi : Net::Mobile;
}

void apply_network() {
  const Net now = effective();
  if (now == sent) {
    return;
  }
  sent = now;
  td_api::object_ptr<td_api::NetworkType> type;
  if (now == Net::None) {
    type = td_api::make_object<td_api::networkTypeNone>();
  } else if (now == Net::WiFi) {
    type = td_api::make_object<td_api::networkTypeWiFi>();  // the probe's choice
  } else {
    type = td_api::make_object<td_api::networkTypeMobile>();
  }
  tdlib.send(td_api::make_object<td_api::setNetworkType>(std::move(type)));
}

void set_network(bool on) {
  network_on = on;
  apply_network();
}

long long ms_since(Clock::time_point t) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

void end_sync() {
  syncing = false;
  say("mode: sync done in %lld ms, %zu updates%s", ms_since(sync_start), sync_updates,
      sync_ready ? "" : " (never ready)");
}

// Brings the network in line with what the mode wants now.
void evaluate() {
  const bool want = wants_batched();
  if (want && network_on && !syncing) {
    set_network(false);
    next_sync = Clock::now() + std::chrono::seconds(prefs.batch_secs);
    say("mode: offline, next sync in %d s", prefs.batch_secs);
  } else if (!want && !network_on) {
    set_network(true);
    say("mode: online (%s)", why_online());
  } else if (!want && syncing) {
    end_sync();  // the network stays on
  }
}

void begin_sync() {
  set_network(true);
  syncing = true;
  sync_ready = false;
  sync_ready = conn_ready;  // a sync that starts on a connection that is already up has no Ready coming
  sync_updates = 0;
  sync_start = last_update = Clock::now();
  say("mode: sync");
}

// When the running sync ends: ready and quiet for kQuiet, never later than kSyncMax after its start.
Clock::time_point sync_end_at() {
  const auto cap = sync_start + kSyncMax;
  return sync_ready ? std::min(last_update + kQuiet, cap) : cap;
}

// ---- prefs.json

std::string prefs_path() {
  return data_dir + "/prefs.json";
}

void save_prefs() {
  const std::string json = td::json_encode<std::string>(td::json_object([](auto &o) {
    o("mode", prefs.mode);
    o("batchSecs", prefs.batch_secs);
    o("notifyPreview", td::JsonBool(prefs.notify_preview));
    o("language", prefs.language);
    o("cacheMB", prefs.cache_mb);
    o("autoNet", prefs.data.auto_net);
    o("autoPhotos", td::JsonBool(prefs.data.auto_photos));
    o("autoVideoMB", prefs.data.auto_video_mb);
    o("autoFileMB", prefs.data.auto_file_mb);
    o("galleryPrivate", td::JsonBool(prefs.data.gallery_private));
    o("galleryGroups", td::JsonBool(prefs.data.gallery_groups));
    o("galleryChannels", td::JsonBool(prefs.data.gallery_channels));
    o("keepMB", prefs.data.keep_mb);
  }));
  const std::string tmp = prefs_path() + ".tmp";
  if (!write_file(tmp, json) || rename(tmp.c_str(), prefs_path().c_str()) < 0) {
    say("cannot write %s", prefs_path().c_str());
  }
}

bool valid_mode(const std::string &m) {
  return m == "auto" || m == "instant" || m == "batched";
}

// A whole number from lo to hi in a JSON number.
bool whole_in(const td::JsonValue &v, int lo, int hi, int &out) {
  char *end = nullptr;
  const std::string text = v.get_number().str();
  const long n = std::strtol(text.c_str(), &end, 10);
  out = static_cast<int>(n);
  return !*end && n >= lo && n <= hi;
}

bool valid_language(const td::JsonValue &v, std::string &out) {
  out = v.get_string().str();
  return out.size() <= 16;
}

// The data preferences in `obj`, merged into `d`: how many there were, or -1 when one has the wrong type or a value
// out of range (`d` is then half changed: the caller drops it). Takes the fields out of obj.
int data_prefs_read(td::JsonObject &obj, DataPrefs &d) {
  int given = 0;
  auto add = [&](int has) { given = has < 0 || given < 0 ? -1 : given + has; };
  auto flag = [&](const char *name, bool &out) {
    td::JsonValue v;
    const int has = optional_field(obj, name, td::JsonValue::Type::Boolean, v);
    if (has == 1) {
      out = v.get_boolean();
    }
    add(has);
  };
  // 0, or a whole number from lo to hi.
  auto number = [&](const char *name, int lo, int hi, int &out) {
    td::JsonValue v;
    int has = optional_field(obj, name, td::JsonValue::Type::Number, v);
    int n = 0;
    if (has == 1) {
      has = whole_in(v, 0, hi, n) && (n == 0 || n >= lo) ? 1 : -1;
      out = has == 1 ? n : out;
    }
    add(has);
  };
  td::JsonValue net;
  int has_net = optional_field(obj, "autoNet", td::JsonValue::Type::String, net);
  if (has_net == 1) {
    const std::string n = net.get_string().str();
    if (n == "none" || n == "wifi" || n == "all") {
      d.auto_net = n;
    } else {
      has_net = -1;
    }
  }
  add(has_net);
  flag("autoPhotos", d.auto_photos);
  number("autoVideoMB", 0, 2000, d.auto_video_mb);
  number("autoFileMB", 0, 2000, d.auto_file_mb);
  flag("galleryPrivate", d.gallery_private);
  flag("galleryGroups", d.gallery_groups);
  flag("galleryChannels", d.gallery_channels);
  number("keepMB", 256, 20000, d.keep_mb);
  return given;
}

void ui_state(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  td::JsonValue fullscreen, open_chat;
  const int has_open = optional_field(req, "openChat", td::JsonValue::Type::String, open_chat);
  if (optional_field(req, "fullscreen", td::JsonValue::Type::Boolean, fullscreen) != 1 || has_open < 0) {
    server.reply_error(conn, id, "bad_request", "need fullscreen, true or false, and openChat, a string if given");
    return;
  }
  Ui &u = ui[conn];
  u.fullscreen = fullscreen.get_boolean();
  if (has_open == 1) {
    u.open_chat = open_chat.get_string().str();
  }
  server.reply_ok(conn, id);
  mode_conn_changed();
}

void prefs_set(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  td::JsonValue mode, secs, preview, language, cache;
  const int has_mode = optional_field(req, "mode", td::JsonValue::Type::String, mode);
  const int has_secs = optional_field(req, "batchSecs", td::JsonValue::Type::Number, secs);
  const int has_preview = optional_field(req, "notifyPreview", td::JsonValue::Type::Boolean, preview);
  const int has_language = optional_field(req, "language", td::JsonValue::Type::String, language);
  const int has_cache = optional_field(req, "cacheMB", td::JsonValue::Type::Number, cache);
  DataPrefs data = prefs.data;
  const int has_data = data_prefs_read(req, data);
  int batch = 0, cache_mb = 0;
  std::string lang;
  if (has_mode < 0 || has_secs < 0 || has_preview < 0 || has_language < 0 || has_cache < 0 || has_data < 0 ||
      has_mode + has_secs + has_preview + has_language + has_cache + has_data == 0 ||
      (has_mode == 1 && !valid_mode(mode.get_string().str())) ||
      (has_secs == 1 && !whole_in(secs, kBatchMin, kBatchMax, batch)) ||
      (has_language == 1 && !valid_language(language, lang)) ||
      (has_cache == 1 && !whole_in(cache, 64, 512, cache_mb))) {
    server.reply_error(conn, id, "bad_request",
                       "mode is auto, instant or batched, batchSecs a whole number from " + std::to_string(kBatchMin) +
                           " to " + std::to_string(kBatchMax) + ", notifyPreview, autoPhotos, galleryPrivate, " +
                           "galleryGroups and galleryChannels true or false, language a string of at most 16 bytes, " +
                           "cacheMB a whole number from 64 to 512, autoNet none, wifi or all, autoVideoMB and " +
                           "autoFileMB whole numbers from 0 to 2000, keepMB 0 or a whole number from 256 to 20000; " +
                           "at least one");
    return;
  }
  if (has_mode == 1) {
    prefs.mode = mode.get_string().str();
  }
  if (has_secs == 1) {
    prefs.batch_secs = batch;
    if (!network_on && !syncing) {  // a shorter wait counts from now
      next_sync = std::min(next_sync, Clock::now() + std::chrono::seconds(batch));
    }
  }
  if (has_preview == 1) {
    prefs.notify_preview = preview.get_boolean();
  }
  if (has_language == 1) {
    prefs.language = lang;
  }
  if (has_cache == 1) {
    prefs.cache_mb = cache_mb;
  }
  prefs.data = data;
  data_network_changed();
  save_prefs();
  server.reply_ok(conn, id);
  say("prefs: mode %s, every %d s, preview %s, auto %s, keep %d MB", prefs.mode.c_str(), prefs.batch_secs,
      prefs.notify_preview ? "on" : "off", prefs.data.auto_net.c_str(), prefs.data.keep_mb);
  evaluate();
}

}  // namespace

void mode_init() {
  std::ifstream in(prefs_path());
  std::stringstream text;
  text << in.rdbuf();
  std::string json = text.str();
  auto parsed = td::json_decode(td::MutableSlice(json));
  if (parsed.is_error() || parsed.ok_ref().type() != td::JsonValue::Type::Object) {
    return;  // missing or unreadable: the defaults
  }
  td::JsonValue mode, secs, preview, language, cache;
  auto &obj = parsed.ok_ref().get_object();
  int batch = 0, cache_mb = 0;
  std::string lang;
  if (optional_field(obj, "mode", td::JsonValue::Type::String, mode) == 1 && valid_mode(mode.get_string().str())) {
    prefs.mode = mode.get_string().str();
  }
  if (optional_field(obj, "batchSecs", td::JsonValue::Type::Number, secs) == 1 &&
      whole_in(secs, kBatchMin, kBatchMax, batch)) {
    prefs.batch_secs = batch;
  }
  if (optional_field(obj, "notifyPreview", td::JsonValue::Type::Boolean, preview) == 1) {
    prefs.notify_preview = preview.get_boolean();
  }
  if (optional_field(obj, "language", td::JsonValue::Type::String, language) == 1 && valid_language(language, lang)) {
    prefs.language = lang;
  }
  if (optional_field(obj, "cacheMB", td::JsonValue::Type::Number, cache) == 1 && whole_in(cache, 64, 512, cache_mb)) {
    prefs.cache_mb = cache_mb;
  }
  DataPrefs data = prefs.data;  // a file with a wrong value keeps the defaults; mediaOnMobile of an old file is ignored
  if (data_prefs_read(obj, data) >= 0) {
    prefs.data = data;
  }
  say("prefs: mode %s, every %d s, preview %s, auto %s, keep %d MB", prefs.mode.c_str(), prefs.batch_secs,
      prefs.notify_preview ? "on" : "off", prefs.data.auto_net.c_str(), prefs.data.keep_mb);
}

bool mode_request(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  if (cmd == "ui.state") {
    ui_state(conn, id, req);
  } else if (cmd == "prefs.set") {
    prefs_set(conn, id, req);
  } else {
    return false;
  }
  return true;
}

bool mode_notify_preview() {
  return prefs.notify_preview;
}

const DataPrefs &mode_data_prefs() {
  return prefs.data;
}

bool mode_auto_download_ok() {
  const std::string &net = prefs.data.auto_net;
  return net_available && (net == "all" || (net == "wifi" && net_wifi));
}

bool mode_ui_shows(std::int64_t chat) {
  for (const auto &entry : ui) {
    if (entry.second.fullscreen && entry.second.open_chat == std::to_string(chat)) {
      return true;
    }
  }
  return false;
}

void mode_conn_closed(std::uint64_t conn) {
  if (ui.erase(conn)) {
    mode_conn_changed();
  }
}

// The UI may have come to the front or left it: TDLib's online option follows, and the mode is looked at again.
void mode_conn_changed() {
  const bool now_front = front();
  if (now_front != front_seen) {
    front_seen = now_front;
    say("ui: %s", now_front ? "in front" : "not in front");
    if (now_front) {
      online_elsewhere = false;  // our own status is our doing while the UI is in front
      evaluate();                // the network first: TDLib then connects at once (see NOTES.md)
    }
    if (link_ready()) {
      tdlib.set_bool_option("online", now_front);
    }
  }
  evaluate();
}

void mode_network(bool available, bool wifi) {
  if (available == net_available && wifi == net_wifi) {
    return;
  }
  net_available = available;
  net_wifi = wifi;
  say("network: %s", !available ? "none" : wifi ? "wifi" : "mobile");
  data_network_changed();
  if (link_ready()) {  // before that, mode_ready() tells TDLib
    apply_network();   // sends nothing while the mode keeps the network off: TDLib was told none then
  }
}

void mode_ready() {
  apply_network();  // the system's word may have come before TDLib was ready for it
  tdlib.set_bool_option("online", front());  // what the UI says, from the start
  if (mode_batched() && network_on && !syncing) {  // busy or not: the chat list load is usually still out
    // The engine has just started: what came in while it was away, and the chat list, are fetched now, not
    // after batchSecs. The network goes off when this first sync ends.
    begin_sync();
  } else {
    evaluate();
  }
}

void mode_evaluate() {
  evaluate();
}

void mode_reset() {
  network_on = true;  // a new client starts with it on
  sent = Net::WiFi;
  conn_ready = false;
  syncing = false;
  online_elsewhere = false;
}

void mode_note_update() {
  last_update = Clock::now();
  if (syncing) {
    ++sync_updates;
  }
}

void mode_connection(bool ready) {
  conn_ready = ready;
  if (ready && syncing) {
    sync_ready = true;
  }
}

void mode_update(td_api::Object &update) {
  if (update.get_id() != td_api::updateUserStatus::ID) {
    return;
  }
  auto &u = static_cast<td_api::updateUserStatus &>(update);
  if (!u.status_ || link_me_id() == 0 || u.user_id_ != link_me_id()) {
    return;
  }
  bool online = false;
  long expires_in = 0;
  if (u.status_->get_id() == td_api::userStatusOnline::ID) {
    expires_in = static_cast<long>(static_cast<td_api::userStatusOnline &>(*u.status_).expires_) -
                 static_cast<long>(std::time(nullptr));
    online = expires_in > 0;
  }
  char what[64] = "offline";
  if (u.status_->get_id() == td_api::userStatusOnline::ID) {
    std::snprintf(what, sizeof what, "online, expires in %ld s", expires_in);
  }
  const bool ours = front() || busy();
  say("own status: %s%s", what, front() ? " (ignored: the UI is in front)" : ours ? " (ignored: our own doing)" : "");
  if (!front() && ours && tdlib.pending() > 0) {
    say("mode: busy with %zu TDLib requests of types %s", tdlib.pending(), tdlib.pending_types().c_str());
  }
  if (!ours && online != online_elsewhere) {
    online_elsewhere = online;
    evaluate();
  }
}

bool mode_deadline(std::chrono::steady_clock::time_point &due) {
  if (syncing) {
    due = sync_end_at();
    return true;
  }
  if (!network_on) {
    due = next_sync;
    return true;
  }
  if (mode_batched()) {  // the network is on only because the engine is busy: look again when it may be done
    const auto now = Clock::now();
    if (!busy()) {
      due = now;  // done: mode_tick takes the network off
    } else {
      due = requested && now - last_request < kBusy ? last_request + kBusy : now + kBusy;
    }
    return true;
  }
  return false;
}

void mode_note_request() {
  requested = true;
  last_request = Clock::now();
  if (!network_on || syncing) {
    evaluate();  // a request needs the network now, not at the next sync
  }
}

void mode_tick() {
  if (syncing) {
    if (Clock::now() >= sync_end_at()) {
      end_sync();
      evaluate();  // still batched: the network goes off, the next sync is batchSecs away
    }
  } else if (!network_on && Clock::now() >= next_sync) {
    begin_sync();
  } else if (network_on) {
    evaluate();  // the busy time is over, or not yet: mode_deadline says when to look again
  }
}
