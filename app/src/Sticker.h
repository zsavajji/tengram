#ifndef STICKER_H
#define STICKER_H

#include <QByteArray>
#include <QImage>
#include <QSet>
#include <QString>

class EngineLink;

// A sticker's picture (step 6.5b): neither Qt 4.8 nor the phone reads WebP, so libwebp decodes it here. A TGS (Lottie)
// sticker's is its frame 0, drawn by rlottie (libtgs.so); StickerPlayer plays the rest.
namespace Sticker {
// The side of a sticker's picture and of the frames its animation is drawn at, in px.
const int kStickerPx = 180;

// The PNG, at most kStickerPx square, of the first frame (the whole picture, for a still one) of the WebP or TGS
// file at path, kept in data/cache/previews/<id>.png. It is made the first time by PreviewMaker, on a worker thread: until
// then, and when the file can't be read or decoded, "". MessageModel reads the row again when the PNG is there.
QByteArray picture(const QString &id, const QString &path);

// The still picture of the file at path, at most side square: a WebP's first frame, a TGS's frame 0, or a JPEG or PNG
// thumbnail. Null on failure. Safe on a worker thread (QImage, QFile, libwebp and rlottie only): PreviewMaker makes picture's
// PNG from it.
QImage still(const QString &path, int side);

// The PNG of the still picture of the file at path, at most side square: a WebP's first frame, a TGS's frame 0, or a
// JPEG or PNG thumbnail. Kept in data/cache/previews/st-<side>-<hash of path>.png; empty on failure. Safe on a worker
// thread (QImage, QFile, libwebp and rlottie only), which the picker's grid and preview run it on.
QByteArray gridPicture(const QString &path, int side);

// Whether the file at path is a TGS: it starts with the two bytes of gzip, which a TGS is (gzipped Lottie JSON) and a
// WebP is not. The UI looks at the file instead of asking the engine for its format, so a row already in chats.db works.
bool isTgs(const QString &path);

// The Lottie JSON inside the TGS at path, NUL-terminated as QByteArray's own data is (rlottie takes a C string); empty
// when the file can't be read or inflated, or the JSON is over 16 MB. Touches only QFile and zlib, so a worker thread may call it.
QByteArray tgsJson(const QString &path);

// Asks the engine, quietly (EngineLink::requestQuiet), for the file of a sticker still on the server, the
// first time only: asked holds the ids already asked for, and takes id when the request went out (not
// while the engine is down). The engine's msgs event then makes the chat re-read the row. preview asks
// for the display-size picture of a photo or the thumbnail of a video instead (media.get's preview), which
// the row then carries as extra.preview. Returns the request's id, "" when none went out.
QString fetch(EngineLink *engine, const QString &chat, const QString &id, QSet<QString> &asked, bool preview = false);
}

#endif
