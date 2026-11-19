#ifndef CALLVIDEO_H
#define CALLVIDEO_H

#include "CameraEncoder.h"
#include "VideoDecoder.h"
#include "VideoWindow.h"

#include <QAtomicInt>
#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>

class EngineLink;

// The video of the live call (docs/protocol.md, "Video link"): the camera and the screen belong
// to the UI process, the engine keeps the call. start() opens a third loopback connection, says
// hello with the call's id, then two threads work: one reads the peer's H.264 access units from the
// socket into the hardware decoder and its window (VideoWindow, turned upright by the orientation
// each record carries); the other opens the camera's hardware encoder while `sending` is on and
// the link is up, and the encoder's callback writes our access units to the socket. A call without
// the link has no picture and keeps going. HyperBerry's group calls had up to 4 tiles, each a decoder
// and a window; Telegram's calls here are 1:1 and Tiles is 1, but the structure stays (a record's tile
// is the flags' high byte, and a record for another tile is dropped).
class CallVideo : public QObject, private CameraEncoder::Sink
{
    Q_OBJECT
    // True from the engine's hello reply until the link ends.
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // Our camera is open and its pictures go out (QML sets it from the call event's videoOurs).
    // False closes the camera.
    Q_PROPERTY(bool sending READ sending WRITE setSending NOTIFY sendingChanged)
    // The front camera; false is the rear. Changing it while sending reopens the camera.
    Q_PROPERTY(bool front READ front WRITE setFront NOTIFY frontChanged)
    // The resolution of our camera: 0 follows the network (Auto: steps of 1280x720, 640x480 and 320x240 by WebRTC's target
    // bit rate), 1 is 1280x720, 2 is 640x480 and 3 is 320x240 always. The bit rate follows the target either way, capped
    // by the step. Settings' videoQuality; read by the camera thread, so a change comes at its next look.
    Q_PROPERTY(int quality READ quality WRITE setQuality NOTIFY qualityChanged)
    // Our camera runs 16:9 (the 1280x720 step), not 4:3: the shape of our preview, whose window is the camera's own.
    Q_PROPERTY(bool wide READ wide NOTIFY wideChanged)
    // The peer's picture as it is shown, after turning: 0 x 0 until the first frame.
    Q_PROPERTY(int pictureWidth READ pictureWidth NOTIFY pictureChanged)
    Q_PROPERTY(int pictureHeight READ pictureHeight NOTIFY pictureChanged)
public:
    explicit CallVideo(EngineLink *engine, QObject *parent = 0);
    ~CallVideo();

    bool running() const { return m_running; }
    bool sending() const { return m_sending != 0; }
    bool wide() const { return m_wide; }
    bool front() const { return m_front != 0; }
    int quality() const { return m_quality; }
    int pictureWidth() const { return m_picW; }
    int pictureHeight() const { return m_picH; }
    void setSending(bool on);
    void setFront(bool on);

    // Replaces any earlier video. remoteWindowIds (one per tile; the first is used) and viewfinderWindowId are
    // ids of ForeignWindowControls that exist: the peer's picture goes to the first, our
    // camera's own viewfinder is adopted by the other. Returns at once: the link, the decoders and
    // the camera open on their threads.
    Q_INVOKABLE void start(const QString &callId, const QStringList &remoteWindowIds, const QString &viewfinderWindowId);
    // Joins both threads (the camera and the decoders closed; a camera that is still opening makes
    // it a few seconds) and frees the windows. Safe to call twice, and when nothing runs.
    Q_INVOKABLE void stop();
    // The size of the peer's picture control, for the crop (VideoWindow::setViewSize).
    Q_INVOKABLE void setViewSize(int w, int h) { m_tile[0].window.setViewSize(w, h); }

signals:
    void runningChanged();
    void sendingChanged();
    void frontChanged();
    void qualityChanged();
    void wideChanged();
    void pictureChanged();

public slots:
    void setQuality(int quality);

private slots:
    void setRunning(bool on);
    void setWide(bool on);
    void tick();

private:
    // One QThread type for both loops; run() calls back into the owner.
    class Worker : public QThread
    {
    public:
        Worker(CallVideo *owner, bool camera) : m_owner(owner), m_camera(camera) {}
    protected:
        void run();
    private:
        CallVideo *m_owner;
        bool m_camera;
    };
    friend class Worker;

    static const int Tiles = 1;

    // One tile: a window and the decoder that draws into it.
    struct Tile
    {
        enum State { Idle, Decoding, Dropped };
        Tile() : decoder(&window), hasWindow(false), state(Idle) {}
        VideoWindow window;
        VideoDecoder decoder;
        bool hasWindow;        // set by start(), before the reader begins
        State state;           // the reader thread's: Dropped is a decoder that would not open
        QAtomicInt refused;    // pictures the decoder had no input buffer for
    };

    void accessUnit(const QByteArray &annexB, qint64 tsUs, bool keyframe, int orientation);
    void cameraSize(int width, int height);
    void sendRecord(int type, int orientation, int flags, qint64 tsUs, const QByteArray &payload);
    void readLoop();
    void cameraLoop();
    bool openLink();
    void closeLink();
    bool openTile(int tile, bool midStream);
    void summarize();

    EngineLink *m_engine;
    Tile m_tile[Tiles];
    CameraEncoder m_encoder;
    Worker m_reader;
    Worker m_camera;
    QTimer m_timer;
    QString m_callId;
    QByteArray m_group, m_viewfinderId;
    int m_fd;                  // the video link; -1 when none
    QMutex m_fdLock;           // so stop()'s shutdown never meets a closed, reused descriptor
    QMutex m_sendLock;         // one send at a time; closeLink() waits for it before it closes the fd
    QAtomicInt m_linked;       // the link is up; only set and cleared with m_sendLock held
    QAtomicInt m_stopping;
    QAtomicInt m_sending, m_front;
    QAtomicInt m_quality;          // 0 Auto, 1 to 3 a fixed size
    QAtomicInt m_targetBps;        // WebRTC's target for our video, from the engine's type-3 record; 0 until it comes
    QAtomicInt m_step, m_cameraBps; // the camera thread's: the step it runs at and the bit rate it set, for the summary
    QAtomicInt m_keyframeWanted;   // the peer asked; the camera thread passes it on
    QAtomicInt m_recordsIn;
    bool m_running;
    bool m_wide;
    int m_picW, m_picH;
    int m_ticks;
};

#endif
