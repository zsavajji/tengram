// The requests about our own account: profile.get and profile.set (docs/protocol.md). They name no chat, so they do
// not go through requests_handle, which starts by reading one.

#include "engine.h"

#include <algorithm>
#include <memory>

namespace {

using td_api::make_object;

void bad_request(std::uint64_t conn, const std::string &id, const std::string &what) {
  server.reply_error(conn, id, "bad_request", what);
}

void td_failed(std::uint64_t conn, const std::string &id, const td_api::error &e) {
  say("td error: %d %s", e.code_, e.message_.c_str());
  reply_td_error(conn, id, e);
}

// ---- profile.get: getMe, then the photo if it is not here yet, then getUserFullInfo for the bio

struct Profile {
  std::string name, about, phone, picture;
};

void profile_reply(std::uint64_t conn, const std::string &id, const Profile &p) {
  server.reply_ok(conn, id, td::json_encode<std::string>(td::json_object([&](auto &o) {
                    o("name", p.name);
                    o("about", p.about);
                    o("phone", p.phone);
                    o("picture", p.picture);
                  })));
}

void profile_get(std::uint64_t conn, const std::string &id) {
  tdlib.send(make_object<td_api::getMe>(), [conn, id](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    const auto &me = static_cast<const td_api::user &>(*o);
    auto p = std::make_shared<Profile>();
    p->name = me.first_name_ + (me.last_name_.empty() ? "" : " " + me.last_name_);
    p->phone = me.phone_number_.empty() ? "" : "+" + me.phone_number_;
    // A photo that does not come is no reason to fail the rest of the profile: the path is then "".
    download_path(me.profile_photo_ ? me.profile_photo_->big_.get() : nullptr, "profile.get: the picture",
                  [conn, id, p, me_id = me.id_](const std::string &path) {
                    p->picture = path;
                    tdlib.send(make_object<td_api::getUserFullInfo>(me_id), [conn, id, p](Obj o2) {
                      if (auto e = as_error(o2)) {
                        return td_failed(conn, id, *e);
                      }
                      const auto &bio = static_cast<const td_api::userFullInfo &>(*o2).bio_;
                      p->about = bio ? bio->text_ : "";
                      profile_reply(conn, id, *p);
                    });
                  });
  });
}

// ---- profile.set: the requests one after another, the first error answers

using Step = std::function<void(Td::Callback)>;  // sends one request and hands TDLib's answer to the callback

void run(const std::shared_ptr<std::vector<Step>> &steps, std::size_t at, std::uint64_t conn, const std::string &id) {
  if (at == steps->size()) {
    return server.reply_ok(conn, id);
  }
  (*steps)[at]([=](Obj o) {
    if (auto e = as_error(o)) {
      return td_failed(conn, id, *e);
    }
    run(steps, at + 1, conn, id);
  });
}

// An empty picture deletes the current one; with none there is nothing to do.
void delete_photo(Td::Callback done) {
  tdlib.send(make_object<td_api::getMe>(), [done](Obj o) {
    if (as_error(o)) {
      return done(std::move(o));
    }
    const auto &photo = static_cast<const td_api::user &>(*o).profile_photo_;
    if (!photo) {
      return done(make_object<td_api::ok>());
    }
    tdlib.send(make_object<td_api::deleteProfilePhoto>(photo->id_), done);
  });
}

// The characters of a UTF-8 string, which is how Telegram counts its limits: the bytes that do not continue one.
std::size_t code_points(const std::string &s) {
  return std::count_if(s.begin(), s.end(), [](char c) { return (c & 0xC0) != 0x80; });
}

void profile_set(std::uint64_t conn, const std::string &id, td::JsonObject &req) {
  td::JsonValue name, about, picture;
  const int has_name = optional_field(req, "name", td::JsonValue::Type::String, name);
  const int has_about = optional_field(req, "about", td::JsonValue::Type::String, about);
  const int has_picture = optional_field(req, "picture", td::JsonValue::Type::String, picture);
  if (has_name < 0 || has_about < 0 || has_picture < 0) {
    return bad_request(conn, id, "name, about and picture are strings");
  }
  if (has_name + has_about + has_picture == 0) {
    return bad_request(conn, id, "at least one of name, about, picture");
  }

  // All of it is checked before anything is sent: a request that fails here changes nothing.
  std::string first, last;
  if (has_name) {
    const std::string full = trim(name.get_string().str());
    if (full.empty() || code_points(full) > 64) {
      return bad_request(conn, id, "name is 1 to 64 characters");
    }
    const std::size_t space = full.find(' ');  // Telegram's first name is the part before the first space
    first = full.substr(0, space);
    last = space == std::string::npos ? "" : trim(full.substr(space + 1));
  }
  const std::string bio = has_about ? about.get_string().str() : "";
  if (code_points(bio) > 70) {
    return bad_request(conn, id, "about is at most 70 characters");
  }
  const std::string path = has_picture ? picture.get_string().str() : "";
  std::int64_t bytes = 0;
  if (!path.empty() && !regular_file(path, bytes)) {
    return bad_request(conn, id, "picture is \"\" or the absolute path of a file");
  }

  auto steps = std::make_shared<std::vector<Step>>();
  if (has_name) {
    steps->push_back([first, last](Td::Callback cb) { tdlib.send(make_object<td_api::setName>(first, last), cb); });
  }
  if (has_about) {
    steps->push_back([bio](Td::Callback cb) { tdlib.send(make_object<td_api::setBio>(bio), cb); });
  }
  if (has_picture && path.empty()) {
    steps->push_back(delete_photo);
  } else if (has_picture) {
    steps->push_back([path](Td::Callback cb) {
      tdlib.send(make_object<td_api::setProfilePhoto>(
                     make_object<td_api::inputChatPhotoStatic>(make_object<td_api::inputFileLocal>(path)), false),
                 cb);
    });
  }
  run(steps, 0, conn, id);
}

}  // namespace

bool profile_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  if (cmd != "profile.get" && cmd != "profile.set") {
    return false;
  }
  if (!link_ready()) {
    server.reply_error(conn, id, "unlinked", "not logged in");
  } else if (cmd == "profile.get") {
    profile_get(conn, id);
  } else {
    profile_set(conn, id, req);
  }
  return true;
}
