#include "ContactPicker.h"

#include "EngineLink.h"
#include "colors.h"

#include <QStringList>
#include <QVariantMap>

ContactPicker::ContactPicker(QObject *parent)
    : QObject(parent)
    , m_model(new bb::cascades::ArrayDataModel(this))
    , m_limit(0)
    , m_loaded(false)
    , m_wanted(false)
{
}

QObject *ContactPicker::link() const
{
    return m_engine;
}

void ContactPicker::setLink(QObject *link)
{
    m_engine = qobject_cast<EngineLink *>(link);
    if (!m_engine)
        return;
    connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
            SLOT(onReplied(QString, QString, QVariant)));
    connect(m_engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
    connect(m_engine, SIGNAL(photo(QString, QString)), this, SLOT(onPhoto(QString, QString)));
}

// A picture the engine fetched for a contact: in the list, and in the row on show, which is replaced in place so the
// list doesn't jump.
void ContactPicker::onPhoto(const QString &jid, const QString &path)
{
    for (int i = 0; i < m_all.size(); ++i) {
        QVariantMap c = m_all[i].toMap();
        if (c["jid"].toString() == jid) {
            c["photo"] = path;
            m_all[i] = c;
            break;
        }
    }
    for (int i = 0; i < m_model->size(); ++i) {
        QVariantMap row = m_model->value(i).toMap();
        if (row["jid"].toString() == jid) {
            row["photo"] = path;
            m_model->replace(i, row);
            break;
        }
    }
}

bb::cascades::DataModel *ContactPicker::model() const
{
    return m_model;
}

bool ContactPicker::loaded() const
{
    return m_loaded;
}

int ContactPicker::total() const
{
    return m_all.size();
}

int ContactPicker::shown() const
{
    return m_model->size();
}

int ContactPicker::chosenCount() const
{
    return m_chosen.size();
}

void ContactPicker::load()
{
    m_wanted = true;
    ask();
}

void ContactPicker::loadGroup(const QString &chat)
{
    m_chat = chat;
    m_limit = 31;
    m_wanted = true;
    ask();
}

void ContactPicker::ask()
{
    if (m_loaded || !m_engine || !m_engine->connected())
        return;
    if (m_chat.isEmpty()) {
        m_askId = m_engine->request("contacts.list", QVariantMap());
        return;
    }
    QVariantMap args;
    args["chat"] = m_chat;
    m_askId = m_engine->request("group.info", args);
}

// Asks again on a reconnect if the first ask never came back.
void ContactPicker::onConnected()
{
    if (m_wanted)
        ask();
}

// Adds what the page draws to each contact (or group member, us left out): the tile's initials
// and the colour the chat's tile has in the chat list.
void ContactPicker::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    if (cmd == "group.create" && id == m_createId) {
        m_createId.clear();
        const QVariantMap r = result.toMap();
        emit groupCreated(r["jid"].toString(), r["refused"].toInt());
        return;
    }
    if ((cmd != "contacts.list" && cmd != "group.info") || id != m_askId)
        return;
    QVariantList people = result.toList();
    if (cmd == "group.info")
        people = result.toMap()["participants"].toList();
    m_all.clear();
    foreach (const QVariant &v, people) {
        QVariantMap c = v.toMap();
        if (c["me"].toBool())
            continue;
        c["kind"] = 0;
        c["initials"] = initials(c["name"].toString());
        c["color"] = pickColor(c["jid"].toString(), kColors, kColorCount);
        c["chosen"] = m_chosen.contains(c["jid"].toString());
        m_all << c;
    }
    m_loaded = true;
    filter(m_text);
    emit loadedChanged();
}

QStringList ContactPicker::chosen() const
{
    return m_chosen.toList();
}

void ContactPicker::clearChosen()
{
    if (m_chosen.isEmpty())
        return;
    m_chosen.clear();
    filter(m_text); // redraws the rows without their ticks
    emit chosenChanged();
}

// ponytail: a linear pass per keystroke; fine for a few thousand contacts.
void ContactPicker::filter(const QString &text)
{
    m_text = text;
    const QString t = text.trimmed();
    m_model->clear();
    foreach (const QVariant &v, m_all) {
        bool match = t.isEmpty();
        if (!match) {
            foreach (const QString &w, v.toMap()["name"].toString().split(' ', QString::SkipEmptyParts)) {
                if (w.startsWith(t, Qt::CaseInsensitive)) {
                    match = true;
                    break;
                }
            }
        }
        if (match) {
            QVariantMap row = v.toMap();
            row["chosen"] = m_chosen.contains(row["jid"].toString());
            m_model->append(row);
        }
    }
    emit shownChanged();
}

bool ContactPicker::toggle(const QVariantList &indexPath)
{
    const int i = indexPath.value(0).toInt();
    if (i < 0 || i >= m_model->size())
        return true;
    QVariantMap row = m_model->value(i).toMap();
    const QString jid = row["jid"].toString();
    const bool on = !m_chosen.contains(jid);
    if (on && m_limit > 0 && m_chosen.size() >= m_limit)
        return false;
    if (on)
        m_chosen.insert(jid);
    else
        m_chosen.remove(jid);
    row["chosen"] = on;
    m_model->replace(i, row);
    emit chosenChanged();
    return true;
}

QString ContactPicker::createGroup(const QString &name)
{
    if (!m_engine || !m_engine->connected())
        return QString();
    QVariantList members;
    foreach (const QString &jid, m_chosen)
        members << jid;
    QVariantMap req;
    req["name"] = name;
    req["participants"] = members;
    m_createId = m_engine->request("group.create", req);
    return m_createId;
}

QString ContactPicker::startGroupCall(bool video)
{
    if (!m_engine || !m_engine->connected())
        return QString();
    QVariantList members;
    foreach (const QString &jid, m_chosen)
        members << jid;
    QVariantMap req;
    req["chat"] = m_chat;
    req["members"] = members;
    if (video)
        req["video"] = true;
    return m_engine->request("call.start", req);
}
