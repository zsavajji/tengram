// The login: the link body the UI shows, TDLib's authorization states behind it, and the link.* commands.

#include "engine.h"

#ifdef __QNX__
#include <bps/bps.h>
#include <bps/deviceinfo.h>
#endif

namespace {

enum class State { Linking, Unlinked, Qr, Code, Password, Ready };
const char *const kStates[] = {"linking", "unlinked", "qr", "code", "password", "ready"};

struct Link {
  State state = State::Linking;
  std::string conn = "connecting";
  std::string qr;                // Qr
  std::string phone, code_type;  // Code
  int length = 0;                // Code, 0 unknown
  std::string hint;              // Password
  std::string me, name;          // Ready
  std::int64_t me_id = 0;
  bool syncing = false;          // Ready: the first full chat list load is still running
} cur;

bool logging_out = false;  // authorizationStateLoggingOut came: the Closed after it is a logout's
bool tdlib_ready = false;  // authorizationStateReady is TDLib's current state

void fields(td::JsonObjectScope &o) {
  o("state", kStates[static_cast<int>(cur.state)]);
  o("conn", cur.conn);
  switch (cur.state) {
    case State::Qr:
      o("qr", cur.qr);
      break;
    case State::Code:
      o("phone", cur.phone);
      o("codeType", cur.code_type);
      o("length", cur.length);
      break;
    case State::Password:
      o("hint", cur.hint);
      break;
    case State::Ready:
      o("me", cur.me);
      o("name", cur.name);
      if (cur.syncing) {
        o("syncing", td::JsonTrue());
      }
      break;
    default:
      break;
  }
}

void changed() {
  server.broadcast(td::json_encode<std::string>(td::json_object([](auto &o) {
    o("ev", "link");
    fields(o);
  })));
}

// How Telegram sends the code, in the protocol's four words, and the code's length when the type has one.
const char *code_type(const td_api::AuthenticationCodeType &t, int &length) {
  switch (t.get_id()) {
    case td_api::authenticationCodeTypeTelegramMessage::ID:
      length = static_cast<const td_api::authenticationCodeTypeTelegramMessage &>(t).length_;
      return "app";
    case td_api::authenticationCodeTypeSms::ID:
      length = static_cast<const td_api::authenticationCodeTypeSms &>(t).length_;
      return "sms";
    case td_api::authenticationCodeTypeSmsWord::ID:
    case td_api::authenticationCodeTypeSmsPhrase::ID:
      return "sms";
    case td_api::authenticationCodeTypeCall::ID:
      length = static_cast<const td_api::authenticationCodeTypeCall &>(t).length_;
      return "call";
    case td_api::authenticationCodeTypeMissedCall::ID:
      length = static_cast<const td_api::authenticationCodeTypeMissedCall &>(t).length_;
      return "call";
    case td_api::authenticationCodeTypeFlashCall::ID:
      return "call";
    case td_api::authenticationCodeTypeFragment::ID:
      length = static_cast<const td_api::authenticationCodeTypeFragment &>(t).length_;
      return "other";
    case td_api::authenticationCodeTypeFirebaseAndroid::ID:
      length = static_cast<const td_api::authenticationCodeTypeFirebaseAndroid &>(t).length_;
      return "other";
    case td_api::authenticationCodeTypeFirebaseIos::ID:
      length = static_cast<const td_api::authenticationCodeTypeFirebaseIos &>(t).length_;
      return "other";
    default:
      return "other";
  }
}

// What Telegram shows in the account's Devices list, read from the phone (the user has a Q10 and a Classic):
// "BlackBerry Classic" and "BlackBerry 10.3.3.3216" on the Classic, where deviceinfo says model "Classic",
// os_version "10.3.3.3216" (and device_name "RIM BlackBerry Device", which says nothing). bps_initialize is
// counted per thread, so this pair leaves the service's BPS thread alone.
void device_strings(std::string &model, std::string &system) {
  model = "BlackBerry 10";
  system = "BlackBerry 10";
#ifdef __QNX__
  if (bps_initialize() != BPS_SUCCESS) {
    return;
  }
  deviceinfo_details_t *d = nullptr;
  if (deviceinfo_get_details(&d) == BPS_SUCCESS && d) {
    const char *name = deviceinfo_details_get_model_name(d);
    const char *version = deviceinfo_details_get_device_os_version(d);
    if (name && *name) {
      model = std::strncmp(name, "BlackBerry", 10) == 0 ? name : std::string("BlackBerry ") + name;
    }
    if (version && *version) {
      system = std::string("BlackBerry ") + version;
    }
    deviceinfo_free_details(&d);
  }
  bps_shutdown();
#endif
}

// TDLib's database goes under our key (db_key). `default_key` is the second try, for a database made before
// the engine had a key of its own: it opens under TDLib's default key and is moved to ours at once.
void send_parameters(bool default_key = false) {
  auto p = td_api::make_object<td_api::setTdlibParameters>();
  p->database_directory_ = data_dir + "/db";
  p->files_directory_ = data_dir + "/files";
  p->database_encryption_key_ = default_key ? std::string() : db_key;
  p->use_file_database_ = true;
  p->use_chat_info_database_ = true;
  p->use_message_database_ = true;
  p->use_secret_chats_ = false;
  p->api_id_ = api_id;
  p->api_hash_ = api_hash;
  p->system_language_code_ = "en";
  device_strings(p->device_model_, p->system_version_);
  p->application_version_ = "tengramd 0.1";
  tdlib.send(std::move(p), [default_key](Obj o) {
    if (auto e = as_error(o)) {
      if (e->code_ == 401 && !default_key) {
        say("TDLib's database is not under our key: trying its default key");
        return send_parameters(true);
      }
      say("setTdlibParameters: %d %s", e->code_, e->message_.c_str());  // the state stays `linking`
      return;
    }
    if (default_key) {
      tdlib.send(td_api::make_object<td_api::setDatabaseEncryptionKey>(db_key), [](Obj o2) {
        if (auto e2 = as_error(o2)) {
          say("setDatabaseEncryptionKey: %d %s", e2->code_, e2->message_.c_str());
        } else {
          say("TDLib's database is now under our key");
        }
      });
    }
  });
}

void on_ready() {
  // What the probe's --no-files and --background do, always: nothing downloads by itself, and the
  // account is offline until the UI says it is in the foreground.
  tdlib.set_bool_option("disable_auto_download", true);  // "online" is set by mode_ready, once we know who we are
  if (notifier_active()) {  // TDLib's own notification logic, for the Hub: 20 groups (chats) of one notification
    tdlib.set_int_option("notification_group_count_max", 20);
    tdlib.set_int_option("notification_group_size_max", 1);
  }
  tdlib.send(td_api::make_object<td_api::getMe>(), [](Obj o) {
    if (!tdlib_ready) {  // the state moved on while getMe was out
      return;
    }
    if (auto e = as_error(o)) {
      say("getMe: %d %s", e->code_, e->message_.c_str());
      cur.me.clear();
      cur.name.clear();
      cur.me_id = 0;
    } else {
      auto &u = static_cast<td_api::user &>(*o);
      cur.me = std::to_string(u.id_);
      cur.me_id = u.id_;
      cur.name = u.first_name_ + (u.last_name_.empty() ? "" : " " + u.last_name_);
    }
    chats_start();  // before the state is ready: if it sets `syncing`, the first link event carries it
    cur.state = State::Ready;
    changed();
    mode_ready();
  });
}

void on_closed() {
  if (tdlib.stopping) {
    if (logging_out) {
      db.wipe();  // the logout was under way when the stop came
      chats_reset();
      messages_reset();
      notifier_reset();
      live_reset();
    }
    done = true;
    return;
  }
  // The end of link.logout (or TDLib closing by itself): the account's rows go and a new client starts.
  db.wipe();
  chats_reset();
  messages_reset();
  notifier_reset();
  live_reset();
  mode_reset();
  logging_out = false;
  cur = Link{};
  changed();
  tdlib.new_client();
}

// One login step. The state reads `linking` until TDLib moves on; if TDLib refuses, it goes back.
void step(std::uint64_t conn, const std::string &id, td_api::object_ptr<td_api::Function> f) {
  const State before = cur.state;
  cur.state = State::Linking;
  changed();
  tdlib.send(std::move(f), [=](Obj o) {
    answer(conn, id, o);  // first: a page that waits for the next state sees the failure before the state goes back
    if (as_error(o) && cur.state == State::Linking) {
      cur.state = before;
      changed();
    }
  });
}

bool need_string(std::uint64_t conn, const std::string &id, td::JsonObject &req, const char *name, std::string &out) {
  auto r = string_field(req, name);
  if (r.is_error()) {
    server.reply_error(conn, id, "bad_request", std::string("need a string field ") + name);
    return false;
  }
  out = r.move_as_ok();
  return true;
}

}  // namespace

void link_syncing(bool on) {
  if (cur.syncing != on) {
    cur.syncing = on;
    if (cur.state == State::Ready) {
      changed();
    }
  }
}

std::int64_t link_me_id() {
  return cur.me_id;
}

bool link_ready() {
  return cur.state == State::Ready;
}

std::string link_body() {
  return td::json_encode<std::string>(td::json_object([](auto &o) { fields(o); }));
}

void link_auth(const td_api::AuthorizationState &s) {
  say("auth: %s", type_name(s).c_str());
  const std::int32_t id = s.get_id();
  tdlib_ready = id == td_api::authorizationStateReady::ID;
  switch (id) {
    case td_api::authorizationStateWaitTdlibParameters::ID:
      send_parameters();
      return;
    case td_api::authorizationStateWaitPhoneNumber::ID:
      cur.state = State::Unlinked;
      break;
    case td_api::authorizationStateWaitOtherDeviceConfirmation::ID:
      cur.state = State::Qr;
      cur.qr = static_cast<const td_api::authorizationStateWaitOtherDeviceConfirmation &>(s).link_;
      break;
    case td_api::authorizationStateWaitCode::ID: {
      auto &info = *static_cast<const td_api::authorizationStateWaitCode &>(s).code_info_;
      cur.state = State::Code;
      cur.phone = info.phone_number_;
      cur.length = 0;
      cur.code_type = info.type_ ? code_type(*info.type_, cur.length) : "other";
      break;
    }
    case td_api::authorizationStateWaitPassword::ID:
      cur.state = State::Password;
      cur.hint = static_cast<const td_api::authorizationStateWaitPassword &>(s).password_hint_;
      break;
    case td_api::authorizationStateReady::ID:
      on_ready();  // the state becomes `ready` when getMe answers
      return;
    case td_api::authorizationStateClosed::ID:
      on_closed();
      return;
    case td_api::authorizationStateLoggingOut::ID:
      logging_out = true;
      cur.state = State::Linking;
      break;
    case td_api::authorizationStateClosing::ID:
      cur.state = State::Linking;
      break;
    default:
      say("auth: no handling for this state, staying `linking`");
      cur.state = State::Linking;
      break;
  }
  changed();
  mode_evaluate();  // a state that is not ready: the network must be on, for a logout to get through
}

void link_conn(const td_api::ConnectionState &s) {
  say("conn: %s", type_name(s).c_str());
  mode_connection(s.get_id() == td_api::connectionStateReady::ID);
  const char *state = "connecting";
  if (s.get_id() == td_api::connectionStateWaitingForNetwork::ID) {
    state = "offline";
  } else if (s.get_id() == td_api::connectionStateReady::ID) {
    state = "ready";
  }
  if (cur.conn != state) {
    cur.conn = state;
    server.broadcast(td::json_encode<std::string>(td::json_object([](auto &o) {
      o("ev", "conn");
      o("state", cur.conn);
    })));
  }
}

bool link_request(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  const State s = cur.state;
  auto fail = [&](const char *code, const std::string &message) {
    server.reply_error(conn, id, code, message);
    return true;
  };
  auto bad_state = [&] { return fail("bad_state", std::string("the link is ") + kStates[static_cast<int>(s)]); };
  std::string text;

  if (cmd == "link.qr") {
    if (s == State::Ready) {
      return fail("linked", "already linked");
    }
    if (s != State::Unlinked && s != State::Code && s != State::Password) {
      return bad_state();
    }
    step(conn, id, td_api::make_object<td_api::requestQrCodeAuthentication>(std::vector<std::int64_t>()));
    return true;
  }
  if (cmd == "link.phone") {
    if (!need_string(conn, id, req, "phone", text)) {
      return true;
    }
    if (s == State::Ready) {
      return fail("linked", "already linked");
    }
    if (s != State::Unlinked && s != State::Code && s != State::Password) {
      return bad_state();
    }
    std::string digits;  // international format, anything that is not a digit ignored
    for (char ch : text) {
      if (ch >= '0' && ch <= '9') {
        digits += ch;
      }
    }
    if (digits.empty()) {
      return fail("bad_phone", "no digits in the phone number");
    }
    step(conn, id, td_api::make_object<td_api::setAuthenticationPhoneNumber>(digits, nullptr));
    return true;
  }
  if (cmd == "link.code") {
    if (!need_string(conn, id, req, "code", text)) {
      return true;
    }
    if (s != State::Code) {
      return bad_state();
    }
    step(conn, id, td_api::make_object<td_api::checkAuthenticationCode>(text));
    return true;
  }
  if (cmd == "link.password") {
    if (!need_string(conn, id, req, "password", text)) {
      return true;
    }
    if (s != State::Password) {
      return bad_state();
    }
    step(conn, id, td_api::make_object<td_api::checkAuthenticationPassword>(text));
    return true;
  }
  if (cmd == "link.logout") {
    if (s == State::Linking) {
      return bad_state();
    }
    // No step(): the state follows TDLib's LoggingOut and Closing. The reply is TDLib's answer.
    tdlib.send(td_api::make_object<td_api::logOut>(), [=](Obj o) { answer(conn, id, o); });
    return true;
  }
  return false;
}
