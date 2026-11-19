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
#include "sizes.h"

bool Settings::s_roundShape = false;

Settings::Settings(EngineLink *engine, QObject *parent)
    : QObject(parent),
      m_file(QDir::homePath() + "/settings.ini", QSettings::IniFormat),
      m_engine(engine),
      m_region(bb::system::LocaleType::Region)
{
    s_roundShape = round();
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
bool Settings::compact() const { return m_file.value("compact", false).toBool(); }
bool Settings::round() const { return m_file.value("round", false).toBool(); }
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

// The round copies roundPhoto has made, by the picture's path: a list scrolling asks again and again. Emptied with the
// cache, which holds the files.
static QHash<QString, QString> s_round;

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

// An empty cache reads "0 KB", not the empty text sizeText gives for an unknown size.
static QString cacheText(qint64 bytes, const QLocale &locale)
{
    return bytes > 0 ? sizeText(bytes, locale) : QString("0 KB");
}

QString Settings::cacheSize()
{
    qint64 bytes = 0;
    foreach (const QString &path, cacheFiles())
        bytes += QFileInfo(path).size();
    return cacheText(bytes, m_region.locale());
}

// Returns the size freed; a file that can't be removed is skipped. Empty directories stay.
QString Settings::clearCache()
{
    s_round.clear();
    qint64 freed = 0;
    foreach (const QString &path, cacheFiles()) {
        const qint64 size = QFileInfo(path).size();
        if (QFile::remove(path))
            freed += size;
    }
    return cacheText(freed, m_region.locale());
}

// Round mode's pictures (the user's ask, 2026-11-19): the file is read scaled to 160 px on its short side at most (a tile is
// 74 px on the Classic), cut to its centre square and painted into a transparent image through an ellipse brush, which has
// anti-aliased edges where a clip path has none. Named by an md5 of the path and the file's time, in data/cache/round/: a
// new picture is a new file, and one already made is not drawn again.
// ponytail: never pruned (about 15 KB a picture); Clear cache empties it. Prune it with the other caches if it ever grows.
QString roundPhoto(const QString &path)
{
    const QString known = s_round.value(path);
    if (!known.isEmpty())
        return known;
    const QFileInfo file(path);
    if (!file.isFile())
        return QString();
    const QString dir = QDir::homePath() + "/cache/round/";
    const QByteArray name = QCryptographicHash::hash((path + QString::number(file.lastModified().toTime_t())).toUtf8(),
                                                     QCryptographicHash::Md5).toHex();
    const QString out = dir + QString::fromLatin1(name) + ".png";
    if (!QFile::exists(out)) {
        QImageReader reader(path);
        QSize to = reader.size(); // from the header
        if (!to.isValid())
            return QString();
        if (qMin(to.width(), to.height()) > 160) {
            to.scale(160, 160, Qt::KeepAspectRatioByExpanding);
            reader.setScaledSize(to);
        }
        const QImage image = reader.read();
        if (image.isNull())
            return QString();
        const int side = qMin(image.width(), image.height());
        QImage disc(side, side, QImage::Format_ARGB32);
        disc.fill(0);
        QPainter painter(&disc);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side)));
        painter.drawEllipse(QRectF(0, 0, side, side));
        painter.end();
        if (!QDir().mkpath(dir) || !disc.save(out, "PNG")) {
            QFile::remove(out);
            return QString();
        }
    }
    s_round.insert(path, out);
    return out;
}

QString Settings::roundPhoto(const QString &path)
{
    return ::roundPhoto(path);
}
