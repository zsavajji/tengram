// tdprobe: measures TDLib (TDLight fork) on a BB10 phone. Single-threaded loop on
// ClientManager::receive; RSS is measured from outside (pidin), CPU time is read here.
// See README.md. Usage: tdprobe <data-dir> [--once] [--no-history] [--background]
//                        [--no-files] [--qr] [--first-page] [--interval <s>] [--batch <s>]
// --batch <s> (implies --background): after the load the network is set to None, then every <s>
// seconds one sync is run (WiFi, wait for connectionStateReady + 3 s quiet, None) and timed.

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>

#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace td_api = td::td_api;

namespace {

using Clock = std::chrono::steady_clock;
using Obj = td_api::object_ptr<td_api::Object>;
using Handler = std::function<void(Obj)>;

const Clock::time_point t0 = Clock::now();  // "process start": static init, before main
constexpr std::uint64_t kWakeId = ~std::uint64_t{0};  // request id reserved for the signal thread

td::ClientManager *mgr = nullptr;
td::ClientManager::ClientId cid = 0;

// Options.
std::string data_dir;
int api_id = 0;
std::string api_hash;
bool once = false, no_history = false, background = false, no_files = false, qr = false, first_page = false;
int interval = 300;  // seconds between status lines; 0 = only on SIGUSR1
int batch = 0;       // seconds between syncs; 0 = off

// State.
std::map<std::uint64_t, Handler> handlers;
std::uint64_t next_id = 0;
std::int32_t auth_id = 0;
bool closing = false, closed = false, idle = false;
int exit_code = 0;
std::size_t total_updates = 0, chat_count = 0;
std::map<std::int32_t, std::size_t> since;  // update id -> count since the last status line
std::map<std::int32_t, std::string> names;  // update id -> type name, from the first object seen
std::atomic<bool> dump_flag{false};
long long last_cpu = -1;
Clock::time_point next_status;

long long ms_since(Clock::time_point t) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char *fmt, ...) {
  std::printf("[%6lld] ", ms_since(t0));
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
  std::putchar('\n');
}

// User+sys CPU of the whole process in ms, or -1 if neither clock exists.
long long cpu_ms() {
#ifdef CLOCK_PROCESS_CPUTIME_ID
  timespec ts;
  if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) {
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
  }
#endif
#ifdef RUSAGE_SELF
  rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) == 0) {
    return (ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000LL + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000;
  }
#endif
  return -1;
}

// First token of to_string(), i.e. the TL constructor name ("updateNewChat").
std::string type_name(const td_api::BaseObject &o) {
  std::string s = td_api::to_string(o);
  return s.substr(0, s.find_first_of(" \n"));
}

const td_api::error *as_error(const Obj &o) {
  return o->get_id() == td_api::error::ID ? static_cast<const td_api::error *>(o.get()) : nullptr;
}

void send(td_api::object_ptr<td_api::Function> f, Handler h = nullptr) {
  ++next_id;
  if (h) {
    handlers[next_id] = std::move(h);
  }
  mgr->send(cid, next_id, std::move(f));
}

void finish(int code) {
  exit_code = code;
  if (!closing) {
    closing = true;
    send(td_api::make_object<td_api::close>());  // flush the databases; loop ends on Closed
  }
}

void fatal(Obj o) {
  if (auto e = as_error(o)) {
    say("fatal: %d %s", e->code_, e->message_.c_str());
    finish(1);
  }
}

void set_bool_option(const char *name, bool value) {
  send(td_api::make_object<td_api::setOption>(name, td_api::make_object<td_api::optionValueBoolean>(value)));
}

// One status line, one update-count line, then (with_mem) the memory statistics as they arrive.
void dump(bool with_mem) {
  long long cpu = cpu_ms();
  say("status: up=%llds updates=%zu chats=%zu cpu=%lldms (+%lldms)", ms_since(t0) / 1000, total_updates, chat_count,
      cpu, cpu >= 0 && last_cpu >= 0 ? cpu - last_cpu : -1);
  last_cpu = cpu;

  std::string line = "upd:";
  for (auto &p : since) {
    line += " " + names[p.first] + "=" + std::to_string(p.second);
  }
  say("%s", since.empty() ? "upd: none" : line.c_str());
  since.clear();
  if (!with_mem) {
    return;
  }

  send(td_api::make_object<td_api::getMemoryStatistics>(true), [](Obj o) {
    if (auto e = as_error(o)) {
      say("mem: error %d %s", e->code_, e->message_.c_str());
    } else {
      const std::string &s = static_cast<td_api::memoryStatistics &>(*o).statistics_;
      for (std::size_t b = 0; b <= s.size();) {
        std::size_t e = std::min(s.find('\n', b), s.size());
        say("mem: %.*s", static_cast<int>(e - b), s.c_str() + b);
        b = e + 1;
      }
    }
    if (once) {
      finish(0);
    }
  });
}

// --batch: the network is None between syncs. A sync is WiFi on, wait for connectionStateReady, then
// until no update has arrived for kQuiet, then None again; kConnectTimeout bounds the wait for Ready.
const auto kQuiet = std::chrono::seconds(3);
const auto kConnectTimeout = std::chrono::seconds(60);
bool syncing = false, sync_ready = false;
Clock::time_point next_sync, sync_start, last_update;
long long sync_cpu0 = -1, sync_connect_ms = 0;
std::size_t sync_updates = 0;

void set_network(bool wifi) {
  td_api::object_ptr<td_api::NetworkType> t;
  if (wifi) {
    t = td_api::make_object<td_api::networkTypeWiFi>();
  } else {
    t = td_api::make_object<td_api::networkTypeNone>();
  }
  send(td_api::make_object<td_api::setNetworkType>(std::move(t)));
}

void sync_begin() {
  syncing = true;
  sync_ready = false;
  sync_connect_ms = 0;
  sync_updates = 0;
  sync_start = last_update = Clock::now();
  sync_cpu0 = cpu_ms();
  set_network(true);
}

void sync_end(bool ok) {
  const long long cpu = cpu_ms();  // before None, so the disconnect is not in the sync's cost
  const auto due = sync_start + std::chrono::seconds(batch);
  syncing = false;
  next_sync = std::max(due, Clock::now());
  set_network(false);
  if (ok) {
    say("sync: connect=%lld total=%lld cpu=%+lld updates=%zu", sync_connect_ms, ms_since(sync_start),
        cpu >= 0 && sync_cpu0 >= 0 ? cpu - sync_cpu0 : -1LL, sync_updates);
  } else {
    say("sync: timeout");
  }
}

// When the running sync ends: Ready + kQuiet without updates, never later than kConnectTimeout after
// its start (a busy account sends updates without a 3 s gap, and the sync must still end).
Clock::time_point sync_end_at() {
  const auto cap = sync_start + kConnectTimeout;
  return sync_ready ? std::min(last_update + kQuiet, cap) : cap;
}

// Next time sync_step() has something to do, or `limit` if nothing is pending.
Clock::time_point sync_deadline(Clock::time_point limit) {
  if (!batch || !idle) {
    return limit;
  }
  return std::min(limit, !syncing ? next_sync : sync_end_at());
}

void sync_step() {
  if (!batch || !idle) {
    return;
  }
  const auto now = Clock::now();
  if (!syncing) {
    if (now >= next_sync) {
      sync_begin();
    }
  } else if (now >= sync_end_at()) {
    sync_end(sync_ready);
  }
}

// End of the post-Ready load. From here on the probe only receives.
void load_done() {
  if (background) {
    set_bool_option("online", false);  // like a backgrounded messenger
  }
  idle = true;
  next_status = Clock::now() + std::chrono::seconds(interval);
  if (once) {
    return dump(true);
  }
  if (batch) {
    set_network(false);
    next_sync = Clock::now() + std::chrono::seconds(batch);
  }
}

// Chat history for the first 20 chats of the main list, paged until ~50 messages each:
// TDLib may answer a first page with fewer than `limit`.
std::vector<std::int64_t> hist_ids;
std::size_t hist_idx = 0, hist_msgs = 0;
Clock::time_point hist_start;

void history_step(std::int64_t from, int got, int rounds) {
  if (hist_idx >= hist_ids.size()) {
    say("history: %zu chats, %zu messages, %lld ms", hist_ids.size(), hist_msgs, ms_since(hist_start));
    return load_done();
  }
  const std::size_t dup = from ? 1 : 0;  // offset 0 includes from_message_id itself
  send(td_api::make_object<td_api::getChatHistory>(hist_ids[hist_idx], from, 0, 50 - got + static_cast<int>(dup), false),
       [=](Obj o) {
         std::size_t fresh = 0;
         std::int64_t oldest = 0;
         if (auto e = as_error(o)) {
           say("getChatHistory error %d %s", e->code_, e->message_.c_str());
         } else {
           auto &v = static_cast<td_api::messages &>(*o).messages_;
           fresh = v.size() > dup ? v.size() - dup : 0;
           oldest = v.empty() ? 0 : v.back()->id_;
         }
         hist_msgs += fresh;
         if (fresh && got + static_cast<int>(fresh) < 50 && rounds < 5) {
           return history_step(oldest, got + static_cast<int>(fresh), rounds + 1);
         }
         ++hist_idx;
         history_step(0, 0, 0);
       });
}

void load_history() {
  send(td_api::make_object<td_api::getChats>(td_api::make_object<td_api::chatListMain>(), 20), [](Obj o) {
    if (auto e = as_error(o)) {
      say("getChats error %d %s", e->code_, e->message_.c_str());
      return load_done();
    }
    hist_ids = static_cast<td_api::chats &>(*o).chat_ids_;
    hist_start = Clock::now();
    history_step(0, 0, 0);
  });
}

int load_calls = 0;
Clock::time_point load_start;

void load_chats() {
  if (first_page) {
    // What a real engine does at start (meegram2's demand-loaded list): one page, nothing else.
    send(td_api::make_object<td_api::loadChats>(td_api::make_object<td_api::chatListMain>(), 20), [](Obj o) {
      if (auto e = as_error(o)) {
        say("loadChats error %d %s", e->code_, e->message_.c_str());
      }
      say("first page: %zu chats known, %lld ms after Ready, %lld ms after start, cpu=%lldms", chat_count,
          ms_since(load_start), ms_since(t0), cpu_ms());
      load_done();
    });
    return;
  }
  send(td_api::make_object<td_api::loadChats>(td_api::make_object<td_api::chatListMain>(), 200), [](Obj o) {
    ++load_calls;
    if (auto e = as_error(o)) {
      if (e->code_ != 404) {  // 404 = whole list loaded
        say("loadChats error %d %s", e->code_, e->message_.c_str());
      }
      say("chats: %zu loaded, %d loadChats calls, %lld ms", chat_count, load_calls, ms_since(load_start));
      return no_history ? load_done() : load_history();
    }
    load_chats();
  });
}

void on_ready() {
  if (no_files) {
    set_bool_option("disable_auto_download", true);  // TDLight, local only (not setAutoDownloadSettings)
  }
  set_bool_option("online", true);  // foreground behaviour; --background flips it after the load
  load_start = Clock::now();
  load_chats();
}

bool ask(const char *what, std::string &out) {
  say("%s", what);
  if (std::getline(std::cin, out)) {
    return true;
  }
  say("stdin closed");
  finish(1);
  return false;
}

void prompt_auth() {
  auto retry = [](Obj o) {
    if (auto e = as_error(o)) {
      say("auth error %d %s", e->code_, e->message_.c_str());
      prompt_auth();  // TDLib sends no new state after a rejected answer
    }
  };
  std::string in;
  switch (auth_id) {
    case td_api::authorizationStateWaitPhoneNumber::ID:
      if (qr) {
        // No code message: TDLib answers with authorizationStateWaitOtherDeviceConfirmation and a
        // link to show as a QR code, renewed about every 30 s until an official app scans it.
        send(td_api::make_object<td_api::requestQrCodeAuthentication>(std::vector<std::int64_t>()), fatal);
      } else if (ask("phone number (international format)?", in)) {
        send(td_api::make_object<td_api::setAuthenticationPhoneNumber>(in, nullptr), retry);
      }
      break;
    case td_api::authorizationStateWaitCode::ID:
      if (!ask("login code? (r = ask Telegram to send it another way)", in)) {
      } else if (in == "r") {
        // On success TDLib sends a new authorizationStateWaitCode, which prompts again.
        send(td_api::make_object<td_api::resendAuthenticationCode>(
                 td_api::make_object<td_api::resendCodeReasonUserRequest>()),
             retry);
      } else {
        send(td_api::make_object<td_api::checkAuthenticationCode>(in), retry);
      }
      break;
    case td_api::authorizationStateWaitPassword::ID:
      if (ask("2FA password?", in)) {
        send(td_api::make_object<td_api::checkAuthenticationPassword>(in), retry);
      }
      break;
  }
}

void on_auth(const td_api::AuthorizationState &s) {
  auth_id = s.get_id();
  say("auth: %s", type_name(s).c_str());
  switch (auth_id) {
    case td_api::authorizationStateWaitTdlibParameters::ID: {
      auto p = td_api::make_object<td_api::setTdlibParameters>();
      p->database_directory_ = data_dir + "/db";
      p->files_directory_ = data_dir + "/files";
      p->use_file_database_ = true;
      p->use_chat_info_database_ = true;
      p->use_message_database_ = true;
      p->use_secret_chats_ = false;
      p->api_id_ = api_id;
      p->api_hash_ = api_hash;
      p->system_language_code_ = "en";
      p->device_model_ = "BlackBerry Q10";
      p->system_version_ = "BlackBerry 10.3.3";
      p->application_version_ = "tdprobe 0.1";
      send(std::move(p), fatal);
      break;
    }
    case td_api::authorizationStateWaitCode::ID: {
      // How Telegram sent the code (in-app message, SMS, call...), what "r" would try next, and when.
      auto &info = *static_cast<const td_api::authorizationStateWaitCode &>(s).code_info_;
      say("code sent via %s; next: %s; resend allowed after %d s", type_name(*info.type_).c_str(),
          info.next_type_ ? type_name(*info.next_type_).c_str() : "nothing", info.timeout_);
      if (qr) {  // a saved session resumes here, not at WaitPhoneNumber: switch it to QR login
        send(td_api::make_object<td_api::requestQrCodeAuthentication>(std::vector<std::int64_t>()), fatal);
        break;
      }
      prompt_auth();
      break;
    }
    case td_api::authorizationStateWaitPhoneNumber::ID:
    case td_api::authorizationStateWaitPassword::ID:
      prompt_auth();
      break;
    case td_api::authorizationStateWaitOtherDeviceConfirmation::ID:
      say("confirm this login on another device: %s",
          static_cast<const td_api::authorizationStateWaitOtherDeviceConfirmation &>(s).link_.c_str());
      break;
    case td_api::authorizationStateWaitRegistration::ID:
      say("register in another client");
      finish(1);
      break;
    case td_api::authorizationStateReady::ID:
      on_ready();
      break;
    case td_api::authorizationStateLoggingOut::ID:
    case td_api::authorizationStateClosing::ID:
      break;
    case td_api::authorizationStateClosed::ID:
      closed = true;
      break;
    default:
      say("unsupported authorization state");
      finish(1);
  }
}

void on_update(Obj obj) {
  ++total_updates;
  const std::int32_t id = obj->get_id();
  if (since[id]++ == 0 && names[id].empty()) {
    names[id] = type_name(*obj);
  }
  if (syncing) {
    ++sync_updates;
    last_update = Clock::now();
  }
  switch (id) {
    case td_api::updateAuthorizationState::ID:
      on_auth(*static_cast<td_api::updateAuthorizationState &>(*obj).authorization_state_);
      break;
    case td_api::updateConnectionState::ID: {
      auto &state = *static_cast<td_api::updateConnectionState &>(*obj).state_;
      say("conn: %s", type_name(state).c_str());
      if (syncing && !sync_ready && state.get_id() == td_api::connectionStateReady::ID) {
        sync_ready = true;
        sync_connect_ms = ms_since(sync_start);
      }
      break;
    }
    case td_api::updateNewChat::ID:
      ++chat_count;
      break;
  }
}

void on_response(std::uint64_t id, Obj obj) {
  if (id == kWakeId) {
    return;
  }
  auto it = handlers.find(id);
  if (it == handlers.end()) {
    if (auto e = as_error(obj)) {
      say("error (request %llu): %d %s", static_cast<unsigned long long>(id), e->code_, e->message_.c_str());
    }
    return;
  }
  Handler h = std::move(it->second);
  handlers.erase(it);
  h(std::move(obj));
}

}  // namespace

int main(int argc, char **argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--once") {
      once = true;
    } else if (a == "--no-history") {
      no_history = true;
    } else if (a == "--background") {
      background = true;
    } else if (a == "--first-page") {
      first_page = true;
    } else if (a == "--qr") {
      qr = true;
    } else if (a == "--no-files") {
      no_files = true;
    } else if (a == "--interval" && i + 1 < argc && (interval = std::atoi(argv[++i])) >= 0) {
    } else if (a == "--batch" && i + 1 < argc && (batch = std::atoi(argv[++i])) > 0) {
      background = true;
    } else if (a[0] != '-' && data_dir.empty()) {
      data_dir = a;
    } else {
      data_dir.clear();
      break;
    }
  }
  if (data_dir.empty()) {
    std::fprintf(stderr,
                 "usage: %s <data-dir> [--once] [--no-history] [--background] [--no-files] [--qr] [--first-page] [--interval <s>] "
                 "[--batch <s>]\n",
                 argv[0]);
    return 2;
  }
  const char *id_env = std::getenv("TG_API_ID");
  const char *hash_env = std::getenv("TG_API_HASH");
  if (!id_env || !hash_env || (api_id = std::atoi(id_env)) <= 0 || !*hash_env) {
    std::fprintf(stderr, "set TG_API_ID (number) and TG_API_HASH (from my.telegram.org)\n");
    return 2;
  }
  api_hash = hash_env;

  // SIGUSR1 is blocked in every thread (TDLib's inherit the mask) and taken by one sigwait thread.
  // ClientManager::receive retries poll() on EINTR, so a handler would not wake the main loop.
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);

  td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
  td::ClientManager manager;
  mgr = &manager;
  cid = manager.create_client_id();
  send(td_api::make_object<td_api::getOption>("version"));  // activates the client

  std::thread([set] {
    for (int sig; sigwait(&set, &sig) == 0;) {
      dump_flag = true;
      mgr->send(cid, kWakeId, td_api::make_object<td_api::getOption>("version"));  // wakes receive()
    }
  }).detach();

  while (!closed) {
    // Block until the next deadline: status line, next sync, or the running sync's quiet/connect limit.
    auto deadline = Clock::now() + std::chrono::hours(1);
    if (idle && interval > 0) {
      deadline = std::min(deadline, next_status);
    }
    deadline = sync_deadline(deadline);
    auto r = manager.receive(std::max(0.0, std::chrono::duration<double>(deadline - Clock::now()).count()));
    if (r.object) {
      if (r.request_id) {
        on_response(r.request_id, std::move(r.object));
      } else {
        on_update(std::move(r.object));
      }
    }
    sync_step();
    bool timer = idle && interval > 0 && Clock::now() >= next_status;
    if (timer) {
      next_status = Clock::now() + std::chrono::seconds(interval);
    }
    // SIGUSR1 always asks for memory; the timed status line does not in batch mode, to leave the sync alone.
    bool want_mem = dump_flag.exchange(false);
    if (want_mem || timer) {
      dump(want_mem || !batch);
    }
  }
  // Closed: databases are flushed. _Exit leaves the detached sigwait thread nothing to race with.
  std::fflush(stdout);
  std::_Exit(exit_code);
}
