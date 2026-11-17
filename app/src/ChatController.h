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
    // The title bar's status line: "typing…", "online", "last seen …", a group's "N participants" or "".
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

    // chat is a ChatListModel row (jid, kind). Once; later calls do nothing.
    Q_INVOKABLE void open(const QVariantMap &chat);
    Q_INVOKABLE void nearTop(const QVariantList &firstVisible);
    // False when the engine isn't connected: nothing was sent, so QML keeps the text. mentions are
    // the {jid, name} of the people the text's "@name"s name (msg.send's, docs/protocol.md).
    Q_INVOKABLE bool send(const QString &text, const QVariantList &mentions = QVariantList());
    // Called on every change of the composer's text; sends our typing on and off.
    Q_INVOKABLE void composing(const QString &text);
    // Whether the list shows the newest message; QML reports it on scrolling.
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
    // The messages pinned now, newest pin first: maps of id and preview (Pins.h, step 6.5h).
    Q_INVOKABLE QVariantList pins() const { return Pins::current(m_jid); }
    // Delete for me waits for the Undo toast: the card hides at once, the request goes with
    // commitDelete (the toast closed, or the page going away).
    Q_INVOKABLE void deleteForMe(const QString &id);
    Q_INVOKABLE void undoDelete();
    Q_INVOKABLE void commitDelete();
    // False when the engine isn't connected: nothing was sent.
    Q_INVOKABLE bool deleteForEveryone(const QString &id);
    // A group's member changed by an admin: action is add, remove, promote or demote. groupInfo
    // is asked again once WhatsApp agrees; a refusal shows in the engine's failure.
    Q_INVOKABLE void changeParticipant(const QString &jid, const QString &action);

signals:
    void messagesChanged();
    void statusChanged();
    void replyChanged();
    void groupInfoChanged();
    // The reply to a voice.play: the request's id, and the WAV to play.
    void voiceReady(const QString &id, const QString &path);
    // A recording was dropped from here (minimised): the page resets its recorder.
    void voiceCancelled();

private slots:
    void onTyping(const QString &chat, const QString &who, bool on, bool voice);
    void onPresence(const QString &chat, bool online, qint64 lastSeen);
    void onConnected();
    void typingIdle();
    void composePaused();
    void onIncoming();
    void onFullscreenChanged();
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onPhoto(const QString &jid, const QString &path);

private:
    void takeGroupInfo(QVariantMap info);
    void sendOpen();
    void askGroupInfo();
    void markRead();
    void setTyping(const QString &text);
    void setComposing(bool on);

    QPointer<EngineLink> m_engine;
    MessageModel *m_messages;
    QString m_jid;
    QString m_name;
    QString m_quoteId;
    QString m_editId;
    QString m_voicePath; // the WAV being recorded
    QString m_replyName;
    QString m_replyText;
    QVariantMap m_groupInfo;
    QString m_infoId; // the group.info request we wait for
    QString m_changeId; // the group.participants request we wait for
    QString m_participants; // "N participants", the status line of a group
    QStringList m_pending;
    bool m_group;
    bool m_unseen;   // a message arrived that no one has seen yet
    bool m_atBottom; // the list shows the newest message
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
