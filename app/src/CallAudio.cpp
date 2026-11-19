#include "CallAudio.h"

#include "EngineLink.h"
#include "LinkIo.h"

#include <audio/audio_manager_device.h>
#include <audio/audio_manager_routing.h>
#include <sys/asoundlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <QDebug>
#include <QMetaObject>

// How the call routes. AUDIO_TYPE_VOICE sends the sound to the earpiece and the microphone to
// the voice path; if the Q10 echoes on the loudspeaker, AUDIO_TYPE_VIDEO_CHAT is the one to try
// instead (docs/protocol.md, "Calls"). The only place the type is named.
static const audio_manager_audio_type_t CallAudioType = AUDIO_TYPE_VOICE;

// The audio link's frame: 960 samples of 16 kHz mono s16le, 60 ms (the contract's framing).
static const int FrameBytes = 1920;
static const int SampleRate = 16000;
// The fragment asked for; the device grants its own (1024 bytes on the Q10, 2026-11-07), and all
// reads and writes go a granted fragment at a time, as block mode wants. Up to 8 queued (256 ms)
// let a burst of late packets in without blocking; what plays ahead is kept to two frames below.
static const int FragAsk = 1024;
static const int FragsMax = 8;
// Two frames (120 ms) are in before playback starts, and again after it ran dry; more than four
// waiting and the oldest go, so the delay can't grow through a call.
static const int Prime = 2 * FrameBytes;
static const int Keep = 4 * FrameBytes;
// A PCM that fails this many reads/writes in a row is given up on, ending that side's audio.
static const int MaxErrors = 50;

CallAudio::CallAudio(EngineLink *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_play(this, false)
    , m_cap(this, true)
    , m_fd(-1)
    , m_capHandle(0)
    , m_playHandle(0)
    , m_muted(false)
    , m_speaker(false)
    , m_running(false)
{
}

CallAudio::~CallAudio()
{
    stop();
}

void CallAudio::Worker::run()
{
    if (m_capture)
        m_owner->captureLoop();
    else
        m_owner->session();
}

void CallAudio::setMuted(bool on)
{
    if (m_muted == on)
        return;
    m_muted = on;
    emit mutedChanged();
}

void CallAudio::setSpeaker(bool on)
{
    if (m_speaker == on)
        return;
    m_speaker = on;
    {
        QMutexLocker lock(&m_route);
        applyRoute(m_capHandle);
        applyRoute(m_playHandle);
    }
    emit speakerChanged();
}

// Call with m_route held. Voice defaults to the handset; the loudspeaker is a preference on top.
void CallAudio::applyRoute(unsigned int handle)
{
    if (!handle)
        return;
    const int rc = audio_manager_set_handle_type(handle, CallAudioType,
                                                 m_speaker ? AUDIO_DEVICE_SPEAKER : AUDIO_DEVICE_DEFAULT,
                                                 AUDIO_DEVICE_DEFAULT);
    if (rc != 0)
        qWarning("call audio: route failed, rc %d", rc);
}

void CallAudio::setRunning(bool on)
{
    if (m_running == on)
        return;
    m_running = on;
    emit runningChanged();
}

void CallAudio::start(const QString &callId)
{
    stop();
    signal(SIGPIPE, SIG_IGN); // a send to a closed link must fail, not kill the app
    m_callId = callId;
    m_stopping = 0;
    m_play.start();
}

void CallAudio::stop()
{
    m_stopping = 1;
    // The socket's shutdown wakes whichever thread waits on it; the PCM calls return within a frame.
    {
        QMutexLocker lock(&m_fdLock);
        if (m_fd >= 0)
            ::shutdown(m_fd, SHUT_RDWR);
    }
    m_play.wait(); // session() joins the capture thread itself
    m_cap.wait();
    setRunning(false);
}

void CallAudio::closeLink()
{
    QMutexLocker lock(&m_fdLock);
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
}

// Connects, says hello and reads the reply. m_fd is set from the first moment so stop() can shut
// it down while we wait (the reply has a 3 s timeout besides).
bool CallAudio::openLink()
{
    const int port = m_engine->port();
    if (port <= 0) {
        qWarning("call audio: the engine's port is unknown");
        return false;
    }
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        qWarning("call audio: socket failed, errno %d", errno);
        return false;
    }
    {
        QMutexLocker lock(&m_fdLock);
        m_fd = fd;
    }
    if (m_stopping)
        return false;
    if (!LinkIo::connectLoopback(fd, port)) {
        qWarning("call audio: connect failed, errno %d", errno);
        return false;
    }
    return LinkIo::hello(fd, m_engine->token(), "a", "audio", m_callId, "call audio");
}

bool CallAudio::openPcm(bool capture, void **pcm, unsigned int *handle, int *frag)
{
    const char *dir = capture ? "capture" : "playback";
    // "voice" is the audio path named in the SDK's audio type names (/dev/snd/voicec and voicep on
    // the Q10); "pcmPreferred", the system's default device, in case the voice path refuses an app.
    // Unverified on the phone (2026-11-07): the log says which one opened.
    char names[2][16] = { "voice", "pcmPreferred" };
    snd_pcm_t *h = 0;
    unsigned int am = 0;
    int rc = -1;
    for (int i = 0; i < 2 && rc != 0; i++) {
        rc = audio_manager_snd_pcm_open_name(CallAudioType, &h, &am, names[i],
                                             capture ? SND_PCM_OPEN_CAPTURE : SND_PCM_OPEN_PLAYBACK);
        if (rc != 0)
            qWarning("call audio: %s open of %s failed, rc %d", dir, names[i], rc);
        else
            qDebug("call audio: %s opened %s", dir, names[i]);
    }
    if (rc != 0)
        return false;
    const int channel = capture ? SND_PCM_CHANNEL_CAPTURE : SND_PCM_CHANNEL_PLAYBACK;
    snd_pcm_channel_params_t params;
    memset(&params, 0, sizeof params);
    params.channel = channel;
    params.mode = SND_PCM_MODE_BLOCK;
    params.start_mode = SND_PCM_START_DATA; // playback starts at the first write, after we've two frames
    params.stop_mode = SND_PCM_STOP_STOP;   // an underrun stops it; session() prepares it again
    params.format.interleave = 1;
    params.format.format = SND_PCM_SFMT_S16_LE;
    params.format.rate = SampleRate;
    params.format.voices = 1;
    params.buf.block.frag_size = FragAsk;
    params.buf.block.frags_min = 1;
    params.buf.block.frags_max = FragsMax;
    rc = snd_pcm_plugin_params(h, &params);
    if (rc != 0) {
        qWarning("call audio: %s params failed, rc %d, why %u", dir, rc, params.why_failed);
    } else {
        snd_pcm_channel_setup_t setup;
        memset(&setup, 0, sizeof setup);
        setup.channel = channel;
        rc = snd_pcm_plugin_setup(h, &setup);
        if (rc != 0)
            qWarning("call audio: %s setup failed, rc %d", dir, rc);
        else
            qDebug("call audio: %s open, %d Hz, %d voice, fragment %d bytes x %d", dir, setup.format.rate,
                   setup.format.voices, setup.buf.block.frag_size, setup.buf.block.frags);
        *frag = rc == 0 && setup.buf.block.frag_size > 0 ? setup.buf.block.frag_size : FragAsk;
    }
    if (rc == 0) {
        rc = snd_pcm_plugin_prepare(h, channel);
        if (rc != 0)
            qWarning("call audio: %s prepare failed, rc %d", dir, rc);
    }
    if (rc != 0) {
        snd_pcm_close(h);
        audio_manager_free_handle(am);
        return false;
    }
    {
        QMutexLocker lock(&m_route);
        *pcm = h;
        *handle = am;
        applyRoute(am);
    }
    return true;
}

void CallAudio::closePcm(bool capture, void **pcm, unsigned int *handle)
{
    Q_UNUSED(capture);
    QMutexLocker lock(&m_route); // setSpeaker() never sees a handle that's being freed
    if (*pcm)
        snd_pcm_close(static_cast<snd_pcm_t *>(*pcm));
    if (*handle)
        audio_manager_free_handle(*handle);
    *pcm = 0;
    *handle = 0;
}

// Brings a stopped channel (underrun, overrun, device change) back. Returns false if it can't.
// The status it found goes into *what for the log (SND_PCM_STATUS_*).
static bool recover(snd_pcm_t *pcm, int channel, int *what)
{
    snd_pcm_channel_status_t status;
    memset(&status, 0, sizeof status);
    status.channel = channel;
    snd_pcm_plugin_status(pcm, &status);
    *what = status.status;
    return snd_pcm_plugin_prepare(pcm, channel) == 0;
}

// The capture thread: the microphone to the engine. The device gives fragments (1024 bytes on the
// Q10), gathered here into the link's 60 ms frames; a short read keeps what it got.
void CallAudio::captureLoop()
{
    void *pcm = 0;
    int frag = FragAsk;
    // Without a microphone the call still plays the other side (the engine sends silence for us),
    // which tells the user more than a call that ends by itself; the log says why.
    if (!openPcm(true, &pcm, &m_capHandle, &frag))
        return;
    snd_pcm_t *h = static_cast<snd_pcm_t *>(pcm);
    QByteArray chunk(frag, 0), acc;
    int errors = 0, shorts = 0, overruns = 0;
    while (!m_stopping) {
        const ssize_t n = snd_pcm_plugin_read(h, chunk.data(), frag);
        if (n > 0)
            acc.append(chunk.constData(), int(n));
        if (n != frag) {
            if (m_stopping)
                break;
            int what = 0;
            const bool ok = recover(h, SND_PCM_CHANNEL_CAPTURE, &what);
            if (n > 0)
                shorts++;
            else
                overruns++;
            if (errors++ == 0)
                qWarning("call audio: capture read gave %d of %d, status %d, errno %d", int(n), frag, what, errno);
            if (errors > MaxErrors || !ok)
                break;
            continue;
        }
        errors = 0;
        while (acc.size() >= FrameBytes) {
            if (m_muted)
                memset(acc.data(), 0, FrameBytes);
            if (!LinkIo::sendAll(m_fd, acc.constData(), FrameBytes))
                goto done; // the link is gone: the playback side sees it too
            acc.remove(0, FrameBytes);
        }
    }
done:
    if (shorts || overruns)
        qDebug("call audio: capture had %d short reads and %d failed ones", shorts, overruns);
    closePcm(true, &pcm, &m_capHandle);
}

// The playback thread: opens the link, starts the capture thread, then the engine to the speaker,
// a fragment at a time from what has come in.
void CallAudio::session()
{
    const bool linked = openLink();
    if (!linked) {
        closeLink();
        QMetaObject::invokeMethod(this, "setRunning", Qt::QueuedConnection, Q_ARG(bool, false));
        return;
    }
    QMetaObject::invokeMethod(this, "setRunning", Qt::QueuedConnection, Q_ARG(bool, true));
    m_cap.start();
    void *pcm = 0;
    int frag = FragAsk;
    if (openPcm(false, &pcm, &m_playHandle, &frag)) {
        snd_pcm_t *h = static_cast<snd_pcm_t *>(pcm);
        char frame[FrameBytes];
        QByteArray acc;
        bool primed = false;
        int errors = 0, underruns = 0, dropped = 0;
        while (!m_stopping && LinkIo::recvAll(m_fd, frame, FrameBytes)) { // false: the engine closed it, or stop()
            acc.append(frame, FrameBytes);
            if (acc.size() > Keep) {
                dropped += acc.size() - Prime;
                acc.remove(0, acc.size() - Prime);
            }
            if (!primed && acc.size() < Prime)
                continue;
            primed = true;
            while (acc.size() >= frag && !m_stopping) {
                const ssize_t n = snd_pcm_plugin_write(h, acc.constData(), frag);
                if (n == frag) {
                    acc.remove(0, frag);
                    errors = 0;
                    continue;
                }
                // Ran dry, or the device changed: start again once two frames are in, keeping
                // what hasn't played.
                int what = 0;
                const bool ok = recover(h, SND_PCM_CHANNEL_PLAYBACK, &what);
                underruns++;
                if (errors++ == 0)
                    qWarning("call audio: playback write gave %d of %d, status %d, errno %d", int(n), frag, what, errno);
                if (errors > MaxErrors || !ok)
                    goto done;
                primed = false;
                break;
            }
        }
done:
        qDebug("call audio: playback ran dry %d times; %d bytes dropped to keep the delay down", underruns, dropped);
    }
    m_stopping = 1;
    ::shutdown(m_fd, SHUT_RDWR); // wakes the capture thread's send/read
    m_cap.wait();
    closePcm(false, &pcm, &m_playHandle);
    closeLink();
    QMetaObject::invokeMethod(this, "setRunning", Qt::QueuedConnection, Q_ARG(bool, false));
}
