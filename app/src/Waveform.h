#ifndef WAVEFORM_H
#define WAVEFORM_H

#include <QByteArray>
#include <QVariant>

// A voice note's waveform (step 4.3b), drawn into an image for ImageView as QrImage draws its code.
namespace Waveform {
// 64 bars, 3 px wide with a 3 px gap, 45 px tall (the Q10's 5 du), white on transparent so QML
// tints it with filterColor. The BLOB is 64 values of 0 to 100; another length is resampled,
// an empty one gives all-minimum bars.
QVariant image(const QByteArray &values);
// Only the first `bars` bars, at their places: the played part, laid over the grey whole.
QVariant image(const QByteArray &values, int bars);
}

#endif
