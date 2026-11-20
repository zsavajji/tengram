#ifndef MEDIAOPENER_H
#define MEDIAOPENER_H

#include <bb/system/CardDoneMessage>
#include <bb/system/InvokeManager>
#include <bb/system/LocaleHandler>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class EngineLink;

// Fetches a received file and opens it (step 6.1), made by QML in the chat page's attachedObjects:
// the request's owner dies with the page, so no connection to the engine outlives it.
class MediaOpener : public QObject
{
    Q_OBJECT
    // The EngineLink, set as ContactInfo's link is.
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    // The message being fetched, "" when none: its bubble shows a spinner.
    Q_PROPERTY(QString loadingId READ loadingId NOTIFY loadingIdChanged)
    // How much of that message's file is here, 0 to 1: 0 until bytes arrive. And "1.2 MB / 23 MB", "" until then.
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    // The messages of ours being sent whose file is going up: message id -> {progress: 0 to 1, text: "1.2 MB / 23 MB"}.
    // An entry goes when its last bytes are up. Keyed by the message id alone, whatever the chat.
    // ponytail: ids from two chats could collide (a temporary id is not reused across chats in practice); key by chat
    // too if it ever matters.
    Q_PROPERTY(QVariantMap uploads READ uploads NOTIFY uploadsChanged)
public:
    explicit MediaOpener(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    QString loadingId() const;
    qreal progress() const;
    QString progressText() const;
    QVariantMap uploads() const;

    // Asks the engine for the file of message id (media.get); ready() follows. A failure shows in
    // the engine's failure. A newer get replaces an older one, whose reply is dropped. With save the engine also copies
    // the file to the phone's Downloads (the menu's Download) and saved() follows, with the copy's path, not ready().
    Q_INVOKABLE void get(const QString &chat, const QString &id, const QString &kind, bool save = false);
    // The last get() was a save: the page reads it when that get fails, to word the toast.
    Q_INVOKABLE bool saving() const;
    // Stops the fetch of loadingId (media.cancel); its get() ends with a "cancelled" failure that nobody shows.
    Q_INVOKABLE void cancel();
    // Hands the file to the system (bb.action.OPEN); false when the system gave no reply. Whether
    // an app opens it is not known.
    Q_INVOKABLE bool openFile(const QString &path);
    // Opens the system's picture editor card (sys.pictureeditor.cardeditor, bb.action.EDIT) on a .jpg, .png, .gif or
    // .bmp, its video editor card (sys.video_editor.card: trim, crop) on a .mp4, .3gp or .mov; pictureEdited() follows
    // when it hands an edited file back, openFailed() when the system gave no reply.
    Q_INVOKABLE void editPicture(const QString &path);
    // Opens the Contacts app's import screen on a shared contact's vCard text (step 6.5d). A failure
    // is logged, and shows as openFailed().
    Q_INVOKABLE void importContact(const QString &vcard);
    // Copies a photo into Pictures (shared/photos/Tengram/); the copy's path, "" on failure.
    Q_INVOKABLE QString saveToPictures(const QString &path);
    // What media.send is given with a photo (step 6.3), since the engine decodes no picture: for a
    // .jpg or .jpeg Qt can read, {thumb: base64 of a JPEG of at most 96 px and 16 KB, width,
    // height} (the original's size); thumb is missing when no quality made it small enough. Empty
    // for any other file or on failure.
    Q_INVOKABLE QVariantMap photoInfo(const QString &path);
    // A picture as a JPEG the engine can send as a photo (it decodes nothing): a .jpg or .jpeg is itself; a .png
    // is read by Qt, put on white where it is transparent, its long side held to 2048 px, and saved in
    // data/cache/convert/ at quality 90: that path. "" for anything else or on failure.
    Q_INVOKABLE QString asJpeg(const QString &path);
    // A picture to look at on the new-story screen: the file scaled down to fit 720 x 720 (Qt reads a JPEG straight
    // to the smaller size), saved in data/cache/previews/ as compose-<hash>.jpg at quality 90: that path. "" when Qt
    // can't read it. photoInfo's 96 px thumbnail is for the message, not for looking at.
    Q_INVOKABLE QString composePreview(const QString &path);
    // The thumbnail a video goes out with, as base64 of a small JPEG: a play mark on grey, so the phones that get
    // a video see one and not a grey box. "" on failure.
    Q_INVOKABLE QString videoPoster();
    // An MP4's length in ms, read from the file itself (Mp4Length.h), for the video viewer when the player reports
    // none; 0 when it can't be read.
    Q_INVOKABLE int videoLength(const QString &path);
    // A fragmented MP4 (Mp4Length.h), in which the player can't seek: the video viewer shows no timeline for it, and a
    // button that opens it in the system's player.
    Q_INVOKABLE bool videoFragmented(const QString &path);
    // What the staged bar shows for a file picked to send (step 6.4): {name, sizeText, picture (a
    // photo or a video: its caption rides in the message), image (a .jpg's thumbnail, as an Image;
    // missing for the rest)}. Empty when the file can't be read.
    Q_INVOKABLE QVariantMap fileInfo(const QString &path);

signals:
    void loadingIdChanged();
    void progressChanged();
    void uploadsChanged();
    // The file of message id is at path; kind is what get() was given.
    void ready(const QString &id, const QString &kind, const QString &path);
    // The save asked of get() is done: the copy in Downloads is at path.
    void saved(const QString &path);
    // The system found no app to open a file, or the app refused it.
    void openFailed();
    // The editor card closed with an edited file, at path.
    void pictureEdited(const QString &path);

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onFailure();
    void onProgress(const QString &chat, const QString &id, qint64 done, qint64 size, bool up);
    void onInvoked();
    void onCardDone(const bb::system::CardDoneMessage &done);

private:
    void setLoadingId(const QString &id);

    QPointer<EngineLink> m_engine;
    bb::system::InvokeManager m_invoker;
    QString m_loadingId;
    QString m_chat; // of m_loadingId
    qreal m_progress;
    QString m_progressText;
    QVariantMap m_uploads; // the uploads' progress, by message id
    QString m_kind;
    QString m_reqId; // the media.get request we wait for
    bool m_saving; // that request is a save: its reply is saved(), not ready()
    bool m_editing; // the picture editor card is open: its done is ours, not an OPEN's or the contact import's
    bb::system::LocaleHandler m_region; // for the size text, as the chat models have it
};

#endif
