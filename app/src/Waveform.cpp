#include "Waveform.h"

#include <QtCore/qmath.h>

#include <bb/ImageData>
#include <bb/PixelFormat>
#include <bb/cascades/Image>

namespace {
const int kBars = 64;
const int kBarW = 3;
const int kWidth = kBars * 2 * kBarW - kBarW;
const int kHeight = 45;
// Drawn: half as many bars, twice as wide, as capsules of this radius, their ends anti-aliased (both shapes, the user,
// 2026-11-19).
const int kRoundBars = kBars / 2;
const int kRoundW = 6;
const int kRoundR = kRoundW / 2;

// The values 0 to 100 of the 64th at `i`, the BLOB resampled as it comes.
int value(const QByteArray &values, int i)
{
    const int n = values.size();
    return qMin(100, n ? int(values.at(i * n / kBars)) : 0);
}

// A bar as a capsule: a pixel's alpha is 3.5 less its distance from the bar's centre line, from 0 to 1.
void roundBar(unsigned char *pixels, int stride, int x0, int h)
{
    const int top = (kHeight - h) / 2;
    const float cx = x0 + kRoundR;
    const float y0 = top + kRoundR;
    const float y1 = top + h - kRoundR;
    for (int y = top; y < top + h; ++y) {
        const float yc = y + 0.5f;
        const float dy = yc < y0 ? y0 - yc : yc > y1 ? yc - y1 : 0;
        for (int x = x0; x < x0 + kRoundW; ++x) {
            const float dx = x + 0.5f - cx;
            float a = kRoundR + 0.5f - qSqrt(dx * dx + dy * dy);
            a = a < 0 ? 0 : a > 1 ? 1 : a;
            for (int c = 0; c < 4; ++c) // premultiplied white: every channel is the alpha
                pixels[y * stride + 4 * x + c] = (unsigned char)(a * 255 + 0.5f);
        }
    }
}
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

    for (int i = 0; i < kRoundBars && 2 * i < bars; ++i) {
        const int v = qMax(value(values, 2 * i), value(values, 2 * i + 1));
        roundBar(pixels, stride, i * 2 * kRoundW, qMax(2 * kRoundR, v * kHeight / 100));
    }
    return QVariant::fromValue(bb::cascades::Image(img));
}
