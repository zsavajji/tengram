// Run on the Mac: clang++ -std=c++11 -I app/src app/tests/rotate_test.cpp -o /tmp/rotate_test && /tmp/rotate_test
#include "Nv12Rotate.h"

#include <assert.h>
#include <stdio.h>

// A 4x2 frame, rows padded to 6 bytes with 0xEE (which must never be copied), turned 0..3 times.
// The expected pictures are written out by hand, row by row.
int main()
{
    const unsigned char y[2 * 6] = { 0, 1, 2, 3, 0xEE, 0xEE,
                                     4, 5, 6, 7, 0xEE, 0xEE };
    // 2 pairs on 1 row: (10,11) and (12,13)
    const unsigned char uv[1 * 6] = { 10, 11, 12, 13, 0xEE, 0xEE };
    const unsigned char expY[4][4 * 4] = {
        { 0, 1, 2, 3,   4, 5, 6, 7 },
        { 4, 0,   5, 1,   6, 2,   7, 3 },
        { 7, 6, 5, 4,   3, 2, 1, 0 },
        { 3, 7,   2, 6,   1, 5,   0, 4 },
    };
    const unsigned char expUv[4][4] = {
        { 10, 11, 12, 13 },
        { 10, 11,   12, 13 },        // 1 wide, 2 tall: a then b
        { 12, 13, 10, 11 },
        { 12, 13,   10, 11 },        // b then a
    };
    for (int turns = 0; turns < 4; turns++) {
        const int dw = turns & 1 ? 2 : 4, dh = turns & 1 ? 4 : 2;       // luma
        const int cw = turns & 1 ? 1 : 2, ch = turns & 1 ? 2 : 1;       // chroma pairs
        unsigned char oy[5 * 8], ouv[5 * 8];
        memset(oy, 0xAA, sizeof oy);
        memset(ouv, 0xAA, sizeof ouv);
        rotateNv12(y, uv, 4, 2, 6, turns, oy, 5, ouv, 5);                // destination strides of 5
        for (int r = 0; r < dh; r++) {
            for (int c = 0; c < dw; c++)
                assert(oy[r * 5 + c] == expY[turns][r * dw + c]);
            assert(oy[r * 5 + dw] == 0xAA); // no write past the row
        }
        for (int r = 0; r < ch; r++) {
            for (int c = 0; c < cw * 2; c++)
                assert(ouv[r * 5 + c] == expUv[turns][r * cw * 2 + c]);
            assert(ouv[r * 5 + cw * 2] == 0xAA);
        }
    }
    // Sizes with whole NEON blocks and edges, against the plain definition.
    const int sizes[][2] = { { 16, 8 }, { 34, 18 }, { 64, 48 }, { 90, 70 } };
    for (int s = 0; s < 4; s++) {
        const int w = sizes[s][0], h = sizes[s][1], ss = w + 6, ds = (w > h ? w : h) + 10;
        static unsigned char sy[100 * 100], suv[100 * 50], oy[100 * 100], ouv[100 * 50];
        for (int i = 0; i < ss * h; i++) sy[i] = (unsigned char)(i * 37 + i / 251);
        for (int i = 0; i < ss * h / 2; i++) suv[i] = (unsigned char)(i * 53 + i / 241);
        for (int turns = 0; turns < 4; turns++) {
            memset(oy, 0xAA, sizeof oy);
            memset(ouv, 0xAA, sizeof ouv);
            rotateNv12(sy, suv, w, h, ss, turns, oy, ds, ouv, ds);
            for (int plane = 0; plane < 2; plane++) {
                const int sz = plane + 1, pw = plane ? w / 2 : w, ph = plane ? h / 2 : h;
                const int dw = turns & 1 ? ph : pw, dh = turns & 1 ? pw : ph;
                const unsigned char *src = plane ? suv : sy, *out = plane ? ouv : oy;
                for (int r = 0; r < dh; r++) {
                    for (int c = 0; c < dw; c++) {
                        const int x = turns == 0 ? c : turns == 1 ? r : turns == 2 ? pw - 1 - c : pw - 1 - r;
                        const int y = turns == 0 ? r : turns == 1 ? ph - 1 - c : turns == 2 ? ph - 1 - r : c;
                        assert(!memcmp(out + r * ds + c * sz, src + y * ss + x * sz, sz));
                    }
                    assert(out[r * ds + dw * sz] == 0xAA);
                }
                assert(out[dh * ds] == 0xAA);
            }
        }
    }
    // The crop a fill shows: a portrait picture in a square view, a landscape one, no view yet.
    int x, yy, cw, ch;
    centreCrop(212, 320, 720, 720, &x, &yy, &cw, &ch);
    assert(x == 0 && yy == 54 && cw == 212 && ch == 212);
    centreCrop(320, 180, 720, 720, &x, &yy, &cw, &ch);
    assert(x == 70 && yy == 0 && cw == 180 && ch == 180);
    centreCrop(320, 212, 0, 0, &x, &yy, &cw, &ch);
    assert(x == 0 && yy == 0 && cw == 320 && ch == 212);
    printf("rotate ok\n");
    return 0;
}
