#include "Profile.h"

#include "EngineLink.h"
#include "colors.h"

#include <QDir>
#include <QImage>
#include <QImageReader>

// The side of the picture sent: what WhatsApp's own apps make.
static const int kPhotoSide = 640;

Profile::Profile(QObject *parent)
    : QObject(parent)
    , m_wanted(false)
{
}

QObject *Profile::link() const
{
    return m_engine;
}

// A QPointer, as ContactInfo's: at app exit the page may outlive the EngineLink.
void Profile::setLink(QObject *link)
{
    m_engine = qobject_cast<EngineLink *>(link);
    if (m_engine) {
        connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
                SLOT(onReplied(QString, QString, QVariant)));
        connect(m_engine, SIGNAL(failureChanged()), this, SLOT(onFailure()));
        connect(m_engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
    }
}

// A page opened while the engine (re)starts asks before it's there: the request fails with
// no_engine and nothing would ask again (Q10, 2026-11-01). So it's asked again on connecting.
void Profile::onConnected()
{
    if (m_engine && m_engine->connected() && m_wanted && m_info.isEmpty())
        ask();
}

QVariantMap Profile::info() const
{
    return m_info;
}

void Profile::ask()
{
    if (!m_engine)
        return;
    m_wanted = true;
    m_id = m_engine->request("profile.get", QVariantMap());
}

void Profile::set(const QString &key, const QString &value)
{
    if (!m_engine)
        return;
    QVariantMap args;
    args[key] = value;
    m_engine->request("profile.set", args);
}

void Profile::setName(const QString &name)
{
    set("name", name);
}

void Profile::setAbout(const QString &about)
{
    set("about", about);
}

void Profile::removePhoto()
{
    // "" removes the picture; the engine reads a missing field as "not given".
    set("picture", QString(""));
}

// ponytail: EXIF orientation isn't applied (Qt 4.8's reader ignores it), as photoInfo's; the
// picker's crop writes the picture as shown. The upload stays in cache/ until the next one or
// Clear cache.
bool Profile::setPhoto(const QString &croppedPath)
{
    QImageReader reader(croppedPath);
    const QSize size = reader.size(); // from the header
    if (!size.isValid())
        return false;
    // Decoded straight to the size whose short side is 640, as photoInfo's thumbnail is (the
    // JPEG reader scales while it decodes, Q10 0.0.1.115), then the centred square is cut out.
    // Not setClipRect: Qt 4.8's JPEG handler may not clip, and then the reader decodes the whole
    // picture before it clips.
    const qreal scale = qreal(kPhotoSide) / qMin(size.width(), size.height());
    reader.setScaledSize(QSize(qMax(kPhotoSide, qRound(size.width() * scale)),
                               qMax(kPhotoSide, qRound(size.height() * scale))));
    QImage image = reader.read();
    if (image.isNull())
        return false;
    const int side = qMin(image.width(), image.height());
    image = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side);
    if (side != kPhotoSide)
        image = image.scaled(kPhotoSide, kPhotoSide, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QString dir = QDir::homePath() + "/cache";
    const QString path = dir + "/profile-upload.jpg";
    if (!QDir().mkpath(dir) || !image.save(path, "JPEG", 85))
        return false;
    set("picture", path);
    return true;
}

// A set's reply is answered by asking again, so the page shows what the engine has. The cmd is
// enough: only this object sends profile.set.
void Profile::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    if (cmd == "profile.set") {
        ask();
        return;
    }
    if (cmd != "profile.get" || id != m_id)
        return;
    QVariantMap info = result.toMap();
    info["initials"] = initials(info["name"].toString());
    info["color"] = pickColor(info["phone"].toString(), kColors, kColorCount);
    m_info = info;
    emit infoChanged();
}

void Profile::onFailure()
{
    if (m_engine && m_engine->failure().value("cmd").toString() == "profile.set")
        emit failed();
}
