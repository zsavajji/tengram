#include "https.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace {

constexpr std::size_t kMaxBody = 1 << 20;

// ponytail: the phone's own roots. Google's chain ends at GlobalSign Root CA there, which expires 2028-01-28; after that, bundle Google Trust Services' roots with the app and load them here.
const char *const kCaDir = "/etc/openssl/certs";

// A connected socket with `timeout_s` on connect, read and write, or -1.
int dial(const std::string &host, int timeout_s) {
  addrinfo hints = {}, *list = nullptr;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), "443", &hints, &list) != 0) {
    return -1;
  }
  int fd = -1;
  for (const addrinfo *a = list; a && fd < 0; a = a->ai_next) {
    const int s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s < 0) {
      continue;
    }
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    bool ok = connect(s, a->ai_addr, a->ai_addrlen) == 0;
    if (!ok && errno == EINPROGRESS) {
      pollfd p = {s, POLLOUT, 0};
      int err = 0;
      socklen_t len = sizeof(err);
      ok = poll(&p, 1, timeout_s * 1000) == 1 && getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
    }
    if (!ok) {
      close(s);
      continue;
    }
    fcntl(s, F_SETFL, flags);
    timeval tv = {static_cast<time_t>(timeout_s), 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    fd = s;
  }
  freeaddrinfo(list);
  return fd;
}

// The Retry-After header of `head` in seconds, 0 when missing or not a number.
int retry_after_of(const std::string &head) {
  std::string low = head;
  std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::size_t at = low.find("\r\nretry-after:");
  return at == std::string::npos ? 0 : std::max(0, std::atoi(head.c_str() + at + 14));
}

}  // namespace

HttpsAnswer https_post(const std::string &host, const std::string &path, const std::vector<std::string> &headers,
                       const std::string &body, int timeout_s) {
  HttpsAnswer answer;
  const int fd = dial(host, timeout_s);
  if (fd < 0) {
    return answer;
  }
  SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
  SSL *ssl = nullptr;
  if (ctx) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_load_verify_locations(ctx, nullptr, kCaDir);
    ssl = SSL_new(ctx);
  }
  if (ssl && SSL_set_fd(ssl, fd) == 1 && SSL_set_tlsext_host_name(ssl, host.c_str()) == 1 &&
      SSL_set1_host(ssl, host.c_str()) == 1 && SSL_connect(ssl) == 1) {
    std::string request = "POST " + path + " HTTP/1.0\r\nHost: " + host +
                          "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
                          "\r\nConnection: close\r\n";
    for (const std::string &h : headers) {
      request += h + "\r\n";
    }
    request += "\r\n" + body;
    std::size_t sent = 0;
    while (sent < request.size()) {
      const int n = SSL_write(ssl, request.data() + sent, static_cast<int>(request.size() - sent));
      if (n <= 0) {
        break;
      }
      sent += static_cast<std::size_t>(n);
    }
    std::string raw;
    char buf[8192];
    while (sent == request.size() && raw.size() < kMaxBody + 16384) {
      const int n = SSL_read(ssl, buf, sizeof(buf));
      if (n <= 0) {  // the end, or an error: what arrived is judged below (a close without close_notify is common)
        break;
      }
      raw.append(buf, static_cast<std::size_t>(n));
    }
    const std::size_t split = raw.find("\r\n\r\n");
    const std::size_t sp = raw.find(' ');
    if (split != std::string::npos && raw.compare(0, 5, "HTTP/") == 0 && sp != std::string::npos && sp < split) {
      const int status = std::atoi(raw.c_str() + sp + 1);
      if (status >= 100 && status <= 599) {
        answer.status = status;
        answer.body = raw.substr(split + 4, kMaxBody);
        answer.retry_after = retry_after_of(raw.substr(0, split + 2));
      }
    }
  }
  SSL_free(ssl);
  SSL_CTX_free(ctx);
  close(fd);
  return answer;
}
