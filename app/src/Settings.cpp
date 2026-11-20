#include "Settings.h"

#include <bb/cascades/Application>
#include <bb/cascades/Color>
#include <bb/cascades/ThemeSupport>
#include <bb/cascades/VisualStyle>
#include <bb/system/InvokeDateTime>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRecurrenceRule>
#include <bb/system/InvokeRecurrenceRuleFrequency>
#include <bb/system/InvokeReply>
#include <bb/system/InvokeReplyError>
#include <bb/system/InvokeTimerRequest>
#include <QCryptographicHash>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QTime>

#include "EngineLink.h"
#include "PreviewMaker.h"
#include "StickerPlayer.h"
#include "sizes.h"

bool Settings::s_roundShape = true; // Round is the default (the user, 2026-11-19), as round() reads it
int Settings::s_stickerPlay = 0; // Loop, as stickerPlay() reads it
int Settings::s_stickerSize = 1; // Normal, as stickerSize() reads it

Settings::Settings(EngineLink *engine, QObject *parent)
    : QObject(parent),
      m_file(QDir::homePath() + "/settings.ini", QSettings::IniFormat),
      m_engine(engine),
      m_region(bb::system::LocaleType::Region)
{
    s_roundShape = round();
    s_stickerPlay = stickerPlay();
    s_stickerSize = stickerSize();
    // Dark is the bar descriptor's theme (CASCADES_THEME): nothing to apply, and no reload.
    if (theme() == 1)
        applyTheme();
    // Refused while the timer stands (2, 12292, at every start on the Q10), though navigator_invoke.h
    // says a new one replaces the old. Harmless: it survived a reboot and about ten reinstalls. It
    // should add the timer again if one is ever lost, which is unproven.
    if (startAtBoot())
        applyBootTimer(true);
}

int Settings::theme() const { return m_file.value("theme", 0).toInt() == 1 ? 1 : 0; }
bool Settings::enterSends() const { return m_file.value("enterSends", true).toBool(); }
bool Settings::notifyPreview() const { return m_file.value("notifyPreview", true).toBool(); }
bool Settings::startAtBoot() const { return m_file.value("startAtBoot", false).toBool(); }
QString Settings::autoNet() const { return dataPrefs(m_file)["autoNet"].toString(); }
bool Settings::autoPhotos() const { return dataPrefs(m_file)["autoPhotos"].toBool(); }
int Settings::autoVideoMB() const { return dataPrefs(m_file)["autoVideoMB"].toInt(); }
int Settings::autoFileMB() const { return dataPrefs(m_file)["autoFileMB"].toInt(); }
bool Settings::galleryPrivate() const { return dataPrefs(m_file)["galleryPrivate"].toBool(); }
bool Settings::galleryGroups() const { return dataPrefs(m_file)["galleryGroups"].toBool(); }
bool Settings::galleryChannels() const { return dataPrefs(m_file)["galleryChannels"].toBool(); }
int Settings::keepMB() const { return dataPrefs(m_file)["keepMB"].toInt(); }
bool Settings::earSensor() const { return m_file.value("earSensor", true).toBool(); }
int Settings::cacheLimit() const { return qBound(64, m_file.value("cacheLimit", 64).toInt(), 512); }
QString Settings::deliveryMode() const { return m_file.value("deliveryMode", "auto").toString(); }
// Larger (3, 140 %) went at the user's word (2026-11-01); a saved 3 reads as Large.
int Settings::textSize() const { return qBound(0, m_file.value("textSize", 1).toInt(), 2); }
int Settings::videoQuality() const { return qBound(0, m_file.value("videoQuality", 0).toInt(), 3); }
int Settings::stickerPlay() const { return qBound(0, m_file.value("stickerPlay", 0).toInt(), 2); }
int Settings::stickerSize() const { return qBound(0, m_file.value("stickerSize", 1).toInt(), 2); }

qreal Settings::stickerScale()
{
    static const qreal scales[] = { 0.7, 1.0, 1.2 };
    return scales[s_stickerSize];
}
bool Settings::compact() const { return m_file.value("compact", false).toBool(); }
// Round by default (the user, 2026-11-19; Square was until then): a saved choice stays.
bool Settings::round() const { return m_file.value("round", true).toBool(); }
bool Settings::darkGrey() const { return m_file.value("darkGrey", false).toBool(); }
int Settings::callHints() const { return qMax(0, m_file.value("callHints", 0).toInt()); }
bool Settings::syncContacts() const { return m_file.value("syncContacts", false).toBool(); }

qreal Settings::textScale() const
{
    static const qreal scales[] = { 0.9, 1.0, 1.2 };
    return scales[textSize()];
}

void Settings::store(const char *key, const QVariant &value)
{
    m_file.setValue(key, value);
    m_file.sync();
}

// With the primary colours of the bar descriptor's CASCADES_THEME, so a switch keeps them.
void Settings::applyTheme()
{
    using namespace bb::cascades;
    Application::instance()->themeSupport()->setVisualStyleAndPrimaryColor(
        theme() == 1 ? VisualStyle::Bright : VisualStyle::Dark, Color::fromARGB(0xff24a1de),
        Color::fromARGB(0xff1b79a7));
}

void Settings::setTheme(int value)
{
    value = value == 1 ? 1 : 0;
    if (value == theme())
        return;
    store("theme", value);
    applyTheme();
    emit themeChanged();
}

void Settings::setEnterSends(bool on)
{
    if (on == enterSends())
        return;
    store("enterSends", on);
    emit enterSendsChanged();
}

void Settings::setEarSensor(bool on)
{
    if (on == earSensor())
        return;
    store("earSensor", on);
    emit earSensorChanged();
}

void Settings::setNotifyPreview(bool on)
{
    if (on == notifyPreview())
        return;
    store("notifyPreview", on);
    // Not connected: the hello path sends it on the next connect.
    if (m_engine->connected()) {
        QVariantMap prefs;
        prefs["notifyPreview"] = on;
        m_engine->request("prefs.set", prefs);
    }
    emit notifyPreviewChanged();
}

void Settings::setStartAtBoot(bool on)
{
    if (on == startAtBoot())
        return;
    store("startAtBoot", on);
    applyBootTimer(on);
    emit startAtBootChanged();
}

// Start at boot (step 8.2). It can't happen at boot itself: an unsigned app runs only once root is
// up. So a recurring timer on the engine's headless target, which survives reboots
// (navigator_invoke.h): the fires before root is up fail, the first after starts the engine, and one
// that finds it running does nothing, so a killed engine comes back too. Every 6 minutes is the
// shortest the service takes (InvokeRecurrenceRule.hpp).
static const char *const kBootTimer = "tengram-boot";

void Settings::applyBootTimer(bool on)
{
    bb::system::InvokeManager *invoker = m_engine->invoker();
    bb::system::InvokeReply *reply = 0;
    if (on) {
        bb::system::InvokeRecurrenceRule rule(bb::system::InvokeRecurrenceRuleFrequency::Minutely,
                                              bb::system::InvokeDateTime(QDate::currentDate(), QTime(0, 0)));
        rule.setInterval(6);
        if (!rule.isValid()) {
            qWarning() << "boot timer: the rule is not valid";
            return;
        }
        reply = invoker->registerTimer(bb::system::InvokeTimerRequest(kBootTimer, rule, "com.zsavajji.tengram.service"));
    } else {
        reply = invoker->deregisterTimer(kBootTimer);
    }
    if (!reply) {
        qWarning() << "boot timer: request not sent";
        return;
    }
    reply->setProperty("on", on);
    connect(reply, SIGNAL(finished()), this, SLOT(bootTimerReplied()));
}

void Settings::bootTimerReplied()
{
    bb::system::InvokeReply *reply = qobject_cast<bb::system::InvokeReply *>(sender());
    if (!reply)
        return;
    if (reply->error() == bb::system::InvokeReplyError::None)
        qDebug() << "boot timer:" << (reply->property("on").toBool() ? "registered" : "removed");
    else
        qWarning() << "boot timer: failed," << int(reply->error()) << reply->errorCode();
    reply->deleteLater();
}

// The defaults are the engine's (docs/protocol.md, "prefs.set"); the limits are the ones it refuses a value outside of,
// which would leave the whole prefs.set unapplied.
QVariantMap Settings::dataPrefs(const QSettings &file)
{
    QVariantMap prefs;
    const QString net = file.value("autoNet", "wifi").toString();
    prefs["autoNet"] = net == "none" || net == "all" ? net : QString("wifi");
    prefs["autoPhotos"] = file.value("autoPhotos", true).toBool();
    prefs["autoVideoMB"] = qBound(0, file.value("autoVideoMB", 15).toInt(), 2000);
    prefs["autoFileMB"] = qBound(0, file.value("autoFileMB", 3).toInt(), 2000);
    prefs["galleryPrivate"] = file.value("galleryPrivate", false).toBool();
    prefs["galleryGroups"] = file.value("galleryGroups", false).toBool();
    prefs["galleryChannels"] = file.value("galleryChannels", false).toBool();
    const int keep = file.value("keepMB", 2048).toInt();
    prefs["keepMB"] = keep <= 0 ? 0 : qBound(256, keep, 20000);
    return prefs;
}

bool Settings::storeData(const char *key, const QVariant &value)
{
    const QVariant before = dataPrefs(m_file)[key];
    store(key, value);
    const QVariant now = dataPrefs(m_file)[key];
    if (now == before)
        return false;
    // Not connected: the hello path sends it on the next connect.
    if (m_engine->connected()) {
        QVariantMap prefs;
        prefs[key] = now;
        m_engine->request("prefs.set", prefs);
    }
    return true;
}

void Settings::setAutoNet(const QString &net)
{
    if (storeData("autoNet", net))
        emit autoNetChanged();
}

void Settings::setAutoPhotos(bool on)
{
    if (storeData("autoPhotos", on))
        emit autoPhotosChanged();
}

void Settings::setAutoVideoMB(int mb)
{
    if (storeData("autoVideoMB", mb))
        emit autoVideoMBChanged();
}

void Settings::setAutoFileMB(int mb)
{
    if (storeData("autoFileMB", mb))
        emit autoFileMBChanged();
}

void Settings::setGalleryPrivate(bool on)
{
    if (storeData("galleryPrivate", on))
        emit galleryPrivateChanged();
}

void Settings::setGalleryGroups(bool on)
{
    if (storeData("galleryGroups", on))
        emit galleryGroupsChanged();
}

void Settings::setGalleryChannels(bool on)
{
    if (storeData("galleryChannels", on))
        emit galleryChannelsChanged();
}

void Settings::setKeepMB(int mb)
{
    if (storeData("keepMB", mb))
        emit keepMBChanged();
}

void Settings::setCacheLimit(int mb)
{
    mb = qBound(64, mb, 512);
    if (mb == cacheLimit())
        return;
    store("cacheLimit", mb);
    // Not connected: the hello path sends it on the next connect. The engine prunes, since it decodes.
    if (m_engine->connected()) {
        QVariantMap prefs;
        prefs["cacheMB"] = mb;
        m_engine->request("prefs.set", prefs);
    }
    emit cacheLimitChanged();
}

void Settings::setDeliveryMode(const QString &mode)
{
    if (mode == deliveryMode())
        return;
    store("deliveryMode", mode);
    // Not connected: the hello path sends it on the next connect. The engine keeps it in prefs.json and works
    // by it while the UI is closed.
    if (m_engine->connected()) {
        QVariantMap prefs;
        prefs["mode"] = mode;
        m_engine->request("prefs.set", prefs);
    }
    emit deliveryModeChanged();
}

void Settings::setTextSize(int size)
{
    size = qBound(0, size, 2);
    if (size == textSize())
        return;
    store("textSize", size);
    emit textSizeChanged();
}

void Settings::setVideoQuality(int quality)
{
    quality = qBound(0, quality, 3);
    if (quality == videoQuality())
        return;
    store("videoQuality", quality);
    emit videoQualityChanged(quality);
}

void Settings::setStickerPlay(int mode)
{
    mode = qBound(0, mode, 2);
    if (mode == stickerPlay())
        return;
    store("stickerPlay", mode);
    s_stickerPlay = mode;
    emit stickerPlayChanged();
}

void Settings::setStickerSize(int size)
{
    size = qBound(0, size, 2);
    if (size == stickerSize())
        return;
    store("stickerSize", size);
    s_stickerSize = size;
    emit stickerSizeChanged();
}

void Settings::setCompact(bool on)
{
    if (on == compact())
        return;
    store("compact", on);
    emit compactChanged();
}

void Settings::setRound(bool on)
{
    if (on == round())
        return;
    store("round", on);
    s_roundShape = on;
    emit roundChanged();
}

void Settings::setDarkGrey(bool on)
{
    if (on == darkGrey())
        return;
    store("darkGrey", on);
    emit darkGreyChanged();
}

void Settings::setCallHints(int count)
{
    count = qMax(0, count);
    if (count == callHints())
        return;
    store("callHints", count);
    emit callHintsChanged();
}

void Settings::setSyncContacts(bool on)
{
    if (on == syncContacts())
        return;
    store("syncContacts", on);
    emit syncContactsChanged();
}

// The round copies roundPhoto has made, by the picture's path, size and time: a list scrolling asks again and again. Emptied
// with the cache, which holds the files.
static QHash<QString, QString> s_round;
// roundPhotoSoon's: by the picture's path alone.
static QHash<QString, QString> s_roundSoon;

// The cache is data/cache/ except upload/, which holds an upload in progress. Everything else in
// it is made again when needed (a voice note's WAV on its next play, a photo's preview on its next
// showing). Collected first, so nothing is deleted while the iterator is reading the directory.
static QStringList cacheFiles()
{
    const QString root = QDir::homePath() + "/cache";
    QStringList files;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!path.startsWith(root + "/upload/"))
            files << path;
    }
    return files;
}

// On Storage's worker: files only, nothing of the UI's.
qint64 Settings::cacheBytes()
{
    qint64 bytes = 0;
    for (const QString &path : cacheFiles())
        bytes += QFileInfo(path).size();
    return bytes;
}

// The size freed; a file that can't be removed is skipped. Empty directories stay. On Storage's worker.
qint64 Settings::removeCache()
{
    qint64 freed = 0;
    for (const QString &path : cacheFiles()) {
        const qint64 size = QFileInfo(path).size();
        if (QFile::remove(path))
            freed += size;
    }
    return freed;
}

void Settings::coverStickers(bool covered)
{
    StickerPlayer::setCovered(covered);
}

void Settings::forgetRound()
{
    s_roundSoon.clear();
    s_round.clear(); // the round pictures' names: their files are about to go
}

// Round mode's pictures (the user's ask, 2026-11-19): the file is read scaled to `size` px on its short side at most (160 for a
// tile, 74 px on the Classic; 400 for a profile page's, 20 du, the user, 2026-11-19: 160 was blurred there), cut to its centre
// square and painted into a transparent image through an ellipse brush, which has anti-aliased edges where a clip path has
// none. Named by an md5 of the path, the size and the file's time, in data/cache/round/: a new picture is a new file (the
// profile's own is rewritten under one path), a size has its own, and one already made is not drawn again.
// ponytail: never pruned (about 15 KB a tile, 100 KB a profile picture); Clear cache empties it. Prune it with the other
// caches if it ever grows.
// The round copy's file for a picture: named by an md5 of the path, the size and the file's time (modified, in seconds).
static QString roundFilePath(const QString &path, int size, uint modified)
{
    const QString key = path + QChar('@') + QString::number(size) + QChar('@') + QString::number(modified);
    const QByteArray name = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex();
    return QDir::homePath() + "/cache/round/" + QString::fromLatin1(name) + ".png";
}

// The disc itself: the picture read scaled and painted into a transparent image, null when it can't be read. It touches
// only QImageReader, QImage and QPainter on a QImage, so a worker thread may call it (PreviewMaker does).
QImage roundImage(const QString &path, int size)
{
    QImageReader reader(path);
    QSize to = reader.size(); // from the header
    if (!to.isValid())
        return QImage();
    if (qMin(to.width(), to.height()) > size) {
        to.scale(size, size, Qt::KeepAspectRatioByExpanding);
        reader.setScaledSize(to);
    }
    const QImage image = reader.read();
    if (image.isNull())
        return QImage();
    const int side = qMin(image.width(), image.height());
    QImage disc(side, side, QImage::Format_ARGB32);
    disc.fill(0);
    QPainter painter(&disc);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side)));
    painter.drawEllipse(QRectF(0, 0, side, side));
    painter.end();
    return disc;
}

QString roundPhoto(const QString &path, int size)
{
    const QFileInfo file(path);
    if (!file.isFile())
        return QString();
    const QString key = path + QChar('@') + QString::number(size) + QChar('@') + QString::number(file.lastModified().toTime_t());
    const QString known = s_round.value(key);
    if (!known.isEmpty())
        return known;
    const QString out = roundFilePath(path, size, file.lastModified().toTime_t());
    if (!QFile::exists(out)) {
        const QImage disc = roundImage(path, size);
        if (disc.isNull())
            return QString();
        if (!QDir().mkpath(QDir::homePath() + "/cache/round/") || !disc.save(out, "PNG")) {
            QFile::remove(out);
            return QString();
        }
    }
    s_round.insert(key, out);
    return out;
}

// roundPhotoSoon's memory (the header says what it is for): picture path -> its round file, "" while it is being made.
// Emptied with the cache (forgetRound) and per picture when PreviewMaker has made it (roundForget).
QString roundPhotoSoon(const QString &path)
{
    const QHash<QString, QString>::const_iterator known = s_roundSoon.constFind(path);
    if (known != s_roundSoon.constEnd())
        return known.value();
    const QFileInfo file(path);
    if (!file.isFile())
        return QString(); // not remembered: the picture may arrive later
    const QString out = roundFilePath(path, 160, file.lastModified().toTime_t());
    if (QFile::exists(out)) {
        s_roundSoon.insert(path, out);
        return out;
    }
    PreviewMaker::instance()->want("round:" + path, PreviewMaker::RoundPicture, path, out);
    s_roundSoon.insert(path, QString());
    return QString();
}

void roundForget(const QString &path)
{
    s_roundSoon.remove(path);
}

QString Settings::roundPhoto(const QString &path)
{
    return ::roundPhoto(path);
}

QString Settings::roundPhoto(const QString &path, int size)
{
    return ::roundPhoto(path, size);
}
