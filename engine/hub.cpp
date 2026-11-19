// What is common to the Hub backends: which one is used, the ids of the items, and the actions that come back.

#include "engine.h"
#include "hub.h"

bool hub_open() {
  return service ? hubuds_open() : hublog_open();
}

bool hub_new() {
  return service && hubuds_new();
}

void hub_account(bool add) {
  service ? hubuds_account(add) : hublog_account(add);
}

void hub_item(const HubItem &item, bool is_new) {
  service ? hubuds_item(item, is_new) : hublog_item(item, is_new);
}

void hub_remove(std::int64_t chat, bool call) {
  service ? hubuds_remove(chat, call) : hublog_remove(chat, call);
}

std::string hub_item_id(std::int64_t chat, bool call) {
  std::string id = call ? "call_chat_" : "chat_";
  if (chat < 0) {
    id += 'n';
    id += std::to_string(chat).substr(1);
  } else {
    id += std::to_string(chat);
  }
  return id;
}

bool hub_chat_from(const std::string &text, std::int64_t &chat) {
  for (std::size_t at = text.find("chat_"); at != std::string::npos; at = text.find("chat_", at + 1)) {
    std::size_t i = at + 5;
    const bool negative = i < text.size() && text[i] == 'n';
    i += negative ? 1 : 0;
    std::size_t end = i;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
      ++end;
    }
    if (end == i || end - i > 18) {  // no digits, or more than an int64 holds of them
      continue;
    }
    chat = std::stoll(text.substr(i, end - i)) * (negative ? -1 : 1);
    return true;
  }
  return false;
}

void hub_invoked(const std::string &action, const std::string &mime, const std::string &uri, const std::string &data) {
  // Logged whole: where the Hub puts the item's id is for the phone to show. Control characters are blanked.
  auto plain = [](std::string s) {
    for (char &ch : s) {
      if (static_cast<unsigned char>(ch) < 32) {
        ch = ' ';
      }
    }
    return s;
  };
  say("invoke: %s mime %s uri %s data %s", plain(action).c_str(), plain(mime).c_str(), plain(uri).c_str(),
      plain(data).c_str());
  const char *what = nullptr;
  if (action == "com.zsavajji.tengram.MARKREAD") {
    what = "read";
  } else if (action == "com.zsavajji.tengram.MARKUNREAD") {
    what = "unread";
  } else if (action == "com.zsavajji.tengram.DELETE") {
    what = "delete";
  }
  if (!what) {  // START, TIMER_FIRED and the like need nothing
    return;
  }
  std::int64_t chat = 0;
  if (!hub_chat_from(uri + " " + data, chat)) {
    say("invoke: no chat in it");
    return;
  }
  notifier_action(what, chat);
}
