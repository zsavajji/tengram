#include "ChatController.h"

#include "EngineLink.h"

// The comments of a channel's post: the Comments line under it, or the menu's, asks which group and thread they are
// (msg.thread), and the page opens that thread over this one. The connections are made at the first use, so a chat that
// never opens comments carries none.

bool ChatController::openThread(const QString &id)
{
    if (!m_engine)
        return false;
    connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
            SLOT(onThreadReplied(QString, QString, QVariant)), Qt::UniqueConnection);
    connect(m_engine, SIGNAL(failureChanged()), this, SLOT(onThreadFailure()), Qt::UniqueConnection);
    QVariantMap args;
    args["chat"] = m_jid;
    args["id"] = id;
    // Not connected, request() fails at once, inside the call: the "-" is there for onThreadFailure.
    m_threadId = "-";
    m_threadId = m_engine->request("msg.thread", args);
    return !m_threadId.isEmpty();
}

void ChatController::onThreadReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    // A newer request leaves m_threadId different: an older reply is stale.
    if (cmd != "msg.thread" || id != m_threadId)
        return;
    m_threadId.clear();
    const QVariantMap map = result.toMap();
    emit threadFound(map["chat"].toString(), map["thread"].toString(), map["name"].toString());
}

void ChatController::onThreadFailure()
{
    const QVariantMap failure = m_engine ? m_engine->failure() : QVariantMap();
    if (m_threadId.isEmpty() || m_threadId == "-" || failure.value("cmd").toString() != "msg.thread")
        return;
    m_threadId.clear();
    emit threadFailed();
}
