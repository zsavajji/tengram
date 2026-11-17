#include "VideoDecoder.h"
#include "VideoOmx.h"

#include <OMX_IVCommon.h>
#include <OMX_Video.h>
#include <QDebug>
#include <QMutexLocker>

// The output side of VideoDecoder: the port's description, its buffers, and the thread that hands
// the decoded pictures to the sink.


void VideoDecoder::readOutputPort()
{
    OMX_PARAM_PORTDEFINITIONTYPE d;
    initStruct(d);
    d.nPortIndex = m_outPort;
    if (!step("GetParameter(output port definition)", OMX_GetParameter(m_handle, OMX_IndexParamPortDefinition, &d)))
        return;
    m_outW = d.format.video.nFrameWidth;
    m_outH = d.format.video.nFrameHeight;
    m_outStride = d.format.video.nStride;
    m_outSlice = d.format.video.nSliceHeight;
    m_outColor = d.format.video.eColorFormat;
    m_outCount = d.nBufferCountActual < OMX_U32(MaxBuffers) ? int(d.nBufferCountActual) : MaxBuffers;
    m_outBytes = d.nBufferSize;
    qDebug("video decoder: output %dx%d stride %d slice %d color 0x%x, %d buffers of %u bytes",
           m_outW, m_outH, m_outStride, m_outSlice, m_outColor, m_outCount, m_outBytes);
    if (m_outColor != TiledNV12 && m_outColor != LinearNV12)
        qWarning("video decoder: output color 0x%x is neither tiled nor linear NV12", m_outColor);
}

bool VideoDecoder::allocateOutput()
{
    for (int i = 0; i < m_outCount; i++) {
        OMX_BUFFERHEADERTYPE *h = 0;
        if (!step("AllocateBuffer(output)", OMX_AllocateBuffer(m_handle, &h, m_outPort, NULL, m_outBytes)))
            return false;
        m_outHeaders.append(h);
    }
    return true;
}

void VideoDecoder::outputLoop()
{
    for (;;) {
        Filled filled = { 0, 0 };
        bool reconfigure = false;
        unsigned index = 0;
        {
            QMutexLocker lock(&m_mu);
            while (!m_stopping && m_filled.isEmpty() && !m_portChanged)
                m_cv.wait(&m_mu);
            if (m_stopping)
                return;
            if (m_portChanged) {
                m_portChanged = false;
                reconfigure = true;
                index = m_portIndex;
            } else {
                filled = m_filled.takeFirst();
            }
        }
        if (reconfigure)
            reconfigureOutput(index);
        else
            deliver(filled);
    }
}

void VideoDecoder::deliver(const Filled &filled)
{
    OMX_BUFFERHEADERTYPE *h = filled.header;
    if (h->nFilledLen) {
        const qint64 ts = h->nTimeStamp;
        {
            QMutexLocker lock(&m_mu);
            QHash<qint64, double>::iterator it = m_sent.find(ts);
            if (it != m_sent.end()) {
                const double lat = filled.arrivedMs - it.value();
                m_sent.erase(it);
                m_latSum += lat;
                m_latCount++;
                if (lat > m_latMax)
                    m_latMax = lat;
            }
            if (m_framesOut++ == 0)
                m_firstFrameMs = filled.arrivedMs - m_firstFeedMs;
        }
        const Frame frame = { h->pBuffer + h->nOffset, int(h->nAllocLen - h->nOffset), m_outW, m_outH,
                              m_outStride, m_outSlice, m_outColor, ts };
        m_sink->frame(frame);
    }
    bool stopping;
    {
        QMutexLocker lock(&m_mu);
        stopping = m_stopping;
    }
    if (stopping || (h->nFlags & OMX_BUFFERFLAG_EOS))
        return;
    const OMX_ERRORTYPE e = OMX_FillThisBuffer(m_handle, h);
    if (e != OMX_ErrorNone && !m_warnedFill) {
        m_warnedFill = true;
        qWarning("video decoder: FillThisBuffer -> %s", errName(e).constData());
    }
}

// Every output buffer is back with us (FillBufferDone puts each in m_filled, the port being
// disabled returns the ones the decoder held). False after ms.
bool VideoDecoder::waitOutputReturned(int ms)
{
    const double end = nowMs() + ms;
    QMutexLocker lock(&m_mu);
    while (m_filled.size() < m_outHeaders.size()) {
        const double left = end - nowMs();
        if (left <= 0)
            return false;
        m_cv.wait(&m_mu, (unsigned long)left + 1);
    }
    return true;
}

// The decoder learned the real picture size (or its crop). A crop alone keeps the port: read the
// new size. Else disable the output port, free its buffers once the decoder has handed them all
// back (freeing one it still held stalled the disable on the Q10, 0.0.1.202: the picture stopped
// after one frame), read the new definition, enable it and allocate again (what h264il did; 270 ms).
void VideoDecoder::reconfigureOutput(unsigned index)
{
    if (index == OMX_IndexConfigCommonOutputCrop) {
        qDebug("video decoder: output crop changed");
        readOutputPort();
        return;
    }
    qDebug("video decoder: output port settings changed, reconfiguring");
    {
        QMutexLocker lock(&m_mu);
        m_outDisabled = m_outEnabled = false;
    }
    bool ok = step("SendCommand(PortDisable, output)", OMX_SendCommand(m_handle, OMX_CommandPortDisable, m_outPort, NULL));
    if (!waitOutputReturned(1000))
        qWarning("video decoder: output buffers not all returned in 1 s; freeing them anyway");
    for (int i = 0; i < m_outHeaders.size(); i++)
        OMX_FreeBuffer(m_handle, m_outPort, m_outHeaders[i]);
    m_outHeaders.clear();
    if (!waitFlag(&m_outDisabled, 2000)) {
        qWarning("video decoder: output port not disabled in 2 s");
        ok = false;
    }
    {
        QMutexLocker lock(&m_mu);
        m_filled.clear(); // buffers that no longer exist
    }
    readOutputPort();
    ok = step("SendCommand(PortEnable, output)", OMX_SendCommand(m_handle, OMX_CommandPortEnable, m_outPort, NULL)) && ok;
    ok = allocateOutput() && ok;
    if (!waitFlag(&m_outEnabled, 2000)) {
        qWarning("video decoder: output port not enabled in 2 s");
        ok = false;
    }
    for (int i = 0; i < m_outHeaders.size(); i++)
        OMX_FillThisBuffer(m_handle, m_outHeaders[i]);
    if (!ok) {
        qWarning("video decoder: reconfiguration failed; the decoder will be reopened");
        QMutexLocker lock(&m_mu);
        m_broken = true;
    }
}
