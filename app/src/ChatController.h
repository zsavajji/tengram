#ifndef CHATCONTROLLER_H
#define CHATCONTROLLER_H

#include "Pins.h"

#include <bb/cascades/DataModel>
#include <bb/system/LocaleHandler>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class EngineLink;
class MessageModel;

// One open chat page (PLAN.md §7, step 3.1), made by QML in the page's attachedObjects so it
// dies with the page. It owns the page's MessageModel, tells the engine the chat is open, turns
// typing and presence events into the title bar's status line, and (step 3.2) sends messages and
// our own typing state.
class ChatController : public QObject
{
    Q_OBJECT
    // The EngineLink (a QObject so QML can pass the context property). Not named engine: in
    // `engine: engine` QML would read the controller's own property, not the context's.
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    Q_PROPERTY(bb::cascades::DataModel *messages READ messages NOTIFY messagesChanged)
    // The title bar's status line: "typing…", "online", "last seen …", a group's "N participants" or "". A topic's
    // page has the group's name under the topic's.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // The message the composer is replying to (step 3.3): who wrote it and a line of it; "" when
    // not replying.
    Q_PROPERTY(QString replyName READ replyName NOTIFY replyChanged)
    Q_PROPERTY(QString replyText READ replyText NOTIFY replyChanged)
    // The composer edits one of our messages (replyText is its text): the next send replaces it.
    Q_PROPERTY(bool editing READ editing NOTIFY replyChanged)
    // The last group.info result {subject, description, participants}, each participant with
    // initials and color added (and photo, when it has one); empty until it comes (groups only).
    Q_PROPERTY(QVariantMap groupInfo READ groupInfo NOTIFY groupInfoChanged)
public:
    explicit ChatController(QObject *parent = 0);
    ~ChatController();

    QObject *link() const;
    void setLink(QObject *link);
    bb::cascades::DataModel *messages() const;
    QString status() const;
    QString replyName() const;
    QString replyText() const;
    bool editing() const { return !m_editId.isEmpty(); }
    QVariantMap groupInfo() const;

    // chat is a ChatListModel row (jid, kind). Once; later calls do nothing. topic is a forum topic's id (docs/protocol.md,
    // Forum topics) when the page shows one topic of the chat, 0 for the whole chat: everything the page asks for and
    // sends then carries it.
    Q_INVOKABLE void open(const QVariantMap &chat, int topic = 0);
    // A group's info only, for a page with no messages (TopicsPage's Group info): groupInfo and changeParticipant
    // work, nothing else. Asks group.info again at each call; does nothing after open().
    Q_INVOKABLE void openInfo(const QVariantMap &chat);
    Q_INVOKABLE void nearTop(const QVariantList &firstVisible);
    // False when the engine isn't connected: nothing was sent, so QML keeps the text. mentions are
    // the {jid, name} of the people the text's "@name"s name (msg.send's, docs/protocol.md). instant: a new text goes in the
    // list at once as a local row (MessageModel::addLocal) and the engine's row takes its place; the composer's text asks for
    // it, the caption that follows a file does not (its row comes after the file's, which the local row would sit below).
    Q_INVOKABLE bool send(const QString &text, const QVariantList &mentions = QVariantList(), bool instant = false);
    // Called on every change of the composer's text; sends our typing on and off.
    Q_INVOKABLE void composing(const QString &text);
    // Whether the list shows the newest message; QML reports it on scrolling, and once after it has
    // positioned the list (a chat with unread messages opens at the first one): open() starts it false.
    Q_INVOKABLE void setAtBottom(bool atBottom);

    // Voice notes (step 4.4b). startVoice returns the absolute path of the WAV to record into, "" when
    // the engine isn't connected; the page's recorder stops before sendVoice, so the tail is on disk.
    Q_INVOKABLE QString startVoice();
    Q_INVOKABLE void sendVoice();
    Q_INVOKABLE void cancelVoice();
    // The first `bars` of a voice note's 64 bars (its waveData), for the part played (4.3b).
    Q_INVOKABLE QVariant playedWave(const QVariant &values, int bars) const;

    // msg is a MessageModel "msg" map; the next send quotes it.
    Q_INVOKABLE void reply(const QVariantMap &msg);
    // msg is a MessageModel "msg" map of ours; the next send is its new text (msg.edit).
    Q_INVOKABLE void edit(const QVariantMap &msg);
    // Ends a reply or an edit.
    Q_INVOKABLE void cancelReply();
    // The reply's message id, "" when not replying: a file sent as the reply carries it (step 6.4).
    Q_INVOKABLE QString quoteId() const { return m_quoteId; }
    Q_INVOKABLE void copy(const QString &text);
    // Asks Telegram for the chat's (or the topic's) pinned messages: pinsReady comes with them (a failure shows in the
    // engine's failure, as chat.pins).
    Q_INVOKABLE void askPins();
    // Delete for me waits for the Undo toast: the card hides at once, the request goes with
    // commitDelete (the toast closed, or the page going away).
    Q_INVOKABLE void deleteForMe(const QString &id);
    Q_INVOKABLE void undoDelete();
    Q_INVOKABLE void commitDelete();
    // False when the engine isn't connected: nothing was sent.
    Q_INVOKABLE bool deleteForEveryone(const QString &id);
    // A group's member changed by an admin: action is add, remove, promote or demote. groupInfo
    // is asked again once Telegram agrees; a refusal shows in the engine's failure.
    Q_INVOKABLE void changeParticipant(const QString &jid, const QString &action);

signals:
    void messagesChanged();
    void statusChanged();
    void replyChanged();
    void groupInfoChanged();
    // The reply to a voice.play: the request's id, and the WAV to play.
    void voiceReady(const QString &id, const QString &path);
    // The pinned messages, newest message first: maps of id and preview (Pins.h), after askPins.
    void pinsReady(const QVariantList &pins);
    // A recording was dropped from here (minimised): the page resets its recorder.
    void voiceCancelled();

private slots:
    void sendReadUpto();
    void onTyping(const QString &chat, const QString &who, bool on, bool voice, int topic);
    void onPresence(const QString &chat, bool online, qint64 lastSeen);
    void onConnected();
    void typingIdle();
    void composePaused();
    void onIncoming();
    void onFullscreenChanged();
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onPhoto(const QString &jid, const QString &path);
    void onWantOpen();
    void onFailure();

private:
    void takeGroupInfo(QVariantMap info);
    void addTopic(QVariantMap &args) const;
    void sendOpen();
    void askGroupInfo();
    void markRead();
    void setTyping(const QString &text);
    void setComposing(bool on);

    QPointer<EngineLink> m_engine;
    MessageModel *m_messages;
    QString m_jid;
    int m_topic; // the forum topic the page shows, 0 for none
    QString m_name;
    QString m_quoteId;
    QString m_editId;
    QString m_voicePath; // the WAV being recorded
    QString m_replyName;
    QString m_replyText;
    QVariantMap m_groupInfo;
    QString m_infoId; // the group.info request we wait for
    QString m_changeId; // the group.participants request we wait for
    QString m_pinsId; // the chat.pins request we wait for
    QString m_participants; // "N participants", the status line of a group
    QStringList m_pending;
    bool m_group;
    bool m_unseen;   // a message arrived that no one has seen yet
    bool m_atBottom; // the list shows the newest message; false from open() until the page says
    bool m_placed; // the page has positioned the list (its first setAtBottom): reads by scrolling count from then
    QString m_pendingUpto; // the newest message shown since the last chat.read upto, sent by m_readSoon
    QTimer m_readSoon;
    qint64 m_readUpto; // the newest id chat.read was last sent up to in window mode (markRead), 0 for none
    QString m_typing;
    QString m_presence;
    QTimer m_idle;
    // Our own typing state, apart from the other person's above.
    bool m_composing;
    QTimer m_composeRepeat;
    QTimer m_composePause;
    bb::system::LocaleHandler m_region;
};

#endif
