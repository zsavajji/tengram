#include "CallLog.h"

#include "EngineLink.h"
#include "dates.h"

#include <bb/utility/i18n/Formatting>
#include <QDate>
#include <QDateTime>
#include <QLocale>

using namespace bb::utility::i18n;

CallLog::CallLog(QObject *parent)
    : QObject(parent)
    , m_loaded(false)
    , m_wanted(false)
    , m_region(bb::system::LocaleType::Region)
{
}

QObject *CallLog::link() const
{
    return m_engine;
}

// A QPointer, as Sessions': at app exit the page may outlive the EngineLink.
void CallLog::setLink(QObject *link)
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
void CallLog::onConnected()
{
    if (m_engine && m_engine->connected() && m_wanted && !m_loaded)
        ask();
}

void CallLog::ask()
{
    m_wanted = true;
    if (!m_engine)
        return;
    const QString id = m_engine->request("calls.list", QVariantMap());
    if (!id.isEmpty())
        m_sent.insert(id, "calls.list");
}

// When a call was made, as the chat list words a time: the clock today, "Yesterday" or the weekday
// within a week, else the date.
QString CallLog::whenText(qint64 stamp) const
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

// A call's length as the stock phone shows it: m:ss, h:mm:ss from an hour.
static QString clock(int secs)
{
    const QString s = QString::number(secs % 60).rightJustified(2, '0');
    if (secs < 3600)
        return QString::number(secs / 60) + ":" + s;
    return QString::number(secs / 3600) + ":" + QString::number(secs / 60 % 60).rightJustified(2, '0') + ":" + s;
}

QVariantMap CallLog::row(const QVariantMap &c) const
{
    const bool out = c["out"].toBool();
    const int duration = c["duration"].toInt();
    const bool declined = c["reason"].toString() == "declined";
    const bool video = c["video"].toBool();
    QVariantMap r;
    r["chat"] = c["chat"];
    r["name"] = c["name"];
    r["out"] = out;
    r["missed"] = !out && duration <= 0 && !declined;
    r["video"] = video;
    // A video call is worded as such, each phrase whole: a language may inflect the noun.
    if (duration > 0 && video)
        r["detail"] = (out ? tr("Outgoing video, %1") : tr("Incoming video, %1")).arg(clock(duration));
    else if (duration > 0)
        r["detail"] = (out ? tr("Outgoing, %1") : tr("Incoming, %1")).arg(clock(duration));
    else if (declined)
        r["detail"] = video ? tr("Declined video call") : tr("Declined call");
    else if (out)
        r["detail"] = video ? tr("Cancelled video call") : tr("Cancelled call");
    else
        r["detail"] = video ? tr("Missed video call") : tr("Missed call");
    r["time"] = whenText(c["date"].toLongLong());
    return r;
}

void CallLog::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    Q_UNUSED(cmd);
    if (m_sent.take(id).isEmpty())
        return;
    m_list.clear();
    const QVariantList calls = result.toMap().value("calls").toList();
    for (int i = 0; i < calls.size(); ++i)
        m_list << row(calls.at(i).toMap());
    m_loaded = true;
    emit listChanged();
}

// The failure carries no id: the first of our requests with its cmd is the one that failed. A list
// that fails leaves the page as it was.
void CallLog::onFailure()
{
    if (!m_engine)
        return;
    const QString id = m_sent.key(m_engine->failure().value("cmd").toString());
    if (!id.isEmpty())
        m_sent.remove(id);
}
