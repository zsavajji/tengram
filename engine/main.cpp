// tengramd: the engine of Tengram, a Telegram client for BlackBerry 10. The UI reads chats.db and talks to
// the engine over a loopback socket (docs/protocol.md). Usage: tengramd <data-dir>, with TG_API_ID and
// TG_API_HASH in the environment.
//
// This thread owns everything (the database, the sockets, all state) and sleeps in poll(). The TDLib thread
// (td.cpp) only receives and queues; a third thread waits for signals. The engine has no timer of its own:
// idle, it wakes only for TDLib or the UI. The exceptions are the 5 s limit of a stop and the flush, which
// comes 50 ms after something changed.

#include "engine.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <thread>

Db db;
Td tdlib;
Server server;
std::string data_dir;
std::string db_key;
int api_id = 0;
std::string api_hash;
bool done = false;
bool service = false;

namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point t0 = Clock::now();  // "process start": static init, before main

bool flush_pending = false;  // something is dirty: flush() is due at flush_at
Clock::time_point flush_at;

void usage(std::FILE *to) {
  std::fprintf(to, "usage: tengramd <data-dir> [--service]\n"
                   "  TG_API_ID and TG_API_HASH (from my.telegram.org) must be in the environment\n"
                   "  --service: the packaged app's headless service (BlackBerry Hub, navigator, network status)\n");
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

// The rows that changed, written in one transaction, and only then their events: what the UI is told to
// re-read is already in chats.db.
void flush() {
  flush_pending = false;
  db.begin();
  chats_write();
  messages_write();
  db.commit();
  chats_events();
  messages_events();
  messages_replies();
}

}  // namespace

void mark_dirty() {
  if (!flush_pending) {
    flush_pending = true;
    flush_at = Clock::now() + std::chrono::milliseconds(50);
  }
}

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

// How many updates of each type came, for the log line a stop writes: what wakes the engine is what costs
// battery. The name is worked out once per type (to_string of a whole update is not cheap).
std::map<std::int32_t, std::pair<std::string, long>> update_counts;

void log_update_counts() {
  std::vector<std::pair<long, std::string>> top;
  long total = 0;
  for (const auto &entry : update_counts) {
    top.push_back({entry.second.second, entry.second.first});
    total += entry.second.second;
  }
  std::sort(top.rbegin(), top.rend());
  std::string line;
  for (std::size_t i = 0; i < top.size() && i < 15; ++i) {
    line += " " + top[i].second + "=" + std::to_string(top[i].first);
  }
  say("updates: %ld in all;%s", total, line.c_str());
}

void handle_update(Obj update) {
  mode_note_update();
  auto &count = update_counts[update->get_id()];
  if (count.second++ == 0) {
    count.first = type_name(*update);
  }
  switch (update->get_id()) {
    case td_api::updateAuthorizationState::ID:
      link_auth(*static_cast<td_api::updateAuthorizationState &>(*update).authorization_state_);
      break;
    case td_api::updateConnectionState::ID:
      link_conn(*static_cast<td_api::updateConnectionState &>(*update).state_);
      break;
    default:  // the parts that mirror state take what is theirs and ignore the rest
      chats_update(*update);
      messages_update(*update);
      mode_update(*update);
      notifier_update(*update);
      break;
  }
}

void handle_request(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  mode_note_request();
  if (link_request(conn, id, cmd, req) || requests_handle(conn, id, cmd, req) ||
      mode_request(conn, id, cmd, req)) {
    return;
  }
  server.reply_error(conn, id, "unknown_cmd", "unknown cmd " + cmd);
}

int main(int argc, char **argv) {
  if (argc == 2 && std::strcmp(argv[1], "-h") == 0) {
    usage(stdout);
    return 0;
  }
  const char *dir = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--service") == 0) {
      service = true;
    } else if (argv[i][0] && argv[i][0] != '-' && !dir) {
      dir = argv[i];
    } else {
      usage(stderr);
      return 2;
    }
  }
  if (!dir) {
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
  make_dirs(data_dir = dir);
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

  bool fresh_key = false;
  if (!load_db_key(data_dir, fresh_key) || !db.open(data_dir, fresh_key) || !server.start(data_dir)) {
    return 1;
  }
  mode_init();
  notifier_init();

  // A socketpair, not a pipe: pipes are not reliable with poll here. Both ends never block.
  int wake[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, wake) < 0 || !set_nonblock(wake[0]) || !set_nonblock(wake[1])) {
    say("socketpair: %s", std::strerror(errno));
    return 1;
  }
  tdlib.start(wake[1]);
  if (service) {
    bb10_start(wake[1]);
  }
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
    // Sleep until something happens, or until the nearest of the stop, the flush and the delivery mode's timer.
    int timeout = -1;
    if (stop_asked && Clock::now() >= stop_by) {
      say("TDLib did not close in 5 s");
      break;
    }
    auto due = Clock::time_point::max();
    Clock::time_point mode_due;
    if (stop_asked) {
      due = std::min(due, stop_by);
    }
    if (flush_pending) {
      due = std::min(due, flush_at);
    }
    if (mode_deadline(mode_due)) {
      due = std::min(due, mode_due);
    }
    if (due != Clock::time_point::max()) {
      const auto us = std::chrono::duration_cast<std::chrono::microseconds>(due - Clock::now()).count();
      timeout = us <= 0 ? 0 : static_cast<int>((us + 999) / 1000);  // rounded up: no early wake-up to spin on
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
      if (service) {
        bb10_drain();
      }
    }
    server.service(&fds[1]);
    Clock::time_point mode_now_due;
    if (mode_deadline(mode_now_due) && Clock::now() >= mode_now_due) {
      mode_tick();
    }
    if (flush_pending && Clock::now() >= flush_at) {
      flush();
    }
  }

  // TDLib has flushed its databases (or the 5 s ran out). _Exit: the TDLib thread may still be in
  // receive(), and the destructors of the globals must not run beside it.
  if (flush_pending) {
    flush();
  }
  chats_close();
  messages_close();
  db.commit();
  db.close();
  server.stop();
  log_update_counts();
  say("stopped");
  std::_Exit(exit_code);
}
