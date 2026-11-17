#ifndef OPENREQUESTS_H
#define OPENREQUESTS_H

#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>
#include <QObject>
#include <QString>

// A tap on a chat's item in the Hub (engine/hub_qnx.go) starts or resumes the app with
// bb.action.OPEN or VIEW. The item's id, "chat_" and the chat key in hex, rides in the URI or
// the data; the first match of either is the chat. The signal reaches QML as it comes; main.qml
// holds it until the chat list is up.
class OpenRequests : public QObject
{
    Q_OBJECT
public:
    // EngineLink's manager: one per app, so an invocation can't go to an instance nobody
    // listens on.
    OpenRequests(bb::system::InvokeManager *manager, QObject *parent = 0);

signals:
    void openChat(const QString &jid);
    // The Share menu's file (a local path) or text; one of the two is empty.
    void shared(const QString &path, const QString &text);

private slots:
    void invoked(const bb::system::InvokeRequest &request);
};

#endif
