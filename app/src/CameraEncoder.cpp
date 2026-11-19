#include "CameraEncoder.h"

#include <camera/camera_encoder.h>
#include <string.h>
#include <time.h>
#include <QDebug>
#include <QMutexLocker>
#include <QString>
#include <QVector>

static double nowMs()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static const char *errorName(camera_error_t e)
{
    switch (e) {
    case CAMERA_EOK: return "EOK";
    case CAMERA_EAGAIN: return "EAGAIN";
    case CAMERA_EINVAL: return "EINVAL";
    case CAMERA_ENODEV: return "ENODEV";
    case CAMERA_EMFILE: return "EMFILE";
    case CAMERA_EBADF: return "EBADF";
    case CAMERA_EACCESS: return "EACCESS (is use_camera granted?)";
    case CAMERA_EBADR: return "EBADR";
    case CAMERA_ENODATA: return "ENODATA";
    case CAMERA_ENOENT: return "ENOENT";
    case CAMERA_ENOMEM: return "ENOMEM";
    case CAMERA_EOPNOTSUPP: return "EOPNOTSUPP";
    case CAMERA_ETIMEDOUT: return "ETIMEDOUT";
    case CAMERA_EALREADY: return "EALREADY";
    case CAMERA_EBUSY: return "EBUSY";
    case CAMERA_ENOSPC: return "ENOSPC";
    case CAMERA_EUNINIT: return "EUNINIT";
    case CAMERA_EREGFAULT: return "EREGFAULT";
    case CAMERA_EMICINUSE: return "EMICINUSE";
    case CAMERA_EDESKTOPCAMERAINUSE: return "EDESKTOPCAMERAINUSE";
    case CAMERA_EPOWERDOWN: return "EPOWERDOWN";
    case CAMERA_EVIEWFINDERFROZEN: return "EVIEWFINDERFROZEN";
    case CAMERA_EOVERFLOW: return "EOVERFLOW";
    default: return "unknown";
    }
}

static const char *statusName(camera_devstatus_t s)
{
    switch (s) {
    case CAMERA_STATUS_DISCONNECTED: return "DISCONNECTED";
    case CAMERA_STATUS_CONNECTED: return "CONNECTED";
    case CAMERA_STATUS_POWERDOWN: return "POWERDOWN";
    case CAMERA_STATUS_VIDEOVF: return "VIDEOVF";
    case CAMERA_STATUS_MM_ERROR: return "MM_ERROR";
    case CAMERA_STATUS_CAPTURE_ABORTED: return "CAPTURE_ABORTED";
    case CAMERA_STATUS_RESOURCENOTAVAIL: return "RESOURCENOTAVAIL";
    case CAMERA_STATUS_POWERUP: return "POWERUP";
    case CAMERA_STATUS_VIEWFINDER_FREEZE: return "VIEWFINDER_FREEZE";
    case CAMERA_STATUS_VIEWFINDER_UNFREEZE: return "VIEWFINDER_UNFREEZE";
    case CAMERA_STATUS_LOWLIGHT: return "LOWLIGHT";
    case CAMERA_STATUS_FRAME_DROPPED: return "FRAME_DROPPED";
    case CAMERA_STATUS_VIEWFINDER_SUSPEND: return "VIEWFINDER_SUSPEND";
    case CAMERA_STATUS_VIEWFINDER_UNSUSPEND: return "VIEWFINDER_UNSUSPEND";
    default: return "other";
    }
}

// The knob for a picture the peer sees sideways: added to the quarter turns the camera's native
// orientation gives. The Q10 hasn't told us yet (the camera reported frameorientation 0 in portrait,
// and the SDK says rotation 0 gives upright buffers on a phone whose native orientation is 90, so
// the true count may be less than native/90): if the peer sees us turned, try 1, 2 or 3 (3 is -1).
static const int CalibrationTurns = 0;

// Where the NAL units of an Annex-B buffer are: the payload start (past the 3-byte start code) of
// each, so the type is the byte there & 31. ponytail: a start code is 00 00 01; the extra leading
// zero of a 4-byte one stays in the previous NAL's tail, harmless to the decoder.
static QList<int> nalStarts(const QByteArray &d)
{
    QList<int> out;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(d.constData());
    for (int i = 0; i + 3 < d.size(); i++) {
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) {
            out.append(i + 3);
            i += 2;
        }
    }
    return out;
}

CameraEncoder::CameraEncoder(Sink *sink)
    : m_sink(sink)
    , m_handle(CAMERA_HANDLE_INVALID)
    , m_viewfinder(false)
    , m_encoding(false)
    , m_orientation(0)
    , m_firstUs(-1)
    , m_logged(0)
    , m_lastMs(0)
    , m_t0(0)
    , m_ageSum(0)
    , m_intervalSum(0)
{
    memset(&m_stats, 0, sizeof m_stats);
}

CameraEncoder::~CameraEncoder()
{
    stop();
}

void CameraEncoder::fail(const char *what, camera_error_t error)
{
    if (error != CAMERA_EOK)
        qWarning("camera: %s failed: %s (%d)", what, errorName(error), int(error));
}

void CameraEncoder::logCapabilities()
{
    uint32_t n = 0;
    if (camera_get_supported_vf_resolutions(m_handle, 0, &n, 0) == CAMERA_EOK && n > 0) {
        QVector<camera_res_t> res(n);
        uint32_t got = 0;
        if (camera_get_supported_vf_resolutions(m_handle, n, &got, res.data()) == CAMERA_EOK) {
            QString s;
            for (uint32_t i = 0; i < got; i++)
                s += QString("%1x%2 ").arg(res[i].width).arg(res[i].height);
            qDebug("camera: video sizes: %s", qPrintable(s.trimmed()));
        }
    } else {
        qWarning("camera: no list of video sizes");
    }
    double rates[32];
    bool range = false;
    n = 0;
    if (camera_get_supported_vf_framerates(m_handle, CAMERA_FRAMETYPE_NV12, 32, &n, rates, &range) == CAMERA_EOK) {
        QString s;
        for (uint32_t i = 0; i < n && i < 32; i++)
            s += QString("%1 ").arg(rates[i]);
        qDebug("camera: frame rates (%s): %s", range ? "max, min" : "discrete", qPrintable(s.trimmed()));
    }
    uint32_t rot[8];
    bool nonSquare = false;
    n = 0;
    if (camera_get_supported_vf_rotations(m_handle, CAMERA_FRAMETYPE_NV12, 8, &n, rot, &nonSquare) == CAMERA_EOK) {
        QString s;
        for (uint32_t i = 0; i < n && i < 8; i++)
            s += QString("%1 ").arg(rot[i]);
        qDebug("camera: rotations: %s(non-square only: %d); preview-is-video: %d", qPrintable(s), nonSquare,
               camera_has_feature(m_handle, CAMERA_FEATURE_PREVIEWISVIDEO));
    }
}

bool CameraEncoder::start(bool front, int width, int height, int fps, int bitrate, int keyframeSeconds,
                          const QByteArray &group, const QByteArray &vfId)
{
    stop();
    camera_error_t e = camera_open(front ? CAMERA_UNIT_FRONT : CAMERA_UNIT_REAR, CAMERA_MODE_RW, &m_handle);
    if (e != CAMERA_EOK) {
        fail("camera_open", e);
        m_handle = CAMERA_HANDLE_INVALID;
        return false;
    }
    qDebug("camera: opened the %s camera", front ? "front" : "rear");
    // How far clockwise the sensor sits from the screen: the peer turns our frames back by it. If
    // the camera won't say, a guess stands in: a phone's rear sensor sits 90 degrees clockwise, its
    // front one 270 (unverified on the Q10).
    uint32_t native = 0;
    if (camera_get_native_orientation(m_handle, &native) == CAMERA_EOK) {
        qDebug("camera: native orientation %u degrees", unsigned(native));
        m_orientation = (int(native / 90) + CalibrationTurns) & 3;
    } else {
        qWarning("camera: no native orientation; assuming %d", front ? 3 : 1);
        m_orientation = ((front ? 3 : 1) + CalibrationTurns) & 3;
    }
    m_firstUs = -1;
    m_logged = 0;
    m_sps.clear();
    m_pps.clear();
    resetStats();

    // Rotation 0: the sensor's own orientation. What the camera says it is goes out per frame
    // (frameorientation, logged for the first), and a call sends it as our orientation.
    const uint32_t w = width, h = height, rotation = 0;
    const double rate = fps;
    if (!camera_has_feature(m_handle, CAMERA_FEATURE_VIDEO)) {
        qWarning("camera: this unit cannot record video");
        stop();
        return false;
    }
    if ((e = camera_set_vf_mode(m_handle, CAMERA_VFMODE_VIDEO)) != CAMERA_EOK) {
        fail("camera_set_vf_mode(VIDEO)", e); // not fatal: the default mode may take video too
    }
    logCapabilities();
    // The viewfinder window is made by the library in our group, named for the control. A preview
    // that is the video (this camera's PREVIEWISVIDEO) must match it in size and rotation.
    if ((e = camera_set_vf_property(m_handle,
                                    CAMERA_IMGPROP_WIN_GROUPID, group.constData(),
                                    CAMERA_IMGPROP_WIN_ID, vfId.constData(),
                                    CAMERA_IMGPROP_WIDTH, w,
                                    CAMERA_IMGPROP_HEIGHT, h,
                                    CAMERA_IMGPROP_ROTATION, rotation,
                                    CAMERA_IMGPROP_FRAMERATE, rate)) != CAMERA_EOK) {
        fail("camera_set_vf_property", e);
        stop();
        return false;
    }
    if ((e = camera_set_video_property(m_handle,
                                       CAMERA_IMGPROP_WIDTH, w,
                                       CAMERA_IMGPROP_HEIGHT, h,
                                       CAMERA_IMGPROP_FRAMERATE, rate,
                                       CAMERA_IMGPROP_ROTATION, rotation,
                                       CAMERA_IMGPROP_VIDEOCODEC, CAMERA_VIDEOCODEC_H264,
                                       CAMERA_IMGPROP_AUDIOCODEC, CAMERA_AUDIOCODEC_NONE)) != CAMERA_EOK) {
        fail("camera_set_video_property", e);
        stop();
        return false;
    }
    // Baseline for the peer's decoders; the interval is in frames. Rate control and level stay the
    // camera's own (a refused profile is logged, not fatal).
    if ((e = camera_set_videoencoder_parameter(m_handle,
                                               CAMERA_H264AVC_PROFILE, CAMERA_H264AVC_PROFILE_BASELINE,
                                               CAMERA_H264AVC_BITRATE, bitrate,
                                               CAMERA_H264AVC_KEYFRAMEINTERVAL, fps * keyframeSeconds,
                                               CAMERA_H264AVC_RATECONTROL, CAMERA_H264AVC_RATECONTROL_CBR)) != CAMERA_EOK)
        fail("camera_set_videoencoder_parameter", e);
    uint32_t gotW = 0, gotH = 0;
    int unitW = width, unitH = height; // the units' size: the camera's own word for it, when it gives it
    double gotRate = 0;
    if (camera_get_video_property(m_handle, CAMERA_IMGPROP_WIDTH, &gotW, CAMERA_IMGPROP_HEIGHT, &gotH,
                                  CAMERA_IMGPROP_FRAMERATE, &gotRate) == CAMERA_EOK) {
        qDebug("camera: video asked %ux%u@%g, camera has %ux%u@%g%s", w, h, rate, gotW, gotH, gotRate,
               (gotW != w || gotH != h) ? " (SIZE DIFFERS: the decoder and the window will mismatch)" : "");
        m_stats.width = gotW;
        m_stats.height = gotH;
        if (gotW > 0 && gotH > 0) {
            unitW = gotW;
            unitH = gotH;
        }
    }
    if ((e = camera_start_viewfinder(m_handle, 0, onStatus, this)) != CAMERA_EOK) {
        fail("camera_start_viewfinder", e);
        stop();
        return false;
    }
    m_viewfinder = true;
    // Before the first unit, which comes on the camera library's thread as soon as the encoding starts: from here the
    // sink's size record is on the link ahead of every unit of this run.
    m_sink->cameraSize(unitW, unitH);
    if ((e = camera_start_encode(m_handle, 0, onVideo, 0, onStatus, this)) != CAMERA_EOK) {
        fail("camera_start_encode", e);
        stop();
        return false;
    }
    m_encoding = true;
    QMutexLocker lock(&m_mu);
    m_t0 = nowMs();
    qDebug("camera: encoding H.264 baseline %dx%d@%d, %d bit/s, a keyframe every %d frames", width, height, fps,
           bitrate, fps * keyframeSeconds);
    return true;
}

void CameraEncoder::stop()
{
    if (m_handle == CAMERA_HANDLE_INVALID)
        return;
    if (m_encoding)
        fail("camera_stop_encode", camera_stop_encode(m_handle));
    if (m_viewfinder)
        fail("camera_stop_viewfinder", camera_stop_viewfinder(m_handle));
    camera_close(m_handle);
    m_handle = CAMERA_HANDLE_INVALID;
    m_encoding = m_viewfinder = false;
    qDebug("camera: closed");
}

bool CameraEncoder::setBitrate(int bitsPerSecond)
{
    if (!m_encoding)
        return false;
    const camera_error_t e = camera_set_videoencoder_parameter(m_handle, CAMERA_H264AVC_BITRATE, bitsPerSecond);
    qDebug("camera: bit rate %d bit/s while encoding: %s (%d)", bitsPerSecond, errorName(e), int(e));
    return e == CAMERA_EOK;
}

void CameraEncoder::requestKeyframe()
{
    if (m_encoding)
        fail("camera_force_video_keyframe", camera_force_video_keyframe(m_handle));
}

CameraEncoder::Stats CameraEncoder::stats() const
{
    QMutexLocker lock(&m_mu);
    Stats s = m_stats;
    s.wallMs = nowMs() - m_t0;
    if (s.frames > 0) {
        s.ageMeanMs = m_ageSum / s.frames;
    }
    if (s.frames > 1)
        s.intervalMeanMs = m_intervalSum / (s.frames - 1);
    return s;
}

void CameraEncoder::resetStats()
{
    QMutexLocker lock(&m_mu);
    const int w = m_stats.width, h = m_stats.height;
    memset(&m_stats, 0, sizeof m_stats);
    m_stats.width = w;
    m_stats.height = h;
    m_t0 = nowMs();
    m_ageSum = m_intervalSum = 0;
    m_lastMs = 0;
}

void CameraEncoder::onStatus(camera_handle_t, camera_devstatus_t status, uint16_t extra, void *arg)
{
    CameraEncoder *self = static_cast<CameraEncoder *>(arg);
    if (status == CAMERA_STATUS_FRAME_DROPPED) {
        QMutexLocker lock(&self->m_mu);
        self->m_stats.dropped++;
        return;
    }
    qDebug("camera: status %s (%d), extra %u", statusName(status), int(status), unsigned(extra));
}

void CameraEncoder::onVideo(camera_handle_t, camera_buffer_t *buf, void *arg)
{
    static_cast<CameraEncoder *>(arg)->video(buf);
}

void CameraEncoder::video(camera_buffer_t *buf)
{
    const double now = nowMs();
    if (buf->frametype != CAMERA_FRAMETYPE_COMPRESSEDVIDEO || !buf->framebuf) {
        qWarning("camera: a frame of type %d, not compressed video", int(buf->frametype));
        return;
    }
    const camera_frame_compressedvideo_t &cv = buf->framedesc.compvid;
    const QByteArray data(reinterpret_cast<const char *>(buf->framebuf), int(cv.bufsize));

    // Which NAL units are in it: 7 SPS, 8 PPS, 5 IDR slice, 1 other slice, 6 SEI, 9 delimiter.
    bool hasSlice = false, hasIdr = false, hasSps = false, hasPps = false;
    QString types;
    const QList<int> starts = nalStarts(data);
    for (int k = 0; k < starts.size(); k++) {
        const int i = starts[k];
        const int type = data[i] & 31;
        types += QString::number(type) + " ";
        const int end = k + 1 < starts.size() ? starts[k + 1] - 3 : data.size();
        // Trailing zeros are the next start code's leading zero_byte or padding.
        int stop = end;
        while (stop > i && data[stop - 1] == 0)
            stop--;
        const QByteArray nal = QByteArray("\0\0\0\1", 4) + data.mid(i, stop - i);
        if (type == 7) {
            hasSps = true;
            m_sps = nal;
        } else if (type == 8) {
            hasPps = true;
            m_pps = nal;
        } else if (type == 5) {
            hasSlice = hasIdr = true;
        } else if (type == 1) {
            hasSlice = true;
        }
    }
    if (m_logged < 4) {
        m_logged++;
        qDebug("camera: frame %d: %d bytes, codec %d, keyframe flag %d, NAL types [%s], orientation %d, "
               "age %.1f ms", m_logged, data.size(), int(cv.codec), cv.keyframe, qPrintable(types.trimmed()),
               int(buf->frameorientation), now - buf->frametimestamp / 1e6);
    }
    if (!hasSlice) // parameter sets (or an SEI) on their own: kept above, sent with the next IDR
        return;

    const bool key = hasIdr || cv.keyframe;
    QByteArray unit = data;
    if (key && !(hasSps && hasPps)) {
        if (m_sps.isEmpty() || m_pps.isEmpty())
            qWarning("camera: a keyframe with no SPS/PPS seen yet");
        else
            unit = m_sps + m_pps + data;
    }
    const qint64 us = buf->frametimestamp / 1000;
    if (m_firstUs < 0)
        m_firstUs = us;
    {
        QMutexLocker lock(&m_mu);
        m_stats.frames++;
        if (key)
            m_stats.keyframes++;
        m_stats.bytes += unit.size();
        if (unit.size() > m_stats.maxFrameBytes)
            m_stats.maxFrameBytes = unit.size();
        const double age = now - buf->frametimestamp / 1e6;
        m_ageSum += age;
        if (age > m_stats.ageMaxMs)
            m_stats.ageMaxMs = age;
        if (m_lastMs > 0) {
            const double gap = now - m_lastMs;
            m_intervalSum += gap;
            if (gap > m_stats.intervalMaxMs)
                m_stats.intervalMaxMs = gap;
        }
        m_lastMs = now;
    }
    m_sink->accessUnit(unit, us - m_firstUs, key, m_orientation);
}
