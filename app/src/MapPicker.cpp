#include "MapPicker.h"

#include "EngineLink.h"
#include "Mercator.h"
#include "MessageModel.h"

#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTargetReply>
#include <bb/system/LocaleHandler>
#include <QDir>
#include <QSettings>

using namespace bb::system;

// The map asked for, in logical pixels (map.get's width and height): the page shows it square, filling its width.
// ponytail: the map is a 720 px PNG, drawn 1:1 on the 720 px phones and stretched on the Passport's 1440; ask 720 logical px
// there if it looks soft (Telegram refuses map files over 1 MB).
static const int kSide = 360;
static const int kZoomMin = 13;
static const int kZoomMax = 18;
static const int kZoomStart = 16;

static QString settingsFile()
{
    return QDir::homePath() + "/settings.ini";
}

MapPicker::MapPicker(QObject *parent)
    : QObject(parent)
    , m_lat(0)
    , m_lon(0)
    , m_fixLat(0)
    , m_fixLon(0)
    , m_accuracy(0)
    , m_zoom(kZoomStart)
    , m_centred(false)
    , m_located(false)
    , m_mine(false)
    , m_follow(true)
    , m_slow(false)
{
}

QObject *MapPicker::link() const
{
    return m_engine;
}

// A QPointer, as ChatController's: at app exit the page may outlive the EngineLink.
void MapPicker::setLink(QObject *link)
{
    m_engine = qobject_cast<EngineLink *>(link);
    if (m_engine) {
        connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
                SLOT(onReplied(QString, QString, QVariant)));
        connect(m_engine, SIGNAL(failed(QString, QString, QString)), this, SLOT(onFailed(QString, QString, QString)));
        connect(m_engine, SIGNAL(fullscreenChanged()), this, SLOT(onFullscreen()));
    }
}

// Back in front with Location Services still said to be off: the user may have just turned them on in the Settings the note's
// button opened, so the position is asked for again without another tap.
void MapPicker::onFullscreen()
{
    if (m_engine && m_engine->fullscreen() && m_problem == "location_off" && m_locateId.isEmpty())
        askPosition();
}

// The map's centre from now on, and its map.
void MapPicker::centre(double lat, double lon)
{
    m_lat = lat;
    m_lon = lon;
    m_centred = true;
    askMap();
}

// The centre is kept for the next opening on a phone that doesn't know where it last was.
void MapPicker::keep() const
{
    QSettings file(settingsFile(), QSettings::IniFormat);
    file.setValue("map/lat", m_lat);
    file.setValue("map/lon", m_lon);
    file.setValue("map/zoom", m_zoom);
}

void MapPicker::start(const QString &chat)
{
    m_chat = chat;
    m_follow = true;
    if (!m_engine)
        return;
    // Before the position's request: the engine's position thread takes them in this order.
    m_lastId = m_engine->requestQuiet("location.last", QVariantMap());
    askPosition();
}

void MapPicker::locate()
{
    m_follow = true;
    // The fix we have is shown at once; a newer one moves the map again.
    if (m_located) {
        m_mine = true;
        centre(m_fixLat, m_fixLon);
    }
    askPosition();
}

// location.get, unless one is out. The problems with the position go until the answer says otherwise.
void MapPicker::askPosition()
{
    if (!m_engine || !m_locateId.isEmpty())
        return;
    if (m_problem != "map")
        m_problem.clear();
    m_locateId = m_engine->requestQuiet("location.get", QVariantMap());
    if (m_locateId.isEmpty())
        m_problem = "no_fix"; // the engine is away
    emit changed();
}

void MapPicker::moved(qreal dx, qreal dy)
{
    if (!m_centred)
        return;
    mercatorPan(m_lat, m_lon, m_zoom, -dx * kSide, -dy * kSide);
    m_mine = false;
    m_follow = false;
    askMap();
}

void MapPicker::reload()
{
    askMap();
}

void MapPicker::zoomBy(int step)
{
    const int zoom = qBound(kZoomMin, m_zoom + step, kZoomMax);
    if (zoom == m_zoom || !m_centred)
        return;
    m_zoom = zoom;
    askMap();
}

// The newest request replaces the one waited for: an answer to an older one is not shown.
void MapPicker::askMap()
{
    if (!m_engine || !m_centred)
        return;
    QVariantMap args;
    args["lat"] = m_lat;
    args["lon"] = m_lon;
    args["zoom"] = m_zoom;
    args["width"] = kSide;
    args["height"] = kSide;
    if (!m_chat.isEmpty())
        args["chat"] = m_chat;
    m_mapId = m_engine->requestQuiet("map.get", args);
    if (m_mapId.isEmpty() && m_problem.isEmpty())
        m_problem = "map";
    emit changed();
}

void MapPicker::setPlaces(const QVariantList &places, const QString &note)
{
    m_places = places;
    m_placesNote = note;
    emit placesChanged();
}

void MapPicker::find(const QString &text)
{
    const QString query = text.simplified();
    if (!m_engine || query.isEmpty())
        return;
    QVariantMap args;
    args["query"] = query;
    m_findId = m_engine->requestQuiet("location.search", args);
    setPlaces(QVariantList(), m_findId.isEmpty() ? "offline" : "");
    emit changed();
}

void MapPicker::goTo(int index)
{
    if (index < 0 || index >= m_places.size())
        return;
    const QVariantMap place = m_places.at(index).toMap();
    m_mine = false;
    m_follow = false;
    m_zoom = kZoomStart;
    setPlaces(QVariantList(), QString());
    centre(place.value("lat").toDouble(), place.value("lon").toDouble());
}

void MapPicker::clearPlaces()
{
    m_findId.clear();
    setPlaces(QVariantList(), QString());
    emit changed();
}

void MapPicker::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    Q_UNUSED(cmd);
    if (id.isEmpty())
        return;
    const QVariantMap r = result.toMap();
    if (id == m_lastId) {
        // Where the phone last was: the map's start, unless the phone has been found or the map moved meanwhile.
        m_lastId.clear();
        if (m_follow && !m_located)
            centre(r.value("lat").toDouble(), r.value("lon").toDouble());
    } else if (id == m_locateId) {
        // The phone's position. The map goes to it while it follows; a map the user has moved stays where it is.
        m_locateId.clear();
        m_fixLat = r.value("lat").toDouble();
        m_fixLon = r.value("lon").toDouble();
        m_accuracy = qRound(r.value("accuracy").toDouble());
        m_accuracyText = m_accuracy > 0 ? MessageModel::distance(m_accuracy, LocaleHandler(LocaleType::Region).locale())
                                        : QString();
        m_located = true;
        m_slow = false;
        if (m_problem != "map")
            m_problem.clear();
        if (m_follow) {
            m_mine = true;
            centre(m_fixLat, m_fixLon); // emits changed
            keep();
        } else {
            emit changed();
        }
    } else if (id == m_mapId) {
        m_mapId.clear();
        m_picture = r.value("path").toString();
        if (m_problem == "map")
            m_problem.clear();
        emit pictureChanged();
        emit changed();
    } else if (id == m_findId) {
        m_findId.clear();
        // "Via Roma, Padova, Veneto, Italia": the first name over the rest.
        QVariantList places;
        foreach (const QVariant &v, r.value("places").toList()) {
            QVariantMap place = v.toMap();
            const QString name = place.value("name").toString();
            place["title"] = name.section(", ", 0, 0);
            place["detail"] = name.section(", ", 1);
            places << place;
        }
        setPlaces(places, places.isEmpty() ? "none" : "");
        emit changed();
    }
}

void MapPicker::onFailed(const QString &id, const QString &cmd, const QString &code)
{
    Q_UNUSED(cmd);
    if (id.isEmpty())
        return;
    if (id == m_lastId) {
        // The phone knows no last position: the centre kept from the last time this page had one, if any.
        m_lastId.clear();
        const QSettings file(settingsFile(), QSettings::IniFormat);
        if (m_follow && !m_located && !m_centred && file.contains("map/lat")) {
            m_zoom = qBound(kZoomMin, file.value("map/zoom", m_zoom).toInt(), kZoomMax);
            centre(file.value("map/lat").toDouble(), file.value("map/lon").toDouble());
        }
    } else if (id == m_locateId) {
        m_locateId.clear();
        if (code == "no_fix") {
            // No position in 30 s, which is the rule indoors: the GPS goes on looking (the engine leaves its request out)
            // and so does the page, saying that it takes long.
            m_slow = true;
            askPosition(); // emits changed
            return;
        }
        // Off or not allowed asks for the settings; anything else (the engine gone) for another try.
        m_problem = code == "location_off" ? "location_off" : "no_fix";
        emit changed();
    } else if (id == m_mapId) {
        m_mapId.clear();
        if (m_problem.isEmpty())
            m_problem = "map";
        emit changed();
    } else if (id == m_findId) {
        m_findId.clear();
        setPlaces(QVariantList(), code == "offline" || code == "no_engine" ? "offline" : "failed");
        emit changed();
    }
}

// Where the message goes in the chat, as the chat page's own sends add it.
static void addPlace(QVariantMap &args, int topic, const QString &thread, const QString &quote)
{
    if (topic > 0)
        args["topic"] = topic;
    if (!thread.isEmpty())
        args["thread"] = thread;
    if (!quote.isEmpty())
        args["quote"] = quote;
}

bool MapPicker::sendPoint(const QString &chat, int topic, const QString &thread, const QString &quote)
{
    if (!m_engine || !m_centred)
        return false;
    // While mine the centre is the fix itself (anything that moves the map ends mine).
    QVariantMap args;
    args["chat"] = chat;
    args["lat"] = m_lat;
    args["lon"] = m_lon;
    if (m_mine && m_accuracy > 0)
        args["accuracy"] = m_accuracy;
    addPlace(args, topic, thread, quote);
    if (m_engine->request("location.send", args).isEmpty())
        return false;
    keep();
    return true;
}

bool MapPicker::sendLive(const QString &chat, int topic, const QString &thread, const QString &quote, int period)
{
    if (!m_engine || !m_located)
        return false;
    QVariantMap args;
    args["chat"] = chat;
    args["lat"] = m_fixLat;
    args["lon"] = m_fixLon;
    args["period"] = period;
    if (m_accuracy > 0)
        args["accuracy"] = m_accuracy;
    addPlace(args, topic, thread, quote);
    return !m_engine->request("location.live", args).isEmpty();
}

// The Settings app at its Location Services page. Its own manifest on the phone (sys.settings.target, read on the Q10,
// 2026-11-27) takes bb.action.OPEN with the type settings/view, and the uri names the page.
void MapPicker::openSettings()
{
    InvokeRequest request;
    request.setTarget("sys.settings.target");
    request.setAction("bb.action.OPEN");
    request.setMimeType("settings/view");
    request.setUri("settings://location");
    InvokeTargetReply *reply = m_invoker.invoke(request);
    if (reply)
        connect(reply, SIGNAL(finished()), reply, SLOT(deleteLater()));
}
