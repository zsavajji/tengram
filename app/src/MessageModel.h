#ifndef MESSAGEMODEL_H
#define MESSAGEMODEL_H

#include <bb/cascades/DataModel>
#include <bb/system/LocaleHandler>
#include <bb/utility/i18n/RelativeDateFormatter>
#include <QDate>
#include <QHash>
#include <QList>
#include <QLocale>
#include <QPointer>
#include <QSet>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class EngineLink;

// One chat's messages (PLAN.md §7, step 3.1), read-only from chats.db. The model is flat
// ([i]) and newest first: the newest day's messages newest to oldest, then that day's header,
// then the next older day, and so on. The list is bottom to top, so index 0 is at the bottom,
// it opens at the newest message with no scroll call, each header shows above its day, and an
// older page is appended at the model's end (the visual top) without moving the rows on
// screen. itemType: "header" (data: the title), "msg", or "service" for kinds 6 and 13 (MessageKinds
// words it). A "msg" is a map of id, out, state, edited, starred, deleted, revocable, time, label, text, quote,
// sender and senderColor, and media ("photo", "video", "doc", "sticker", "audio" or "") with the keys
// MediaData.h lists. Reading a sticker row that is still on the server asks the engine for its file,
// once per page. Every "msg" has album, a list: empty, or for an album (step 6.5g, Albums.h's rule) the
// members' maps oldest first. An album is ONE entry, keyed "album:" and its first member's id, whose
// map is that first member's plus album, albumCount, media "" (so MediaBubble isn't built) and the
// last member's time and state, since the card shows when it ended.
// The newest 50 load at once, older pages as the list nears its top; the loaded set always
// holds whole days, so a page never lands inside a loaded day.
class MessageModel : public bb::cascades::DataModel
{
    Q_OBJECT
    // True when there is nothing to show: the page says so (a cleared chat, a new one).
    Q_PROPERTY(bool empty READ empty NOTIFY emptyChanged)
    // True from opening the chat until its first page is in: the page shows a spinner meanwhile.
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
public:
    MessageModel(EngineLink *engine, const QString &chat, bool group, QObject *parent = 0);

    int childCount(const QVariantList &indexPath);
    bool hasChildren(const QVariantList &indexPath);
    QString itemType(const QVariantList &indexPath);
    QVariant data(const QVariantList &indexPath);

    // Loads the next older page when the first visible entry, an index path, is within 25
    // entries of the visual top (in a bottom-to-top list that is the model's end).
    Q_INVOKABLE void nearTop(const QVariantList &firstVisible);

    // The entry index of the message for a jump, after loading older messages back to its day;
    // -1 when it isn't shown (hidden by a pending delete, say).
    Q_INVOKABLE int reveal(const QString &id);
    // A tap on a spoiler (step 6.6d): reads its row again, now uncovered.
    Q_INVOKABLE void unspoil(const QString &id);

    bool empty() const { return m_entries.isEmpty(); }
    bool loading() const { return m_loading; }

    // The chat's unread count as chats.db has it now.
    int unread() const;

    // Messages the page deleted for me and may still take back: they leave the list at once,
    // before the engine's msgs event, and unhide brings them back.
    void hide(const QStringList &ids);
    void unhide(const QStringList &ids);

signals:
    void emptyChanged();
    void loadingChanged();
    // A merge added a message that isn't ours and wasn't there before (not a first load or an
    // older page): the controller decides whether it was seen.
    void incoming();

private slots:
    void loadFirst();
    void onMsgs(const QString &chat, const QStringList &ids);
    void onConnected();

private:
    struct Msg
    {
        qint64 ts;
        qint64 rowid;
        int day;       // ts's day in local time, a julian day: read once, when the row is
        QString id;
        int kind;      // messages.kind
        QString who;   // messages.sender (else its name): the sender Albums compares
        bool service;
        QVariantMap data;
    };
    struct Entry
    {
        QString key;       // the message id, "album:" and the first member's id, or "day:" and the julian day
        QString title;     // a header's
        int msg;           // index into m_all; -1 for a header, an album's first member
        QList<int> members; // an album's, in m_all; empty for a message
        QString sig;       // an album's member ids joined: refresh tells a changed album by it
    };

    static bool lessThan(const Msg &a, const Msg &b);
    Msg read(QSqlQuery &q);
    QList<Msg> select(const QString &where, const QVariantList &args, int limit);
    QList<qint64> pageTimes(const QString &where, const QVariantList &args, int limit);
    void loadPage();
    void merge(const QStringList &asked, const QList<Msg> &fresh, bool older);
    void rebuild();
    QStringList keys() const;
    int entryOf(const QString &id) const;
    QVariantMap albumData(const Entry &e) const;
    void refresh(const QStringList &old, const QSet<QString> &updated);
    QString longDay(const QDate &date, const QLocale &locale);
    QString dayTitle(const Msg &m);

    QPointer<EngineLink> m_engine;
    QString m_chat;
    bool m_group;
    bool m_atStart; // the oldest message is loaded
    bool m_loading;
    QList<Msg> m_all;
    QSet<QString> m_hidden;
    QSet<QString> m_revealed; // the spoilers tapped open (unspoil)
    QSet<QString> m_asked; // the sticker ids asked for from the engine (Sticker::fetch)
    QList<Entry> m_entries;
    bb::system::LocaleHandler m_region;
    QLocale m_locale; // the region's locale, read once per select()
    QString m_clock; // its short time format
    uint m_now; // the time select() started, unix seconds
    QHash<int, QString> m_longDays; // longDay()'s labels by julian day
    QHash<int, QString> m_dayTitles; // dayTitle()'s header titles by julian day, for the day m_titlesDay
    int m_titlesDay; // today's julian day when m_dayTitles was filled: "Today" moves at midnight
};

#endif
