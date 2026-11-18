#ifndef SETTINGS_H
#define SETTINGS_H

#include <bb/system/LocaleHandler>
#include <QObject>
#include <QSettings>

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
    Q_PROPERTY(bool mediaOnMobile READ mediaOnMobile WRITE setMediaOnMobile NOTIFY mediaOnMobileChanged)
    // Off for a phone whose proximity sensor misreads: voice notes stay on the speaker, the call screen never blanks.
    Q_PROPERTY(bool earSensor READ earSensor WRITE setEarSensor NOTIFY earSensorChanged)
    Q_PROPERTY(int cacheLimit READ cacheLimit WRITE setCacheLimit NOTIFY cacheLimitChanged)         // MB
    // "auto", "instant" or "batched" (every 5 minutes): the engine's prefs.set mode.
    Q_PROPERTY(QString deliveryMode READ deliveryMode WRITE setDeliveryMode NOTIFY deliveryModeChanged)
    Q_PROPERTY(int textSize READ textSize WRITE setTextSize NOTIFY textSizeChanged) // 0..2
    Q_PROPERTY(qreal textScale READ textScale NOTIFY textSizeChanged)
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
    bool mediaOnMobile() const;
    bool earSensor() const;
    int cacheLimit() const;
    QString deliveryMode() const;
    int textSize() const;
    qreal textScale() const;
    bool compact() const;
    int callHints() const;
    bool syncContacts() const;
    void setTheme(int theme);
    void setEnterSends(bool on);
    void setNotifyPreview(bool on);
    void setStartAtBoot(bool on);
    void setMediaOnMobile(bool on);
    void setEarSensor(bool on);
    void setCacheLimit(int mb);
    void setDeliveryMode(const QString &mode);
    void setTextSize(int size);
    void setCompact(bool on);
    void setCallHints(int count);
    void setSyncContacts(bool on);
    Q_INVOKABLE QString cacheSize();
    Q_INVOKABLE QString clearCache();

signals:
    void themeChanged();
    void enterSendsChanged();
    void notifyPreviewChanged();
    void startAtBootChanged();
    void mediaOnMobileChanged();
    void earSensorChanged();
    void cacheLimitChanged();
    void deliveryModeChanged();
    void textSizeChanged();
    void compactChanged();
    void callHintsChanged();
    void syncContactsChanged();

private slots:
    void bootTimerReplied();

private:
    void applyTheme();
    void applyBootTimer(bool on);
    void store(const char *key, const QVariant &value);
    QSettings m_file;
    EngineLink *m_engine;
    bb::system::LocaleHandler m_region; // for the size text, as the chat models have it
};

#endif
