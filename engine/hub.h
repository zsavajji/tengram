// The BlackBerry Hub as the notifier sees it: one item per chat. All of it is called from the main thread.
// Two backends stand behind these functions, chosen by the flag --service: hub_log.cpp only logs (a run from a
// shell), hub_qnx.cpp talks to the real Hub (the packaged app's headless service). hub.cpp picks.
#pragma once

#include <cstdint>
#include <string>

struct HubItem {
  std::int64_t chat;
  std::string title, body;
  int unread;  // 0 shows the item as read
  std::int64_t ts_ms;
  bool alert;  // sound, LED and the instant preview
};

bool hub_open();                                    // false: there is no Hub, and the notifier does nothing more
bool hub_new();                                     // the Hub had never seen this app: it holds nothing of ours
void hub_account(bool add);                         // the app's account in the Hub: added once, removed at logout
void hub_item(const HubItem &item, bool is_new);    // adds or updates the chat's item
void hub_remove(std::int64_t chat);                 // takes the chat's item away

// The id of a chat's item, which the Hub hands back with an action: "chat_1001", and "chat_n1001" for the chat
// -1001 (a minus sign is no good in an id). hub_chat_from finds one in a piece of text, wherever the Hub put it.
std::string hub_item_id(std::int64_t chat);
bool hub_chat_from(const std::string &text, std::int64_t &chat);

// A Hub item's action reached the engine (bb10.cpp): the three that are ours become notifier_action().
void hub_invoked(const std::string &action, const std::string &mime, const std::string &uri, const std::string &data);

// The backends.
bool hublog_open();
void hublog_account(bool add);
void hublog_item(const HubItem &item, bool is_new);
void hublog_remove(std::int64_t chat);
bool hubuds_open();
bool hubuds_new();
void hubuds_account(bool add);
void hubuds_item(const HubItem &item, bool is_new);
void hubuds_remove(std::int64_t chat);
