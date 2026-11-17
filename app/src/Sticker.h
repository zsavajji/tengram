#ifndef STICKER_H
#define STICKER_H

#include <QByteArray>
#include <QSet>
#include <QString>

class EngineLink;

// A sticker's picture (step 6.5b): neither Qt 4.8 nor the phone reads WebP, so libwebp decodes it here.
namespace Sticker {
// The PNG, at most 180 x 180 px, of the first frame (the whole picture, for a still one) of the WebP
// file at path. Made the first time and kept in data/cache/previews/<id>.png; "" when the file can't
// be read or decoded.
QByteArray picture(const QString &id, const QString &path);

// Asks the engine, quietly (EngineLink::requestQuiet), for the file of a sticker still on the server, the
// first time only: asked holds the ids already asked for, and takes id when the request went out (not
// while the engine is down). The engine's msgs event then makes the chat re-read the row.
void fetch(EngineLink *engine, const QString &chat, const QString &id, QSet<QString> &asked);
}

#endif
