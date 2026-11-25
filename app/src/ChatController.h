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
    // page, and a comment thread's, has the group's name under its own.
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
    // The chat's (or the topic's) unread mentions and replies to us, and our messages with unread reactions, from chats.db
    // (chats.mentions and reactions, or the topic's): the @ and reactions buttons show while theirs isn't 0, with the number.
    Q_PROPERTY(int mentions READ mentions NOTIFY mentionsChanged)
    Q_PROPERTY(int reactions READ reactions NOTIFY mentionsChanged)
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
    int mentions() const { return m_mentions; }
    int reactions() const { return m_reactions; }

    // chat is a ChatListModel row (jid, kind). Once; later calls do nothing. topic is a forum topic's id (docs/protocol.md,
    // Forum topics) when the page shows one topic of the chat, 0 for the whole chat: everything the page asks for and
    // sends then carries it. thread is a channel post's comment thread (docs/protocol.md, Comments) in the discussion group
    // `chat`: the root's message id as a string (ids exceed 2^53, and QML's int is 32 bits), "" for none; it works as topic
    // does, and the two are never both given.
    Q_INVOKABLE void open(const QVariantMap &chat, int topic = 0, const QString &thread = QString());
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
    // Keeps the composer's text, with the message it replies to, as Telegram's draft of the chat, topic or thread, when it
    // differs from the draft there ('' or blanks remove it). Not while editing a message.
    Q_INVOKABLE void saveDraft(const QString &text);
    // The reply's message id, "" when not replying: a file sent as the reply carries it (step 6.4).
    Q_INVOKABLE QString quoteId() const { return m_quoteId; }
    Q_INVOKABLE void copy(const QString &text);
    // Asks Telegram for the chat's (or the topic's) pinned messages: pinsReady comes with them (a failure shows in the
    // engine's failure, as chat.pins).
    Q_INVOKABLE void askPins();
    // The @ button: asks for the oldest unread mention, which the engine marks read (chat.mention); unreadFound comes with
    // its id. Nothing while an answer is awaited, so a double tap doesn't skip one.
    Q_INVOKABLE void nextMention();
    // The reactions button, the same for our oldest message with unread reactions (chat.reaction).
    Q_INVOKABLE void nextReaction();
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
    // Translates one message into the language `to` (msg.translate); translated or translateFailed answers.
    Q_INVOKABLE void translate(const QString &id, const QString &to);
    // Asks which reactions a message allows (msg.reactions); reactionsAvailable or reactionsFailed answers. False when the
    // engine isn't connected: nothing was sent, and nothing will be emitted.
    Q_INVOKABLE bool reactionsFor(const QString &id);
    // Asks for the comments of a channel's post (msg.thread); threadFound or threadFailed answers. False when the engine isn't
    // connected: nothing was sent, and nothing will be emitted. The page's chat is the channel.
    Q_INVOKABLE bool openThread(const QString &id);
    // Searches the chat's messages (the topic's or thread's, when the page shows one) on Telegram's server, its whole history
    // and its own matching (chat.find); found() answers. False when nothing was sent: found() is then emitted at once, empty.
    Q_INVOKABLE bool find(const QString &text);
    // A language's name in its own language, capitalised ("Italiano"); the code when Qt knows none.
    Q_INVOKABLE QString languageName(const QString &code) const;

signals:
    void messagesChanged();
    void statusChanged();
    void replyChanged();
    void groupInfoChanged();
    // The reply to a voice.play: the request's id, and the WAV to play.
    void voiceReady(const QString &id, const QString &path);
    // The pinned messages, newest message first: maps of id and preview (Pins.h), after askPins.
    void pinsReady(const QVariantList &pins);
    void mentionsChanged();
    // chat.mention's or chat.reaction's message: the page jumps to it.
    void unreadFound(const QString &id);
    // A recording was dropped from here (minimised): the page resets its recorder.
    void voiceCancelled();
    // The answer to translate(): the message and its text in `to` ("" when it already is in it).
    void translated(const QString &msgId, const QString &text, const QString &to);
    // translate() failed: the engine's error code (rate_limited, no_key, unsupported, offline, no_engine, ...).
    void translateFailed(const QString &code);
    // The answer to reactionsFor(): the emoji the message allows, Telegram's own bar first (`top`), then all of them
    // (`top` again at the head). Both empty when the message takes no reactions.
    void reactionsAvailable(const QString &msgId, const QStringList &top, const QStringList &all);
    // reactionsFor() was refused or failed.
    void reactionsFailed(const QString &msgId);
    // The answer to openThread(): the discussion group (a chat id), the thread's root message there (a string) and the
    // group's title, "" when the group is not mirrored.
    void threadFound(const QString &chat, const QString &thread, const QString &name);
    // openThread() was refused or failed.
    void threadFailed();
    // The draft found at open() (draft.get): its text and, when it replies to a message, that message as reply() takes it.
    void draftFound(const QString &text, const QVariantMap &reply);
    // The app went to the background (a thumbnail): the page keeps its draft then too, as an app closed from there never pops it.
    void minimised();
    // The answer to find(): the ids of the messages found, a list of strings, newest first; empty when none matched or the
    // search failed.
    void found(const QVariant &ids);

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
    void onChats(const QStringList &jids);
    void onTopics(const QString &chat);
    void readMentions();
    void askUnread(const QString &cmd);
    void onTranslateReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onTranslateFailure();
    void onReactionsReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onReactionsFailure();
    void onThreadReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onThreadFailure();
    void onFindReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onFindFailure();

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
    QString m_thread; // the comment thread the page shows (its root's message id), "" for none
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
    QString m_translateId; // the msg.translate request we wait for
    QString m_translateTo; // and the language it asked for
    QString m_reactionsId; // the msg.reactions request we wait for
    QString m_reactionsMsg; // and the message it asked about
    QString m_threadId; // the msg.thread request we wait for
    QString m_findId; // the chat.find request we wait for (a newer one makes an older reply stale)
    QString m_mentionId; // the chat.mention or chat.reaction request we wait for (one at a time)
    QString m_draftId; // the draft.get request we wait for
    QString m_draftText; // the draft Telegram has, as found or last saved: saveDraft sends only a change
    QString m_draftQuote; // and the message it replies to, "" for none
    int m_mentions;
    int m_reactions;
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
