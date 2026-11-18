#include "MessageModel.h"

#include <bb/cascades/Image>

#include "ChatsDb.h"
#include "EngineLink.h"
#include "MediaData.h"
#include "MessageKinds.h"
#include "Sticker.h"
#include "colors.h"
#include "dates.h"
#include "Waveform.h"

#include <bb/utility/i18n/Formatting>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QSqlDatabase>
#include <QSqlError>

using namespace bb::utility::i18n;

// The chats.db side of MessageModel, out of MessageModel.cpp to keep that file short: the
// connection, the query and the reading of one row into a Msg.

// The connection ChatListModel opens, read-only (ChatsDb): only the engine writes chats.db.
static const char *const kDb = "chats";

// The three QUOTED are the quoted message's out, sender and sender_name, by its primary key. The
// reactions are in extra (docs/protocol.md), read in read() below.
#define QUOTED(col) "(SELECT " col " FROM messages q WHERE q.chat = messages.chat AND q.id = messages.quote_id)"
static const char *const kColumns = "CAST(id AS INTEGER), id, ts, out, state, sender, sender_name, kind, "
                                    "text, quote_preview, edited, deleted, "
                                    QUOTED("out") ", " QUOTED("sender") ", " QUOTED("sender_name") ", duration, waveform, media_state, played, "
                                    "thumb, width, height, size, path, extra, starred, quote_id";
#undef QUOTED

MessageModel::Msg MessageModel::read(QSqlQuery &q)
{
    Msg m;
    m.rowid = q.value(0).toLongLong();
    m.id = q.value(1).toString();
    m.ts = q.value(2).toLongLong();
    const QDateTime when = QDateTime::fromTime_t(m.ts);
    m.day = when.date().toJulianDay();
    const bool out = q.value(3).toInt() != 0;
    const int kind = q.value(7).toInt();
    m.kind = kind;
    m.who = q.value(5).isNull() ? q.value(6).toString() : q.value(5).toString();
    m.data["readOnly"] = m_channel; // a channel: no reply, reaction, delete or vote (step 6.6g)
    m.data["album"] = QVariantList(); // MessageModel::albumData fills it for an album; never undefined in QML
    const QString text = q.value(8).toString();
    const bool deleted = q.value(11).toInt() != 0;
    const QLocale &locale = m_locale;
    const QString time = locale.toString(when.time(), m_clock);
    // The wording of kinds 6, 7 and 9 to 14 (kind 6 and 13 are service lines).
    QString label, shown = text;
    const QVariantMap extra = MessageKinds::parse(q.value(24).toString());
    // The cap on the media kept and "Clear media" delete TDLib's files without rewriting the row (docs/protocol.md,
    // "Missing files"): a file that is not there is not downloaded, so the row is read as on the server and the next tap
    // asks media.get for it; a preview that is gone is asked for again below. Stickers are never pruned.
    bool gone = false;
    int mediaState = q.value(17).toInt();
    QString path = q.value(23).toString();
    if (mediaState == 2 && !QFile::exists(path)) {
        mediaState = 1;
        path.clear();
        gone = true;
    }
    QString previewPath = extra.value("preview").toString();
    if (!previewPath.isEmpty() && !QFile::exists(previewPath)) {
        previewPath.clear();
        gone = true;
    }
    m.service = MessageKinds::fill(kind, m_chat, text, extra, out,
                                   q.value(6).toString(), time, locale, m.data, label, shown);
    if (m.service) {
        m.data["text"] = shown;
        m.data["html"] = QString(); // set on every row (below too), so QML never reads undefined
        return m;
    }
    // Photos, videos, stickers, audio files and documents are drawn by MediaBubble: no label, and a
    // document's text (its name) is the bubble's. A caption stays in the text.
    if (kind == 5 && !deleted && mediaState == 1)
        Sticker::fetch(m_engine, m_chat, m.id, m_asked); // a history sticker, asked once per page
    // A photo not downloaded and a video show Telegram's tiny blurred thumbnail until they are opened: ask once for
    // a display-size picture, which comes back as extra.preview. Not for our own message still sending.
    if (((kind == 1 && mediaState != 2) || kind == 2) && !deleted
        && previewPath.isEmpty() && !(out && q.value(4).toInt() == 0)) {
        const QString request = Sticker::fetch(m_engine, m_chat, m.id, m_asked, true);
        // The file comes back at the path the row already has, so the engine writes nothing and sends no msgs event: the
        // row is read again when the answer comes (onReplied).
        if (gone && !request.isEmpty())
            m_refetch.insert(request, m.id);
    }
    // Covered until a tap takes the cover off (unspoil), for as long as this chat stays open.
    const bool covered = m.data["spoiler"].toBool() && !deleted && !m_revealed.contains(m.id);
    m.data["spoiler"] = covered;
    if (MediaData::fill(m.data, m.id, deleted ? 0 : kind, text, q.value(19).toByteArray(),
                        q.value(20).toInt(), q.value(21).toInt(), q.value(22).toLongLong(),
                        q.value(15).toInt(), mediaState, path, previewPath, locale, covered)
        && (kind == 4 || kind == 8))
        shown = QString();
    if (deleted) {
        label = QString();
        shown = tr("This message was deleted");
    }
    QString sender;
    if (m_group && !out)
        sender = q.value(6).toString();

    m.data["id"] = m.id;
    m.data["out"] = out;
    m.data["state"] = q.value(4).toInt();
    m.data["edited"] = q.value(10).toInt() != 0;
    m.data["starred"] = q.value(25).toInt() != 0;
    m.data["deleted"] = deleted;
    // The chips under the card: one per emoji, [{e, n, me}] as the engine wrote them (extra.reactions), and ours.
    QVariantList chips;
    QStringList mine;
    if (!deleted) {
        foreach (const QVariant &v, extra.value("reactions").toList()) {
            const QVariantMap r = v.toMap();
            QVariantMap chip;
            chip["e"] = r.value("e");
            chip["n"] = r.value("n");
            chip["me"] = r.value("me").toBool();
            chips << chip;
            if (r.value("me").toBool())
                mine << r.value("e").toString();
        }
    }
    m.data["reactions"] = chips;
    m.data["myReactions"] = mine;
    // Delete for everyone: ours, sent, and inside WhatsApp's limit of about 2.5 days.
    // ponytail: read when the row is read, so a page left open for days can offer it a little late.
    const int state = q.value(4).toInt();
    m.data["revocable"] = out && !deleted && state >= 1 && state <= 3
                          && m_now - m.ts < 60 * 3600;
    // Edit: our own sent text or caption (a photo's or video's), inside WhatsApp's 15 minutes (the server allows 20).
    m.data["editable"] = !m_secret && out && !deleted && (kind == 0 || ((kind == 1 || kind == 2) && !text.isEmpty())) && state >= 1 && state <= 3
                         && m_now - m.ts < 15 * 60;
    // Forward: what the engine sends again (text, photo, video, voice, document, sticker, audio file); a
    // media row of ours waits for its upload (sending) and isn't one that failed.
    const bool media = kind == 1 || kind == 2 || kind == 3 || kind == 4 || kind == 5 || kind == 8;
    // Not in a secret chat: Telegram does not forward from there (nor edit or react, below and in the card's menu).
    m.data["forwardable"] = !m_secret && !deleted && (kind == 0 || media) && !(out && media && (state == 0 || state == 4));
    m.data["secret"] = m_secret;
    m.data["time"] = time;
    // A self-destructing message: its timer's seconds (none for one that goes after one view), and when it goes once its
    // countdown runs (unix seconds). ttlText is the foot's mark; data() keeps it current from expiresAt.
    const int ttl = extra.value("ttl").toInt();
    const qint64 expires = extra.value("expiresAt").toLongLong();
    m.data["ttl"] = ttl;
    m.data["expiresAt"] = expires;
    m.data["ttlText"] = ttl > 0 ? span(ttl) : QString();
    if (expires > 0 && !deleted && !m_expiry.isActive())
        m_expiry.start();
    // The long-press menu's title: the day in full and the time.
    m.data["stamp"] = longDay(when.date(), locale) + ", " + time;
    m.data["label"] = label;
    const bool voice = kind == 3 && !deleted;
    m.data["voice"] = voice;
    if (voice) {
        m.data["duration"] = q.value(15).toInt();
        m.data["mediaState"] = mediaState;
        m.data["played"] = q.value(18).toInt() != 0;
        m.data["wave"] = Waveform::image(q.value(16).toByteArray());
        // As numbers: bytes could reach QML as a string, cut at the first 0 (a silent bar).
        QVariantList wave;
        foreach (char v, q.value(16).toByteArray())
            wave << int(v);
        m.data["waveData"] = wave;
    }
    m.data["text"] = shown;
    // The same text with its mentions linked, "" for none (MessageKinds::html).
    m.data["html"] = MessageKinds::html(shown, extra);
    m.data["albumId"] = extra.value("album").toString(); // Telegram's album (Albums.h), "" for none
    m.data["quote"] = q.value(9).toString();
    m.data["quoteId"] = q.value(26).toString(); // a tap on the quote jumps to it
    m.data["sender"] = sender;
    // Who sent it, by id: the runs of one person's messages are made of it, and so is the tile beside them. Only an
    // incoming message in a group has one; a service line or a notice (kinds 6, 13) is no one's, so it breaks a run.
    QString senderId;
    if (m_group && !out && kind != 6 && kind != 13 && !q.value(5).isNull())
        senderId = q.value(5).toString();
    m.data["senderId"] = senderId;
    m.data["senderInitials"] = initials(sender);
    // The colour of a name the engine hasn't answered for yet: a hash of the id, over the same seven colours. data()
    // swaps in the sender's own once senders.get has said it.
    QString color;
    if (!sender.isEmpty() || !senderId.isEmpty())
        color = pickColor(senderId.isEmpty() ? sender : senderId, kSenders, kSenderCount);
    m.data["senderColor"] = color;
    // A positive id is a user, which is all senders.get knows: a chat that posts as itself has a negative one.
    if (!senderId.isEmpty() && !senderId.startsWith('-') && !m_senders.contains(senderId)
        && !m_sendersAsked.contains(senderId))
        m_sendersWanted.insert(senderId);
    // In groups a quote says whose message it was, in that person's colour; ours is "You", in
    // the primary colour (quoteColor ""). ponytail: only when the quoted message is in
    // chats.db; store ContextInfo's participant (a quote_sender column) if older quotes need it.
    QString quoteName, quoteColor;
    if (m_group && !q.value(9).toString().isEmpty() && !q.value(12).isNull()) {
        if (q.value(12).toInt() != 0) {
            quoteName = tr("You");
        } else if (!q.value(14).toString().isEmpty()) {
            quoteName = q.value(14).toString();
            const QString key = q.value(13).isNull() ? quoteName : q.value(13).toString();
            quoteColor = pickColor(key, kSenders, kSenderCount);
        }
    }
    m.data["quoteName"] = quoteName;
    m.data["quoteColor"] = quoteColor;
    // A reply to a story, or a like on one of ours (docs/protocol.md, Story quotes): the quote shows the story's picture
    // and a tap on it plays the story. An Image even when empty, so a recycled card never keeps another row's.
    bool storyQuote = false;
    QString storyPoster;
    QByteArray storyThumb;
    if (!q.value(9).toString().isEmpty() && extra.value("story").toBool()) {
        storyQuote = true;
        storyPoster = extra.value("sposter").toString();
        storyThumb = QByteArray::fromBase64(extra.value("qthumb").toString().toLatin1());
    }
    m.data["quoteStory"] = storyQuote;
    m.data["storyPoster"] = storyPoster;
    m.data["hasQuoteThumb"] = !storyThumb.isEmpty();
    m.data["quoteThumb"] = QVariant::fromValue(storyThumb.isEmpty() ? bb::cascades::Image() : bb::cascades::Image(storyThumb));
    return m;
}

// A day's long name is asked once, not once per message of that day.
QString MessageModel::longDay(const QDate &date, const QLocale &locale)
{
    const int day = date.toJulianDay();
    QHash<int, QString>::const_iterator it = m_longDays.constFind(day);
    if (it != m_longDays.constEnd())
        return it.value();
    const QString label = capitalized(locale.toString(date, QLocale::LongFormat));
    m_longDays.insert(day, label);
    return label;
}

// The read-only connection, opened on first use. Not open before the first link: the engine
// hasn't made chats.db yet.
static QSqlDatabase database()
{
    return ChatsDb::open(kDb);
}

// Binds the chat of a query that has `chat = ?` and topicSql() after it.
void MessageModel::bindChat(QSqlQuery &q) const
{
    q.addBindValue(m_chat);
    if (m_topic)
        q.addBindValue(m_topic);
}

// The chat's unread count in chats.db, 0 when it has no row (a contact with no chat yet). A topic's is its row's in topics.
int MessageModel::unread() const
{
    QSqlDatabase db = database();
    if (!db.isOpen())
        return 0;
    QSqlQuery q(db);
    q.prepare(m_topic ? "SELECT unread FROM topics WHERE chat = ? AND topic = ?" : "SELECT unread FROM chats WHERE jid = ?");
    bindChat(q);
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

// More unread messages than this are not loaded back to the first one: that would hold the UI thread too
// long (about 3 ms a row). They open in a window around the first unread instead (findUnread).
static const int kUnreadMax = 150;

// Where the chat opens. A chat with unread messages (chats.unread) opens at the first one that came after
// chats.read_inbox. If chats.db holds every unread message and the rows from it to the newest are no more
// than kUnreadMax, loadFirst loads back to it and chat.open verifies that far (openLimit). Otherwise (the cache
// lacks unread rows, so it has gaps there, or there are too many) the model goes into window mode: it shows
// one verified page after read_inbox, chat.newer (askWindow), and pages newer and older from it (the class
// comment). Run once, by the constructor: chats.db is not open at the first launch, and then none of this
// applies. read_inbox is 0 when unknown (an old database, until the engine next writes the row): the chat
// opens at the newest, as one with nothing unread does.
void MessageModel::findUnread()
{
    QSqlDatabase db = database();
    if (!db.isOpen())
        return;
    QSqlQuery q(db);
    q.prepare(m_topic ? "SELECT read_inbox, unread FROM topics WHERE chat = ? AND topic = ?"
                      : "SELECT read_inbox, unread FROM chats WHERE jid = ?");
    bindChat(q);
    if (!q.exec() || !q.next())
        return;
    const qint64 readInbox = q.value(0).toLongLong();
    const int unread = q.value(1).toInt();
    if (unread <= 0 || readInbox <= 0)
        return;
    // The cache must hold every unread message: one that TDLib announced only by its last message has
    // the newest alone, and the divider would sit above that one while `unread` says 20.
    QSqlQuery have(db);
    have.prepare(QString("SELECT count(*), min(CAST(id AS INTEGER)) FROM messages WHERE chat = ?%1 AND out = 0 "
                         "AND CAST(id AS INTEGER) > ?").arg(topicSql()));
    bindChat(have);
    have.addBindValue(readInbox);
    if (!have.exec() || !have.next())
        return;
    if (have.value(0).toInt() < unread) {
        m_window = true;
        m_readInbox = readInbox;
        return;
    }
    const qint64 first = have.value(1).toLongLong();
    // Every row from it to the newest, ours included: what loadFirst reads and chat.open must verify.
    QSqlQuery count(db);
    count.prepare(QString("SELECT count(*) FROM messages WHERE chat = ?%1 AND CAST(id AS INTEGER) >= ?").arg(topicSql()));
    bindChat(count);
    count.addBindValue(first);
    if (!count.exec() || !count.next())
        return;
    if (count.value(0).toInt() > kUnreadMax) {
        m_window = true;
        m_readInbox = readInbox;
        return;
    }
    m_unreadId = QString::number(first);
    m_openLimit = qMax(50, count.value(0).toInt() + 10);
}

// The times select would read, with no row built: what deciding a page's size needs.
QList<qint64> MessageModel::pageTimes(const QString &where, const QVariantList &args, int limit)
{
    QList<qint64> times;
    QSqlDatabase db = database();
    if (!db.isOpen())
        return times;
    QSqlQuery q(db);
    q.prepare(QString("SELECT ts FROM messages WHERE chat = ?%3 %1 ORDER BY ts DESC, CAST(id AS INTEGER) DESC LIMIT %2")
                  .arg(where).arg(limit).arg(topicSql()));
    bindChat(q);
    foreach (const QVariant &a, args)
        q.addBindValue(a);
    if (q.exec()) {
        while (q.next())
            times << q.value(0).toLongLong();
    }
    return times;
}

// Runs the messages query of this chat: "where" adds conditions on top of chat = ?, args are
// bound after it. Newest first, then reversed to the order the list shows. oldestFirst takes the oldest
// `limit` rows instead (the next page after a row), and returns them in the same order.
QList<MessageModel::Msg> MessageModel::select(const QString &where, const QVariantList &args,
                                              int limit, bool oldestFirst)
{
    QList<Msg> rows;
    QSqlDatabase db = database();
    if (!db.isOpen())
        return rows;
    m_locale = m_region.locale();
    m_clock = timeFormat(m_locale, DateFormat::Short);
    m_now = QDateTime::currentDateTime().toTime_t();
    QSqlQuery q(db);
    q.prepare(QString("SELECT %1 FROM messages WHERE chat = ?%5 %2 ORDER BY ts %3, CAST(id AS INTEGER) %3 "
                      "LIMIT %4").arg(kColumns, where, oldestFirst ? "ASC" : "DESC").arg(limit).arg(topicSql()));
    bindChat(q);
    foreach (const QVariant &a, args)
        q.addBindValue(a);
    if (!q.exec()) {
        qWarning() << "messages:" << q.lastError().text();
        return rows;
    }
    while (q.next()) {
        if (oldestFirst)
            rows.append(read(q));
        else
            rows.prepend(read(q));
    }
    return rows;
}
