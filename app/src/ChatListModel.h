#ifndef CHATLISTMODEL_H
#define CHATLISTMODEL_H

#include <bb/cascades/DataModel>
#include <bb/system/LocaleHandler>
#include <bb/utility/i18n/RelativeDateFormatter>
#include <QList>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class EngineLink;

// The chat list (PLAN.md §7, step 2.4): chats.db's chats that aren't archived (or, for the Archived page, only those), pinned first,
// then newest first. Headers ("Pinned", "Today", "Yesterday", "Monday", "Jan 20") are the top
// level ([g]), and the chats are their children ([g, i]): a map of jid, kind, name, initials,
// color, preview, time, out, state, unread, muted, pinned, pinOrder (chats.pinned, a qint64: the order the user gave the
// pins), ts (last_ts, -1 for none), photo (a file path, or "") and forum (a group with topics).
class ChatListModel : public bb::cascades::DataModel
{
    Q_OBJECT
    // True when there's no chat to show.
    Q_PROPERTY(bool empty READ empty NOTIFY emptyChanged)
    // The Active Frame's rows (step 5.2): what is unread in 1:1 chats, groups and channels ([0], [1], [2]), an
    // unmuted chat's unread messages and a muted one's unread mentions and replies to us (what the Hub
    // notifies), and those chats' names, newest first. Only recomputed while the app isn't fullscreen.
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY frameChanged)
    Q_PROPERTY(QVariantList frameCounts READ frameCounts NOTIFY frameChanged)
    Q_PROPERTY(QStringList frameNames READ frameNames NOTIFY frameChanged)
    // Bumped after each reload or patch, so a QML binding that calls chat() is evaluated again.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    // Which chats a list shows: the chat list or the Archived page. Only the chat list does frame work.
    enum Mode { Chats, Archived };
    explicit ChatListModel(EngineLink *engine, Mode mode = Chats, QObject *parent = 0);

    int childCount(const QVariantList &indexPath);
    bool hasChildren(const QVariantList &indexPath);
    QString itemType(const QVariantList &indexPath);
    QVariant data(const QVariantList &indexPath);
    bool empty() const;
    int unreadTotal() const { return m_unreadTotal; }
    QVariantList frameCounts() const { return m_frameCounts; }
    QStringList frameNames() const { return m_frameNames; }
    int revision() const { return m_revision; }
    // The chat's map as the rows have it, archived chats included; empty when there is none.
    Q_INVOKABLE QVariantMap chat(const QString &jid);
    // The ids of the chat's messages whose text has text in it, any case, newest first (step 5.10).
    Q_INVOKABLE QStringList findMessages(const QString &jid, const QString &text);
    // The jids of the pinned chats of the main list, top first (all of them, whatever the filter); for chat.pinOrder.
    Q_INVOKABLE QStringList pinnedJids();
    // Shows only the chats whose name has a word starting with text; "" shows all.
    Q_INVOKABLE void setFilter(const QString &text);

signals:
    void emptyChanged();
    void frameChanged();
    void revisionChanged();

private slots:
    void onChats(const QStringList &jids);
    void onConnected();
    void onFullscreenChanged();
    void reloadSoon();
    void update();
    void reload();
    void updateFrame();

private:
    bool matches(const QString &name) const;
    typedef QMap<QPair<int, int>, QVariantMap> Patches; // new rows, by [g, i]
    struct Group
    {
        QString title;
        QList<QVariantMap> rows;
    };
    bool patch();
    const QVariantMap *adjacent(int g, int i, int step, const Patches &patched) const;
    EngineLink *m_engine;
    Mode m_mode;
    QString m_dbName;
    QList<Group> m_groups;
    int m_unreadTotal;
    QVariantList m_frameCounts;
    QStringList m_frameNames;
    int m_revision;
    bool m_stale;          // the app was not in front for a change: read the list when it is
    bool m_full;           // the changed jids are not all there is: reload, don't patch
    QSet<QString> m_changed; // jids of chats events since the last update
    QString m_filter;
    QTimer m_soon;
    bb::system::LocaleHandler m_region;
};

#endif
