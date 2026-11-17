// The login: the link body the UI shows, TDLib's authorization states behind it, and the link.* commands.

#include "engine.h"

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

void send_parameters() {
  auto p = td_api::make_object<td_api::setTdlibParameters>();
  p->database_directory_ = data_dir + "/db";
  p->files_directory_ = data_dir + "/files";
  p->use_file_database_ = true;
  p->use_chat_info_database_ = true;
  p->use_message_database_ = true;
  p->use_secret_chats_ = false;
  p->api_id_ = api_id;
  p->api_hash_ = api_hash;
  p->system_language_code_ = "en";
  p->device_model_ = "BlackBerry Q10";
  p->system_version_ = "BlackBerry 10.3.3";
  p->application_version_ = "tengramd 0.1";
  tdlib.send(std::move(p), [](Obj o) {
    if (auto e = as_error(o)) {
      say("setTdlibParameters: %d %s", e->code_, e->message_.c_str());  // the state stays `linking`
    }
  });
}

void on_ready() {
  // What the probe's --no-files and --background do, always: nothing downloads by itself, and the
  // account is offline until the UI says it is in the foreground.
  tdlib.set_bool_option("disable_auto_download", true);
  tdlib.set_bool_option("online", false);
  tdlib.send(td_api::make_object<td_api::getMe>(), [](Obj o) {
    if (!tdlib_ready) {  // the state moved on while getMe was out
      return;
    }
    if (auto e = as_error(o)) {
      say("getMe: %d %s", e->code_, e->message_.c_str());
      cur.me.clear();
      cur.name.clear();
    } else {
      auto &u = static_cast<td_api::user &>(*o);
      cur.me = std::to_string(u.id_);
      cur.name = u.first_name_ + (u.last_name_.empty() ? "" : " " + u.last_name_);
    }
    cur.state = State::Ready;
    changed();
  });
}

void on_closed() {
  if (tdlib.stopping) {
    if (logging_out) {
      db.wipe();  // the logout was under way when the stop came
    }
    done = true;
    return;
  }
  // The end of link.logout (or TDLib closing by itself): the account's rows go and a new client starts.
  db.wipe();
  logging_out = false;
  cur = Link{};
  changed();
  tdlib.new_client();
}

void answer(std::uint64_t conn, const std::string &id, const Obj &o) {
  if (auto e = as_error(o)) {
    say("td error: %d %s", e->code_, e->message_.c_str());
    reply_td_error(conn, id, *e);
  } else {
    server.reply_ok(conn, id);
  }
}

// One login step. The state reads `linking` until TDLib moves on; if TDLib refuses, it goes back.
void step(std::uint64_t conn, const std::string &id, td_api::object_ptr<td_api::Function> f) {
  const State before = cur.state;
  cur.state = State::Linking;
  changed();
  tdlib.send(std::move(f), [=](Obj o) {
    if (as_error(o) && cur.state == State::Linking) {
      cur.state = before;
      changed();
    }
    answer(conn, id, o);
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
}

void link_conn(const td_api::ConnectionState &s) {
  say("conn: %s", type_name(s).c_str());
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
    if (s != State::Unlinked && s != State::Qr) {
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
    if (s != State::Unlinked && s != State::Qr && s != State::Code) {
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
