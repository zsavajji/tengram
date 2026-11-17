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

bool MessageModel::lessThan(const Msg &a, const Msg &b)
{
    return a.ts < b.ts || (a.ts == b.ts && a.rowid < b.rowid);
}

MessageModel::MessageModel(EngineLink *engine, const QString &chat, bool group, QObject *parent)
    : DataModel(parent)
    , m_engine(engine)
    , m_chat(chat)
    , m_group(group)
    , m_atStart(false)
    , m_loading(true)
    , m_region(bb::system::LocaleType::Region)
    , m_now(0)
    , m_titlesDay(0)
{
    connect(engine, SIGNAL(msgsChanged(QString, QStringList)), this,
            SLOT(onMsgs(QString, QStringList)));
    connect(engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
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

// The newest page, or the page older than the oldest loaded message. A row is dear to read, about
// 3 ms with its JSON and pictures (Q10, 0.0.1.156: 123 rows, 416 ms, for 13 cards), so a page stops at
// the first day's end with kPageMin rows in it, found from the rows' times alone. With no day's end
// in kPage rows it takes the rest of its oldest row's day. Either way the loaded set holds whole days.
void MessageModel::loadPage()
{
    QString where;
    QVariantList args;
    if (!m_all.isEmpty()) {
        const Msg &o = m_all.first();
        where = "AND (ts < ? OR (ts = ? AND rowid < ?))";
        args << o.ts << o.ts << o.rowid;
    }
    const QList<qint64> times = pageTimes(where, args, kPage);
    int cut = -1;
    for (int i = kPageMin; i < times.size() && cut < 0; ++i) {
        if (QDateTime::fromTime_t(times[i]).date() != QDateTime::fromTime_t(times[i - 1]).date())
            cut = i;
    }
    // A short page with no cut is the start of the chat.
    m_atStart = cut < 0 && times.size() < kPage;
    QList<Msg> rows = select(where, args, cut >= 0 ? cut : kPage);
    if (cut < 0 && !m_atStart && !rows.isEmpty()) {
        const Msg o = rows.first();
        const uint midnight =
            QDateTime(QDateTime::fromTime_t(o.ts).date(), QTime(0, 0)).toTime_t();
        rows = select("AND ts >= ? AND (ts < ? OR (ts = ? AND rowid < ?))",
                      QVariantList() << midnight << o.ts << o.ts << o.rowid, -1) + rows;
    }
    merge(QStringList(), rows, true);
}

// In a bottom-to-top list the model's end is the top.
void MessageModel::nearTop(const QVariantList &firstVisible)
{
    if (m_atStart || firstVisible.isEmpty())
        return;
    if (firstVisible.value(0).toInt() >= m_entries.size() - kNearTop)
        loadPage();
}

// The entry index of a message to jump to, -1 when it isn't shown. One older than what is loaded
// brings in every message back to its day first.
// ponytail: a match far back loads every message in between, one itemAdded each; a window around
// the match if that is slow.
int MessageModel::reveal(const QString &id)
{
    const int at = entryOf(id);
    if (at >= 0 || m_atStart || m_all.isEmpty())
        return at;
    const QList<Msg> target = select("AND id = ?", QVariantList() << id, 1);
    if (target.isEmpty())
        return -1;
    // Every day from the match's to the oldest loaded one, so the loaded set stays whole days
    // with no gap, as loadPage keeps it.
    const Msg o = m_all.first();
    const uint midnight =
        QDateTime(QDateTime::fromTime_t(target.first().ts).date(), QTime(0, 0)).toTime_t();
    merge(QStringList(), select("AND ts >= ? AND (ts < ? OR (ts = ? AND rowid < ?))",
                                QVariantList() << midnight << o.ts << o.ts << o.rowid, -1), true);
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
    if (m_all.isEmpty()) {
        loadPage();
        return;
    }
    const Msg o = m_all.first();
    QStringList asked;
    foreach (const Msg &m, m_all)
        asked << m.id; // one that isn't read again is gone
    const QList<Msg> fresh = select("AND (ts > ? OR (ts = ? AND rowid >= ?))",
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
        if (older || empty || m_atStart || m.ts > m_all.first().ts
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
        if (u == 0 || m_all[units[u - 1].msg].day != m.day) {
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
