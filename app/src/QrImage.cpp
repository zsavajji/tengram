#include "QrImage.h"

#include <bb/ImageData>
#include <bb/PixelFormat>
#include <bb/cascades/Image>

extern "C" {
#include "qrcodegen.h"
}

QrImage::QrImage(QObject *parent)
    : QObject(parent)
{
}

QVariant QrImage::image(const QString &text, int size)
{
    if (text.isEmpty())
        return QVariant();
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
    uint8_t code[qrcodegen_BUFFER_LEN_MAX];
    // LOW keeps the version small, so the modules big on a 720 px screen; boostEcl raises it
    // for free when it fits.
    if (!qrcodegen_encodeText(text.toUtf8().constData(), temp, code, qrcodegen_Ecc_LOW,
                              qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
        return QVariant();
    const int n = qrcodegen_getSize(code);
    const int quiet = 4;
    int scale = size / (n + 2 * quiet);
    if (scale < 1)
        scale = 1;
    const int side = (n + 2 * quiet) * scale;

    bb::ImageData img(bb::PixelFormat::RGBA_Premultiplied, side, side);
    unsigned char *pixels = img.pixels();
    const int stride = img.bytesPerLine();
    for (int y = 0; y < side; ++y) {
        unsigned char *row = pixels + y * stride;
        const int my = y / scale - quiet;
        for (int x = 0; x < side; ++x) {
            const int mx = x / scale - quiet;
            const bool dark = mx >= 0 && my >= 0 && mx < n && my < n && qrcodegen_getModule(code, mx, my);
            const unsigned char v = dark ? 0 : 255;
            row[4 * x] = v;
            row[4 * x + 1] = v;
            row[4 * x + 2] = v;
            row[4 * x + 3] = 255;
        }
    }
    return QVariant::fromValue(bb::cascades::Image(img));
}
