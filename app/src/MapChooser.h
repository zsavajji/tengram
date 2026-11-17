#ifndef MAPCHOOSER_H
#define MAPCHOOSER_H

#include <bb/system/InvokeManager>
#include <bb/system/SystemUiResult>
#include <QObject>
#include <QString>
#include <QStringList>

namespace bb {
namespace system {
class InvokeQueryTargetsReply;
}
}

// Opens a location where the user picks, every time (step 6.5c), made by QML in the chat page's
// attachedObjects. The Q10 has no maps app of its own, so the list is whatever the system finds for
// a geo: link, then Google Maps and OpenStreetMap in the browser.
class MapChooser : public QObject
{
    Q_OBJECT
public:
    explicit MapChooser(QObject *parent = 0);

    // Asks the system which apps open geo:<lat>,<lon>, then lists them with the two web maps in a
    // dialog ("Open with") and opens the place in the one tapped. name is the place's, "" when it
    // has none. A newer choose() replaces an older one that is still asking.
    Q_INVOKABLE void choose(double lat, double lon, const QString &name);

private slots:
    void onTargets();
    void onChosen(bb::system::SystemUiResult::Type result);
    void onInvoked();

private:
    void offer();
    void open(const QString &target, const QString &uri);

    bb::system::InvokeManager m_invoker;
    bb::system::InvokeQueryTargetsReply *m_reply; // the query we wait for, only compared
    QString m_lat, m_lon, m_name; // the place, coordinates as text with a dot
    QStringList m_labels, m_targets; // the apps found, as listed: what the user reads, and the target to invoke
};

#endif
