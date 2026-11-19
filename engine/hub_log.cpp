// The Hub backend that only logs. Never the title or the body: a log line has no message text.

#include "engine.h"
#include "hub.h"

bool hublog_open() {
  say("hub: log backend");
  return true;
}

void hublog_account(bool add) {
  say("hub: account %s", add ? "add" : "remove");
}

void hublog_item(const HubItem &item, bool is_new) {
  say("hub: %sitem %lld unread=%d alert=%d new=%d body=%zu bytes", item.call ? "call " : "",
      static_cast<long long>(item.chat), item.unread, item.alert ? 1 : 0, is_new ? 1 : 0, item.body.size());
}

void hublog_remove(std::int64_t chat, bool call) {
  say("hub: remove %s%lld", call ? "call " : "", static_cast<long long>(chat));
}
