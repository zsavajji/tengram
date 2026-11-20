#ifndef STORAGE_H
#define STORAGE_H

#include <bb/system/LocaleHandler>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class EngineLink;

// Settings > Data and storage's requests (docs/protocol.md "Data and storage"): what TDLib keeps on the phone
// (storage.get), the data used (network.get), and the two changes, storage.clear and network.reset. Made by
// DataSettingsPage in its attachedObjects, as Privacy is: the owner dies with the page, so no connection to the engine
// outlives it.
class Storage : public QObject
{
    Q_OBJECT
    // The EngineLink, set as Privacy's link is.
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    // The last storage.get result: {files, count, db, logs}, bytes; empty until it comes.
    Q_PROPERTY(QVariantMap storage READ storage NOTIFY storageChanged)
    // The last network.get result: {since, mobile: {sent, received}, wifi: {...}, other: {...}}; empty until it comes.
    Q_PROPERTY(QVariantMap network READ network NOTIFY networkChanged)
    // The size the last storage.clear freed, as text ("1.2 MB"), for the toast that follows done("storage.clear").
    Q_PROPERTY(QString freed READ freed)
public:
    explicit Storage(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    QVariantMap storage() const { return m_storage; }
    QVariantMap network() const { return m_network; }
    QString freed() const { return m_freed; }

    // Asks for both; nothing without an engine, and asked again on connecting.
    Q_INVOKABLE void ask();
    // Each is followed by an ask when it is done.
    Q_INVOKABLE void clear();
    Q_INVOKABLE void reset();
    // A size in bytes as "0 KB", "812 KB", "12.3 MB" or "3.45 GB" (in the phone's region), and a unix time as a date.
    Q_INVOKABLE QString bytesText(double bytes) const;
    Q_INVOKABLE QString dateText(double secs) const;
    // data/cache/'s size, and its clearing, on a worker (Settings::cacheBytes, removeCache: thousands of files took the page 3 s
    // to open, the user, 0.1.0.81). Here and not in the app-wide Settings: the answers die with the page, as the others do.
    Q_INVOKABLE void measureCache(); // then cacheMeasured
    Q_INVOKABLE void clearCache();   // then cacheCleared

signals:
    void storageChanged();
    void networkChanged();
    // A change was done (its reply is in) / a request failed (the engine's failure is the reason); the cmd is the
    // request's. An ask that finds no engine is not reported: it is asked again on connecting, and the page binds
    // engine.failure.cmd to show it meanwhile.
    void done(const QString &cmd);
    void failed(const QString &cmd);
    void cacheMeasured(const QString &size); // "0 KB" for an empty cache
    void cacheCleared(const QString &freed);

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onFailure();
    void onConnected();
    void onMeasured();
    void onCleared();

private:
    QString send(const QString &cmd);

    QPointer<EngineLink> m_engine;
    QVariantMap m_storage;
    QVariantMap m_network;
    QString m_freed;
    bool m_want; // the page asked; asked again when the engine connects, until it answers
    QString m_storageId; // the asks we wait for; an older one that comes late is dropped
    QString m_networkId;
    QHash<QString, QString> m_sent; // the requests we wait for: id to cmd
    bb::system::LocaleHandler m_region;
};

#endif
