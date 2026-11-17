#ifndef MEDIAOPENER_H
#define MEDIAOPENER_H

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
public:
    explicit MediaOpener(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    QString loadingId() const;

    // Asks the engine for the file of message id (media.get); ready() follows. A failure shows in
    // the engine's failure. A newer get replaces an older one, whose reply is dropped.
    Q_INVOKABLE void get(const QString &chat, const QString &id, const QString &kind);
    // Hands the file to the system (bb.action.OPEN); false when the system gave no reply. Whether
    // an app opens it is not known.
    Q_INVOKABLE bool openFile(const QString &path);
    // Opens the Contacts app's import screen on a shared contact's vCard text (step 6.5d). A failure
    // is logged, and shows as openFailed().
    Q_INVOKABLE void importContact(const QString &vcard);
    // Copies a photo into Pictures (shared/photos/HyperBerry/); the copy's path, "" on failure.
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
    // What the staged bar shows for a file picked to send (step 6.4): {name, sizeText, picture (a
    // photo or a video: its caption rides in the message), image (a .jpg's thumbnail, as an Image;
    // missing for the rest)}. Empty when the file can't be read.
    Q_INVOKABLE QVariantMap fileInfo(const QString &path);

signals:
    void loadingIdChanged();
    // The file of message id is at path; kind is what get() was given.
    void ready(const QString &id, const QString &kind, const QString &path);
    // The system found no app to open a file, or the app refused it.
    void openFailed();

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onFailure();
    void onInvoked();

private:
    void setLoadingId(const QString &id);

    QPointer<EngineLink> m_engine;
    bb::system::InvokeManager m_invoker;
    QString m_loadingId;
    QString m_kind;
    QString m_reqId; // the media.get request we wait for
    bb::system::LocaleHandler m_region; // for the size text, as the chat models have it
};

#endif
