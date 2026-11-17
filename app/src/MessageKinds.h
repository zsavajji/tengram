#ifndef MESSAGEKINDS_H
#define MESSAGEKINDS_H

#include <QLocale>
#include <QString>
#include <QVariantMap>

// The wording of the kinds that are not media (step 6.5), kept out of MessageModel.cpp to keep that
// file short. docs/protocol.md has the kinds and what `extra` holds for each.
namespace MessageKinds {
// The `extra` column's JSON as a map: empty for "" (NULL) or a damaged one. A row's is parsed once, here,
// and the map goes to fill and html.
QVariantMap parse(const QString &json);

// Words a message of messages.kind `kind`. chat and id (the row's), text, extra (parse()'s map of the row's
// JSON), out and sender (the sender's name, "" when none) are the row's; time is the message's time as
// the model words it, the locale the phone's. Sets label (the card's grey line) and shown (the text
// under it) for kinds 7, 9 to 12 and 14, and leaves them alone for the others (shown starts as text).
// A location (9) also puts lat, lon and place (its name) in data, for MapChooser (step 6.5c). A contact
// (10) has no label and an empty shown (MediaBubble draws it): data gets vcard (every card's vCard
// joined by "\r\n", for MediaOpener::importContact) and contactInfo (the first number, "N contacts"
// for several; step 6.5d). A poll (11) keeps the question as shown; its label says whether one option
// or several can be chosen, and data gets Polls::fill's keys for PollBubble (step 6.5e). data["poll"]
// is set for every kind (true for 11), so QML never reads undefined. So are linkTitle, linkDesc and
// linkUrl, a text's (kind 0) link preview from extra for LinkPreview ("" without one; step 6.5f);
// MediaData::fill adds its picture.
// Returns true for a service line (kinds 6 and 13): then shown is the whole line and label is not used.
bool fill(int kind, const QString &chat, const QString &id, const QString &text,
          const QVariantMap &extra, bool out, const QString &sender, const QString &time,
          const QLocale &locale, QVariantMap &data, QString &label, QString &shown);

// The bubble's text as HTML, when `extra` (parse()'s map) has mentions with a page to open
// and `shown` (the text the bubble finally shows, so after fill and MediaData) has their "@name", or
// when `shown` has WhatsApp's styles (*bold* _italic_ ~strike~ `mono`, see Markup.h): escaped, a
// newline a <br/>, U+20E3 dropped as the Label does, each "@name" an anchor to "hbmention:<key>"
// (MessageCard.cpp opens that person's page), each style a tag. "" otherwise: the Label then shows
// `shown` as plain text. MessageRows::read sets it as data["html"] beside data["text"], for every row.
QString html(const QString &shown, const QVariantMap &extra);
}

#endif
