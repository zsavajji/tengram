#ifndef MESSAGEMODEL_H
#define MESSAGEMODEL_H

#include <bb/cascades/DataModel>
#include <bb/system/LocaleHandler>
#include <bb/utility/i18n/RelativeDateFormatter>
#include <QDate>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QLocale>
#include <QPointer>
#include <QSet>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class EngineLink;

// The first page of a chat that opens at its newest message stops early (loadFirst, the `budget` of select()), by what the rows
// cost on screen: two screens (the user, 2026-11-20). A message with a picture across the card (photo, video, location map)
// fills a screen, a sticker a third, and text messages are 12 to 15 to a screen (counted as 15). A row costs 2 to 10 ms to
// read (file checks, pictures; ui.log on the Classic, 2026-11-20), so what the list shows first is read, the rest after.
static const int kBudget = 30;      // two screens of 15
static const int kCostPicture = 15; // kind 1 photo, 2 video, 9 location
static const int kCostSticker = 5;  // kind 5
static const int kCostText = 1;     // any other row

// One chat's messages (PLAN.md §7, step 3.1), read-only from chats.db. The model is flat
// ([i]) and newest first: the newest day's messages newest to oldest, then that day's header,
// then the next older day, and so on. The list is bottom to top, so index 0 is at the bottom,
// it opens at the newest message with no scroll call, each header shows above its day, and an
// older page is appended at the model's end (the visual top) without moving the rows on
// screen. itemType: "header" (data: the title), "msg", or "service" for kinds 6 and 13 (MessageKinds
// words it). A "msg" is a map of id, out, state, edited, starred, deleted, revocable, time, label, text, quote,
// sender (the name), senderId, senderColor, senderTag, senderPhoto, senderInitials, showName, showTile (see "Group
// chats" below), and media ("photo", "video", "doc", "sticker", "audio" or "") with the keys
// MediaData.h lists. Reading a sticker row that is still on the server asks the engine for its file,
// once per page. Every "msg" has album, a list: empty, or for an album (step 6.5g, Albums.h's rule) the
// members' maps oldest first. An album is ONE entry, keyed "album:" and its first member's id, whose
// map is that first member's plus album, albumCount, media "" (so MediaBubble isn't built) and the
// last member's time and state, since the card shows when it ended.
// Group chats show each sender's square picture beside their messages, in Telegram's colours, with an admin's tag
// after the name. A row starts with the colour and picture chats.db's `people` table has for its sender (the query joins
// it, MessageRows.cpp), so a chat opens with the colours it keeps; only a sender the engine has not written yet has the
// hash of the id. What the engine knows of a sender (senders.get: colour, tag, photo) is kept in m_senders, asked
// once per sender for the senders of the rows just read, and applied by data(), not baked into the rows: an answer,
// or a photo event, is an itemUpdated of that sender's entries. Whose messages make a run is decided in rebuild
// (markRuns): the name shows on the oldest message of a run, the picture on the newest, and refresh tells the entries
// whose flags changed (a new message from the same person takes the picture off the one above) with itemUpdated.
// The newest 50 load at once, older pages as the list nears its top. Only rows from the verified
// floor up are shown: chat.open and chat.history (docs/protocol.md) say the oldest id the engine
// made exactly Telegram's, and older rows may have gaps. A page can end inside a day, so the
// oldest loaded day has no header until an older day's message, or the start of the history, is
// loaded (docs/ui-fork.md 3.6): older rows of that day append at the model's end and the header
// appends after them, so a page never lands inside the loaded rows.
// A chat with unread messages opens at the first one, as Telegram does, and an "Unread messages" header
// ("unread", a "header" entry like a day's) sits above it for the life of the model; unreadEntry says where
// it is. Two ways there. When chats.db holds every unread message and they are no more than kUnreadMax rows
// (MessageRows.cpp findUnread), loadFirst loads back to the first one. Otherwise the model is in WINDOW mode
// (the `window` property): the cache may have gaps around the unread messages, so nothing of it is shown.
// The page keeps its spinner while chat.newer (docs/protocol.md) verifies one page after chats.read_inbox,
// and the model shows exactly that page, [m_floor, m_top], with the divider above its first incoming row.
// Older pages come as always (chat.history lowers the floor). Newer pages come as the list nears its
// bottom (index 0): loadNewer reads what chat.newer verified, askNewer asks for the next page, and when the
// verified run reaches Telegram's newest message joinLive leaves window mode and loads what arrived live
// meanwhile. Rows are only ever added at an end (the pitfall in docs/ui-fork.md 3.6), and the page turns its
// stick-to-edge policy off while `window` is true, so a page landing at the bottom doesn't drag the view.
// In window mode a live row beyond the loaded newest is dropped (merge), chat.open replies are ignored
// (takeOpen), and toLiveEnd (sending from far back) or a window that never answers (15 s) drops the window
// and opens at the newest, as a chat with nothing unread does (leaveWindow).
// A chat that opens at its newest message (nothing unread, no window) reads only two screens of rows first (loadFirst, the
// budget of select(): a picture message counts a screen, a sticker a third, a text a fifteenth; the user, 2026-11-20), so the
// list shows after a few rows, not 20 to 50 at 2 to 10 ms each. The rest of the page is read 700 ms later (loadRest), as a
// scroll to the top would; until then nearTop and fillShort read no older page. A chat that opens at its first unread
// message, or in a window, reads every row down to the divider as before.
// A text of ours is in the list the instant it is sent (the user's ask, 2026-11-20: the engine's round trip showed): addLocal
// puts a LOCAL row in m_all, sending (state 0, the clock), with the id "local-<n>", and the page slides it in as any new row.
// The engine's row for that text (merge: ours, new, the same words) takes its place: the entry keeps its place and takes the
// row's id, so the card is told itemUpdated, not a removal and an addition. A local row nobody took goes after 15 s, or at once
// when msg.send fails (dropLocals) or the engine reconnects. Never in window mode (a row beyond the loaded ones is dropped).
class MessageModel : public bb::cascades::DataModel
{
    Q_OBJECT
    // True when there is nothing to show: the page says so (a cleared chat, a new one).
    Q_PROPERTY(bool empty READ empty NOTIFY emptyChanged)
    // True from opening the chat until its first page is in: the page shows a spinner meanwhile.
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // True while the model shows a window of history around the first unread message, not the newest end:
    // newer pages are still to come. The page's stick-to-edge policy follows it.
    Q_PROPERTY(bool window READ window NOTIFY windowChanged)
    // True while a chat.history (older) or a chat.newer (window mode) is out and unanswered: the page shows a spinner at
    // the list's top (older) or bottom (newer) edge while the list rests there (the user, 2026-11-20).
    Q_PROPERTY(bool awaitingOlder READ awaitingOlder NOTIFY awaitingChanged)
    Q_PROPERTY(bool awaitingNewer READ awaitingNewer NOTIFY awaitingChanged)
    // True until the chat.open answer that gives the floor has come: no older page can be read or asked for before it, so
    // a list resting at its top has nothing to show but this wait (the user, 2026-11-20: 8 s with no spinner).
    Q_PROPERTY(bool awaitingOpen READ awaitingOpen NOTIFY awaitingChanged)
    // The language this chat's incoming messages are shown in ("it"), "" for off: chats.translate, read again on a chats event.
    Q_PROPERTY(QString translate READ translate NOTIFY translateChanged)
    // True while a msg.translate of this chat's is not answered yet: Telegram can take half a minute for a page, and the
    // title bar says so meanwhile (the user, 2026-11-21: "it worked but it did not tell me it was working").
    Q_PROPERTY(bool translating READ translating NOTIFY translatingChanged)
public:
    // topic: a forum topic's id (docs/protocol.md, Forum topics) when the model is one topic's messages, 0 for the chat's:
    // every read of messages then has `AND topic = ?`, the requests carry it, and the unread count and read_inbox are the
    // topic's own (the topics table's).
    // secret: a secret chat (chats.secret above 0): its messages can't be forwarded or edited, and carry timers (extra.ttl,
    // extra.expiresAt: the foot's countdown, ticked once a second by m_expiry while any row has one).
    MessageModel(EngineLink *engine, const QString &chat, bool group, bool channel, bool secret, int topic, QObject *parent = 0);

    int childCount(const QVariantList &indexPath);
    bool hasChildren(const QVariantList &indexPath);
    QString itemType(const QVariantList &indexPath);
    QVariant data(const QVariantList &indexPath);

    // Loads the next older page when the first visible entry, an index path, is within kNearTop
    // entries of the visual top (in a bottom-to-top list that is the model's end). Does nothing
    // until the first chat.open reply set the floor: only the first paint from the cache shows.
    // In window mode it also loads the next newer page when that entry is within kNearTop of the
    // bottom: the first visible item of a bottom-to-top list is the lowest index on screen.
    Q_INVOKABLE void nearTop(const QVariantList &firstVisible);
    // Leaves window mode for the newest message (sending from far back, as Telegram jumps to the newest
    // too): drops the loaded rows, shows the newest page and has chat.open sent again. No-op otherwise.
    Q_INVOKABLE void toLiveEnd();
    // The rows older than the unread divider, held back while the page puts the divider at the top of the screen
    // (m_holdOlder), go in above it: the page calls this once its scroll has settled.
    Q_INVOKABLE void releaseOlder();

    // The entry index of the message for a jump, after loading older messages back to its day;
    // -1 when it isn't shown (hidden by a pending delete, say).
    Q_INVOKABLE int reveal(const QString &id);
    // The entry index of the "Unread messages" divider, -1 when the chat opened at its newest message.
    Q_INVOKABLE int unreadEntry() const;
    // The "id" that data([entry]) carries, without building its map (the slide of a new row asks for nothing else): the
    // message's, an album's first member's; "" for a header or divider and for an entry out of range.
    Q_INVOKABLE QString idAt(int entry) const;
    // A tap on a spoiler (step 6.6d): reads its row again, now uncovered.
    Q_INVOKABLE void unspoil(const QString &id);

    bool empty() const { return m_entries.isEmpty(); }
    bool loading() const { return m_loading; }
    bool window() const { return m_window; }
    bool awaitingOlder() const { return !m_historyId.isEmpty(); }
    bool awaitingNewer() const { return !m_newerId.isEmpty(); }
    bool awaitingOpen() const { return !m_floorKnown; }
    QString translate() const { return m_translate; }
    bool translating() const { return !m_translating.isEmpty(); }
    // The id of the newest loaded message, "" when none: what reading down to the bottom reports.
    // The newest message on screen when entry `first` is the bottom-most one shown (chat.read's upto).
    QString shownUpTo(int first) const;
    QString newestLoaded() const { return m_all.isEmpty() ? QString() : m_all.last().id; }

    // The id of the chat.open request the controller just sent: its reply sets the verified floor.
    void expectOpen(const QString &requestId);

    // A text of ours as a row at the bottom now, before the engine has written its own (the class comment): the quote's line
    // is the quoted row's, read from chats.db as a real row's is (localRow). Returns its id, "" when none was added: window
    // mode, still loading, a blank text, or chats.db isn't open. The text goes as it is typed; the row shows it trimmed, as
    // Telegram keeps it.
    QString addLocal(const QString &text, const QString &quoteId);
    // Takes a local row out at once: its request was not sent. Nothing when it is gone already (the engine's row took its place).
    void dropLocal(const QString &id);
    // Takes every local row out: msg.send failed.
    void dropLocals();

    // The chat's (or topic's) unread count as chats.db has it now.
    int unread() const;

    // How many of the newest messages chat.open verifies: 50, more when the unread ones reach back further.
    int openLimit() const { return m_openLimit; }

    // Messages the page deleted for me and may still take back: they leave the list at once,
    // before the engine's msgs event, and unhide brings them back.
    void hide(const QStringList &ids);
    void unhide(const QStringList &ids);

signals:
    void emptyChanged();
    void loadingChanged();
    void windowChanged();
    void translateChanged();
    void translatingChanged();
    // A msg.translate of this model's failed (the engine's error code, or no_engine): the page warns the user.
    void translateStalled(const QString &code);
    // awaitingOlder or awaitingNewer changed (one signal for both).
    void awaitingChanged();
    // The model left window mode and has no verified floor: the controller sends chat.open again.
    void wantOpen();
    // A merge added a message that isn't ours and wasn't there before (not a first load or an
    // older page): the controller decides whether it was seen.
    void incoming();
    // A new row is about to go in at the bottom (index 0), before itemAdded tells the list: the page pins the list there, so
    // the row goes in below the screen and slides in (ChatPage's coming).
    void newestComing();

private slots:
    void loadFirst();
    void loadRest(); // the rest of a budgeted first page, once the list is in view
    void onMsgs(const QString &chat, const QStringList &ids);
    void onPreviews(const QStringList &ids);
    void onConnected();
    void onChats(const QStringList &jids);
    void onFailed(const QString &id, const QString &cmd, const QString &code);
    void retryTranslate();
    void askTranslate();
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onPhoto(const QString &jid, const QString &path);
    void giveUp();
    void tickExpiry();
    void tickLocal();
    void onFullscreen();
    void retryHistory();
    void syncEdge(); // the loading row, once the model operation that changed the waiting state is done

private:
    // Tells the page the waiting state changed (awaitingChanged) and queues syncEdge, which adds or drops the loading row.
    void edgeChanged() { emit awaitingChanged(); m_edgeSync.start(); }
    // Whether the list's top edge shows the loading row (the model's last entry): see the .cpp.
    bool edgeWaiting() const;
    // Sets a request slot (m_historyId or m_newerId) and tells the page when it went from empty to set or back. The
    // history slot also runs m_historyRetry: a set id starts it, an empty one stops it.
    void setAsked(QString &slot, const QString &id);
    struct Msg
    {
        qint64 ts;
        qint64 rowid;  // the message id as a number (messages.id is text): the tie-breaker of ts
        int day;       // ts's day in local time, a julian day: read once, when the row is
        QString id;
        int kind;      // messages.kind
        QString who;   // messages.sender (else its name): the sender Albums compares
        bool service;
        QVariantMap data;
    };
    struct Entry
    {
        QString key;       // the message id, "album:" and the first member's id, "day:" and the julian day, "unread", or "loading"
        QString title;     // a header's
        int msg;           // index into m_all; -1 for a header, the divider and the loading row; an album's first member
        QList<int> members; // an album's, in m_all; empty for a message
        QString sig;       // an album's member ids joined: refresh tells a changed album by it
        // A group's runs of one person's messages (markRuns): the name shows on the run's oldest message, the picture
        // on its newest. Both are for the card to read; a header has no use for them.
        bool showName;
        bool showTile;
        Entry() : msg(-1), showName(true), showTile(false) {}
    };
    // What the engine said of a sender (senders.get, then a photo event).
    struct Sender
    {
        int color;       // 0 to 6: an index into kSenders
        QString tag;     // an admin's tag, "" for none
        QString photo;   // the small picture's path, "" for none yet
    };

    // A local row not taken yet (addLocal): its id, the words it shows (words(): what merge compares) and when it went in.
    struct Local
    {
        QString id;
        QString text;
        qint64 at; // ms since the epoch
    };

    static bool lessThan(const Msg &a, const Msg &b);
    static bool isLocal(const QString &id) { return id.startsWith("local-"); }
    // A text as it is compared: runs of white space are one space and the ends are cut, so what TDLib trims or tidies in a
    // text it keeps (trailing newlines, a doubled space) still reads as what was typed.
    static QString words(const QString &text) { return text.simplified(); }
    // " AND topic = ?" in a topic's model, "": bindChat binds what chat = ? and it want.
    const char *topicSql() const { return m_topic ? " AND topic = ?" : ""; }
    void bindChat(QSqlQuery &q) const;
    Msg read(QSqlQuery &q);
    // The Msg of a local row: read() of a one-row table with messages' columns (MessageRows.cpp). id "" in the result: no chats.db.
    Msg localRow(const QString &id, const QString &text, const QString &quoteId, qint64 ts);
    // oldestFirst: the oldest `limit` rows that match instead of the newest (still returned oldest first).
    QList<Msg> select(const QString &where, const QVariantList &args, int limit, bool oldestFirst = false, int budget = 0);
    QList<qint64> pageTimes(const QString &where, const QVariantList &args, int limit);
    void findUnread();
    // budget: the first page's early stop (select); 0 reads the whole page.
    void loadPage(int budget = 0);
    void olderWhere(QString &where, QVariantList &args) const;
    void askOlder();
    void takeOpen(const QVariantMap &r);
    void takeHistory(const QVariantMap &r);
    void askBridge(const QString &before);
    void takeBridge(const QVariantMap &r);
    void askWindow();
    void takeNewer(const QVariantMap &r);
    void takeWindow(const QVariantMap &r);
    void askNewer();
    void loadNewer();
    void joinLive();
    void leaveWindow(bool awaitOpen = false);
    void dropBelowFloor();
    bool atStart() const;
    void merge(const QStringList &asked, const QList<Msg> &fresh, bool older);
    // Row m of ours, new to the list, takes the place of the oldest local row that says the same: that row leaves `all`, m
    // joins it, and the entry keeps its place under m's id. False when no local row waits for it.
    bool takeLocal(const Msg &m, QList<Msg> &all);
    // The entry keyed from (or the album keyed after it) is keyed to from now on: the list is told itemUpdated, not moved.
    void renameEntry(const QString &from, const QString &to);
    // These local rows leave m_all and m_locals, and the list is told.
    void removeLocals(const QStringList &ids);
    // Reads these rows again and merges them: onMsgs without its check for loading.
    void reread(const QStringList &ids);
    void rebuild();
    void fillShort();
    void pageIfNearTop();
    QString runOf(int entry) const;
    void markRuns();
    void askSenders();
    void takeSenders(const QString &id, const QVariantMap &r);
    void updateSenders(const QSet<QString> &senders, bool tilesOnly);
    QStringList keys() const;
    int entryOf(const QString &id) const;
    QVariantMap albumData(const Entry &e) const;
    void refresh(const QStringList &old, const QSet<QString> &updated);
    QString longDay(const QDate &date, const QLocale &locale);
    // A time as the foot shows it: "10 s", "5 min", "1 h", "7 d" (rounded up).
    static QString span(qint64 secs);
    QString dayTitle(const Msg &m);
    QString readTranslate() const;
    // Every loaded row read again, or the first page when none is loaded: onConnected, a new language, the retry.
    void reload();
    void wantTranslation(const Msg &m);

    QPointer<EngineLink> m_engine;
    QString m_chat;
    bool m_group;
    bool m_channel; // a channel (kind 3): read-only for us
    bool m_secret; // a secret chat: no forward, edit or reaction
    int m_topic; // the forum topic shown, 0 for the whole chat
    QString m_translate; // the language this chat's incoming messages are shown in (chats.translate), "" for off
    QSet<QString> m_askedTr; // the ids asked to be translated: never twice until the chat is reopened (or the link comes back)
    QHash<QString, QStringList> m_translating; // the msg.translate requests in flight: request id and the message ids it asked for
    QTimer m_retry; // single shot: the one retry armed after a failure that may pass (rate_limited, offline, no_engine)
    int m_retries; // the retries run in a row; a reply, a new language or a reconnect starts the count again
    QStringList m_toTranslate; // the rows drawn that want a translation (wantTranslation), sent by askTranslate
    QTimer m_trSoon; // single shot, 200 ms: a screenful of rows is one request
    qint64 m_floor; // the oldest verified message id: older rows are not shown
    bool m_floorKnown; // false until the first chat.open reply
    int m_seenFirst; // the first visible entry the list last reported (nearTop), whatever the model's state; -1 before any
    // The newest id of the continuous verified run that starts at m_floor (0 for none): a later chat.open
    // page that reaches it extends the run, one that does not is a gap (takeOpen).
    qint64 m_top;
    bool m_more; // older verified pages can still come (chat.history)
    bool m_starved; // the last loadPage found no row
    bool m_loading;
    // Whether a sender's small picture file is there, by path, for the rows of one select(): cleared at each, so a file
    // TDLib deleted since is seen at the next read.
    QHash<QString, bool> m_photoThere;
    int m_openLimit; // openLimit()
    QString m_unreadId; // the first unread message, "" for none: the chat opens at it (findUnread)
    QString m_openId; // the chat.open request the reply of which sets the floor
    QString m_historyId; // the chat.history request in flight, "" for none
    QElapsedTimer m_historyAt; // when it was sent; invalid until the first
    bool m_restPending; // a budgeted first page stopped early: loadRest reads the rest (nearTop and fillShort wait for it)
    QTimer m_restTimer; // single shot, kRestMs after the first page: loadRest
    QTimer m_edgeSync; // single shot, 0 ms: syncEdge runs after the operation that changed what the loading row depends on
    QTimer m_historyRetry; // single shot, kHistoryMs after a chat.history went out: no answer, so ask again (retryHistory)
    QString m_bridgeId; // the chat.history request that bridges a gap, "" for none
    QElapsedTimer m_bridgeAt; // when it was sent; invalid until the first
    int m_bridgePages; // the bridge's requests so far
    qint64 m_bridgeNewest; // the newest id of the chat.open page that started the bridge
    qint64 m_bridgeBefore; // the before of the bridge's last request
    // A chat opening at its first unread message shows nothing older than the divider until the page has scrolled
    // (releaseOlder): the divider is then the top of the list, which the list can only show at the top of the screen.
    // Where scrollToItem leaves an item in this bottom-to-top list it did not say (Classic, 0.1.0.8 to 0.1.0.11).
    bool m_holdOlder;
    bool m_window; // window mode (the class comment): the rows from m_floor to the newest loaded, not the newest end
    bool m_awaitOpen; // left the window for a jump: loading until chat.open sets the floor (leaveWindow)
    bool m_newerMore; // newer messages exist beyond m_top (chat.newer's more)
    qint64 m_readInbox; // chats.read_inbox when the window was chosen: the window starts after it
    QString m_newerId; // the chat.newer request in flight (the window's first included), "" for none
    QElapsedTimer m_newerAt; // when it was sent; invalid until the first
    QTimer m_windowGiveUp; // the window's first reply never came: open at the newest instead
    QTimer m_expiry; // once a second while a row has an expiresAt and the app is in front: those cards are drawn again
    int m_low; // the lowest visible entry the list last reported: whether the bottom is within reach
    QList<Msg> m_all;
    QSet<QString> m_hidden;
    QSet<QString> m_revealed; // the spoilers tapped open (unspoil)
    QSet<QString> m_asked; // the ids of the stickers, photos and videos asked for from the engine (Sticker::fetch)
    QHash<QString, QString> m_refetch; // the fetches of a picture whose file was gone, by request id: the message's id
    QHash<QString, Sender> m_senders; // a group's senders by user id, as senders.get answered
    QSet<QString> m_sendersAsked; // the user ids senders.get was sent for, and the ones it answered: asked once
    QSet<QString> m_sendersWanted; // the user ids of the rows read that nothing was asked for yet (askSenders)
    QHash<QString, QStringList> m_senderAsks; // the senders.get requests in flight, by request id: the user ids in each
    QList<Entry> m_entries;
    bb::system::LocaleHandler m_region;
    QLocale m_locale; // the region's locale, read once per select()
    QString m_clock; // its short time format
    uint m_now; // the time select() started, unix seconds
    QHash<int, QString> m_longDays; // longDay()'s labels by julian day
    QHash<int, QString> m_dayTitles; // dayTitle()'s header titles by julian day, for the day m_titlesDay
    int m_titlesDay; // today's julian day when m_dayTitles was filled: "Today" moves at midnight
    QList<Local> m_locals; // the local rows in m_all that no engine row has taken, oldest first
    QTimer m_localExpiry; // once a second while there are any: tickLocal drops the ones nothing took in kLocalMs
    int m_localCount; // the number of the last local id
    // Ours sent while the chat is open keep the place they first had (the user, 2026-11-20): the ts and day they sorted by, by
    // id, through their local, temporary and final rows and every later read of them. Telegram's time is still the one shown.
    QHash<QString, QPair<qint64, int> > m_place;
    void keepPlace(Msg &m, const Msg &old);
    void placed(Msg &m) const;
};

#endif
