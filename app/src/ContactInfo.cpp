#include "ContactInfo.h"

#include "EngineLink.h"
#include "colors.h"

#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTargetReply>

ContactInfo::ContactInfo(QObject *parent)
    : QObject(parent)
    , m_command("contact.info")
{
}

QObject *ContactInfo::link() const
{
    return m_engine;
}

// A QPointer, as ChatController's: at app exit the page may outlive the EngineLink.
void ContactInfo::setLink(QObject *link)
{
    m_engine = qobject_cast<EngineLink *>(link);
    if (m_engine) {
        connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
                SLOT(onReplied(QString, QString, QVariant)));
        connect(m_engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
    }
}

// As Profile's: a page opened while the engine (re)starts is asked again once it's connected.
void ContactInfo::onConnected()
{
    if (m_engine && m_engine->connected() && !m_jid.isEmpty() && m_info.isEmpty())
        ask(m_jid);
}

QVariantMap ContactInfo::info() const
{
    return m_info;
}

void ContactInfo::ask(const QString &jid)
{
    if (!m_engine)
        return;
    m_jid = jid;
    QVariantMap args;
    args["chat"] = jid;
    m_id = m_engine->request(m_command, args);
}

void ContactInfo::block(bool on)
{
    if (!m_engine)
        return;
    QVariantMap args;
    args["chat"] = m_jid;
    args["block"] = on;
    m_blockId = m_engine->request("contact.block", args);
}

void ContactInfo::exportChat()
{
    if (!m_engine)
        return;
    QVariantMap args;
    args["chat"] = m_jid;
    m_exportId = m_engine->request("chat.export", args);
}

// The card the phone app's "Add to contacts" opens: new contact or an existing one, the number
// filled in. Its target and type are from the Contacts app's manifest on the Q10 (2026-11-01).
void ContactInfo::saveNumber(const QString &phone)
{
    bb::system::InvokeRequest request;
    request.setTarget("sys.pim.contacts.card.picker");
    request.setAction("bb.action.ADDTOCONTACT");
    request.setMimeType("application/vnd.blackberry.string.phone");
    request.setData(phone.toUtf8());
    bb::system::InvokeTargetReply *reply = m_invoker.invoke(request);
    if (reply)
        connect(reply, SIGNAL(finished()), reply, SLOT(deleteLater()));
}

// Gives each group the tile the chat list gives it: initials and the jid's colour.
void ContactInfo::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    if (cmd == "contact.block" && id == m_blockId) {
        ask(m_jid); // info.blocked is read again with the rest
        return;
    }
    if (cmd == "chat.export" && id == m_exportId) {
        emit exported(result.toMap()["path"].toString());
        return;
    }
    if (cmd != m_command || id != m_id)
        return;
    QVariantMap info = result.toMap();
    QVariantList groups;
    foreach (const QVariant &v, info["groups"].toList()) {
        QVariantMap g = v.toMap();
        if (g["name"].toString().isEmpty())
            g["name"] = tr("Unnamed group");
        g["initials"] = initials(g["name"].toString());
        g["color"] = pickColor(g["jid"].toString(), kColors, kColorCount);
        groups << g;
    }
    info["groups"] = groups;
    m_info = info;
    emit infoChanged();
}
