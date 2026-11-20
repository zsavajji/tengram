#include "Sticker.h"

#include "EngineLink.h"
#include "PreviewMaker.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <rlottie_capi.h>
#include <string.h>
#include <webp/demux.h>
#include <zlib.h>

using Sticker::kStickerPx;

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

bool Sticker::isTgs(const QString &path)
{
    QFile f(path);
    unsigned char magic[2];
    return f.open(QIODevice::ReadOnly) && f.read(reinterpret_cast<char *>(magic), 2) == 2 && magic[0] == 0x1f
        && magic[1] == 0x8b;
}

QByteArray Sticker::tgsJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    const QByteArray zipped = f.readAll(); // Telegram caps a TGS at 64 KB
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, 15 + 16) != Z_OK) // 15 + 16: gzip
        return QByteArray();
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(zipped.constData()));
    zs.avail_in = uInt(zipped.size());
    const int kChunk = 64 * 1024, kMax = 16 * 1024 * 1024; // the JSON is a few MB at most
    QByteArray json;
    int ret = Z_OK;
    while (ret == Z_OK && json.size() <= kMax) {
        const int have = json.size();
        json.resize(have + kChunk);
        zs.next_out = reinterpret_cast<Bytef *>(json.data()) + have;
        zs.avail_out = kChunk;
        ret = inflate(&zs, Z_NO_FLUSH);
        json.resize(have + kChunk - int(zs.avail_out));
    }
    inflateEnd(&zs);
    return ret == Z_STREAM_END && json.size() <= kMax ? json : QByteArray();
}

// Frame 0 of a TGS, drawn at side square (null on failure).
static QImage tgsFirstFrame(const QString &path, int side)
{
    const QByteArray json = Sticker::tgsJson(path);
    if (json.isEmpty())
        return QImage();
    Lottie_Animation *animation = lottie_animation_from_data(json.constData(), path.toUtf8().constData(), "");
    if (!animation)
        return QImage();
    // rlottie draws premultiplied ARGB as 0xAARRGGBB: B, G, R, A in memory on a little-endian ARM, which is what
    // Format_ARGB32_Premultiplied is.
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(0);
    lottie_animation_render(animation, 0, reinterpret_cast<uint32_t *>(image.bits()), side, side, image.bytesPerLine());
    lottie_animation_destroy(animation);
    return image;
}

// The still picture of the file at path, at most side square: a TGS's frame 0, a WebP's first frame, else what Qt's own
// readers know (the JPEG or PNG a sticker's thumbnail may be). Null on failure.
QImage Sticker::still(const QString &path, int side)
{
    QImage image = Sticker::isTgs(path) ? tgsFirstFrame(path, side) : firstFrame(path);
    if (image.isNull())
        image = QImage(path);
    if (image.width() > side || image.height() > side)
        image = image.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

// The PNG of the still picture of path kept in file, made the first time; empty when it can't be made.
static QByteArray cached(const QString &file, const QString &path, int side)
{
    if (!QFile::exists(file)) {
        const QImage image = Sticker::still(path, side);
        if (image.isNull() || !QDir().mkpath(QFileInfo(file).path()))
            return QByteArray();
        if (!image.save(file, "PNG")) {
            QFile::remove(file); // no half-written preview left to be taken for a good one
            return QByteArray();
        }
    }
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// The cached picture's bytes. A picture not made yet is asked of PreviewMaker, which makes it on a worker thread (a TGS
// takes 20 to 60 ms to parse) and tells the model when it is there, which reads the row again: empty until then, and for a
// file that doesn't decode, which isn't tried again until the app restarts.
// ponytail: two at a time, in the order the rows were read; no priority for the ones on screen.
QByteArray Sticker::picture(const QString &id, const QString &path)
{
    const QString file = QDir::homePath() + "/cache/previews/" + id + ".png";
    QFile f(file);
    if (f.open(QIODevice::ReadOnly))
        return f.readAll();
    PreviewMaker::instance()->want(id, PreviewMaker::StickerPicture, path, file);
    return QByteArray();
}

// The picker's grid and preview: the same, by the path's hash (a sticker's file is not a message's id) and the side, so the
// 128 px of the grid and the 360 px of the preview don't share a file. Only QImage, QFile, libwebp and rlottie are touched:
// a worker thread may call it.
QByteArray Sticker::gridPicture(const QString &path, int side)
{
    return cached(QDir::homePath() + "/cache/previews/st-" + QString::number(side) + "-" + QString::number(qHash(path), 16)
                      + ".png",
                  path, side);
}

QString Sticker::fetch(EngineLink *engine, const QString &chat, const QString &id, QSet<QString> &asked, bool preview)
{
    if (!engine || asked.contains(id))
        return QString();
    QVariantMap args;
    args["chat"] = chat;
    args["id"] = id;
    if (preview)
        args["preview"] = true;
    const QString request = engine->requestQuiet("media.get", args);
    if (!request.isEmpty())
        asked.insert(id);
    return request;
}
