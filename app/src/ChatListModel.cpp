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
#include <QMetaType>
#include <QPair>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QThread>

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

RowClock::RowClock(const QLocale &l)
    : locale(l)
    , clock(timeFormat(l, DateFormat::Short))
    , today(QDate::currentDate())
    , now(QDateTime::currentMSecsSinceEpoch() / 1000)
{
    // Only the last week's days are named (and tomorrow, for a phone whose clock is behind): eight labels, of which
    // BB10's formatter gives three ("Today", "Yesterday", tomorrow's) and the locale the weekdays. Asked here, on the
    // UI thread, so place() can run on the reader's thread; and once a day, not once a clock: the formatter is dear
    // (milliseconds a label), and a history sync makes a clock for every patch, four a second.
    static QDate labelled;
    static QString labelledIn;
    static QHash<int, QString> labels;
    if (labelled != today || labelledIn != locale.name()) {
        labelled = today;
        labelledIn = locale.name();
        labels.clear();
        dayFormatter().applyCurrentTimeAsAnchor();
        for (int back = -1; back < 7; ++back) {
            const QDate date = today.addDays(-back);
            labels.insert(date.toJulianDay(), dayLabel(QDateTime(date, QTime(12, 0)), today, locale, dayFormatter()));
        }
    }
    days = labels;
}

QString RowClock::place(QVariantMap *row, const QVariant &lastTs, bool pinned) const
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
            day = days.value(date.toJulianDay());
            if (day.isEmpty()) // a date beyond tomorrow
                day = locale.toString(date, QLocale::ShortFormat);
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

// By the start of any word of the name, ignoring case, as ContactPicker::filter does. An empty filter matches all.
static bool nameMatches(const QString &filter, const QString &name)
{
    if (filter.isEmpty())
        return true;
    foreach (const QString &w, name.split(' ', QString::SkipEmptyParts)) {
        if (w.startsWith(filter, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

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
    , m_thread(new QThread) // no parent, see ~ChatListModel
    , m_reading(false)
    , m_again(false)
    , m_loaded(false)
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
    qRegisterMetaType<ChatListRequest>("ChatListRequest");
    qRegisterMetaType<ChatListRead>("ChatListRead");
    ChatListReader *reader = new ChatListReader(m_dbName + "-reader");
    reader->moveToThread(m_thread);
    connect(this, SIGNAL(readWanted(ChatListRequest)), reader, SLOT(read(ChatListRequest)));
    connect(reader, SIGNAL(done(ChatListRead)), this, SLOT(onRead(ChatListRead)));
    m_thread->start();
    // A cold start asks for what chats.db already has before the engine answers (PLAN.md §12); the rows are there
    // when the event loop first runs.
    reload();
}

ChatListModel::~ChatListModel()
{
    m_thread->quit();
    m_thread->wait(2000);
    // The thread (and the reader on it) is deliberately not deleted: a QThread destroyed while it still runs aborts
    // the process, as it would if the reader is in a long read, and this object lasts as long as the app does.
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
    return m_loaded && m_groups.isEmpty();
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
    // While a read is out a patch would be overwritten by its older rows, so the change waits for the read (reload()
    // notes it).
    if (m_reading || !m_engine->fullscreen() || m_full || m_groups.isEmpty() || !patch())
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
    return nameMatches(m_filter, name);
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
// moves, appears or goes still re-reads every chat, with the moves for the list: the read itself happens on the
// reader's thread, and onRead() shows it.
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
    // One read at a time: what changes while it is out is read again when it is back.
    if (m_reading) {
        m_again = true;
        return;
    }
    m_reading = true;
    m_again = false;
    m_stale = false;
    m_changed.clear();
    m_full = false;
    m_readTimer.start();
    ChatListRequest request;
    request.archived = m_mode == Archived;
    request.filter = m_filter;
    request.clock = RowClock(m_region.locale());
    emit readWanted(request);
}

ChatListReader::ChatListReader(const QString &connectionName)
    : m_dbName(connectionName)
{
}

// On the reader's thread.
void ChatListReader::read(const ChatListRequest &request)
{
    ChatListRead out;
    QElapsedTimer timer;
    timer.start();
    QSqlDatabase db = ChatsDb::open(m_dbName);
    // Before the first link the engine hasn't made chats.db: try again at the next event.
    if (!db.isOpen()) {
        emit done(out);
        return;
    }
    QSqlQuery q(db);
    const QString where = QString("archived = %1").arg(request.archived ? 1 : 0);
    if (!q.exec(QString("SELECT %1 FROM chats WHERE %2 ORDER BY pinned DESC, last_ts DESC").arg(kRowColumns).arg(where))) {
        qWarning() << "chats:" << q.lastError().text();
        emit done(out);
        return;
    }
    const RowClock &clock = request.clock;

    // Where a reload's time goes (the user, 2026-11-24: a cold start reads 500 chats twice, 293 and 278 ms on the
    // Classic): the query, SQLCipher's fetching and decrypting of the rows (next()), and building them.
    const qint64 execNs = timer.nsecsElapsed();
    qint64 fetchNs = 0;
    QElapsedTimer step;
    for (;;) {
        step.start();
        const bool more = q.next();
        fetchNs += step.nsecsElapsed();
        if (!more)
            break;
        const QString jid = q.value(0).toString();
        QVariantMap row = baseRow(q, clock.now);
        if (!nameMatches(request.filter, row["name"].toString()))
            continue;
        const QString title = clock.place(&row, q.value(3), row["pinned"].toBool());
        if (out.groups.isEmpty() || out.groups.last().title != title) {
            out.groups.append(ChatListGroup());
            out.groups.last().title = title;
            out.groupAt.insert(title, out.groups.size() - 1);
        }
        out.itemAt.insert(jid, qMakePair(out.groups.size() - 1, out.groups.last().rows.size()));
        out.groups.last().rows.append(row);
        ++out.chats;
    }
    const qint64 builtNs = timer.nsecsElapsed();
    out.queryMs = int(execNs / 1000000);
    out.fetchMs = int(fetchNs / 1000000);
    out.rowsMs = int((builtNs - execNs - fetchNs) / 1000000);
    out.ok = true;
    emit done(out);
}

// On the UI thread: swaps the rows in and tells the list.
void ChatListModel::onRead(const ChatListRead &read)
{
    QElapsedTimer timer;
    timer.start();
    m_reading = false;
    const bool shownEmpty = empty();
    m_loaded = true;
    if (read.ok) {
        const bool wasEmpty = m_groups.isEmpty();
        Moves *moves = 0;
        if (!wasEmpty) {
            moves = new Moves;
            moves->groups = read.groupAt;
            moves->items = read.itemAt;
            foreach (const Group &g, m_groups) {
                moves->oldGroups.append(g.title);
                QStringList jids;
                foreach (const QVariantMap &row, g.rows)
                    jids.append(row["jid"].toString());
                moves->oldItems.append(jids);
            }
        }
        m_groups = read.groups;
        if (wasEmpty) {
            emit itemsChanged(DataModelChangeType::Init);
        } else {
            emit itemsChanged(DataModelChangeType::AddRemove, QSharedPointer<IndexMapper>(moves));
            // The rows that stayed may have new text: the list reads its cached rows again.
            emit itemsChanged(DataModelChangeType::Update);
        }
        ++m_revision;
        emit revisionChanged();
        if (m_mode == Chats) {
            // Once per run: with "ui: main" and the Hub tap's line it times a cold start.
            static bool first = true;
            if (first) {
                first = false;
                qDebug() << "chat list: first draw," << read.chats << "chats";
            }
            updateFrame();
        }
        // A phone run decides whether insertions and moves need a patch too. The read is off the UI thread now: only
        // "list" (the emits and the frame, here) is on it, and the rest of the total is the answer's wait for the UI
        // thread, which at a cold start is busy loading main.qml.
        const int total = int(m_readTimer.elapsed());
        if (total > 50)
            qDebug("chat list: reload, %d chats in %d ms (off the UI thread: query %d, fetch %d, rows %d; on it: list %d)",
                   read.chats, total, read.queryMs, read.fetchMs, read.rowsMs, int(timer.elapsed()));
    } else {
        // chats.db could not be read: the next event reads everything again instead of patching.
        m_full = true;
    }
    if (shownEmpty != empty())
        emit emptyChanged();
    if (m_again)
        reload();
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
