#include "MapChooser.h"

#include <bb/system/InvokeAction>
#include <bb/system/InvokeQueryTargetsReply>
#include <bb/system/InvokeQueryTargetsRequest>
#include <bb/system/InvokeReplyError>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTarget>
#include <bb/system/InvokeTargetReply>
#include <bb/system/SystemListDialog>
#include <bb/system/SystemUiButton>
#include <QDebug>
#include <QUrl>

using namespace bb::system;

MapChooser::MapChooser(QObject *parent)
    : QObject(parent)
    , m_reply(0)
{
}

// QString::number formats in the C locale: a dot, never the Italian comma.
static QString degrees(double value)
{
    return QString::number(value, 'f', 6);
}

void MapChooser::choose(double lat, double lon, const QString &name)
{
    m_lat = degrees(lat);
    m_lon = degrees(lon);
    m_name = name;
    m_labels.clear();
    m_targets.clear();
    InvokeQueryTargetsRequest request;
    request.setAction("bb.action.OPEN");
    request.setUri(QString("geo:%1,%2").arg(m_lat, m_lon));
    // No Self in the types: the system leaves Tengram out. Services and headless apps have no screen.
    request.setTargetTypes(InvokeTarget::Application | InvokeTarget::Card);
    m_reply = m_invoker.queryTargets(request);
    if (m_reply)
        connect(m_reply, SIGNAL(finished()), this, SLOT(onTargets()));
    else
        offer(); // the web maps alone
}

// The apps the system found, each once, then the dialog. A failed query offers the web maps alone.
void MapChooser::onTargets()
{
    InvokeQueryTargetsReply *reply = qobject_cast<InvokeQueryTargetsReply *>(sender());
    if (!reply)
        return;
    reply->deleteLater();
    if (reply != m_reply)
        return; // a newer choose() asked again
    m_reply = 0;
    if (reply->error() != InvokeReplyError::None) {
        qWarning() << "map: target query failed, error" << reply->error();
    } else {
        foreach (const InvokeAction &action, reply->actions()) {
            foreach (const InvokeTarget &target, action.targets()) {
                // An app can answer as an application and as a card, under one label.
                const QString label = target.label().isEmpty() ? target.name() : target.label();
                if (target.name().isEmpty() || m_targets.contains(target.name()) || m_labels.contains(label))
                    continue;
                m_labels << label;
                m_targets << target.name();
            }
        }
    }
    offer();
}

// A tap on an item chooses (dismissOnSelection), so the confirm button goes: no label, no button.
void MapChooser::offer()
{
    SystemListDialog *dialog = new SystemListDialog(this);
    dialog->setTitle(tr("Open with"));
    dialog->confirmButton()->setLabel(QString());
    dialog->cancelButton()->setLabel(tr("Cancel"));
    dialog->setDismissOnSelection(true);
    QStringList items = m_labels;
    items << "Google Maps" << "OpenStreetMap";
    dialog->appendItems(items);
    connect(dialog, SIGNAL(finished(bb::system::SystemUiResult::Type)), this,
            SLOT(onChosen(bb::system::SystemUiResult::Type)));
    dialog->show();
}

void MapChooser::onChosen(bb::system::SystemUiResult::Type result)
{
    SystemListDialog *dialog = qobject_cast<SystemListDialog *>(sender());
    if (!dialog)
        return;
    dialog->deleteLater();
    // A tap on an item ends as ItemSelection; Cancel and a dismissal leave the list alone. No header or
    // separator is appended, so an index is the item's place.
    const QList<int> picked = dialog->selectedIndices();
    if ((result != SystemUiResult::ItemSelection && result != SystemUiResult::ConfirmButtonSelection)
        || picked.isEmpty())
        return;

    const int i = picked.first();
    const QString at = m_lat + "," + m_lon;
    if (i < m_targets.size()) {
        const QString place = m_name.isEmpty() ? QString()
                                               : "(" + QString::fromLatin1(QUrl::toPercentEncoding(m_name)) + ")";
        open(m_targets[i], "geo:" + at + "?q=" + at + place);
    } else if (i == m_targets.size()) {
        // The browser is named: the maps app may claim Google Maps links too.
        open("sys.browser", "https://www.google.com/maps/search/?api=1&query=" + at);
    } else {
        open("sys.browser", "https://www.openstreetmap.org/?mlat=" + m_lat + "&mlon=" + m_lon
                                + "#map=17/" + m_lat + "/" + m_lon);
    }
}

void MapChooser::open(const QString &target, const QString &uri)
{
    qDebug() << "map: OPEN" << target << uri;
    InvokeRequest request;
    request.setAction("bb.action.OPEN");
    request.setTarget(target);
    request.setUri(uri);
    InvokeTargetReply *reply = m_invoker.invoke(request);
    if (reply)
        connect(reply, SIGNAL(finished()), this, SLOT(onInvoked()));
}

void MapChooser::onInvoked()
{
    InvokeTargetReply *reply = qobject_cast<InvokeTargetReply *>(sender());
    if (!reply)
        return;
    if (reply->error() != InvokeReplyError::None)
        qWarning() << "map: OPEN failed, error" << reply->error();
    reply->deleteLater();
}
