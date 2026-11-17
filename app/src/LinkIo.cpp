#include "LinkIo.h"

#include <bb/data/JsonDataAccess>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <QByteArray>
#include <QDebug>
#include <QVariant>

namespace LinkIo {

bool sendAll(int fd, const char *data, int size)
{
    while (size > 0) {
        const ssize_t n = ::send(fd, data, size, 0);
        if (n <= 0)
            return false;
        data += n;
        size -= int(n);
    }
    return true;
}

bool recvAll(int fd, char *data, int size)
{
    while (size > 0) {
        const ssize_t n = ::recv(fd, data, size, 0);
        if (n <= 0)
            return false;
        data += n;
        size -= int(n);
    }
    return true;
}

bool connectLoopback(int fd, int port)
{
    sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return ::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0;
}

bool hello(int fd, const QString &token, const char *marker, const char *key, const QString &callId,
           const char *prefix)
{
    timeval timeout = { 3, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    QString escapedToken = token, escapedId = callId;
    escapedToken.replace('\\', "\\\\").replace('"', "\\\"");
    escapedId.replace('\\', "\\\\").replace('"', "\\\"");
    const QByteArray line = QString("{\"@\":\"%1\",\"cmd\":\"hello\",\"token\":\"%2\",\"proto\":1,\"%3\":\"%4\"}\n")
                                .arg(QString::fromLatin1(marker), escapedToken, QString::fromLatin1(key), escapedId)
                                .toUtf8();
    if (!sendAll(fd, line.constData(), line.size())) {
        qWarning("%s: hello not sent, errno %d", prefix, errno);
        return false;
    }
    QByteArray reply;
    char c;
    while (reply.size() < 4096 && ::recv(fd, &c, 1, 0) == 1 && c != '\n')
        reply += c;
    bb::data::JsonDataAccess json;
    const QVariant parsed = json.loadFromBuffer(reply);
    if (json.hasError() || parsed.toMap()["ok"].toBool() != true) {
        qWarning("%s: hello refused: %s", prefix, reply.constData());
        return false;
    }
    timeval none = { 0, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &none, sizeof none);
    return true;
}

}
