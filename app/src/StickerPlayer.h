#ifndef STICKERPLAYER_H
#define STICKERPLAYER_H

#include <bb/cascades/Image>
#include <bb/cascades/ImageView>
#include <QAtomicInt>
#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

class QThread;
class StickerWorker;

// Plays a TGS (Lottie) sticker in an ImageView (docs/stickers-plan.md, G2), one at a time. rlottie draws each frame on a
// thread of its own (G1: 4 to 17 ms a frame at 180 px, which would stall scrolling on the UI thread); this side only turns
// the finished bytes into an ImageData and sets it on the view. While a view plays, the player owns its image; stop() and
// release() give the still picture back. It pauses while no chat page is on top (setCovered) and goes on when one is.
// ponytail: one sticker plays at a time (the newest shown or tapped); several at once if a dual-core budget allows, measured.
class StickerPlayer : public QObject
{
    Q_OBJECT
public:
    // Made on the first call, a child of the application.
    static StickerPlayer *instance();
    ~StickerPlayer();

    // Plays the TGS at path in view from its first frame, looping or once, and stops what played before (its view gets its
    // still back). On the view that already plays it starts over: a tap. still is what the view shows when it stops.
    void play(bb::cascades::ImageView *view, const bb::cascades::Image &still, const QString &path, bool loop);
    // Stops when view is the one playing; does nothing otherwise.
    void release(bb::cascades::ImageView *view);
    bool playing(bb::cascades::ImageView *view) const;
    // True while a page other than a chat covers the chats: what plays stops drawing (its view shows the still) and starts
    // again from its first frame when it turns false. Called through Settings::coverStickers.
    static void setCovered(bool covered);

public slots:
    // Stops whatever plays; its view gets its still picture back. Also when the app leaves the foreground.
    void stop();

private slots:
    void onFrame(int token, const QByteArray &bytes);
    void onFinished(int token);

private:
    explicit StickerPlayer(QObject *parent);
    // Starts m_path in m_view: a new token, then the worker's start.
    void begin();

    QThread *m_thread;
    StickerWorker *m_worker;
    QPointer<bb::cascades::ImageView> m_view;
    bb::cascades::Image m_still;
    int m_token;        // what the worker's frames are tagged with: one that isn't the current is stale
    QString m_path;     // what plays, kept so it can start again after a pause
    bool m_loop;
    QAtomicInt m_latest; // the newest token given out, read by the worker to skip a start that was replaced
    static bool s_covered; // a page other than a chat is on top
    QAtomicInt m_inFlight; // set by the worker with each frame, cleared when this side has taken it (onFrame)
};

#endif
