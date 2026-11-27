#include "Proximity.h"

#include <bb/cascades/Application>
#include <bb/cascades/Window>
#include <bps/bps.h>
#include <bps/screen_input_guard.h>
#include <bps/sensor.h>
#include <audio/audio_manager_device.h>
#include <audio/audio_manager_routing.h>
#include <errno.h>
#include <unistd.h>
#include <QDebug>

Proximity::Proximity(QObject *parent)
    : QObject(parent)
    , m_users(0)
    , m_near(false)
    , m_ear(0)
{
}

Proximity::~Proximity()
{
    if (!m_guards.isEmpty())
        screen_input_guard_stop_events(0); // which also turns the guard off, whatever its count
    if (m_users > 0) {
        m_users = 1;
        drop();
    }
}

static void keepAwake(bool on)
{
    bb::cascades::Application *app = bb::cascades::Application::instance();
    if (app && app->mainWindow())
        app->mainWindow()->setScreenIdleMode(on ? bb::cascades::ScreenIdleMode::KeepAwake
                                                : bb::cascades::ScreenIdleMode::Normal);
}

void Proximity::acquire(QObject *holder)
{
    if (!holder || m_holders.contains(holder))
        return;
    m_holders.insert(holder);
    connect(holder, SIGNAL(destroyed(QObject *)), this, SLOT(holderGone(QObject *)));
    if (m_users++ > 0)
        return;
    keepAwake(true);
    // Cascades has started BPS already; the sensor domain is delivered to this handler.
    subscribe(sensor_get_domain());
    if (sensor_request_events(SENSOR_TYPE_PROXIMITY) != BPS_SUCCESS)
        qWarning("proximity: sensor refused, errno %d", errno);
}

void Proximity::release(QObject *holder)
{
    if (!holder || !m_holders.remove(holder))
        return;
    disconnect(holder, SIGNAL(destroyed(QObject *)), this, SLOT(holderGone(QObject *)));
    drop();
}

void Proximity::holderGone(QObject *holder)
{
    if (m_holders.remove(holder))
        drop();
}

void Proximity::drop()
{
    if (m_users <= 0 || --m_users > 0)
        return;
    sensor_stop_events(SENSOR_TYPE_PROXIMITY);
    unsubscribe(sensor_get_domain());
    keepAwake(false);
    setNear(false);
}

// The first holder turns the guard on, the last one off. request_events starts the face detection and enable lets it turn
// the screen and the touchscreen off; stop_events ends both (the header: "forcibly disable"). Its status events come to
// event() below, which only logs them: `near`, and with it the sound's way to the ear, stays the proximity sensor's.
void Proximity::guard(QObject *holder, bool on)
{
    if (!holder || on == m_guards.contains(holder))
        return;
    if (on) {
        m_guards.insert(holder);
        connect(holder, SIGNAL(destroyed(QObject *)), this, SLOT(guardGone(QObject *)));
        if (m_guards.size() > 1)
            return;
        subscribe(screen_input_guard_get_domain());
        if (screen_input_guard_request_events(0) != BPS_SUCCESS || screen_input_guard_enable() != BPS_SUCCESS)
            qWarning("proximity: screen guard refused, errno %d", errno);
        return;
    }
    disconnect(holder, SIGNAL(destroyed(QObject *)), this, SLOT(guardGone(QObject *)));
    m_guards.remove(holder);
    guardGone(0);
}

// A holder left (0), or was destroyed while it asked: with none left the guard goes off.
void Proximity::guardGone(QObject *holder)
{
    if (holder && !m_guards.remove(holder))
        return;
    if (!m_guards.isEmpty())
        return;
    screen_input_guard_disable();
    screen_input_guard_stop_events(0);
    unsubscribe(screen_input_guard_get_domain());
}

unsigned int Proximity::earHandle()
{
    if (m_ear)
        return m_ear;
    unsigned int h = 0;
    int rc = audio_manager_get_handle(AUDIO_TYPE_VOICE, getpid(), false, &h);
    if (rc == 0)
        rc = audio_manager_set_handle_type(h, AUDIO_TYPE_VOICE, AUDIO_DEVICE_HANDSET, AUDIO_DEVICE_DEFAULT);
    if (rc != 0) {
        qWarning("proximity: ear handle failed, rc %d", rc);
        if (h)
            audio_manager_free_handle(h);
        return 0;
    }
    return m_ear = h;
}

void Proximity::event(bps_event_t *event)
{
    if (bps_event_get_domain(event) == screen_input_guard_get_domain()) {
        if (bps_event_get_code(event) == SCREEN_INPUT_GUARD_EVENT_STATUS)
            qDebug("proximity: screen guard %s",
                   screen_input_guard_event_get_status(event) == SCREEN_INPUT_GUARD_STATUS_ACTIVATED ? "on: the screen is off"
                                                                                                   : "off");
        return;
    }
    if (bps_event_get_domain(event) != sensor_get_domain()
        || bps_event_get_code(event) != SENSOR_PROXIMITY_READING)
        return;
    // 0.0 is close, 1.0 far.
    setNear(sensor_event_get_proximity(event) < 0.5f);
}

void Proximity::setNear(bool on)
{
    if (m_near == on)
        return;
    m_near = on;
    emit nearChanged();
}
