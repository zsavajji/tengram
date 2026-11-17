#include "MessageModel.h"

#include "Albums.h"
#include "EngineLink.h"
#include "Moves.h"
#include "colors.h"
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
// How close to the bottom a newer page is loaded in window mode, in entries (see nearTop).
static const int kNearBottom = 20;
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
// How long the window's first chat.newer reply is waited for: a request that fails quietly (the engine is
// older, TDLib refused) gives no reply, and the page would keep its spinner for good.
static const int kWindowGiveUpMs = 15000;
// m_low before the list says where it is: far from the bottom, so nothing loads newer by a stale guess.
static const int kFar = 0x7fffffff;
// The most users one senders.get names (docs/protocol.md).
static const int kSenderBatch = 100;

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
    , m_openLimit(50)
    , m_bridgePages(0)
    , m_bridgeNewest(0)
    , m_bridgeBefore(0)
    , m_holdOlder(false)
    , m_window(false)
    , m_awaitOpen(false)
    , m_newerMore(false)
    , m_readInbox(0)
    , m_low(kFar)
    , m_region(bb::system::LocaleType::Region)
    , m_now(0)
    , m_titlesDay(0)
{
    connect(engine, SIGNAL(msgsChanged(QString, QStringList)), this,
            SLOT(onMsgs(QString, QStringList)));
    connect(engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
    connect(engine, SIGNAL(replied(QString, QString, QVariant)), this,
            SLOT(onReplied(QString, QString, QVariant)));
    connect(engine, SIGNAL(photo(QString, QString)), this, SLOT(onPhoto(QString, QString)));
    m_historyAt.invalidate(); // QElapsedTimer has no constructor
    m_bridgeAt.invalidate();
    m_newerAt.invalidate();
    m_windowGiveUp.setSingleShot(true);
    m_windowGiveUp.setInterval(kWindowGiveUpMs);
    connect(&m_windowGiveUp, SIGNAL(timeout()), this, SLOT(giveUp()));
    findUnread();
    QTimer::singleShot(kFirstLoadMs, this, SLOT(loadFirst()));
}

// The first page and its cards hold the UI thread for most of a second, which used to come before
// the page moved at all (the user, 2026-11-06). So the page goes first, with a spinner: the system
// runs the slide by itself, so this doesn't stop it.
void MessageModel::loadFirst()
{
    if (m_window) {
        // Not the newest page: the cache may have gaps around the unread messages, so nothing unverified is
        // shown and m_loading stays true (the spinner) until chat.newer answers (takeWindow).
        askWindow();
        m_windowGiveUp.start();
        return;
    }
    QElapsedTimer took;
    took.start();
    m_holdOlder = !m_unreadId.isEmpty(); // rebuild stops at the divider until the page releases the rest
    loadPage();
    // A chat that opens at its first unread message needs the rows back to it before the list shows: up to
    // four more pages, stopping when one adds nothing.
    for (int more = 0; more < 4 && !m_unreadId.isEmpty() && entryOf(m_unreadId) < 0 && !m_starved; ++more)
        loadPage();
    if (!m_unreadId.isEmpty() && entryOf(m_unreadId) < 0) {
        m_unreadId.clear(); // not reached: the chat opens at the newest, as before
        m_holdOlder = false;
    }
    m_loading = false;
    emit loadingChanged();
    askOlder(); // loadPage's own call came while loading; a chat.open reply may already be in
    qDebug() << "chat: first page," << m_all.size() << "messages in" << took.elapsed() << "ms"
             << (m_unreadId.isEmpty() ? "" : "(opens at the first unread)");
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
    QVariantMap d = e.members.isEmpty() ? m_all[e.msg].data : albumData(e);
    if (!m_group)
        return d;
    // A group's cards: the runs' flags, and what the engine said of the sender. Set on every row that has a sender id,
    // "" and all, so a recycled card never keeps another sender's picture.
    d["showName"] = e.showName;
    d["showTile"] = e.showTile;
    const QString who = d.value("senderId").toString();
    if (!who.isEmpty()) {
        const QHash<QString, Sender>::const_iterator it = m_senders.constFind(who);
        if (it != m_senders.constEnd() && it->color >= 0 && it->color < kSenderCount)
            d["senderColor"] = QString::fromLatin1(kSenders[it->color]);
        d["senderTag"] = it != m_senders.constEnd() ? it->tag : QString();
        d["senderPhoto"] = it != m_senders.constEnd() ? it->photo : QString();
    }
    return d;
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

// The "Unread messages" divider's entry, -1 when there is none.
int MessageModel::unreadEntry() const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].key == "unread")
            return i;
    }
    return -1;
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
    else if (!m_newerId.isEmpty() && id == m_newerId)
        takeNewer(result.toMap());
    else if (id == m_historyId)
        takeHistory(result.toMap());
    else if (id == m_bridgeId)
        takeBridge(result.toMap());
    else if (m_senderAsks.contains(id))
        takeSenders(id, result.toMap());
}

// Asks senders.get for the senders of the rows just read, in batches of 100: the colour of their names, an admin's tag
// and their picture (docs/protocol.md). Not before the engine is there; the ids stay wanted, and the rows read again on
// connecting collect them again.
void MessageModel::askSenders()
{
    if (!m_group || m_sendersWanted.isEmpty() || !m_engine || !m_engine->connected())
        return;
    const QStringList wanted = m_sendersWanted.toList();
    for (int from = 0; from < wanted.size(); from += kSenderBatch) {
        const QStringList part = wanted.mid(from, kSenderBatch);
        QVariantList users;
        foreach (const QString &id, part)
            users << id; // strings: ids exceed 2^53
        QVariantMap ask;
        ask["chat"] = m_chat;
        ask["users"] = users;
        const QString request = m_engine->requestQuiet("senders.get", ask);
        if (request.isEmpty())
            return;
        m_senderAsks.insert(request, part);
        foreach (const QString &id, part) {
            m_sendersAsked.insert(id);
            m_sendersWanted.remove(id);
        }
    }
}

// The answer: an entry for each sender the engine knows. The ones it left out (TDLib hasn't told it of them yet) are
// forgotten, so the next row of theirs that is read asks again; their names keep the hash colour meanwhile.
void MessageModel::takeSenders(const QString &id, const QVariantMap &r)
{
    const QStringList asked = m_senderAsks.take(id);
    QSet<QString> got;
    foreach (const QString &who, asked) {
        if (!r.contains(who)) {
            m_sendersAsked.remove(who);
            continue;
        }
        const QVariantMap one = r.value(who).toMap();
        Sender s;
        s.color = one.value("color").toInt();
        s.tag = one.value("tag").toString();
        s.photo = one.value("photo").toString();
        m_senders.insert(who, s);
        got.insert(who);
    }
    updateSenders(got, false);
}

// A sender's small picture came after senders.get answered (the engine's photo event, for every connection): only the
// senders this chat knows, and only their pictures change.
void MessageModel::onPhoto(const QString &jid, const QString &path)
{
    QHash<QString, Sender>::iterator it = m_senders.find(jid);
    if (it == m_senders.end() || it->photo == path)
        return;
    it->photo = path;
    updateSenders(QSet<QString>() << jid, true);
}

// The cards of these senders are drawn again: every entry of theirs that shows something of them, a name, a tag or a
// picture (tilesOnly: just the pictures). itemUpdated doesn't move the list.
void MessageModel::updateSenders(const QSet<QString> &senders, bool tilesOnly)
{
    if (senders.isEmpty())
        return;
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries[i];
        if (e.msg < 0 || !(e.showTile || (e.showName && !tilesOnly)))
            continue;
        if (senders.contains(m_all[e.msg].data.value("senderId").toString()))
            emit itemUpdated(QVariantList() << i);
    }
}

// chat.open verified the newest 50 and says where they start and end.
void MessageModel::takeOpen(const QVariantMap &r)
{
    // The newest page is not the window: the floor comes from chat.newer (takeWindow), and after joinLive a
    // later chat.open is the normal one below.
    if (m_window)
        return;
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
    m_openLimit = 50; // the unread ones are verified now: a later chat.open checks the newest page only
    m_floor = oldest.isEmpty() ? kNoFloor : oldest.toLongLong();
    m_top = newest;
    m_more = r["more"].toBool() && !oldest.isEmpty();
    if (m_awaitOpen) {
        // Left the window for a jump (reveal): msgs events were skipped while loading, so the newest page is
        // read again, now from the floor up, and the spinner goes; the page's pending jump runs on that.
        m_awaitOpen = false;
        m_windowGiveUp.stop();
        const QStringList old = keys();
        m_all.clear();
        refresh(old, QSet<QString>());
        loadPage();
        m_loading = false;
        emit loadingChanged();
        return;
    }
    if (m_loading) // the first page reads from the floor up
        return;
    dropBelowFloor();
    askOlder();
    fillShort();
}

// A list too short to scroll never nears its top, so nearTop never loads the page the engine just verified: a chat
// opened with only its last message cached showed that one and nothing older (Classic, 0.1.0.11). Fewer rows than
// the smallest page, with more verified below, loads the next page now.
void MessageModel::fillShort()
{
    if (!m_loading && !atStart() && m_all.size() < kPageMin)
        loadPage();
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
    fillShort();
}

// Window mode (the class comment in the header). The first chat.newer asks for the page right after
// chats.read_inbox; its reply says where the verified run starts and ends, which is what chat.open does for
// the newest page.
void MessageModel::askWindow()
{
    if (!m_engine)
        return;
    QVariantMap ask;
    ask["chat"] = m_chat;
    ask["after"] = QString::number(m_readInbox); // a string: ids exceed 2^53
    ask["limit"] = kPage;
    m_newerId = m_engine->requestQuiet("chat.newer", ask);
    m_newerAt.start();
}

// A chat.newer reply: the window's first (no floor yet), or a later page that moves m_top up.
void MessageModel::takeNewer(const QVariantMap &r)
{
    m_newerId.clear();
    if (!m_window)
        return;
    if (!m_floorKnown) {
        takeWindow(r);
        return;
    }
    const QString oldest = r["oldest"].toString();
    if (!oldest.isEmpty())
        m_top = qMax(m_top, r["newest"].toString().toLongLong());
    m_newerMore = r["more"].toBool() && !oldest.isEmpty();
    // The rows are in chats.db now; they join the list when the view is at the bottom (the user waited for
    // them there), else when it scrolls there (nearTop). Loading them always would walk the whole backlog.
    if (m_low <= kNearBottom)
        loadNewer();
}

// The window's first reply. Its rows are shown as a page, the divider goes above the first incoming one, and
// the spinner ends; the page scrolls to the divider (loaded()).
void MessageModel::takeWindow(const QVariantMap &r)
{
    const QString oldest = r["oldest"].toString();
    // Nothing newer than read_inbox after all (a stale unread count): open at the newest.
    if (oldest.isEmpty()) {
        leaveWindow();
        return;
    }
    const qint64 floor = oldest.toLongLong();
    const qint64 top = r["newest"].toString().toLongLong();
    const QList<Msg> rows = select("AND CAST(id AS INTEGER) BETWEEN ? AND ?", QVariantList() << floor << top,
                                   kPage * 2);
    if (rows.isEmpty()) { // the engine said it wrote them: not showing anything would be worse than the newest
        leaveWindow();
        return;
    }
    m_floorKnown = true;
    m_floor = floor;
    m_top = top;
    m_newerMore = r["more"].toBool();
    m_more = true; // older is unknown until chat.history says
    foreach (const Msg &m, rows) {
        if (!m.data["out"].toBool()) {
            m_unreadId = m.id; // oldest first: the first one the user hasn't read
            break;
        }
    }
    m_windowGiveUp.stop();
    m_holdOlder = !m_unreadId.isEmpty();
    merge(QStringList(), rows, true);
    m_loading = false;
    emit loadingChanged();
    askOlder();
    // The window already reaches the newest message: it is the normal model with a divider. Else the next page.
    if (m_newerMore)
        askNewer();
    else
        joinLive();
    qDebug() << "chat: window of" << m_all.size() << "messages after" << m_readInbox
             << (m_window ? "(more to come)" : "(reaches the newest)");
}

// One page ahead of the bottom: chat.newer verifies the next page after m_top into chats.db (docs/protocol.md).
void MessageModel::askNewer()
{
    if (!m_window || !m_floorKnown || !m_engine)
        return;
    // A request that failed quietly is asked again on a later call, as askOlder's.
    if (!m_newerId.isEmpty() && m_newerAt.isValid() && m_newerAt.elapsed() < kHistoryMs)
        return;
    QVariantMap ask;
    ask["chat"] = m_chat;
    ask["after"] = QString::number(m_top); // a string: ids exceed 2^53
    ask["limit"] = kPage;
    m_newerId = m_engine->requestQuiet("chat.newer", ask);
    m_newerAt.start();
}

// The next page below the loaded newest: the verified rows (up to m_top) that chats.db holds, oldest first,
// appended at index 0 like a live message (itemAdded at an end never moves the rows on screen). When the
// verified run is all loaded it either ends at Telegram's newest message (join) or the engine is asked for more.
void MessageModel::loadNewer()
{
    if (!m_window || m_all.isEmpty())
        return;
    const Msg n = m_all.last(); // a copy: merge replaces m_all
    const QList<Msg> rows = select("AND (ts > ? OR (ts = ? AND CAST(id AS INTEGER) > ?)) AND CAST(id AS INTEGER) <= ?",
                                   QVariantList() << n.ts << n.ts << n.rowid << m_top, kPage, true);
    merge(QStringList(), rows, true); // paged: never `incoming`, the user is reading down to them
    // Fewer than a page: chats.db has nothing more up to m_top (the message at m_top may have been deleted
    // since, so reaching its id is not the only way to be done).
    if (rows.size() < kPage || rows.last().rowid >= m_top) {
        if (m_newerMore)
            askNewer();
        else
            joinLive();
    }
}

// The window reaches the newest message: the model is the normal one from here (a later chat.open reply
// checks the run against m_top), and the live rows that came meanwhile, which merge skipped, are loaded now.
// ponytail: all of them at once, 3 ms a row; a page at a time if a chat can get hundreds in one window's life.
void MessageModel::joinLive()
{
    m_window = false;
    m_newerId.clear();
    emit windowChanged();
    merge(QStringList(), select("AND CAST(id AS INTEGER) > ?", QVariantList() << m_top, -1), true);
}

// Back to the newest end, with the floor unknown as at a first open: the window never answered, held nothing,
// or the user sent a message (toLiveEnd). Everything loaded goes (a window is not the newest page); the
// cache's newest page shows as the first paint of any chat and chat.open, sent again by the controller,
// sets the floor.
// awaitOpen (a jump to a message the window doesn't hold): the spinner stays until chat.open sets the floor,
// since reveal can't load back to a message before that; the page's pending jump then runs (ChatPage.loaded).
void MessageModel::leaveWindow(bool awaitOpen)
{
    const bool wasLoading = m_loading;
    const bool had = !m_entries.isEmpty();
    m_windowGiveUp.stop();
    m_window = false;
    emit windowChanged();
    m_newerId.clear();
    m_newerMore = false;
    m_unreadId.clear();
    m_holdOlder = false;
    m_floorKnown = false;
    m_floor = 0;
    m_top = 0;
    m_more = false;
    m_starved = false;
    m_historyId.clear(); // requests of the window's floor: their replies must not set the new one
    m_bridgeId.clear();
    m_all.clear();
    rebuild();
    if (had)
        emit itemsChanged(DataModelChangeType::Init);
    loadPage();
    if (awaitOpen) {
        m_awaitOpen = true;
        m_windowGiveUp.start(); // an answer that never comes ends the spinner (giveUp)
        if (!wasLoading) {
            m_loading = true;
            emit loadingChanged();
        }
    } else if (wasLoading) {
        m_loading = false;
        emit loadingChanged();
    }
    emit wantOpen();
}

void MessageModel::releaseOlder()
{
    if (!m_holdOlder)
        return;
    const QStringList old = keys();
    m_holdOlder = false;
    refresh(old, QSet<QString>()); // appended at the model's end, above the divider: the rows on screen stay
    qDebug() << "chat: older rows released," << old.size() << "->" << m_entries.size() << "entries";
}

void MessageModel::toLiveEnd()
{
    if (m_window)
        leaveWindow();
}

// The window's first reply never came.
void MessageModel::giveUp()
{
    if (m_awaitOpen) { // chat.open never answered after leaving for a jump: the newest page stays, no jump
        m_awaitOpen = false;
        m_loading = false;
        emit loadingChanged();
        return;
    }
    if (m_window && !m_floorKnown) {
        qDebug() << "chat: no window after" << kWindowGiveUpMs << "ms, opening at the newest";
        leaveWindow();
    }
}

// In a bottom-to-top list the model's end is the top. Until chat.open answers only the first paint
// from the cache shows: paging waits. In window mode the floor comes from chat.newer, and the bottom pages too.
void MessageModel::nearTop(const QVariantList &firstVisible)
{
    if (!m_floorKnown || firstVisible.isEmpty())
        return;
    const int first = firstVisible.value(0).toInt();
    m_low = first;
    // Nothing more in chats.db and the engine's page is on its way: every scroll would read nothing.
    const bool waiting = m_starved && !m_historyId.isEmpty() && m_historyAt.elapsed() < kHistoryMs;
    if (!atStart() && !waiting && first >= m_entries.size() - kNearTop)
        loadPage();
    // After the top: an older page appends at the model's end and leaves `first` right, a newer one shifts it.
    // One page is 50 entries, more than kNearTop, so this can't cascade either.
    // 20, not kNearTop: if `first` is the visual top's entry rather than the bottom's, the bottom shows it at
    // about a screen (13 cards) and kNearTop would never be reached. Early is harmless; never is not.
    if (m_window && first <= kNearBottom)
        loadNewer();
}

// The newest message the screen shows when entry `first` is its bottom-most: a header or the divider walks up
// to the message above it (older, also on screen); an album gives its newest member. "" when none.
QString MessageModel::shownUpTo(int first) const
{
    for (int i = qMax(first, 0); i < m_entries.size(); ++i) {
        const Entry &e = m_entries[i];
        if (e.msg < 0)
            continue;
        return e.members.isEmpty() ? m_all[e.msg].id : m_all[e.members.last()].id;
    }
    return QString();
}

// The entry index of a message to jump to, -1 when it isn't shown (or is below the verified floor). One
// older than what is loaded brings in every message back to its day first.
// ponytail: a match far back loads every message in between, one itemAdded each; a window around
// the match if that is slow.
int MessageModel::reveal(const QString &id)
{
    releaseOlder(); // a jump may be to a message the divider holds back
    const int at = entryOf(id);
    if (at >= 0)
        return at;
    // Search and pins reach anywhere: a message the window doesn't hold leaves it for the regular chat, and the
    // page jumps once chat.open has set the floor (-1 now, the model loading meanwhile; the user's ask).
    if (m_window) {
        leaveWindow(true);
        return -1;
    }
    if (atStart() || m_all.isEmpty())
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
    // In window mode a row beyond the loaded newest is dropped by merge, so it isn't read at all: chat.open's
    // newest page and every chat.newer page name 50 ids that would cost 150 ms each time.
    QStringList want = ids;
    if (m_window) {
        want.clear();
        const qint64 reach = m_all.isEmpty() ? m_top : m_all.last().rowid;
        foreach (const QString &id, ids) {
            if (id.toLongLong() <= reach)
                want << id;
        }
    }
    // The sim's seed names 600 ids at once; SQLite binds 999 at most.
    QList<Msg> fresh;
    for (int from = 0; from < want.size(); from += 500) {
        const QStringList part = want.mid(from, 500);
        QVariantList args;
        QStringList marks;
        foreach (const QString &id, part) {
            args << id;
            marks << "?";
        }
        fresh += select(QString("AND id IN (%1)").arg(marks.join(",")), args, part.size());
    }
    merge(want, fresh, false); // not ids: a loaded row that wasn't read again would count as gone
}

// Events sent while the link was down are lost: read everything from the oldest loaded message
// on again (not the newest N: messages that came meanwhile would push loaded ones out).
void MessageModel::onConnected()
{
    if (!m_engine || !m_engine->connected())
        return;
    // The window's first request went out before the engine was there, or died with its restart: ask again.
    // The give-up timer keeps running, so a window that never answers still ends. Before loadFirst's own
    // request (m_newerAt invalid) there is nothing to repeat.
    if (m_window && !m_floorKnown) {
        if (m_newerAt.isValid())
            askWindow();
        return;
    }
    if (m_loading)
        return;
    m_asked.clear(); // a fetch the engine lost with its restart is asked again
    m_historyId.clear(); // so is a chat.history
    m_bridgeId.clear(); // and a bridge: the chat.open that follows checks the gap again
    m_newerId.clear(); // and a chat.newer
    m_senderAsks.clear(); // and a senders.get: the rows read again below collect the senders still unknown
    m_sendersAsked = QSet<QString>::fromList(m_senders.keys());
    if (m_all.isEmpty()) {
        loadPage();
        return;
    }
    const Msg o = m_all.first();
    QStringList asked;
    foreach (const Msg &m, m_all)
        asked << m.id; // one that isn't read again is gone
    // In window mode only up to the loaded newest: the rows beyond it come with loadNewer, and there may be
    // thousands in chats.db.
    QString where = "AND (ts > ? OR (ts = ? AND CAST(id AS INTEGER) >= ?))";
    QVariantList args;
    args << o.ts << o.ts << o.rowid;
    if (m_window) {
        where += " AND CAST(id AS INTEGER) <= ?";
        args << m_all.last().rowid;
    }
    const QList<Msg> fresh = select(where, args, -1); // no limit
    foreach (const Msg &m, fresh)
        asked << m.id;
    merge(asked, fresh, false);
    if (m_window) {
        askOlder(); // takeOpen, which asks it after a reconnect, returns early in window mode
        askNewer();
    }
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
    // In window mode what is beyond the loaded newest arrives with loadNewer, or with joinLive when the window
    // reaches it: a live row appended now would leave a gap below it, since loadNewer reads from the newest
    // loaded on. loadNewer and joinLive pass older, which skips this.
    const qint64 reach = m_all.isEmpty() ? m_top : m_all.last().rowid;
    foreach (const Msg &m, fresh) {
        if (loaded.contains(m.id))
            continue;
        if (m_window && !older && m.rowid > reach)
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
    askSenders(); // the senders of the rows read, now that they are in
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
    QHash<QString, int> drawn;    // and how a group's entries were drawn: showName and showTile
    foreach (const Entry &e, m_entries) {
        if (!e.sig.isEmpty())
            sigs.insert(e.key, e.sig);
        if (m_group)
            drawn.insert(e.key, (e.showName ? 1 : 0) | (e.showTile ? 2 : 0));
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
        qDebug() << "chat: rows reordered, the list is told AddRemove";
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
        // A new message from the same person ended the run the entry above it was the newest of: it loses its picture
        // (and an older page can take the name off the oldest one).
        const QHash<QString, int>::const_iterator was = drawn.constFind(e.key);
        if (was != drawn.constEnd() && was.value() != ((e.showName ? 1 : 0) | (e.showTile ? 2 : 0)))
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
        // Telegram's divider: the entry after a unit is drawn above it, and before that day's header
        // when the unit is the day's oldest, so the order on screen is date, divider, message.
        if (!m_unreadId.isEmpty()) {
            bool first = units[u].key == m_unreadId;
            foreach (int j, units[u].members) {
                if (m_all[j].id == m_unreadId)
                    first = true;
            }
            if (first) {
                Entry h;
                h.key = "unread";
                h.title = tr("Unread messages");
                h.msg = -1;
                m_entries.append(h);
                if (m_holdOlder)
                    break; // nothing older than the divider yet (m_holdOlder)
            }
        }
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
    if (m_group)
        markRuns();
    // m_entries changes only here, so this is the one place that tells `empty` changed.
    if (wasEmpty != m_entries.isEmpty())
        emit emptyChanged();
}

// Whose message entry i is, for the runs: the sender's id of an incoming message in a group. "" for what is no one's: a
// day's header, the unread divider, a service line, our own message, and what is past either end of the list. Each of
// those breaks a run.
QString MessageModel::runOf(int i) const
{
    if (i < 0 || i >= m_entries.size() || m_entries[i].msg < 0)
        return QString();
    return m_all[m_entries[i].msg].data.value("senderId").toString(); // an album's is its first member's
}

// A group's runs, as Telegram draws them: one person's consecutive messages share a single name, above the oldest, and a
// single picture, beside the newest. The list is newest first, so the older neighbour is the next index. A message with
// no sender id (nothing to tell it apart by) keeps its name and takes no picture.
void MessageModel::markRuns()
{
    for (int i = 0; i < m_entries.size(); ++i) {
        const QString who = runOf(i);
        m_entries[i].showName = who.isEmpty() || runOf(i + 1) != who;
        m_entries[i].showTile = !who.isEmpty() && runOf(i - 1) != who;
    }
}
