#ifndef CONTACTINFO_H
#define CONTACTINFO_H

#include <bb/system/InvokeManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class EngineLink;

// The contact info page's reply (step 5.8), made by QML in the page's attachedObjects: the
// request's owner dies with the page, so no connection to the engine outlives it.
class ContactInfo : public QObject
{
    Q_OBJECT
    // The EngineLink, set as ChatController's link is.
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    // The request asked: the channel page asks channel.info through the same object.
    Q_PROPERTY(QString command READ command WRITE setCommand)
    // The last contact.info result {phone, about, groups}, each group with initials and color
    // added; empty until it comes.
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged)
public:
    explicit ContactInfo(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    QString command() const { return m_command; }
    void setCommand(const QString &command) { m_command = command; }
    QVariantMap info() const;

    // Asks the engine about a person's chat (a chat list row's jid); nothing without an engine.
    Q_INVOKABLE void ask(const QString &jid);
    // Blocks or unblocks the person asked about (step 5.8b); info.blocked follows once it's done.
    Q_INVOKABLE void block(bool on);
    // Writes the chat as a text file into Downloads; exported() says where.
    Q_INVOKABLE void exportChat();
    // Opens the Contacts app's card to save the number in a new or an existing contact.
    Q_INVOKABLE void saveNumber(const QString &phone);

signals:
    void infoChanged();
    void exported(const QString &path);

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onConnected();

private:
    QPointer<EngineLink> m_engine;
    QVariantMap m_info;
    QString m_command;
    QString m_jid; // the chat asked about
    QString m_id; // the contact.info request we wait for
    QString m_blockId; // the contact.block request we wait for
    QString m_exportId; // the chat.export request we wait for
    bb::system::InvokeManager m_invoker;
};

#endif
