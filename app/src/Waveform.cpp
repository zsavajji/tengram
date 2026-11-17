#include "Waveform.h"

#include <bb/ImageData>
#include <bb/PixelFormat>
#include <bb/cascades/Image>

namespace {
const int kBars = 64;
const int kBarW = 3;
const int kWidth = kBars * 2 * kBarW - kBarW;
const int kHeight = 45;
const int kMinBar = 3;
}

QVariant Waveform::image(const QByteArray &values)
{
    return image(values, kBars);
}

QVariant Waveform::image(const QByteArray &values, int bars)
{
    bb::ImageData img(bb::PixelFormat::RGBA_Premultiplied, kWidth, kHeight);
    unsigned char *pixels = img.pixels();
    const int stride = img.bytesPerLine();
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth * 4; ++x)
            pixels[y * stride + x] = 0;

    const int n = values.size();
    for (int i = 0; i < kBars && i < bars; ++i) {
        int v = n ? values.at(i * n / kBars) : 0;
        if (v > 100)
            v = 100;
        int h = v * kHeight / 100;
        if (h < kMinBar)
            h = kMinBar;
        const int top = (kHeight - h) / 2;
        for (int y = top; y < top + h; ++y)
            for (int x = i * 2 * kBarW; x < i * 2 * kBarW + kBarW; ++x)
                for (int c = 0; c < 4; ++c)
                    pixels[y * stride + 4 * x + c] = 255;
    }
    return QVariant::fromValue(bb::cascades::Image(img));
}
