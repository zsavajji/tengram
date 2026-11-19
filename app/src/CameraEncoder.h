#ifndef CAMERAENCODER_H
#define CAMERAENCODER_H

#include <QByteArray>
#include <QMutex>
#include <camera/camera_api.h>

// The Q10's camera as a hardware H.264 encoder (docs/protocol.md, "Video link"): the BB10 camera
// API opens a unit, shows its viewfinder in a window of ours and hands back compressed frames.
// CAMERA_VIDEOCODEC_H264 is Annex-B (the SDK header says so; AVC1 would be length-prefixed). The
// access units reach the Sink with SPS and PPS in front of every keyframe, whether or not the
// camera sends them that way. The sending half of a call (CallVideo).
class CameraEncoder
{
public:
    class Sink
    {
    public:
        virtual ~Sink() {}
        // On the camera library's thread, one access unit at a time, in order. Keep it short.
        // tsUs counts from the first frame. orientation: the quarter turns clockwise that make this
        // camera's picture upright for whoever looks at it (0 to 3), the same for every unit of a run.
        virtual void accessUnit(const QByteArray &annexB, qint64 tsUs, bool keyframe, int orientation) = 0;
        // From start(), on the thread that called it, after the camera is set and before camera_start_encode: no unit
        // of this run has come yet and none can come before this returns. width x height is the size the camera says it
        // encodes (what was asked, if it won't say), that of the units that follow.
        virtual void cameraSize(int width, int height) = 0;
    };

    struct Stats
    {
        int frames, keyframes, dropped;   // dropped: the camera's own FRAME_DROPPED statuses
        double bytes;
        double maxFrameBytes;
        double wallMs;                    // since start() or resetStats()
        double intervalMeanMs, intervalMaxMs;   // between two callbacks
        double ageMeanMs, ageMaxMs;       // the camera's timestamp to the callback
        int width, height;                // what the camera reports it encodes
    };

    explicit CameraEncoder(Sink *sink);
    ~CameraEncoder();

    // Blocks (the camera can take seconds to open): never the UI thread. The viewfinder window
    // is made by the camera library in `group` with id `vfId`, for a ForeignWindowControl with that
    // id to adopt. False after logging why, with the camera closed again.
    bool start(bool front, int width, int height, int fps, int bitrate, int keyframeSeconds,
               const QByteArray &group, const QByteArray &vfId);
    // Changes the bit rate while encoding: whether the camera took it (logged each time, with its answer). The SDK
    // sets the encoder's parameters before the encoding starts; whether this phone takes a change in the middle of
    // it is what a call tells us. Like requestKeyframe, only from the thread that starts and stops.
    bool setBitrate(int bitsPerSecond);
    // Safe to call twice, and when nothing runs. Returns once no callback is running.
    void stop();
    bool isRunning() const { return m_handle != CAMERA_HANDLE_INVALID; }
    // The next unit is an IDR, for a peer that lost its place. Only while running, and not while
    // another thread may be in start() or stop(): the handle is not guarded.
    void requestKeyframe();

    Stats stats() const;
    void resetStats();

private:
    static void onVideo(camera_handle_t, camera_buffer_t *buf, void *arg);
    static void onStatus(camera_handle_t, camera_devstatus_t status, uint16_t extra, void *arg);
    void video(camera_buffer_t *buf);
    void logCapabilities();
    void fail(const char *what, camera_error_t error);

    Sink *m_sink;
    camera_handle_t m_handle;
    bool m_viewfinder, m_encoding;
    int m_orientation;                      // set by start() before the first callback
    // Callback thread only.
    QByteArray m_sps, m_pps;
    qint64 m_firstUs;
    int m_logged;
    double m_lastMs;

    mutable QMutex m_mu;
    Stats m_stats;
    double m_t0;
    double m_ageSum, m_intervalSum;
};

#endif
