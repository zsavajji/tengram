#include "TopicModel.h"

#include "ChatsDb.h"
#include "EngineLink.h"
#include "Moves.h"
#include "colors.h"
#include "dates.h"

#include <bb/utility/i18n/Formatting>
#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QLocale>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

using namespace bb::cascades;
using namespace bb::utility::i18n;

// The connection ChatListModel and MessageModel share, read-only (ChatsDb).
static const char *const kDb = "chats";

TopicModel::TopicModel(QObject *parent)
    : DataModel(parent)
    , m_loading(true)
    , m_region(bb::system::LocaleType::Region)
{
    m_soon.setSingleShot(true);
    m_soon.setInterval(100);
    connect(&m_soon, SIGNAL(timeout()), this, SLOT(reload()));
}

QObject *TopicModel::link() const
{
    return m_engine;
}

// A QPointer, as ChatController's: at app exit the page may outlive the EngineLink.
void TopicModel::setLink(QObject *link)
{
    m_engine = qobject_cast<EngineLink *>(link);
    if (m_engine) {
        connect(m_engine, SIGNAL(topicsChanged(QString)), this, SLOT(onTopics(QString)));
        connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
                SLOT(onReplied(QString, QString, QVariant)));
        connect(m_engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
    }
}

int TopicModel::childCount(const QVariantList &indexPath)
{
    return indexPath.isEmpty() ? m_rows.size() : 0;
}

bool TopicModel::hasChildren(const QVariantList &indexPath)
{
    return indexPath.isEmpty() && !m_rows.isEmpty();
}

QString TopicModel::itemType(const QVariantList &)
{
    return "item";
}

QVariant TopicModel::data(const QVariantList &indexPath)
{
    return m_rows.value(indexPath.value(0).toInt());
}

void TopicModel::open(const QString &chat)
{
    m_chat = chat;
    reload();
    ask();
}

void TopicModel::ask()
{
    if (!m_engine)
        return;
    QVariantMap args;
    args["chat"] = m_chat;
    m_id = m_engine->request("topics.list", args);
}

// The events were lost while the link was down: the list is asked again, which also puts the rows right.
void TopicModel::onConnected()
{
    if (m_engine && m_engine->connected() && !m_chat.isEmpty()) {
        reload();
        ask();
    }
}

void TopicModel::onTopics(const QString &chat)
{
    if (chat == m_chat && !m_soon.isActive())
        m_soon.start();
}

void TopicModel::onReplied(const QString &id, const QString &cmd, const QVariant &)
{
    if (cmd != "topics.list" || id != m_id)
        return;
    reload();
    m_loading = false;
    emit loadingChanged();
}

void TopicModel::readAll()
{
    if (!m_engine)
        return;
    foreach (const QVariantMap &row, m_rows) {
        if (row["unread"].toInt() <= 0)
            continue;
        QVariantMap args;
        args["chat"] = m_chat;
        args["topic"] = row["topic"];
        m_engine->request("chat.read", args);
    }
}

// The rows again, by docs/protocol.md's order: pinned first, then TDLib's, which SQLite sorts (more than a number in
// QML holds). A topic is muted by its own setting, else the chat's. The rows that stay may have new text: the list reads
// its cached rows again, as ChatListModel's reload does.
void TopicModel::reload()
{
    QSqlDatabase db = ChatsDb::open(kDb);
    if (m_chat.isEmpty() || !db.isOpen())
        return;
    QSqlQuery q(db);
    q.prepare("SELECT topic, name, color, general, pinned, closed, unread, last_ts, last_preview, muted_until, "
              "(SELECT muted_until FROM chats WHERE jid = topics.chat), draft FROM topics WHERE chat = ? AND hidden = 0 "
              "ORDER BY pinned DESC, CAST(ord AS INTEGER) DESC");
    q.addBindValue(m_chat);
    if (!q.exec()) {
        qWarning() << "topics:" << q.lastError().text();
        return;
    }
    dayFormatter().applyCurrentTimeAsAnchor();
    const QLocale locale = m_region.locale();
    const QString clock = timeFormat(locale, DateFormat::Short);
    const QDate today = QDate::currentDate();
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    QHash<int, QString> days; // the formatter's label by julian day: one call per day, not per topic
    QList<QVariantMap> rows;
    Moves *moves = new Moves;
    while (q.next()) {
        const QString name = q.value(1).toString();
        const int color = q.value(2).toInt() & 0xFFFFFF;
        const qint64 ts = q.value(7).toLongLong();
        const qint64 mute = q.value(9).toLongLong();
        QVariantMap row;
        row["topic"] = q.value(0).toInt();
        row["name"] = name;
        row["initials"] = q.value(3).toInt() != 0 || name.isEmpty() ? QString("#") : firstChar(name);
        row["color"] = color ? QString("#%1").arg(color, 6, 16, QChar('0')).toUpper() : QString();
        row["preview"] = q.value(8).toString();
        row["draft"] = q.value(11).toString(); // the topic's draft: ChatRow shows it in the preview's place
        row["out"] = false; // a topic has no delivery state: "You: …" says it
        row["unread"] = q.value(6).toInt();
        row["muted"] = mute > now || (mute == 0 && q.value(10).toLongLong() > now);
        row["pinned"] = q.value(4).toInt() != 0;
        row["closed"] = q.value(5).toInt() != 0;
        row["photo"] = QString();
        if (ts > 0) {
            const QDateTime when = QDateTime::fromTime_t(ts);
            const int age = when.date().daysTo(today);
            if (age > 0 && age < 7 && !days.contains(when.date().toJulianDay()))
                days.insert(when.date().toJulianDay(), dayLabel(when, today, locale, dayFormatter()));
            row["time"] = age == 0 ? locale.toString(when.time(), clock)
                        : age > 0 && age < 7 ? days.value(when.date().toJulianDay())
                        : locale.toString(when.date(), QLocale::ShortFormat);
        }
        moves->groups.insert(QString::number(row["topic"].toInt()), rows.size());
        rows.append(row);
    }

    for (int i = 0; i < m_rows.size(); ++i)
        moves->oldGroups.append(QString::number(m_rows.at(i)["topic"].toInt()));
    const bool wasEmpty = m_rows.isEmpty();
    m_rows = rows;
    if (wasEmpty && rows.isEmpty()) {
        delete moves;
        return;
    }
    if (wasEmpty) {
        delete moves;
        emit itemsChanged(DataModelChangeType::Init);
    } else {
        emit itemsChanged(DataModelChangeType::AddRemove, QSharedPointer<IndexMapper>(moves));
        emit itemsChanged(DataModelChangeType::Update);
    }
    if (wasEmpty != rows.isEmpty())
        emit emptyChanged();
}
