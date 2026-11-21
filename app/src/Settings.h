#ifndef SETTINGS_H
#define SETTINGS_H

#include <bb/system/LocaleHandler>
#include <QImage>
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
    // Animated stickers (docs/stickers-plan.md, G2): 0 Loop, as stock Telegram, 1 Play once (a tap plays it again), 2 Off, shown
    // still. MediaBubble reads it through stickerPlayMode(); StickerPlayer stops when it turns to Off.
    Q_PROPERTY(int stickerPlay READ stickerPlay WRITE setStickerPlay NOTIFY stickerPlayChanged) // 0..2
    // Settings > Chats > Sticker size (the user's ask, 2026-11-20): 0 Small (x0.7), 1 Normal (30 du, as so far), 2 Big (x1.2).
    Q_PROPERTY(int stickerSize READ stickerSize WRITE setStickerSize NOTIFY stickerSizeChanged) // 0..2
    // Interface: Normal (false) has the chat rows' bigger pictures and padding, Compact the old rows (ChatRows.cpp).
    Q_PROPERTY(bool compact READ compact WRITE setCompact NOTIFY compactChanged)
    // Shape (the user's ask, 2026-11-19): Square (false) is the look so far; Round has the pictures in the lists as discs, the
    // buttons as discs and pills, and the message cards with 20 px corners (roundPhoto and the images of scripts/gen-round.py).
    Q_PROPERTY(bool round READ round WRITE setRound NOTIFY roundChanged)
    // The Dark theme's page background (the user's ask, 2026-11-19): pure black (false, the look so far) or #181818, like Android's.
    // main.qml's pageColor reads it; the Bright theme has no such choice.
    Q_PROPERTY(bool darkGrey READ darkGrey WRITE setDarkGrey NOTIFY darkGreyChanged)
    // How many group calls showed the people drawer's first-time hint (CallDrawer.qml shows it in the first 2).
    Q_PROPERTY(int callHints READ callHints WRITE setCallHints NOTIFY callHintsChanged)
    // Privacy and security > Sync contacts: the phone's address book goes to Telegram (contacts.book) only while this
    // is on. Off by default: uploading the address book is a setting the user turns on (ContactBook acts on it).
    Q_PROPERTY(bool syncContacts READ syncContacts WRITE setSyncContacts NOTIFY syncContactsChanged)
    // Settings > Translation: a Google Cloud Translation API key, "" for none (Telegram translates then). The engine's pref.
    Q_PROPERTY(QString googleKey READ googleKey WRITE setGoogleKey NOTIFY googleKeyChanged)

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
    int stickerPlay() const;
    int stickerSize() const;
    qreal textScale() const;
    bool compact() const;
    bool round() const;
    bool darkGrey() const;
    int callHints() const;
    bool syncContacts() const;
    QString googleKey() const;
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
    void setStickerPlay(int mode);
    void setCompact(bool on);
    void setRound(bool on);
    void setDarkGrey(bool on);
    // The Shape as the code that has no QML list to read it from has it (Waveform): kept in step by the constructor's load and
    // setRound.
    static bool roundShape() { return s_roundShape; }
    // The same for the sticker setting, for MediaBubble and StickerPlayer: kept in step by the constructor's load and setStickerPlay.
    static int stickerPlayMode() { return s_stickerPlay; }
    // The size's factor, for MediaBubble: kept in step the same way, by the constructor and setStickerSize.
    static qreal stickerScale();
    void setStickerSize(int size);
    void setCallHints(int count);
    void setSyncContacts(bool on);
    void setGoogleKey(const QString &key);
    // The eight Data and storage prefs as prefs.set takes them, read from file with their defaults and limits: what the
    // getters answer, and what EngineLink sends at every connect.
    static QVariantMap dataPrefs(const QSettings &file);
    // The size of data/cache/ and its clearing: both walk thousands of files (the previews, the round pictures), which took the
    // Data and storage page 3 s to open on the UI thread (the user, 0.1.0.81). Storage runs them on a worker: they touch files
    // only. forgetRound() is the clearing's part on the UI thread, before it starts.
    static qint64 cacheBytes();
    static qint64 removeCache();
    static void forgetRound();
    // Called by main.qml's nav when the top page changes: true while no chat page is on top, and the animated stickers pause.
    // Here because Settings is the QML object that already has the sticker settings.
    Q_INVOKABLE void coverStickers(bool covered);
    // Plays images/welcome.tgs (scripts/gen-welcome.py: the tangram square springing into the icon) once in WelcomePage's
    // logo, an ImageView, at px; icon-288.png, its last frame, is the still. Again from the start when called again: a tap.
    Q_INVOKABLE void playLogo(QObject *view, int px);
    // The round copy of a picture's file for Round mode (the free function below), "" when it can't be made: QML falls back to
    // the square picture. Two overloads, not a default argument: QML calls either by its argument count (160 px, or `size`).
    Q_INVOKABLE QString roundPhoto(const QString &path);
    Q_INVOKABLE QString roundPhoto(const QString &path, int size);

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
    void stickerPlayChanged();
    void stickerSizeChanged();
    void compactChanged();
    void roundChanged();
    void darkGreyChanged();
    void callHintsChanged();
    void syncContactsChanged();
    void googleKeyChanged();

private slots:
    void bootTimerReplied();

private:
    void applyTheme();
    void applyBootTimer(bool on);
    void store(const char *key, const QVariant &value);
    // Stores one of dataPrefs' values and sends it to the engine; true when what dataPrefs reads back has changed.
    bool storeData(const char *key, const QVariant &value);
    static bool s_roundShape;
    static int s_stickerPlay;
    static int s_stickerSize;
    QSettings m_file;
    EngineLink *m_engine;
    bb::system::LocaleHandler m_region; // for the size text, as the chat models have it
};

// A copy of the picture at `path` cut to a disc, for Round mode: Cascades has no clipping, so the corners are transparent in
// the file. "" when the picture can't be read or the copy written; the callers then show the picture as it is. `size` is the
// px the copy's side is at most: 160 for a tile, more for the profile pages' big pictures.
QString roundPhoto(const QString &path, int size = 160);

// The disc image roundPhoto saves: the picture at `path` read at `size` px at most, cut to its centre square and painted
// through an ellipse brush. Null when the picture can't be read. It touches only QImageReader, QImage and QPainter on a
// QImage, so a worker thread may call it (PreviewMaker's RoundPicture job does).
QImage roundImage(const QString &path, int size);

// For the chat's cards: the round copy of a sender's picture (160 px) if it is made, else "" and it is asked of
// PreviewMaker, which makes it on a worker and names "round:<path>" in ready(); roundForget(path) then lets the next
// call find it. No file is touched for a picture already found or being made: fillTile runs at every fill of a card
// (606 of 828 ms of one open's fills, the Classic, 1.0.0.99). A sender's picture has its own file name per picture,
// so its path alone is the key here (roundPhoto's key has the file's time, for the profile's own, rewritten in place).
QString roundPhotoSoon(const QString &path);
// The picture at path is made, or its making should be looked at again: the next roundPhotoSoon(path) checks the file.
void roundForget(const QString &path);

#endif
