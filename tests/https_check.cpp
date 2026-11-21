// https_check [key]: one request to Google's Translation API through engine/https.cpp, to see the TLS and the answer
// on the phone. Prints the status, Retry-After and the start of the body; exit 0 when Google answered at all.
#include "../engine/https.h"

#include <csignal>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
  if (argc > 1 && std::strcmp(argv[1], "-h") == 0) {
    std::printf("usage: https_check [key]\nPOSTs a one-word translation to translation.googleapis.com with the key (default \"invalid\").\n");
    return 0;
  }
  signal(SIGPIPE, SIG_IGN);
  const std::string key = argc > 1 ? argv[1] : "invalid";
  const HttpsAnswer a = https_post("translation.googleapis.com", "/language/translate/v2", {"X-Goog-Api-Key: " + key},
                                   "{\"q\":[\"hello\"],\"target\":\"it\",\"format\":\"text\"}", 15);
  std::printf("status %d, retry_after %d\n%.500s\n", a.status, a.retry_after, a.body.c_str());
  return a.status != 0 ? 0 : 1;
}
