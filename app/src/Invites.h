#ifndef INVITES_H
#define INVITES_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class EngineLink;

// Joining a group from an invite (step 6.6f): a link tapped in a message, or an invite card's
// button. One per app, made in main(), as the dialog that asks is main.qml's.
class Invites : public QObject
{
    Q_OBJECT
public:
    Invites(EngineLink *engine, QObject *parent = 0);

    // A tap on a link: true when it's a WhatsApp group or channel link, now being asked about
    // (offered or joined follows; a failure shows in engine's failure), false for anything else,
    // which the caller opens as usual.
    Q_INVOKABLE bool openLink(const QString &url);
    // Join group on an invite card, a message of the chat: asks at once, by the name the card shows.
    Q_INVOKABLE void openCard(const QString &chat, const QString &id, const QString &name);
    // The user said yes to what offered asked.
    Q_INVOKABLE void accept();

signals:
    // kind: "group" or "channel"; count 0 when unknown.
    void offered(const QString &kind, const QString &name, int count);
    void joined(const QString &jid);

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);

private:
    QPointer<EngineLink> m_engine;
    QString m_asking; // the invite.info request we wait for
    QString m_accepting; // the invite.accept or group.join request we wait for
    QString m_cmd; // what accept() sends: set when offered
    QVariantMap m_args;
};

#endif
