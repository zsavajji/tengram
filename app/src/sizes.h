#ifndef SIZES_H
#define SIZES_H

#include <QLocale>
#include <QString>

// Whole KB under 1 MB (at least 1), else MB with one decimal in the phone's region; "" when the
// size is unknown (0 or less).
inline QString sizeText(qint64 bytes, const QLocale &locale)
{
    if (bytes <= 0)
        return QString();
    if (bytes < 1024 * 1024)
        return QString("%1 KB").arg(qMax<qint64>(1, bytes / 1024));
    return QString("%1 MB").arg(locale.toString(bytes / (1024.0 * 1024.0), 'f', 1));
}

#endif
