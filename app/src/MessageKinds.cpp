#include "MessageKinds.h"

#include "MediaData.h"
#include "Markup.h"
#include "Polls.h"
#include "dates.h"

#include <bb/data/JsonDataAccess>
#include <bb/utility/i18n/Formatting>
#include <QCoreApplication>
#include <QDateTime>
#include <QMap>
#include <QRegExp>
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

static bool longer(const QString &a, const QString &b)
{
    return a.size() > b.size();
}

QString MessageKinds::html(const QString &shown, const QVariantMap &extra)
{
    const QString text = QString(shown).remove(QChar(0x20E3)); // as the bubble's plain text drops it

    // "@name" -> the key of the page it opens. Ours has no key (the profile is another page), so
    // it stays plain text. Two people of one name share a link: the last wins.
    QMap<QString, QString> pages;
    foreach (const QVariant &v, extra.value("mentions").toList()) {
        const QVariantMap m = v.toMap();
        if (!m.value("jid").toString().isEmpty() && !m.value("name").toString().isEmpty())
            pages.insert("@" + m.value("name").toString(), m.value("jid").toString());
    }

    // One pass over the text, longest name first, so "@Ann" never takes the start of "@Anna". Each
    // hit becomes one private-use char, so the styles (Markup) see a mention as a single word and
    // can't cut an anchor; the anchors go back in once the text is escaped and styled.
    QString plain = text;
    QStringList anchors;
    if (!pages.isEmpty()) {
        QStringList names = pages.keys();
        qSort(names.begin(), names.end(), longer);
        QStringList alternatives;
        foreach (const QString &n, names)
            alternatives << QRegExp::escape(n);
        const QRegExp any(alternatives.join("|"));
        plain.clear();
        int from = 0, at;
        while ((at = any.indexIn(text, from)) != -1 && anchors.size() < 0x800) {
            const QString hit = any.cap(0);
            plain += text.mid(from, at - from) + QChar(0xE000 + anchors.size());
            anchors << "<a href=\"hbmention:" + Markup::escaped(pages.value(hit)) + "\">" + Markup::escaped(hit) + "</a>";
            from = at + hit.length();
        }
        plain += text.mid(from);
    }

    QString out = Markup::html(plain);
    if (out.isEmpty()) {
        if (anchors.isEmpty())
            return QString();
        out = Markup::escaped(plain);
    }
    for (int i = 0; i < anchors.size(); ++i)
        out.replace(QChar(0xE000 + i), anchors.at(i));
    return out;
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

bool MessageKinds::fill(int kind, const QString &chat, const QString &id, const QString &text,
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
    data["forwarded"] = extra.value("forwarded").toInt(); // the score: 5 and up is "Forwarded many times"
    data["spoiler"] = extra.value("spoiler").toBool() && (kind == 1 || kind == 2); // MessageRows takes it off once tapped
    data["invite"] = kind == 14 && !out ? chat : QString(); // the chat of an invite card we can join, for its button
    data["poll"] = kind == 11; // always set, so QML never reads undefined
    // And an empty options list: a recycled card's PollBubble reads msg.options for its new row
    // before the delegate holding it goes (Q10 ui.log, 2026-11-02 and 11-04, six TypeErrors).
    data["options"] = QVariantList();
    switch (kind) {
    case 6:
        shown = text;
        return true;
    case 7: // from before step 6.5: "Location", "Poll: …"
        label = text;
        shown = QString();
        break;
    case 9:
        label = extra.value("live").toBool() ? QCoreApplication::translate("MessageKinds", "Live location")
                                              : QCoreApplication::translate("MessageKinds", "Location");
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
        Polls::fill(chat, id, extra.value("options").toList(), max, data);
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
