#include "MessageModel.h"

#include <bb/cascades/Image>

#include "BigEmoji.h"
#include "ChatsDb.h"
#include "EngineLink.h"
#include "MediaData.h"
#include "MessageKinds.h"
#include "Pins.h"
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
// reactions are in extra (docs/protocol.md), read in read() below. The last two columns are the sender's colour and small
// picture from `people` (the query joins it to messages on the sender's id: NULL for no sender, or one the engine hasn't
// written yet). Then the quoted sender's colour from `people`, for the quote's name. Then come the message's
// translation and the language it is in (docs/protocol.md, Translation): read() shows it in a chat with a language. Then the
// quoted message's kind, for the quote's line when it has no text (a photo with no caption).
#define QUOTED(col) "(SELECT " col " FROM messages q WHERE q.chat = messages.chat AND q.id = messages.quote_id)"
static const char *const kColumns = "CAST(id AS INTEGER), id, ts, out, state, sender, sender_name, kind, "
                                    "text, quote_preview, edited, deleted, "
                                    QUOTED("out") ", " QUOTED("sender") ", " QUOTED("sender_name") ", duration, waveform, media_state, played, "
                                    "thumb, width, height, size, path, extra, starred, quote_id, people.color, people.photo, "
                                    "(SELECT color FROM people WHERE people.user = " QUOTED("sender") "), "
                                    "messages.tr, messages.tr_lang, " QUOTED("kind");
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
    m.data["readOnly"] = m_channel; // a channel: no reply, reaction or delete (step 6.6g); its polls take votes
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
        m.data["canTranslate"] = false; // nor does the menu's Translate read undefined
        m.data["wantTr"] = false;
        m.data["translated"] = false;
        m.data["original"] = QString();
        return m;
    }
    // Photos, videos, stickers, audio files and documents are drawn by MediaBubble: no label, and a
    // document's text (its name) is the bubble's. A caption stays in the text.
    if (kind == 5 && !deleted && mediaState == 1 && !isLocal(m.id)) {
        const QString request = Sticker::fetch(m_engine, m_chat, m.id, m_asked); // a history sticker, asked once per page
        if (!request.isEmpty())
            m_stickerAsks.insert(request, m.id);
    }
    // A sticker whose picture won't come (no file, its download failed, or it doesn't decode) shows its word; one whose picture
    // is on its way, a spinner in the sticker's square (MediaBubble; the user, 2026-11-26).
    m.data["stickerFailed"] = kind == 5
        && (mediaState == 0 || m_stickerFailed.contains(m.id) || (mediaState == 2 && Sticker::pictureFailed(path)));
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
    // A location's map (docs/protocol.md, kind 9): the engine's 640 x 320 PNG with the point at its centre, asked once for
    // each point the row has had, which comes back as extra.preview. Not for a local row or our own still sending.
    // ponytail: every move of a live location in a loaded chat fetches a map (about 50 KB); a floor of N seconds between asks
    // per message if a busy live location shows in the data use.
    if (kind == 9 && !deleted) {
        if (previewPath.isEmpty() && m_engine && !isLocal(m.id) && !(out && q.value(4).toInt() == 0)) {
            const QString key = m.id + "@" + QString::number(extra.value("lat").toDouble(), 'f', 6) + ","
                                + QString::number(extra.value("lon").toDouble(), 'f', 6);
            if (!m_askedMaps.contains(key)) {
                QVariantMap args;
                args["chat"] = m_chat;
                args["id"] = m.id;
                args["preview"] = true;
                const QString request = m_engine->requestQuiet("media.get", args);
                if (!request.isEmpty()) {
                    m_askedMaps.insert(key);
                    // As a photo's: a file that was gone comes back where the row says, so no msgs event; the reply reads it.
                    if (gone)
                        m_refetch.insert(request, m.id);
                }
            }
        }
        // No blink while a live location moves: its row comes again with the new point and no map until the engine has that
        // point's, so the card keeps the last map it showed, and its pin, meanwhile.
        if (!previewPath.isEmpty())
            m_lastMap.insert(m.id, previewPath);
        else if (m_lastMap.contains(m.id) && QFile::exists(m_lastMap.value(m.id)))
            previewPath = m_lastMap.value(m.id);
    }
    // Covered until a tap takes the cover off (unspoil), for as long as this chat stays open.
    const bool covered = m.data["spoiler"].toBool() && !deleted && !m_revealed.contains(m.id);
    m.data["spoiler"] = covered;
    const bool filled = MediaData::fill(m.data, m.id, deleted ? 0 : kind, text, q.value(19).toByteArray(),
                        q.value(20).toInt(), q.value(21).toInt(), q.value(22).toLongLong(),
                        q.value(15).toInt(), mediaState, path, previewPath, locale, covered);
    if (filled && (kind == 4 || kind == 8))
        shown = QString();
    if (deleted) {
        label = QString();
        shown = tr("This message was deleted");
        m.data["live"] = false; // data() words no live label over the note, and the tick passes it by
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
    // Translate: texts and captions, not in a secret chat. An incoming one in a chat with a language shows its translation
    // once the engine has written it for that language; until then it wants one (wantTr), which is asked for when the
    // list draws the row (data()).
    const QString trLang = q.value(31).toString();
    const bool canTranslate = !deleted && !m_secret && (kind == 0 || kind == 1 || kind == 2) && !text.isEmpty();
    const bool translated = canTranslate && !out && !m_translate.isEmpty() && trLang == m_translate
                            && !q.value(30).toString().isEmpty();
    m.data["wantTr"] = canTranslate && !out && !m_translate.isEmpty() && trLang != m_translate;
    m.data["canTranslate"] = canTranslate;
    m.data["translated"] = translated;
    m.data["original"] = translated ? text : QString();
    if (translated)
        shown = q.value(30).toString();
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
    // A channel post's comments (docs/protocol.md, Comments): how many, 0 too; -1 for a row that has none to show (no
    // discussion group, or deleted), so QML and the card never read undefined. commentsText is the line under the card.
    const int comments = extra.contains("comments") && !deleted ? extra.value("comments").toInt() : -1;
    m.data["comments"] = comments;
    m.data["commentsText"] = comments > 0 ? tr("%n comments", "", comments) : comments == 0 ? tr("Leave a comment") : QString();
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
    // A live location still running: its label follows the clock (tickLive).
    const qint64 liveUntil = m.data.value("liveUntil").toLongLong();
    if (m.data.value("live").toBool() && (liveUntil <= 0 || m_now < liveUntil) && !m_liveTick.isActive())
        m_liveTick.start();
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
    m.data["bigEmoji"] = kind == 0 && !deleted ? bigEmoji(shown) : 0; // 1 to 3 emoji alone: MessageCard draws them big
    // The same text with its mentions linked, "" for none (MessageKinds::html).
    const QString html = MessageKinds::html(shown, extra);
    m.data["html"] = html;
    m.data["albumId"] = extra.value("album").toString(); // Telegram's album (Albums.h), "" for none
    // The engine's quote_preview is the quoted row's text: a photo, sticker or voice note with none says what it is, as a pin
    // does (Pins::summary), so the reply still shows (the user, 2026-11-25: a reply to Jan's photo in BerryFarm had no quote).
    QString quote = q.value(9).toString();
    if (quote.isEmpty() && !q.value(32).isNull())
        quote = Pins::summary(q.value(32).toInt(), QString());
    else if (quote.isEmpty() && extra.contains("qkind"))
        quote = Pins::summary(extra.value("qkind").toInt(), QString());
    m.data["quote"] = quote;
    m.data["quoteId"] = q.value(26).toString(); // a tap on the quote jumps to it
    m.data["sender"] = sender;
    // Who sent it, by id: the runs of one person's messages are made of it, and so is the tile beside them. Only an
    // incoming message in a group has one; a service line or a notice (kinds 6, 13) is no one's, so it breaks a run.
    QString senderId;
    if (m_group && !out && kind != 6 && kind != 13 && !q.value(5).isNull())
        senderId = q.value(5).toString();
    m.data["senderId"] = senderId;
    m.data["senderInitials"] = initials(sender);
    // The sender's colour and small picture as the engine wrote them in `people` (docs/protocol.md): what senders.get says
    // too, so the chat opens with the colour it keeps. A sender the engine hasn't written yet has a hash of the id, over the
    // same seven colours; data() swaps in the one senders.get says.
    const int own = q.value(27).isNull() ? -1 : q.value(27).toInt();
    QString color;
    if (own >= 0 && own < kSenderCount && !senderId.isEmpty())
        color = QString::fromLatin1(kSenders[own]);
    else if (!sender.isEmpty() || !senderId.isEmpty())
        color = pickColor(senderId.isEmpty() ? sender : senderId, kSenders, kSenderCount);
    m.data["senderColor"] = color;
    // A file TDLib has since deleted is no picture (Clear media); the initials show until senders.get asks for it again.
    QString photo = senderId.isEmpty() ? QString() : q.value(28).toString();
    // Looked for once per path in a select(), not once per row: a group's rows repeat a few senders, and a file check costs
    // about 1 ms on the phone (m_photoThere).
    if (!photo.isEmpty()) {
        QHash<QString, bool>::const_iterator there = m_photoThere.constFind(photo);
        if (there == m_photoThere.constEnd())
            there = m_photoThere.insert(photo, QFile::exists(photo));
        if (!there.value())
            photo.clear();
    }
    if (!senderId.isEmpty())
        m.data["senderPhoto"] = photo; // data() keeps it unless senders.get has a picture of its own
    // A positive id is a user, which is all senders.get knows: a chat that posts as itself has a negative one.
    if (!senderId.isEmpty() && !senderId.startsWith('-') && !m_senders.contains(senderId)
        && !m_sendersAsked.contains(senderId))
        m_sendersWanted.insert(senderId);
    // In groups a quote says whose message it was, in that person's colour; ours is "You", in
    // the primary colour (quoteColor ""). ponytail: only when the quoted message is in
    // chats.db; store ContextInfo's participant (a quote_sender column) if older quotes need it.
    QString quoteName, quoteColor;
    if (m_group && !quote.isEmpty() && !q.value(12).isNull()) {
        if (q.value(12).toInt() != 0) {
            quoteName = tr("You");
        } else if (!q.value(14).toString().isEmpty()) {
            quoteName = q.value(14).toString();
            // Their colour from `people`, as their name's in their own messages (the user, 2026-11-20); the hash when the
            // engine hasn't written them.
            const int known = q.value(29).isNull() ? -1 : q.value(29).toInt();
            const QString key = q.value(13).isNull() ? quoteName : q.value(13).toString();
            quoteColor = known >= 0 && known < kSenderCount ? QString::fromLatin1(kSenders[known])
                                                            : pickColor(key, kSenders, kSenderCount);
        }
    } else if (q.value(12).isNull() && !quote.isEmpty() && !extra.value("qname").toString().isEmpty()) {
        // A reply to a message of another chat (docs/protocol.md, qname and qkind): its sender, in any chat, as the
        // name says which message it is. Not in chats.db here, so the engine's name and the hash of it for the colour.
        quoteName = extra.value("qname").toString();
        quoteColor = pickColor(quoteName, kSenders, kSenderCount);
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

static QSqlDatabase database();

// The language incoming messages are shown in, "" for off (and when chats.db has no such row).
QString MessageModel::readTranslate() const
{
    QSqlDatabase db = database();
    if (!db.isOpen())
        return QString();
    QSqlQuery q(db);
    q.prepare("SELECT translate FROM chats WHERE jid = ?");
    q.addBindValue(m_chat);
    return q.exec() && q.next() ? q.value(0).toString() : QString();
}

// A row the list draws that still wants its translation is queued, once, and the queue goes out 200 ms later, so a
// screenful is one request. Only what is drawn is asked for (the user, 2026-11-21): the list asks data() for the rows on
// screen and the few it keeps ready around them, and a whole page at once kept Telegram busy for over 40 s.
void MessageModel::wantTranslation(const Msg &m)
{
    if (!m.data.value("wantTr").toBool() || m_askedTr.contains(m.id) || m_toTranslate.contains(m.id))
        return;
    m_toTranslate << m.id;
    if (!m_trSoon.isActive())
        m_trSoon.start(200);
}

// What wantTranslation queued goes to the engine in requests of at most 200 ids. The engine writes each translation
// as it lands and says so with a msgs event, which reads the row again, now with its tr_lang: nothing is queued twice. A
// failure forgets the ids (onFailed); one timer retries.
void MessageModel::askTranslate()
{
    QStringList ids = m_toTranslate;
    m_toTranslate.clear();
    while (!ids.isEmpty() && m_engine) {
        const QStringList part = ids.mid(0, 200);
        ids = ids.mid(part.size());
        QVariantMap args;
        args["chat"] = m_chat;
        QVariantList list;
        foreach (const QString &id, part)
            list << id;
        args["ids"] = list;
        args["to"] = m_translate;
        const QString request = m_engine->requestQuiet("msg.translate", args);
        if (request.isEmpty())
            return; // not connected: the link coming back reads the rows again
        m_translating.insert(request, part);
        if (m_translating.size() == 1)
            emit translatingChanged();
        foreach (const QString &id, part)
            m_askedTr.insert(id);
    }
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
    else if (m_thread)
        q.addBindValue(qlonglong(m_thread));
}

// The chat's unread count in chats.db, 0 when it has no row (a contact with no chat yet). A topic's is its row's in topics.
// A comment thread has no row in either: 0.
// ponytail: a thread opens at its newest comment, not at the first unread one; messageThreadInfo.unread_message_count would
// give that.
int MessageModel::unread() const
{
    QSqlDatabase db = database();
    if (!db.isOpen() || m_thread)
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
    if (!db.isOpen() || m_thread) // a thread has no row to read it from (unread())
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
    // The unread run is the chat's newest rows. Both counts below are bounded by the time of the last read message when
    // chats.db has it, so they walk the (chat, ts) index from there, not every row of the chat as CAST(id) alone made them
    // (279 ms to open a busy group on the Classic, 1.0.0.95). Ids and times rise together in a chat. Without that row
    // (deleted, never cached) they count as before.
    qint64 since = 0;
    QSqlQuery at(db);
    at.prepare("SELECT ts FROM messages WHERE chat = ? AND id = ?");
    at.addBindValue(m_chat);
    at.addBindValue(QString::number(readInbox));
    if (at.exec() && at.next())
        since = at.value(0).toLongLong();
    const QString bound = since > 0 ? " AND ts >= ?" : "";
    QSqlQuery have(db);
    have.prepare(QString("SELECT count(*), min(CAST(id AS INTEGER)) FROM messages WHERE chat = ?%1%2 AND out = 0 "
                         "AND CAST(id AS INTEGER) > ?").arg(topicSql(), bound));
    bindChat(have);
    if (since > 0)
        have.addBindValue(since);
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
    count.prepare(QString("SELECT count(*) FROM messages WHERE chat = ?%1%2 AND CAST(id AS INTEGER) >= ?")
                      .arg(topicSql(), bound));
    bindChat(count);
    if (since > 0)
        count.addBindValue(since);
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
// budget (not 0, rows newest first only): the loop stops once the rows kept cost `budget` (kCost*), never inside an album
// (one message: its members after the budget is spent are still kept, and only the first one read is charged). The row that
// ends the page is not read at all (read() asks the engine for senders and previews), so the next page, older than the oldest
// kept row, starts with it.
QList<MessageModel::Msg> MessageModel::select(const QString &where, const QVariantList &args,
                                              int limit, bool oldestFirst, int budget)
{
    QList<Msg> rows;
    QSqlDatabase db = database();
    if (!db.isOpen())
        return rows;
    m_locale = m_region.locale();
    m_clock = timeFormat(m_locale, DateFormat::Short);
    m_now = QDateTime::currentDateTime().toTime_t();
    m_photoThere.clear(); // a sender's picture file is looked for once in a select()
    QSqlQuery q(db);
    q.prepare(QString("SELECT %1 FROM messages LEFT JOIN people ON people.user = messages.sender "
                      "WHERE chat = ?%5 %2 ORDER BY ts %3, CAST(id AS INTEGER) %3 "
                      "LIMIT %4").arg(kColumns, where, oldestFirst ? "ASC" : "DESC").arg(limit).arg(topicSql()));
    bindChat(q);
    foreach (const QVariant &a, args)
        q.addBindValue(a);
    if (!q.exec()) {
        qWarning() << "messages:" << q.lastError().text();
        return rows;
    }
    int spent = 0;
    QString lastAlbum; // the album of the last row kept, "" for none
    for (;;) {
        const bool more = q.next();
        if (!more)
            break;
        // Spent: the page is over unless this row is a further member of the last one's album. Known from extra's text
        // (engine/messages.cpp append_extra writes "album":"<id>") without read(), which has side effects.
        if (budget > 0 && !oldestFirst && spent >= budget
            && (lastAlbum.isEmpty() || !q.value(24).toString().contains("\"album\":\"" + lastAlbum + "\"")))
            break;
        const Msg row = read(q);
        if (budget > 0 && !oldestFirst) {
            const QString album = row.data.value("albumId").toString();
            if (album.isEmpty() || album != lastAlbum)
                spent += (row.kind == 1 || row.kind == 2 || row.kind == 9) ? kCostPicture
                         : row.kind == 5 ? kCostSticker : kCostText;
            lastAlbum = album;
        }
        if (oldestFirst)
            rows.append(row);
        else
            rows.prepend(row);
    }
    // Ours sent while the chat is open sort where they were sent (m_place), a page read again included.
    for (int i = 0; i < rows.size(); ++i)
        placed(rows[i]);
    return rows;
}

// The Msg of a local row (addLocal), a text of ours with no row yet: read() of a one-row table that has messages' columns, aliased
// messages so kColumns reads it unchanged. So its map has exactly the keys a real row's has (the cards read them all), and the
// quote's out, sender and name are the quoted row's by the same subselects; the quote's line is what the engine writes to
// quote_preview, one_line()'s (docs/protocol.md): newlines and tabs to spaces, 150 characters here, bytes there. Ours, sending
// (out 1, state 0), kind 0 with no extra; with sticker, kind 5 with no text and its file still to come (media_state 1, which
// read() doesn't ask the engine for: the id is the UI's). rowid is the caller's to set (CAST of "local-n" is 0). An empty Msg (no id) when
// chats.db isn't open or the query fails. m_locale, m_clock and m_now are what select() sets before its reads.
MessageModel::Msg MessageModel::localRow(const QString &id, const QString &text, const QString &quoteId, qint64 ts, bool sticker)
{
    Msg none;
    QSqlDatabase db = database();
    if (!db.isOpen())
        return none;
    m_locale = m_region.locale();
    m_clock = timeFormat(m_locale, DateFormat::Short);
    m_now = QDateTime::currentDateTime().toTime_t();
    m_photoThere.clear();
    QSqlQuery q(db);
    q.prepare(QString("SELECT %1 FROM (SELECT ? AS chat, ? AS id, ? AS ts, 1 AS out, 0 AS state, NULL AS sender, "
                      "NULL AS sender_name, ? AS kind, ? AS text, "
                      "(SELECT replace(replace(substr(p.text, 1, 150), ?, ' '), ?, ' ') FROM messages p "
                      "WHERE p.chat = ? AND p.id = ?) AS quote_preview, "
                      "0 AS edited, 0 AS deleted, 0 AS duration, NULL AS waveform, ? AS media_state, 0 AS played, "
                      "NULL AS thumb, 0 AS width, 0 AS height, 0 AS size, NULL AS path, NULL AS extra, 0 AS starred, "
                      "? AS quote_id, NULL AS tr, NULL AS tr_lang) messages LEFT JOIN people ON people.user = messages.sender").arg(kColumns));
    q.addBindValue(m_chat);
    q.addBindValue(id);
    q.addBindValue(ts);
    q.addBindValue(sticker ? 5 : 0);
    q.addBindValue(sticker ? QVariant(QVariant::String) : QVariant(text)); // a sticker's text is NULL, as the engine writes it
    q.addBindValue(QString("\n")); // bound, not char(10): the quote's line is one line
    q.addBindValue(QString("\t"));
    q.addBindValue(m_chat);
    q.addBindValue(quoteId);
    q.addBindValue(sticker ? 1 : 0);
    q.addBindValue(quoteId);
    if (!q.exec() || !q.next()) {
        qWarning() << "messages: local row:" << q.lastError().text();
        return none;
    }
    return read(q);
}
