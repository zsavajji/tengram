#ifndef QRIMAGE_H
#define QRIMAGE_H

#include <QObject>
#include <QString>
#include <QVariant>

// QR codes for the linking page (PLAN.md §7), drawn by qrcodegen into an image for ImageView.
class QrImage : public QObject
{
    Q_OBJECT
public:
    explicit QrImage(QObject *parent = 0);

    // text as a QR code: black modules on white, with the standard 4-module quiet zone, each
    // module a whole number of pixels (no blur), as large as fits in size px. An empty QVariant
    // when text is empty or doesn't fit in a QR code.
    Q_INVOKABLE QVariant image(const QString &text, int size);
};

#endif
