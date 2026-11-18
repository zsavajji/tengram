// The requests behind the info pages: contact.info, channel.info, group.info, contacts.list, senders.get, chat.clear,
// chat.delete and channel.unfollow (docs/protocol.md). Each is a chain of TDLib calls with one reply at the end;
// what the pages show of people and chats comes from the mirror in chats.cpp (user_name, chat_title) where TDLib
// guarantees it is there already: an updateUser or updateNewChat comes before any answer names the user or the chat.

#include "engine.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <unordered_map>

namespace {

using td_api::make_object;

constexpr int kMembers = 200;        // the most members group.info lists (TDLib gives 200 a call)
constexpr int kMemberPhotos = 50;    // the most member photos one group.info asks for in the background
constexpr int kContactPhotos = 100;  // the same for contacts.list
constexpr std::size_t kSenders = 100;  // the most users one senders.get names
constexpr int kSenderPhotos = 50;      // the most sender photos one senders.get asks for in the background

void td_failed(std::uint64_t conn, const std::string &id, const td_api::error &e) {
  say("td error: %d %s", e.code_, e.message_.c_str());
  reply_td_error(conn, id, e);
}

void bad_chat(std::uint64_t conn, const std::string &id, const char *what) {
  server.reply_error(conn, id, "bad_chat", what);
}

}  // namespace

// getChat, then `next` gets the chat. A chat TDLib does not know is bad_chat. This and the three helpers below are
// outside the anonymous namespace because manage.cpp uses them too (engine.h).
void with_chat(std::uint64_t conn, const std::string &id, std::int64_t chat,
               std::function<void(const td_api::chat &)> next) {
  tdlib.send(make_object<td_api::getChat>(chat), [conn, id, next](Obj o) {
    if (as_error(o)) {
      return bad_chat(conn, id, "no such chat");
    }
    next(static_cast<const td_api::chat &>(*o));
  });
}

// ---- what a chat is. The ids are TDLib's positive ones (a user, a basic group, a supergroup): 0 says it is not that.

// The user a private or a secret chat is with.
std::int64_t person_of(const td_api::chat &c) {
  if (c.type_ && c.type_->get_id() == td_api::chatTypePrivate::ID) {
    return static_cast<const td_api::chatTypePrivate &>(*c.type_).user_id_;
  }
  if (c.type_ && c.type_->get_id() == td_api::chatTypeSecret::ID) {
    return static_cast<const td_api::chatTypeSecret &>(*c.type_).user_id_;
  }
  return 0;
}

std::int64_t basic_group_of(const td_api::chat &c) {
  if (c.type_ && c.type_->get_id() == td_api::chatTypeBasicGroup::ID) {
    return static_cast<const td_api::chatTypeBasicGroup &>(*c.type_).basic_group_id_;
  }
  return 0;
}

// The supergroup of a chat, when it is a channel (`channel` true) or a group that is not (false).
std::int64_t supergroup_of(const td_api::chat &c, bool channel) {
  if (c.type_ && c.type_->get_id() == td_api::chatTypeSupergroup::ID) {
    const auto &t = static_cast<const td_api::chatTypeSupergroup &>(*c.type_);
    if (t.is_channel_ == channel) {
      return t.supergroup_id_;
    }
  }
  return 0;
}

namespace {

// ---- photos of people, which come after the reply

// Asks for the small photos that a reply left out, at most `cap` of them; each arrives as a `photo` event. Called
// after the reply is written: a page that gets the event before the list drops it.
void ask_photos(const std::vector<std::int64_t> &users, int cap) {
  for (std::int64_t user : users) {
    if (cap > 0 && user_photo_ask(user)) {
      --cap;
    }
  }
}

// ---- contact.info

struct Contact {
  std::string phone, about, picture;
  bool blocked = false;
  std::vector<std::int64_t> groups;  // the chats we have in common
};

void contact_reply(std::uint64_t conn, const std::string &id, const Contact &c) {
  server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &o) {
                    o("phone", c.phone);
                    o("about", c.about);
                    o("groups", td::json_array([&](auto &list) {
                      for (std::int64_t group : c.groups) {
                        list(td::json_object([&](auto &one) {
                          one("jid", std::to_string(group));
                          one("name", chat_title(group));
                        }));
                      }
                    }));
                    o("blocked", td::JsonBool(c.blocked));
                    o("picture", c.picture);
                  })));
}

// getUserFullInfo for the bio and the block list, then the groups in common.
void contact_full(std::uint64_t conn, const std::string &id, std::int64_t user, const std::shared_ptr<Contact> &c) {
  tdlib.send(make_object<td_api::getUserFullInfo>(user), [conn, id, user, c](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    const auto &full = static_cast<const td_api::userFullInfo &>(*o);
    c->about = full.bio_ ? full.bio_->text_ : "";
    c->blocked = full.block_list_ != nullptr;
    if (user == link_me_id()) {  // Saved Messages: TDLib has no groups in common with ourselves
      return contact_reply(conn, id, *c);
    }
    tdlib.send(make_object<td_api::getGroupsInCommon>(user, 0, 100), [conn, id, c](Obj o2) {
      if (auto e = as_error(o2)) {
        return td_failed(conn, id, *e);
      }
      c->groups = static_cast<const td_api::chats &>(*o2).chat_ids_;
      contact_reply(conn, id, *c);
    });
  });
}

void contact_info(std::uint64_t conn, const std::string &id, std::int64_t chat) {
  with_chat(conn, id, chat, [conn, id](const td_api::chat &c) {
    const std::int64_t user = person_of(c);
    if (!user) {
      return bad_chat(conn, id, "not a chat with a person");
    }
    tdlib.send(make_object<td_api::getUser>(user), [conn, id, user](Obj o) {
      if (auto e = as_error(o)) {
        return td_failed(conn, id, *e);
      }
      const auto &u = static_cast<const td_api::user &>(*o);
      auto contact = std::make_shared<Contact>();
      contact->phone = u.phone_number_.empty() ? "" : "+" + u.phone_number_;
      download_path(u.profile_photo_ ? u.profile_photo_->big_.get() : nullptr, "contact.info: the picture",
                    [conn, id, user, contact](const std::string &path) {
                      contact->picture = path;
                      contact_full(conn, id, user, contact);
                    });
    });
  });
}

// ---- channel.info

void channel_info(std::uint64_t conn, const std::string &id, std::int64_t chat) {
  with_chat(conn, id, chat, [conn, id](const td_api::chat &c) {
    const std::int64_t channel = supergroup_of(c, true);
    if (!channel) {
      return bad_chat(conn, id, "not a channel");
    }
    download_path(c.photo_ ? c.photo_->big_.get() : nullptr, "channel.info: the picture",
                  [conn, id, channel](const std::string &picture) {
                    tdlib.send(make_object<td_api::getSupergroupFullInfo>(channel), [conn, id, picture](Obj o) {
                      if (auto e = as_error(o)) {
                        return td_failed(conn, id, *e);
                      }
                      const auto &full = static_cast<const td_api::supergroupFullInfo &>(*o);
                      server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                                        r("followers", full.member_count_);
                                        r("description", full.description_);
                                        r("picture", picture);
                                      })));
                    });
                  });
  });
}

// ---- group.info

struct Member {
  std::int64_t user;
  bool admin;
};

struct Group {
  std::string subject, description, picture;
  bool members_add = false;
  std::int32_t count = 0;  // all the members, not the ones listed
  std::vector<Member> members;
};

// The members that are users (a supergroup can have chats as members; they are skipped), at most kMembers.
void take_members(Group &g, const std::vector<td_api::object_ptr<td_api::chatMember>> &list) {
  for (const auto &m : list) {
    if (g.members.size() >= static_cast<std::size_t>(kMembers)) {
      break;
    }
    if (!m || !m->member_id_ || m->member_id_->get_id() != td_api::messageSenderUser::ID) {
      continue;
    }
    const int status = m->status_ ? m->status_->get_id() : 0;
    g.members.push_back({static_cast<const td_api::messageSenderUser &>(*m->member_id_).user_id_,
                         status == td_api::chatMemberStatusCreator::ID ||
                             status == td_api::chatMemberStatusAdministrator::ID});
  }
}

void group_reply(std::uint64_t conn, const std::string &id, const Group &g) {
  std::vector<std::int64_t> missing;  // the members whose photo is not here
  for (const Member &m : g.members) {
    if (user_photo(m.user).empty()) {
      missing.push_back(m.user);
    }
  }
  server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &o) {
                    o("subject", g.subject);
                    o("description", g.description);
                    o("participants", td::json_array([&](auto &list) {
                      for (const Member &m : g.members) {
                        const std::string &photo = user_photo(m.user);
                        list(td::json_object([&](auto &one) {
                          one("jid", std::to_string(m.user));
                          one("name", user_name(m.user, true));  // "" when unknown: the UI says "Unknown"
                          one("admin", td::JsonBool(m.admin));
                          one("me", td::JsonBool(m.user == link_me_id()));
                          if (!photo.empty()) {
                            one("photo", photo);
                          }
                        }));
                      }
                    }));
                    o("count", g.count);
                    o("picture", g.picture);
                    o("membersAdd", td::JsonBool(g.members_add));
                  })));
  ask_photos(missing, kMemberPhotos);
}

void basic_group_members(std::uint64_t conn, const std::string &id, std::int64_t group,
                         const std::shared_ptr<Group> &g) {
  tdlib.send(make_object<td_api::getBasicGroupFullInfo>(group), [conn, id, g](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    const auto &full = static_cast<const td_api::basicGroupFullInfo &>(*o);
    g->description = full.description_;
    g->count = static_cast<std::int32_t>(full.members_.size());
    take_members(*g, full.members_);
    group_reply(conn, id, *g);
  });
}

void supergroup_members(std::uint64_t conn, const std::string &id, std::int64_t group,
                        const std::shared_ptr<Group> &g) {
  tdlib.send(make_object<td_api::getSupergroupFullInfo>(group), [conn, id, group, g](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    const auto &full = static_cast<const td_api::supergroupFullInfo &>(*o);
    g->description = full.description_;
    g->count = full.member_count_;
    // A group that does not let us list its members still lets us list its administrators.
    td_api::object_ptr<td_api::SupergroupMembersFilter> filter;
    if (full.can_get_members_) {
      filter = make_object<td_api::supergroupMembersFilterRecent>();
    } else {
      filter = make_object<td_api::supergroupMembersFilterAdministrators>();
    }
    tdlib.send(make_object<td_api::getSupergroupMembers>(group, std::move(filter), 0, kMembers),
               [conn, id, g](Obj o2) {
                 if (auto e = as_error(o2)) {
                   return td_failed(conn, id, *e);
                 }
                 take_members(*g, static_cast<const td_api::chatMembers &>(*o2).members_);
                 group_reply(conn, id, *g);
               });
  });
}

void group_info(std::uint64_t conn, const std::string &id, std::int64_t chat) {
  with_chat(conn, id, chat, [conn, id](const td_api::chat &c) {
    const std::int64_t basic = basic_group_of(c);
    const std::int64_t super = supergroup_of(c, false);
    if (!basic && !super) {
      return bad_chat(conn, id, "not a group");
    }
    auto g = std::make_shared<Group>();
    g->subject = c.title_;
    g->members_add = c.permissions_ && c.permissions_->can_invite_users_;
    download_path(c.photo_ ? c.photo_->big_.get() : nullptr, "group.info: the picture",
                  [conn, id, basic, super, g](const std::string &path) {
                    g->picture = path;
                    if (basic) {
                      basic_group_members(conn, id, basic, g);
                    } else {
                      supergroup_members(conn, id, super, g);
                    }
                  });
  });
}

// ---- contacts.list

struct Person {
  std::int64_t user;
  std::string name, key;  // key: the name in lower case, what the list is sorted by
};

void contacts_list(std::uint64_t conn, const std::string &id) {
  tdlib.send(make_object<td_api::getContacts>(), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    std::vector<Person> people;
    for (std::int64_t user : static_cast<const td_api::users &>(*o).user_ids_) {
      Person p = {user, user_name(user, true), ""};
      p.key = p.name;
      // ponytail: ASCII only, a name in another script sorts by its bytes; ICU would fold it, the phone has none.
      for (char &ch : p.key) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      }
      people.push_back(std::move(p));
    }
    std::sort(people.begin(), people.end(), [](const Person &a, const Person &b) {
      return a.key != b.key ? a.key < b.key : a.user < b.user;
    });
    std::vector<std::int64_t> missing;  // the contacts whose photo is not here
    for (const Person &p : people) {
      if (user_photo(p.user).empty()) {
        missing.push_back(p.user);
      }
    }
    server.reply_ok(conn, id, td::json_encode<std::string>(td::json_array([&](auto &list) {
                      for (const Person &p : people) {
                        const std::string &photo = user_photo(p.user);
                        list(td::json_object([&](auto &one) {
                          one("jid", std::to_string(p.user));
                          one("name", p.name);
                          if (!photo.empty()) {
                            one("photo", photo);
                          }
                        }));
                      }
                    })));
    ask_photos(missing, kContactPhotos);
  });
}

// ---- senders.get

// What a group chat's cards show of each sender: the colour of the name, an admin's tag, the small photo. Everything
// comes from the mirror in chats.cpp except the tags, which TDLib gives for the chat as a whole.
struct Senders {
  std::vector<std::int64_t> users;
  std::unordered_map<std::int64_t, std::string> tags;  // an administrator's tag; the others have none
};

void senders_reply(std::uint64_t conn, const std::string &id, const Senders &s) {
  std::vector<std::int64_t> missing;  // the senders whose photo is not here
  server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &o) {
                    for (std::int64_t user : s.users) {
                      int color = 0;
                      if (!user_color(user, color)) {  // not known: left out, the UI keeps its own colour for them
                        continue;
                      }
                      const auto tag = s.tags.find(user);
                      const std::string &photo = user_photo(user);
                      if (photo.empty()) {
                        missing.push_back(user);
                      }
                      o(std::to_string(user), td::json_object([&](auto &one) {
                          one("color", color);
                          one("tag", tag == s.tags.end() ? std::string() : tag->second);
                          if (!photo.empty()) {
                            one("photo", photo);
                          }
                        }));
                    }
                  })));
  ask_photos(missing, kSenderPhotos);
}

void senders_get(std::uint64_t conn, const std::string &id, td::JsonObject &req, std::int64_t chat) {
  td::JsonValue list;
  if (optional_field(req, "users", td::JsonValue::Type::Array, list) != 1) {
    return server.reply_error(conn, id, "bad_request", "need users, an array of user ids");
  }
  if (list.get_array().size() > kSenders) {  // before the walk below, which looks for duplicates in what it has
    return server.reply_error(conn, id, "bad_request", "users holds 1 to 100 user ids");
  }
  auto s = std::make_shared<Senders>();
  for (const td::JsonValue &item : list.get_array()) {
    std::int64_t user = 0;
    if (item.type() != td::JsonValue::Type::String || !parse_i64(item.get_string().str(), user)) {
      return server.reply_error(conn, id, "bad_request", "users are strings with user ids");
    }
    if (std::find(s->users.begin(), s->users.end(), user) == s->users.end()) {
      s->users.push_back(user);
    }
  }
  if (s->users.empty() || s->users.size() > kSenders) {
    return server.reply_error(conn, id, "bad_request", "users holds 1 to 100 user ids");
  }
  with_chat(conn, id, chat, [conn, id, chat, s](const td_api::chat &c) {
    if (!basic_group_of(c) && !supergroup_of(c, false)) {
      return bad_chat(conn, id, "not a group");
    }
    tdlib.send(make_object<td_api::getChatAdministrators>(chat), [conn, id, s](Obj o) {
      if (auto e = as_error(o)) {  // no tags then, and the colours and photos still go
        say("senders.get: administrators: %d %s", e->code_, e->message_.c_str());
      } else {
        for (const auto &a : static_cast<const td_api::chatAdministrators &>(*o).administrators_) {
          if (a) {
            s->tags[a->user_id_] = !a->custom_title_.empty() ? a->custom_title_ : a->is_owner_ ? "owner" : "admin";
          }
        }
      }
      senders_reply(conn, id, *s);
    });
  });
}

// ---- chat.clear, chat.delete, channel.unfollow

// What deleteChatHistory's revoke has to be for TDLib to take it. The history goes for us alone (revoke false), which
// TDLib allows for every chat but an active secret chat: that one is only ever deleted for both sides, and
// chat.can_be_deleted_only_for_self says so. Nothing else is ever revoked.
bool revoke_of(const td_api::chat &c) {
  return c.type_ && c.type_->get_id() == td_api::chatTypeSecret::ID && !c.can_be_deleted_only_for_self_;
}

// Deleting a group is leaving it. A basic group that we left stays in the list with its messages, and TDLib lets its
// history be deleted from the list after that. A supergroup's or a channel's it does not (deleteChatHistory is only
// for a member of a private supergroup), so there leaving is all, and whether the chat then leaves the list is TDLib's.
void leave(std::uint64_t conn, const std::string &id, std::int64_t chat, bool basic_group) {
  tdlib.send(make_object<td_api::leaveChat>(chat), [conn, id, chat, basic_group](Obj o) {
    if (!basic_group || as_error(o)) {
      return answer(conn, id, o);
    }
    tdlib.send(make_object<td_api::deleteChatHistory>(chat, true, false), [conn, id](Obj o2) { answer(conn, id, o2); });
  });
}

void chat_clear(std::uint64_t conn, const std::string &id, std::int64_t chat) {
  with_chat(conn, id, chat, [conn, id, chat](const td_api::chat &c) {
    // The chat stays in the list; the messages go with TDLib's updateDeleteMessages, as msg.delete's do.
    tdlib.send(make_object<td_api::deleteChatHistory>(chat, false, revoke_of(c)),
               [conn, id](Obj o) { answer(conn, id, o); });
  });
}

void chat_delete(std::uint64_t conn, const std::string &id, std::int64_t chat) {
  with_chat(conn, id, chat, [conn, id, chat](const td_api::chat &c) {
    if (person_of(c)) {
      tdlib.send(make_object<td_api::deleteChatHistory>(chat, true, revoke_of(c)),
                 [conn, id](Obj o) { answer(conn, id, o); });
    } else {
      leave(conn, id, chat, basic_group_of(c) != 0);
    }
  });
}

void channel_unfollow(std::uint64_t conn, const std::string &id, std::int64_t chat) {
  with_chat(conn, id, chat, [conn, id, chat](const td_api::chat &c) {
    if (!supergroup_of(c, true)) {
      return bad_chat(conn, id, "not a channel");
    }
    leave(conn, id, chat, false);
  });
}

}  // namespace

bool pages_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  static const char *const ours[] = {"contact.info", "channel.info", "group.info", "contacts.list",
                                     "senders.get",  "chat.clear",   "chat.delete", "channel.unfollow"};
  if (std::find(std::begin(ours), std::end(ours), cmd) == std::end(ours)) {
    return false;
  }
  if (cmd == "contacts.list") {  // no chat
    if (link_ready()) {
      contacts_list(conn, id);
    } else {
      server.reply_error(conn, id, "unlinked", "not logged in");
    }
    return true;
  }
  std::int64_t chat = 0;
  if (!request_chat(conn, id, req, chat)) {
    return true;
  }
  if (cmd == "contact.info") {
    contact_info(conn, id, chat);
  } else if (cmd == "channel.info") {
    channel_info(conn, id, chat);
  } else if (cmd == "group.info") {
    group_info(conn, id, chat);
  } else if (cmd == "senders.get") {
    senders_get(conn, id, req, chat);
  } else if (cmd == "chat.clear") {
    chat_clear(conn, id, chat);
  } else if (cmd == "chat.delete") {
    chat_delete(conn, id, chat);
  } else {
    channel_unfollow(conn, id, chat);
  }
  return true;
}
