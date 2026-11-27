// One blocking HTTPS POST or GET, callable from any thread. OpenSSL and POSIX sockets only: no TDLib, no engine.h.
#pragma once

#include <string>
#include <vector>

struct HttpsAnswer {
  int status = 0;       // the HTTP status; 0: no answer (no network, no TLS, a timeout, a broken answer)
  std::string body;     // at most 1 MiB
  int retry_after = 0;  // the Retry-After header in seconds, 0 when missing or not a number
};

// POST `body` as JSON to https://host/path with the extra `headers` ("Name: value"). The certificate must verify
// against the phone's own roots; `timeout_s` bounds the connect and each read and write.
HttpsAnswer https_post(const std::string &host, const std::string &path, const std::vector<std::string> &headers,
                       const std::string &body, int timeout_s);
// GET https://host/path (the path with its query, already encoded) with the extra `headers`; the rest as https_post.
HttpsAnswer https_get(const std::string &host, const std::string &path, const std::vector<std::string> &headers,
                      int timeout_s);
