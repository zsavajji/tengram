// The real Hub backend: the Unified Data Source library (BB10's BlackBerry Hub). Ported from
// ../hyperberry/engine/hub_qnx.go, which is proven on the Q10: read its comments for the pitfalls.
//
// libunifieddatasourcec drags QtCore and QtGui into the process, so it is not linked: hubuds_open() loads it
// with dlopen, and only a run with --service gets there. Everything here runs on the main thread, as the UDS
// calls must all come from one thread.

#include "engine.h"
#include "hub.h"

#include <bb/pim/unified/unified_data_source.h>
#include <dlfcn.h>

namespace {

constexpr uds_account_key_t kAccount = 1413960269;  // "TENM"
const char *const kMime = "application/vnd.zsavajji.tengram.chat";

// Every UDS function used. The pointer types come from the header, the names from this list.
#define UDS_FUNCTIONS(X)                                                                                       \
  X(init) X(close) X(register_client) X(get_service_status) X(account_added) X(account_updated)               \
  X(account_removed) X(account_data_create) X(account_data_destroy) X(account_data_set_id)                     \
  X(account_data_set_name) X(account_data_set_description) X(account_data_set_icon)                            \
  X(account_data_set_target_name) X(account_data_set_supports_compose) X(account_data_set_type)                \
  X(item_action_data_create) X(item_action_data_destroy) X(item_action_data_set_action)                        \
  X(item_action_data_set_target) X(item_action_data_set_type) X(item_action_data_set_title)                    \
  X(item_action_data_set_image_source) X(item_action_data_set_mime_type) X(item_action_data_set_placement)     \
  X(item_action_data_set_context_mask) X(register_item_context_action) X(inbox_item_data_create)               \
  X(inbox_item_data_destroy) X(inbox_item_data_set_account_id) X(inbox_item_data_set_source_id)                \
  X(inbox_item_data_set_user_data) X(inbox_item_data_set_name) X(inbox_item_data_set_description)              \
  X(inbox_item_data_set_icon) X(inbox_item_data_set_mime_type) X(inbox_item_data_set_unread_count)             \
  X(inbox_item_data_set_total_count) X(inbox_item_data_set_timestamp) X(inbox_item_data_set_context_state)     \
  X(inbox_item_data_set_notification_state) X(item_added) X(item_updated) X(item_removed)

struct Uds {
#define X(name) decltype(&uds_##name) name = nullptr;
  UDS_FUNCTIONS(X)
#undef X
} uds;

uds_context_t context = nullptr;
bool fresh = false;

bool load() {
  void *lib = dlopen("libunifieddatasourcec.so.1", RTLD_NOW);
  if (!lib) {
    say("hub: cannot load libunifieddatasourcec: %s", dlerror());
    return false;
  }
  bool all = true;
#define X(name)                                                                       \
  uds.name = reinterpret_cast<decltype(uds.name)>(dlsym(lib, "uds_" #name));          \
  if (!uds.name) {                                                                    \
    say("hub: libunifieddatasourcec has no uds_%s", #name);                           \
    all = false;                                                                      \
  }
  UDS_FUNCTIONS(X)
#undef X
  return all;
}

// The Hub reads the app's public assets (the icons) from /apps/<pkg>/public/. The data folder is
// /accounts/1000/appdata/<pkg>/data on the phone, so <pkg> is the name of its parent.
std::string assets_path() {
  const std::string parent = data_dir.substr(0, data_dir.rfind('/'));
  return "/apps/" + parent.substr(parent.rfind('/') + 1) + "/public/";
}

// One of the item menu's actions, shown for the items whose context state has a bit of `mask`. They go to the
// app's headless target.
int register_action(const char *action, const char *title, const char *image, unsigned int mask,
                    uds_placement_type_t placement) {
  uds_item_action_data_t *a = uds.item_action_data_create();
  uds.item_action_data_set_action(a, action);
  uds.item_action_data_set_target(a, "com.zsavajji.tengram.service");
  uds.item_action_data_set_type(a, "service");
  uds.item_action_data_set_title(a, title);
  uds.item_action_data_set_image_source(a, image);
  uds.item_action_data_set_mime_type(a, kMime);
  uds.item_action_data_set_placement(a, placement);
  uds.item_action_data_set_context_mask(a, mask);
  const int rc = uds.register_item_context_action(context, kAccount, a);
  uds.item_action_data_destroy(a);
  return rc;
}

// Adds the account with its three item actions. An account that is already there is updated, and its actions are
// left as they are (the Hub refuses to change them: uds 501 on the Q10).
// ponytail: the action titles are English; HyperBerry follows the phone's language (locale_get) when the account is
// added. Do that here when Tengram is translated.
int add_account() {
  uds_account_data_t *a = uds.account_data_create();
  uds.account_data_set_id(a, kAccount);
  uds.account_data_set_name(a, "Tengram");
  uds.account_data_set_description(a, "Telegram");
  uds.account_data_set_icon(a, "hub_account2.png");  // white: the lock screen and the instant preview draw it
  uds.account_data_set_target_name(a, "com.zsavajji.tengram");
  uds.account_data_set_supports_compose(a, false);
  uds.account_data_set_type(a, UDS_ACCOUNT_TYPE_IM);
  int rc = uds.account_added(context, a);
  if (rc != 0) {
    rc = uds.account_updated(context, a);
    uds.account_data_destroy(a);
    return rc;
  }
  uds.account_data_destroy(a);
  if ((rc = register_action("com.zsavajji.tengram.MARKREAD", "Mark as read", "tick_action.png", 1,
                            UDS_PLACEMENT_DEFAULT)) != 0) {
    return rc;
  }
  if ((rc = register_action("com.zsavajji.tengram.MARKUNREAD", "Mark as unread", "unread_action.png", 2,
                            UDS_PLACEMENT_DEFAULT)) != 0) {
    return rc;
  }
  // FIXED is the placement for a delete: without this action the Hub's Delete does nothing.
  return register_action("com.zsavajji.tengram.DELETE", "Delete", "delete_action.png", 3, UDS_PLACEMENT_FIXED);
}

}  // namespace

bool hubuds_open() {
  if (!load()) {
    return false;
  }
  int rc = 0;
  if (!context && (rc = uds.init(&context, false)) != 0) {
    context = nullptr;
    say("hub: uds_init: %d", rc);
    return false;
  }
  if ((rc = uds.register_client(context, "tengram_hub", "", assets_path().c_str())) != 0) {
    uds.close(&context);
    context = nullptr;
    say("hub: uds_register_client: %d", rc);
    return false;
  }
  fresh = uds.get_service_status(context) == UDS_REGISTRATION_NEW;
  say("hub: registered, new to the Hub: %s", fresh ? "true" : "false");
  return true;
}

bool hubuds_new() {
  return fresh;
}

void hubuds_account(bool add) {
  const int rc = add ? add_account() : uds.account_removed(context, kAccount);
  say("hub: account %s: uds %d", add ? "add" : "remove", rc);
}

void hubuds_item(const HubItem &item, bool is_new) {
  // The source id rides in the user data too.
  const std::string source = hub_item_id(item.chat);
  uds_inbox_item_data_t *i = uds.inbox_item_data_create();
  uds.inbox_item_data_set_account_id(i, kAccount);
  uds.inbox_item_data_set_source_id(i, const_cast<char *>(source.c_str()));
  uds.inbox_item_data_set_user_data(i, source.c_str());
  uds.inbox_item_data_set_name(i, item.title.c_str());
  uds.inbox_item_data_set_description(i, item.body.c_str());
  // The Hub is light (a white glyph vanished, Q10) and tints nothing: yellow unread, grey read.
  uds.inbox_item_data_set_icon(i, item.unread > 0 ? "hub_unread.png" : "hub_read.png");
  uds.inbox_item_data_set_mime_type(i, kMime);
  uds.inbox_item_data_set_unread_count(i, item.unread);
  uds.inbox_item_data_set_total_count(i, 1);
  uds.inbox_item_data_set_timestamp(i, item.ts_ms);
  uds.inbox_item_data_set_context_state(i, item.unread > 0 ? 1 : 2);
  uds.inbox_item_data_set_notification_state(i, item.alert);  // true: the Hub banners and flashes the LED
  const int rc = is_new ? uds.item_added(context, i) : uds.item_updated(context, i);
  uds.inbox_item_data_destroy(i);
  say("hub: item %lld unread=%d alert=%d new=%d uds %d", static_cast<long long>(item.chat), item.unread,
      item.alert ? 1 : 0, is_new ? 1 : 0, rc);
}

void hubuds_remove(std::int64_t chat) {
  const std::string source = hub_item_id(chat);
  const int rc = uds.item_removed(context, kAccount, const_cast<char *>(source.c_str()));
  say("hub: remove %lld: uds %d", static_cast<long long>(chat), rc);
}
