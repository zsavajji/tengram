#include "ChatController.h"

#include "EngineLink.h"

// The reaction bar's request: which emoji the long-pressed message allows (TDLib refuses the others, "The reaction isn't
// available for the message", the user, 2026-11-22), so the bar shows only those. The connections are made at the first use,
// so a chat that never reacts carries none.

bool ChatController::reactionsFor(const QString &id)
{
    if (!m_engine)
        return false;
    connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
            SLOT(onReactionsReplied(QString, QString, QVariant)), Qt::UniqueConnection);
    connect(m_engine, SIGNAL(failureChanged()), this, SLOT(onReactionsFailure()), Qt::UniqueConnection);
    QVariantMap args;
    args["chat"] = m_jid;
    args["id"] = id;
    m_reactionsMsg = id;
    // Not connected, request() fails at once, inside the call: the "-" is there for onReactionsFailure.
    m_reactionsId = "-";
    m_reactionsId = m_engine->request("msg.reactions", args);
    return !m_reactionsId.isEmpty();
}

void ChatController::onReactionsReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    // A newer request leaves m_reactionsId different: an older reply is stale.
    if (cmd != "msg.reactions" || id != m_reactionsId)
        return;
    m_reactionsId.clear();
    const QVariantMap map = result.toMap();
    emit reactionsAvailable(m_reactionsMsg, map["top"].toStringList(), map["all"].toStringList());
}

void ChatController::onReactionsFailure()
{
    const QVariantMap failure = m_engine ? m_engine->failure() : QVariantMap();
    if (m_reactionsId.isEmpty() || m_reactionsId == "-" || failure.value("cmd").toString() != "msg.reactions")
        return;
    m_reactionsId.clear();
    emit reactionsFailed(m_reactionsMsg);
}
