#include "Invites.h"

#include "EngineLink.h"

Invites::Invites(EngineLink *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
            SLOT(onReplied(QString, QString, QVariant)));
}

// Only a cheap look: the engine does the real parsing, and says bad_request to what isn't one.
bool Invites::openLink(const QString &url)
{
    if (!m_engine
        || (!url.contains("chat.whatsapp.com/") && !url.contains("whatsapp.com/channel/")))
        return false;
    QVariantMap args;
    args["url"] = url;
    m_asking = m_engine->request("invite.info", args);
    return true;
}

void Invites::openCard(const QString &chat, const QString &id, const QString &name)
{
    m_cmd = "group.join";
    m_args.clear();
    m_args["chat"] = chat;
    m_args["id"] = id;
    emit offered("group", name, 0);
}

void Invites::accept()
{
    if (m_engine && !m_cmd.isEmpty())
        m_accepting = m_engine->request(m_cmd, m_args);
}

void Invites::onReplied(const QString &id, const QString &, const QVariant &result)
{
    const QVariantMap map = result.toMap();
    if (id == m_asking) {
        m_asking.clear();
        if (map.value("member").toBool()) {
            emit joined(map.value("jid").toString());
            return;
        }
        m_cmd = "invite.accept";
        m_args.clear();
        const char *keys[] = { "kind", "code", "jid", "name" };
        for (int i = 0; i < 4; ++i)
            m_args[keys[i]] = map.value(keys[i]);
        emit offered(map.value("kind").toString(), map.value("name").toString(),
                     map.value("count").toInt());
    } else if (id == m_accepting) {
        m_accepting.clear();
        emit joined(map.value("jid").toString());
    }
}
