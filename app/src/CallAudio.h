#ifndef CALLAUDIO_H
#define CALLAUDIO_H

#include <QAtomicInt>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>

class EngineLink;

// The audio of the live call (docs/protocol.md, "Calls"): the microphone and the
// speaker belong to the UI process, the engine keeps the codec. start() opens a second loopback
// connection to the engine, says hello with the call's id, then two threads move raw PCM (16 kHz
// mono s16le, 60 ms frames of 1920 bytes) between that socket and libasound. Nothing here runs or
// blocks on the UI thread except stop(), which joins the threads (at most about one frame).
class CallAudio : public QObject
{
    Q_OBJECT
    // While true the microphone's frames are sent as zeros.
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    // Route to the loudspeaker; false is the earpiece (the handset, voice calls' default).
    Q_PROPERTY(bool speaker READ speaker WRITE setSpeaker NOTIFY speakerChanged)
    // True from the engine's hello reply until both threads have ended.
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // The call the audio belongs to; "" when not running.
    Q_PROPERTY(QString callId READ callId NOTIFY runningChanged)
public:
    explicit CallAudio(EngineLink *engine, QObject *parent = 0);
    ~CallAudio();

    bool muted() const { return m_muted; }
    bool speaker() const { return m_speaker; }
    bool running() const { return m_running; }
    QString callId() const { return m_running ? m_callId : QString(); }
    void setMuted(bool on);
    void setSpeaker(bool on);

    // Replaces any earlier audio. Returns at once: the connection and the devices open on the thread.
    Q_INVOKABLE void start(const QString &callId);
    // Safe to call twice, and when nothing runs.
    Q_INVOKABLE void stop();

signals:
    void mutedChanged();
    void speakerChanged();
    void runningChanged();

private slots:
    void setRunning(bool on);

private:
    // One QThread type for both loops; run() calls back into the owner.
    class Worker : public QThread
    {
    public:
        Worker(CallAudio *owner, bool capture) : m_owner(owner), m_capture(capture) {}
    protected:
        void run();
    private:
        CallAudio *m_owner;
        bool m_capture;
    };
    friend class Worker;

    void session();       // the playback thread: connects, then plays; ends the capture thread too
    void captureLoop();
    bool openLink();
    void closeLink();
    // Opens the PCM of one direction; false after logging why. frag is the fragment size in bytes.
    bool openPcm(bool capture, void **pcm, unsigned int *handle, int *frag);
    void closePcm(bool capture, void **pcm, unsigned int *handle);
    void applyRoute(unsigned int handle);

    EngineLink *m_engine;
    Worker m_play;
    Worker m_cap;
    QString m_callId;
    int m_fd;                 // the audio link; -1 when none
    QAtomicInt m_stopping;
    QMutex m_fdLock;          // so stop()'s shutdown never meets a closed, reused descriptor
    QMutex m_route;           // guards the handles below against setSpeaker() on the UI thread
    unsigned int m_capHandle;
    unsigned int m_playHandle;
    bool m_muted;
    volatile bool m_speaker;
    bool m_running;
};

#endif
