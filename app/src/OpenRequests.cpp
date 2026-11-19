#include "OpenRequests.h"

#include <bb/data/JsonDataAccess>
#include <QDebug>
#include <QRegExp>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

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
        // The system's multi-file share (the user's ask, 2026-11-18): the mime type is filelist/..., the data a JSON array
        // of objects with a uri (file:///...), as the editor's answer is (MediaOpener). Untested on the phone: the format
        // is as the editor's, not seen from a Share menu yet.
        if (request.mimeType().startsWith("filelist/")) {
            bb::data::JsonDataAccess json;
            const QVariantList list = json.loadFromBuffer(request.data()).toList();
            QStringList paths;
            foreach (const QVariant &item, list) {
                const QString uri = item.toMap().value("uri").toString();
                if (uri.startsWith("file://"))
                    paths << QUrl::fromPercentEncoding(uri.mid(7).toUtf8());
            }
            if (paths.size() > 1)
                emit sharedFiles(paths);
            else if (paths.size() == 1)
                emit shared(paths.first(), QString());
            return;
        }
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
    // A chat's item: the engine writes chat_ and the chat's key, TDLib's number in decimal, with n for the
    // minus sign (a Hub item id takes only letters, digits and _): chat_n1001234567890 is "-1001234567890".
    // A ringing call's item (call_chat_...) opens nothing: the call screen comes by itself, and the chat page built under
    // it cost 0.6 s before it (Q10, 2026-11-19).
    if ((request.uri().toString() + " " + QString::fromUtf8(request.data())).contains("call_chat_"))
        return;
    QRegExp item("chat_(n?[0-9]+)");
    if (item.indexIn(request.uri().toString() + " " + QString::fromUtf8(request.data())) >= 0) {
        QString chat = item.cap(1);
        if (chat.startsWith(QLatin1Char('n')))
            chat[0] = QLatin1Char('-');
        emit openChat(chat);
    }
}
