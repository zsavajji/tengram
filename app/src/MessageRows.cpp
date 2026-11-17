#include "MessageModel.h"

#include <bb/cascades/Image>

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
#include <QLocale>
#include <QSqlDatabase>
#include <QSqlError>

using namespace bb::utility::i18n;

// The chats.db side of MessageModel, out of MessageModel.cpp to keep that file short: the
// connection, the query and the reading of one row into a Msg.

// The connection ChatListModel opens, read-only: only the engine writes chats.db.
static const char *const kDb = "chats";

// The three QUOTED are the quoted message's out, sender and sender_name, by its primary key. The
// last two columns are the message's reactions, all and ours (sender '' is us); the engine makes
// that table when it migrates chats.db, before the UI reads it.
#define QUOTED(col) "(SELECT " col " FROM messages q WHERE q.chat = messages.chat AND q.id = messages.quote_id)"
static const char *const kColumns = "rowid, id, ts, out, state, sender, sender_name, kind, "
                                    "text, quote_preview, edited, deleted, "
                                    QUOTED("out") ", " QUOTED("sender") ", " QUOTED("sender_name") ", duration, waveform, media_state, played, "
                                    "thumb, width, height, size, path, "
                                    "(SELECT group_concat(emoji, ' ') FROM reactions r WHERE r.chat = messages.chat AND r.msg = messages.id), "
                                    "(SELECT emoji FROM reactions r WHERE r.chat = messages.chat AND r.msg = messages.id AND r.sender = ''), extra, starred";
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
    m.data["readOnly"] = m_chat.endsWith("@newsletter"); // a channel: no reply, reaction, delete or vote (step 6.6g)
    m.data["album"] = QVariantList(); // MessageModel::albumData fills it for an album; never undefined in QML
    const QString text = q.value(8).toString();
    const bool deleted = q.value(11).toInt() != 0;
    const QLocale &locale = m_locale;
    const QString time = locale.toString(when.time(), m_clock);
    // The wording of kinds 6, 7 and 9 to 14 (kind 6 and 13 are service lines).
    QString label, shown = text;
    const QVariantMap extra = MessageKinds::parse(q.value(26).toString());
    m.service = MessageKinds::fill(kind, m_chat, m.id, text, extra, out,
                                   q.value(6).toString(), time, locale, m.data, label, shown);
    if (m.service) {
        m.data["text"] = shown;
        m.data["html"] = QString(); // set on every row (below too), so QML never reads undefined
        return m;
    }
    // Photos, videos, stickers, audio files and documents are drawn by MediaBubble: no label, and a
    // document's text (its name) is the bubble's. A caption stays in the text.
    if (kind == 5 && !deleted && q.value(17).toInt() == 1)
        Sticker::fetch(m_engine, m_chat, m.id, m_asked); // a history sticker, asked once per page
    // Covered until a tap takes the cover off (unspoil), for as long as this chat stays open.
    const bool covered = m.data["spoiler"].toBool() && !deleted && !m_revealed.contains(m.id);
    m.data["spoiler"] = covered;
    if (MediaData::fill(m.data, m.id, deleted ? 0 : kind, text, q.value(19).toByteArray(),
                        q.value(20).toInt(), q.value(21).toInt(), q.value(22).toLongLong(),
                        q.value(15).toInt(), q.value(17).toInt(), q.value(23).toString(), locale,
                        covered)
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
    m.data["starred"] = q.value(27).toInt() != 0;
    m.data["deleted"] = deleted;
    // The chip under the card: up to three different emoji, and how many reactions there are.
    const QStringList all = deleted ? QStringList() : q.value(24).toString().split(' ', QString::SkipEmptyParts);
    QStringList kinds;
    foreach (const QString &e, all) {
        if (kinds.size() < 3 && !kinds.contains(e))
            kinds << e;
    }
    m.data["reactions"] = kinds.join(QString());
    m.data["reactionCount"] = all.size();
    m.data["myReaction"] = deleted ? QString() : q.value(25).toString();
    // Delete for everyone: ours, sent, and inside WhatsApp's limit of about 2.5 days.
    // ponytail: read when the row is read, so a page left open for days can offer it a little late.
    const int state = q.value(4).toInt();
    m.data["revocable"] = out && !deleted && state >= 1 && state <= 3
                          && m_now - m.ts < 60 * 3600;
    // Edit: our own sent text or caption (a photo's or video's), inside WhatsApp's 15 minutes (the server allows 20).
    m.data["editable"] = out && !deleted && (kind == 0 || ((kind == 1 || kind == 2) && !text.isEmpty())) && state >= 1 && state <= 3
                         && m_now - m.ts < 15 * 60;
    m.data["time"] = time;
    // The long-press menu's title: the day in full and the time.
    m.data["stamp"] = longDay(when.date(), locale) + ", " + time;
    m.data["label"] = label;
    const bool voice = kind == 3 && !deleted;
    m.data["voice"] = voice;
    if (voice) {
        m.data["duration"] = q.value(15).toInt();
        m.data["mediaState"] = q.value(17).toInt();
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
    m.data["quote"] = q.value(9).toString();
    m.data["sender"] = sender;
    QString color;
    if (!sender.isEmpty()) {
        const QString key = q.value(5).isNull() ? sender : q.value(5).toString();
        color = pickColor(key, kSenders, kSenderCount);
    }
    m.data["senderColor"] = color;
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
    QSqlDatabase db = QSqlDatabase::database(kDb, false);
    if (!db.isValid()) {
        db = QSqlDatabase::addDatabase("QSQLITE", kDb);
        db.setDatabaseName(QDir::homePath() + "/chats.db");
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
    }
    if (!db.isOpen())
        db.open();
    return db;
}

// The chat's unread count in chats.db, 0 when it has no row (a contact with no chat yet).
int MessageModel::unread() const
{
    QSqlDatabase db = database();
    if (!db.isOpen())
        return 0;
    QSqlQuery q(db);
    q.prepare("SELECT unread FROM chats WHERE jid = ?");
    q.addBindValue(m_chat);
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

// The times select would read, with no row built: what deciding a page's size needs.
QList<qint64> MessageModel::pageTimes(const QString &where, const QVariantList &args, int limit)
{
    QList<qint64> times;
    QSqlDatabase db = database();
    if (!db.isOpen())
        return times;
    QSqlQuery q(db);
    q.prepare(QString("SELECT ts FROM messages WHERE chat = ? %1 ORDER BY ts DESC, rowid DESC LIMIT %2")
                  .arg(where).arg(limit));
    q.addBindValue(m_chat);
    foreach (const QVariant &a, args)
        q.addBindValue(a);
    if (q.exec()) {
        while (q.next())
            times << q.value(0).toLongLong();
    }
    return times;
}

// Runs the messages query of this chat: "where" adds conditions on top of chat = ?, args are
// bound after it. Newest first, then reversed to the order the list shows.
QList<MessageModel::Msg> MessageModel::select(const QString &where, const QVariantList &args,
                                              int limit)
{
    QList<Msg> rows;
    QSqlDatabase db = database();
    if (!db.isOpen())
        return rows;
    m_locale = m_region.locale();
    m_clock = timeFormat(m_locale, DateFormat::Short);
    m_now = QDateTime::currentDateTime().toTime_t();
    QSqlQuery q(db);
    q.prepare(QString("SELECT %1 FROM messages WHERE chat = ? %2 ORDER BY ts DESC, rowid DESC "
                      "LIMIT %3").arg(kColumns, where).arg(limit));
    q.addBindValue(m_chat);
    foreach (const QVariant &a, args)
        q.addBindValue(a);
    if (!q.exec()) {
        qWarning() << "messages:" << q.lastError().text();
        return rows;
    }
    while (q.next())
        rows.prepend(read(q));
    return rows;
}
