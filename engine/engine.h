// tengramd's shared state, and the functions its parts call on each other. All of it belongs to the main
// thread (td.h says what the TDLib receive thread does).
#pragma once

#include "db.h"
#include "server.h"
#include "td.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// A log line on stderr, with the milliseconds since start. Never put the token, a phone number, a code or
// a password in one.
void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

extern Db db;
extern Td tdlib;
extern Server server;
extern std::string data_dir;  // absolute
extern std::string db_key;    // 32 bytes: the key of chats.db and of TDLib's database (db.cpp)
bool load_db_key(const std::string &dir, bool &fresh);
extern int api_id;
extern std::string api_hash;
extern bool done;     // the stop is complete: the main loop ends
extern bool service;  // --service: the packaged app's headless service (the Hub, the navigator, the network status)
extern int mem_mb;    // the soft RLIMIT_DATA in MiB (rounded down) at start, after the raise; 0 for none or unreadable (limits() sets it)

bool write_file(const std::string &path, const std::string &data);  // mode 0600
bool set_nonblock(int fd);

// A string field of a request. Strict: a number is not a string (td's own get_*_string_field says it is).
// Takes the value out of obj, so ask once per field.
td::Result<std::string> string_field(td::JsonObject &obj, const char *name);
// An optional field of one JSON type: 1 it is there (in v), 0 it is missing, -1 it has another type.
int optional_field(td::JsonObject &obj, const char *name, td::JsonValue::Type type, td::JsonValue &v);

// main.cpp: where TDLib's updates and the UI's requests go. A later step adds a line to each.
void handle_update(Obj update);
void handle_request(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);

// main.cpp: something changed that flush() has to write. Schedules the flush 50 ms ahead unless one is already
// scheduled; `now` makes it due at once (what the user is waiting to see). flush() runs each part's write function
// in one transaction, then each part's events.
void mark_dirty(bool now = false);

// link.cpp: the login.
std::string link_body();  // the JSON object of the `hello` result and the `link` event
void link_auth(const td_api::AuthorizationState &s);
void link_conn(const td_api::ConnectionState &s);
bool link_request(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);
bool link_ready();  // the account is logged in: the requests that need it answer `unlinked` otherwise
std::int64_t link_me_id();  // our own user id, 0 until known
void link_syncing(bool on);  // the first full chat list load is running: `syncing` in the link body (while ready)

// td.cpp: the reply for a TDLib error. Every command that hands TDLib's answer to the UI uses this.
void reply_td_error(std::uint64_t conn, const std::string &id, const td_api::error &e);
// The reply for a TDLib answer that carries nothing for the UI: ok, or the error.
void answer(std::uint64_t conn, const std::string &id, const Obj &o);
// Downloads a file now (synchronous, as media.get) and gives its absolute path to `done`: at once when it is here
// already, "" when there is no file or the download fails (logged under `what`; the caller goes on without it).
void download_path(const td_api::file *file, const std::string &what, std::function<void(const std::string &)> done);

// chats.cpp: the mirror of the chat list in `chats`.
void chats_update(td_api::Object &update);  // the chat updates; anything else is ignored
void chats_start();                         // the account is ready: load the chat list
void chats_write();                         // flush(): write the changed rows (inside its transaction)
void chats_events();                        // flush(): the `chats` events, after the commit
void chats_reset();                         // a new client after a logout, db.wipe() done: forget everything
void chats_close();                         // before db.close(): finalizes the prepared statements
// chat.set's translate: the language the chat's incoming messages are shown in ('' off), written at the next flush.
void chats_set_translate(std::int64_t chat, const std::string &lang);
std::string one_line(std::string s);  // newlines and tabs to spaces, at most 150 bytes, cut between characters
struct ChatBrief {
  int kind;  // chats.kind
  bool has_last;
  std::int64_t last_id;
  std::int64_t read_outbox;  // last_read_outbox_message_id_
  std::int64_t peer;         // the other person's user id in a private or a secret chat, else 0
  bool forum;                // a supergroup with topics (chats.forum)
  int secret;                // chats.secret: 0 not a secret chat, 1 pending, 2 ready, 3 closed
};
bool chat_brief(std::int64_t chat_id, ChatBrief &out);  // false when the chat is not mirrored
void chats_last_message(const td_api::message &m);      // m is the chat's newest message now: refresh its preview
void chats_rewrite(std::int64_t chat, bool emptied);    // the row again; emptied: chat.clear took its history
std::string message_preview(const td_api::message &m);  // the chat list's one line for a message, sender prefix and all
// muted_until of a topic's own notification settings: 0 when they follow the chat's (or there is no mute).
std::int64_t topic_muted_until(const td_api::chatNotificationSettings &n);
// Marks the chat read up to its newest message, or up to `upto` (a message id; 0 is the newest, and one past the
// newest is the newest), as chat.read does. False when the chat is not mirrored. With nothing to read done gets
// nullptr at once; else TDLib's answer: its error, or nullptr.
bool chats_read(std::int64_t chat, std::function<void(const td_api::error *)> done, std::int64_t upto = 0);
const std::string &user_name(std::int64_t user_id, bool full);  // first name, or full; "" when unknown
const std::string &chat_title(std::int64_t chat_id);            // "" when the chat is not mirrored
// The path of a user's small profile photo; "" when they have none or it is not downloaded yet.
const std::string &user_photo(std::int64_t user_id);
// Asks TDLib, in the background, for the small profile photo of a user who has one that is not here yet. The `photo`
// event (jid: the user id, photo: the path) comes to every connection when it is. False when there is nothing to ask:
// no photo, it is here, or it was asked for already (its event is still to come).
bool user_photo_ask(std::int64_t user_id);
// Telegram's built-in accent colour of a user, 0 red, 1 orange, 2 violet, 3 green, 4 cyan, 5 blue, 6 pink: what colours
// their name in a group. False when the user is not known (no updateUser came for them).
bool user_color(std::int64_t user_id, int &color);

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
// The kind of the message's row in the table, or -1 when there is none.
int message_kind(std::int64_t chat, std::int64_t id);
// The formatted text a message shows (a text's, or a media's caption), or nullptr when its kind has none.
const td_api::formattedText *shown_text(const td_api::MessageContent &c);
// The file of a message that has one (a photo's largest size, a video, a voice note...), or nullptr.
const td_api::file *message_file(const td_api::message &m);
// The file a bubble shows before the message's own is opened: a photo's display size (the smallest size whose shorter
// side is at least 400 px, else the largest), the JPEG thumbnail of a video, animation or video note. Or nullptr.
const td_api::file *message_preview_file(const td_api::message &m);
// A message TDLib just gave us (the answer to msg.send): its row is written at the next flush.
// local: the UI's own name for a message it is sending (msg.send's `local`), written on the row as extra.local, so the UI
// renames its local row to this one exactly rather than by its words.
void messages_queue(const td_api::message &m, const std::string &local = std::string());
// A page of history, newest first, that must become the table's content for the chat from `oldest` up to but
// not including `upper`: rows are added or changed, and rows in that range that are not in the page are deleted.
// `topic` is the forum topic (or the comment thread's root id, in the topic column) the page is of: only its rows are
// compared. -1 is the whole chat.
void messages_reconcile(std::int64_t chat, std::int64_t topic,
                        const std::vector<td_api::object_ptr<td_api::message>> &page, std::int64_t oldest,
                        std::int64_t upper);
// A message's translation (translate.cpp): written when the row still holds `source` as its text, and the chat's own
// language, if it has one, is `lang`. The `msgs` event goes out only when a row changed.
void messages_translated(std::int64_t chat, std::int64_t id, const std::string &source, const std::string &tr,
                         const std::string &lang);
// A reply to send after the next flush has committed. Marks dirty.
void queue_reply(std::uint64_t conn, const std::string &id, const std::string &result_json);

// requests.cpp: chat.open, chat.history, chat.newer, chat.pins, chat.close, chat.read, chat.typing, chat.set, msg.send,
// media.send, msg.delete, msg.edit, msg.forward, msg.react, msg.reactions, poll.vote, media.get, media.cancel.
bool requests_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);
// A file TDLib updated: when a media.get is downloading it, or a message of ours is uploading it, the `media.progress`
// event (at most every 200 ms; an upload's last one, with done = size, is never held back).
void media_progress(const td_api::file &f);
// A message of ours TDLib is sending (updateNewMessage): its file's upload is reported by media.progress, with `up`.
void upload_watch(const td_api::message &m);
// The message with this (temporary) id is sent or has failed: its file is no longer reported.
void upload_forget(std::int64_t chat, std::int64_t message);
// The common start of a request about a chat: the account is logged in and "chat" is a string holding an int64
// (`unlinked` or `bad_request` otherwise). False after the reply.
bool request_chat(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t &chat);
// A whole decimal int64 as the keys travel ("-1001234567890"); false for anything else, or one that does not fit.
bool parse_i64(const std::string &s, std::int64_t &out);
// A path a request names for a file to send: absolute and an existing regular file (stat). Its size goes in `size`.
bool regular_file(const std::string &path, std::int64_t &size);
// The helpers of a request that sends a message or names one (requests.cpp, voice.cpp):
void bad_request(std::uint64_t conn, const std::string &id, const std::string &what);
// A message id in the string field `name`. False after the bad_request reply.
bool message_id(std::uint64_t conn, const std::string &id, td::JsonObject &req, const char *name, std::int64_t &out);
// The optional field "quote", a message id in a string: `reply` becomes the message it replies to, or stays null. False
// after the bad_request reply.
bool quote_field(std::uint64_t conn, const std::string &id, td::JsonObject &req,
                 td_api::object_ptr<td_api::InputMessageReplyTo> &reply);
// The optional field "topic", a forum topic's id (a whole number from 1; General is 1). `topic` is -1 when the field is
// missing. False after the bad_request reply.
bool topic_field(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int32_t &topic);
// The optional field "thread", the id of a comment thread's root in the discussion group (a message id in a string).
// `thread` is 0 when the field is missing. False after the bad_request reply.
bool thread_field(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t &thread);
// Both fields of a request, topic_field and thread_field; a request may not carry both (bad_request, false).
bool topic_thread_fields(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int32_t &topic,
                         std::int64_t &thread);
// The topic as TDLib takes it, or nullptr for no topic (-1): the comment thread when `thread` is given.
td_api::object_ptr<td_api::MessageTopic> topic_object(std::int32_t topic, std::int64_t thread = 0);
// Sends the content as a new message of the chat, in the forum topic `topic` unless it is -1, or in the comment thread
// `thread` unless it is 0: its row is queued (state 0, under a temporary id), the reply is `{id}`.
void send_content(std::uint64_t conn, const std::string &id, std::int64_t chat, std::int32_t topic, std::int64_t thread,
                  td_api::object_ptr<td_api::InputMessageReplyTo> reply,
                  td_api::object_ptr<td_api::InputMessageContent> content,
                  const std::string &local = std::string());
std::string path_json(const std::string &path);  // {"path": ...}, the result of media.get and voice.play

// voice.cpp: voice.start, voice.send, voice.cancel and voice.play. The codec is opus_ogg.h's.
bool voice_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);
bool voice_deadline(std::chrono::steady_clock::time_point &due);  // while recording: when "recording" goes out again
void voice_tick();                                                // that time has come

// translate.cpp: msg.translate, a message's text through Telegram's own translation (docs/protocol.md, "Translation"). Up
// to four requests to TDLib at once for one msg.translate, and a pause after Telegram refuses for too many.
bool translate_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);
void translate_start(int wake_fd);  // keeps the wake fd for the threads of the Google requests
void translate_drain();             // runs what those threads posted (main thread)
bool language_code(const std::string &s);  // 2 to 16 bytes of letters and '-': what msg.translate's `to` and chat.set's translate hold

// topics.cpp: the `topics` table, topics.list, and what keeps the rows current. Only a chat the UI has asked for
// (topics.list, or a chat.open, chat.newer or chat.read with a topic) is kept: no fetch for the forums it never saw.
bool topics_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);
void topics_update(td_api::Object &update);  // updateForumTopicInfo and updateForumTopic; anything else is ignored
void topics_write();                         // flush(): the queued rows (inside its transaction)
void topics_events();                        // flush(): the `topics` events, after the commit
void topics_reset();                         // a new client after a logout: drop what is queued
void topics_close();                         // before db.close(): finalizes the prepared statements
// A message was added to, deleted from or edited in the topic (0 or less: not in one): its row is fetched again.
void topics_note(std::int64_t chat, std::int32_t topic);
void topics_watch(std::int64_t chat);  // the chat's topics are kept from now on
// Fetches the topic from Telegram, writes its row, and gives `done` the id of the topic's newest message (0 when it has
// none) or the error. At most one fetch of a topic runs at a time; one asked for meanwhile follows it.
void topics_fetch(std::int64_t chat, std::int32_t topic,
                  std::function<void(const td_api::error *, std::int64_t)> done);
// chats_read for a topic: marks it read up to its newest message, or up to `upto` (0 is the newest), then fetches its
// row again. False when the chat is not a mirrored forum. `done` as chats_read's.
bool topics_read(std::int64_t chat, std::int32_t topic, std::function<void(const td_api::error *)> done,
                 std::int64_t upto = 0);

// profile.cpp: profile.get and profile.set, our own account's name, about, phone and picture. They have no chat.
bool profile_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);

// stickers.cpp: stickers.sets, stickers.get, stickers.file, sticker.send and sticker.fave, the sticker picker's requests.
// Only sticker.send names a chat.
bool stickers_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);

// sessions.cpp: sessions.list, sessions.terminate, sessions.terminateOthers and sessions.confirmQr, the devices the
// account is signed in on and the QR code that signs a new one in. They have no chat.
bool sessions_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);

// pages.cpp: contact.info, channel.info, group.info, contacts.list, senders.get, chat.clear, chat.delete and
// channel.unfollow, the requests behind the info pages and the group chat's senders.
bool pages_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);
// pages.cpp, for the requests that name a chat: the user a private or secret chat is with, the basic group, the
// supergroup (a channel when `channel`, else a group); 0 when the chat is not that.
std::int64_t person_of(const td_api::chat &c);
std::int64_t basic_group_of(const td_api::chat &c);
std::int64_t supergroup_of(const td_api::chat &c, bool channel);
// The request that gives a chat: a private chat's id is its user's, so a positive id is one, and createPrivateChat (force:
// no network) gives the chat of a person we have none with yet (an address book contact); getChat for the rest.
td_api::object_ptr<td_api::Function> chat_lookup(std::int64_t chat);
// chat_lookup, then `next` gets the chat; a chat TDLib does not know is answered `bad_chat`.
void with_chat(std::uint64_t conn, const std::string &id, std::int64_t chat,
               std::function<void(const td_api::chat &)> next);

// manage.cpp: contact.block, group.participants, group.create, poll.create, invite.info, invite.accept, link.open,
// group.join, secret.start, chat.ttl and chat.export, what the UI does to people, groups, invite links and Telegram
// links, starting a secret chat, the chat's self-destruct or auto-delete timer, and the chat's export to a text file.
bool manage_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);

// account.cpp: privacy.get, privacy.set, password.*, autodelete.set, blocked.*, account.ttl, websites.*, payments.clear,
// contacts.clearImported, contacts.book and topchats.set, the rows of Settings → Privacy and security. They have no chat.
bool account_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);

// The preferences of data.cpp (docs/protocol.md, "Data and storage"); mode.cpp keeps and saves them with the others.
struct DataPrefs {
  std::string auto_net = "wifi";  // the network automatic download is allowed on: none, wifi (Wi-Fi only) or all
  bool auto_photos = true;
  int auto_video_mb = 15;  // videos and GIFs up to this size; 0 never
  int auto_file_mb = 3;    // documents and audio files up to this size; 0 never
  bool gallery_private = false, gallery_groups = false, gallery_channels = false;  // save photos and videos to the gallery
  int keep_mb = 2048;  // the cap on TDLib's downloaded media; 0 no limit
};

// data.cpp: storage.get, storage.clear, network.get and network.reset, and the data preferences at work: the automatic
// download of new messages' media, the gallery copy, and the pruning of TDLib's downloaded media.
bool data_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);
void data_update(td_api::Object &update);  // updateNewMessage (starts a download) and updateFile (one finished); the rest is ignored
void data_reset();                         // a new client after a logout: forget the downloads under way
void data_network_changed();               // the network or autoNet changed: stop our downloads it no longer allows
bool data_deadline(std::chrono::steady_clock::time_point &due);  // while the media kept are capped: the next pruning
void data_tick();                                                // that time has come
// Copies the photo or video of an incoming message, just downloaded to `path`, to the gallery when the chat's kind is
// switched on in the preferences. Nothing for any other message, or when the copy is there already.
void gallery_save(const td_api::message &m, const std::string &path);
// A document or audio file from a chat (the menu's Download): its copy in the phone's Downloads under its own name
// (made now, or the one made before), else "" (not a document or audio file, a secret chat's, a protected or
// self-destructing one, a failed copy).
std::string downloads_copy(const td_api::message &m, const std::string &path);

// mode.cpp: the delivery mode, ui.state and prefs.set (prefs.json).
void mode_init();                                                    // read prefs.json
bool mode_request(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);
void mode_conn_closed(std::uint64_t conn);  // an authenticated connection went away: its fullscreen with it
void mode_conn_changed();                   // the UI may have come to the front or left it
void mode_ready();                          // the link became ready
void mode_evaluate();                       // the link left ready, or anything else that changes what is wanted
void mode_reset();                          // a new client after a logout: the network is on, no sync
void mode_note_update();
void mode_note_request();  // a client's request came: the network is needed now                    // an update of any kind came
void mode_connection(bool ready);           // TDLib's connection state changed: ready, or not
void mode_update(td_api::Object &update);   // updateUserStatus of our own account
void mode_network(bool available, bool wifi);  // the system's network status: none, or Wi-Fi, or mobile data
bool mode_notify_preview();                 // the preference notifyPreview
const std::string &mode_language();         // the `language` preference, as the UI sent it ("" until it did)
const std::string &mode_google_key();       // the `googleKey` preference: "" for none. Never to a log
const DataPrefs &mode_data_prefs();         // the preferences of data.cpp
bool mode_auto_download_ok();               // the system's network is one that autoNet allows
bool mode_ui_shows(std::int64_t chat);      // a UI that is in front has this chat open
bool mode_deadline(std::chrono::steady_clock::time_point &due);  // the next sync, or the end of the running one
void mode_tick();                           // the deadline has passed

// notifier.cpp: TDLib's notification groups as Hub items (hub.h).
void notifier_init();                                  // opens the Hub, reads hub.json
bool notifier_active();                                // the Hub opened: TDLib is asked for notifications
void notifier_update(td_api::Object &update);          // the notification updates; anything else is ignored
void notifier_reset();                                 // a logout: the account goes, everything is forgotten
void notifier_action(const std::string &action, std::int64_t chat);  // "read", "unread" or "delete", from a Hub item
void notifier_call(std::int64_t chat, bool ringing, bool video);  // the incoming call item: put in the Hub, or taken out

// bb10.cpp (--service only): the navigator's and the network status's events, from a thread of its own.
void bb10_start(int wake_fd);  // the thread writes to wake_fd when it has something: main calls bb10_drain()
void bb10_drain();

// calls.cpp: call.start, call.answer, call.video, call.reject and call.hangup, calls.list (the call log), and the `call`
// event: 1:1 voice and video calls, TDLib's call states and signalling with Telegram's tgcalls library
// (docs/protocol.md, "Calls"). One call at a time.
void calls_start(int wake_fd);              // registers the tgcalls versions; keeps the wake fd for its threads
void calls_drain();                         // runs what tgcalls' threads posted (main thread)
void calls_update(td_api::Object &update);  // updateCall, updateNewCallSignalingData; anything else is ignored
bool calls_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req);
bool calls_live();                          // a call is ringing, being placed or under way: the network must stay on
bool calls_audio_wanted(const std::string &call_id);  // the audio link's hello names the live call (server.cpp asks)
void calls_audio_attach(int fd);                      // the audio link, after its ok reply: the call owns the fd now
bool calls_video_wanted(const std::string &call_id);  // the same for the video link's hello
void calls_video_attach(int fd);                      // the video link, after its ok reply: the call owns the fd now
void calls_hello();                                   // a connection said hello: it gets the live call's event, if any

// ring.cpp: what an incoming call does until it stops ringing: the phone's ringtone, and the Hub's call item. Both from
// the main thread; the tone plays on a thread of its own.
void ring_start(std::int64_t chat, bool video);  // the call from this chat rings (a tone already playing is stopped first)
void ring_stop(std::int64_t chat);   // it stopped ringing, however: the tone ends, the item goes. Safe if nothing rings

// live.cpp: typing and presence for the chats the UI has open.
void live_open(std::uint64_t conn, std::int64_t chat);   // chat.open succeeded: the chat is open for this connection
void live_close(std::uint64_t conn, std::int64_t chat);  // chat.close
void live_conn_closed(std::uint64_t conn);               // a connection went away: what it opened is closed
void live_reset();                                       // a new client after a logout
void live_update(td_api::Object &update);                // updateChatAction, updateUser, updateUserStatus
