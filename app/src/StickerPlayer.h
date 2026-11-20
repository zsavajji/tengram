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
// release() give the still picture back.
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

public slots:
    // Stops whatever plays; its view gets its still picture back. Also when the app leaves the foreground.
    void stop();

private slots:
    void onFrame(int token, const QByteArray &bytes);
    void onFinished(int token);

private:
    explicit StickerPlayer(QObject *parent);

    QThread *m_thread;
    StickerWorker *m_worker;
    QPointer<bb::cascades::ImageView> m_view;
    bb::cascades::Image m_still;
    int m_token;        // what the worker's frames are tagged with: one that isn't the current is stale
    QAtomicInt m_inFlight; // set by the worker with each frame, cleared when this side has taken it (onFrame)
};

#endif
