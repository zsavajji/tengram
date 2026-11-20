#ifndef MEDIADATA_H
#define MEDIADATA_H

#include <QByteArray>
#include <QImage>
#include <QLocale>
#include <QString>
#include <QVariantMap>

// What a photo, video, audio file or document row hands QML on top of the common message fields
// (step 6.1), kept out of MessageModel.cpp to keep that file short.
namespace MediaData {
// "m:ss", also for a call notice's length (MessageKinds).
QString clock(int seconds);

// A kind's name as a reply and the long-press menu word a message with no text: "Photo", "Video",
// "Document", "Sticker", "Audio file", "Location" or "Contact"; "" for any other kind.
QString kindName(int kind);

// The photo at path decoded to its preview's size (see fill): a short side of at most 640 px, a long side of at most 1280. Null
// when the file can't be read. Touches only QImageReader, so PreviewMaker's worker thread calls it.
QImage previewImage(const QString &path);

// Sets data's media keys from the row's columns; id is the message's, kind is messages.kind, 0 for a
// deleted row. A downloaded photo's thumb is its 640 px preview, made from the file (step 6.3), a
// sticker's its 180 px picture (Sticker.h, step 6.5b), else the row's thumbnail, which also stands in until the file is
// made: PreviewMaker makes both on a worker thread and MessageModel reads the row again then; a location's is its map
// picture, its ratio from the JPEG's own size (step 6.5c). A contact (step 6.5d) is media too, drawn as
// a row; its summary is the names, and MessageKinds adds its vcard and contactInfo. A text (kind 0, also
// a deleted row's) gets linkThumb, an Image of the row's thumbnail (empty without one), and hasLinkThumb
// for its link preview (step 6.5f); MessageKinds has the preview's text.
// media: "photo", "video", "doc", "sticker", "audio", "location" or "contact", "" for any other kind
// (always set, so QML never reads undefined); the rest (thumb, hasThumb, ratio, mediaState, path, tgs,
// sizeText, durationText, fileName) only for media; tgs is true for a downloaded sticker whose file is a TGS (Lottie).
// Returns whether it is media. blur (step 6.6d) shows the picture blurred, for a spoiler under its cover. previewPath
// is the row's extra.preview ("" when it has none): the display-size file the engine fetched for a photo not downloaded
// yet (the thumb is made from it, as from the photo, under another cache name), or a video's JPEG thumbnail (the thumb
// is that file).
bool fill(QVariantMap &data, const QString &id, int kind, const QString &text,
          const QByteArray &thumb, int width, int height, qint64 size, int duration,
          int mediaState, const QString &path, const QString &previewPath, const QLocale &locale,
          bool blur);
}

#endif
