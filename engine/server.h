// The UI socket: loopback TCP, one JSON object per line (docs/protocol.md, "Connecting and framing").
// Main thread only. It owns no thread: main polls the fds that fill() lists and hands the results to service().
#pragma once

#include "td/utils/JsonBuilder.h"

#include <poll.h>

#include <cstdint>
#include <string>
#include <vector>

class Server {
 public:
  // Listens on 127.0.0.1, port chosen by the system, and publishes port and token in <dir>/engine.json.
  bool start(const std::string &dir);
  // Closes every connection and removes engine.json.
  void stop();

  // Appends the listener and then one entry per connection to fds. service() takes the same entries, polled.
  void fill(std::vector<pollfd> &fds);
  void service(const pollfd *fds);

  // Connections are named by number: a reply can come after its connection is gone, and is then dropped.
  void reply_ok(std::uint64_t conn, const std::string &id);
  void reply_ok(std::uint64_t conn, const std::string &id, const std::string &result_json);
  void reply_error(std::uint64_t conn, const std::string &id, const std::string &code, const std::string &message);
  bool alive(std::uint64_t conn);  // the connection is still open
  // One event line, as JSON, to every authenticated connection.
  void broadcast(const std::string &event_json);

 private:
  struct Conn {
    std::uint64_t id;
    int fd;  // -1 once closed; fill() drops those
    bool authed = false;
    bool close_after = false;  // close once `out` is sent
    std::string in, out;
  };

  Conn *find(std::uint64_t id);
  void accept_all();
  void read_conn(Conn &c);
  void on_line(Conn &c, std::string &line);
  void hello(Conn &c, const std::string &id, const std::string &cmd, td::JsonObject *req);
  void send_line(Conn &c, const std::string &json);
  void flush(Conn &c);
  void close_conn(Conn &c);

  std::string dir_, token_;
  int listen_fd_ = -1;
  std::uint64_t next_id_ = 1;
  std::vector<Conn> conns_;
};
