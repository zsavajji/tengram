// The requests behind Settings → Privacy and security: privacy.get and privacy.set, the two-step verification (password.*),
// the auto-delete timer, the block list, the account's time to live, the connected websites, the saved payment data, the
// imported contacts and the top chats (docs/protocol.md, "Privacy and security"). They name no chat.

#include "engine.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace {

using td_api::make_object;
using Rule = td_api::object_ptr<td_api::UserPrivacySettingRule>;

constexpr std::int32_t kBlockedPage = 50;  // the people one blocked.list gives

void td_failed(std::uint64_t conn, const std::string &id, const td_api::error &e) {
  say("td error: %d %s", e.code_, e.message_.c_str());
  reply_td_error(conn, id, e);
}

bool has(const td_api::error &e, const char *part) {
  return e.message_.find(part) != std::string::npos;
}

// An error of ours, worded as the td error is: TDLib's code and message.
void refuse(std::uint64_t conn, const std::string &id, const char *code, const td_api::error &e) {
  say("td error: %d %s", e.code_, e.message_.c_str());
  server.reply_error(conn, id, code, std::to_string(e.code_) + " " + e.message_);
}

// Sends the call and answers ok or its error.
void send_answer(std::uint64_t conn, const std::string &id, td_api::object_ptr<td_api::Function> call) {
  tdlib.send(std::move(call), [conn, id](Obj o) { answer(conn, id, o); });
}

// A whole-number field: 1 it is there (in out), 0 it is missing, -1 it has another type or is not whole.
int number_field(td::JsonObject &req, const char *name, long &out) {
  td::JsonValue v;
  const int given = optional_field(req, name, td::JsonValue::Type::Number, v);
  if (given != 1) {
    return given;
  }
  char *end = nullptr;
  const std::string text = v.get_number().str();
  out = std::strtol(text.c_str(), &end, 10);
  return *end ? -1 : 1;
}

// The required field `name`, a whole number that is one of `allowed`. False after the bad_request reply.
bool choice_field(std::uint64_t conn, const std::string &id, td::JsonObject &req, const char *name,
                  std::initializer_list<long> allowed, std::int32_t &out) {
  long n = 0;
  if (number_field(req, name, n) == 1 && std::find(allowed.begin(), allowed.end(), n) != allowed.end()) {
    out = static_cast<std::int32_t>(n);
    return true;
  }
  std::string list;
  for (long a : allowed) {
    list += (list.empty() ? "" : ", ") + std::to_string(a);
  }
  bad_request(conn, id, std::string("need the field ") + name + ", one of " + list);
  return false;
}

// ---- privacy.get and privacy.set

template <class T>
td_api::object_ptr<td_api::UserPrivacySetting> setting() {
  return make_object<T>();
}

// The keys of the page and the settings behind them.
struct Kind {
  const char *key;
  td_api::object_ptr<td_api::UserPrivacySetting> (*make)();
};

const Kind kSettings[] = {
    {"phone", setting<td_api::userPrivacySettingShowPhoneNumber>},
    {"phoneFind", setting<td_api::userPrivacySettingAllowFindingByPhoneNumber>},
    {"status", setting<td_api::userPrivacySettingShowStatus>},
    {"photo", setting<td_api::userPrivacySettingShowProfilePhoto>},
    {"forwards", setting<td_api::userPrivacySettingShowLinkInForwardedMessages>},
    {"calls", setting<td_api::userPrivacySettingAllowCalls>},
    {"birthday", setting<td_api::userPrivacySettingShowBirthdate>},
    {"gifts", setting<td_api::userPrivacySettingAutosaveGifts>},
    {"bio", setting<td_api::userPrivacySettingShowBio>},
    {"music", setting<td_api::userPrivacySettingShowProfileAudio>},
    {"invites", setting<td_api::userPrivacySettingAllowChatInvites>},
};

// A rule that adds something to the base: a list that holds someone, bots, Premium users. An empty list adds nothing
// (TDLib also leaves out of a list the chats it does not know), so it is not kept either.
bool is_exception(const td_api::UserPrivacySettingRule &r) {
  using namespace td_api;
  switch (r.get_id()) {
    case userPrivacySettingRuleAllowUsers::ID:
      return !static_cast<const userPrivacySettingRuleAllowUsers &>(r).user_ids_.empty();
    case userPrivacySettingRuleRestrictUsers::ID:
      return !static_cast<const userPrivacySettingRuleRestrictUsers &>(r).user_ids_.empty();
    case userPrivacySettingRuleAllowChatMembers::ID:
      return !static_cast<const userPrivacySettingRuleAllowChatMembers &>(r).chat_ids_.empty();
    case userPrivacySettingRuleRestrictChatMembers::ID:
      return !static_cast<const userPrivacySettingRuleRestrictChatMembers &>(r).chat_ids_.empty();
    case userPrivacySettingRuleAllowBots::ID:
    case userPrivacySettingRuleRestrictBots::ID:
    case userPrivacySettingRuleAllowPremiumUsers::ID:
      return true;
    default:
      return false;
  }
}

// What the rules give everyone the exceptions do not name. The first rule that matches decides (td_api.tl,
// userPrivacySettingRules), so the first base rule is the answer; none at all means "not allowed" there, and TDLib drops
// a trailing RestrictAll when it reads from Telegram, so "nobody" often has no rule of its own.
// RestrictContacts is not looked at: Telegram's apps never write it.
const char *base_of(const std::vector<Rule> &rules) {
  for (const Rule &r : rules) {
    if (!r) {
      continue;
    }
    switch (r->get_id()) {
      case td_api::userPrivacySettingRuleAllowAll::ID:
        return "everybody";
      case td_api::userPrivacySettingRuleAllowContacts::ID:
        return "contacts";
      case td_api::userPrivacySettingRuleRestrictAll::ID:
        return "nobody";
      default:
        break;
    }
  }
  return "nobody";
}

struct Password {
  bool set = false, recovery = false;
  std::string hint, pending;  // pending: where the confirmation code went; "" when none is waiting
  std::int32_t reset = 0;     // unix seconds after which the password can be reset at once; 0 none
};

Password password_of(const td_api::passwordState &s) {
  Password p;
  p.set = s.has_password_;
  p.hint = s.password_hint_;
  p.recovery = s.has_recovery_email_address_;
  if (s.recovery_email_address_code_info_) {
    p.pending = s.recovery_email_address_code_info_->email_address_pattern_;
  }
  p.reset = s.pending_reset_date_;
  return p;
}

auto password_json(const Password &p) {
  return td::json_object([p](auto &o) {
    o("set", td::JsonBool(p.set));
    o("hint", p.hint);
    o("recovery", td::JsonBool(p.recovery));
    o("pendingEmail", p.pending);
    o("resetDate", p.reset);
  });
}

// What privacy.get has heard so far. A field whose call failed stays empty and is left out of the reply.
struct Privacy {
  std::uint64_t conn;
  std::string id;
  int pending = 0;  // the calls not answered yet
  std::map<std::string, std::string> rules;
  std::set<std::string> exceptions;
  std::optional<Password> password;
  std::optional<std::int32_t> auto_delete, blocked, ttl_days, imported;
  std::optional<bool> top_chats;
};

void privacy_reply(const Privacy &p) {
  server.reply_ok(p.conn, p.id, td::json_encode<std::string>(td::json_object([&p](auto &o) {
                    o("rules", td::json_object([&p](auto &r) {
                      for (const Kind &k : kSettings) {
                        const auto it = p.rules.find(k.key);
                        if (it != p.rules.end()) {
                          r(td::Slice(k.key), it->second);
                        }
                      }
                    }));
                    o("exceptions", td::json_object([&p](auto &x) {
                      for (const std::string &key : p.exceptions) {
                        x(key, td::JsonBool(true));
                      }
                    }));
                    if (p.password) {
                      o("password", password_json(*p.password));
                    }
                    if (p.auto_delete) {
                      o("autoDelete", *p.auto_delete);
                    }
                    if (p.blocked) {
                      o("blocked", *p.blocked);
                    }
                    if (p.ttl_days) {
                      o("ttlDays", *p.ttl_days);
                    }
                    if (p.top_chats) {
                      o("topChats", td::JsonBool(*p.top_chats));
                    }
                    if (p.imported) {
                      o("imported", *p.imported);
                    }
                  })));
}

// One of privacy.get's calls: `take` reads the answer. The reply goes with the last answer, so one call that fails (it
// is logged) costs the page its field only.
template <class F>
void ask(const std::shared_ptr<Privacy> &p, const char *what, td_api::object_ptr<td_api::Function> call, F take) {
  ++p->pending;
  tdlib.send(std::move(call), [p, what, take](Obj o) {
    if (auto e = as_error(o)) {
      say("privacy.get: %s: %d %s", what, e->code_, e->message_.c_str());
    } else {
      take(*o);
    }
    if (--p->pending == 0) {
      privacy_reply(*p);
    }
  });
}

void privacy_get(std::uint64_t conn, const std::string &id, td::JsonObject &) {
  auto p = std::make_shared<Privacy>();
  p->conn = conn;
  p->id = id;
  for (const Kind &k : kSettings) {
    ask(p, k.key, make_object<td_api::getUserPrivacySettingRules>(k.make()),
        [p, key = std::string(k.key)](const td_api::Object &o) {
          const auto &rules = static_cast<const td_api::userPrivacySettingRules &>(o).rules_;
          p->rules[key] = base_of(rules);
          if (std::any_of(rules.begin(), rules.end(), [](const Rule &r) { return r && is_exception(*r); })) {
            p->exceptions.insert(key);
          }
        });
  }
  ask(p, "password", make_object<td_api::getPasswordState>(), [p](const td_api::Object &o) {
    p->password = password_of(static_cast<const td_api::passwordState &>(o));
  });
  ask(p, "auto-delete", make_object<td_api::getDefaultMessageAutoDeleteTime>(), [p](const td_api::Object &o) {
    p->auto_delete = static_cast<const td_api::messageAutoDeleteTime &>(o).time_;
  });
  ask(p, "blocked", make_object<td_api::getBlockedMessageSenders>(make_object<td_api::blockListMain>(), 0, 1),
      [p](const td_api::Object &o) { p->blocked = static_cast<const td_api::messageSenders &>(o).total_count_; });
  ask(p, "account ttl", make_object<td_api::getAccountTtl>(),
      [p](const td_api::Object &o) { p->ttl_days = static_cast<const td_api::accountTtl &>(o).days_; });
  // "disable_top_chats" is empty until Telegram says it is turned off: the page is on then.
  ask(p, "top chats", make_object<td_api::getOption>("disable_top_chats"), [p](const td_api::Object &o) {
    p->top_chats = !(o.get_id() == td_api::optionValueBoolean::ID &&
                     static_cast<const td_api::optionValueBoolean &>(o).value_);
  });
  ask(p, "imported contacts", make_object<td_api::getImportedContactCount>(),
      [p](const td_api::Object &o) { p->imported = static_cast<const td_api::count &>(o).count_; });
}

const Kind *find_setting(const std::string &key) {
  const auto it = std::find_if(std::begin(kSettings), std::end(kSettings), [&](const Kind &k) { return key == k.key; });
  return it == std::end(kSettings) ? nullptr : &*it;
}

void privacy_set(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto key = string_field(req, "key");
  auto value = string_field(req, "value");
  if (key.is_error() || value.is_error()) {
    return bad_request(conn, id, "need the fields key and value, strings");
  }
  const Kind *kind = find_setting(key.ok());
  if (!kind) {
    return bad_request(conn, id,
                       "key is phone, phoneFind, status, photo, forwards, calls, birthday, gifts, bio, music or invites");
  }
  const std::string &v = value.ok();
  if (v != "everybody" && v != "contacts" && v != "nobody") {
    return bad_request(conn, id, "value is everybody, contacts or nobody");
  }
  if (v == "nobody" && key.ok() == "phoneFind") {  // Telegram has no third answer to who finds us by our number
    return bad_request(conn, id, "phoneFind is everybody or contacts");
  }
  // The exceptions stay as Telegram has them, in their order; only the base changes. They come before it, where the
  // first match decides: a person on a list is answered by it, everyone else by the base.
  tdlib.send(make_object<td_api::getUserPrivacySettingRules>(kind->make()), [conn, id, kind, v](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    std::vector<Rule> next;
    for (Rule &r : static_cast<td_api::userPrivacySettingRules &>(*o).rules_) {
      if (r && is_exception(*r)) {
        next.push_back(std::move(r));
      }
    }
    if (v == "everybody") {
      next.push_back(make_object<td_api::userPrivacySettingRuleAllowAll>());
    } else {
      if (v == "contacts") {
        next.push_back(make_object<td_api::userPrivacySettingRuleAllowContacts>());
      }
      next.push_back(make_object<td_api::userPrivacySettingRuleRestrictAll>());  // TDLib leaves it off the wire
    }
    send_answer(conn, id,
                make_object<td_api::setUserPrivacySettingRules>(
                    kind->make(), make_object<td_api::userPrivacySettingRules>(std::move(next))));
  });
}

// ---- password.*: two-step verification

// Telegram's words for what went wrong with a password or its e-mail. PASSWORD_HASH_INVALID is `bad_password` in
// reply_td_error already. A "too fresh" error says how long to wait: PASSWORD_TOO_FRESH_<seconds> or
// SESSION_TOO_FRESH_<seconds>, the password or this session being younger than Telegram allows for the change.
void password_failed(std::uint64_t conn, const std::string &id, const td_api::error &e) {
  static const std::string kFresh = "_TOO_FRESH_";
  const std::size_t fresh = e.message_.find(kFresh);
  if (has(e, "EMAIL_INVALID")) {
    refuse(conn, id, "bad_email", e);
  } else if (has(e, "CODE_INVALID") || has(e, "EMAIL_VERIFY_EXPIRED") || has(e, "EMAIL_HASH_EXPIRED")) {
    refuse(conn, id, "bad_code", e);
  } else if (fresh != std::string::npos) {
    say("td error: %d %s", e.code_, e.message_.c_str());
    const std::size_t from = fresh + kFresh.size();
    const std::string secs = e.message_.substr(from, e.message_.find_first_not_of("0123456789", from) - from);
    server.reply_error(conn, id, "too_fresh", secs.empty() ? e.message_ : secs);
  } else {
    td_failed(conn, id, e);
  }
}

// The reply of every call that gives the password state back. TDLib takes EMAIL_UNCONFIRMED for an answer, not an error
// (PasswordManager.cpp, do_update_password_settings_impl): the state it gives then holds where the code went.
void password_reply(std::uint64_t conn, const std::string &id, const Obj &o) {
  if (auto e = as_error(o)) {
    return password_failed(conn, id, *e);
  }
  server.reply_ok(conn, id,
                  td::json_encode<std::string>(password_json(password_of(static_cast<const td_api::passwordState &>(*o)))));
}

void password_send(std::uint64_t conn, const std::string &id, td_api::object_ptr<td_api::Function> call) {
  tdlib.send(std::move(call), [conn, id](Obj o) { password_reply(conn, id, o); });
}

void password_set(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto old = string_field(req, "old");
  auto next = string_field(req, "new");
  td::JsonValue hint, email;
  const int has_hint = optional_field(req, "hint", td::JsonValue::Type::String, hint);
  const int has_email = optional_field(req, "email", td::JsonValue::Type::String, email);
  if (old.is_error() || next.is_error() || next.ok().empty() || has_hint < 0 || has_email < 0) {
    return bad_request(conn, id,
                       "need the fields old, a string, and new, a string that is not empty; hint and email are strings");
  }
  const std::string address = has_email == 1 ? email.get_string().str() : "";
  password_send(conn, id,
                make_object<td_api::setPassword>(old.move_as_ok(), next.move_as_ok(),
                                                 has_hint == 1 ? hint.get_string().str() : "", !address.empty(), address));
}

void password_email(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto password = string_field(req, "password");
  auto email = string_field(req, "email");
  if (password.is_error() || email.is_error() || email.ok().empty()) {
    return bad_request(conn, id, "need the fields password, a string, and email, a string that is not empty");
  }
  password_send(conn, id, make_object<td_api::setRecoveryEmailAddress>(password.move_as_ok(), email.move_as_ok()));
}

void password_code(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto code = string_field(req, "code");
  if (code.is_error() || code.ok().empty()) {
    return bad_request(conn, id, "need the field code, a string that is not empty");
  }
  tdlib.send(make_object<td_api::checkRecoveryEmailAddressCode>(code.move_as_ok()), [conn, id](Obj o) {
    // TDLib takes CODE_INVALID and EMAIL_HASH_EXPIRED for answers (PasswordManager.cpp, check_recovery_email_address_code)
    // and gives the state: a code that was no good leaves the e-mail waiting for one.
    if (!as_error(o) && static_cast<const td_api::passwordState &>(*o).recovery_email_address_code_info_) {
      return server.reply_error(conn, id, "bad_code", "the code was not accepted");
    }
    password_reply(conn, id, o);
  });
}

void password_off(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto old = string_field(req, "old");
  if (old.is_error()) {
    return bad_request(conn, id, "need the field old, the password, a string");
  }
  password_send(conn, id, make_object<td_api::setPassword>(old.move_as_ok(), "", "", false, ""));
}

// ---- autodelete.set, account.ttl, topchats.set

void autodelete_set(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  std::int32_t secs = 0;
  if (choice_field(conn, id, req, "secs", {0, 86400, 604800, 2678400}, secs)) {
    send_answer(conn, id,
                make_object<td_api::setDefaultMessageAutoDeleteTime>(make_object<td_api::messageAutoDeleteTime>(secs)));
  }
}

void account_ttl(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  std::int32_t days = 0;
  if (choice_field(conn, id, req, "days", {30, 90, 180, 365, 548, 730}, days)) {
    send_answer(conn, id, make_object<td_api::setAccountTtl>(make_object<td_api::accountTtl>(days)));
  }
}

void topchats_set(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  td::JsonValue on;
  if (optional_field(req, "on", td::JsonValue::Type::Boolean, on) != 1) {
    return bad_request(conn, id, "need the field on, true or false");
  }
  // The option says "disabled"; TDLib tells Telegram when it changes (TopDialogManager::update_is_enabled).
  send_answer(conn, id,
              make_object<td_api::setOption>("disable_top_chats",
                                             make_object<td_api::optionValueBoolean>(!on.get_boolean())));
}

// ---- blocked.list and blocked.set

void blocked_list(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  long offset = 0;
  if (number_field(req, "offset", offset) < 0 || offset < 0) {
    return bad_request(conn, id, "offset is a whole number, 0 or more");
  }
  tdlib.send(make_object<td_api::getBlockedMessageSenders>(make_object<td_api::blockListMain>(),
                                                           static_cast<std::int32_t>(offset), kBlockedPage),
             [conn, id](Obj o) {
               if (auto e = as_error(o)) {
                 return td_failed(conn, id, *e);
               }
               const auto &list = static_cast<const td_api::messageSenders &>(*o);
               std::vector<std::int64_t> missing;  // the users whose photo is not here
               server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                                 r("total", list.total_count_);
                                 r("people", td::json_array([&](auto &people) {
                                   for (const auto &s : list.senders_) {
                                     if (!s) {
                                       continue;
                                     }
                                     const bool user = s->get_id() == td_api::messageSenderUser::ID;
                                     const std::int64_t who =
                                         user ? static_cast<const td_api::messageSenderUser &>(*s).user_id_
                                              : static_cast<const td_api::messageSenderChat &>(*s).chat_id_;
                                     // A blocked chat has no photo here: the engine keeps those of the user's alone.
                                     const std::string photo = user ? user_photo(who) : std::string();
                                     if (user && photo.empty()) {
                                       missing.push_back(who);
                                     }
                                     people(td::json_object([&](auto &one) {
                                       one("jid", std::to_string(who));
                                       one("name", user ? user_name(who, true) : chat_title(who));  // "" when unknown
                                       one("kind", std::string(user ? "user" : "chat"));
                                       if (!photo.empty()) {
                                         one("photo", photo);
                                       }
                                     }));
                                   }
                                 }));
                               })));
               // After the reply, as pages.cpp does: each photo that comes arrives as a `photo` event.
               for (std::int64_t user : missing) {
                 user_photo_ask(user);
               }
             });
}

void blocked_set(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  auto jid = string_field(req, "jid");
  td::JsonValue block, kind;
  const int has_kind = optional_field(req, "kind", td::JsonValue::Type::String, kind);
  const std::string what = has_kind == 1 ? kind.get_string().str() : "user";
  std::int64_t who = 0;
  if (jid.is_error() || !parse_i64(jid.ok(), who) || who == 0 || has_kind < 0 || (what != "user" && what != "chat") ||
      (what == "user" && who < 0) || optional_field(req, "block", td::JsonValue::Type::Boolean, block) != 1) {
    return bad_request(conn, id,
                       "need the fields jid, a string with an id, and block, true or false; kind is user or chat");
  }
  td_api::object_ptr<td_api::MessageSender> sender;
  if (what == "chat") {
    sender = make_object<td_api::messageSenderChat>(who);
  } else {
    sender = make_object<td_api::messageSenderUser>(who);
  }
  td_api::object_ptr<td_api::BlockList> list;  // null takes the sender off the list
  if (block.get_boolean()) {
    list = make_object<td_api::blockListMain>();
  }
  send_answer(conn, id, make_object<td_api::setMessageSenderBlockList>(std::move(sender), std::move(list)));
}

// ---- websites.list and websites.disconnect

void websites_list(std::uint64_t conn, const std::string &id, td::JsonObject &) {
  tdlib.send(make_object<td_api::getConnectedWebsites>(), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    const auto &list = static_cast<const td_api::connectedWebsites &>(*o);
    server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&list](auto &r) {
                      r("websites", td::json_array([&list](auto &sites) {
                        for (const auto &w : list.websites_) {
                          if (!w) {
                            continue;
                          }
                          sites(td::json_object([&w](auto &one) {
                            one("id", std::to_string(w->id_));  // an int64: a string, as the other ids
                            one("domain", w->domain_name_);
                            one("bot", user_name(w->bot_user_id_, true));  // "" when unknown
                            one("browser", w->browser_);
                            one("platform", w->platform_);
                            one("ip", w->ip_address_);
                            one("location", w->location_);
                            one("login", w->log_in_date_);
                            one("active", w->last_active_date_);
                          }));
                        }
                      }));
                    })));
  });
}

void websites_disconnect(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  td::JsonValue all;
  if (optional_field(req, "all", td::JsonValue::Type::Boolean, all) < 0) {
    return bad_request(conn, id, "all is true or false");
  }
  if (all.type() == td::JsonValue::Type::Boolean && all.get_boolean()) {
    return send_answer(conn, id, make_object<td_api::disconnectAllWebsites>());
  }
  auto text = string_field(req, "id");
  std::int64_t site = 0;
  if (text.is_error() || !parse_i64(text.ok(), site)) {
    return bad_request(conn, id, "need the field id, a string with a website's id, or all: true");
  }
  send_answer(conn, id, make_object<td_api::disconnectWebsite>(site));
}

// ---- payments.clear

void payments_clear(std::uint64_t conn, const std::string &id, td::JsonObject &) {
  struct Both {
    int pending = 2;
    std::array<Obj, 2> answers;
  };
  auto both = std::make_shared<Both>();
  auto done = [conn, id, both](std::size_t at, Obj o) {
    both->answers[at] = std::move(o);
    if (--both->pending == 0) {  // the first error, else ok
      answer(conn, id, as_error(both->answers[0]) ? both->answers[0] : both->answers[1]);
    }
  };
  tdlib.send(make_object<td_api::deleteSavedOrderInfo>(), [done](Obj o) { done(0, std::move(o)); });
  tdlib.send(make_object<td_api::deleteSavedCredentials>(), [done](Obj o) { done(1, std::move(o)); });
}

// ---- contacts.clearImported and contacts.book

// Only the UI can read the phone's address book, and sends it only when the user turned "Sync contacts" on: uploading
// it to Telegram is a setting, never a default. The engine does not ask.
void contacts_book(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  td::JsonValue list;
  if (optional_field(req, "entries", td::JsonValue::Type::Array, list) != 1) {
    return bad_request(conn, id, "need the field entries, an array of objects with number and name");
  }
  std::vector<td_api::object_ptr<td_api::importedContact>> contacts;
  for (td::JsonValue &item : list.get_array()) {
    if (item.type() != td::JsonValue::Type::Object) {
      return bad_request(conn, id, "entries are objects with number and name");
    }
    auto number = string_field(item.get_object(), "number");
    auto name = string_field(item.get_object(), "name");
    if (number.is_error() || name.is_error()) {
      return bad_request(conn, id, "entries are objects with number and name, both strings");
    }
    std::string digits;  // international format, anything that is not a digit ignored, as link.phone
    for (char ch : number.ok()) {
      if (ch >= '0' && ch <= '9') {
        digits += ch;
      }
    }
    const std::string full = trim(name.ok());
    if (digits.empty() || full.empty()) {
      return bad_request(conn, id, "an entry's number holds digits and its name is not empty");
    }
    const std::size_t space = full.find(' ');  // the first name is what comes before the first space, as profile.set
    contacts.push_back(make_object<td_api::importedContact>(
        digits, full.substr(0, space), space == std::string::npos ? "" : trim(full.substr(space + 1)), nullptr));
  }
  tdlib.send(make_object<td_api::changeImportedContacts>(std::move(contacts)), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    const auto &users = static_cast<const td_api::importedContacts &>(*o).user_ids_;
    const auto imported =
        static_cast<std::int32_t>(std::count_if(users.begin(), users.end(), [](std::int64_t u) { return u != 0; }));
    server.reply_ok(conn, id,
                    td::json_encode<std::string>(td::json_object([imported](auto &r) { r("imported", imported); })));
  });
}

// ---- the commands

using Handler = void (*)(std::uint64_t, const std::string &, td::JsonObject &);

const struct {
  const char *cmd;
  Handler run;
} kCommands[] = {
    {"privacy.get", privacy_get},
    {"privacy.set", privacy_set},
    {"password.set", password_set},
    {"password.email", password_email},
    {"password.code", password_code},
    {"password.resend",
     [](std::uint64_t conn, const std::string &id, td::JsonObject &) {
       password_send(conn, id, make_object<td_api::resendRecoveryEmailAddressCode>());
     }},
    {"password.off", password_off},
    {"autodelete.set", autodelete_set},
    {"blocked.list", blocked_list},
    {"blocked.set", blocked_set},
    {"account.ttl", account_ttl},
    {"websites.list", websites_list},
    {"websites.disconnect", websites_disconnect},
    {"payments.clear", payments_clear},
    {"contacts.clearImported",
     [](std::uint64_t conn, const std::string &id, td::JsonObject &) {
       send_answer(conn, id, make_object<td_api::clearImportedContacts>());
     }},
    {"contacts.book", contacts_book},
    {"topchats.set", topchats_set},
};

}  // namespace

bool account_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  for (const auto &c : kCommands) {
    if (cmd != c.cmd) {
      continue;
    }
    if (link_ready()) {
      c.run(conn, id, req);
    } else {
      server.reply_error(conn, id, "unlinked", "not logged in");
    }
    return true;
  }
  return false;
}
