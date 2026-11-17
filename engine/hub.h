// The BlackBerry Hub as the notifier sees it: one item per chat. All of it is called from the main thread.
// hub_log.cpp only logs; step 6b adds the real one (hub_qnx.cpp) behind these same functions.
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
void hub_account(bool add);                         // the app's account in the Hub: added once, removed at logout
void hub_item(const HubItem &item, bool is_new);    // adds or updates the chat's item
void hub_remove(std::int64_t chat);                 // takes the chat's item away
