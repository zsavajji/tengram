#include "VideoDecoder.h"

#include <string.h>

// The stream and picture formats VideoDecoder deals in: Annex-B access units in, Qualcomm's
// tiled NV12 out.

struct Nal
{
    int start; // of the start code, its leading zero of a 4-byte one included
    int type;
};

static QList<Nal> scanNals(const QByteArray &d)
{
    QList<Nal> out;
    const unsigned char *p = (const unsigned char *)d.constData();
    const int n = d.size();
    for (int i = 0; i + 3 < n; i++) {
        if (!p[i] && !p[i + 1] && p[i + 2] == 1) {
            // The type is the byte after the 3-byte code whichever length the start code has.
            Nal nal = { (i && !p[i - 1]) ? i - 1 : i, p[i + 3] & 31 };
            out.append(nal);
            i += 2;
        }
    }
    return out;
}

QList<QByteArray> VideoDecoder::splitAccessUnits(const QByteArray &d)
{
    QList<Nal> nals = scanNals(d);
    QList<QByteArray> out;
    int start = -1;
    bool have = false;
    for (int k = 0; k < nals.size(); k++) {
        const int t = nals[k].type;
        if (start < 0) {
            start = nals[k].start;
        } else if (have && (t == 1 || t == 5 || t == 6 || t == 7 || t == 8 || t == 9)) {
            out.append(d.mid(start, nals[k].start - start));
            start = nals[k].start;
            have = false;
        }
        if (t == 1 || t == 5)
            have = true;
    }
    if (start >= 0)
        out.append(d.mid(start));
    return out;
}

// SPS and PPS of an access unit, start codes kept; empty if it has none.
QByteArray VideoDecoder::parameterSets(const QByteArray &d)
{
    QList<Nal> nals = scanNals(d);
    QByteArray out;
    for (int k = 0; k < nals.size(); k++) {
        if (nals[k].type == 7 || nals[k].type == 8) {
            const int end = k + 1 < nals.size() ? nals[k + 1].start : d.size();
            out.append(d.mid(nals[k].start, end - nals[k].start));
        }
    }
    return out;
}

// Qualcomm's 64x32 tile order inside a plane (found by trying; the interleave of rows 2 apart).
static size_t tilePos(size_t x, size_t y, size_t w, size_t h)
{
    size_t f = x + (y & ~size_t(1)) * w;
    if (y & 1)
        f += (x & ~size_t(3)) + 2;
    else if ((h & 1) == 0 || y != h - 1)
        f += (x + 2) & ~size_t(3);
    return f;
}

bool VideoDecoder::detile(const Frame &f, unsigned char *y, int yStride, unsigned char *uv, int uvStride)
{
    const size_t W = f.width, H = f.height;
    const size_t tw = (W + 63) / 64, twa = (tw + 1) & ~size_t(1);
    const size_t th = (H + 31) / 32, cth = (H / 2 + 31) / 32;
    // The chroma plane follows the luma one, which is padded to 8 KB.
    const size_t lumaBytes = (twa * th * 2048 + 8191) & ~size_t(8191);
    if (f.width <= 0 || f.height <= 0 || size_t(f.size) < lumaBytes + twa * cth * 2048)
        return false;
    for (int p = 0; p < 2; p++) {
        const size_t rows = p ? H / 2 : H, tileRows = p ? cth : th;
        const unsigned char *plane = f.data + (p ? lumaBytes : 0);
        unsigned char *dst = p ? uv : y;
        const size_t stride = p ? uvStride : yStride;
        for (size_t ty = 0; ty < tileRows; ty++) {
            for (size_t tx = 0; tx < tw; tx++) {
                const unsigned char *tile = plane + tilePos(tx, ty, twa, tileRows) * 2048;
                const size_t n = W - tx * 64 < 64 ? W - tx * 64 : 64;
                for (size_t r = 0; r < 32 && ty * 32 + r < rows; r++)
                    memcpy(dst + (ty * 32 + r) * stride + tx * 64, tile + r * 64, n);
            }
        }
    }
    return true;
}

