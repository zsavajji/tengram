#ifndef INVITES_H
#define INVITES_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

namespace bb {
namespace system {
class InvokeManager;
}
}

class EngineLink;

// Telegram links in the app (steps 6.6f, 6.6g): a link tapped in a message or a link preview, or an
// invite card's button. The engine says what the link is (link.open): a person's page, a chat to open
// (at a message, for a link to one), a group or channel to join (asked first, main.qml's dialog), or
// not ours, which goes to the browser from here. One per app, made in main().
class Invites : public QObject
{
    Q_OBJECT
public:
    Invites(EngineLink *engine, QObject *parent = 0);

    // A tap on a link: true when it's a Telegram link (t.me, telegram.me, telegram.dog, tg:), now
    // being asked about (person, joined or offered follows, or the browser opens it; a failure
    // shows in engine's failure), false for anything else, which the caller opens as usual.
    Q_INVOKABLE bool openLink(const QString &url);
    // Join group on an invite card, a message of the chat: asks at once, by the name the card shows.
    Q_INVOKABLE void openCard(const QString &chat, const QString &id, const QString &name);
    // The user said yes to what offered asked.
    Q_INVOKABLE void accept();

signals:
    // kind: "group" or "channel"; count 0 when unknown.
    void offered(const QString &kind, const QString &name, int count);
    // A chat to open: msg is the message a link names, "" for none (and after a join).
    void joined(const QString &jid, const QString &msg);
    // A link to a user or a bot: the person's page.
    void person(const QString &jid, const QString &name);

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onOpened();

private:
    void openBrowser(const QString &url);

    QPointer<EngineLink> m_engine;
    QString m_asking; // the link.open request we wait for
    QString m_url; // its link, for the browser when the engine says it's not ours
    QString m_accepting; // the invite.accept or group.join request we wait for
    bb::system::InvokeManager *m_invoker; // made at the first browser open
    QString m_cmd; // what accept() sends: set when offered
    QVariantMap m_args;
};

#endif
