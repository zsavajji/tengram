#ifndef CHATLISTMODEL_H
#define CHATLISTMODEL_H

#include <bb/cascades/DataModel>
#include <bb/system/LocaleHandler>
#include <bb/utility/i18n/RelativeDateFormatter>
#include <QDate>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QLocale>
#include <QMap>
#include <QMetaType>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class EngineLink;
class QThread;

// A group of the list: a header and its chats.
struct ChatListGroup
{
    QString title;
    QList<QVariantMap> rows;
};

// What a row's time and its group's title come from, for a reload and a patch alike so they cannot disagree.
// Made on the UI thread (BB10's formatters are asked there, once per run), then a plain value: the reader's
// thread uses a copy, and place() asks nothing but Qt.
struct RowClock
{
    RowClock() : now(0) {}
    explicit RowClock(const QLocale &l);   // UI thread only
    // Sets row["time"] from last_ts (null when the chat has no message) and pinned, and returns the
    // title of the group the row belongs to.
    QString place(QVariantMap *row, const QVariant &lastTs, bool pinned) const;

    QLocale locale;
    QString clock;
    QDate today;
    qint64 now;
    QHash<int, QString> days; // the day's label by julian day, tomorrow to six days back
};

// One read of the list, asked by the model on the UI thread.
struct ChatListRequest
{
    bool archived;
    QString filter;
    RowClock clock;
};

// Its answer. ok is false when chats.db could not be read (the engine hasn't made it yet).
struct ChatListRead
{
    ChatListRead() : ok(false), chats(0), queryMs(0), fetchMs(0), rowsMs(0) {}
    bool ok;
    QList<ChatListGroup> groups;
    QHash<QString, int> groupAt;              // new group index, by title (Moves::groups)
    QHash<QString, QPair<int, int> > itemAt;  // new [g, i], by jid (Moves::items)
    int chats;
    int queryMs, fetchMs, rowsMs;
};
Q_DECLARE_METATYPE(ChatListRequest)
Q_DECLARE_METATYPE(ChatListRead)

// Reads the list on its own thread, under its own connection to chats.db (a connection belongs to one thread).
class ChatListReader : public QObject
{
    Q_OBJECT
public:
    explicit ChatListReader(const QString &connectionName); // no parent: it moves to a thread
public slots:
    void read(const ChatListRequest &request);
signals:
    void done(const ChatListRead &read);
private:
    QString m_dbName;
};

// The chat list (PLAN.md §7, step 2.4): chats.db's chats that aren't archived (or, for the Archived page, only those), pinned first,
// then newest first. Headers ("Pinned", "Today", "Yesterday", "Monday", "Jan 20") are the top
// level ([g]), and the chats are their children ([g, i]): a map of jid, kind, name, initials,
// color, preview, time, out, state, unread, muted, pinned, pinOrder (chats.pinned, a qint64: the order the user gave the
// pins), ts (last_ts, -1 for none), photo (a file path, or "") and forum (a group with topics).
class ChatListModel : public bb::cascades::DataModel
{
    Q_OBJECT
    // True when there's no chat to show, once the first read has answered.
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
    ~ChatListModel();

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
    void readWanted(const ChatListRequest &request);

private slots:
    void onChats(const QStringList &jids);
    void onConnected();
    void onFullscreenChanged();
    void reloadSoon();
    void update();
    void reload();
    void updateFrame();
    void onRead(const ChatListRead &read);

private:
    bool matches(const QString &name) const;
    typedef QMap<QPair<int, int>, QVariantMap> Patches; // new rows, by [g, i]
    typedef ChatListGroup Group;
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
    QThread *m_thread;       // no parent, see ~ChatListModel
    bool m_reading;          // a read is out
    bool m_again;            // something changed while it was out: read again when it is back
    bool m_loaded;           // the first answer has come: before it an empty list is not "no chats"
    QElapsedTimer m_readTimer;
};

#endif
