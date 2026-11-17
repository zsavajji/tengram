#include "Sticker.h"

#include "EngineLink.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <webp/demux.h>

// The first frame of a still or animated WebP, as an image of its own (null on failure).
static QImage firstFrame(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QImage();
    const QByteArray bytes = f.readAll(); // the decoder reads it in place until it is deleted
    WebPData data = { reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()) };
    // Stickers are 512 px; the decoder allocates the whole canvas, 4 bytes a pixel, for whatever a
    // sender claims. WebPGetInfo reads the canvas of an animation too.
    int w = 0, h = 0;
    if (!WebPGetInfo(data.bytes, data.size, &w, &h) || w > 2048 || h > 2048)
        return QImage();
    WebPAnimDecoderOptions options;
    if (!WebPAnimDecoderOptionsInit(&options))
        return QImage();
    // B, G, R, then premultiplied alpha: QImage::Format_ARGB32_Premultiplied on a little-endian ARM.
    options.color_mode = MODE_bgrA;
    WebPAnimDecoder *decoder = WebPAnimDecoderNew(&data, &options);
    if (!decoder)
        return QImage();
    QImage image;
    WebPAnimInfo info;
    uint8_t *pixels = 0; // the decoder's, overwritten by the next frame and freed with it: copied
    int timestamp = 0;
    if (WebPAnimDecoderGetInfo(decoder, &info) && WebPAnimDecoderGetNext(decoder, &pixels, &timestamp)) {
        const int width = int(info.canvas_width), height = int(info.canvas_height);
        image = QImage(pixels, width, height, width * 4, QImage::Format_ARGB32_Premultiplied).copy();
    }
    WebPAnimDecoderDelete(decoder);
    return image;
}

// ponytail: made on the UI thread, as the photo previews are; a worker thread if scrolling stutters. A
// file that doesn't decode is tried again at every read of its row.
QByteArray Sticker::picture(const QString &id, const QString &path)
{
    const QString file = QDir::homePath() + "/cache/previews/" + id + ".png";
    if (!QFile::exists(file)) {
        QImage image = firstFrame(path);
        if (image.isNull() || !QDir().mkpath(QFileInfo(file).path()))
            return QByteArray();
        if (image.width() > 180 || image.height() > 180)
            image = image.scaled(180, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (!image.save(file, "PNG")) {
            QFile::remove(file); // no half-written preview left to be taken for a good one
            return QByteArray();
        }
    }
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void Sticker::fetch(EngineLink *engine, const QString &chat, const QString &id, QSet<QString> &asked)
{
    if (!engine || asked.contains(id))
        return;
    QVariantMap args;
    args["chat"] = chat;
    args["id"] = id;
    if (!engine->requestQuiet("media.get", args).isEmpty())
        asked.insert(id);
}
