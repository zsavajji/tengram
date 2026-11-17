#ifndef CHATLISTMODEL_H
#define CHATLISTMODEL_H

#include <bb/cascades/DataModel>
#include <bb/system/LocaleHandler>
#include <bb/utility/i18n/RelativeDateFormatter>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class EngineLink;

// The chat list (PLAN.md §7, step 2.4): chats.db's chats that aren't archived or channels (or, for the Archived and Channels pages, only those), pinned first,
// then newest first. Headers ("Pinned", "Today", "Yesterday", "Monday", "Jan 20") are the top
// level ([g]), and the chats are their children ([g, i]): a map of jid, kind, name, initials,
// color, preview, time, out, state, unread, muted, pinned and photo (a file path, or "").
class ChatListModel : public bb::cascades::DataModel
{
    Q_OBJECT
    // True when there's no chat to show.
    Q_PROPERTY(bool empty READ empty NOTIFY emptyChanged)
    // The Active Frame's two facts (step 5.2): unread messages in unmuted chats, and the newest
    // incoming "Name: preview". Only recomputed while the app isn't fullscreen.
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY frameChanged)
    Q_PROPERTY(QString unreadChats READ unreadChats NOTIFY frameChanged)
    // Bumped after each reload, so a QML binding that calls chat() is evaluated again.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    // Which chats a list shows: the chat list, the Archived page or the Channels page (step 6.6g). Only the chat list does frame work.
    enum Mode { Chats, Archived, Channels };
    explicit ChatListModel(EngineLink *engine, Mode mode = Chats, QObject *parent = 0);

    int childCount(const QVariantList &indexPath);
    bool hasChildren(const QVariantList &indexPath);
    QString itemType(const QVariantList &indexPath);
    QVariant data(const QVariantList &indexPath);
    bool empty() const;
    int unreadTotal() const { return m_unreadTotal; }
    QString unreadChats() const { return m_unreadChats; }
    int revision() const { return m_revision; }
    // The chat's map as the rows have it, archived chats included; empty when there is none.
    Q_INVOKABLE QVariantMap chat(const QString &jid);
    // The ids of the chat's messages whose text has text in it, any case, newest first (step 5.10).
    Q_INVOKABLE QStringList findMessages(const QString &jid, const QString &text);
    // The starred messages, newest first, each a map of chat (its jid), id, chatName, sender (the sender's name, "You"
    // for ours, in a group; "" elsewhere), text (Pins::summary's line) and time (as a chat's row has it); deleted ones are left out.
    Q_INVOKABLE QVariantList starred();
    // Who reacted to a message and how, oldest first, one per line (step 5.11).
    Q_INVOKABLE QString reactionsText(const QString &jid, const QString &id);
    // Shows only the chats whose name has a word starting with text; "" shows all.
    Q_INVOKABLE void setFilter(const QString &text);

signals:
    void emptyChanged();
    void frameChanged();
    void revisionChanged();

private slots:
    void reloadSoon();
    void reload();
    void updateFrame();

private:
    bool matches(const QString &name) const;
    struct Group
    {
        QString title;
        QList<QVariantMap> rows;
    };
    EngineLink *m_engine;
    Mode m_mode;
    QString m_dbName;
    QList<Group> m_groups;
    int m_unreadTotal;
    QString m_unreadChats;
    int m_revision;
    QString m_filter;
    QTimer m_soon;
    bb::system::LocaleHandler m_region;
};

#endif
