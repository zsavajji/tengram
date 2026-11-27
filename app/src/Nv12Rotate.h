#ifndef NV12ROTATE_H
#define NV12ROTATE_H

#include <stddef.h>
#include <string.h>
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#include <arm_neon.h>
#define NV12_NEON 1
#endif

// Turning a linear NV12 picture upright (step 7c.3): the peer's frames come with the quarter turns
// that make them upright, and Screen's window draws a buffer as it is. Header-only and free of Qt so
// app/tests/rotate_test.cpp builds on the Mac.

// The destination's columns x0..x1-1 of rows y0..y1-1, one element at a time. It is read off in
// order, row by row, because the window's buffer is write-only to the CPU and a scattered write is
// the slow one; the source is walked by steps that are set once per turn.
template <typename T>
inline void rotateRect(const unsigned char *first, ptrdiff_t perX, ptrdiff_t perY, int x0, int x1,
                       int y0, int y1, unsigned char *dst, int dstStride)
{
    const ptrdiff_t sz = sizeof(T);
    for (int y = y0; y < y1; y++) {
        const unsigned char *p = first + y * perY + x0 * perX;
        unsigned char *out = dst + size_t(y) * dstStride + x0 * sz;
        for (int x = x0; x < x1; x++, p += perX, out += sz)
            memcpy(out, p, sizeof(T)); // memcpy: no alignment or aliasing question for the compiler
    }
}

#ifdef NV12_NEON
// A square block of 8 bytes a side: 8x8 luma bytes, or 4x4 chroma pairs.
template <int Size> struct NeonBlock;

template <> struct NeonBlock<1> {
    static const int n = 8;
    static void transpose(uint8x8_t r[8])
    {
        for (int i = 0; i < 8; i += 2) {
            const uint8x8x2_t t = vtrn_u8(r[i], r[i + 1]);
            r[i] = t.val[0];
            r[i + 1] = t.val[1];
        }
        for (int i = 0; i < 8; i += 4)
            for (int j = i; j < i + 2; j++) {
                const uint16x4x2_t t = vtrn_u16(vreinterpret_u16_u8(r[j]), vreinterpret_u16_u8(r[j + 2]));
                r[j] = vreinterpret_u8_u16(t.val[0]);
                r[j + 2] = vreinterpret_u8_u16(t.val[1]);
            }
        for (int j = 0; j < 4; j++) {
            const uint32x2x2_t t = vtrn_u32(vreinterpret_u32_u8(r[j]), vreinterpret_u32_u8(r[j + 4]));
            r[j] = vreinterpret_u8_u32(t.val[0]);
            r[j + 4] = vreinterpret_u8_u32(t.val[1]);
        }
    }
    static uint8x16_t reverse(uint8x16_t v)
    {
        v = vrev64q_u8(v);
        return vcombine_u8(vget_high_u8(v), vget_low_u8(v));
    }
};

template <> struct NeonBlock<2> {
    static const int n = 4;
    static void transpose(uint8x8_t r[4])
    {
        for (int i = 0; i < 4; i += 2) {
            const uint16x4x2_t t = vtrn_u16(vreinterpret_u16_u8(r[i]), vreinterpret_u16_u8(r[i + 1]));
            r[i] = vreinterpret_u8_u16(t.val[0]);
            r[i + 1] = vreinterpret_u8_u16(t.val[1]);
        }
        for (int j = 0; j < 2; j++) {
            const uint32x2x2_t t = vtrn_u32(vreinterpret_u32_u8(r[j]), vreinterpret_u32_u8(r[j + 2]));
            r[j] = vreinterpret_u8_u32(t.val[0]);
            r[j + 2] = vreinterpret_u8_u32(t.val[1]);
        }
    }
    static uint8x16_t reverse(uint8x16_t v)
    {
        v = vreinterpretq_u8_u16(vrev64q_u16(vreinterpretq_u16_u8(v)));
        return vcombine_u8(vget_high_u8(v), vget_low_u8(v));
    }
};
#endif

// One plane of w x h elements of T (1 byte for luma, a 2-byte U,V pair for chroma) turned clockwise
// by `turns`. With NEON, whole blocks go first and rotateRect does the right and bottom edges.
template <typename T>
inline void rotatePlane(const unsigned char *src, int w, int h, int srcStride, int turns,
                        unsigned char *dst, int dstStride)
{
    const ptrdiff_t sz = sizeof(T), row = srcStride;
    const int dw = turns & 1 ? h : w, dh = turns & 1 ? w : h;
    const unsigned char *first;  // the source element that lands at the destination's top left
    ptrdiff_t perX, perY;        // source steps for one destination column, one destination row
    switch (turns & 3) {
    case 0: first = src;                            perX = sz;   perY = row; break;
    case 1: first = src + (h - 1) * row;            perX = -row; perY = sz;  break;
    case 2: first = src + (h - 1) * row + (w - 1) * sz; perX = -sz; perY = -row; break;
    default: first = src + (w - 1) * sz;            perX = row;  perY = -sz; break;
    }
    int doneX = 0, doneY = 0;
#ifdef NV12_NEON
    typedef NeonBlock<sizeof(T)> B;
    if (turns & 1) {
        const bool back = (turns & 3) == 3; // the source row runs against the destination's rows
        doneX = dw - dw % B::n;
        doneY = dh - dh % B::n;
        for (int y = 0; y < doneY; y += B::n) {
            for (int x = 0; x < doneX; x += B::n) {
                const unsigned char *p = first + y * perY + x * perX - (back ? (B::n - 1) * sz : 0);
                uint8x8_t r[B::n];
                for (int i = 0; i < B::n; i++)
                    r[i] = vld1_u8(p + i * perX);
                B::transpose(r);
                unsigned char *out = dst + size_t(y) * dstStride + x * sz;
                for (int j = 0; j < B::n; j++)
                    vst1_u8(out + size_t(j) * dstStride, r[back ? B::n - 1 - j : j]);
            }
        }
    } else if ((turns & 3) == 2) {
        const int per = 16 / sizeof(T);
        doneX = dw - dw % per;
        doneY = dh;
        for (int y = 0; y < dh; y++) {
            const unsigned char *p = first + y * perY - (per - 1) * sz;
            unsigned char *out = dst + size_t(y) * dstStride;
            for (int x = 0; x < doneX; x += per, p -= 16, out += 16)
                vst1q_u8(out, B::reverse(vld1q_u8(p)));
        }
    }
#endif
    rotateRect<T>(first, perX, perY, doneX, dw, 0, doneY, dst, dstStride); // right of the blocks
    rotateRect<T>(first, perX, perY, 0, dw, doneY, dh, dst, dstStride);    // below them
}

// The (U,V) pair as one 2-byte element, so the chroma turns as a half-size picture. (At namespace
// scope: the NDK's gcc takes no local type as a template argument.)
struct Nv12Pair { unsigned char u, v; };

// Linear NV12, w x h with even sides, rotated clockwise by quarterTurns (0 to 3; 0 copies). The
// output is h x w for an odd count. y and uv are the source planes (uv: w/2 pairs on h/2 rows), both
// with srcStride bytes per row; dy and duv the destination's, with their own strides.
inline void rotateNv12(const unsigned char *y, const unsigned char *uv, int w, int h, int srcStride,
                       int quarterTurns, unsigned char *dy, int dyStride, unsigned char *duv, int duvStride)
{
    rotatePlane<unsigned char>(y, w, h, srcStride, quarterTurns, dy, dyStride);
    rotatePlane<Nv12Pair>(uv, w / 2, h / 2, srcStride, quarterTurns, duv, duvStride);
}

// The centred part of a w x h picture with the view's aspect (vw x vh): what a fill shows, the
// rest cropped. Even sides and offsets, for NV12. The whole picture if the view has no size yet.
inline void centreCrop(int w, int h, int vw, int vh, int *x, int *y, int *cw, int *ch)
{
    *cw = w;
    *ch = h;
    if (vw > 0 && vh > 0) {
        if ((long long)w * vh > (long long)vw * h)
            *cw = int((long long)h * vw / vh) & ~1; // wider than the view: the sides go
        else
            *ch = int((long long)w * vh / vw) & ~1; // taller: the top and foot go
    }
    *x = ((w - *cw) / 2) & ~1;
    *y = ((h - *ch) / 2) & ~1;
}

#endif
