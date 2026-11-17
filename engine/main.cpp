// tengramd: the engine of Tengram, a Telegram client for BlackBerry 10. The UI reads chats.db and talks to
// the engine over a loopback socket (docs/protocol.md). Usage: tengramd <data-dir>, with TG_API_ID and
// TG_API_HASH in the environment.
//
// This thread owns everything (the database, the sockets, all state) and sleeps in poll(). The TDLib thread
// (td.cpp) only receives and queues; a third thread waits for signals. The engine has no timer of its own:
// idle, it wakes only for TDLib or the UI (the one exception is the 5 s limit of a stop).

#include "engine.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

Db db;
Td tdlib;
Server server;
std::string data_dir;
int api_id = 0;
std::string api_hash;
bool done = false;

namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point t0 = Clock::now();  // "process start": static init, before main

void usage(std::FILE *to) {
  std::fprintf(to, "usage: tengramd <data-dir>\n"
                   "  TG_API_ID and TG_API_HASH (from my.telegram.org) must be in the environment\n");
}

// mkdir -p. Whether it worked is for the caller to see.
void make_dirs(const std::string &path) {
  for (std::size_t i = 1; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      mkdir(path.substr(0, i).c_str(), 0700);
    }
  }
}

// Reads what is waiting in the wake socket. True if one of the bytes was the signal thread's 'q'.
bool drain_wake(int fd) {
  bool quit = false;
  char buf[64];
  for (ssize_t n; (n = read(fd, buf, sizeof buf)) > 0;) {
    quit = quit || std::memchr(buf, 'q', static_cast<std::size_t>(n)) != nullptr;
  }
  return quit;
}

}  // namespace

void say(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
  std::fprintf(stderr, "[%6lld] %s\n", static_cast<long long>(ms), buf);
}

bool write_file(const std::string &path, const std::string &data) {
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) {
    return false;
  }
  const bool ok = fchmod(fd, 0600) == 0 && write(fd, data.data(), data.size()) == static_cast<ssize_t>(data.size());
  return close(fd) == 0 && ok;
}

bool set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

void handle_update(Obj update) {
  switch (update->get_id()) {
    case td_api::updateAuthorizationState::ID:
      link_auth(*static_cast<td_api::updateAuthorizationState &>(*update).authorization_state_);
      break;
    case td_api::updateConnectionState::ID:
      link_conn(*static_cast<td_api::updateConnectionState &>(*update).state_);
      break;
    default:  // everything else is ignored until a step needs it
      break;
  }
}

void handle_request(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  if (link_request(conn, id, cmd, req)) {
    return;
  }
  server.reply_error(conn, id, "unknown_cmd", "unknown cmd " + cmd);
}

int main(int argc, char **argv) {
  if (argc == 2 && std::strcmp(argv[1], "-h") == 0) {
    usage(stdout);
    return 0;
  }
  if (argc != 2 || argv[1][0] == '-' || !argv[1][0]) {
    usage(stderr);
    return 2;
  }
  const char *id_env = std::getenv("TG_API_ID");
  const char *hash_env = std::getenv("TG_API_HASH");
  if (!id_env || !hash_env || (api_id = std::atoi(id_env)) <= 0 || !*hash_env) {
    std::fprintf(stderr, "set TG_API_ID (a number) and TG_API_HASH (from my.telegram.org)\n");
    return 2;
  }
  api_hash = hash_env;

  // Signals are blocked in every thread (TDLib's inherit the mask) and TERM, INT and HUP are taken by one
  // sigwait thread, because QNX has no usable sigaltstack or SA_RESTART. SIGPIPE is ignored and blocked.
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGHUP);
  sigaddset(&set, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);
  signal(SIGPIPE, SIG_IGN);
  sigdelset(&set, SIGPIPE);

  umask(077);  // chats.db and everything else the engine makes: this user only
  make_dirs(data_dir = argv[1]);
  char real[PATH_MAX];
  if (!realpath(data_dir.c_str(), real)) {
    std::fprintf(stderr, "cannot make the data directory %s: %s\n", data_dir.c_str(), std::strerror(errno));
    return 1;
  }
  data_dir = real;
  say("tengramd, data in %s", data_dir.c_str());

  // One engine per data folder: a second one would replace engine.json and then fail on TDLib's own lock.
  // The lock goes with the process, so nothing is left behind after a crash.
  const int lock_fd = open((data_dir + "/engine.lock").c_str(), O_WRONLY | O_CREAT, 0600);
  struct flock lock = {};
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  if (lock_fd < 0 || fcntl(lock_fd, F_SETLK, &lock) < 0) {
    std::fprintf(stderr, "another tengramd is using %s\n", data_dir.c_str());
    return 3;
  }

  if (!db.open(data_dir) || !server.start(data_dir)) {
    return 1;
  }

  // A socketpair, not a pipe: pipes are not reliable with poll here. Both ends never block.
  int wake[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, wake) < 0 || !set_nonblock(wake[0]) || !set_nonblock(wake[1])) {
    say("socketpair: %s", std::strerror(errno));
    return 1;
  }
  tdlib.start(wake[1]);
  std::thread([set, fd = wake[1]] {
    for (int sig; sigwait(&set, &sig) == 0;) {
      ssize_t n = write(fd, "q", 1);
      (void)n;
    }
  }).detach();

  int exit_code = 0;
  bool stop_asked = false;
  Clock::time_point stop_by;
  std::vector<pollfd> fds;
  while (!done) {
    fds.clear();
    fds.push_back({wake[0], POLLIN, 0});
    server.fill(fds);
    int timeout = -1;  // nothing pending: sleep until something happens
    if (stop_asked) {
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(stop_by - Clock::now()).count();
      if (left <= 0) {
        say("TDLib did not close in 5 s");
        break;
      }
      timeout = static_cast<int>(left);
    }
    if (poll(fds.data(), fds.size(), timeout) < 0) {
      if (errno == EINTR) {
        continue;
      }
      say("poll: %s", std::strerror(errno));
      exit_code = 1;
      break;
    }
    if (fds[0].revents) {
      if (drain_wake(wake[0]) && !stop_asked) {
        say("stopping");
        stop_asked = true;
        stop_by = Clock::now() + std::chrono::seconds(5);
        tdlib.stopping = true;
        tdlib.send(td_api::make_object<td_api::close>());
      }
      tdlib.drain();
    }
    server.service(&fds[1]);
    db.commit();
  }

  // TDLib has flushed its databases (or the 5 s ran out). _Exit: the TDLib thread may still be in
  // receive(), and the destructors of the globals must not run beside it.
  db.commit();
  db.close();
  server.stop();
  say("stopped");
  std::_Exit(exit_code);
}
