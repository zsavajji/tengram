// Run with Qt 4 or 5 core (the Mac has none; a debian container with qtbase5-dev does):
//   g++ -fPIC -std=c++11 -I app/src $(pkg-config --cflags --libs Qt5Core) app/tests/bigemoji_test.cpp -o /tmp/bigemoji_test && /tmp/bigemoji_test
#include "BigEmoji.h"

#include <assert.h>
#include <stdio.h>

static void check(const char *in, int want)
{
    const int got = bigEmoji(QString::fromUtf8(in));
    if (got != want) {
        printf("FAIL %s: want %d, got %d\n", in, want, got);
        assert(false);
    }
}

int main()
{
    check("😀", 1);
    check("❤️", 1);
    check("❤", 1);
    check(" 👍🏽 ", 1);                  // a skin tone, white space around
    check("👨‍👩‍👧", 1);                     // a ZWJ family
    check("🇮🇹", 1);                     // a flag
    check("🇮🇹🇩🇪", 2);
    check("1️⃣", 1);                      // a keycap
    check("😀 😃", 2);
    check("😀😃😄", 3);
    check("😀😃😄😁", 0);                 // four is text
    check("", 0);
    check("1", 0);
    check("hi 😀", 0);
    check("★", 0);                        // a symbol, not an emoji
    check("🏽", 0);                       // a skin tone alone
    printf("ok\n");
    return 0;
}
