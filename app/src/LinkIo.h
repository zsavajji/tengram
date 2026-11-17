#ifndef LINKIO_H
#define LINKIO_H

#include <QString>

// What the engine's two media links (audio, video) share: blocking socket helpers and the hello
// that opens a link on the engine's loopback port. Each owner makes its socket, keeps the
// descriptor where stop() can shut it down, and passes it here.
namespace LinkIo {

// All of the bytes, or false (the other side closed, an error, or a receive timeout).
bool sendAll(int fd, const char *data, int size);
bool recvAll(int fd, char *data, int size);

// Connects the socket to 127.0.0.1:port; errno says why when false.
bool connectLoopback(int fd, int port);

// Says hello: {"@":marker,"cmd":"hello","token":…,"proto":1,key:callId}, then reads the ok line
// (3 s at most), a byte at a time so nothing behind it is swallowed. False after logging why, with
// `prefix` ("call audio") in front of the line.
bool hello(int fd, const QString &token, const char *marker, const char *key, const QString &callId,
           const char *prefix);

}

#endif
