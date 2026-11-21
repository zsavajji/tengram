// msg.translate: the text of messages through Telegram's own translation (translateMessageText), as HyperBerry's
// handleTranslate does for its services (docs/protocol.md, "Translation"). The answer is kept in messages.tr, so the
// next ask for the same language costs nothing; chats.cpp's and messages.cpp's inserts keep it when a row is written
// again with the same text. With a Google API key (the googleKey preference) the text goes to Google's Cloud Translation
// v2 instead, in batches, from one thread per request (https.cpp); the answers are handled back on the main thread.

#include "engine.h"
#include "https.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unistd.h>

#include <utility>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kAtOnce = 4;   // requests in flight for one msg.translate
constexpr std::size_t kMaxIds = 200;  // ids in one msg.translate
constexpr int kPauseMin = 60;         // seconds
constexpr int kPauseMax = 15 * 60;

constexpr std::size_t kGoogleTexts = 50;     // texts in one request to Google
constexpr std::size_t kGoogleBytes = 10000;  // bytes of text in one request (a longer text is a request of its own)
constexpr int kGoogleSecs = 15;

enum Service { kTelegram, kGoogle };

// After a service refuses for too many: nothing goes to it until `until`. `wait` is the last pause in seconds, 0 after a
// success.
struct Pause {
  Clock::time_point until;
  int wait = 0;
} pauses[2];

bool paused(Service which) {
  return Clock::now() < pauses[which].until;
}

// `told`: the seconds the service asked for, 0 when it said nothing.
void pause_for(Service which, int told) {
  Pause &p = pauses[which];
  if (paused(which)) {  // four in flight are one refusal
    return;
  }
  p.wait = told > 0 ? told : std::max(std::min(p.wait * 2, kPauseMax), kPauseMin);
  p.until = Clock::now() + std::chrono::seconds(p.wait);
  say("translate: %s paused for %d s", which == kGoogle ? "Google" : "Telegram", p.wait);
}

int retry_told(const std::string &message) {
  const std::size_t at = message.find("retry after ");
  return at == std::string::npos ? 0 : std::atoi(message.c_str() + at + 12);
}

// From any thread: runs on the main thread, in translate_drain(), as calls.cpp's.
int wake_fd = -1;
std::mutex mu;
std::vector<std::function<void()>> queue;  // guarded by mu

void post(std::function<void()> f) {
  bool was_empty;
  {
    std::lock_guard<std::mutex> lock(mu);
    was_empty = queue.empty();
    queue.push_back(std::move(f));
  }
  if (was_empty) {
    ssize_t n = write(wake_fd, "t", 1);
    (void)n;
  }
}

struct Item {
  std::int64_t id;
  std::string text;  // the row's text
  std::string out;   // what the item says: the translation, '' for nothing to show
  bool ready;        // out is final
};

struct Job {
  std::uint64_t conn;
  std::string id, to;
  std::int64_t chat;
  bool google = false;
  std::vector<Item> rows;
  std::size_t next = 0, in_flight = 0;
  bool failed = false, finished = false;
  std::string code, message;
};

// A text with no letter and no byte of a character (digits, punctuation) has nothing to translate.
bool translatable(const std::string &s) {
  return std::any_of(s.begin(), s.end(), [](char c) {
    return static_cast<unsigned char>(c) >= 0x80 || std::isalpha(static_cast<unsigned char>(c));
  });
}

void pump(const std::shared_ptr<Job> &job);

void answered(const std::shared_ptr<Job> &job, std::size_t at, Obj o) {
  --job->in_flight;
  if (const td_api::error *e = as_error(o)) {
    say("translate: td error %d %s", e->code_, e->message_.c_str());
    const bool limited =
        e->code_ == 429 || e->message_.find("FLOOD") != std::string::npos || e->message_.find("QUOTA") != std::string::npos;
    if (limited) {
      pause_for(kTelegram, retry_told(e->message_));
    }
    if (!job->failed) {  // the first failure is the answer; the requests in flight still finish
      job->failed = true;
      job->message = e->message_;
      job->code = limited ? "rate_limited"
                          : e->message_.find("LANG") != std::string::npos ? "unsupported" : "translate_failed";
    }
  } else {
    Item &item = job->rows[at];
    item.out = static_cast<td_api::formattedText &>(*o).text_;
    if (item.out == item.text) {
      item.out.clear();
    }
    item.ready = true;
    pauses[kTelegram] = Pause();
    messages_translated(job->chat, item.id, item.text, item.out, job->to);
  }
  pump(job);
}

// The member `name` of an object, taken out of it; null when `v` is not an object or has none.
td::JsonValue member(td::JsonValue &v, const char *name) {
  return v.type() == td::JsonValue::Type::Object ? v.get_object().extract_field(td::Slice(name)) : td::JsonValue();
}

std::string base_of(const std::string &lang) {  // "pt-BR" or "pt_BR": the base
  std::string base = lang.substr(0, lang.find_first_of("-_"));
  std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return base;
}

// What a failed answer of Google's means, as HyperBerry's googleAPIFail has it: a limit is rate_limited, a bad key or
// any other 403 (the API is not enabled for the key) no_key, any other 400 unsupported.
std::string google_code(int status, std::string body) {
  bool limited = status == 429, bad_key = false;
  auto is_limit = [](const std::string &s) {
    for (const char *name : {"rateLimitExceeded", "userRateLimitExceeded", "dailyLimitExceeded", "quotaExceeded",
                             "RESOURCE_EXHAUSTED", "RATE_LIMIT_EXCEEDED"}) {
      if (s == name) {
        return true;
      }
    }
    return false;
  };
  auto parsed = td::json_decode(td::MutableSlice(body));
  if (parsed.is_ok()) {
    td::JsonValue error = member(parsed.ok_ref(), "error"), state = member(error, "status");
    limited = limited || (state.type() == td::JsonValue::Type::String && is_limit(state.get_string().str()));
    for (const char *list : {"errors", "details"}) {
      td::JsonValue items = member(error, list);
      if (items.type() != td::JsonValue::Type::Array) {
        continue;
      }
      for (td::JsonValue &item : items.get_array()) {
        td::JsonValue reason = member(item, "reason");
        if (reason.type() != td::JsonValue::Type::String) {
          continue;
        }
        const std::string r = reason.get_string().str();
        limited = limited || is_limit(r);
        bad_key = bad_key || (r == "API_KEY_INVALID" && std::string(list) == "details");
      }
    }
  }
  return limited ? "rate_limited" : bad_key || status == 403 ? "no_key" : status == 400 ? "unsupported" : "translate_failed";
}

void pump(const std::shared_ptr<Job> &job);

// Google's answer for the rows `at` of the job (main thread).
void google_answered(const std::shared_ptr<Job> &job, const std::vector<std::size_t> &at, const HttpsAnswer &a) {
  --job->in_flight;
  std::vector<std::string> out;
  std::vector<std::string> from;
  bool good = false;
  std::string body = a.body;  // the parsed values point into it
  if (a.status == 200) {
    auto parsed = td::json_decode(td::MutableSlice(body));
    if (parsed.is_ok()) {
      td::JsonValue data = member(parsed.ok_ref(), "data"), list = member(data, "translations");
      if (list.type() != td::JsonValue::Type::Array) {
        list = member(parsed.ok_ref(), "translations");
      }
      if (list.type() == td::JsonValue::Type::Array && list.get_array().size() >= at.size()) {
        good = true;
        for (std::size_t i = 0; i < at.size() && good; ++i) {
          td::JsonValue text = member(list.get_array()[i], "translatedText");
          td::JsonValue source = member(list.get_array()[i], "detectedSourceLanguage");
          good = text.type() == td::JsonValue::Type::String;
          out.push_back(good ? text.get_string().str() : std::string());
          from.push_back(source.type() == td::JsonValue::Type::String ? source.get_string().str() : std::string());
        }
      }
    }
    if (!good) {
      say("translate: Google HTTP 200, not a translation");
      job->failed = true;
      job->code = "translate_failed";
      job->message = "translate: the answer is not what Google sends";
    }
  } else {
    say("translate: Google HTTP %d", a.status);
    job->failed = true;
    if (a.status == 0) {
      job->code = "offline";
      job->message = "translate: no answer from Google";
    } else {
      job->code = google_code(a.status, a.body);
      job->message = "translate: HTTP " + std::to_string(a.status);
      if (job->code == "rate_limited") {
        pause_for(kGoogle, a.retry_after);
      }
    }
  }
  if (good) {
    const std::string to_base = base_of(job->to);
    for (std::size_t i = 0; i < at.size(); ++i) {
      Item &item = job->rows[at[i]];
      item.out = out[i];
      if (item.out == item.text || (!from[i].empty() && base_of(from[i]) == to_base)) {
        item.out.clear();
      }
      item.ready = true;
      messages_translated(job->chat, item.id, item.text, item.out, job->to);
    }
    pauses[kGoogle] = Pause();
  }
  pump(job);
}

// Starts the next request to Google: the next rows that need one, up to kGoogleTexts of them and kGoogleBytes of text.
void google_next(const std::shared_ptr<Job> &job) {
  std::vector<std::size_t> at;
  std::size_t bytes = 0;
  while (job->next < job->rows.size() && at.size() < kGoogleTexts) {
    Item &item = job->rows[job->next];
    if (item.ready) {  // cached
      ++job->next;
    } else if (!translatable(item.text)) {
      ++job->next;
      item.ready = true;
      messages_translated(job->chat, item.id, item.text, std::string(), job->to);
    } else if (at.empty() || bytes + item.text.size() <= kGoogleBytes) {
      bytes += item.text.size();
      at.push_back(job->next++);
    } else {
      break;
    }
  }
  if (at.empty()) {
    return;
  }
  const std::string body = td::json_encode<std::string>(td::json_object([&](auto &r) {
    r("q", td::json_array([&](auto &texts) {
      for (std::size_t i : at) {
        texts(job->rows[i].text);
      }
    }));
    r("target", job->to);
    r("format", std::string("text"));
  }));
  ++job->in_flight;
  // The thread touches only its own copies and post(); the job is parsed and used on the main thread.
  std::thread([job, at, body, key = mode_google_key()]() mutable {
    HttpsAnswer a = https_post("translation.googleapis.com", "/language/translate/v2", {"X-Goog-Api-Key: " + key}, body,
                               kGoogleSecs);
    post([job = std::move(job), at = std::move(at), a = std::move(a)] { google_answered(job, at, a); });
  }).detach();
}

void pump(const std::shared_ptr<Job> &job) {
  if (job->google) {
    if (!job->failed && job->in_flight == 0) {
      google_next(job);
    }
  }
  while (!job->google && !job->failed && job->in_flight < kAtOnce && job->next < job->rows.size()) {
    const std::size_t at = job->next++;
    Item &item = job->rows[at];
    if (item.ready) {  // cached
      continue;
    }
    if (!translatable(item.text)) {
      item.ready = true;
      messages_translated(job->chat, item.id, item.text, std::string(), job->to);
      continue;
    }
    ++job->in_flight;
    tdlib.send(td_api::make_object<td_api::translateMessageText>(job->chat, item.id, job->to, ""),
               [job, at](Obj o) { answered(job, at, std::move(o)); });
  }
  if (job->finished || job->in_flight > 0 || !(job->failed || job->next >= job->rows.size())) {
    return;
  }
  job->finished = true;
  if (job->failed) {
    return server.reply_error(job->conn, job->id, job->code, job->message);
  }
  queue_reply(job->conn, job->id, td::json_encode<std::string>(td::json_object([&](auto &r) {
                r("items", td::json_array([&](auto &items) {
                  for (const Item &item : job->rows) {
                    items(td::json_object([&](auto &one) {
                      one("id", std::to_string(item.id));
                      one("text", item.out);
                    }));
                  }
                }));
              })));
}

}  // namespace

bool language_code(const std::string &s) {
  return s.size() >= 2 && s.size() <= 16 && std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
         });
}

bool translate_handle(std::uint64_t conn, const std::string &id, const std::string &cmd, td::JsonObject &req) {
  if (cmd != "msg.translate") {
    return false;
  }
  std::int64_t chat = 0;
  if (!request_chat(conn, id, req, chat)) {
    return true;
  }
  const char *const need = "msg.translate needs chat, 1 to 200 ids and to";
  auto job = std::make_shared<Job>();
  job->conn = conn;
  job->id = id;
  job->chat = chat;
  std::vector<std::int64_t> ids;
  td::JsonValue list, to;
  if (optional_field(req, "ids", td::JsonValue::Type::Array, list) != 1 || list.get_array().empty() ||
      list.get_array().size() > kMaxIds) {
    bad_request(conn, id, need);
    return true;
  }
  for (const td::JsonValue &item : list.get_array()) {
    std::int64_t one = 0;
    if (item.type() != td::JsonValue::Type::String || !parse_i64(item.get_string().str(), one)) {
      bad_request(conn, id, need);
      return true;
    }
    ids.push_back(one);
  }
  const int has_to = optional_field(req, "to", td::JsonValue::Type::String, to);
  if (has_to < 0 || (has_to == 1 && !language_code(to.get_string().str()))) {
    bad_request(conn, id, need);
    return true;
  }
  if (has_to == 1) {
    job->to = to.get_string().str();
  } else {
    job->to = base_of(mode_language());
    if (job->to.empty()) {
      job->to = "en";
    }
  }
  ChatBrief brief = {};
  if (chat_brief(chat, brief) && brief.secret != 0) {
    server.reply_error(conn, id, "translate_failed", "not in a secret chat");
    return true;
  }
  job->google = !mode_google_key().empty();
  if (paused(job->google ? kGoogle : kTelegram)) {
    server.reply_error(conn, id, "rate_limited", "translation is paused for a while");
    return true;
  }

  auto s = db.prepare("SELECT kind, text, deleted, tr, tr_lang FROM messages WHERE chat = ?1 AND id = ?2");
  if (s.empty()) {
    server.reply_error(conn, id, "translate_failed", "chats.db");
    return true;
  }
  const std::string chat_text = std::to_string(chat);  // bound by reference: they outlive the step
  for (std::int64_t one : ids) {
    const std::string id_text = std::to_string(one);
    s.reset();
    td::Status status = s.bind_string(1, chat_text);
    if (status.is_ok()) {
      status = s.bind_string(2, id_text);
    }
    if (status.is_ok()) {
      status = s.step();
    }
    using Type = td::SqliteStatement::Datatype;
    if (status.is_error() || !s.has_row() || s.view_int32(2) != 0 || s.view_datatype(1) != Type::Text ||
        s.view_int32(0) < 0 || s.view_int32(0) > 2) {
      continue;
    }
    Item item = {one, s.view_string(1).str(), "", false};
    if (item.text.empty()) {
      continue;
    }
    if (s.view_datatype(3) == Type::Text && s.view_datatype(4) == Type::Text && s.view_string(4).str() == job->to) {
      item.out = s.view_string(3).str();
      item.ready = true;
    }
    job->rows.push_back(std::move(item));
  }
  s.reset();
  if (job->rows.empty() && ids.size() == 1) {
    server.reply_error(conn, id, "not_found", "no such text");
    return true;
  }
  pump(job);
  return true;
}

void translate_start(int wake) {
  wake_fd = wake;
}

void translate_drain() {
  std::vector<std::function<void()>> jobs;
  {
    std::lock_guard<std::mutex> lock(mu);
    jobs.swap(queue);
  }
  for (auto &job : jobs) {
    job();
  }
}
