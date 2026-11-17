#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <OMX_Core.h>
#include <OMX_Component.h>

// The Q10's hardware H.264 decoder, through OpenMAX IL (docs/plan/phase7.md, "Video calls: the
// hardware-decode gate", third round): Qualcomm's OMX.qcom.video.decoder.avc from the phone's own
// libOmxCore.so, one Annex-B access unit per feed(). The receiving half of a call (CallVideo).
// The IL work runs on the caller of feed()/open()/close() (never the UI thread) and
// on one thread of its own, which hands each decoded frame to the Sink and gives the buffer back.
class VideoDecoder
{
public:
    // Qualcomm's NV12 in 64x32 tiles (QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka).
    static const unsigned TiledNV12 = 0x7f000004;
    // Plain semi-planar NV12 (OMX_COLOR_FormatYUV420SemiPlanar).
    static const unsigned LinearNV12 = 21;

    // One decoded picture, valid only inside Sink::frame(). width and height are the picture's;
    // stride and sliceHeight are the buffer's (the tiles follow width and height, see detile()).
    struct Frame
    {
        const unsigned char *data;
        int size;            // bytes that may be read from data
        int width, height, stride, sliceHeight;
        unsigned colorFormat;
        qint64 timestampUs;  // the one given to feed()
    };

    // Called on the decoder's output thread, one frame at a time, in order. Keep it short: the
    // decoder gets its buffer back only when it returns.
    class Sink
    {
    public:
        virtual ~Sink() {}
        virtual void frame(const Frame &frame) = 0;
    };

    struct Stats
    {
        int framesIn, framesOut;
        int errors;               // OMX errors seen since open(), logged only the first few
        double firstFrameMs;      // first feed() to the first picture; -1 until there is one
        double latencyMeanMs;     // feed() to the picture's arrival, per frame, since resetStats()
        double latencyMaxMs;
        double cpuMs, wallMs;     // this whole process's CPU time (clock_gettime, process) and the
                                  // time elapsed, both since open() or resetStats()
    };

    explicit VideoDecoder(Sink *sink);
    ~VideoDecoder();

    // Loads the core and brings the decoder to Executing. Declare the size the stream really has:
    // a wrong one costs a 270 ms output reconfiguration (handled, nothing lost). maxAccessUnit
    // sizes the input buffers (+4 KB); a larger unit is refused by feed(). False after logging why.
    bool open(int width, int height, int fps, int maxAccessUnit = 128 * 1024);
    // One access unit (all of a picture's NALs; SPS/PPS may lead it or come in an earlier call).
    // Waits up to 200 ms for a free input buffer; false if none or on error.
    bool feed(const QByteArray &accessUnit, qint64 timestampUs);
    // Safe to call twice, and when nothing is open.
    void close();
    bool isOpen() const { return m_open; }
    // An output reconfiguration that didn't complete: the decoder gives no more pictures. The
    // feeder closes and opens it again (CallVideo); the next IDR starts the picture.
    bool broken() const;

    Stats stats() const;
    void resetStats();

    // Splits an Annex-B stream into access units (3- or 4-byte start codes). ponytail: a unit ends
    // where a slice has been seen and the next NAL is a slice, SEI, SPS, PPS or delimiter, so one
    // slice per picture; a multi-slice stream would need first_mb_in_slice read.
    static QList<QByteArray> splitAccessUnits(const QByteArray &annexB);
    // De-tiles a TiledNV12 frame into linear NV12 planes: y (height rows) and uv (height/2 rows),
    // each with its own stride of at least width. False if the frame is too short for its size.
    static bool detile(const Frame &frame, unsigned char *y, int yStride, unsigned char *uv, int uvStride);

private:
    // SPS and PPS of an access unit, start codes kept; empty if it has none.
    static QByteArray parameterSets(const QByteArray &accessUnit);

    class Thread : public QThread
    {
    public:
        explicit Thread(VideoDecoder *owner) : m_owner(owner) {}
    protected:
        void run() { m_owner->outputLoop(); }
    private:
        VideoDecoder *m_owner;
    };
    struct Filled
    {
        OMX_BUFFERHEADERTYPE *header;
        double arrivedMs;
    };

    static OMX_ERRORTYPE onEvent(OMX_HANDLETYPE, OMX_PTR app, OMX_EVENTTYPE event, OMX_U32 d1, OMX_U32 d2, OMX_PTR);
    static OMX_ERRORTYPE onEmptied(OMX_HANDLETYPE, OMX_PTR app, OMX_BUFFERHEADERTYPE *header);
    static OMX_ERRORTYPE onFilled(OMX_HANDLETYPE, OMX_PTR app, OMX_BUFFERHEADERTYPE *header);

    bool step(const char *what, OMX_ERRORTYPE error);
    bool setUp(int width, int height, int fps, int maxAccessUnit);
    bool allocateOutput();
    bool waitState(unsigned state, int ms);
    bool waitFlag(bool *flag, int ms);
    OMX_BUFFERHEADERTYPE *takeFreeInput(int ms);
    bool sendBuffer(OMX_BUFFERHEADERTYPE *header, const char *data, int size, unsigned flags, qint64 ts);
    void outputLoop();
    void deliver(const Filled &filled);
    void reconfigureOutput(unsigned index);
    bool waitOutputReturned(int ms);
    void readOutputPort();

    Sink *m_sink;
    Thread m_thread;
    OMX_HANDLETYPE m_handle;
    bool m_open;
    bool m_inited;                      // OMX_Init done and not yet undone
    int m_inPort, m_outPort;
    QList<OMX_BUFFERHEADERTYPE *> m_inHeaders, m_outHeaders;
    int m_fps;
    // The output port as the decoder reports it; the output thread owns these.
    int m_outW, m_outH, m_outStride, m_outSlice, m_outCount;
    unsigned m_outBytes;
    unsigned m_outColor;
    bool m_warnedFill, m_warnedSize, m_warnedFree;

    // Feeder-thread only.
    bool m_cfgSent, m_firstUnit;

    // Everything below is guarded by m_mu; m_cv wakes every waiter.
    mutable QMutex m_mu;
    QWaitCondition m_cv;
    QList<OMX_BUFFERHEADERTYPE *> m_freeIn;
    QList<Filled> m_filled;
    QHash<qint64, double> m_sent;       // timestamp -> when feed() sent it
    int m_state;                        // the last state a SendCommand reached; -1 none
    bool m_outDisabled, m_outEnabled, m_portChanged, m_stopping, m_broken;
    unsigned m_portIndex;               // the last PortSettingsChanged's data2: what changed
    int m_framesIn, m_framesOut, m_errors, m_latCount;
    double m_firstFeedMs, m_firstFrameMs, m_latSum, m_latMax, m_cpu0, m_wall0;
};

#endif
