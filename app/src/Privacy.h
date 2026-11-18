#ifndef PRIVACY_H
#define PRIVACY_H

#include <bb/cascades/ArrayDataModel>
#include <bb/cascades/DataModel>
#include <bb/system/LocaleHandler>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class EngineLink;

// Settings > Privacy and security's requests (docs/protocol.md "Privacy and security"): the settings page, the
// two-step verification page, the blocked users and the connected websites. Made by QML in each page's
// attachedObjects, as Sessions is: the request's owner dies with the page, so no connection to the engine outlives
// it. A page uses the part it needs (info; blocked; websites).
class Privacy : public QObject
{
    Q_OBJECT
    // The EngineLink, set as Profile's link is.
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    // The last privacy.get result: {rules, exceptions, password, autoDelete, blocked, ttlDays, topChats, imported}; a
    // field whose call failed is absent. Empty until it comes. A password.* reply replaces its password. Writable so
    // the password page can start from what the settings page already has, instead of asking for everything again.
    Q_PROPERTY(QVariantMap info READ info WRITE setInfo NOTIFY infoChanged)
    // The blocked people, a row for each ContactRow draws: {jid, kind, name, initials, color, photo}. A model, not a
    // list, so that a photo that arrives later replaces its row alone (up to 500 rows).
    Q_PROPERTY(bb::cascades::DataModel *blocked READ blocked CONSTANT)
    // How many are blocked in all (can be more than the model holds), and true once the first page has come.
    Q_PROPERTY(int blockedTotal READ blockedTotal NOTIFY blockedChanged)
    Q_PROPERTY(bool blockedLoaded READ blockedLoaded NOTIFY blockedChanged)
    // The connected websites, a row for each with its lines ready: {id, domain, bot, device, place}; and true once
    // the list has come.
    Q_PROPERTY(QVariantList websites READ websites NOTIFY websitesChanged)
    Q_PROPERTY(bool websitesLoaded READ websitesLoaded NOTIFY websitesChanged)
public:
    explicit Privacy(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    QVariantMap info() const { return m_info; }
    void setInfo(const QVariantMap &info);
    bb::cascades::DataModel *blocked() const { return m_blocked; }
    int blockedTotal() const { return m_blockedTotal; }
    bool blockedLoaded() const { return m_blockedLoaded; }
    QVariantList websites() const { return m_websites; }
    bool websitesLoaded() const { return m_websitesLoaded; }

    // Each asks the engine (privacy.get, blocked.list, websites.list); nothing without an engine. A change asks
    // again when it is done (a failed change too, so the page goes back to what Telegram has).
    Q_INVOKABLE void ask();
    Q_INVOKABLE void askBlocked();
    Q_INVOKABLE void askWebsites();
    // key is a key of info.rules, value everybody, contacts or nobody.
    Q_INVOKABLE void setRule(const QString &key, const QString &value);
    Q_INVOKABLE void setAutoDelete(int secs);
    Q_INVOKABLE void setTtl(int days);
    Q_INVOKABLE void setTopChats(bool on);
    Q_INVOKABLE void clearPayments();
    Q_INVOKABLE void clearImported();
    // The two-step verification; the reply is the new info.password. old is "" when no password is set.
    Q_INVOKABLE void setPassword(const QString &old, const QString &password, const QString &hint, const QString &email);
    Q_INVOKABLE void setEmail(const QString &password, const QString &email);
    Q_INVOKABLE void sendCode(const QString &code);
    Q_INVOKABLE void resendCode();
    Q_INVOKABLE void passwordOff(const QString &old);
    // kind is "user" or "chat", as the row has it.
    Q_INVOKABLE void unblock(const QString &jid, const QString &kind);
    // Not disconnect(): that is QObject's.
    Q_INVOKABLE void disconnectSite(const QString &id);
    Q_INVOKABLE void disconnectAll();

signals:
    void infoChanged();
    void blockedChanged();
    void websitesChanged();
    // A change was done (its reply is in; an ask is not a change) / a request failed (the engine's failure is the
    // reason); the cmd is the request's. An ask that finds no engine is not reported: it is asked again on connecting,
    // and the page binds engine.failure.cmd to show it meanwhile.
    void done(const QString &cmd);
    void failed(const QString &cmd);

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onFailure();
    void onConnected();
    void onPhoto(const QString &jid, const QString &path);

private:
    QString send(const QString &cmd, const QVariantMap &args = QVariantMap());
    void askBlockedFrom(int offset);
    QVariantMap blockedRow(const QVariantMap &person) const;
    QVariantMap websiteRow(const QVariantMap &site) const;

    QPointer<EngineLink> m_engine;
    QVariantMap m_info;
    bb::cascades::ArrayDataModel *m_blocked;
    int m_blockedTotal;
    bool m_blockedLoaded;
    int m_blockedOffset; // the offset of the blocked.list we wait for
    QVariantList m_websites;
    bool m_websitesLoaded;
    // The page asked; asked again when the engine connects, until it answers.
    bool m_wantInfo;
    bool m_wantBlocked;
    bool m_wantWebsites;
    // The asks we wait for; an older one that comes late is dropped.
    QString m_infoId;
    QString m_blockedId;
    QString m_websitesId;
    QHash<QString, QString> m_sent; // the requests we wait for: id to cmd
    bb::system::LocaleHandler m_region;
};

#endif
