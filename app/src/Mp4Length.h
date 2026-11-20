#ifndef MP4LENGTH_H
#define MP4LENGTH_H

#include <stdio.h>
#include <string.h>

// An MP4's length in ms, read from the file: moov's mvhd box, or mvex's mehd when the file is fragmented (moof boxes;
// its mvhd and track headers say 0, and mm-renderer then reports no duration: a 3:44 video showed 0:00 / 0:00 and its
// timeline sought to 0, the user 2026-11-20). 0 when it can't be read, as for a fragmented file without mehd.
// engine/requests.cpp's video_seconds is the engine's reader (whole seconds, mvhd only, for sending).
// Run app/tests/mp4length_test.cpp after a change.
namespace Mp4Length
{
inline unsigned long long be(const unsigned char *p, int n)
{
    unsigned long long v = 0;
    for (int i = 0; i < n; ++i) {
        v = v << 8 | p[i];
    }
    return v;
}

// The boxes from at to end, into moov and mvex; mvex says whether an mvex box was seen. moov may come after the media
// (mdat), so a box is skipped by its size, not read.
inline void walk(FILE *f, long at, long end, unsigned long long &scale, unsigned long long &whole,
                 unsigned long long &fragments, bool &mvex)
{
    unsigned char h[16];
    while (at + 8 <= end && fseek(f, at, SEEK_SET) == 0 && fread(h, 1, 8, f) == 8) {
        unsigned long long size = be(h, 4);
        long head = 8;
        if (size == 1) { // the size follows as 64 bits
            if (fread(h + 8, 1, 8, f) != 8) {
                return;
            }
            size = be(h + 8, 8);
            head = 16;
        } else if (size == 0) { // to the end
            size = (unsigned long long)(end - at);
        }
        if (size < (unsigned long long)head || size > (unsigned long long)(end - at)) {
            return;
        }
        const long body = (long)size - head;
        unsigned char b[32];
        if (memcmp(h + 4, "moov", 4) == 0 || memcmp(h + 4, "mvex", 4) == 0) {
            mvex = mvex || memcmp(h + 4, "mvex", 4) == 0;
            walk(f, at + head, at + (long)size, scale, whole, fragments, mvex);
        } else if (memcmp(h + 4, "mvhd", 4) == 0 && body >= 32 && fread(b, 1, 32, f) == 32) {
            // version, flags; the two dates, the timescale and the length: 4 bytes each in version 0, 8 (but the
            // timescale) in version 1. All ones is "unknown".
            const bool wide = b[0] == 1;
            scale = be(b + (wide ? 20 : 12), 4);
            whole = be(b + (wide ? 24 : 16), wide ? 8 : 4);
            if (whole == (wide ? ~0ULL : 0xFFFFFFFFULL)) {
                whole = 0;
            }
        } else if (memcmp(h + 4, "mehd", 4) == 0 && body >= 8 && fread(b, 1, 8, f) == 8) {
            // version, flags; the length of all the fragments, in mvhd's timescale
            const bool wide = b[0] == 1;
            if (!wide || (body >= 12 && fread(b + 8, 1, 4, f) == 4)) {
                fragments = be(b + 4, wide ? 8 : 4);
            }
        }
        at += (long)size;
    }
}

// Both answers from one open and walk of the file.
struct Info
{
    long long ms;
    bool fragmented;
};

// ms: the length, 0 when it can't be read. fragmented: a fragmented MP4, whose moov has an mvex box and mvhd gives no
// length; the phone's player plays such a file but can't seek in it (it answers UnsupportedOperation and stops, 1.0.0.90).
inline Info info(const char *path)
{
    Info r = {0, false};
    FILE *f = fopen(path, "rb");
    if (!f) {
        return r;
    }
    unsigned long long scale = 0, whole = 0, fragments = 0;
    bool mvex = false;
    if (fseek(f, 0, SEEK_END) == 0) {
        walk(f, 0, ftell(f), scale, whole, fragments, mvex);
    }
    fclose(f);
    const unsigned long long length = whole > 0 ? whole : fragments;
    r.ms = scale > 0 ? (long long)(length * 1000 / scale) : 0;
    r.fragmented = mvex && whole == 0;
    return r;
}

inline long long ms(const char *path)
{
    return info(path).ms;
}

inline bool fragmented(const char *path)
{
    return info(path).fragmented;
}
}

#endif
