// tengramd's shared state, and the functions its parts call on each other. All of it belongs to the main
// thread (td.h says what the TDLib receive thread does).
#pragma once

#include "db.h"
#include "server.h"
#include "td.h"

#include <cstdint>
#include <string>
#include <vector>

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

// main.cpp: something changed that flush() has to write. Schedules the flush 50 ms ahead unless one is already
// scheduled. flush() runs each part's write function in one transaction, then each part's events.
void mark_dirty();

// link.cpp: the login.
std::string link_body();  // the JSON object of the `hello` result and the `link` event
void link_auth(const td_api::AuthorizationState &s);
void link_conn(const td_api::ConnectionState &s);
bool link_request(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);
bool link_ready();  // the account is logged in: the requests that need it answer `unlinked` otherwise

// td.cpp: the reply for a TDLib error. Every command that hands TDLib's answer to the UI uses this.
void reply_td_error(std::uint64_t conn, const std::string &id, const td_api::error &e);
// The reply for a TDLib answer that carries nothing for the UI: ok, or the error.
void answer(std::uint64_t conn, const std::string &id, const Obj &o);

// chats.cpp: the mirror of the chat list in `chats`.
void chats_update(td_api::Object &update);  // the chat updates; anything else is ignored
void chats_start();                         // the account is ready: load the chat list
void chats_write();                         // flush(): write the changed rows (inside its transaction)
void chats_events();                        // flush(): the `chats` events, after the commit
void chats_reset();                         // a new client after a logout, db.wipe() done: forget everything
void chats_close();                         // before db.close(): finalizes the prepared statements
std::string one_line(std::string s);  // newlines and tabs to spaces, at most 150 bytes, cut between characters
struct ChatBrief {
  int kind;  // chats.kind
  bool has_last;
  std::int64_t last_id;
  std::int64_t read_outbox;  // last_read_outbox_message_id_
};
bool chat_brief(std::int64_t chat_id, ChatBrief &out);  // false when the chat is not mirrored
void chats_last_message(const td_api::message &m);      // m is the chat's newest message now: refresh its preview
const std::string &user_name(std::int64_t user_id, bool full);  // first name, or full; "" when unknown
const std::string &chat_title(std::int64_t chat_id);            // "" when the chat is not mirrored

// body.cpp: what a message says, as one piece of text, without a sender's name in front. is_service is set for the
// service messages ("Anna joined") and the calls: their previews take no name either.
std::string message_body(const td_api::message &m, bool &is_service);
bool call_missed(const td_api::message &m);  // m is a messageCall we did not place and nobody answered
std::string sender_name(const td_api::message &m);  // the sender's first name or the sender chat's title; "" unknown

// messages.cpp: the `messages` table. Rows are queued and written by flush(), which then calls the other two.
void messages_update(td_api::Object &update);  // the message updates; anything else is ignored
void messages_write();                         // flush(): the queued rows (inside its transaction)
void messages_events();                        // flush(): the `msgs` events, after the commit
void messages_replies();                       // flush(): the replies that wait for the commit
void messages_reset();                         // a new client after a logout: drop what is queued
void messages_close();                         // before db.close(): finalizes the prepared statements
// A message TDLib just gave us (the answer to msg.send): its row is written at the next flush.
void messages_queue(const td_api::message &m);
// A page of history, newest first, that must become the table's content for the chat from `oldest` up to but
// not including `upper`: rows are added or changed, and rows in that range that are not in the page are deleted.
void messages_reconcile(std::int64_t chat, const std::vector<td_api::object_ptr<td_api::message>> &page,
                        std::int64_t oldest, std::int64_t upper);
// A reply to send after the next flush has committed. Marks dirty.
void queue_reply(std::uint64_t conn, const std::string &id, const std::string &result_json);

// requests.cpp: chat.open, chat.history, chat.close, chat.read, chat.typing, chat.set, msg.send.
bool requests_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);
