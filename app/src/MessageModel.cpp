#include "MessageModel.h"

#include "Albums.h"
#include "EngineLink.h"
#include "Moves.h"
#include "PreviewMaker.h"
#include "Settings.h"
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
// How long after a budgeted first page the rest of it is read: the list is in view and its cards are made by then.
static const int kRestMs = 700;
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
// How long a local row (addLocal) waits for the engine's row before it goes: msg.send answers at once with the row written,
// so this is a request that was lost, not a slow one.
static const qint64 kLocalMs = 15000;

bool MessageModel::lessThan(const Msg &a, const Msg &b)
{
    return a.ts < b.ts || (a.ts == b.ts && a.rowid < b.rowid);
}

MessageModel::MessageModel(EngineLink *engine, const QString &chat, bool group, bool channel, bool secret, int topic, const QString &thread, QObject *parent)
    : DataModel(parent)
    , m_engine(engine)
    , m_chat(chat)
    , m_group(group)
    , m_channel(channel)
    , m_secret(secret)
    , m_topic(topic)
    , m_thread(thread.toLongLong())
    , m_translate(readTranslate())
    , m_retries(0)
    , m_floor(0)
    , m_floorKnown(false)
    , m_seenFirst(-1)
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
    , m_restPending(false)
    , m_newerMore(false)
    , m_readInbox(0)
    , m_low(kFar)
    , m_region(bb::system::LocaleType::Region)
    , m_now(0)
    , m_titlesDay(0)
    , m_localCount(0)
{
    connect(engine, SIGNAL(msgsChanged(QString, QStringList)), this,
            SLOT(onMsgs(QString, QStringList)));
    connect(engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
    connect(engine, SIGNAL(replied(QString, QString, QVariant)), this,
            SLOT(onReplied(QString, QString, QVariant)));
    connect(engine, SIGNAL(photo(QString, QString)), this, SLOT(onPhoto(QString, QString)));
    connect(engine, SIGNAL(chatsChanged(QStringList)), this, SLOT(onChats(QStringList)));
    connect(engine, SIGNAL(failed(QString, QString, QString)), this, SLOT(onFailed(QString, QString, QString)));
    m_retry.setSingleShot(true);
    connect(&m_retry, SIGNAL(timeout()), this, SLOT(retryTranslate()));
    m_trSoon.setSingleShot(true);
    connect(&m_trSoon, SIGNAL(timeout()), this, SLOT(askTranslate()));
    connect(PreviewMaker::instance(), SIGNAL(ready(QStringList)), this, SLOT(onPreviews(QStringList)));
    m_historyAt.invalidate(); // QElapsedTimer has no constructor
    m_bridgeAt.invalidate();
    m_newerAt.invalidate();
    m_historyRetry.setSingleShot(true);
    m_historyRetry.setInterval(kHistoryMs);
    connect(&m_historyRetry, SIGNAL(timeout()), this, SLOT(retryHistory()));
    m_edgeSync.setSingleShot(true);
    m_edgeSync.setInterval(0);
    connect(&m_edgeSync, SIGNAL(timeout()), this, SLOT(syncEdge()));
    m_windowGiveUp.setSingleShot(true);
    m_windowGiveUp.setInterval(kWindowGiveUpMs);
    connect(&m_windowGiveUp, SIGNAL(timeout()), this, SLOT(giveUp()));
    // One timer for the whole chat, not one per card. It starts when a row with expiresAt is read (read()).
    m_restTimer.setSingleShot(true);
    m_restTimer.setInterval(kRestMs);
    connect(&m_restTimer, SIGNAL(timeout()), this, SLOT(loadRest()));
    m_expiry.setInterval(1000);
    connect(&m_expiry, SIGNAL(timeout()), this, SLOT(tickExpiry()));
    // The same for a live location's label, which counts in minutes: started by read() when a running one is read.
    m_liveTick.setInterval(30000);
    connect(&m_liveTick, SIGNAL(timeout()), this, SLOT(tickLive()));
    connect(engine, SIGNAL(fullscreenChanged()), this, SLOT(onFullscreen()));
    // Started by addLocal, stopped by tickLocal when none is left.
    m_localExpiry.setInterval(1000);
    connect(&m_localExpiry, SIGNAL(timeout()), this, SLOT(tickLocal()));
    findUnread();
    QTimer::singleShot(kFirstLoadMs, this, SLOT(loadFirst()));
}

// The first page and its cards hold the UI thread for most of a second, which used to come before
// the page moved at all (the user, 2026-11-06). So the page goes first, with a spinner: the system
// runs the slide by itself, so this doesn't stop it.
// A chat that opens at its newest message reads two screens of rows first (kBudget in MessageModel.h) and the rest of the
// page 700 ms later (loadRest): a row costs 2 to 10 ms and the screen holds far fewer than the page (the user, 2026-11-20).
// One that opens at its first unread message reads every row down to the divider, as before.
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
    const bool atNewest = m_unreadId.isEmpty(); // the loop below may clear m_unreadId
    loadPage(atNewest ? kBudget : 0);
    // A chat that opens at its first unread message needs the rows back to it before the list shows: up to
    // four more pages, stopping when one adds nothing.
    for (int more = 0; more < 4 && !m_unreadId.isEmpty() && entryOf(m_unreadId) < 0 && !m_starved; ++more)
        loadPage();
    if (!m_unreadId.isEmpty() && entryOf(m_unreadId) < 0) {
        m_unreadId.clear(); // not reached: the chat opens at the newest, as before
        m_holdOlder = false;
    }
    if (atNewest && !m_all.isEmpty() && !m_starved) {
        m_restPending = true;
        m_restTimer.start();
    }
    m_loading = false;
    emit loadingChanged();
    m_edgeSync.start(); // m_loading, m_restPending and m_holdOlder changed: the loading row may be due
    askOlder(); // loadPage's own call came while loading; a chat.open reply may already be in
    qDebug() << "chat: first page," << m_all.size() << "messages in" << took.elapsed() << "ms"
             << (m_unreadId.isEmpty() ? "" : "(opens at the first unread)");
}

// The same read a scroll to the top would make, done once the list is in view: the older page after the budgeted first one.
void MessageModel::loadRest()
{
    if (!m_restPending)
        return;
    m_restPending = false;
    m_edgeSync.start(); // m_restPending ended: the loading row may be due (also on the returns below)
    if (m_loading || atStart())
        return;
    loadPage();
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
    if (e.key == "loading")
        return "loading"; // the spinner at the top edge (edgeWaiting): no message behind it, tested before the headers
    if (e.msg < 0)
        return "header";
    const Msg &m = m_all[e.msg];
    if (m.service)
        return "service";
    // Plain text gets the plain MessageCard, a photo, video or location the one the picture spans
    // (the user, 2026-11-08: as Android shows them). A row's type never changes: its kind, media and
    // link preview don't, and a location is a picture with or without its map, which the engine fetches after the row.
    const QString media = m.data["media"].toString();
    if (e.members.isEmpty() && (media == "photo" || media == "video" || media == "location"))
        return "picture";
    return e.members.isEmpty() && m.kind == 0 && m.data["linkTitle"].toString().isEmpty() ? "text" : "msg";
}

QVariant MessageModel::data(const QVariantList &indexPath)
{
    const Entry e = m_entries.value(indexPath.value(0).toInt());
    if (e.key == "loading")
        return QVariant(); // the loading row has nothing to show: LoadingCard takes no data
    if (e.msg < 0)
        return e.title;
    QVariantMap d = e.members.isEmpty() ? m_all[e.msg].data : albumData(e);
    // The list draws this row: its translation is asked for now, if the chat has a language and the row none yet.
    if (!m_translate.isEmpty()) {
        wantTranslation(m_all[e.msg]);
        foreach (int member, e.members)
            wantTranslation(m_all[member]);
    }
    // A countdown running: what is left now, every time the row is asked for (tickExpiry makes the list ask).
    const qint64 until = d.value("expiresAt").toLongLong();
    if (until > 0)
        d["ttlText"] = span(qMax<qint64>(0, until - QDateTime::currentMSecsSinceEpoch() / 1000));
    // A live location: its label and whether it still runs, as of now (tickLive makes the list ask every 30 s).
    if (d.value("live").toBool()) {
        const qint64 ends = d.value("liveUntil").toLongLong();
        d["label"] = liveLabel(d, m_locale);
        d["liveOn"] = ends <= 0 || QDateTime::currentMSecsSinceEpoch() / 1000 < ends;
    }
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
        if (it != m_senders.constEnd() && !it->photo.isEmpty())
            d["senderPhoto"] = it->photo; // else the row's own, from `people` (read), "" for none
    }
    return d;
}

QString MessageModel::idAt(int entry) const
{
    if (entry < 0 || entry >= m_entries.size())
        return QString();
    const Entry &e = m_entries.at(entry);
    if (e.msg < 0)
        return QString();
    return m_all[e.members.isEmpty() ? e.msg : e.members.first()].data["id"].toString();
}

// "10 s", "5 min", "1 h", "7 d": a timer as the foot shows it, a longer one rounded up to the next unit's whole.
QString MessageModel::span(qint64 secs)
{
    if (secs < 60)
        return tr("%1 s").arg(secs);
    if (secs < 3600)
        return tr("%1 min").arg((secs + 59) / 60);
    if (secs < 86400)
        return tr("%1 h").arg((secs + 3599) / 3600);
    return tr("%1 d").arg((secs + 86399) / 86400);
}

QString MessageModel::distance(int metres, const QLocale &locale)
{
    if (metres < 1000)
        return tr("%1 m").arg(locale.toString(metres));
    return tr("%1 km").arg(metres % 1000 == 0 ? locale.toString(metres / 1000) : locale.toString(metres / 1000.0, 'f', 1));
}

// "Live location ended"; or "Live location", then "Updated 5 min ago · 1 h left" (no end: no "left"), then for ours with a
// proximity alert "Alert within 500 m". The times are the foot's spans, rounded up.
QString MessageModel::liveLabel(const QVariantMap &d, const QLocale &locale)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    const qint64 until = d.value("liveUntil").toLongLong();
    if (until > 0 && now >= until)
        return tr("Live location ended");
    const qint64 ago = now - d.value("liveAt").toLongLong();
    QString label = tr("Live location") + "\n" + (ago < 60 ? tr("Updated just now") : tr("Updated %1 ago").arg(span(ago)));
    if (until > 0)
        label += QString::fromUtf8(" · ") + tr("%1 left").arg(span(until - now));
    const int alert = d.value("liveAlert").toInt();
    if (d.value("out").toBool() && alert > 0)
        label += "\n" + tr("Alert within %1").arg(distance(alert, locale));
    return label;
}

// Every live location's card is drawn again every 30 s while the app is in front, so its label and its ended state follow the
// clock; the timer stops when none is left running (read() starts it again for one that is read).
void MessageModel::tickLive()
{
    if (!m_engine || !m_engine->fullscreen()) {
        m_liveTick.stop();
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    bool running = false;
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        if (e.msg < 0 || !m_all.at(e.msg).data.value("live").toBool())
            continue;
        const qint64 until = m_all.at(e.msg).data.value("liveUntil").toLongLong();
        if (until <= 0 || now < until)
            running = true;
        emit itemUpdated(QVariantList() << i);
    }
    if (!running)
        m_liveTick.stop();
}

// The cards that count down are drawn again, once a second, while the app is in front; the timer stops with the last
// of them. A card that reaches 0 stays at 0 until the engine's deletion takes its row (docs/protocol.md, Secret chats).
void MessageModel::tickExpiry()
{
    if (!m_engine || !m_engine->fullscreen()) {
        m_expiry.stop();
        return;
    }
    bool any = false;
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        if (e.msg < 0 || m_all.at(e.msg).data.value("expiresAt").toLongLong() <= 0)
            continue;
        any = true;
        emit itemUpdated(QVariantList() << i);
    }
    if (!any)
        m_expiry.stop();
}

// Coming to the front draws the countdowns and the live locations again at once and restarts the ticks (tickExpiry and
// tickLive stop theirs with none).
void MessageModel::onFullscreen()
{
    if (m_engine && m_engine->fullscreen()) {
        m_expiry.start();
        tickExpiry();
        m_liveTick.start();
        tickLive();
    }
}

// An album's card: the first member's map (so its menu, reactions and search tint are the first
// member's), the members' maps, and the last one's time and ticks, since the card shows when it ended. The caption
// is the first member's that has one: Telegram's apps put it on any one of them, and the card shows it under the tiles.
QVariantMap MessageModel::albumData(const Entry &e) const
{
    QVariantMap d = m_all[e.members.first()].data;
    const QVariantMap last = m_all[e.members.last()].data;
    QVariantList members;
    bool captioned = !d["text"].toString().isEmpty();
    foreach (int i, e.members) {
        members << m_all[i].data;
        if (!captioned && !m_all[i].data["text"].toString().isEmpty()) {
            d["text"] = m_all[i].data["text"];
            d["html"] = m_all[i].data["html"];
            d["ent"] = m_all[i].data["ent"];
            d["parts"] = m_all[i].data["parts"];
            d["tailText"] = m_all[i].data["tailText"];
            d["veiled"] = m_all[i].data["veiled"];
            captioned = true;
        }
    }
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
void MessageModel::loadPage(int budget)
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
    const QList<Msg> rows = select(where, args, cut >= 0 ? cut : kPage, false, budget);
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
    if (m_topic)
        ask["topic"] = m_topic;
    if (m_thread)
        ask["thread"] = QString::number(m_thread); // a string: ids exceed 2^53
    ask["before"] = QString::number(m_floor); // a string: ids exceed 2^53
    ask["limit"] = kPage;
    setAsked(m_historyId, m_engine->requestQuiet("chat.history", ask));
    m_historyAt.start();
}

// Sets a request slot and emits awaitingChanged when its emptiness changed. The history slot also runs
// m_historyRetry: started with each new id, stopped when the answer came or the request no longer matters.
void MessageModel::setAsked(QString &slot, const QString &id)
{
    const bool changed = slot.isEmpty() != id.isEmpty();
    slot = id;
    if (&slot == &m_historyId) {
        if (id.isEmpty())
            m_historyRetry.stop();
        else
            m_historyRetry.start();
    }
    if (changed)
        edgeChanged();
}

// Whether the list's top edge shows the loading row (the last entry, drawn as a spinner by LoadingCard): an older page is
// awaited. In a bottom-to-top list the model's end is the top, and a spinner on its own row sits on empty space instead of
// over the oldest message (the user, 2026-11-20).
bool MessageModel::edgeWaiting() const
{
    return !m_loading // the first page is loading: EmptyNote's spinner is up
           && !m_holdOlder // the rows above the unread divider are held: the divider must be the list's top while the page places it
           && !m_restPending // the 0.7 s before the rest of a budgeted first page is read: it is no wait for the engine
           && (awaitingOlder() // chat.history is in flight
               // chat.open is awaited (no older page can be asked for before it): only when a page's worth of rows is loaded,
               // or every short chat would show a spinner as it opens
               || (awaitingOpen() && m_all.size() >= kPageMin));
}

// Adds or drops the loading row once what it depends on has changed. Deferred (m_edgeSync, 0 ms) and not done in
// setAsked or beside the writes of m_floorKnown: several callers sit between a change of m_all and their own refresh(), whose
// `old` keys were taken before the change, so a refresh here would tell the list the same change twice. By the time the
// timer runs every operation is done and m_entries agrees with m_all.
void MessageModel::syncEdge()
{
    if (m_loading) // the list is not built yet (or a jump's spinner is up): the rebuild that ends it counts the row
        return;
    const bool has = !m_entries.isEmpty() && m_entries.last().key == "loading";
    if (has == (!m_entries.isEmpty() && edgeWaiting()))
        return;
    refresh(keys(), QSet<QString>()); // rebuilds, and tells the list of the one row added or removed
}

// A chat.history nobody answered in kHistoryMs is asked again by itself: a list resting at its top edge scrolls no
// more, so no later call would ask again (the user, 2026-11-20). Clearing the id lifts askOlder's gate.
void MessageModel::retryHistory()
{
    if (m_historyId.isEmpty())
        return;
    qDebug() << "chat: no answer to chat.history in" << kHistoryMs << "ms, asking again";
    setAsked(m_historyId, QString());
    askOlder();
}

void MessageModel::expectOpen(const QString &requestId)
{
    m_openId = requestId;
}

void MessageModel::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    m_stickerAsks.remove(id); // the file is here: its row comes in a msgs event
    if (cmd == "msg.translate") {
        if (m_translating.remove(id) > 0) {
            m_retries = 0;
            if (m_translating.isEmpty())
                emit translatingChanged();
        }
        return;
    }
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
    else if (m_refetch.contains(id))
        onMsgs(m_chat, QStringList(m_refetch.take(id))); // the file is back where the row says: read it again
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
        // Only an answer that changes what a card draws repaints the cards (the user's log, 2026-11-20: cards were filled
        // almost twice per open): the rows already carry the colour and the picture from chats.db's `people`.
        bool changed;
        const QHash<QString, Sender>::const_iterator old = m_senders.constFind(who);
        if (old != m_senders.constEnd()) {
            changed = old->color != s.color || old->tag != s.tag || old->photo != s.photo;
        } else {
            changed = false; // no row of theirs loaded: nothing to repaint
            for (int i = 0; i < m_all.size(); ++i) {
                const QVariantMap &d = m_all.at(i).data;
                if (d.value("senderId").toString() != who)
                    continue;
                changed = !s.tag.isEmpty()
                    || (s.color >= 0 && s.color < kSenderCount
                        && QString::fromLatin1(kSenders[s.color]) != d.value("senderColor").toString())
                    || (!s.photo.isEmpty() && s.photo != d.value("senderPhoto").toString());
                break;
            }
        }
        m_senders.insert(who, s);
        if (changed)
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
        if (senders.contains(m_all[e.msg].data.value("senderId").toString())) {
            emit itemUpdated(QVariantList() << i);
        }
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
    edgeChanged(); // awaitingOpen ends
    m_openLimit = 50; // the unread ones are verified now: a later chat.open checks the newest page only
    m_floor = oldest.isEmpty() ? kNoFloor : oldest.toLongLong();
    m_top = newest;
    m_more = r["more"].toBool() && !oldest.isEmpty();
    if (m_loading) // the first page reads from the floor up
        return;
    dropBelowFloor();
    askOlder();
    fillShort();
    pageIfNearTop(); // the list may have rested at its top for the whole wait
}

// The list rests near its top and something just made an older page readable (chat.open's answer gave the floor, a
// chat.history page arrived): read it now, as nearTop would at the next scroll, which never comes for a list that
// is not moving (the user, 2026-11-20: at the top of a chat for 30 s, nothing loaded). An older page is appended
// above the list, so m_seenFirst is then far from the new top and this cannot run twice for one page. nearTop pages
// older in window mode too, so there is no m_window test here.
void MessageModel::pageIfNearTop()
{
    if (m_seenFirst < 0 || m_restPending || m_loading || !m_floorKnown || atStart())
        return;
    if (m_seenFirst >= m_entries.size() - kNearTop)
        loadPage();
}

// A list too short to scroll never nears its top, so nearTop never loads the page the engine just verified: a chat
// opened with only its last message cached showed that one and nothing older (Classic, 0.1.0.11). Fewer rows than
// the smallest page, with more verified below, loads the next page now.
void MessageModel::fillShort()
{
    // A budgeted first page is short on purpose: loadRest reads the next page.
    if (!m_restPending && !m_loading && !atStart() && m_all.size() < kPageMin)
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
    if (m_topic)
        ask["topic"] = m_topic;
    if (m_thread)
        ask["thread"] = QString::number(m_thread); // a string: ids exceed 2^53
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
    setAsked(m_historyId, QString());
    if (m_loading) // the first page reads from the floor up
        return;
    dropBelowFloor();
    askOlder();
}

// chat.history verified the page before the floor.
void MessageModel::takeHistory(const QVariantMap &r)
{
    setAsked(m_historyId, QString());
    const QString oldest = r["oldest"].toString();
    if (!oldest.isEmpty())
        m_floor = qMin(m_floor, oldest.toLongLong());
    m_more = r["more"].toBool() && !oldest.isEmpty();
    if (m_starved)
        loadPage(); // the list was waiting at its top
    else {
        refresh(keys(), QSet<QString>()); // atStart() may have just become true: the oldest day's header
        pageIfNearTop();
    }
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
    if (m_topic)
        ask["topic"] = m_topic;
    if (m_thread)
        ask["thread"] = QString::number(m_thread); // a string: ids exceed 2^53
    ask["after"] = QString::number(m_readInbox); // a string: ids exceed 2^53
    ask["limit"] = kPage;
    setAsked(m_newerId, m_engine->requestQuiet("chat.newer", ask));
    m_newerAt.start();
}

// A chat.newer reply: the window's first (no floor yet), or a later page that moves m_top up.
void MessageModel::takeNewer(const QVariantMap &r)
{
    setAsked(m_newerId, QString());
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
    edgeChanged(); // awaitingOpen ends
    m_floor = floor;
    m_top = top;
    m_newerMore = r["more"].toBool();
    m_more = true; // older is unknown until chat.history says
    // A jump window has no divider: the user came for a message, not for what is unread.
    if (m_jumpWindow.isEmpty()) {
        foreach (const Msg &m, rows) {
            if (!m.data["out"].toBool()) {
                m_unreadId = m.id; // oldest first: the first one the user hasn't read
                break;
            }
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
             << (m_jumpWindow.isEmpty() ? "" : "(a jump)") << (m_window ? "(more to come)" : "(reaches the newest)");
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
    if (m_topic)
        ask["topic"] = m_topic;
    if (m_thread)
        ask["thread"] = QString::number(m_thread); // a string: ids exceed 2^53
    ask["after"] = QString::number(m_top); // a string: ids exceed 2^53
    ask["limit"] = kPage;
    setAsked(m_newerId, m_engine->requestQuiet("chat.newer", ask));
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
    setAsked(m_newerId, QString());
    emit windowChanged();
    merge(QStringList(), select("AND CAST(id AS INTEGER) > ?", QVariantList() << m_top, -1), true);
}

// Everything loaded goes, and the model is as at a first open before chat.open or chat.newer answers: the floor is
// unknown, nothing is asked for or held, no divider. The caller says what comes next (leaveWindow: the cache's newest page
// and chat.open; openWindowAt: a window at a message).
void MessageModel::dropLoaded()
{
    const bool had = !m_entries.isEmpty();
    m_windowGiveUp.stop();
    m_restPending = false; // everything loaded goes: no stale loadRest
    m_restTimer.stop();
    setAsked(m_newerId, QString());
    m_newerMore = false;
    m_unreadId.clear();
    m_holdOlder = false;
    if (m_floorKnown) {
        m_floorKnown = false;
        edgeChanged(); // awaitingOpen starts
    }
    m_floor = 0;
    m_top = 0;
    m_more = false;
    m_starved = false;
    setAsked(m_historyId, QString()); // requests of the window's floor: their replies must not set the new one
    m_bridgeId.clear();
    m_all.clear();
    rebuild();
    if (had)
        emit itemsChanged(DataModelChangeType::Init);
}

// Back to the newest end, with the floor unknown as at a first open: the window never answered, held nothing,
// or the user sent a message (toLiveEnd). Everything loaded goes (a window is not the newest page); the
// cache's newest page shows as the first paint of any chat and chat.open, sent again by the controller,
// sets the floor.
void MessageModel::leaveWindow()
{
    const bool wasLoading = m_loading;
    m_window = false;
    emit windowChanged();
    dropLoaded();
    loadPage();
    if (wasLoading) {
        m_loading = false;
        emit loadingChanged();
    }
    emit wantOpen();
}

// A window of history at message `id`, which the loaded rows and the cache can't reach (reveal): everything loaded goes
// and chat.newer verifies a page that starts at the message itself (after = the id before it), as for the unread ones;
// the spinner stays until it answers and the page's pending jump runs then (ChatPage.loaded).
void MessageModel::openWindowAt(const QString &id)
{
    m_jumpWindow = id;
    dropLoaded();
    m_window = true;
    emit windowChanged();
    // One server id back, not id - 1: TDLib's ids keep a type in their low 20 bits, and id - 1 is no valid id (its history
    // would start elsewhere). The page after it starts at the message, as fetch_newer drops anything not newer.
    m_readInbox = id.toLongLong() - (Q_INT64_C(1) << 20);
    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }
    askWindow();
    m_windowGiveUp.start();
    qDebug() << "chat: window at" << id;
}

void MessageModel::releaseOlder()
{
    if (!m_holdOlder)
        return;
    const QStringList old = keys();
    m_holdOlder = false;
    m_edgeSync.start(); // the held rows are in now: the loading row may be due (the refresh below already counts it)
    refresh(old, QSet<QString>()); // appended at the model's end, above the divider: the rows on screen stay
    qDebug() << "chat: older rows released," << old.size() << "->" << m_entries.size() << "entries";
}

void MessageModel::toLiveEnd()
{
    m_jumpWindow.clear(); // the user went to the newest end: a jump may open a window again
    if (m_window)
        leaveWindow();
}

// The window's first reply never came.
void MessageModel::giveUp()
{
    if (m_window && !m_floorKnown) {
        qDebug() << "chat: no window after" << kWindowGiveUpMs << "ms, opening at the newest";
        leaveWindow();
    }
}

// In a bottom-to-top list the model's end is the top. Until chat.open answers only the first paint
// from the cache shows: paging waits. In window mode the floor comes from chat.newer, and the bottom pages too.
void MessageModel::nearTop(const QVariantList &firstVisible)
{
    if (firstVisible.isEmpty())
        return;
    m_seenFirst = firstVisible.value(0).toInt(); // kept even before the floor is known: pageIfNearTop uses it
    if (!m_floorKnown)
        return;
    const int first = firstVisible.value(0).toInt();
    m_low = first;
    // Nothing more in chats.db and the engine's page is on its way: every scroll would read nothing.
    const bool waiting = m_starved && !m_historyId.isEmpty() && m_historyAt.elapsed() < kHistoryMs;
    // A short budgeted first page is "near its top" at once: loadRest reads that page, not a scroll.
    if (!m_restPending && !atStart() && !waiting && first >= m_entries.size() - kNearTop)
        loadPage();
    // After the top: an older page appends at the model's end and leaves `first` right, a newer one shifts it.
    // One page is 50 entries, more than kNearTop, so this can't cascade either.
    // 20, not kNearTop: if `first` is the visual top's entry rather than the bottom's, the bottom shows it at
    // about a screen (13 cards) and kNearTop would never be reached. Early is harmless; never is not.
    if (m_window && first <= kNearBottom)
        loadNewer();
}

// The newest message the screen shows when entry `first` is its bottom-most: a header or the divider walks up
// to the message above it (older, also on screen); an album gives its newest member. "" when none. A local row has no id
// Telegram knows (chat.read's upto): it walks up past it too.
QString MessageModel::shownUpTo(int first) const
{
    for (int i = qMax(first, 0); i < m_entries.size(); ++i) {
        const Entry &e = m_entries[i];
        if (e.msg < 0)
            continue;
        const QString id = e.members.isEmpty() ? m_all[e.msg].id : m_all[e.members.last()].id;
        if (!isLocal(id))
            return id;
    }
    return QString();
}

// The entry index of a message to jump to, -1 when it isn't shown. One older than what is loaded, and in the cache above
// the floor, brings in every message back to its day first. Search, pins and notifications reach anywhere: a message the
// loaded rows and the verified cache don't hold (below the floor, or the floor not known yet, or the model in another
// window) opens a window of history at it (openWindowAt): -1 now, the model loading meanwhile, and the page jumps once
// it is in (ChatPage.loaded). A window opened at the message that still doesn't show it means the message is gone: -1
// again, and no new window.
// ponytail: a match far back loads every message in between, one itemAdded each; a window around
// the match if that is slow.
int MessageModel::reveal(const QString &id)
{
    releaseOlder(); // a jump may be to a message the divider holds back
    const int at = entryOf(id);
    if (at >= 0)
        return at;
    if (id == m_jumpWindow)
        return -1;
    if (m_window) {
        openWindowAt(id);
        return -1;
    }
    if (atStart() || m_all.isEmpty())
        return at;
    if (m_floorKnown) {
        const QList<Msg> target = select("AND id = ?", QVariantList() << id, 1);
        if (!target.isEmpty() && target.first().rowid >= m_floor) {
            // Every row from the match's day to the oldest loaded one, so there is no gap.
            const Msg o = m_all.first();
            const uint midnight =
                QDateTime(QDateTime::fromTime_t(target.first().ts).date(), QTime(0, 0)).toTime_t();
            merge(QStringList(), select("AND ts >= ? AND (ts < ? OR (ts = ? AND CAST(id AS INTEGER) < ?))"
                                        " AND CAST(id AS INTEGER) >= ?",
                                        QVariantList() << midnight << o.ts << o.ts << o.rowid << m_floor, -1), true);
            return entryOf(id);
        }
    }
    openWindowAt(id);
    return -1;
}

void MessageModel::unspoil(const QString &id)
{
    m_revealed.insert(id);
    onMsgs(m_chat, QStringList(id));
}

// PreviewMaker made the picture files of these rows, in any chat: the loaded rows of this one are read again and take the
// file in place of their inline thumbnail. Not through onMsgs' check for loading, which is for rows not read yet (the first
// page reads them with the file there): these are read already.
void MessageModel::onPreviews(const QStringList &ids)
{
    // A sender's round picture is made (roundPhotoSoon asked for it): let the next call look at the file again, and draw
    // the cards that show that picture. The card gets the sender's photo from m_senders when it has one, else the row's own
    // (data() above), so both are compared.
    foreach (const QString &id, ids) {
        if (!id.startsWith("round:"))
            continue;
        const QString path = id.mid(6);
        roundForget(path);
        for (int i = 0; i < m_entries.size(); ++i) {
            const Entry &e = m_entries[i];
            if (e.msg < 0 || !e.showTile)
                continue;
            const QVariantMap &d = m_all[e.msg].data;
            const QHash<QString, Sender>::const_iterator it = m_senders.constFind(d.value("senderId").toString());
            const QString shown = it != m_senders.constEnd() && !it->photo.isEmpty() ? it->photo
                                                                                    : d.value("senderPhoto").toString();
            if (shown == path) {
                emit itemUpdated(QVariantList() << i);
            }
        }
    }
    const QSet<QString> made = QSet<QString>::fromList(ids);
    QStringList mine;
    foreach (const Msg &m, m_all) {
        if (made.contains(m.id))
            mine << m.id;
    }
    if (!mine.isEmpty())
        reread(mine);
}

void MessageModel::onMsgs(const QString &chat, const QStringList &ids)
{
    if (chat != m_chat || m_loading) // the first page reads it anyway
        return;
    reread(ids);
}

void MessageModel::reread(const QStringList &ids)
{
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
    // A local row waited on a request the old engine may never have had: its real row, if there is one, comes as a new row.
    dropLocals();
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
    m_askedMaps.clear(); // a location's map too
    m_stickerAsks.clear();
    m_stickerFailed.clear(); // and a sticker's that failed
    m_refetch.clear();
    setAsked(m_historyId, QString()); // so is a chat.history
    m_bridgeId.clear(); // and a bridge: the chat.open that follows checks the gap again
    setAsked(m_newerId, QString()); // and a chat.newer
    m_senderAsks.clear(); // and a senders.get: the rows read again below collect the senders still unknown
    m_sendersAsked = QSet<QString>::fromList(m_senders.keys());
    m_askedTr.clear(); // and a translation
    m_toTranslate.clear();
    if (!m_translating.isEmpty()) {
        m_translating.clear();
        emit translatingChanged();
    }
    m_retries = 0;
    m_retry.stop();
    const bool had = !m_all.isEmpty();
    reload();
    if (had && m_window) {
        askOlder(); // takeOpen, which asks it after a reconnect, returns early in window mode
        askNewer();
    }
}

// The chat's row changed: when its language did, the rows are read again, in the new one.
void MessageModel::onChats(const QStringList &jids)
{
    if (m_loading || !jids.contains(m_chat))
        return;
    const QString now = readTranslate();
    if (now == m_translate)
        return;
    m_translate = now;
    emit translateChanged();
    m_askedTr.clear();
    m_toTranslate.clear();
    // The requests still with Telegram are for the language just left: nothing of theirs is shown, so they no longer
    // count as translating (the title bar said so for as long as Telegram took, the user, 2026-11-21), and a late
    // failure of theirs is not this language's.
    if (!m_translating.isEmpty()) {
        m_translating.clear();
        emit translatingChanged();
    }
    m_retries = 0;
    m_retry.stop();
    reload();
}

// A translation failed. Its ids are asked again by whatever reads those rows next: the rows the engine wrote before
// failing come back through its msgs events with their tr_lang, so they are not queued. A failure never asks again
// by itself: only the one timer (60 s, which the engine's own pause outlasts or nearly) re-reads the loaded rows.
void MessageModel::onFailed(const QString &id, const QString &cmd, const QString &code)
{
    if (m_stickerAsks.contains(id)) {
        const QString msg = m_stickerAsks.take(id);
        // The engine going away is no failure: onConnected asks again.
        if (code != "no_engine") {
            m_stickerFailed.insert(msg);
            reread(QStringList(msg));
        }
        return;
    }
    if (cmd != "msg.translate" || !m_translating.contains(id))
        return;
    foreach (const QString &m, m_translating.take(id))
        m_askedTr.remove(m);
    if (m_translating.isEmpty())
        emit translatingChanged();
    emit translateStalled(code);
    // ponytail: a fixed 60 s, 5 times; the engine's pause doubles up to 15 min, so after the fifth the next page or reconnect asks again.
    if ((code == "rate_limited" || code == "offline" || code == "no_engine") && m_retries < 5 && !m_retry.isActive())
        m_retry.start(60000);
}

void MessageModel::retryTranslate()
{
    if (m_loading || !m_engine || !m_engine->connected())
        return;
    ++m_retries;
    reload();
}

// Every loaded row again, or the first page when none is.
void MessageModel::reload()
{
    if (m_all.isEmpty()) {
        loadPage();
        return;
    }
    const Msg o = m_all.first();
    QStringList asked;
    foreach (const Msg &m, m_all) {
        if (!isLocal(m.id)) // a local row waits for the engine's own; it is not read again here
            asked << m.id; // one that isn't read again is gone
    }
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
}

// Puts rows into m_all and tells the list. asked: the ids that were read again; fresh: the
// rows that came back. A loaded asked id that isn't in fresh is gone.
// m takes old's place in the list (a local row's, or a temporary id's): it sorts where old did, and keeps that from now on.
void MessageModel::keepPlace(Msg &m, const Msg &old)
{
    const QPair<qint64, int> at = m_place.contains(old.id) ? m_place.value(old.id) : qMakePair(old.ts, old.day);
    m.ts = at.first;
    m.day = at.second;
    m_place.insert(m.id, at);
}

// A row read again (a tick, an edit) sorts where it was kept, not at the time the server gave it.
void MessageModel::placed(Msg &m) const
{
    if (m_place.contains(m.id)) {
        const QPair<qint64, int> at = m_place.value(m.id);
        m.ts = at.first;
        m.day = at.second;
    }
}

void MessageModel::merge(const QStringList &asked, const QList<Msg> &fresh, bool older)
{
    QHash<QString, Msg> back;
    foreach (const Msg &m, fresh)
        back.insert(m.id, m);
    const QSet<QString> was = QSet<QString>::fromList(asked);

    QSet<QString> loaded, updated;
    QList<Msg> all;
    QStringList sending; // ours that were sending and are gone: Telegram gave them their own ids
    foreach (const Msg &m, m_all) {
        loaded.insert(m.id);
        if (!was.contains(m.id))
            all.append(m);
        else if (back.contains(m.id)) {
            Msg again = back.value(m.id);
            placed(again);
            all.append(again);
            updated.insert(m.id);
        } else if (m.data["out"].toBool() && m.data["state"].toInt() == 0)
            sending << m.id;
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
        // A row of ours that a local row stands for (addLocal) takes its place: the entry stays where it is under the new id
        // and is told itemUpdated, so the card doesn't blink and the page doesn't slide a second time (no newestComing, no
        // itemAdded). Before the gate below, which is for rows older than the loaded ones: this one is what the local row was
        // waiting for. After `sending`'s turn: a temporary id and its final one are one message whatever its words, and it has
        // the entry already. Not `incoming`: it is ours.
        // The engine names both exactly (the user's burst of sends, 0.1.0.77: one event held one message's temporary -> final
        // swap and the next one's new row, and pairing in order swapped them, moved the list and left duplicates):
        // - a sent row's final id names its temporary one (extra.was): that entry, renamed, whatever else the event holds;
        // - a new row of ours names the local row it was sent from (extra.local): that one.
        // The in-order pairings below are for rows without the names (an engine from before them).
        if (!older && m.data["out"].toBool()) {
            const QString was = m.data.value("wasId").toString();
            if (!was.isEmpty() && sending.contains(was)) {
                sending.removeOne(was);
                Msg done = m;
                foreach (const Msg &old, m_all) {
                    if (old.id == was) {
                        keepPlace(done, old);
                        break;
                    }
                }
                all.append(done);
                renameEntry(was, m.id);
                updated.insert(m.id);
                continue;
            }
            const bool named = !m.data.value("sentAs").toString().isEmpty();
            if ((named || sending.isEmpty()) && takeLocal(m, all)) {
                updated.insert(m.id);
                continue;
            }
        }
        // A new message older than the loaded ones comes with a later page.
        if (older || empty || atStart() || m.ts > m_all.first().ts
            || (m.ts == m_all.first().ts && m.rowid > m_all.first().rowid)) {
            all.append(m);
            // Only loadPage passes older (the first load and older pages): anything else that is
            // new arrived live, even into a chat that opened empty.
            if (!older && !m.data["out"].toBool())
                incoming = true;
            // A sent message's row under its final id (the engine names both ids in one event): it keeps the
            // sending row's entry, so the list updates that card instead of dropping it and adding a new one,
            // which blinked. Paired in order, as they were sent.
            if (!sending.isEmpty() && m.data["out"].toBool()) {
                const QString from = sending.takeFirst();
                foreach (const Msg &old, m_all) {
                    if (old.id == from) {
                        keepPlace(all.last(), old);
                        break;
                    }
                }
                renameEntry(from, m.id);
                updated.insert(m.id);
            }
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

// The entry of message `from` is `to`'s from now on. merge calls it before it takes the list's keys, so the list sees the same
// key where it was and the change is an update of that card.
void MessageModel::renameEntry(const QString &from, const QString &to)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        Entry &e = m_entries[i];
        if (e.key == from)
            e.key = to;
        else if (e.key == "album:" + from)
            e.key = "album:" + to;
    }
}

// A row of ours, new to the list, in the place of the oldest local row that has the same words (the first that has: one
// whose request was lost stays until it expires and doesn't hold the ones after it). The local row leaves `all`, m joins it and
// the entry is renamed; m is given the local id as `local`, which the card of the row that is sliding in knows it by (MessageCard's
// reportSlide), since the engine's row can come before the card has been measured. False when none matches (or the one that
// did was no longer in `all`: the model was cleared under it): then m is a new row as any other.
// Done in merge, before it takes the keys, and not as a removal and an addition: the same position, the same card, an itemUpdated.
bool MessageModel::takeLocal(const Msg &m, QList<Msg> &all)
{
    if (m_locals.isEmpty())
        return false;
    // The engine names the local row the message was sent from (msg.send's local, extra.local): that one and no other, so two
    // quick sends of the same words can't swap (the user, 2026-11-20). A tagged row whose local row is gone (expired, dropped)
    // is a new row. The words are matched only for a row with no tag (an engine from before the tag).
    const QString sentAs = m.data.value("sentAs").toString();
    if (m.kind != 0 && sentAs.isEmpty())
        return false;
    const QString said = words(m.data["text"].toString());
    for (int i = 0; i < m_locals.size(); ++i) {
        if (sentAs.isEmpty() ? m_locals[i].text != said : m_locals[i].id != sentAs)
            continue;
        const QString local = m_locals.takeAt(i).id;
        int at = -1;
        for (int j = 0; j < all.size() && at < 0; ++j) {
            if (all[j].id == local)
                at = j;
        }
        if (at < 0) { // the model was cleared under it: forgotten, and the next local row with these words has its turn
            --i;
            continue;
        }
        // Another card type than the local row's, which an itemUpdated wouldn't change: the local row goes (itemRemoved) and m
        // is a new row (itemAdded). A one-emoji text's local sticker that comes back as a text (TDLib has no animated sticker
        // of that emoji), or a text's local row that comes back as a sticker.
        if (m.kind != all.at(at).kind) {
            all.removeAt(at);
            return false;
        }
        Msg taken = m;
        keepPlace(taken, all.at(at));
        all.removeAt(at);
        taken.data["local"] = local;
        all.append(taken);
        renameEntry(local, m.id);
        return true;
    }
    return false;
}

// A text of ours at the bottom of the list at once (the class comment). The row is the newest by every key the sort has: its
// time is the clock's, or the newest row's when the phone's clock is behind Telegram's, and its rowid the greatest, which also
// keeps dropBelowFloor from taking it in a chat that opened empty (kNoFloor). The list is told as for a new row: refresh
// emits newestComing and itemAdded([0]), and a day's header for a new day, as rebuild makes it for any row.
QString MessageModel::addLocal(const QString &text, const QString &quoteId, bool sticker)
{
    const QString said = words(text);
    // Not loading: the first page reads the rows from chats.db, and a row put in before it would meet that page's merge.
    if (m_window || m_loading || said.isEmpty())
        return QString();
    const qint64 ts = qMax<qint64>(QDateTime::currentDateTime().toTime_t(), m_all.isEmpty() ? 0 : m_all.last().ts);
    const QString id = QString("local-%1").arg(++m_localCount);
    Msg m = localRow(id, text.trimmed(), quoteId, ts, sticker);
    if (m.id.isEmpty())
        return QString();
    m.rowid = kNoFloor;
    m.data["local"] = id;
    Local l;
    l.id = id;
    l.text = said;
    l.at = QDateTime::currentMSecsSinceEpoch();
    const QStringList old = keys();
    m_all.append(m);
    m_locals.append(l);
    if (!m_localExpiry.isActive())
        m_localExpiry.start();
    refresh(old, QSet<QString>());
    return id;
}

void MessageModel::dropLocal(const QString &id)
{
    removeLocals(QStringList(id));
}

void MessageModel::dropLocals()
{
    QStringList ids;
    foreach (const Local &l, m_locals)
        ids << l.id;
    removeLocals(ids);
}

// Each is an itemRemoved (and its day's header, when it was the day's only row), through refresh as hide() does.
void MessageModel::removeLocals(const QStringList &ids)
{
    QList<Msg> kept;
    foreach (const Msg &m, m_all) {
        if (!ids.contains(m.id))
            kept.append(m);
    }
    for (int i = m_locals.size() - 1; i >= 0; --i) {
        if (ids.contains(m_locals[i].id))
            m_locals.removeAt(i);
    }
    if (kept.size() == m_all.size())
        return; // gone already: the engine's row took its place
    const QStringList old = keys();
    m_all = kept;
    refresh(old, QSet<QString>());
}

// The local rows nothing took in kLocalMs go. The timer stops with the last of them.
void MessageModel::tickLocal()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList gone;
    foreach (const Local &l, m_locals) {
        if (now - l.at >= kLocalMs)
            gone << l.id;
    }
    if (!gone.isEmpty())
        qDebug() << "chat:" << gone.size() << "local row(s) not taken by the engine's after" << kLocalMs << "ms";
    removeLocals(gone);
    if (m_locals.isEmpty())
        m_localExpiry.stop();
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
        if (!now.isEmpty() && !QSet<QString>::fromList(old).contains(now[0]))
            emit newestComing();
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
    foreach (int at, touched) {
        emit itemUpdated(QVariantList() << at);
    }
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
    // m_all is oldest first: the visible rows become units (a message, or Telegram's album),
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
        r.kind = m.kind;
        r.deleted = m.data["deleted"].toBool();
        r.spoiler = m.data["spoiler"].toBool();
        r.album = m.data["albumId"].toString().toUtf8().constData();
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
    // The loading row, the list's last entry (its top): an older page is awaited (edgeWaiting). No message behind it (msg < 0),
    // so markRuns and runOf treat it as a header: it is no one's, and it ends the run of the oldest message below it. Not
    // on an empty list: EmptyNote has its own spinner.
    if (!m_entries.isEmpty() && edgeWaiting()) {
        Entry l;
        l.key = "loading";
        l.msg = -1;
        m_entries.append(l);
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
