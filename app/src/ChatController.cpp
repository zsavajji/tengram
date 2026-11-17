#include "ChatController.h"

#include "EngineLink.h"
#include "colors.h"
#include "MessageModel.h"
#include "Waveform.h"

#include <bb/cascades/Application>
#include <bb/cascades/Window>
#include <bb/system/Clipboard>
#include <bb/utility/i18n/Formatting>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QLocale>

using namespace bb::utility::i18n;

// On while recording: the idle lock minimises the app, which drops the recording (Q10,
// 2026-11-01: a note was lost 34 s in). Checked, since the page may outlive the window at exit.
static void keepAwake(bool on)
{
    bb::cascades::Application *app = bb::cascades::Application::instance();
    if (app && app->mainWindow())
        app->mainWindow()->setScreenIdleMode(on ? bb::cascades::ScreenIdleMode::KeepAwake
                                                : bb::cascades::ScreenIdleMode::Normal);
}

ChatController::ChatController(QObject *parent)
    : QObject(parent)
    , m_messages(0)
    , m_group(false)
    , m_unseen(false)
    , m_atBottom(true)
    , m_composing(false)
    , m_region(bb::system::LocaleType::Region)
{
    // WhatsApp's typing has no guaranteed end: clear it after 25 s without an update.
    m_idle.setSingleShot(true);
    m_idle.setInterval(25000);
    connect(&m_idle, SIGNAL(timeout()), this, SLOT(typingIdle()));
    // WhatsApp's typing state times out on the other side: re-send "on" at most every 5 s while
    // typing continues (step 3.2).
    m_composeRepeat.setSingleShot(true);
    m_composeRepeat.setInterval(5000);
    // After 5 s without a key, send "off" so the other side never shows a stale "typing…".
    m_composePause.setSingleShot(true);
    m_composePause.setInterval(5000);
    connect(&m_composePause, SIGNAL(timeout()), this, SLOT(composePaused()));
}

ChatController::~ChatController()
{
    commitDelete(); // popping the page while the Undo toast shows still deletes
    cancelVoice(); // a recording the page never ended: the engine drops it and the WAV
    if (m_engine && m_engine->connected() && !m_jid.isEmpty()) {
        QVariantMap args;
        args["chat"] = m_jid;
        if (m_composing) {
            QVariantMap off = args;
            off["on"] = false;
            m_engine->request("chat.typing", off);
        }
        m_engine->request("chat.close", args);
    }
    if (m_engine && !m_jid.isEmpty() && m_engine->openChat() == m_jid)
        m_engine->setOpenChat(QString());
}

QObject *ChatController::link() const
{
    return m_engine;
}

// At app exit the scene (and this) may outlive the EngineLink on main()'s stack: a QPointer.
void ChatController::setLink(QObject *link)
{
    m_engine = qobject_cast<EngineLink *>(link);
}

bb::cascades::DataModel *ChatController::messages() const
{
    return m_messages;
}

QString ChatController::status() const
{
    if (!m_typing.isEmpty())
        return m_typing;
    return m_group ? m_participants : m_presence;
}

QVariant ChatController::playedWave(const QVariant &values, int bars) const
{
    QByteArray bytes;
    foreach (const QVariant &v, values.toList())
        bytes.append(char(v.toInt()));
    return Waveform::image(bytes, bars);
}

QVariantMap ChatController::groupInfo() const
{
    return m_groupInfo;
}

void ChatController::askGroupInfo()
{
    if (!m_group)
        return;
    QVariantMap args;
    args["chat"] = m_jid;
    m_infoId = m_engine->request("group.info", args);
}

void ChatController::changeParticipant(const QString &jid, const QString &action)
{
    if (!m_group || !m_engine)
        return;
    QVariantMap args;
    args["chat"] = m_jid;
    args["jid"] = jid;
    args["action"] = action;
    m_changeId = m_engine->request("group.participants", args);
}

void ChatController::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    if (cmd == "voice.play") {
        emit voiceReady(id, result.toMap()["path"].toString());
        return;
    }
    if (cmd == "group.participants" && id == m_changeId) {
        askGroupInfo();
        return;
    }
    if (cmd != "group.info" || id != m_infoId)
        return;
    takeGroupInfo(result.toMap());
}

void ChatController::open(const QVariantMap &chat)
{
    if (m_messages || !m_engine)
        return;
    m_jid = chat["jid"].toString();
    m_name = chat["name"].toString();
    m_group = chat["kind"].toInt() == 1;
    m_messages = new MessageModel(m_engine, m_jid, m_group, this);
    emit messagesChanged();
    connect(m_engine, SIGNAL(typing(QString, QString, bool, bool)), this,
            SLOT(onTyping(QString, QString, bool, bool)));
    connect(m_engine, SIGNAL(presence(QString, bool, qint64)), this,
            SLOT(onPresence(QString, bool, qint64)));
    connect(m_engine, SIGNAL(photo(QString, QString)), this, SLOT(onPhoto(QString, QString)));
    connect(m_engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
    connect(m_engine, SIGNAL(fullscreenChanged()), this, SLOT(onFullscreenChanged()));
    connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
            SLOT(onReplied(QString, QString, QVariant)));
    connect(m_messages, SIGNAL(incoming()), this, SLOT(onIncoming()));
    // From chats.db, not the row we were given: a contact from the picker has no unread, and
    // a notification tap (step 5.1) won't either. Marked unread is -1 (step 5.3).
    m_unseen = m_messages->unread() != 0;
    m_engine->setOpenChat(m_jid);
    sendOpen();
    askGroupInfo();
    markRead();
}

void ChatController::setAtBottom(bool atBottom)
{
    m_atBottom = atBottom;
    if (atBottom)
        markRead();
}

void ChatController::onIncoming()
{
    m_unseen = true;
    markRead();
}

// A message is read only when its card can be seen: at the bottom of a chat that is on screen.
// This is what keeps a badge from showing for a chat that's open.
void ChatController::markRead()
{
    if (!m_unseen || !m_atBottom || !m_engine || !m_engine->connected() || !m_engine->fullscreen())
        return;
    m_unseen = false;
    QVariantMap args;
    args["chat"] = m_jid;
    m_engine->request("chat.read", args);
}

// Minimised: stop our typing and unsubscribe; back: subscribe again, and read what came meanwhile.
void ChatController::onFullscreenChanged()
{
    // A recording doesn't survive minimising (step 4.4b); the page resets its recorder.
    if (m_engine && !m_engine->fullscreen() && !m_voicePath.isEmpty()) {
        cancelVoice();
        emit voiceCancelled();
    }
    if (!m_engine || !m_engine->connected())
        return;
    if (m_engine->fullscreen()) {
        sendOpen();
        markRead();
        return;
    }
    setComposing(false);
    QVariantMap args;
    args["chat"] = m_jid;
    m_engine->request("chat.close", args);
}

void ChatController::nearTop(const QVariantList &firstVisible)
{
    if (m_messages)
        m_messages->nearTop(firstVisible);
}

bool ChatController::send(const QString &text, const QVariantList &mentions)
{
    if (!m_engine || !m_engine->connected())
        return false;
    setComposing(false);
    QVariantMap args;
    args["chat"] = m_jid;
    args["text"] = text;
    if (!m_quoteId.isEmpty())
        args["quote"] = m_quoteId;
    if (!m_editId.isEmpty())
        args["id"] = m_editId;
    if (!mentions.isEmpty())
        args["mentions"] = mentions;
    m_engine->request(m_editId.isEmpty() ? "msg.send" : "msg.edit", args);
    cancelReply();
    return true;
}

QString ChatController::startVoice()
{
    if (!m_engine || !m_engine->connected())
        return QString();
    QDir home(QDir::homePath());
    home.mkpath("voice");
    cancelVoice();
    m_voicePath = home.absoluteFilePath(
        QString("voice/rec-%1.wav").arg(QDateTime::currentMSecsSinceEpoch()));
    QVariantMap args;
    args["chat"] = m_jid;
    args["path"] = m_voicePath;
    m_engine->request("voice.start", args);
    keepAwake(true);
    return m_voicePath;
}

void ChatController::sendVoice()
{
    if (m_voicePath.isEmpty() || !m_engine)
        return;
    QVariantMap args;
    args["chat"] = m_jid;
    args["path"] = m_voicePath;
    if (!m_quoteId.isEmpty())
        args["quote"] = m_quoteId;
    m_engine->request("voice.send", args);
    m_voicePath.clear();
    keepAwake(false);
    cancelReply();
}

void ChatController::cancelVoice()
{
    if (m_voicePath.isEmpty())
        return;
    if (m_engine) {
        QVariantMap args;
        args["path"] = m_voicePath;
        m_engine->request("voice.cancel", args);
    }
    m_voicePath.clear();
    keepAwake(false);
}

QString ChatController::replyName() const
{
    return m_replyName;
}

QString ChatController::replyText() const
{
    return m_replyText;
}

void ChatController::reply(const QVariantMap &msg)
{
    m_quoteId = msg["id"].toString();
    if (msg["out"].toBool())
        m_replyName = tr("You");
    else if (!msg["sender"].toString().isEmpty())
        m_replyName = msg["sender"].toString();
    else
        m_replyName = m_name;
    m_replyText = msg["text"].toString();
    if (m_replyText.isEmpty()) // a photo or file with no caption has its summary (MediaData)
        m_replyText = msg.value("summary", msg["label"]).toString();
    emit replyChanged();
}

void ChatController::edit(const QVariantMap &msg)
{
    m_quoteId.clear();
    m_editId = msg["id"].toString();
    m_replyName = tr("You");
    m_replyText = msg["text"].toString();
    emit replyChanged();
}

void ChatController::cancelReply()
{
    if (m_quoteId.isEmpty() && m_editId.isEmpty())
        return;
    m_quoteId.clear();
    m_editId.clear();
    m_replyName.clear();
    m_replyText.clear();
    emit replyChanged();
}

void ChatController::copy(const QString &text)
{
    bb::system::Clipboard clipboard;
    clipboard.clear();
    clipboard.insert("text/plain", text.toUtf8());
}

void ChatController::deleteForMe(const QString &id)
{
    if (!m_messages)
        return;
    m_pending << id;
    m_messages->hide(QStringList(id));
    if (id == m_quoteId)
        cancelReply();
}

void ChatController::undoDelete()
{
    if (m_pending.isEmpty() || !m_messages)
        return;
    m_messages->unhide(m_pending);
    m_pending.clear();
}

// A committed id stays hidden for the page's life, so the card doesn't flash back before the
// engine's msgs event removes it.
// ponytail: with the engine down the request fails and the card stays hidden until the page is
// reopened; the failure shows in engine.failure.
void ChatController::commitDelete()
{
    if (m_pending.isEmpty())
        return;
    if (m_engine) {
        QVariantList ids;
        foreach (const QString &id, m_pending)
            ids << id;
        QVariantMap args;
        args["chat"] = m_jid;
        args["ids"] = ids;
        args["everyone"] = false;
        m_engine->request("msg.delete", args);
    }
    m_pending.clear();
}

bool ChatController::deleteForEveryone(const QString &id)
{
    if (!m_engine || !m_engine->connected())
        return false;
    QVariantMap args;
    args["chat"] = m_jid;
    args["ids"] = QVariantList() << id;
    args["everyone"] = true;
    m_engine->request("msg.delete", args);
    return true;
}

void ChatController::composing(const QString &text)
{
    if (text.isEmpty()) {
        setComposing(false);
        return;
    }
    if (!m_composing || !m_composeRepeat.isActive())
        setComposing(true);
    m_composePause.start();
}

void ChatController::composePaused()
{
    setComposing(false);
}

void ChatController::setComposing(bool on)
{
    QVariantMap args;
    args["chat"] = m_jid;
    args["on"] = on;
    if (on) {
        if (m_engine && m_engine->connected())
            m_engine->request("chat.typing", args);
        m_composing = true;
        m_composeRepeat.start();
        return;
    }
    if (!m_composing)
        return;
    m_composing = false;
    m_composeRepeat.stop();
    m_composePause.stop();
    if (m_engine && m_engine->connected())
        m_engine->request("chat.typing", args);
}

void ChatController::sendOpen()
{
    QVariantMap args;
    args["chat"] = m_jid;
    m_engine->request("chat.open", args);
}

// An engine restart loses the presence subscription, and at a cold start the first chat.open
// failed with no_engine.
void ChatController::onConnected()
{
    if (m_engine && m_engine->connected()) {
        sendOpen();
        askGroupInfo();
        markRead();
    }
}

void ChatController::setTyping(const QString &text)
{
    if (text == m_typing)
        return;
    m_typing = text;
    emit statusChanged();
}

// ponytail: in a group only the last one typing shows; keep a set of names if it matters.
void ChatController::onTyping(const QString &chat, const QString &who, bool on, bool voice)
{
    if (chat != m_jid)
        return;
    if (!on) {
        m_idle.stop();
        setTyping(QString());
        return;
    }
    m_idle.start();
    if (m_group)
        setTyping(voice ? tr("%1 is recording audio…").arg(who) : tr("%1 is typing…").arg(who));
    else
        setTyping(voice ? tr("recording audio…") : tr("typing…"));
}

void ChatController::typingIdle()
{
    setTyping(QString());
}

// A group's status line is "N participants", set when group.info arrives.
void ChatController::onPresence(const QString &chat, bool online, qint64 lastSeen)
{
    if (chat != m_jid || m_group)
        return;
    QString text;
    if (online) {
        text = tr("online");
    } else if (lastSeen > 0) {
        const QLocale locale = m_region.locale();
        const QDateTime when = QDateTime::fromTime_t(lastSeen);
        const QString time = locale.toString(when.time(), timeFormat(locale, DateFormat::Short));
        const int age = when.date().daysTo(QDate::currentDate());
        if (age == 0)
            text = tr("last seen today at %1").arg(time);
        else if (age == 1)
            text = tr("last seen yesterday at %1").arg(time);
        else
            text = tr("last seen %1 at %2").arg(locale.toString(when.date(), QLocale::ShortFormat), time);
    }
    if (text != m_presence) {
        m_presence = text;
        emit statusChanged();
    }
}
