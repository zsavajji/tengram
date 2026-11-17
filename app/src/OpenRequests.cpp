#include "OpenRequests.h"

#include <QByteArray>
#include <QDebug>
#include <QRegExp>

using namespace bb::system;

// The invocation that started the app is delivered through the same signal, once the event
// loop runs: main() connects before exec().
OpenRequests::OpenRequests(InvokeManager *manager, QObject *parent)
    : QObject(parent)
{
    connect(manager, SIGNAL(invoked(const bb::system::InvokeRequest &)), this,
            SLOT(invoked(const bb::system::InvokeRequest &)));
}

void OpenRequests::invoked(const InvokeRequest &request)
{
    if (request.action() == "bb.action.SHARE") {
        qDebug() << "shared:" << request.mimeType() << request.uri() << request.data().left(200);
        // ponytail: one file; a multi-file share (filelist) isn't offered by the descriptor.
        const QString path = request.uri().scheme() == "file" ? request.uri().toLocalFile() : QString();
        const QString text = path.isEmpty() ? QString::fromUtf8(request.data()) : QString();
        if (!path.isEmpty() || !text.trimmed().isEmpty())
            emit shared(path, text);
        return;
    }
    if (request.action() != "bb.action.OPEN" && request.action() != "bb.action.VIEW")
        return;
    // Logged whole: where the Hub puts the item id is for the Q10 to show.
    qDebug() << "invoked:" << request.action() << request.mimeType() << request.uri()
             << request.data();
    // A chat's item, or a ringing call's (call_, step 7.2): both open the chat, where the call's
    // sheet shows the call if it still rings.
    QRegExp item("(?:chat|call)_([0-9a-f]+)");
    if (item.indexIn(request.uri().toString() + " " + QString::fromUtf8(request.data())) >= 0)
        emit openChat(QString::fromUtf8(QByteArray::fromHex(item.cap(1).toLatin1())));
}
