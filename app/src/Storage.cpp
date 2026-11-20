#include "Storage.h"

#include "EngineLink.h"
#include "Settings.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QLocale>

// Only what ask() sends: a page shows their failure from the link's, and they are asked again on connecting.
static bool isAsk(const QString &cmd)
{
    return cmd == "storage.get" || cmd == "network.get";
}

Storage::Storage(QObject *parent)
    : QObject(parent)
    , m_want(false)
    , m_region(bb::system::LocaleType::Region)
{
}

QObject *Storage::link() const
{
    return m_engine;
}

// A QPointer, as Privacy's: at app exit the page may outlive the EngineLink.
void Storage::setLink(QObject *link)
{
    m_engine = qobject_cast<EngineLink *>(link);
    if (m_engine) {
        connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
                SLOT(onReplied(QString, QString, QVariant)));
        connect(m_engine, SIGNAL(failureChanged()), this, SLOT(onFailure()));
        connect(m_engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
    }
}

// A page opened while the engine (re)starts asks before it's there: the request fails with no_engine and nothing
// would ask again. So it's asked again on connecting.
void Storage::onConnected()
{
    if (m_want && m_engine && m_engine->connected() && m_storage.isEmpty())
        ask();
}

// Every request goes through here: the other pages' objects hear the same replies and failures, so what is ours is
// told by its id. Returns the request's id, "" without an engine.
QString Storage::send(const QString &cmd)
{
    if (!m_engine)
        return QString();
    const QString id = m_engine->request(cmd, QVariantMap());
    if (!id.isEmpty())
        m_sent.insert(id, cmd);
    else if (!isAsk(cmd)) // no engine: the reason is in the link's failure
        emit failed(cmd);
    return id;
}

void Storage::ask()
{
    m_want = true;
    m_storageId = send("storage.get");
    m_networkId = send("network.get");
}

void Storage::clear()
{
    send("storage.clear");
}

void Storage::reset()
{
    send("network.reset");
}

// KB below 1 MB (at least 1; 0 reads "0 KB"), MB with one decimal below 1 GB, then GB with two: 3.45 GB.
QString Storage::bytesText(double bytes) const
{
    const QLocale locale = m_region.locale();
    if (bytes < 1024)
        return QString("0 KB");
    if (bytes < 1024 * 1024)
        return QString("%1 KB").arg(qMax(1, int(bytes / 1024)));
    if (bytes < 1024.0 * 1024 * 1024)
        return QString("%1 MB").arg(locale.toString(bytes / (1024.0 * 1024), 'f', 1));
    return QString("%1 GB").arg(locale.toString(bytes / (1024.0 * 1024 * 1024), 'f', 2));
}

QString Storage::dateText(double secs) const
{
    return m_region.locale().toString(QDateTime::fromTime_t(uint(secs)).date(), QLocale::ShortFormat);
}

void Storage::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    if (m_sent.take(id).isEmpty())
        return;
    if (cmd == "storage.get") {
        if (id != m_storageId)
            return;
        m_storage = result.toMap();
        emit storageChanged();
    } else if (cmd == "network.get") {
        if (id != m_networkId)
            return;
        m_network = result.toMap();
        emit networkChanged();
    } else {
        if (cmd == "storage.clear")
            m_freed = bytesText(result.toMap()["freed"].toDouble());
        emit done(cmd);
        ask();
    }
}

// The failure carries no id: the first of our requests with its cmd is the one that failed.
void Storage::onFailure()
{
    if (!m_engine)
        return;
    const QString cmd = m_engine->failure().value("cmd").toString();
    const QString id = m_sent.key(cmd);
    if (id.isEmpty())
        return;
    m_sent.remove(id);
    emit failed(cmd);
}

void Storage::measureCache()
{
    QFutureWatcher<qint64> *watcher = new QFutureWatcher<qint64>(this);
    connect(watcher, SIGNAL(finished()), this, SLOT(onMeasured()));
    watcher->setFuture(QtConcurrent::run(&Settings::cacheBytes));
}

void Storage::clearCache()
{
    Settings::forgetRound();
    QFutureWatcher<qint64> *watcher = new QFutureWatcher<qint64>(this);
    connect(watcher, SIGNAL(finished()), this, SLOT(onCleared()));
    watcher->setFuture(QtConcurrent::run(&Settings::removeCache));
}

// An empty cache reads "0 KB", not the empty text an unknown size has.
void Storage::onMeasured()
{
    QFutureWatcher<qint64> *watcher = static_cast<QFutureWatcher<qint64> *>(sender());
    watcher->deleteLater();
    const qint64 bytes = watcher->result();
    emit cacheMeasured(bytes > 0 ? bytesText(bytes) : QString("0 KB"));
}

void Storage::onCleared()
{
    QFutureWatcher<qint64> *watcher = static_cast<QFutureWatcher<qint64> *>(sender());
    watcher->deleteLater();
    const qint64 bytes = watcher->result();
    emit cacheCleared(bytes > 0 ? bytesText(bytes) : QString("0 KB"));
}
