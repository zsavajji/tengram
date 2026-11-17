#include "MediaData.h"
#include "Sticker.h"
#include "sizes.h"

#include <bb/cascades/Image>
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QUrl>

// "m:ss"
QString MediaData::clock(int seconds)
{
    return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

// The bubble's picture for a downloaded photo (step 6.3): a JPEG whose short side is at most 640 px
// (long side 1280), made the first time from the file and kept in data/cache/previews/ as
// <id>-640.jpg (the engine wipes data/cache/ on unlink). The
// engine decodes no picture, and Qt's reader decodes straight to the smaller size. Fills width and
// height from the file when the row has none. Returns the preview's path, "" when the photo can't be
// read: the bubble's Image is made from the path, so Cascades reads and decodes the file off the UI
// thread and the model holds no bytes of it (they were read here for every row, and kept).
// ponytail: made on the UI thread, tens of ms on the Q10; a worker thread if scrolling stutters.
static const int PreviewSide = 640; // the knob: the preview's short side, in px
static QString preview(const QString &id, const QString &path, int &width, int &height)
{
    const QString file = QDir::homePath() + "/cache/previews/" + id + "-640.jpg";
    const bool have = QFile::exists(file);
    if (!have || width <= 0 || height <= 0) {
        QImageReader reader(path);
        const QSize size = reader.size(); // from the header
        if (width <= 0 || height <= 0) {
            width = size.width();
            height = size.height();
        }
        if (!have) {
            if (!size.isValid())
                return QString();
            QSize to = size;
            // The card is ~640 px wide and crops to a square: the short side sets the sharpness.
            // A panorama's long side is capped so its decoded image stays a few MB.
            if (qMin(to.width(), to.height()) > PreviewSide)
                to.scale(PreviewSide, PreviewSide, Qt::KeepAspectRatioByExpanding);
            if (qMax(to.width(), to.height()) > 2 * PreviewSide)
                to.scale(2 * PreviewSide, 2 * PreviewSide, Qt::KeepAspectRatio);
            reader.setScaledSize(to);
            const QImage image = reader.read();
            if (image.isNull() || !QDir().mkpath(QFileInfo(file).path()))
                return QString();
            if (!image.save(file, "JPEG", 80)) {
                QFile::remove(file); // no half-written preview left to be taken for a good one
                return QString();
            }
        }
    }
    return file;
}

// A spoiler's picture under its cover (step 6.6d): down to 12 px and up again, Qt's smooth scaling
// twice, so there's no filter of our own to write. Empty when it can't be read: the bubble then shows
// its plain box, never the sharp picture.
// ponytail: blurred again each time its row is read; keep the bytes if a chat of many spoilers stutters.
static QByteArray blurred(const QByteArray &picture)
{
    const QImage in = QImage::fromData(picture);
    if (in.isNull())
        return QByteArray();
    const QImage out = in.scaled(12, 12, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                           .scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    return out.save(&buffer, "JPEG", 70) ? bytes : QByteArray();
}

// What a reply and the long-press menu show for a message whose text is empty.
QString MediaData::kindName(int kind)
{
    static const char *const names[] = { "", QT_TRANSLATE_NOOP("MediaData", "Photo"),
        QT_TRANSLATE_NOOP("MediaData", "Video"), "", QT_TRANSLATE_NOOP("MediaData", "Document"),
        QT_TRANSLATE_NOOP("MediaData", "Sticker"), "", "", QT_TRANSLATE_NOOP("MediaData", "Audio file"),
        QT_TRANSLATE_NOOP("MediaData", "Location"),
        QT_TRANSLATE_NOOP("MediaData", "Contact") };
    if (kind < 0 || kind >= int(sizeof names / sizeof *names) || !*names[kind])
        return QString();
    return QCoreApplication::translate("MediaData", names[kind]);
}

bool MediaData::fill(QVariantMap &data, const QString &id, int kind, const QString &text,
                     const QByteArray &thumb, int width, int height, qint64 size, int duration,
                     int mediaState, const QString &path, const QLocale &locale, bool blur)
{
    QString media;
    switch (kind) {
    case 1: media = "photo"; break;
    case 2: media = "video"; break;
    case 4: media = "doc"; break;
    case 5: media = "sticker"; break;
    case 8: media = "audio"; break;
    case 9: media = "location"; break;
    case 10: media = "contact"; break;
    }
    data["media"] = media;
    // A text's link preview picture (step 6.5f), before the return below: kind 0 is no media. An
    // Image even when empty, as thumb below, so a recycled card never keeps another row's.
    if (kind == 0) {
        data["linkThumb"] = QVariant::fromValue(thumb.isEmpty() ? bb::cascades::Image()
                                                                : bb::cascades::Image(thumb));
        data["hasLinkThumb"] = !thumb.isEmpty();
    }
    if (media.isEmpty())
        return false;
    // The inline thumbnail stands in when there is no readable photo or sticker. A photo's preview is
    // shown from its file; only a spoiler's is read here, to blur it.
    QByteArray shown = thumb;
    QString file;
    if (kind == 1 && mediaState == 2 && !path.isEmpty()) {
        file = preview(id, path, width, height);
    } else if (kind == 5 && mediaState == 2 && !path.isEmpty()) {
        const QByteArray p = Sticker::picture(id, path);
        if (!p.isEmpty())
            shown = p;
    }
    if (blur) {
        QFile f(file);
        if (!file.isEmpty() && f.open(QIODevice::ReadOnly))
            shown = f.readAll();
        file.clear();
        shown = blurred(shown);
    }
    // A location has no width or height columns: its map picture is a JPEG, sized by its own header.
    if (kind == 9 && !thumb.isEmpty()) {
        QBuffer buffer;
        buffer.setData(thumb);
        buffer.open(QIODevice::ReadOnly);
        const QSize size = QImageReader(&buffer).size();
        width = size.width();
        height = size.height();
    }
    // Always an Image, an empty one without a thumbnail: a recycled card assigned undefined would
    // keep the picture of the row it showed before.
    data["thumb"] = QVariant::fromValue(!file.isEmpty() ? bb::cascades::Image(QUrl::fromLocalFile(file))
                                        : shown.isEmpty() ? bb::cascades::Image()
                                                          : bb::cascades::Image(shown));
    data["hasThumb"] = !file.isEmpty() || !shown.isEmpty();
    data["ratio"] = width > 0 && height > 0 ? double(height) / width : 0.75;
    data["mediaState"] = mediaState;
    data["path"] = path;
    data["sizeText"] = sizeText(size, locale);
    data["durationText"] = (kind == 2 || kind == 8) && duration > 0 ? clock(duration) : QString();
    data["fileName"] = kind == 4 ? text : QString();
    // A contact's text is its names (step 6.5d).
    data["summary"] = (kind == 4 || kind == 10) && !text.isEmpty() ? text : kindName(kind);
    return true;
}
