#ifndef ENGINELINK_H
#define ENGINELINK_H

#include <bb/system/InvokeManager>
#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantMap>

// The UI's side of the engine link, following docs/protocol.md. It reconnects and restarts the
// engine by itself: it retries every 500 ms and sends START at most every 1 s.
class EngineLink : public QObject
{
    Q_OBJECT
    // True once the engine answered hello; false while it's down or restarting.
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    // The link body of docs/protocol.md (state, conn, qr, code, me, name), as last received.
    Q_PROPERTY(QVariantMap link READ link NOTIFY linkChanged)
    // The connection to WhatsApp: offline, connecting or ready.
    Q_PROPERTY(QString conn READ conn NOTIFY connChanged)
    // True when the last link state was ready. Kept in settings.ini, so a cold start shows the
    // chat list at once, before the engine answers (PLAN.md §12).
    Q_PROPERTY(bool linked READ linked NOTIFY linkedChanged)
    // The last request that failed, {cmd, code, message}: the engine's error code, or "no_engine"
    // when the engine was down or went down before replying; message is English, for logs. A
    // property so that pages bind to it and nothing stays connected to a destroyed page. Empty
    // until a failure; request() of the same cmd clears it.
    Q_PROPERTY(QVariantMap failure READ failure NOTIFY failureChanged)
    // The live call, the last `call` event's {id, chat, out, state, reason, since} (docs/plan/phase7.md);
    // empty until the first call. After state "ended" it stays until the next call.
    Q_PROPERTY(QVariantMap call READ call NOTIFY callChanged)
public:
    explicit EngineLink(QObject *parent = 0);
    ~EngineLink();

    // The app's one InvokeManager: it starts the engine, and OpenRequests hears notification taps on it.
    bb::system::InvokeManager *invoker() const { return m_invoker; }

    // The phone's language ("it"), sent with prefs.set on every connect so the engine words previews in it.
    void setLanguage(const QString &language) { m_language = language; }

    bool connected() const;
    QVariantMap link() const;
    QString conn() const;
    bool linked() const;
    QVariantMap failure() const;
    QVariantMap call() const;
    // Where the engine listens and the token its hello wants, as last read from engine.json (0 and "" before the first read): the audio link (CallAudio) says hello itself.
    int port() const { return m_port; }
    QString token() const { return m_token; }
    // Whether the app is on screen full size; thumbnail and invisible both mean false.
    bool fullscreen() const;
    QString openChat() const;
    // The chat page on screen, "" for none. Sends ui.state.
    void setOpenChat(const QString &jid);

    // Connects, starting the engine first when it isn't running. Call once, from main().
    void start();


    // Sends a request (docs/protocol.md "Requests"); a failure shows in failure. Returns the
    // request id, "" when the engine isn't connected.
    Q_INVOKABLE QString request(const QString &cmd, const QVariantMap &args);
    // request() for something the user didn't ask for (a sticker fetched as its row is drawn): a
    // failure, and an engine that's down, are logged but never put in failure, so no page shows them.
    // Returns the request id, "" when the engine isn't connected (then nothing was sent).
    QString requestQuiet(const QString &cmd, const QVariantMap &args);

signals:
    void connectedChanged();
    void linkChanged();
    void connChanged();
    // A chats event: these rows of chats.db changed.
    void chatsChanged(const QStringList &jids);
    // A msgs event: these rows of the chat's messages changed or disappeared.
    void msgsChanged(const QString &chat, const QStringList &ids);
    // A stories event: a row of the stories table was added, changed or dropped.
    void storiesChanged();
    // A storyviews event: someone viewed a story of ours (a row of story_views was added).
    void storyViewsChanged();
    // A storyop event: story.post or story.delete (cmd "post" or "delete") of this story ended, ok or not.
    void storyOp(const QString &cmd, const QString &id, bool ok);
    // A typing event: who ("" in 1:1) started or stopped typing in the chat; voice while recording.
    void typing(const QString &chat, const QString &who, bool on, bool voice);
    // A presence event; lastSeen in unix seconds, 0 when hidden.
    void presence(const QString &chat, bool online, qint64 lastSeen);
    // A photo event: a group member's small picture arrived, "" when they have none.
    void photo(const QString &jid, const QString &path);
    // A successful reply to a request, hello's excepted: its id (request()'s return value), the
    // cmd and the result. For C++ objects only: a connection ends with its object, so nothing
    // stays connected to a destroyed page (as with failure).
    void replied(const QString &id, const QString &cmd, const QVariant &result);
    void linkedChanged();
    void failureChanged();
    void callChanged();
    // Emitted after ui.state went out, so it reaches the engine before a chat.close/chat.open.
    void fullscreenChanged();

private slots:
    void tryConnect();
    void socketConnected();
    void readLines();
    void lost();
    void invokeFinished();
    void onFullscreen();
    void onNotFullscreen();

private:
    void startEngine();
    QString post(const QString &cmd, const QVariantMap &args);
    void send(const QVariantMap &map);
    void setLink(const QVariantMap &link);
    void setConn(const QString &conn);
    void setFullscreen(bool on);
    void sendUiState();
    void fail(const QString &cmd, const QString &code, const QString &message);

    QTcpSocket m_socket;
    QTimer m_retry;
    int m_retryDelay; // ms; see RetryMinMs in EngineLink.cpp
    bb::system::InvokeManager *m_invoker;
    QElapsedTimer m_invokedAt;
    QString m_token;
    int m_port;
    QString m_language;
    int m_counter;
    QString m_helloId;
    QHash<QString, QString> m_pending;
    QSet<QString> m_quiet; // the ids in m_pending that requestQuiet sent
    bool m_connected;
    QVariantMap m_link;
    QString m_conn;
    bool m_linked;
    QVariantMap m_failure;
    QVariantMap m_call;
    bool m_fullscreen;
    QString m_openChat;
};

#endif
