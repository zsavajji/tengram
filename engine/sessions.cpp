// The requests about the devices our account is signed in on: sessions.list, sessions.terminate,
// sessions.terminateOthers and sessions.confirmQr (docs/protocol.md). They name no chat.

#include "engine.h"

#include <algorithm>

namespace {

using td_api::make_object;

void td_failed(std::uint64_t conn, const std::string &id, const td_api::error &e) {
  say("td error: %d %s", e.code_, e.message_.c_str());
  reply_td_error(conn, id, e);
}

// One session, as sessions.list and sessions.confirmQr give it.
auto session_object(const td_api::session &s) {
  return td::json_object([&s](auto &o) {
    o("id", std::to_string(s.id_));  // an int64: a string, as the other ids
    o("current", td::JsonBool(s.is_current_));
    o("app", s.application_name_);
    o("version", s.application_version_);
    o("official", td::JsonBool(s.is_official_application_));
    o("device", s.device_model_);
    o("platform", s.platform_);
    o("system", s.system_version_);
    o("ip", s.ip_address_);
    o("location", s.location_);
    o("active", s.last_active_date_);
    o("login", s.log_in_date_);
    o("unconfirmed", td::JsonBool(s.is_unconfirmed_));
    o("passwordPending", td::JsonBool(s.is_password_pending_));
  });
}

void sessions_list(std::uint64_t conn, const std::string &id) {
  tdlib.send(make_object<td_api::getActiveSessions>(), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    std::vector<const td_api::session *> rows;
    for (const auto &s : static_cast<const td_api::sessions &>(*o).sessions_) {
      if (s) {
        rows.push_back(s.get());
      }
    }
    // This device first, then the ones used last.
    std::stable_sort(rows.begin(), rows.end(), [](const td_api::session *a, const td_api::session *b) {
      return a->is_current_ != b->is_current_ ? a->is_current_ : a->last_active_date_ > b->last_active_date_;
    });
    server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&rows](auto &r) {
                      r("sessions", td::json_array(rows, [](const td_api::session *s) { return session_object(*s); }));
                    })));
  });
}

void session_terminate(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto text = string_field(req, "id");
  std::int64_t session = 0;
  if (text.is_error() || !parse_i64(text.ok(), session)) {
    return bad_request(conn, id, "need the field id, a string with a session id");
  }
  tdlib.send(make_object<td_api::terminateSession>(session), [conn, id](Obj o) { answer(conn, id, o); });
}

// The link a Telegram app shows as a QR code to sign a new device in.
void session_confirm_qr(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto link = string_field(req, "link");
  if (link.is_error() || link.ok().rfind("tg://login?token=", 0) != 0) {
    return bad_request(conn, id, "need the field link, a tg://login?token= link");
  }
  tdlib.send(make_object<td_api::confirmQrCodeAuthentication>(link.move_as_ok()), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    server.reply_ok(conn, id, td::json_encode<std::string>(session_object(static_cast<const td_api::session &>(*o))));
  });
}

}  // namespace

bool sessions_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  if (cmd != "sessions.list" && cmd != "sessions.terminate" && cmd != "sessions.terminateOthers" &&
      cmd != "sessions.confirmQr") {
    return false;
  }
  if (!link_ready()) {
    server.reply_error(conn, id, "unlinked", "not logged in");
  } else if (cmd == "sessions.list") {
    sessions_list(conn, id);
  } else if (cmd == "sessions.terminate") {
    session_terminate(conn, id, req);
  } else if (cmd == "sessions.terminateOthers") {
    tdlib.send(make_object<td_api::terminateAllOtherSessions>(), [conn, id](Obj o) { answer(conn, id, o); });
  } else {
    session_confirm_qr(conn, id, req);
  }
  return true;
}
