#ifndef WAVEFORM_H
#define WAVEFORM_H

#include <QByteArray>
#include <QVariant>

// A voice note's waveform (step 4.3b), drawn into an image for ImageView as QrImage draws its code.
namespace Waveform {
// 32 bars, 6 px wide with a 6 px gap and rounded ends, 45 px tall (the Q10's 5 du), in 381 px, white on transparent so
// QML tints it with filterColor; in both shapes (the user, 2026-11-19; Square had 64 bars of 3 px). The BLOB is 64
// values of 0 to 100, each bar the higher of the two it covers; another length is resampled, an empty one gives
// all-minimum bars.
QVariant image(const QByteArray &values);
// Only the first `bars` 64ths, at their places: the played part, laid over the grey whole.
QVariant image(const QByteArray &values, int bars);
}

#endif
