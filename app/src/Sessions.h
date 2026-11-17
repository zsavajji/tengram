#ifndef SESSIONS_H
#define SESSIONS_H

#include <bb/system/LocaleHandler>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class EngineLink;

// The devices page's and the QR scan page's replies, made by QML in each page's attachedObjects: the
// request's owner dies with the page, so no connection to the engine outlives it.
class Sessions : public QObject
{
    Q_OBJECT
    // The EngineLink, set as Profile's link is.
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    // The last sessions.list result, a row for each session with its lines ready for the page:
    // {id, current, unconfirmed, title, device, place}; this device first. Empty until it comes.
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    // True once a list has come.
    Q_PROPERTY(bool loaded READ loaded NOTIFY listChanged)
    // How many of the sessions are not this device.
    Q_PROPERTY(int others READ others NOTIFY listChanged)
public:
    explicit Sessions(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    QVariantList list() const { return m_list; }
    bool loaded() const { return m_loaded; }
    int others() const { return m_others; }

    // Asks the engine for the sessions (sessions.list); nothing without an engine.
    Q_INVOKABLE void ask();
    // Each sends its request; terminating asks again when it is done, and failed() follows a failure.
    Q_INVOKABLE void terminate(const QString &id);
    Q_INVOKABLE void terminateOthers();
    // Signs in the device that shows the QR code with this tg://login?token= link; confirmed() when done.
    Q_INVOKABLE void confirmQr(const QString &link);

signals:
    void listChanged();
    // A terminate or a confirmQr failed (the engine's failure is the reason); the cmd is the request's.
    void failed(const QString &cmd);
    void confirmed();

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onFailure();
    void onConnected();

private:
    void send(const QString &cmd, const QVariantMap &args);
    QVariantMap row(const QVariantMap &session) const;
    QString whenText(qint64 stamp) const;

    QPointer<EngineLink> m_engine;
    QVariantList m_list;
    bool m_loaded;
    int m_others;
    bool m_wanted; // the page asked; asked again when the engine connects, until it answers
    QHash<QString, QString> m_sent; // the requests we wait for: id to cmd
    bb::system::LocaleHandler m_region;
};

#endif
