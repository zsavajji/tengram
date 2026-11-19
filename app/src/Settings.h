#ifndef SETTINGS_H
#define SETTINGS_H

#include <bb/system/LocaleHandler>
#include <QObject>
#include <QSettings>
#include <QVariantMap>

class EngineLink;

// The user's settings (step 5.4), in settings.ini, which EngineLink reads with its own QSettings:
// each write is synced at once. QML takes them as `settings`.
class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int theme READ theme WRITE setTheme NOTIFY themeChanged)             // 0 Dark, 1 Bright
    Q_PROPERTY(bool enterSends READ enterSends WRITE setEnterSends NOTIFY enterSendsChanged)
    Q_PROPERTY(bool notifyPreview READ notifyPreview WRITE setNotifyPreview NOTIFY notifyPreviewChanged)
    Q_PROPERTY(bool startAtBoot READ startAtBoot WRITE setStartAtBoot NOTIFY startAtBootChanged)
    // Data and storage (docs/protocol.md): each is the engine's prefs.set field of the same name, sent as it changes and
    // at every connect. autoNet is "none", "wifi" or "all"; the MB values are sizes (0: never, for the auto-download
    // ones; no limit, for keepMB).
    Q_PROPERTY(QString autoNet READ autoNet WRITE setAutoNet NOTIFY autoNetChanged)
    Q_PROPERTY(bool autoPhotos READ autoPhotos WRITE setAutoPhotos NOTIFY autoPhotosChanged)
    Q_PROPERTY(int autoVideoMB READ autoVideoMB WRITE setAutoVideoMB NOTIFY autoVideoMBChanged)
    Q_PROPERTY(int autoFileMB READ autoFileMB WRITE setAutoFileMB NOTIFY autoFileMBChanged)
    Q_PROPERTY(bool galleryPrivate READ galleryPrivate WRITE setGalleryPrivate NOTIFY galleryPrivateChanged)
    Q_PROPERTY(bool galleryGroups READ galleryGroups WRITE setGalleryGroups NOTIFY galleryGroupsChanged)
    Q_PROPERTY(bool galleryChannels READ galleryChannels WRITE setGalleryChannels NOTIFY galleryChannelsChanged)
    Q_PROPERTY(int keepMB READ keepMB WRITE setKeepMB NOTIFY keepMBChanged)
    // Off for a phone whose proximity sensor misreads: voice notes stay on the speaker, the call screen never blanks.
    Q_PROPERTY(bool earSensor READ earSensor WRITE setEarSensor NOTIFY earSensorChanged)
    Q_PROPERTY(int cacheLimit READ cacheLimit WRITE setCacheLimit NOTIFY cacheLimitChanged)         // MB
    // "auto", "instant" or "batched" (every 5 minutes): the engine's prefs.set mode.
    Q_PROPERTY(QString deliveryMode READ deliveryMode WRITE setDeliveryMode NOTIFY deliveryModeChanged)
    Q_PROPERTY(int textSize READ textSize WRITE setTextSize NOTIFY textSizeChanged) // 0..2
    Q_PROPERTY(qreal textScale READ textScale NOTIFY textSizeChanged)
    // Video calls: 0 follows the network (resolution by steps), 1 is 1280x720, 2 is 640x480, 3 is 320x240 (CallVideo's quality).
    // The bit rate follows the network either way. Not sent to the engine: the UI's camera is what it sets.
    Q_PROPERTY(int videoQuality READ videoQuality WRITE setVideoQuality NOTIFY videoQualityChanged) // 0..3
    // Interface: Normal (false) has the chat rows' bigger pictures and padding, Compact the old rows (ChatRows.cpp).
    Q_PROPERTY(bool compact READ compact WRITE setCompact NOTIFY compactChanged)
    // How many group calls showed the people drawer's first-time hint (CallDrawer.qml shows it in the first 2).
    Q_PROPERTY(int callHints READ callHints WRITE setCallHints NOTIFY callHintsChanged)
    // Privacy and security > Sync contacts: the phone's address book goes to Telegram (contacts.book) only while this
    // is on. Off by default: uploading the address book is a setting the user turns on (ContactBook acts on it).
    Q_PROPERTY(bool syncContacts READ syncContacts WRITE setSyncContacts NOTIFY syncContactsChanged)

public:
    explicit Settings(EngineLink *engine, QObject *parent = 0);
    int theme() const;
    bool enterSends() const;
    bool notifyPreview() const;
    bool startAtBoot() const;
    QString autoNet() const;
    bool autoPhotos() const;
    int autoVideoMB() const;
    int autoFileMB() const;
    bool galleryPrivate() const;
    bool galleryGroups() const;
    bool galleryChannels() const;
    int keepMB() const;
    bool earSensor() const;
    int cacheLimit() const;
    QString deliveryMode() const;
    int textSize() const;
    int videoQuality() const;
    qreal textScale() const;
    bool compact() const;
    int callHints() const;
    bool syncContacts() const;
    void setTheme(int theme);
    void setEnterSends(bool on);
    void setNotifyPreview(bool on);
    void setStartAtBoot(bool on);
    void setAutoNet(const QString &net);
    void setAutoPhotos(bool on);
    void setAutoVideoMB(int mb);
    void setAutoFileMB(int mb);
    void setGalleryPrivate(bool on);
    void setGalleryGroups(bool on);
    void setGalleryChannels(bool on);
    void setKeepMB(int mb);
    void setEarSensor(bool on);
    void setCacheLimit(int mb);
    void setDeliveryMode(const QString &mode);
    void setTextSize(int size);
    void setVideoQuality(int quality);
    void setCompact(bool on);
    void setCallHints(int count);
    void setSyncContacts(bool on);
    // The eight Data and storage prefs as prefs.set takes them, read from file with their defaults and limits: what the
    // getters answer, and what EngineLink sends at every connect.
    static QVariantMap dataPrefs(const QSettings &file);
    Q_INVOKABLE QString cacheSize();
    Q_INVOKABLE QString clearCache();

signals:
    void themeChanged();
    void enterSendsChanged();
    void notifyPreviewChanged();
    void startAtBootChanged();
    void autoNetChanged();
    void autoPhotosChanged();
    void autoVideoMBChanged();
    void autoFileMBChanged();
    void galleryPrivateChanged();
    void galleryGroupsChanged();
    void galleryChannelsChanged();
    void keepMBChanged();
    void earSensorChanged();
    void cacheLimitChanged();
    void deliveryModeChanged();
    void textSizeChanged();
    void videoQualityChanged(int quality); // the new value, for CallVideo::setQuality (main.cpp connects them)
    void compactChanged();
    void callHintsChanged();
    void syncContactsChanged();

private slots:
    void bootTimerReplied();

private:
    void applyTheme();
    void applyBootTimer(bool on);
    void store(const char *key, const QVariant &value);
    // Stores one of dataPrefs' values and sends it to the engine; true when what dataPrefs reads back has changed.
    bool storeData(const char *key, const QVariant &value);
    QSettings m_file;
    EngineLink *m_engine;
    bb::system::LocaleHandler m_region; // for the size text, as the chat models have it
};

#endif
