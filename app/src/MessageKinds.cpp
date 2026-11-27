#include "MessageKinds.h"

#include "MediaData.h"
#include "Polls.h"
#include "dates.h"

#include <bb/data/JsonDataAccess>
#include <bb/utility/i18n/Formatting>
#include <QCoreApplication>
#include <QDateTime>
#include <QStringList>
#include <QVariantMap>

using namespace bb::data;
using namespace bb::utility::i18n;

// Kind 13's text is a code, not English (docs/protocol.md); a call line ends with its time, as a
// service line shows none of its own.
static QString notice(const QString &code, int secs, bool out, const QString &sender,
                      const QString &time)
{
    QString line;
    if (code == "call")
        line = secs > 0 ? QCoreApplication::translate("MessageKinds", "Voice call, %1").arg(MediaData::clock(secs))
                        : QCoreApplication::translate("MessageKinds", "Voice call");
    else if (code == "video_call")
        line = secs > 0 ? QCoreApplication::translate("MessageKinds", "Video call, %1").arg(MediaData::clock(secs))
                        : QCoreApplication::translate("MessageKinds", "Video call");
    else if (code == "missed_call")
        line = QCoreApplication::translate("MessageKinds", "Missed voice call");
    else if (code == "missed_video_call")
        line = QCoreApplication::translate("MessageKinds", "Missed video call");
    if (!line.isEmpty())
        return line + QString::fromUtf8(" · ") + time;

    if (code == "pin") {
        return out ? QCoreApplication::translate("MessageKinds", "You pinned a message")
               : !sender.isEmpty() ? QCoreApplication::translate("MessageKinds", "%1 pinned a message").arg(sender)
                                   : QCoreApplication::translate("MessageKinds", "A message was pinned");
    }
    if (code == "unpin") {
        return out ? QCoreApplication::translate("MessageKinds", "You unpinned a message")
               : !sender.isEmpty() ? QCoreApplication::translate("MessageKinds", "%1 unpinned a message").arg(sender)
                                   : QCoreApplication::translate("MessageKinds", "A message was unpinned");
    }
    if (code == "view_once")
        return QCoreApplication::translate("MessageKinds", "View once message. Open it on your phone.");
    if (code == "on_phone")
        return QCoreApplication::translate("MessageKinds", "This message can only be seen on your phone.");
    return QCoreApplication::translate("MessageKinds", "Message");
}

QVariantMap MessageKinds::parse(const QString &json)
{
    // Only rows that have details; a damaged one reads as none.
    QVariantMap extra;
    if (!json.isEmpty()) {
        JsonDataAccess parser;
        const QVariant doc = parser.loadFromBuffer(json.toUtf8());
        if (!parser.hasError() && doc.type() == QVariant::Map)
            extra = doc.toMap();
    }
    return extra;
}

bool MessageKinds::fill(int kind, const QString &chat, const QString &text,
                        const QVariantMap &extra, bool out, const QString &sender, const QString &time,
                        const QLocale &locale, QVariantMap &data, QString &label, QString &shown)
{
    // A text's link preview (step 6.5f), drawn by LinkPreview; always set, so QML never reads undefined.
    const bool preview = kind == 0 && !extra.value("title").toString().isEmpty();
    data["linkTitle"] = preview ? extra.value("title").toString() : QString();
    data["linkDesc"] = preview ? extra.value("desc").toString() : QString();
    data["linkUrl"] = preview ? extra.value("url").toString() : QString();

    QStringList lines;
    data["gif"] = extra.value("gif").toBool() && kind == 2; // a video that plays by itself, in a loop
    data["sentAs"] = extra.value("local").toString(); // the local row this one of ours stands for (MessageModel::takeLocal)
    data["wasId"] = extra.value("was").toString();    // the temporary id this sent row replaces (MessageModel::merge)
    data["forwarded"] = extra.value("forwarded").toInt(); // the score: 5 and up is "Forwarded many times"
    data["spoiler"] = extra.value("spoiler").toBool() && (kind == 1 || kind == 2); // MessageRows takes it off once tapped
    data["invite"] = kind == 14 && !out ? chat : QString(); // the chat of an invite card we can join, for its button
    data["poll"] = kind == 11; // always set, so QML never reads undefined
    // And an empty options list: a recycled card's PollBubble reads msg.options for its new row
    // before the delegate holding it goes (Q10 ui.log, 2026-11-02 and 11-04, six TypeErrors).
    data["options"] = QVariantList();
    // A live location's (docs/protocol.md, kind 9): when it ends (unix s, 0 none), its last position's time and our
    // proximity alert's metres. Set for every kind, as poll is; liveOn is MessageModel::data()'s, false here.
    const bool live = kind == 9 && extra.value("live").toBool();
    data["live"] = live;
    data["liveUntil"] = live ? extra.value("until").toLongLong() : qint64(0);
    data["liveAt"] = live ? extra.value("at").toLongLong() : qint64(0);
    data["liveAlert"] = live ? extra.value("alert").toInt() : 0;
    data["liveOn"] = false;
    switch (kind) {
    case 6:
        shown = text;
        return true;
    case 7: // from before step 6.5: "Location", "Poll: …"
        label = text;
        shown = QString();
        break;
    case 9:
        // The map picture alone says what it is (MediaBubble); a venue's title and address stay the text. A live location's
        // label is worded when the row is drawn (MessageModel::liveLabel): this one stands until then.
        label = live ? QCoreApplication::translate("MessageKinds", "Live location") : QString();
        // What a tap on the card opens (MapChooser); the name is the text's first line.
        data["lat"] = extra.value("lat").toDouble();
        data["lon"] = extra.value("lon").toDouble();
        data["place"] = text.section('\n', 0, 0);
        break;
    case 10: {
        // MediaBubble draws the card: the names are MediaData's summary, contactInfo the second line (the
        // first number, or the count). No label and no text, so Copy is off. A tap hands vcard to the
        // Contacts app (MediaOpener::importContact): every card, as one stream.
        const QVariantList cards = extra.value("contacts").toList();
        QStringList vcards;
        QString tel;
        foreach (const QVariant &c, cards) {
            const QVariantMap card = c.toMap();
            const QString vcard = card.value("vcard").toString().trimmed();
            if (!vcard.isEmpty())
                vcards << vcard;
            const QVariantList tels = card.value("tel").toList();
            if (tel.isEmpty() && !tels.isEmpty())
                tel = tels.first().toString();
        }
        data["vcard"] = vcards.join("\r\n");
        data["contactInfo"] = cards.size() > 1 ? QCoreApplication::translate("MessageKinds", "%n contacts", 0,
                                                      QCoreApplication::CodecForTr, cards.size())
                                               : tel;
        shown = QString();
        break;
    }
    case 11: {
        // PollBubble draws the options (Polls); the text is the question alone.
        const int max = extra.value("max").toInt();
        label = max == 1 ? QCoreApplication::translate("MessageKinds", "Poll")
                         : QCoreApplication::translate("MessageKinds", "Poll, choose several");
        Polls::fill(extra, data);
        break;
    }
    case 12: {
        label = extra.value("canceled").toBool() ? QCoreApplication::translate("MessageKinds", "Event canceled")
                                                  : QCoreApplication::translate("MessageKinds", "Event");
        lines << text;
        const uint start = extra.value("start").toLongLong();
        if (start > 0) {
            // The day as a day's header words it, and the time as a message's.
            const QDateTime when = QDateTime::fromTime_t(start);
            lines << capitalized(locale.toString(when.date(), QLocale::LongFormat)) + QString::fromUtf8(" · ")
                         + locale.toString(when.time(), timeFormat(locale, DateFormat::Short));
        }
        lines << extra.value("place").toString();
        lines.removeAll(QString());
        shown = lines.join("\n");
        break;
    }
    case 13:
        shown = notice(text, extra.value("secs").toInt(), out, sender, time);
        return true;
    case 14:
        label = QCoreApplication::translate("MessageKinds", "Group invite");
        break;
    }
    return false;
}
