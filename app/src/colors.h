#ifndef COLORS_H
#define COLORS_H

#include <QString>
#include <QStringList>

// One of n colours for a key (a jid, a sender), the same on every run. Not qHash: Qt 4's string
// hash keeps the last character in its low bits, so every "…@s.whatsapp.net" got one colour
// (Q10, 2026-10-31). FNV-1a spreads them.
inline const char *pickColor(const QString &key, const char *const *colors, int n)
{
    uint h = 2166136261u;
    for (int i = 0; i < key.size(); ++i) {
        h ^= key.at(i).unicode();
        h *= 16777619u;
    }
    return colors[h % n];
}

// Tile colours, from the BB10 guidelines' icon palette and accents, picked by a hash of the jid
// so a chat keeps its colour (in the list, the contact picker and the chat's title bar).
static const char *const kColors[] = { "#779933", "#CC3F10", "#087099", "#A30D7E" };
static const int kColorCount = sizeof kColors / sizeof *kColors;

// Sender name colours: Telegram's seven peer colours, in the order of its accent ids (0 red, 1 orange, 2 violet,
// 3 green, 4 cyan, 5 blue, 6 pink), so the engine's senders.get names one by its index. Until it answers, a sender
// has the one pickColor gives their id. Shared by the chat's cards and the group info page, so a member has one
// colour in both.
static const char *const kSenders[] = { "#E17076", "#FAA774", "#A695E7", "#7BC862", "#6EC9CB", "#65AADD",
                                        "#EE7AAE" };
static const int kSenderCount = sizeof kSenders / sizeof *kSenders;

// The first character of a word, a whole surrogate pair when it's one.
inline QString firstChar(const QString &word)
{
    return word.left(word.at(0).isHighSurrogate() ? 2 : 1).toUpper();
}

// Up to two initials for a tile; "#" for a bare number.
inline QString initials(const QString &name)
{
    const QStringList words = name.split(' ', QString::SkipEmptyParts);
    if (words.isEmpty() || name.startsWith('+'))
        return "#";
    QString s = firstChar(words.first());
    if (words.size() > 1 && words.last().at(0).isLetter())
        s += firstChar(words.last());
    return s;
}

#endif
