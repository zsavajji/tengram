#include "Privacy.h"

#include "EngineLink.h"
#include "colors.h"

#include <QDateTime>
#include <QLocale>
#include <QStringList>

// The most blocked people read, 50 a call (blocked.list's page); the count shown is Telegram's.
static const int kBlockedMax = 500;

// The requests that only ask: a page shows their failure from the link's, and they are asked again on connecting.
static bool isAsk(const QString &cmd)
{
    return cmd == "privacy.get" || cmd == "blocked.list" || cmd == "websites.list";
}

// The changes whose result is in privacy.get: asked again when they are done, or have failed (so a drop-down goes back).
static bool altersInfo(const QString &cmd)
{
    return cmd == "privacy.set" || cmd == "autodelete.set" || cmd == "account.ttl" || cmd == "topchats.set"
        || cmd == "contacts.clearImported";
}

Privacy::Privacy(QObject *parent)
    : QObject(parent)
    , m_blocked(new bb::cascades::ArrayDataModel(this))
    , m_blockedTotal(0)
    , m_blockedLoaded(false)
    , m_blockedOffset(0)
    , m_websitesLoaded(false)
    , m_wantInfo(false)
    , m_wantBlocked(false)
    , m_wantWebsites(false)
    , m_region(bb::system::LocaleType::Region)
{
}

QObject *Privacy::link() const
{
    return m_engine;
}

// A QPointer, as Profile's: at app exit the page may outlive the EngineLink.
void Privacy::setLink(QObject *link)
{
    m_engine = qobject_cast<EngineLink *>(link);
    if (m_engine) {
        connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
                SLOT(onReplied(QString, QString, QVariant)));
        connect(m_engine, SIGNAL(failureChanged()), this, SLOT(onFailure()));
        connect(m_engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
        connect(m_engine, SIGNAL(photo(QString, QString)), this, SLOT(onPhoto(QString, QString)));
    }
}

void Privacy::setInfo(const QVariantMap &info)
{
    m_info = info;
    emit infoChanged();
}

// A page opened while the engine (re)starts asks before it's there: the request fails with no_engine and nothing
// would ask again. So it's asked again on connecting.
void Privacy::onConnected()
{
    if (!m_engine || !m_engine->connected())
        return;
    if (m_wantInfo && !m_info.contains("rules"))
        ask();
    if (m_wantBlocked && !m_blockedLoaded)
        askBlocked();
    if (m_wantWebsites && !m_websitesLoaded)
        askWebsites();
}

// Every request goes through here: the other pages' Privacy hear the same replies and failures, so what is ours is
// told by its id. Returns the request's id, "" without an engine.
QString Privacy::send(const QString &cmd, const QVariantMap &args)
{
    if (!m_engine)
        return QString();
    const QString id = m_engine->request(cmd, args);
    if (!id.isEmpty())
        m_sent.insert(id, cmd);
    else if (!isAsk(cmd)) // no engine: the reason is in the link's failure
        emit failed(cmd);
    return id;
}

void Privacy::ask()
{
    m_wantInfo = true;
    m_infoId = send("privacy.get");
}

// The rows stay until the first page of the new ask comes, so a list asked again after an unblock does not jump.
void Privacy::askBlocked()
{
    m_wantBlocked = true;
    askBlockedFrom(0);
}

void Privacy::askBlockedFrom(int offset)
{
    QVariantMap args;
    args["offset"] = offset;
    m_blockedOffset = offset;
    m_blockedId = send("blocked.list", args);
}

void Privacy::askWebsites()
{
    m_wantWebsites = true;
    m_websitesId = send("websites.list");
}

void Privacy::setRule(const QString &key, const QString &value)
{
    QVariantMap args;
    args["key"] = key;
    args["value"] = value;
    send("privacy.set", args);
}

void Privacy::setAutoDelete(int secs)
{
    QVariantMap args;
    args["secs"] = secs;
    send("autodelete.set", args);
}

void Privacy::setTtl(int days)
{
    QVariantMap args;
    args["days"] = days;
    send("account.ttl", args);
}

void Privacy::setTopChats(bool on)
{
    QVariantMap args;
    args["on"] = on;
    send("topchats.set", args);
}

void Privacy::clearPayments()
{
    send("payments.clear");
}

void Privacy::clearImported()
{
    send("contacts.clearImported");
}

void Privacy::setPassword(const QString &old, const QString &password, const QString &hint, const QString &email)
{
    QVariantMap args;
    args["old"] = old;
    args["new"] = password;
    args["hint"] = hint;
    args["email"] = email;
    send("password.set", args);
}

void Privacy::setEmail(const QString &password, const QString &email)
{
    QVariantMap args;
    args["password"] = password;
    args["email"] = email;
    send("password.email", args);
}

void Privacy::sendCode(const QString &code)
{
    QVariantMap args;
    args["code"] = code;
    send("password.code", args);
}

void Privacy::resendCode()
{
    send("password.resend");
}

void Privacy::passwordOff(const QString &old)
{
    QVariantMap args;
    args["old"] = old;
    send("password.off", args);
}

void Privacy::unblock(const QString &jid, const QString &kind)
{
    QVariantMap args;
    args["jid"] = jid;
    args["block"] = false;
    args["kind"] = kind;
    send("blocked.set", args);
}

void Privacy::disconnectSite(const QString &id)
{
    QVariantMap args;
    args["id"] = id;
    send("websites.disconnect", args);
}

void Privacy::disconnectAll()
{
    QVariantMap args;
    args["all"] = true;
    send("websites.disconnect", args);
}

// What ContactRow draws: the tile's initials, and the colour the person has in the contact picker.
QVariantMap Privacy::blockedRow(const QVariantMap &p) const
{
    QVariantMap row;
    row["jid"] = p["jid"];
    row["kind"] = p["kind"];
    // The engine has no name for a chat that is not in its list.
    const QString name = p["name"].toString().isEmpty() ? tr("Unknown") : p["name"].toString();
    row["name"] = name;
    row["initials"] = initials(name);
    row["color"] = pickColor(p["jid"].toString(), kSenders, kSenderCount);
    if (!p["photo"].toString().isEmpty())
        row["photo"] = p["photo"];
    return row;
}

// A picture the engine fetched for a blocked user: replaced in its row alone, so the list doesn't jump.
void Privacy::onPhoto(const QString &jid, const QString &path)
{
    for (int i = 0; i < m_blocked->size(); ++i) {
        QVariantMap row = m_blocked->value(i).toMap();
        if (row["kind"].toString() == "user" && row["jid"].toString() == jid) {
            row["photo"] = path;
            m_blocked->replace(i, row);
            break;
        }
    }
}

// The three lines under a website's domain: its bot, the browser and system, and where and when it was last used (the
// address when Telegram has no place for it).
QVariantMap Privacy::websiteRow(const QVariantMap &s) const
{
    QVariantMap row;
    row["id"] = s["id"];
    row["domain"] = s["domain"];
    row["bot"] = s["bot"];
    QStringList device;
    device << s["browser"].toString() << s["platform"].toString();
    device.removeAll(QString());
    row["device"] = device.join(QString::fromUtf8(" · "));
    QStringList place;
    place << (s["location"].toString().isEmpty() ? s["ip"].toString() : s["location"].toString());
    if (s["active"].toLongLong() > 0)
        place << m_region.locale().toString(QDateTime::fromTime_t(s["active"].toLongLong()).date(), QLocale::ShortFormat);
    place.removeAll(QString());
    row["place"] = place.join(QString::fromUtf8(" · "));
    return row;
}

void Privacy::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    if (m_sent.take(id).isEmpty())
        return;
    if (cmd == "privacy.get") {
        if (id != m_infoId)
            return;
        m_info = result.toMap();
        emit infoChanged();
    } else if (cmd == "blocked.list") {
        if (id != m_blockedId)
            return;
        const QVariantMap r = result.toMap();
        const QVariantList people = r["people"].toList();
        if (m_blockedOffset == 0)
            m_blocked->clear();
        foreach (const QVariant &p, people)
            m_blocked->append(QVariant(blockedRow(p.toMap())));
        m_blockedTotal = r["total"].toInt();
        m_blockedLoaded = true;
        emit blockedChanged();
        // The next page, until all are in or the most.
        if (!people.isEmpty() && m_blocked->size() < qMin(m_blockedTotal, kBlockedMax))
            askBlockedFrom(m_blocked->size());
    } else if (cmd == "websites.list") {
        if (id != m_websitesId)
            return;
        m_websites.clear();
        foreach (const QVariant &w, result.toMap()["websites"].toList())
            m_websites << websiteRow(w.toMap());
        m_websitesLoaded = true;
        emit websitesChanged();
    } else {
        if (cmd.startsWith("password.")) {
            m_info["password"] = result.toMap();
            emit infoChanged();
        } else if (altersInfo(cmd)) {
            ask();
        } else if (cmd == "blocked.set") {
            askBlocked();
        } else if (cmd == "websites.disconnect") {
            askWebsites();
        }
        emit done(cmd);
    }
}

// The failure carries no id: the first of our requests with its cmd is the one that failed.
void Privacy::onFailure()
{
    if (!m_engine)
        return;
    const QString cmd = m_engine->failure().value("cmd").toString();
    const QString id = m_sent.key(cmd);
    if (id.isEmpty())
        return;
    m_sent.remove(id);
    // First, so the page reads the reason from the link's failure before anything else is asked.
    emit failed(cmd);
    if (altersInfo(cmd))
        ask();
}
