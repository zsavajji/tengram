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
#else
constexpr int kBatchMin = 60;
constexpr auto kQuiet = std::chrono::seconds(2);
constexpr auto kSyncMax = std::chrono::seconds(60);
#endif
constexpr int kBatchMax = 3600;

struct Prefs {
  std::string mode = "auto";  // auto, instant or batched
  int batch_secs = 300;
  bool notify_preview = true;  // the Hub's items say what the message says
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
constexpr auto kBusy = std::chrono::seconds(10);
bool requested = false;
Clock::time_point last_request;

bool busy() {
  return tdlib.pending() > 0 || (requested && Clock::now() - last_request < kBusy);
}

// What the mode asks for, leaving aside whether the engine is busy.
bool mode_batched() {
  if (!link_ready() || front() || prefs.mode == "instant") {
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
  }));
  const std::string tmp = prefs_path() + ".tmp";
  if (!write_file(tmp, json) || rename(tmp.c_str(), prefs_path().c_str()) < 0) {
    say("cannot write %s", prefs_path().c_str());
  }
}

bool valid_mode(const std::string &m) {
  return m == "auto" || m == "instant" || m == "batched";
}

// A whole number of seconds from kBatchMin to kBatchMax in a JSON number.
bool valid_secs(const td::JsonValue &v, int &out) {
  char *end = nullptr;
  const std::string text = v.get_number().str();
  const long n = std::strtol(text.c_str(), &end, 10);
  out = static_cast<int>(n);
  return !*end && n >= kBatchMin && n <= kBatchMax;
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
  td::JsonValue mode, secs, preview;
  const int has_mode = optional_field(req, "mode", td::JsonValue::Type::String, mode);
  const int has_secs = optional_field(req, "batchSecs", td::JsonValue::Type::Number, secs);
  const int has_preview = optional_field(req, "notifyPreview", td::JsonValue::Type::Boolean, preview);
  int batch = 0;
  if (has_mode < 0 || has_secs < 0 || has_preview < 0 || has_mode + has_secs + has_preview == 0 ||
      (has_mode == 1 && !valid_mode(mode.get_string().str())) || (has_secs == 1 && !valid_secs(secs, batch))) {
    server.reply_error(conn, id, "bad_request",
                       "mode is auto, instant or batched, batchSecs a whole number from " + std::to_string(kBatchMin) +
                           " to " + std::to_string(kBatchMax) + ", notifyPreview true or false; at least one");
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
  save_prefs();
  server.reply_ok(conn, id);
  say("prefs: mode %s, every %d s, preview %s", prefs.mode.c_str(), prefs.batch_secs,
      prefs.notify_preview ? "on" : "off");
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
  td::JsonValue mode, secs, preview;
  auto &obj = parsed.ok_ref().get_object();
  int batch = 0;
  if (optional_field(obj, "mode", td::JsonValue::Type::String, mode) == 1 && valid_mode(mode.get_string().str())) {
    prefs.mode = mode.get_string().str();
  }
  if (optional_field(obj, "batchSecs", td::JsonValue::Type::Number, secs) == 1 && valid_secs(secs, batch)) {
    prefs.batch_secs = batch;
  }
  if (optional_field(obj, "notifyPreview", td::JsonValue::Type::Boolean, preview) == 1) {
    prefs.notify_preview = preview.get_boolean();
  }
  say("prefs: mode %s, every %d s, preview %s", prefs.mode.c_str(), prefs.batch_secs,
      prefs.notify_preview ? "on" : "off");
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
  if (link_ready()) {  // before that, mode_ready() tells TDLib
    apply_network();   // sends nothing while the mode keeps the network off: TDLib was told none then
  }
}

void mode_ready() {
  apply_network();  // the system's word may have come before TDLib was ready for it
  tdlib.set_bool_option("online", front());  // what the UI says, from the start
  if (wants_batched() && network_on && !syncing) {
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
  if (mode_batched() && busy()) {  // batched is only waiting for the engine to be done: look again then
    const auto now = Clock::now();
    due = requested && now - last_request < kBusy ? last_request + kBusy : now + kBusy;
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
