#include "engine.h"

#include <unistd.h>

#include <thread>

const td_api::error *as_error(const Obj &o) {
  return o->get_id() == td_api::error::ID ? static_cast<const td_api::error *>(o.get()) : nullptr;
}

std::string type_name(const td_api::BaseObject &o) {
  std::string s = td_api::to_string(o);
  return s.substr(0, s.find_first_of(" \n"));
}

void reply_td_error(std::uint64_t conn, const std::string &id, const td_api::error &e) {
  const std::string &m = e.message_;
  auto has = [&](const char *part) { return m.find(part) != std::string::npos; };
  const char *code = "td";
  if (has("PHONE_NUMBER_INVALID")) {
    code = "bad_phone";
  } else if (has("PHONE_CODE_INVALID") || has("PHONE_CODE_EMPTY") || has("PHONE_CODE_EXPIRED")) {
    code = "bad_code";
  } else if (has("PASSWORD_HASH_INVALID")) {
    code = "bad_password";
  }
  server.reply_error(conn, id, code, std::to_string(e.code_) + " " + m);
}

void Td::start(int wake_fd) {
  wake_fd_ = wake_fd;
  td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
  mgr_ = new td::ClientManager;
  new_client();
  std::thread([this] { run(); }).detach();
}

void Td::new_client() {
  callbacks_.clear();  // the old client answered everything it was going to before it closed
  cid_ = mgr_->create_client_id();
  send(td_api::make_object<td_api::getOption>("version"));  // activates the client
}

void Td::send(td_api::object_ptr<td_api::Function> f, Callback cb) {
  ++next_id_;
  if (cb) {
    callbacks_[next_id_] = std::move(cb);
  }
  mgr_->send(cid_, next_id_, std::move(f));
}

void Td::set_bool_option(const char *name, bool value) {
  send(td_api::make_object<td_api::setOption>(name, td_api::make_object<td_api::optionValueBoolean>(value)));
}

void Td::run() {
  for (;;) {
    auto r = mgr_->receive(300.0);
    if (!r.object) {
      continue;
    }
    const bool closed =
        r.request_id == 0 && r.object->get_id() == td_api::updateAuthorizationState::ID &&
        static_cast<td_api::updateAuthorizationState &>(*r.object).authorization_state_->get_id() ==
            td_api::authorizationStateClosed::ID;
    bool was_empty;
    {
      std::lock_guard<std::mutex> lock(mu_);
      was_empty = queue_.empty();
      queue_.push_back({r.client_id, r.request_id, std::move(r.object)});
    }
    if (was_empty) {
      ssize_t n = write(wake_fd_, "t", 1);  // a full socket already holds a wake-up
      (void)n;
    }
    if (closed && stopping) {
      return;
    }
  }
}

void Td::drain() {
  std::vector<Item> items;
  {
    std::lock_guard<std::mutex> lock(mu_);
    items.swap(queue_);
  }
  for (Item &it : items) {
    if (it.client_id != cid_) {  // an old client: new_client() ran while this batch was being handled
      continue;
    }
    if (it.request_id == 0) {
      handle_update(std::move(it.object));
      continue;
    }
    auto found = callbacks_.find(it.request_id);
    if (found == callbacks_.end()) {
      if (auto e = as_error(it.object)) {
        say("error (request %llu): %d %s", static_cast<unsigned long long>(it.request_id), e->code_,
            e->message_.c_str());
      }
      continue;
    }
    Callback cb = std::move(found->second);
    callbacks_.erase(found);
    cb(std::move(it.object));
  }
}
