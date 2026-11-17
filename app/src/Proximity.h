#ifndef PROXIMITY_H
#define PROXIMITY_H

#include <bb/AbstractBpsEventHandler>
#include <QObject>
#include <QSet>

// The proximity sensor, for the call screen: when the phone is at the ear the screen goes black
// and takes no touch. On while someone needs it: acquire() and release() count, the sensor's events
// are requested on the first acquire and stopped on the last release. The screen also stays awake
// while held, because an idle lock during a call would minimise the app and drop the audio (the
// same reason as while recording, step 4.4b). Lives on the UI thread, where BPS delivers.
class Proximity : public QObject, public bb::AbstractBpsEventHandler
{
    Q_OBJECT
    // True while an object is close to the sensor; false when released or the sensor is far.
    Q_PROPERTY(bool near READ isNear NOTIFY nearChanged)
public:
    explicit Proximity(QObject *parent = 0);
    ~Proximity();

    bool isNear() const { return m_near; }
    // By holder, the object that needs the sensor: a holder destroyed while it holds is released
    // by itself (a voice note's player goes with its chat page, and Cascades QML has no
    // Component.onDestruction to release it there). Twice by one holder counts once. No default
    // argument: moc's per-arity clones are a known Qt 4 trap (pitfalls.md).
    Q_INVOKABLE void acquire(QObject *holder);
    Q_INVOKABLE void release(QObject *holder);
    // The ear: an audio_manager handle routed to the handset, made on first use and kept for the
    // app's run (a MediaPlayer's audioManagerHandle; 0 if audio_manager refused).
    Q_INVOKABLE unsigned int earHandle();

    void event(bps_event_t *event);

signals:
    void nearChanged();

private slots:
    void holderGone(QObject *holder);

private:
    void setNear(bool on);
    void drop();

    int m_users;
    QSet<QObject *> m_holders;
    bool m_near;
    unsigned int m_ear;
};

#endif
