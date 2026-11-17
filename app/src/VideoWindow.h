#ifndef VIDEOWINDOW_H
#define VIDEOWINDOW_H

#include "VideoDecoder.h"

#include <QAtomicInt>
#include <QMutex>
#include <QString>
#include <screen/screen.h>

// A Screen child window the decoder's frames are drawn into, to be bound by a QML
// ForeignWindowControl with the same windowId (docs: .claude/skills/bb10-pitfalls, "ForeignWindowControl
// and Screen windows"). NV12 if Screen takes it, else RGBX8888 with a conversion. The sink de-tiles
// each frame straight into the window's buffer, on the decoder's output thread.
class VideoWindow : public VideoDecoder::Sink
{
public:
    VideoWindow();
    ~VideoWindow();

    // Sized to the video. UI thread only (it asks the Cascades main window for its group), and
    // after the ForeignWindowControl with this windowId exists. False after logging why.
    // rgb skips NV12 and draws RGBX; above puts the window in front of Cascades instead of under
    // its hole (the decoder test's switches, 0.0.1.191: under the hole is the one kept).
    bool create(const QString &windowId, int width, int height, bool rgb, bool above);
    // Only once nothing calls frame() any more (the decoder closed).
    void destroy();
    bool isNv12() const { return m_format == SCREEN_FORMAT_NV12; }

    // Quarter turns clockwise that make the next frames upright (the peer's orientation); any
    // thread, takes effect from the next frame. The RGB fallback ignores it.
    void setRotation(int quarterTurns) { m_turns = quarterTurns & 3; }
    // The size of the control showing the window; the picture is cropped to its aspect (a fill)
    // by Screen's source viewport, so the control never outgrows its parent (a Cascades parent
    // squeezes a bigger child: the picture was squeezed, 0.0.1.203). Any thread; next frame.
    void setViewSize(int w, int h) { m_viewW = w; m_viewH = h; }
    // The picture as drawn, after turning, 0 x 0 until the first frame has been posted. The window
    // follows the frames: its buffers are made again when a frame's (turned) size is not theirs.
    void pictureSize(int *w, int *h) const;
    // Logs where Screen has the window (position and size), for the drawer's slide (7e).
    void logPosition(const char *what) const;

    void frame(const VideoDecoder::Frame &frame);

    // The time frame() took to bring a frame to linear NV12 in the buffer (and to RGB, in the
    // fallback), since the last reset.
    void detileStats(double *meanMs, double *maxMs) const;
    void resetStats();

private:
    bool build(int format, const QString &windowId, const QByteArray &group, int z);
    void toLinear(const VideoDecoder::Frame &f, unsigned char *y, int yStride, unsigned char *uv, int uvStride);
    bool resize(int w, int h);
    void crop(int vw, int vh);

    screen_context_t m_ctx;
    screen_window_t m_win;
    int m_format;
    int m_w, m_h;              // the buffers' size; written on the decoder's thread (and in create())
    QAtomicInt m_turns;
    QAtomicInt m_viewW, m_viewH;
    int m_cropForW, m_cropForH; // the view size the source viewport was set for; decoder thread
    QByteArray m_scratch;      // linear NV12 of the frame, before the turn and in the RGB fallback
    bool m_warned, m_resizeWarned;
    bool m_postLogged;
    mutable QMutex m_mu;
    int m_picW, m_picH;        // under m_mu: what pictureSize() says
    double m_detileSum, m_detileMax;
    int m_detileCount;
};

#endif
