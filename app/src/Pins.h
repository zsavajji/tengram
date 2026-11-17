#ifndef PINS_H
#define PINS_H

#include <QString>
#include <QVariantList>

// A chat's pinned messages (step 6.5h), read from the "chats" connection MessageModel has open, as
// Polls reads poll_votes. docs/protocol.md has the pin and unpin notices (kind 13).
namespace Pins {
// The messages pinned now, newest pin first, each a map {id, preview}. For an id the latest pin or
// unpin notice wins; a pin holds until its notice's `until`, or for 7 days (WhatsApp's default) from
// the notice when it has none (pins stored before step 6.5h). preview is the first line of the
// message's text cut to about 60 characters, else the kind's name (Photo, Video, ...); "" when
// chats.db doesn't hold the message.
QVariantList current(const QString &chat);

// The line a message is listed by: the first line of its text cut to about 60 characters, else the kind's name
// (Photo, Video, Voice message, ...), else "Message". The pins' preview, and the Starred page's text.
QString summary(int kind, const QString &text);
}

#endif
