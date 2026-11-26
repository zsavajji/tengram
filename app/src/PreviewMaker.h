#ifndef PREVIEWMAKER_H
#define PREVIEWMAKER_H

#include <QList>
#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

// Makes the picture files a chat's rows show, off the UI thread: a downloaded photo's 640 px preview (MediaData) and a
// sticker's 180 px PNG (Sticker) and, for the chat's cards, the round copy of a sender's picture
// (roundPhotoSoon, a 160 px PNG; ready() names it "round:<path>"), each ~290 ms and 20 to 60 ms on the Q10, which froze the UI for 5.9 s when a chat of 21
// photos opened. One for the app, a child of the application, used from the UI thread only. A row whose file isn't there
// shows what it has (the inline thumbnail) and asks want(); ready() then says which rows to read again, and MessageModel
// does (the file is there by then).
class PreviewMaker : public QObject
{
    Q_OBJECT
public:
    enum Job { PhotoPreview, StickerPicture, RoundPicture };

    // Made on the first call.
    static PreviewMaker *instance();

    // Queues the making of the file target from source, for the row id: once per target (asking again for one queued, being
    // made, or that failed does nothing; a failed one is not tried again until the app restarts, so a file that doesn't
    // decode doesn't loop). The jobs run in the order asked, two at a time.
    void want(const QString &id, Job job, const QString &source, const QString &target);
    // Whether target could not be made (until the app restarts): its row shows what it has for good, not a wait.
    bool failed(const QString &target) const { return m_failed.contains(target); }

signals:
    // The ids of the rows whose file was made in the last 100 ms, so a chat full of photos reads its rows a few times and
    // not once a photo.
    void ready(const QStringList &ids);

private slots:
    void onMade();
    void flush();

private:
    struct Want
    {
        QString id;
        int job;
        QString source;
        QString target;
    };
    PreviewMaker();
    void pump();

    QList<Want> m_queue;
    // The targets queued, being made or failed, with the rows waiting on each (a sticker's picture is one file for every
    // row of that sticker): one leaves it when its file is made, so a file the engine wipes from the cache is made again,
    // and a failed one never does.
    QHash<QString, QSet<QString> > m_known;
    QSet<QString> m_failed; // the targets that could not be made
    int m_running;
    QSet<QString> m_done; // the ids made, or that failed, since the last ready
    QTimer m_flush;
};

#endif
