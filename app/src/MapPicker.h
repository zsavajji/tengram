#ifndef MAPPICKER_H
#define MAPPICKER_H

#include <bb/system/InvokeManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>

class EngineLink;

// The send-location page's state (LocationPage.qml), made by QML in the page's attachedObjects so it dies with the page:
// where the phone is (location.get, docs/protocol.md), the map shown (map.get, a PNG Telegram draws around a point) and the
// point under the pin, moved by dragging the map (Mercator.h), by the page's zoom buttons and by a place found by its
// address (location.search).
// The phone's position comes from GPS satellites alone (BlackBerry's Wi-Fi position server is gone), which takes minutes
// and a view of the sky: indoors there is none (the user, 2026-11-27, on the Q10). So the page never waits for it: the map
// starts at once where the phone last was (location.last, else the centre kept in settings.ini from the last time: map/lat,
// map/lon, map/zoom), the search for the position goes on for as long as the page is open (location.get again after each
// "no fix in 30 s"), and the first fix moves the map to it unless the user has moved the map since (follow).
// Sending goes through the engine's request(), so a refusal reaches engine.failure and the chat page's toast.
class MapPicker : public QObject
{
    Q_OBJECT
    // The EngineLink, set as ChatController's link is.
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    // The map PNG shown now, "" for none yet. Only the latest map.get's answer is shown.
    Q_PROPERTY(QString picture READ picture NOTIFY pictureChanged)
    // A position, a map or a place search is on its way.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // The phone's position is being looked for; slow once it has taken 30 s.
    Q_PROPERTY(bool searching READ searching NOTIFY changed)
    Q_PROPERTY(bool slow READ slow NOTIFY changed)
    // The phone's position is known.
    Q_PROPERTY(bool located READ located NOTIFY changed)
    // The centre is the phone's own position: nothing has moved the map since the fix.
    Q_PROPERTY(bool mine READ mine NOTIFY changed)
    // "" for none, "location_off" (Location Services off or not allowed), "no_fix" (the position can't be asked for: the
    // engine is away) or "map" (the latest map didn't come). An error leaves whatever map shows.
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    // The fix's accuracy as the page shows it ("25 m"), "" when unknown.
    Q_PROPERTY(QString accuracyText READ accuracyText NOTIFY changed)
    Q_PROPERTY(int zoom READ zoom NOTIFY changed)
    // What find() found: maps of title (the place's first name), detail (the rest of its address), lat and lon; empty before
    // a search, after goTo() and when nothing was found. placesNote says why it is empty after a search: "" (not searched,
    // or found), "none" (no such place), "offline" (no answer) or "failed".
    Q_PROPERTY(QVariantList places READ places NOTIFY placesChanged)
    Q_PROPERTY(QString placesNote READ placesNote NOTIFY placesChanged)
public:
    explicit MapPicker(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    QString picture() const { return m_picture; }
    bool busy() const { return !m_locateId.isEmpty() || !m_mapId.isEmpty() || !m_findId.isEmpty(); }
    bool searching() const { return !m_locateId.isEmpty(); }
    bool slow() const { return m_slow; }
    bool located() const { return m_located; }
    bool mine() const { return m_mine; }
    QString problem() const { return m_problem; }
    QString accuracyText() const { return m_accuracyText; }
    int zoom() const { return m_zoom; }
    QVariantList places() const { return m_places; }
    QString placesNote() const { return m_placesNote; }

    // The page opened for chat (a chat's jid, given to map.get): the map of where the phone last was at once, and the
    // phone's position asked for.
    Q_INVOKABLE void start(const QString &chat);
    // Asks where the phone is again; the answer recentres the map on it (the page's "where am I").
    Q_INVOKABLE void locate();
    // The picture was dragged by that share of its drawn side, positive to the right and down: the centre moves the other
    // way, as many of the map's pixels, and is no longer the phone's position. The new centre's map is asked for.
    Q_INVOKABLE void moved(qreal dx, qreal dy);
    // Asks for the centre's map again: the page's Try again after a map that didn't come (problem "map").
    Q_INVOKABLE void reload();
    // One zoom step in (1) or out (-1), within 13 to 18.
    Q_INVOKABLE void zoomBy(int step);
    // Looks for an address or a place's name (location.search): places and placesNote answer.
    Q_INVOKABLE void find(const QString &text);
    // The map goes to places[index], and the list is emptied.
    Q_INVOKABLE void goTo(int index);
    // Empties the list (the search field was cleared).
    Q_INVOKABLE void clearPlaces();
    // location.send of the phone's position with its accuracy while mine, else of the point under the pin with none; topic
    // when above 0, thread and quote (a message id) when not "". Whether the request went out.
    Q_INVOKABLE bool sendPoint(const QString &chat, int topic, const QString &thread, const QString &quote);
    // location.live of the phone's position (never a dragged point) for period seconds, 0 until it is turned off. False when
    // the position isn't known or the request didn't go out.
    Q_INVOKABLE bool sendLive(const QString &chat, int topic, const QString &thread, const QString &quote, int period);
    // The phone's Location settings, where Location Services are turned on.
    Q_INVOKABLE void openSettings();

signals:
    void pictureChanged();
    void changed();
    void placesChanged();

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onFailed(const QString &id, const QString &cmd, const QString &code);
    void onFullscreen();

private:
    void askMap();
    void askPosition();
    void centre(double lat, double lon);
    void keep() const;
    void setPlaces(const QVariantList &places, const QString &note);

    QPointer<EngineLink> m_engine;
    bb::system::InvokeManager m_invoker;
    QString m_chat;
    QString m_picture;
    QString m_problem;
    QString m_accuracyText;
    QString m_lastId; // the location.last waited for, "" for none
    QString m_locateId; // the location.get waited for, "" for none
    QString m_mapId; // the latest map.get, "" when none is out: an older one's answer is not shown
    QString m_findId; // the latest location.search, "" when none is out
    QVariantList m_places;
    QString m_placesNote;
    double m_lat, m_lon; // the centre: the point under the pin
    double m_fixLat, m_fixLon; // the phone's position, when located
    int m_accuracy; // its metres, 0 unknown
    int m_zoom;
    bool m_centred; // there is a centre (m_lat, m_lon mean something)
    bool m_located;
    bool m_mine;
    bool m_follow; // the map goes where the phone is found to be: until the user moves it, and again after "where am I"
    bool m_slow; // the position has been looked for longer than one location.get's 30 s
};

#endif
