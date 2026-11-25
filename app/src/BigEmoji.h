#ifndef BIGEMOJI_H
#define BIGEMOJI_H

#include "emojiTable.h"

#include <QSet>
#include <QString>
#include <QVector>

// How many emoji `text` is when it is one to three emoji and nothing else (white space aside), which Telegram's apps draw
// big; 0 otherwise. An emoji starts with one of emojiTable.h's first code points and takes its skin tone, variation
// selectors, tags and ZWJ-joined parts; a flag (two regional indicators) and a keycap count as one.
// Test: app/tests/bigemoji_test.cpp.
inline int bigEmoji(const QString &text)
{
    // The first code point of each emoji in the picker's table: the ones an emoji can start with. ASCII ones (a keycap's
    // digit, # or *) count only before U+20E3, below.
    static QSet<uint> starts;
    if (starts.isEmpty()) {
        for (unsigned g = 0; g < sizeof kEmojiTable / sizeof kEmojiTable[0]; ++g) {
            foreach (const QString &e, QString::fromUtf8(kEmojiTable[g].emoji).split(' ', QString::SkipEmptyParts)) {
                const uint first = e.toUcs4().value(0);
                if (first > 0x7F)
                    starts.insert(first);
            }
        }
    }
    const QVector<uint> cps = text.toUcs4();
    int n = 0;
    bool joined = false; // after a ZWJ: the next one is part of the same emoji
    bool flagOpen = false; // after a flag's first regional indicator
    for (int i = 0; i < cps.size(); ++i) {
        const uint c = cps.at(i);
        if (c == 0x200D) {
            if (n == 0)
                return 0;
            joined = true;
            continue;
        }
        // Variation selectors, skin tones and a subdivision flag's tags belong to the emoji before them.
        if (c == 0xFE0F || c == 0xFE0E || (c >= 0x1F3FB && c <= 0x1F3FF) || (c >= 0xE0020 && c <= 0xE007F)) {
            if (n == 0)
                return 0;
            continue;
        }
        if (c < 0x10000 && QChar(ushort(c)).isSpace()) {
            joined = flagOpen = false;
            continue;
        }
        if (c >= 0x1F1E6 && c <= 0x1F1FF) {
            flagOpen = !flagOpen;
            if (!flagOpen)
                continue; // a flag's second letter
        } else if ((c >= '0' && c <= '9') || c == '#' || c == '*') {
            int j = i + 1;
            if (j < cps.size() && cps.at(j) == 0xFE0F)
                ++j;
            if (j >= cps.size() || cps.at(j) != 0x20E3)
                return 0;
            i = j;
        } else if (!starts.contains(c)) {
            return 0;
        }
        if (!joined && ++n > 3)
            return 0;
        joined = false;
    }
    return n;
}

#endif
