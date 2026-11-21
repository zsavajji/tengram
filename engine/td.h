// The TDLib client. Requests go out from the main thread. Responses and updates come back through a
// queue: a receive thread fills it and wakes the main thread, which drains it.
#pragma once

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace td_api = td::td_api;
using Obj = td_api::object_ptr<td_api::Object>;

class Td {
 public:
  using Callback = std::function<void(Obj)>;

  // Set before `close` is sent: the receive thread then ends at authorizationStateClosed.
  std::atomic<bool> stopping{false};

  // Creates the manager and the first client, and starts the receive thread. wake_fd is the write end
  // of the wake socket: one byte goes into it whenever the queue turns from empty to not empty.
  void start(int wake_fd);

  // A fresh client (after a logout closed the old one). Its first request starts TDLib.
  void new_client();

  // Main thread only. The callback gets the response, on the main thread.
  void send(td_api::object_ptr<td_api::Function> f, Callback cb = nullptr);
  void set_bool_option(const char *name, bool value);
  // How many requests with a callback TDLib has not answered yet (a download, a history page).
  std::size_t pending() const {
    return callbacks_.size();
  }
  // The TDLib type ids of those requests (td_api.h says which is which), for a log line.
  std::string pending_types() const;
  void set_int_option(const char *name, std::int64_t value);

  // Main thread only: runs the callbacks and handle_update() for everything queued so far.
  void drain();

 private:
  struct Item {
    td::ClientManager::ClientId client_id;
    td::ClientManager::RequestId request_id;
    Obj object;
  };

  void run();  // the receive thread

  td::ClientManager *mgr_ = nullptr;  // never deleted: its destructor would call receive() beside run()
  td::ClientManager::ClientId cid_ = 0;
  td::ClientManager::RequestId next_id_ = 0;
  std::map<td::ClientManager::RequestId, Callback> callbacks_;  // main thread only
  std::map<td::ClientManager::RequestId, std::int32_t> waiting_;  // the same requests' TDLib type ids, for the log
  int wake_fd_ = -1;

  std::mutex mu_;
  std::vector<Item> queue_;  // guarded by mu_
};

// The TDLib error in a response, or nullptr.
const td_api::error *as_error(const Obj &o);

// First token of to_string(), i.e. the TL constructor name ("updateNewChat"). For logs: no field values.
std::string type_name(const td_api::BaseObject &o);

// The text without the spaces at its two ends.
std::string trim(const std::string &s);
