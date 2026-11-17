// tengramd's shared state, and the functions its parts call on each other. All of it belongs to the main
// thread (td.h says what the TDLib receive thread does).
#pragma once

#include "db.h"
#include "server.h"
#include "td.h"

#include <cstdint>
#include <string>

// A log line on stderr, with the milliseconds since start. Never put the token, a phone number, a code or
// a password in one.
void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

extern Db db;
extern Td tdlib;
extern Server server;
extern std::string data_dir;  // absolute
extern int api_id;
extern std::string api_hash;
extern bool done;  // the stop is complete: the main loop ends

bool write_file(const std::string &path, const std::string &data);  // mode 0600
bool set_nonblock(int fd);

// A string field of a request. Strict: a number is not a string (td's own get_*_string_field says it is).
// Takes the value out of obj, so ask once per field.
td::Result<std::string> string_field(td::JsonObject &obj, const char *name);

// main.cpp: where TDLib's updates and the UI's requests go. A later step adds a line to each.
void handle_update(Obj update);
void handle_request(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);

// link.cpp: the login.
std::string link_body();  // the JSON object of the `hello` result and the `link` event
void link_auth(const td_api::AuthorizationState &s);
void link_conn(const td_api::ConnectionState &s);
bool link_request(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);

// td.cpp: the reply for a TDLib error. Every command that hands TDLib's answer to the UI uses this.
void reply_td_error(std::uint64_t conn, const std::string &id, const td_api::error &e);
