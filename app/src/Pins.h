#ifndef PINS_H
#define PINS_H

#include <QString>
#include <QStringList>
#include <QVariantList>

// A chat's pinned messages (step 6.5h). Telegram's list comes from the engine (chat.pins); the previews are read from
// the "chats" connection MessageModel has open, as Polls reads poll_votes.
namespace Pins {
// The pins, in the order of `ids`, each a map {id, preview}. preview is the first line of the message's text cut to
// about 60 characters, else the kind's name (Photo, Video, ...); "" when chats.db doesn't hold the message.
QVariantList previews(const QString &chat, const QStringList &ids);

// The line a message is listed by: the first line of its text cut to about 60 characters, else the kind's name
// (Photo, Video, Voice message, ...), else "Message". The pins' preview, and the Starred page's text.
QString summary(int kind, const QString &text);
}

#endif
