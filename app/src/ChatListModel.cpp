#include "ChatListModel.h"

#include "ChatsDb.h"
#include "EngineLink.h"
#include "Moves.h"
#include "colors.h"
#include "Pins.h"
#include "dates.h"

#include <bb/utility/i18n/Formatting>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QHash>
#include <QLocale>
#include <QPair>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

using namespace bb::cascades;
using namespace bb::utility::i18n;

// The connection to chats.db (ChatsDb), read-only: only the engine writes it (PLAN.md §12).
static const char *const kDb = "chats";

// A chat with no name yet, by docs/protocol.md's chats.kind.
static QString placeholder(int kind)
{
    switch (kind) {
    case 1:
        return ChatListModel::tr("Unnamed group");
    case 2:
        return ChatListModel::tr("Broadcast list");
    case 3:
        return ChatListModel::tr("Channel");
    }
    return ChatListModel::tr("Unknown");
}

// The row's fields from a SELECT of kRowColumns, all but the time, which depends on the list.
static const char *const kRowColumns =
    "jid, kind, name, last_ts, last_preview, last_out, last_state, unread, muted_until, pinned, photo, forum, secret, mentions, translate";

static QVariantMap baseRow(const QSqlQuery &q, qint64 now)
{
    const QString jid = q.value(0).toString();
    const int kind = q.value(1).toInt();
    const QString name = q.value(2).isNull() ? placeholder(kind) : q.value(2).toString();
    QVariantMap row;
    row["jid"] = jid;
    row["kind"] = kind;
    row["name"] = name;
    row["initials"] = initials(name);
    row["color"] = pickColor(jid, kSenders, kSenderCount);
    row["preview"] = q.value(4).toString();
    row["out"] = q.value(5).toInt() != 0;
    row["state"] = q.value(6).toInt();
    row["unread"] = q.value(7).toInt();
    row["muted"] = q.value(8).toLongLong() > now;
    // chats.pinned is 0, or the main list's position order while pinned (larger is higher): an int64 that a double
    // would round (TDLib's pins sit near 2^63), so it stays a qint64 for the comparisons below; QML reads pinned.
    const qint64 pinOrder = q.value(9).toLongLong();
    row["pinned"] = pinOrder != 0;
    row["pinOrder"] = pinOrder;
    row["ts"] = q.value(3).isNull() ? qint64(-1) : q.value(3).toLongLong(); // the sort key, with pinned
    row["photo"] = q.value(10).toString(); // the engine's preview file's path, "" when none
    row["forum"] = q.value(11).toInt() != 0; // a group with topics: a tap opens its TopicsPage
    row["secret"] = q.value(12).toInt(); // 0 not a secret chat, 1 pending, 2 ready, 3 closed (docs/protocol.md, Secret chats)
    row["mentions"] = q.value(13).toInt(); // unread mentions and replies to us: the row's @ beside the count
    row["translate"] = q.value(14).toString(); // the language incoming messages are shown in, "" for off
    return row;
}

// The list's order, pinned first (in the order the user gave them: the same as SQL's pinned DESC) and then
// newest: positive when a sorts before b, negative when after, 0 for a tie.
static int byKey(const QVariantMap &a, const QVariantMap &b)
{
    const bool pa = a.value("pinned").toBool();
    const bool pb = b.value("pinned").toBool();
    if (pa != pb)
        return pa ? 1 : -1;
    const qint64 oa = a.value("pinOrder").toLongLong();
    const qint64 ob = b.value("pinOrder").toLongLong();
    if (oa != ob)
        return oa > ob ? 1 : -1;
    const qint64 ta = a.value("ts").toLongLong();
    const qint64 tb = b.value("ts").toLongLong();
    return ta > tb ? 1 : ta < tb ? -1 : 0;
}

// What a row's time and its group's title come from, for a reload and a patch alike so they cannot
// disagree. One per run: it asks BB10's formatter for each day once.
struct RowClock
{
    explicit RowClock(const QLocale &l)
        : locale(l)
        , clock(timeFormat(l, DateFormat::Short))
        , today(QDate::currentDate())
        , now(QDateTime::currentMSecsSinceEpoch() / 1000)
    {
    }

    // Sets row["time"] from last_ts (null when the chat has no message) and pinned, and returns the
    // title of the group the row belongs to.
    QString place(QVariantMap *row, const QVariant &lastTs, bool pinned)
    {
        const bool hasTime = !lastTs.isNull();
        const QDateTime when = QDateTime::fromTime_t(lastTs.toUInt());
        // The last week goes by day: "Today" and "Yesterday" from BB10's formatter, then the
        // weekday (the formatter names only two or three days back, and dates the rest). Older
        // chats go by month, so a long tail of old chats isn't a header each.
        QString day, header;
        int age = 0;
        if (hasTime) {
            const QDate date = when.date();
            age = date.daysTo(today);
            // Only the last week's days are named. The formatter was asked for every older day too, a label
            // nothing showed, and it is dear: most of a reload's 65 to 165 ms for 40 chats (Q10, 0.1.1.306).
            if (age < 7) {
                if (!days.contains(date.toJulianDay()))
                    days.insert(date.toJulianDay(), dayLabel(when, today, locale, dayFormatter()));
                day = days.value(date.toJulianDay());
            }
            header = age < 7 ? day : capitalized(locale.standaloneMonthName(date.month()));
            if (age >= 7 && date.year() != today.year())
                header += " " + QString::number(date.year());
        }
        // The time under a day's header; the day, or the date, where no header says it.
        if (hasTime)
            (*row)["time"] = age == 0 || (age < 7 && !pinned) ? locale.toString(when.time(), clock)
                           : age < 7 ? day : locale.toString(when.date(), QLocale::ShortFormat);
        return pinned ? ChatListModel::tr("Pinned") : hasTime ? header : ChatListModel::tr("No messages");
    }

    QLocale locale;
    QString clock;
    QDate today;
    qint64 now;
    QHash<int, QString> days; // the formatter's label by julian day: one call per day, not per chat
};

ChatListModel::ChatListModel(EngineLink *engine, Mode mode, QObject *parent)
    : DataModel(parent)
    , m_engine(engine)
    , m_mode(mode)
    , m_dbName(mode == Archived ? "chats-archived" : kDb)
    , m_unreadTotal(0)
    , m_frameCounts(QVariantList() << 0 << 0 << 0)
    , m_frameNames(QStringList() << QString() << QString() << QString())
    , m_revision(0)
    , m_stale(false)
    , m_full(false)
    , m_region(bb::system::LocaleType::Region)
{
    // During a history sync the engine sends a chats event every 50 ms; a reload at most every
    // 250 ms keeps the UI thread free.
    m_soon.setSingleShot(true);
    m_soon.setInterval(250);
    connect(&m_soon, SIGNAL(timeout()), this, SLOT(update()));
    connect(engine, SIGNAL(chatsChanged(QStringList)), this, SLOT(onChats(QStringList)));
    // Events sent while the link was down are lost; the database has what they said.
    connect(engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
    // Nobody sees the frame while the app is fullscreen: it's computed when the app leaves it. And
    // the list is read only in front: coming back reads it if anything changed meanwhile.
    connect(engine, SIGNAL(fullscreenChanged()), this, SLOT(onFullscreenChanged()));
    // A cold start draws what chats.db already has, before the engine answers (PLAN.md §12).
    reload();
}

int ChatListModel::childCount(const QVariantList &indexPath)
{
    if (indexPath.isEmpty())
        return m_groups.size();
    if (indexPath.size() == 1)
        return m_groups.value(indexPath[0].toInt()).rows.size();
    return 0;
}

bool ChatListModel::hasChildren(const QVariantList &indexPath)
{
    return indexPath.size() < 2 && childCount(indexPath) > 0;
}

QString ChatListModel::itemType(const QVariantList &indexPath)
{
    return indexPath.size() == 1 ? "header" : "item";
}

QVariant ChatListModel::data(const QVariantList &indexPath)
{
    const Group g = m_groups.value(indexPath.value(0).toInt());
    if (indexPath.size() == 1)
        return g.title;
    return g.rows.value(indexPath.value(1).toInt());
}

// ponytail: an archived chat has no "time", which the chat page doesn't show.
QVariantMap ChatListModel::chat(const QString &jid)
{
    foreach (const Group &g, m_groups) {
        foreach (const QVariantMap &row, g.rows) {
            if (row["jid"].toString() == jid)
                return row;
        }
    }
    QSqlDatabase db = ChatsDb::open(m_dbName);
    if (!db.isOpen())
        return QVariantMap();
    QSqlQuery q(db);
    q.prepare(QString("SELECT %1 FROM chats WHERE jid = ?").arg(kRowColumns));
    q.addBindValue(jid);
    if (!q.exec() || !q.next())
        return QVariantMap();
    return baseRow(q, QDateTime::currentMSecsSinceEpoch() / 1000);
}

// Qt does the matching, not SQL's LIKE: LIKE folds ASCII only, so "è" would not find "È".
// ponytail: reads every text of the chat on each search; an FTS table if a chat of many thousands
// makes Enter slow.
QStringList ChatListModel::findMessages(const QString &jid, const QString &text)
{
    QStringList ids;
    const QString t = text.trimmed();
    if (t.isEmpty())
        return ids;
    QSqlDatabase db = ChatsDb::open(m_dbName);
    if (!db.isOpen())
        return ids;
    QSqlQuery q(db);
    q.prepare("SELECT id, text FROM messages WHERE chat = ? AND kind NOT IN (6, 13) AND deleted = 0 "
              "AND text != '' ORDER BY ts DESC, CAST(id AS INTEGER) DESC");
    q.addBindValue(jid);
    if (!q.exec()) {
        qWarning() << "find:" << q.lastError().text();
        return ids;
    }
    while (q.next()) {
        if (q.value(1).toString().contains(t, Qt::CaseInsensitive))
            ids << q.value(0).toString();
    }
    return ids;
}

// Every pinned chat of the main list in their order, top first, read from the table and not from the rows so a filter
// does not hide any: chat.pinOrder takes them all. Empty on the Archived page.
QStringList ChatListModel::pinnedJids()
{
    QStringList jids;
    QSqlDatabase db = ChatsDb::open(m_dbName);
    if (m_mode != Chats || !db.isOpen())
        return jids;
    QSqlQuery q(db);
    if (!q.exec("SELECT jid FROM chats WHERE pinned != 0 AND archived = 0 ORDER BY pinned DESC, last_ts DESC")) {
        qWarning() << "pinned:" << q.lastError().text();
        return jids;
    }
    while (q.next())
        jids << q.value(0).toString();
    return jids;
}

bool ChatListModel::empty() const
{
    return m_groups.isEmpty();
}

void ChatListModel::reloadSoon()
{
    if (!m_soon.isActive())
        m_soon.start();
}

void ChatListModel::onChats(const QStringList &jids)
{
    foreach (const QString &jid, jids)
        m_changed.insert(jid);
    reloadSoon();
}

void ChatListModel::onConnected()
{
    m_full = true;
    reloadSoon();
}

// Both calls can come twice in a row: a second one finds nothing new to do.
void ChatListModel::onFullscreenChanged()
{
    if (m_engine->fullscreen()) {
        if (m_stale)
            reload();
    } else if (m_mode == Chats) {
        updateFrame();
    }
}

// The 250 ms timer. In the background reload() only notes that the list is stale.
void ChatListModel::update()
{
    if (!m_engine->fullscreen() || m_full || m_groups.isEmpty() || !patch())
        reload();
    m_changed.clear();
    m_full = false;
}

// The row next to [g, i] in the order of all groups' rows (step -1 or 1), as the patches leave it;
// 0 past either end. A group is never empty.
const QVariantMap *ChatListModel::adjacent(int g, int i, int step, const Patches &patched) const
{
    i += step;
    if (i < 0 || i >= m_groups.at(g).rows.size()) {
        g += step;
        if (g < 0 || g >= m_groups.size())
            return 0;
        i = step < 0 ? m_groups.at(g).rows.size() - 1 : 0;
    }
    const Patches::const_iterator it = patched.constFind(qMakePair(g, i));
    return it != patched.constEnd() ? &it.value() : &m_groups.at(g).rows.at(i);
}

// Updates the rows of the changed chats in place. False when one can't be: a chat that appears,
// goes, changes group or moves in the order, which only a reload (with its moves) shows.
bool ChatListModel::patch()
{
    if (m_changed.isEmpty())
        return true;
    QSqlDatabase db = ChatsDb::open(m_dbName);
    if (!db.isOpen())
        return false;
    // Where the changed chats sit now, in one pass over the rows.
    QHash<QString, QPair<int, int> > at;
    for (int g = 0; g < m_groups.size(); ++g) {
        const QList<QVariantMap> &rows = m_groups.at(g).rows;
        for (int i = 0; i < rows.size(); ++i) {
            const QString jid = rows.at(i).value("jid").toString();
            if (m_changed.contains(jid))
                at.insert(jid, qMakePair(g, i));
        }
    }

    dayFormatter().applyCurrentTimeAsAnchor();
    RowClock clock(m_region.locale());
    QSqlQuery q(db);
    q.prepare(QString("SELECT %1, archived FROM chats WHERE jid = ?").arg(kRowColumns));
    Patches patched;
    foreach (const QString &jid, m_changed) {
        q.bindValue(0, jid);
        if (!q.exec()) {
            qWarning() << "chat:" << q.lastError().text();
            return false;
        }
        // Does the chat belong in this list now?
        QVariantMap row;
        bool here = false;
        if (q.next() && (q.value(15).toInt() != 0) == (m_mode == Archived)) {
            row = baseRow(q, clock.now);
            here = matches(row["name"].toString());
        }
        const QHash<QString, QPair<int, int> >::const_iterator was = at.constFind(jid);
        if (was == at.constEnd()) {
            if (here)
                return false; // it appears
            continue;
        }
        if (!here)
            return false; // it goes
        if (clock.place(&row, q.value(3), row["pinned"].toBool()) != m_groups.at(was->first).title)
            return false; // another group
        patched.insert(*was, row);
    }
    // It keeps its place only if it still sorts between its neighbours, as they will be.
    for (Patches::const_iterator it = patched.constBegin(); it != patched.constEnd(); ++it) {
        const QVariantMap *before = adjacent(it.key().first, it.key().second, -1, patched);
        const QVariantMap *after = adjacent(it.key().first, it.key().second, 1, patched);
        if ((before && byKey(*before, it.value()) < 0) || (after && byKey(it.value(), *after) < 0))
            return false;
    }
    for (Patches::const_iterator it = patched.constBegin(); it != patched.constEnd(); ++it) {
        m_groups[it.key().first].rows[it.key().second] = it.value();
        emit itemUpdated(QVariantList() << it.key().first << it.key().second);
    }
    // Even with no row here: chat(jid) reads the other lists' chats from the database.
    ++m_revision;
    emit revisionChanged();
    if (m_mode == Chats)
        updateFrame();
    return true;
}

// By the start of any word of the name, ignoring case, as ContactPicker::filter does.
bool ChatListModel::matches(const QString &name) const
{
    if (m_filter.isEmpty())
        return true;
    foreach (const QString &w, name.split(' ', QString::SkipEmptyParts)) {
        if (w.startsWith(m_filter, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

void ChatListModel::setFilter(const QString &text)
{
    const QString t = text.trimmed();
    if (t == m_filter)
        return;
    m_filter = t;
    reload();
}

// A chats event patches the rows it names in place when they keep their place (patch()). A chat that
// moves, appears or goes still re-reads every chat, with the moves for the list.
// ponytail: move the rows with itemMoved if reloads still show in the log.
void ChatListModel::reload()
{
    // Nobody sees the list: the next time the app is in front reads it. The frame's numbers are
    // what the change is for now.
    if (!m_engine->fullscreen()) {
        m_stale = true;
        m_changed.clear();
        m_full = false;
        if (m_mode == Chats)
            updateFrame();
        return;
    }
    QElapsedTimer timer;
    timer.start();
    QSqlDatabase db = ChatsDb::open(m_dbName);
    // Before the first link the engine hasn't made chats.db: try again at the next event.
    if (!db.isOpen())
        return;
    QSqlQuery q(db);
    const QString where = QString("archived = %1").arg(m_mode == Archived ? 1 : 0);
    if (!q.exec(QString("SELECT %1 FROM chats WHERE %2 ORDER BY pinned DESC, last_ts DESC").arg(kRowColumns).arg(where))) {
        qWarning() << "chats:" << q.lastError().text();
        return;
    }
    m_stale = false;
    m_changed.clear();
    m_full = false;

    dayFormatter().applyCurrentTimeAsAnchor();
    RowClock clock(m_region.locale());

    QList<Group> groups;
    Moves *moves = new Moves;
    while (q.next()) {
        const QString jid = q.value(0).toString();
        QVariantMap row = baseRow(q, clock.now);
        if (!matches(row["name"].toString()))
            continue;
        const QString title = clock.place(&row, q.value(3), row["pinned"].toBool());
        if (groups.isEmpty() || groups.last().title != title) {
            groups.append(Group());
            groups.last().title = title;
            moves->groups.insert(title, groups.size() - 1);
        }
        moves->items.insert(jid, qMakePair(groups.size() - 1, groups.last().rows.size()));
        groups.last().rows.append(row);
    }

    foreach (const Group &g, m_groups) {
        moves->oldGroups.append(g.title);
        QStringList jids;
        foreach (const QVariantMap &row, g.rows)
            jids.append(row["jid"].toString());
        moves->oldItems.append(jids);
    }
    const bool wasEmpty = m_groups.isEmpty();
    m_groups = groups;
    if (wasEmpty) {
        delete moves;
        emit itemsChanged(DataModelChangeType::Init);
    } else {
        emit itemsChanged(DataModelChangeType::AddRemove, QSharedPointer<IndexMapper>(moves));
        // The rows that stayed may have new text: the list reads its cached rows again.
        emit itemsChanged(DataModelChangeType::Update);
    }
    if (wasEmpty != m_groups.isEmpty())
        emit emptyChanged();
    ++m_revision;
    emit revisionChanged();
    int chats = 0;
    foreach (const Group &g, m_groups)
        chats += g.rows.size();
    if (m_mode == Chats) {
        // Once per run: with "ui: main" and the Hub tap's line it times a cold start.
        static bool first = true;
        if (first) {
            first = false;
            qDebug() << "chat list: first draw," << chats << "chats";
        }
        updateFrame();
    }
    // A phone run decides whether insertions and moves need a patch too.
    const qint64 ms = timer.elapsed();
    if (ms > 50)
        qDebug() << "chat list: reload," << chats << "chats in" << ms << "ms";
}

// A chat marked unread (unread < 0, step 5.3) counts as 1. 2^40 in muted_until is forever. A muted chat counts only
// its unread mentions and replies to us (chats.mentions, none while chats.mentions_off says they don't notify), which the Hub
// notifies too; marked unread, it counts nothing. Each row names its chats, newest first, and shows no message text:
// the newest line showed after it was read, and with the notification preview off (Q10, 0.0.1.88).
void ChatListModel::updateFrame()
{
    if (m_engine->fullscreen())
        return;
    QSqlDatabase db = ChatsDb::open(m_dbName);
    if (!db.isOpen())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    int counts[3] = {0, 0, 0};
    QStringList names[3];
    QSqlQuery q(db);
    // ponytail: the first 10 names a row are more than its one line holds.
    q.prepare("SELECT name, jid, kind, CASE WHEN muted_until > ? THEN (CASE WHEN mentions_off THEN 0 ELSE mentions END) "
              "ELSE unread END AS n FROM chats "
              "WHERE n != 0 AND archived = 0 ORDER BY last_ts DESC");
    q.addBindValue(now);
    if (q.exec()) {
        while (q.next()) {
            const int kind = q.value(2).toInt();
            const int row = kind == 3 ? 2 : kind == 1 ? 1 : 0;
            const int n = q.value(3).toInt();
            counts[row] += n < 0 ? 1 : n;
            if (names[row].size() < 10) {
                const QString name = q.value(0).toString();
                names[row] << (name.isEmpty() ? q.value(1).toString().section('@', 0, 0) : name);
            }
        }
    }
    QVariantList frameCounts;
    QStringList frameNames;
    for (int i = 0; i < 3; ++i) {
        frameCounts << counts[i];
        frameNames << names[i].join(", ");
    }
    const int total = counts[0] + counts[1] + counts[2];
    if (total != m_unreadTotal || frameCounts != m_frameCounts || frameNames != m_frameNames) {
        m_unreadTotal = total;
        m_frameCounts = frameCounts;
        m_frameNames = frameNames;
        emit frameChanged();
    }
}
