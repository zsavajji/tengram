#include "Invites.h"

#include <bb/system/InvokeManager>
#include <bb/system/InvokeReplyError>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTargetReply>
#include <QDebug>
#include <QRegExp>

#include "EngineLink.h"

using namespace bb::system;

Invites::Invites(EngineLink *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_invoker(0)
{
    connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
            SLOT(onReplied(QString, QString, QVariant)));
}

// Only a cheap look at the host, not a substring ("somechat.me/x" is not ours): the engine does the real
// parsing, and says {open: "browser"} to what isn't one. Not asked while the engine is down: the caller's
// browser is what a link had before.
bool Invites::openLink(const QString &url)
{
    static const QRegExp telegram("^(?:(?:https?://)?(?:www\\.)?(?:t\\.me|telegram\\.me|telegram\\.dog)/|tg:)",
                                  Qt::CaseInsensitive);
    if (!m_engine || !m_engine->connected() || telegram.indexIn(url) < 0)
        return false;
    QVariantMap args;
    args["url"] = url;
    m_url = url;
    m_asking = m_engine->request("link.open", args);
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
        if (map.value("open").toString() == "browser") {
            openBrowser(m_url);
            return;
        }
        const QString who = map.value("person").toString();
        if (!who.isEmpty()) {
            emit person(who, map.value("name").toString());
            return;
        }
        if (map.value("member").toBool()) {
            emit joined(map.value("jid").toString(), map.value("msg").toString());
            return;
        }
        const QString join = map.value("join").toString();
        if (!join.isEmpty()) { // a public group or channel we're not in: group.join, once the user says yes
            m_cmd = "group.join";
            m_args.clear();
            m_args["chat"] = join;
        } else { // an invite link
            m_cmd = "invite.accept";
            m_args.clear();
            const char *keys[] = { "kind", "code", "jid", "name" };
            for (int i = 0; i < 4; ++i)
                m_args[keys[i]] = map.value(keys[i]);
        }
        emit offered(map.value("kind").toString(), map.value("name").toString(),
                     map.value("count").toInt());
    } else if (id == m_accepting) {
        m_accepting.clear();
        emit joined(map.value("jid").toString(), QString());
    }
}

// As LinkPreview's tap does. A link written without its scheme ("t.me/x") gets one: it's only a host to the browser.
void Invites::openBrowser(const QString &url)
{
    if (!m_invoker)
        m_invoker = new InvokeManager(this);
    InvokeRequest request;
    request.setTarget("sys.browser");
    request.setAction("bb.action.OPEN");
    request.setUri(url.contains(':') ? url : "https://" + url);
    InvokeTargetReply *reply = m_invoker->invoke(request);
    if (!reply) {
        qWarning() << "invites: no reply from the system to OPEN" << url;
        return;
    }
    connect(reply, SIGNAL(finished()), this, SLOT(onOpened()));
}

void Invites::onOpened()
{
    InvokeTargetReply *reply = qobject_cast<InvokeTargetReply *>(sender());
    if (!reply)
        return;
    if (reply->error() != InvokeReplyError::None)
        qWarning() << "invites: OPEN failed, error" << reply->error();
    reply->deleteLater();
}
