#include "VideoDecoder.h"
#include "VideoOmx.h"

#include <OMX_IVCommon.h>
#include <OMX_Video.h>
#include <dlfcn.h>
#include <string.h>
#include <time.h>
#include <QDebug>
#include <QMutexLocker>

// The sequence is tools/h264gate/h264il.c's, which decoded every frame on the Q10 as root; what
// an app may do was the question the decoder test answered (0.0.1.190). Each step is logged once per open, errors
// by name; nothing is logged per frame.

static const char *ComponentName = "OMX.qcom.video.decoder.avc";
// The system's core first, so a different working directory or library path can't shadow it.
static const char *CorePath = "/base/lib/libOmxCore.so";
static const int MaxLoggedErrors = 5;

typedef OMX_ERRORTYPE (*InitFn)(void);
typedef OMX_ERRORTYPE (*GetHandleFn)(OMX_HANDLETYPE *, OMX_STRING, OMX_PTR, OMX_CALLBACKTYPE *);
typedef OMX_ERRORTYPE (*FreeHandleFn)(OMX_HANDLETYPE);

// ponytail: the core stays loaded for the life of the process (no dlclose); one decoder at a time.
static struct
{
    void *lib;
    InitFn init, deinit;
    GetHandleFn getHandle;
    FreeHandleFn freeHandle;
} core;

static bool loadCore()
{
    if (core.lib)
        return true;
    void *lib = dlopen(CorePath, RTLD_NOW);
    if (!lib) {
        qWarning("video decoder: dlopen %s: %s", CorePath, dlerror());
        lib = dlopen("libOmxCore.so", RTLD_NOW);
    }
    if (!lib) {
        qWarning("video decoder: dlopen libOmxCore.so: %s", dlerror());
        return false;
    }
    core.init = (InitFn)dlsym(lib, "OMX_Init");
    core.deinit = (InitFn)dlsym(lib, "OMX_Deinit");
    core.getHandle = (GetHandleFn)dlsym(lib, "OMX_GetHandle");
    core.freeHandle = (FreeHandleFn)dlsym(lib, "OMX_FreeHandle");
    if (!core.init || !core.deinit || !core.getHandle || !core.freeHandle) {
        qWarning("video decoder: the OpenMAX IL core lacks an entry point");
        dlclose(lib);
        return false;
    }
    core.lib = lib;
    qDebug("video decoder: core loaded");
    return true;
}

VideoDecoder::VideoDecoder(Sink *sink)
    : m_sink(sink)
    , m_thread(this)
    , m_handle(0)
    , m_open(false)
    , m_inited(false)
    , m_inPort(0)
    , m_outPort(1)
    , m_fps(30)
    , m_outW(0), m_outH(0), m_outStride(0), m_outSlice(0), m_outCount(0), m_outBytes(0)
    , m_outColor(0)
    , m_warnedFill(false), m_warnedSize(false), m_warnedFree(false)
    , m_cfgSent(false), m_firstUnit(true)
    , m_state(-1)
    , m_outDisabled(false), m_outEnabled(false), m_portChanged(false), m_stopping(false), m_broken(false)
    , m_portIndex(0)
    , m_framesIn(0), m_framesOut(0), m_errors(0), m_latCount(0)
    , m_firstFeedMs(-1), m_firstFrameMs(-1), m_latSum(0), m_latMax(0), m_cpu0(0), m_wall0(0)
{
}

VideoDecoder::~VideoDecoder()
{
    close();
}

bool VideoDecoder::step(const char *what, OMX_ERRORTYPE error)
{
    if (error == OMX_ErrorNone)
        qDebug("video decoder: %s -> ok", what);
    else
        qWarning("video decoder: %s -> %s", what, errName(error).constData());
    return error == OMX_ErrorNone;
}

OMX_ERRORTYPE VideoDecoder::onEvent(OMX_HANDLETYPE, OMX_PTR app, OMX_EVENTTYPE event, OMX_U32 d1, OMX_U32 d2, OMX_PTR)
{
    VideoDecoder *self = static_cast<VideoDecoder *>(app);
    QMutexLocker lock(&self->m_mu);
    if (event == OMX_EventCmdComplete) {
        if (d1 == OMX_CommandStateSet)
            self->m_state = int(d2);
        else if (d1 == OMX_CommandPortDisable && d2 == OMX_U32(self->m_outPort))
            self->m_outDisabled = true;
        else if (d1 == OMX_CommandPortEnable && d2 == OMX_U32(self->m_outPort))
            self->m_outEnabled = true;
    } else if (event == OMX_EventPortSettingsChanged) {
        qDebug("video decoder: port settings changed, port %u, index 0x%x", unsigned(d1), unsigned(d2));
        if (d1 == OMX_U32(self->m_outPort)) {
            self->m_portChanged = true;
            self->m_portIndex = unsigned(d2);
        }
    } else if (event == OMX_EventError) {
        // Often a run of them (a corrupt stream); the first few by name, the count in stats().
        if (++self->m_errors <= MaxLoggedErrors)
            qWarning("video decoder: event error %s (%u)", errName(OMX_ERRORTYPE(d1)).constData(), unsigned(d2));
    }
    self->m_cv.wakeAll();
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VideoDecoder::onEmptied(OMX_HANDLETYPE, OMX_PTR app, OMX_BUFFERHEADERTYPE *header)
{
    VideoDecoder *self = static_cast<VideoDecoder *>(app);
    QMutexLocker lock(&self->m_mu);
    self->m_freeIn.append(header);
    self->m_cv.wakeAll();
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VideoDecoder::onFilled(OMX_HANDLETYPE, OMX_PTR app, OMX_BUFFERHEADERTYPE *header)
{
    // Only queued here: the component's thread must not wait for the sink, and FillThisBuffer is
    // called again from the output thread.
    const Filled filled = { header, nowMs() };
    VideoDecoder *self = static_cast<VideoDecoder *>(app);
    QMutexLocker lock(&self->m_mu);
    self->m_filled.append(filled);
    self->m_cv.wakeAll();
    return OMX_ErrorNone;
}

bool VideoDecoder::waitState(unsigned state, int ms)
{
    const double end = nowMs() + ms;
    QMutexLocker lock(&m_mu);
    while (m_state != int(state)) {
        const double left = end - nowMs();
        if (left <= 0) {
            qWarning("video decoder: state %u not reached in %d ms", state, ms);
            return false;
        }
        m_cv.wait(&m_mu, (unsigned long)left + 1);
    }
    return true;
}

bool VideoDecoder::waitFlag(bool *flag, int ms)
{
    const double end = nowMs() + ms;
    QMutexLocker lock(&m_mu);
    while (!*flag) {
        const double left = end - nowMs();
        if (left <= 0)
            return false;
        m_cv.wait(&m_mu, (unsigned long)left + 1);
    }
    return true;
}

bool VideoDecoder::setUp(int width, int height, int fps, int maxAccessUnit)
{
    if (!loadCore() || !step("OMX_Init", core.init()))
        return false;
    m_inited = true;
    static OMX_CALLBACKTYPE callbacks = { onEvent, onEmptied, onFilled };
    if (!step("OMX_GetHandle", core.getHandle(&m_handle, (OMX_STRING)ComponentName, this, &callbacks))) {
        m_handle = 0;
        return false;
    }
    OMX_PORT_PARAM_TYPE ports;
    initStruct(ports);
    if (!step("GetParameter(video ports)", OMX_GetParameter(m_handle, OMX_IndexParamVideoInit, &ports)))
        return false;
    m_inPort = ports.nStartPortNumber;
    m_outPort = m_inPort + 1;
    OMX_PARAM_PORTDEFINITIONTYPE in;
    initStruct(in);
    in.nPortIndex = m_inPort;
    if (!step("GetParameter(input port definition)", OMX_GetParameter(m_handle, OMX_IndexParamPortDefinition, &in)))
        return false;
    in.format.video.nFrameWidth = width;
    in.format.video.nFrameHeight = height;
    in.format.video.xFramerate = fps << 16;
    in.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
    // A buffer for the largest access unit; the component's own size is a default for its streams.
    const unsigned need = (unsigned(maxAccessUnit) + 4096 + 4095) & ~4095u;
    if (in.nBufferSize < need)
        in.nBufferSize = need;
    if (!step("SetParameter(input: AVC)", OMX_SetParameter(m_handle, OMX_IndexParamPortDefinition, &in)))
        return false;
    OMX_VIDEO_PARAM_PORTFORMATTYPE format;
    initStruct(format);
    format.nPortIndex = m_inPort;
    format.eCompressionFormat = OMX_VIDEO_CodingAVC;
    format.xFramerate = fps << 16;
    step("SetParameter(input port format)", OMX_SetParameter(m_handle, OMX_IndexParamVideoPortFormat, &format));
    initStruct(in);
    in.nPortIndex = m_inPort;
    if (!step("GetParameter(input port definition, again)", OMX_GetParameter(m_handle, OMX_IndexParamPortDefinition, &in)))
        return false;
    readOutputPort();
    if (m_outCount <= 0)
        return false;

    // Every buffer of both ports is allocated while the state change to Idle is pending.
    {
        QMutexLocker lock(&m_mu);
        m_state = -1;
    }
    if (!step("SendCommand(Idle)", OMX_SendCommand(m_handle, OMX_CommandStateSet, OMX_StateIdle, NULL)))
        return false;
    const int inCount = in.nBufferCountActual < OMX_U32(MaxBuffers) ? int(in.nBufferCountActual) : MaxBuffers;
    for (int i = 0; i < inCount; i++) {
        OMX_BUFFERHEADERTYPE *h = 0;
        if (!step("AllocateBuffer(input)", OMX_AllocateBuffer(m_handle, &h, m_inPort, NULL, in.nBufferSize)))
            return false;
        m_inHeaders.append(h);
        m_freeIn.append(h);
    }
    qDebug("video decoder: %d input buffers of %u bytes", inCount, unsigned(in.nBufferSize));
    if (!allocateOutput() || !waitState(OMX_StateIdle, 3000))
        return false;
    qDebug("video decoder: Idle");
    {
        QMutexLocker lock(&m_mu);
        m_state = -1;
    }
    if (!step("SendCommand(Executing)", OMX_SendCommand(m_handle, OMX_CommandStateSet, OMX_StateExecuting, NULL))
        || !waitState(OMX_StateExecuting, 3000))
        return false;
    qDebug("video decoder: Executing");
    for (int i = 0; i < m_outHeaders.size(); i++)
        if (!step("FillThisBuffer(initial)", OMX_FillThisBuffer(m_handle, m_outHeaders[i])))
            return false;
    return true;
}

bool VideoDecoder::open(int width, int height, int fps, int maxAccessUnit)
{
    close();
    {
        QMutexLocker lock(&m_mu);
        m_stopping = false;
        m_state = -1;
        m_outDisabled = m_outEnabled = m_portChanged = m_broken = false;
        m_portIndex = 0;
        m_freeIn.clear();
        m_filled.clear();
        m_sent.clear();
        m_errors = 0;
        m_firstFeedMs = m_firstFrameMs = -1;
    }
    m_fps = fps;
    m_cfgSent = false;
    m_firstUnit = true;
    m_warnedFill = m_warnedSize = m_warnedFree = false;
    qDebug("video decoder: open %dx%d @%d fps, access units up to %d bytes", width, height, fps, maxAccessUnit);
    if (!setUp(width, height, fps, maxAccessUnit)) {
        qWarning("video decoder: open failed");
        close();
        return false;
    }
    resetStats();
    m_open = true;
    m_thread.start();
    return true;
}

OMX_BUFFERHEADERTYPE *VideoDecoder::takeFreeInput(int ms)
{
    const double end = nowMs() + ms;
    QMutexLocker lock(&m_mu);
    while (m_freeIn.isEmpty() && !m_stopping) {
        const double left = end - nowMs();
        if (left <= 0)
            return 0;
        m_cv.wait(&m_mu, (unsigned long)left + 1);
    }
    return m_stopping ? 0 : m_freeIn.takeLast();
}

bool VideoDecoder::sendBuffer(OMX_BUFFERHEADERTYPE *h, const char *data, int size, unsigned flags, qint64 ts)
{
    memcpy(h->pBuffer, data, size);
    h->nFilledLen = size;
    h->nOffset = 0;
    h->nFlags = flags;
    h->nTimeStamp = ts;
    const OMX_ERRORTYPE e = OMX_EmptyThisBuffer(m_handle, h);
    if (e != OMX_ErrorNone) {
        qWarning("video decoder: EmptyThisBuffer -> %s", errName(e).constData());
        QMutexLocker lock(&m_mu);
        m_freeIn.append(h);
    }
    return e == OMX_ErrorNone;
}

bool VideoDecoder::feed(const QByteArray &unit, qint64 ts)
{
    if (!m_open || unit.isEmpty())
        return false;
    // The decoder wants the SPS and PPS as a buffer of their own first; the unit that carries
    // them goes in whole after, as the gate did.
    if (!m_cfgSent) {
        const QByteArray cfg = parameterSets(unit);
        if (!cfg.isEmpty()) {
            OMX_BUFFERHEADERTYPE *h = takeFreeInput(200);
            if (!h || cfg.size() > int(h->nAllocLen) || !sendBuffer(h, cfg.constData(), cfg.size(), OMX_BUFFERFLAG_CODECCONFIG, 0))
                return false;
            m_cfgSent = true;
        } else {
            return false; // nothing decodes before the SPS and PPS: wait for the IDR that brings them
        }
    }
    OMX_BUFFERHEADERTYPE *h = takeFreeInput(200);
    if (!h) {
        if (!m_warnedFree)
            qWarning("video decoder: no free input buffer in 200 ms");
        m_warnedFree = true;
        return false;
    }
    if (unit.size() > int(h->nAllocLen)) {
        if (!m_warnedSize)
            qWarning("video decoder: access unit of %d bytes over the %u of an input buffer", unit.size(), unsigned(h->nAllocLen));
        m_warnedSize = true;
        QMutexLocker lock(&m_mu);
        m_freeIn.append(h);
        return false;
    }
    const double t = nowMs();
    {
        QMutexLocker lock(&m_mu);
        if (m_firstUnit)
            m_firstFeedMs = t;
        if (m_sent.size() > 256)
            m_sent.clear(); // frames the decoder never returned must not pile up
        m_sent.insert(ts, t);
        m_framesIn++;
    }
    const unsigned flags = OMX_BUFFERFLAG_ENDOFFRAME | (m_firstUnit ? OMX_BUFFERFLAG_STARTTIME : 0);
    m_firstUnit = false;
    return sendBuffer(h, unit.constData(), unit.size(), flags, ts);
}

bool VideoDecoder::broken() const
{
    QMutexLocker lock(&m_mu);
    return m_broken;
}

void VideoDecoder::close()
{
    {
        QMutexLocker lock(&m_mu);
        m_stopping = true;
        m_cv.wakeAll();
    }
    m_thread.wait();
    if (m_handle) {
        qDebug("video decoder: closing");
        {
            QMutexLocker lock(&m_mu);
            m_state = -1;
        }
        step("SendCommand(Idle)", OMX_SendCommand(m_handle, OMX_CommandStateSet, OMX_StateIdle, NULL));
        waitState(OMX_StateIdle, 3000);
        {
            QMutexLocker lock(&m_mu);
            m_state = -1;
        }
        step("SendCommand(Loaded)", OMX_SendCommand(m_handle, OMX_CommandStateSet, OMX_StateLoaded, NULL));
        for (int i = 0; i < m_inHeaders.size(); i++)
            OMX_FreeBuffer(m_handle, m_inPort, m_inHeaders[i]);
        for (int i = 0; i < m_outHeaders.size(); i++)
            OMX_FreeBuffer(m_handle, m_outPort, m_outHeaders[i]);
        waitState(OMX_StateLoaded, 3000);
        step("OMX_FreeHandle", core.freeHandle(m_handle));
    }
    if (m_inited)
        step("OMX_Deinit", core.deinit());
    m_inited = false;
    m_handle = 0;
    m_open = false;
    m_inHeaders.clear();
    m_outHeaders.clear();
}

VideoDecoder::Stats VideoDecoder::stats() const
{
    QMutexLocker lock(&m_mu);
    Stats s;
    s.framesIn = m_framesIn;
    s.framesOut = m_framesOut;
    s.errors = m_errors;
    s.firstFrameMs = m_firstFrameMs;
    s.latencyMeanMs = m_latCount ? m_latSum / m_latCount : 0;
    s.latencyMaxMs = m_latMax;
    s.cpuMs = cpuMs() - m_cpu0;
    s.wallMs = nowMs() - m_wall0;
    return s;
}

void VideoDecoder::resetStats()
{
    QMutexLocker lock(&m_mu);
    m_framesIn = m_framesOut = m_latCount = 0;
    m_latSum = m_latMax = 0;
    m_cpu0 = cpuMs();
    m_wall0 = nowMs();
}
