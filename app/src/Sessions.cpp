#include "Sessions.h"

#include "EngineLink.h"
#include "dates.h"

#include <bb/utility/i18n/Formatting>
#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QStringList>

using namespace bb::utility::i18n;

Sessions::Sessions(QObject *parent)
    : QObject(parent)
    , m_loaded(false)
    , m_others(0)
    , m_wanted(false)
    , m_region(bb::system::LocaleType::Region)
{
}

QObject *Sessions::link() const
{
    return m_engine;
}

// A QPointer, as Profile's: at app exit the page may outlive the EngineLink.
void Sessions::setLink(QObject *link)
{
    m_engine = qobject_cast<EngineLink *>(link);
    if (m_engine) {
        connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
                SLOT(onReplied(QString, QString, QVariant)));
        connect(m_engine, SIGNAL(failureChanged()), this, SLOT(onFailure()));
        connect(m_engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
    }
}

// A page opened while the engine (re)starts asks before it's there: the request fails with
// no_engine and nothing would ask again. So it's asked again on connecting.
void Sessions::onConnected()
{
    if (m_engine && m_engine->connected() && m_wanted && !m_loaded)
        ask();
}

// Every request goes through here: another Sessions (the scan page's) hears the same replies and
// failures, so what is ours is told by its id.
void Sessions::send(const QString &cmd, const QVariantMap &args)
{
    if (!m_engine)
        return;
    const QString id = m_engine->request(cmd, args);
    if (!id.isEmpty())
        m_sent.insert(id, cmd);
    else if (cmd != "sessions.list") // no engine: the reason is in the link's failure
        emit failed(cmd);
}

void Sessions::ask()
{
    m_wanted = true;
    send("sessions.list", QVariantMap());
}

void Sessions::terminate(const QString &id)
{
    QVariantMap args;
    args["id"] = id;
    send("sessions.terminate", args);
}

void Sessions::terminateOthers()
{
    send("sessions.terminateOthers", QVariantMap());
}

void Sessions::confirmQr(const QString &link)
{
    QVariantMap args;
    args["link"] = link;
    send("sessions.confirmQr", args);
}

// When a session was last used, as the chat list words a time: the clock today, "Yesterday" or the
// weekday within a week, else the date.
QString Sessions::whenText(qint64 stamp) const
{
    const QLocale locale = m_region.locale();
    const QDateTime when = QDateTime::fromTime_t(stamp);
    const QDate today = QDate::currentDate();
    const int age = when.date().daysTo(today);
    if (age <= 0)
        return locale.toString(when.time(), timeFormat(locale, DateFormat::Short));
    if (age < 7)
        return dayLabel(when, today, locale, dayFormatter());
    return locale.toString(when.date(), QLocale::ShortFormat);
}

QVariantMap Sessions::row(const QVariantMap &s) const
{
    QVariantMap r;
    r["id"] = s["id"];
    r["current"] = s["current"];
    r["unconfirmed"] = s["unconfirmed"];
    r["title"] = (s["app"].toString() + " " + s["version"].toString()).trimmed();
    QStringList device;
    device << s["device"].toString() << (s["platform"].toString() + " " + s["system"].toString()).trimmed();
    device.removeAll(QString());
    r["device"] = device.join(QString::fromUtf8(" · "));
    // The address when Telegram has no place for it. This device is in use as long as we are here.
    QStringList place;
    place << (s["location"].toString().isEmpty() ? s["ip"].toString() : s["location"].toString());
    place << (s["current"].toBool() ? tr("online") : s["active"].toLongLong() > 0 ? whenText(s["active"].toLongLong()) : QString());
    place.removeAll(QString());
    r["place"] = place.join(QString::fromUtf8(" · "));
    return r;
}

// A terminate's reply is answered by asking again, so the page shows what Telegram has.
void Sessions::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    if (m_sent.take(id).isEmpty())
        return;
    if (cmd == "sessions.terminate" || cmd == "sessions.terminateOthers") {
        ask();
    } else if (cmd == "sessions.confirmQr") {
        emit confirmed();
    } else {
        m_list.clear();
        m_others = 0;
        const QVariantList sessions = result.toMap().value("sessions").toList();
        for (int i = 0; i < sessions.size(); ++i) {
            const QVariantMap session = sessions.at(i).toMap();
            m_list << row(session);
            if (!session["current"].toBool())
                ++m_others;
        }
        m_loaded = true;
        emit listChanged();
    }
}

// The failure carries no id: the first of our requests with its cmd is the one that failed.
void Sessions::onFailure()
{
    if (!m_engine)
        return;
    const QString cmd = m_engine->failure().value("cmd").toString();
    const QString id = m_sent.key(cmd);
    if (id.isEmpty())
        return;
    m_sent.remove(id);
    // A list that fails shows on the page by itself, from the link's failure.
    if (cmd != "sessions.list")
        emit failed(cmd);
}
