#include "engine.h"

#include "td/utils/Random.h"
#include "td/utils/misc.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {

constexpr std::size_t kLimit = 1 << 20;  // longest input line, and most unsent output, of one connection

unsigned long long num(std::uint64_t id) {
  return static_cast<unsigned long long>(id);
}

// Constant time, so a wrong token says nothing about how much of it was right.
bool same(const std::string &a, const std::string &b) {
  if (a.size() != b.size()) {
    return false;
  }
  unsigned char diff = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  }
  return diff == 0;
}

template <class F>
std::string object_line(F &&fill) {
  return td::json_encode<std::string>(td::json_object(std::forward<F>(fill)));
}

}  // namespace

td::Result<std::string> string_field(td::JsonObject &obj, const char *name) {
  auto v = obj.extract_required_field(td::Slice(name), td::JsonValue::Type::String);
  if (v.is_error()) {
    return v.move_as_error();
  }
  return v.ok().get_string().str();
}

int optional_field(td::JsonObject &obj, const char *name, td::JsonValue::Type type, td::JsonValue &v) {
  auto r = obj.extract_optional_field(td::Slice(name), type);
  if (r.is_error()) {
    return -1;
  }
  v = r.move_as_ok();
  return v.type() == type ? 1 : 0;
}

bool Server::start(const std::string &dir) {
  dir_ = dir;
  std::string raw(16, '\0');
  td::Random::secure_bytes(raw);
  token_ = td::hex_encode(raw);

  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // port 0: the system picks one
  socklen_t len = sizeof addr;
  if (listen_fd_ < 0 || bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0 ||
      listen(listen_fd_, 8) < 0 || !set_nonblock(listen_fd_) ||
      getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&addr), &len) < 0) {
    say("server: %s", std::strerror(errno));
    return false;
  }
  char info[96];
  std::snprintf(info, sizeof info, "{\"port\": %d, \"token\": \"%s\"}", ntohs(addr.sin_port), token_.c_str());
  const std::string tmp = dir + "/engine.json.tmp";
  if (!write_file(tmp, info) || rename(tmp.c_str(), (dir + "/engine.json").c_str()) < 0) {
    say("cannot write %s/engine.json: %s", dir.c_str(), std::strerror(errno));
    return false;
  }
  say("listening on 127.0.0.1:%d", ntohs(addr.sin_port));
  return true;
}

void Server::stop() {
  for (Conn &c : conns_) {
    if (c.fd >= 0) {
      ::close(c.fd);
    }
  }
  conns_.clear();
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  unlink((dir_ + "/engine.json").c_str());
}

void Server::fill(std::vector<pollfd> &fds) {
  std::erase_if(conns_, [](const Conn &c) { return c.fd < 0; });
  fds.push_back({listen_fd_, POLLIN, 0});
  for (const Conn &c : conns_) {
    // POLLOUT only while something is waiting to be sent: always asking for it would spin the loop.
    fds.push_back({c.fd, static_cast<short>((c.close_after ? 0 : POLLIN) | (c.out.empty() ? 0 : POLLOUT)), 0});
  }
}

void Server::service(const pollfd *fds) {
  // Connections are only added at the end, after this loop, so references stay valid while it runs.
  const std::size_t n = conns_.size();
  for (std::size_t i = 0; i < n; ++i) {
    Conn &c = conns_[i];
    const short ev = fds[1 + i].revents;
    if (c.fd < 0 || !ev) {
      continue;
    }
    if (ev & POLLOUT) {
      flush(c);
    }
    if (c.fd >= 0 && (ev & (POLLIN | POLLHUP | POLLERR))) {
      read_conn(c);
    } else if (c.fd >= 0 && (ev & POLLNVAL)) {
      close_conn(c);
    }
  }
  if (fds[0].revents & POLLIN) {
    accept_all();
  }
}

void Server::accept_all() {
  for (;;) {
    int fd = accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) {
      return;  // nothing more waiting
    }
    if (!set_nonblock(fd)) {
      ::close(fd);
      continue;
    }
    conns_.push_back(Conn{.id = next_id_++, .fd = fd});
    say("conn %llu: open", num(conns_.back().id));
  }
}

void Server::close_conn(Conn &c) {
  if (c.fd >= 0) {
    say("conn %llu: closed", num(c.id));
    ::close(c.fd);
    c.fd = -1;
    c.in.clear();
    c.out.clear();
    if (c.authed) {
      mode_conn_closed(c.id);
      live_conn_closed(c.id);
    }
  }
}

void Server::read_conn(Conn &c) {
  char buf[16384];
  ssize_t n = read(c.fd, buf, sizeof buf);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
    return;
  }
  if (n <= 0) {
    close_conn(c);
    return;
  }
  c.in.append(buf, static_cast<std::size_t>(n));
  std::size_t end;
  while (c.fd >= 0 && !c.close_after && (end = c.in.find('\n')) != std::string::npos) {
    if (end > kLimit) {
      break;
    }
    std::string line = c.in.substr(0, end);
    c.in.erase(0, end + 1);
    on_line(c, line);
  }
  if (c.fd >= 0 && c.in.size() > kLimit) {
    say("conn %llu: line over 1 MiB", num(c.id));
    close_conn(c);
  }
}

void Server::on_line(Conn &c, std::string &line) {
  std::string id, cmd;
  auto json = td::json_decode(td::MutableSlice(line));
  td::JsonObject *req = nullptr;
  if (json.is_ok() && json.ok_ref().type() == td::JsonValue::Type::Object) {
    req = &json.ok_ref().get_object();
    auto at = string_field(*req, "@");
    auto name = string_field(*req, "cmd");
    // The caps keep a reply (which echoes both) far under json_encode's 256 KB buffer.
    if (at.is_ok() && name.is_ok() && at.ok().size() <= 256 && name.ok().size() <= 64) {
      id = at.move_as_ok();
      cmd = name.move_as_ok();
    } else {
      req = nullptr;
    }
  }
  if (!c.authed) {
    hello(c, id, cmd, req);
    return;
  }
  if (!req) {
    reply_error(c.id, "", "bad_request", "need a JSON object with \"@\" and \"cmd\" strings");
    return;
  }
  say("conn %llu: %.40s", num(c.id), cmd.c_str());
  handle_request(c.id, id, cmd, *req);
}

void Server::hello(Conn &c, const std::string &id, const std::string &cmd, td::JsonObject *req) {
  const char *refused = nullptr, *why = nullptr;
  td::JsonValue audio, video;
  if (!req || cmd != "hello") {
    refused = "auth", why = "hello with the token first";
  } else if (auto token = string_field(*req, "token"); token.is_error() || !same(token.ok(), token_)) {
    refused = "auth", why = "wrong token";
  } else if (auto proto = req->extract_required_field("proto", td::JsonValue::Type::Number);
             proto.is_error() || proto.ok().get_number() != td::Slice("1")) {
    refused = "proto", why = "this engine speaks proto 1";
  } else if (optional_field(*req, "audio", td::JsonValue::Type::String, audio) < 0) {
    refused = "bad_request", why = "audio is a string: the call's id";
  } else if (optional_field(*req, "video", td::JsonValue::Type::String, video) < 0) {
    refused = "bad_request", why = "video is a string: the call's id";
  } else if (audio.type() == td::JsonValue::Type::String && video.type() == td::JsonValue::Type::String) {
    refused = "bad_request", why = "audio or video, not both";
  } else if (audio.type() == td::JsonValue::Type::String && !calls_audio_wanted(audio.get_string().str())) {
    refused = "not_found", why = "no such call";
  } else if (video.type() == td::JsonValue::Type::String && !calls_video_wanted(video.get_string().str())) {
    refused = "not_found", why = "no such call";
  }
  if (refused) {
    say("conn %llu: hello refused (%s)", num(c.id), refused);
    c.close_after = true;  // after the error line
    reply_error(c.id, id, refused, why);
    return;
  }
  const bool is_audio = audio.type() == td::JsonValue::Type::String;
  if (is_audio || video.type() == td::JsonValue::Type::String) {
    // The audio or video link of the live call is not a UI connection: no events, and nothing to undo when it closes.
    // The ok line must be out before the call takes the fd, and it is a short line on an empty loopback socket.
    reply_ok(c.id, id);
    for (int i = 0; i < 10 && c.fd >= 0 && !c.out.empty(); ++i) {
      pollfd p = {c.fd, POLLOUT, 0};
      poll(&p, 1, 10);
      flush(c);
    }
    if (c.fd >= 0 && c.out.empty()) {
      say("conn %llu: %s link", num(c.id), is_audio ? "audio" : "video");
      const int fd = c.fd;
      c.fd = -1;  // dropped from the server, not closed
      c.in.clear();
      if (is_audio) {
        calls_audio_attach(fd);
      } else {
        calls_video_attach(fd);
      }
    } else {
      close_conn(c);
    }
    return;
  }
  reply_ok(c.id, id, link_body());  // queued before authed is set, so no event comes first
  c.authed = true;
  calls_hello();
  location_hello();
}

Server::Conn *Server::find(std::uint64_t id) {
  for (Conn &c : conns_) {
    if (c.id == id && c.fd >= 0) {
      return &c;
    }
  }
  return nullptr;
}

bool Server::alive(std::uint64_t conn) {
  return find(conn) != nullptr;
}

void Server::send_line(Conn &c, const std::string &json) {
  if (c.out.size() + json.size() + 1 > kLimit) {
    say("conn %llu: too much unsent data", num(c.id));
    close_conn(c);  // the UI reconnects and re-reads chats.db
    return;
  }
  c.out += json;
  c.out += '\n';
  flush(c);
}

void Server::flush(Conn &c) {
  while (c.fd >= 0 && !c.out.empty()) {
    ssize_t n = write(c.fd, c.out.data(), c.out.size());
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        close_conn(c);
      }
      return;  // the rest waits for POLLOUT
    }
    c.out.erase(0, static_cast<std::size_t>(n));
  }
  if (c.close_after && c.out.empty()) {
    close_conn(c);
  }
}

void Server::reply_ok(std::uint64_t conn, const std::string &id) {
  if (Conn *c = find(conn)) {
    send_line(*c, object_line([&](auto &o) {
                o("@", id);
                o("ok", td::JsonTrue());
              }));
  }
}

void Server::reply_ok(std::uint64_t conn, const std::string &id, const std::string &result_json) {
  if (Conn *c = find(conn)) {
    send_line(*c, object_line([&](auto &o) {
                o("@", id);
                o("ok", td::JsonTrue());
                o("result", td::JsonRaw(result_json));
              }));
  }
}

void Server::reply_error(std::uint64_t conn, const std::string &id, const std::string &code,
                         const std::string &message) {
  if (Conn *c = find(conn)) {
    send_line(*c, object_line([&](auto &o) {
                o("@", id);
                o("ok", td::JsonFalse());
                o("error", td::json_object([&](auto &e) {
                    e("code", code);
                    e("message", message);
                  }));
              }));
  }
}

void Server::broadcast(const std::string &event_json) {
  for (Conn &c : conns_) {
    if (c.fd >= 0 && c.authed) {
      send_line(c, event_json);
    }
  }
}
