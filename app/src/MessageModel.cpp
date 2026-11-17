#include "MessageModel.h"

#include "Albums.h"
#include "EngineLink.h"
#include "Moves.h"
#include "dates.h"

#include <bb/utility/i18n/Formatting>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>
#include <QHash>
#include <QLocale>
#include <QSet>

using namespace bb::cascades;
using namespace bb::utility::i18n;

static const int kPage = 50;
// What a page holds at least, when it stops at a day's end: more than the 13 cards the list makes.
static const int kPageMin = 20;
// How close to the top an older page is asked for, in entries. Not much more than a screen holds: from
// the first page's top row on the screen, 25 asked for the next page with no scroll at all, and in a group
// that was 103 rows and five more cards, holding the UI thread 1.4 s just after opening (Q10, 0.0.1.158).
static const int kNearTop = 8;
// Long enough for the page's push to reach the system before the first page takes the UI thread.
static const int kFirstLoadMs = 50;
// How long a chat.history request is waited for before one that failed quietly is asked again.
static const int kHistoryMs = 15000;
// The most chat.history pages (of kBridgeLimit) a gap is bridged with: 300 messages at most. Past that the
// old rows are dropped rather than fetched.
static const int kBridgePages = 3;
static const int kBridgeLimit = 100;
// The floor of an empty chat: no row is old enough to show.
static const qint64 kNoFloor = 0x7fffffffffffffffLL;

bool MessageModel::lessThan(const Msg &a, const Msg &b)
{
    return a.ts < b.ts || (a.ts == b.ts && a.rowid < b.rowid);
}

MessageModel::MessageModel(EngineLink *engine, const QString &chat, bool group, bool channel, QObject *parent)
    : DataModel(parent)
    , m_engine(engine)
    , m_chat(chat)
    , m_group(group)
    , m_channel(channel)
    , m_floor(0)
    , m_floorKnown(false)
    , m_top(0)
    , m_more(false)
    , m_starved(false)
    , m_loading(true)
    , m_bridgePages(0)
    , m_bridgeNewest(0)
    , m_bridgeBefore(0)
    , m_region(bb::system::LocaleType::Region)
    , m_now(0)
    , m_titlesDay(0)
{
    connect(engine, SIGNAL(msgsChanged(QString, QStringList)), this,
            SLOT(onMsgs(QString, QStringList)));
    connect(engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
    connect(engine, SIGNAL(replied(QString, QString, QVariant)), this,
            SLOT(onReplied(QString, QString, QVariant)));
    m_historyAt.invalidate(); // QElapsedTimer has no constructor
    m_bridgeAt.invalidate();
    QTimer::singleShot(kFirstLoadMs, this, SLOT(loadFirst()));
}

// The first page and its cards hold the UI thread for most of a second, which used to come before
// the page moved at all (the user, 2026-11-06). So the page goes first, with a spinner: the system
// runs the slide by itself, so this doesn't stop it.
void MessageModel::loadFirst()
{
    QElapsedTimer took;
    took.start();
    loadPage();
    m_loading = false;
    emit loadingChanged();
    askOlder(); // loadPage's own call came while loading; a chat.open reply may already be in
    qDebug() << "chat: first page," << m_all.size() << "messages in" << took.elapsed() << "ms";
}

int MessageModel::childCount(const QVariantList &indexPath)
{
    return indexPath.isEmpty() ? m_entries.size() : 0;
}

bool MessageModel::hasChildren(const QVariantList &indexPath)
{
    return indexPath.isEmpty() && !m_entries.isEmpty();
}

QString MessageModel::itemType(const QVariantList &indexPath)
{
    const Entry e = m_entries.value(indexPath.value(0).toInt());
    if (e.msg < 0)
        return "header";
    const Msg &m = m_all[e.msg];
    if (m.service)
        return "service";
    // Plain text gets the plain MessageCard, a photo, video or map picture the one the picture spans
    // (the user, 2026-11-08: as Android shows them). A row's type never changes: its kind, media,
    // link preview and a location's map picture (inline, never fetched) don't.
    const QString media = m.data["media"].toString();
    if (e.members.isEmpty() && (media == "photo" || media == "video"
                                || (media == "location" && m.data["hasThumb"].toBool())))
        return "picture";
    return e.members.isEmpty() && m.kind == 0 && m.data["linkTitle"].toString().isEmpty() ? "text" : "msg";
}

QVariant MessageModel::data(const QVariantList &indexPath)
{
    const Entry e = m_entries.value(indexPath.value(0).toInt());
    if (e.msg < 0)
        return e.title;
    return e.members.isEmpty() ? QVariant(m_all[e.msg].data) : QVariant(albumData(e));
}

// An album's card: the first member's map (so its menu, reactions and search tint are the first
// member's), the members' maps, and the last one's time and ticks, since the card shows when it ended.
QVariantMap MessageModel::albumData(const Entry &e) const
{
    QVariantMap d = m_all[e.members.first()].data;
    const QVariantMap last = m_all[e.members.last()].data;
    QVariantList members;
    foreach (int i, e.members)
        members << m_all[i].data;
    d["album"] = members;
    d["albumCount"] = members.size();
    d["media"] = QString(); // MediaBubble isn't built for it
    d["time"] = last["time"];
    d["stamp"] = last["stamp"];
    d["state"] = last["state"];
    return d;
}

// The entry that shows message id (an album's, for one of its members), -1 when none does.
int MessageModel::entryOf(const QString &id) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].key == id)
            return i;
        foreach (int j, m_entries[i].members) {
            if (m_all[j].id == id)
                return i;
        }
    }
    return -1;
}

// The condition for the rows older than the oldest loaded one (none loaded: no condition) and not
// below the verified floor (once chat.open answered).
void MessageModel::olderWhere(QString &where, QVariantList &args) const
{
    if (!m_all.isEmpty()) {
        const Msg &o = m_all.first();
        where = "AND (ts < ? OR (ts = ? AND CAST(id AS INTEGER) < ?))";
        args << o.ts << o.ts << o.rowid;
    }
    if (m_floorKnown) {
        where += " AND CAST(id AS INTEGER) >= ?";
        args << m_floor;
    }
}

// The newest page, or the page older than the oldest loaded message. A row is dear to read, about
// 3 ms with its JSON and pictures (Q10, 0.0.1.156: 123 rows, 416 ms, for 13 cards), so a page stops at
// the first day's end with kPageMin rows in it, found from the rows' times alone. With no day's end in
// kPage rows it takes kPage. Pages no longer hold whole days (a busy group has more than a page a day):
// the header rule in rebuild keeps their rows at the model's end.
void MessageModel::loadPage()
{
    QString where;
    QVariantList args;
    olderWhere(where, args);
    const QList<qint64> times = pageTimes(where, args, kPage);
    int cut = -1;
    for (int i = kPageMin; i < times.size() && cut < 0; ++i) {
        if (QDateTime::fromTime_t(times[i]).date() != QDateTime::fromTime_t(times[i - 1]).date())
            cut = i;
    }
    const QList<Msg> rows = select(where, args, cut >= 0 ? cut : kPage);
    m_starved = rows.isEmpty(); // before merge: its rebuild asks atStart()
    merge(QStringList(), rows, true);
    askOlder();
}

// The oldest message is loaded and the engine has no older verified page. m_starved: the message at
// the floor may have been deleted since, so the oldest loaded row is above it with nothing older.
bool MessageModel::atStart() const
{
    return m_floorKnown && !m_more
           && (m_all.isEmpty() || m_all.first().rowid <= m_floor || m_starved);
}

// One page ahead of chats.db running out of verified rows, so the list never waits at the top for
// the network (docs/ui-fork.md 3.6). The reply comes as a replied signal (takeHistory).
void MessageModel::askOlder()
{
    if (!m_floorKnown || !m_more || !m_engine || m_loading)
        return;
    // A request that failed quietly is asked again on a later call.
    if (!m_historyId.isEmpty() && m_historyAt.isValid() && m_historyAt.elapsed() < kHistoryMs)
        return;
    QString where;
    QVariantList args;
    olderWhere(where, args);
    if (pageTimes(where, args, kPage).size() >= kPage)
        return;
    QVariantMap ask;
    ask["chat"] = m_chat;
    ask["before"] = QString::number(m_floor); // a string: ids exceed 2^53
    ask["limit"] = kPage;
    m_historyId = m_engine->requestQuiet("chat.history", ask);
    m_historyAt.start();
}

void MessageModel::expectOpen(const QString &requestId)
{
    m_openId = requestId;
}

void MessageModel::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    Q_UNUSED(cmd);
    if (id == m_openId)
        takeOpen(result.toMap());
    else if (id == m_historyId)
        takeHistory(result.toMap());
    else if (id == m_bridgeId)
        takeBridge(result.toMap());
}

// chat.open verified the newest 50 and says where they start and end.
void MessageModel::takeOpen(const QVariantMap &r)
{
    const QString oldest = r["oldest"].toString();
    const qint64 newest = r["newest"].toString().toLongLong(); // 0 for an empty reply
    if (m_floorKnown) {
        // A later chat.open (the app back in front, the engine reconnected) verified the newest 50 again.
        // The rows from m_floor to m_top are one verified run: a page that reaches m_top joins it (the run
        // grows to the page's newest). One that does not means a gap opened below the newest 50 while the
        // chat stayed open (long minimised in a busy group, Telegram's "too long" difference), and rows
        // that came live sit next to older verified ones, which chats.db alone cannot tell apart. Then
        // chat.history pages back from the page's oldest until one reaches m_top (askBridge, takeBridge).
        if (oldest.isEmpty())
            return;
        // A bridge that failed quietly is started again, like askOlder's request.
        if (!m_bridgeId.isEmpty() && m_bridgeAt.isValid() && m_bridgeAt.elapsed() < kHistoryMs) {
            m_bridgeNewest = qMax(m_bridgeNewest, newest);
            return;
        }
        if (oldest.toLongLong() <= m_top) {
            m_floor = qMin(m_floor, oldest.toLongLong());
            m_top = qMax(m_top, newest);
        } else {
            m_bridgeNewest = newest;
            m_bridgePages = 0;
            askBridge(oldest);
        }
        return;
    }
    m_floorKnown = true;
    m_floor = oldest.isEmpty() ? kNoFloor : oldest.toLongLong();
    m_top = newest;
    m_more = r["more"].toBool() && !oldest.isEmpty();
    if (m_loading) // the first page reads from the floor up
        return;
    dropBelowFloor();
    askOlder();
}

// The first paint came from the cache: what is older than the verified page goes, and comes back
// verified with the next page. Those rows are at the model's end, so this is safe.
void MessageModel::dropBelowFloor()
{
    const QStringList old = keys();
    QList<Msg> kept;
    foreach (const Msg &m, m_all) {
        if (m.rowid >= m_floor)
            kept.append(m);
    }
    m_all = kept;
    refresh(old, QSet<QString>());
}

// One page of the bridge: the messages older than before. A reply that never comes (the request fails
// quietly) leaves m_bridgeId set until takeOpen's timeout or a reconnect; the next chat.open starts it again.
void MessageModel::askBridge(const QString &before)
{
    if (!m_engine)
        return;
    QVariantMap ask;
    ask["chat"] = m_chat;
    ask["before"] = before; // a string: ids exceed 2^53
    ask["limit"] = kBridgeLimit;
    m_bridgeBefore = before.toLongLong();
    m_bridgeId = m_engine->requestQuiet("chat.history", ask);
    m_bridgeAt.start();
    ++m_bridgePages;
}

// A bridge page. The engine's reconcile wrote the rows it covers and their msgs events put them in the
// model, so reaching the run is all that is left to know.
void MessageModel::takeBridge(const QVariantMap &r)
{
    m_bridgeId.clear();
    const QString oldest = r["oldest"].toString();
    if (!oldest.isEmpty() && oldest.toLongLong() <= m_top) {
        m_top = qMax(m_top, m_bridgeNewest); // bridged: the run reaches the newest page
        return;
    }
    if (!oldest.isEmpty() && m_bridgePages < kBridgePages) {
        askBridge(oldest);
        return;
    }
    // The cap, or nothing older at all (the run's rows are gone): the run restarts at what the bridge
    // verified. A paging reply asked from the old floor would lower the floor below the gap: it is
    // ignored.
    m_floor = oldest.isEmpty() ? m_bridgeBefore : oldest.toLongLong();
    m_top = m_bridgeNewest;
    m_more = r["more"].toBool() && !oldest.isEmpty();
    m_historyId.clear();
    if (m_loading) // the first page reads from the floor up
        return;
    dropBelowFloor();
    askOlder();
}

// chat.history verified the page before the floor.
void MessageModel::takeHistory(const QVariantMap &r)
{
    m_historyId.clear();
    const QString oldest = r["oldest"].toString();
    if (!oldest.isEmpty())
        m_floor = qMin(m_floor, oldest.toLongLong());
    m_more = r["more"].toBool() && !oldest.isEmpty();
    if (m_starved)
        loadPage(); // the list was waiting at its top
    else
        refresh(keys(), QSet<QString>()); // atStart() may have just become true: the oldest day's header
}

// In a bottom-to-top list the model's end is the top. Until chat.open answers only the first paint
// from the cache shows: paging waits.
void MessageModel::nearTop(const QVariantList &firstVisible)
{
    if (!m_floorKnown || atStart() || firstVisible.isEmpty())
        return;
    // Nothing more in chats.db and the engine's page is on its way: every scroll would read nothing.
    if (m_starved && !m_historyId.isEmpty() && m_historyAt.elapsed() < kHistoryMs)
        return;
    if (firstVisible.value(0).toInt() >= m_entries.size() - kNearTop)
        loadPage();
}

// The entry index of a message to jump to, -1 when it isn't shown (or is below the verified floor). One
// older than what is loaded brings in every message back to its day first.
// ponytail: a match far back loads every message in between, one itemAdded each; a window around
// the match if that is slow.
int MessageModel::reveal(const QString &id)
{
    const int at = entryOf(id);
    if (at >= 0 || atStart() || m_all.isEmpty())
        return at;
    if (!m_floorKnown)
        return -1;
    const QList<Msg> target = select("AND id = ?", QVariantList() << id, 1);
    if (target.isEmpty() || target.first().rowid < m_floor)
        return -1;
    // Every row from the match's day to the oldest loaded one, so there is no gap.
    const Msg o = m_all.first();
    const uint midnight =
        QDateTime(QDateTime::fromTime_t(target.first().ts).date(), QTime(0, 0)).toTime_t();
    merge(QStringList(), select("AND ts >= ? AND (ts < ? OR (ts = ? AND CAST(id AS INTEGER) < ?))"
                                " AND CAST(id AS INTEGER) >= ?",
                                QVariantList() << midnight << o.ts << o.ts << o.rowid << m_floor, -1), true);
    return entryOf(id);
}

void MessageModel::unspoil(const QString &id)
{
    m_revealed.insert(id);
    onMsgs(m_chat, QStringList(id));
}

void MessageModel::onMsgs(const QString &chat, const QStringList &ids)
{
    if (chat != m_chat || m_loading) // the first page reads it anyway
        return;
    // The sim's seed names 600 ids at once; SQLite binds 999 at most.
    QList<Msg> fresh;
    for (int from = 0; from < ids.size(); from += 500) {
        const QStringList part = ids.mid(from, 500);
        QVariantList args;
        QStringList marks;
        foreach (const QString &id, part) {
            args << id;
            marks << "?";
        }
        fresh += select(QString("AND id IN (%1)").arg(marks.join(",")), args, part.size());
    }
    merge(ids, fresh, false);
}

// Events sent while the link was down are lost: read everything from the oldest loaded message
// on again (not the newest N: messages that came meanwhile would push loaded ones out).
void MessageModel::onConnected()
{
    if (!m_engine || !m_engine->connected() || m_loading)
        return;
    m_asked.clear(); // a fetch the engine lost with its restart is asked again
    m_historyId.clear(); // so is a chat.history
    m_bridgeId.clear(); // and a bridge: the chat.open that follows checks the gap again
    if (m_all.isEmpty()) {
        loadPage();
        return;
    }
    const Msg o = m_all.first();
    QStringList asked;
    foreach (const Msg &m, m_all)
        asked << m.id; // one that isn't read again is gone
    const QList<Msg> fresh = select("AND (ts > ? OR (ts = ? AND CAST(id AS INTEGER) >= ?))",
                                    QVariantList() << o.ts << o.ts << o.rowid, -1); // no limit
    foreach (const Msg &m, fresh)
        asked << m.id;
    merge(asked, fresh, false);
}

// Puts rows into m_all and tells the list. asked: the ids that were read again; fresh: the
// rows that came back. A loaded asked id that isn't in fresh is gone.
void MessageModel::merge(const QStringList &asked, const QList<Msg> &fresh, bool older)
{
    QHash<QString, Msg> back;
    foreach (const Msg &m, fresh)
        back.insert(m.id, m);
    const QSet<QString> was = QSet<QString>::fromList(asked);

    QSet<QString> loaded, updated;
    QList<Msg> all;
    foreach (const Msg &m, m_all) {
        loaded.insert(m.id);
        if (!was.contains(m.id))
            all.append(m);
        else if (back.contains(m.id)) {
            all.append(back.value(m.id));
            updated.insert(m.id);
        }
    }
    const bool empty = m_all.isEmpty();
    bool incoming = false;
    foreach (const Msg &m, fresh) {
        if (loaded.contains(m.id))
            continue;
        // A new message older than the loaded ones comes with a later page.
        if (older || empty || atStart() || m.ts > m_all.first().ts
            || (m.ts == m_all.first().ts && m.rowid > m_all.first().rowid)) {
            all.append(m);
            // Only loadPage passes older (the first load and older pages): anything else that is
            // new arrived live, even into a chat that opened empty.
            if (!older && !m.data["out"].toBool())
                incoming = true;
        }
    }
    // Sending moves a row: its ts becomes the server's.
    qStableSort(all.begin(), all.end(), lessThan);

    const QStringList old = keys();
    m_all = all;
    refresh(old, updated);
    if (incoming)
        emit this->incoming();
}

QStringList MessageModel::keys() const
{
    QStringList k;
    foreach (const Entry &e, m_entries)
        k << e.key;
    return k;
}

// a is a subsequence of b: b is a with rows inserted anywhere.
static bool isSubsequence(const QStringList &a, const QStringList &b)
{
    int i = 0;
    for (int j = 0; j < b.size() && i < a.size(); ++j) {
        if (a[i] == b[j])
            ++i;
    }
    return i == a.size();
}

// Rebuilds the entries from m_all and tells the list how they changed from old (the keys before).
// itemAdded and itemRemoved never move the rows on screen; an AddRemove did (Q10, 3.1). So when the
// rows both lists hold keep their order, the change is told one by one: the removals (a hidden or
// deleted message, the singles that an album swallows), descending, then the insertions (a new
// message at the bottom, an older page at the top, an unhidden one anywhere, the album), ascending.
// Only a reorder is an AddRemove. An album that keeps its key but gains or loses a member, or has a
// member updated, is itemUpdated.
void MessageModel::refresh(const QStringList &old, const QSet<QString> &updated)
{
    QHash<QString, QString> sigs; // the albums as they were: m_entries is still the old list
    foreach (const Entry &e, m_entries) {
        if (!e.sig.isEmpty())
            sigs.insert(e.key, e.sig);
    }
    rebuild();
    const QStringList now = keys();

    if (old.isEmpty()) {
        if (!now.isEmpty())
            emit itemsChanged(DataModelChangeType::Init);
        return;
    }
    const QSet<QString> there = QSet<QString>::fromList(now);
    QStringList kept;
    foreach (const QString &k, old) {
        if (there.contains(k))
            kept << k;
    }
    if (isSubsequence(kept, now)) {
        for (int j = old.size() - 1; j >= 0; --j) {
            if (!there.contains(old[j]))
                emit itemRemoved(QVariantList() << j); // descending: the others keep their places
        }
        int i = 0;
        for (int j = 0; j < now.size(); ++j) {
            if (i < kept.size() && kept[i] == now[j])
                ++i;
            else
                emit itemAdded(QVariantList() << j);
        }
    } else {
        // ponytail: if a sent message's new server time reorders rows and the list jumps, emit
        // itemMoved.
        Moves *moves = new Moves;
        moves->oldGroups = old;
        for (int i = 0; i < now.size(); ++i)
            moves->groups.insert(now[i], i);
        emit itemsChanged(DataModelChangeType::AddRemove, QSharedPointer<IndexMapper>(moves));
    }
    QSet<int> touched;
    foreach (const QString &id, updated)
        touched.insert(entryOf(id));
    for (int j = 0; j < m_entries.size(); ++j) {
        const Entry &e = m_entries[j];
        if (!e.sig.isEmpty() && sigs.contains(e.key) && sigs.value(e.key) != e.sig)
            touched.insert(j);
    }
    touched.remove(-1);
    foreach (int at, touched)
        emit itemUpdated(QVariantList() << at);
}

void MessageModel::hide(const QStringList &ids)
{
    const QStringList old = keys();
    foreach (const QString &id, ids)
        m_hidden.insert(id);
    refresh(old, QSet<QString>());
}

void MessageModel::unhide(const QStringList &ids)
{
    const QStringList old = keys();
    foreach (const QString &id, ids)
        m_hidden.remove(id);
    refresh(old, QSet<QString>());
}

// The header of a message's day: BB10's "Today" and "Yesterday", the weekday for the rest of the week, then
// the date in full. Asked from the formatter once per day shown, and again when the date changes: it was
// asked on every rebuild, with the anchor and the locale read each time (Q10, 0.1.1.306: 17 to 58 ms a rebuild).
QString MessageModel::dayTitle(const Msg &m)
{
    const QDate today = QDate::currentDate();
    if (m_titlesDay != today.toJulianDay()) {
        m_titlesDay = today.toJulianDay();
        m_dayTitles.clear();
        dayFormatter().applyCurrentTimeAsAnchor();
    }
    QHash<int, QString>::const_iterator it = m_dayTitles.constFind(m.day);
    if (it != m_dayTitles.constEnd())
        return it.value();
    const QDateTime when = QDateTime::fromTime_t(m.ts);
    const QString title = when.date().daysTo(today) < 7 ? dayLabel(when, today, m_locale, dayFormatter())
                                                      : longDay(when.date(), m_locale);
    m_dayTitles.insert(m.day, title);
    return title;
}

// ponytail: rebuilds the entries on each change, fine for the hundreds of rows a chat holds.
void MessageModel::rebuild()
{
    const bool wasEmpty = m_entries.isEmpty();
    m_entries.clear();
    // m_all is oldest first: the visible rows become units (a message, or an album of 4 or more),
    // then walked backwards, and a day's header follows its messages. A day with only hidden
    // messages has no header.
    std::vector<Albums::Row> rows;
    QList<int> shown;
    for (int i = 0; i < m_all.size(); ++i) {
        const Msg &m = m_all[i];
        if (m_hidden.contains(m.id))
            continue;
        shown.append(i);
        Albums::Row r;
        r.ts = m.ts;
        r.day = m.day;
        r.kind = m.kind;
        r.deleted = m.data["deleted"].toBool();
        r.text = !m.data["text"].toString().isEmpty();
        r.spoiler = m.data["spoiler"].toBool();
        r.forwarded = m.data["forwarded"].toInt() > 0;
        r.quote = !m.data["quote"].toString().isEmpty();
        r.out = m.data["out"].toBool();
        r.sender = m.who.toUtf8().constData();
        rows.push_back(r);
    }
    const std::vector<Albums::Run> runs = Albums::find(rows);
    QList<Entry> units;
    int run = 0;
    for (int k = 0; k < shown.size();) {
        Entry e;
        e.key = m_all[shown[k]].id;
        e.msg = shown[k];
        int n = 1;
        if (run < int(runs.size()) && runs[run].start == k) {
            n = runs[run++].count;
            QStringList ids;
            for (int j = k; j < k + n; ++j) {
                e.members << shown[j];
                ids << m_all[shown[j]].id;
            }
            e.key = "album:" + e.key;
            e.sig = ids.join(",");
        }
        k += n;
        units.append(e);
    }
    for (int u = units.size() - 1; u >= 0; --u) {
        const Msg &m = m_all[units[u].msg];
        m_entries.append(units[u]);
        // An album is inside one day, so its first member's date is the unit's.
        // docs/ui-fork.md 3.6: the oldest loaded day has no header until an older day's message or the
        // start of the history is loaded, so older rows of the same day append at the model's end and
        // the header appends after them.
        if (u == 0 ? atStart() : m_all[units[u - 1].msg].day != m.day) {
            Entry h;
            h.key = "day:" + QString::number(m.day);
            h.title = dayTitle(m);
            h.msg = -1;
            m_entries.append(h);
        }
    }
    // m_entries changes only here, so this is the one place that tells `empty` changed.
    if (wasEmpty != m_entries.isEmpty())
        emit emptyChanged();
}
