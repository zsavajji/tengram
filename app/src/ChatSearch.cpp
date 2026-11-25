#include "ChatSearch.h"

#include "ChatListModel.h"
#include "EngineLink.h"
#include "colors.h"

#include <QLocale>

ChatSearch::ChatSearch(QObject *parent)
    : QObject(parent)
    , m_tab("chats")
    , m_results(new bb::cascades::ArrayDataModel(this))
    , m_top(new bb::cascades::ArrayDataModel(this))
    , m_chatRows(0)
    , m_msgFirst(false)
    , m_searched(false)
{
    // Typing waits for a pause: the engine asks Telegram's servers for each text.
    m_timer.setSingleShot(true);
    m_timer.setInterval(300);
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(send()));
}

QObject *ChatSearch::link() const
{
    return m_engine;
}

// A QPointer, as Privacy's: at app exit the page may outlive the EngineLink.
void ChatSearch::setLink(QObject *link)
{
    m_engine = qobject_cast<EngineLink *>(link);
    if (m_engine) {
        connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
                SLOT(onReplied(QString, QString, QVariant)));
        connect(m_engine, SIGNAL(failureChanged()), this, SLOT(onFailure()));
        connect(m_engine, SIGNAL(connectedChanged()), this, SLOT(onEngine()));
        connect(m_engine, SIGNAL(linkedChanged()), this, SLOT(onEngine()));
        connect(m_engine, SIGNAL(photo(QString, QString)), this, SLOT(onPhoto(QString, QString)));
    }
}

// What is out, and what is shown, is for another tab or text: a reply for it is dropped, and the rows of the other tab go.
void ChatSearch::setTab(const QString &tab)
{
    if (tab == m_tab)
        return;
    m_tab = tab;
    m_id.clear();
    m_msgId.clear();
    m_next.clear();
    m_searched = false;
    m_results->clear();
    m_chatRows = 0;
    m_top->clear();
    send();
}

void ChatSearch::setQuery(const QString &query)
{
    const QString text = query.trimmed();
    if (text == m_query)
        return;
    const bool modeChanged = text.isEmpty() != m_query.isEmpty();
    m_query = text;
    m_id.clear();
    m_msgId.clear();
    m_next.clear();
    m_searched = false;
    // The suggestions and the results are two lists: the one shown is not for the other's text.
    if (modeChanged) {
        m_results->clear();
        m_chatRows = 0;
        m_top->clear();
    }
    if (text.isEmpty()) {
        send();
    } else {
        m_timer.start();
        emit stateChanged();
    }
}

void ChatSearch::ask()
{
    send();
}

// A page opened while the engine (re)starts asks before it's there: the search is sent again on connecting.
void ChatSearch::onEngine()
{
    if (!m_searched && m_id.isEmpty() && !m_timer.isActive())
        send();
}

// No engine, or no account yet: nothing is sent, and the page shows no rows.
void ChatSearch::send()
{
    m_timer.stop();
    if (m_engine && m_engine->connected() && m_engine->linked()) {
        QVariantMap args;
        args["query"] = m_query;
        args["tab"] = m_tab;
        m_id = m_engine->request("search.chats", args);
        // Telegram's message search follows the chats' on the Chats tab, as in its app: another request, which comes
        // back on its own and is put under the chats.
        m_next.clear();
        if (!m_query.isEmpty() && m_tab == "chats") {
            QVariantMap find;
            find["query"] = m_query;
            m_msgFirst = true;
            m_msgId = m_engine->request("search.messages", find);
        }
    }
    emit stateChanged();
}

// The list reached its end: the next page of the messages, from the offset the last one gave.
void ChatSearch::more()
{
    if (m_tab != "chats" || m_query.isEmpty() || !m_msgId.isEmpty() || m_next.isEmpty() || !m_engine ||
        !m_engine->connected() || !m_engine->linked())
        return;
    QVariantMap args;
    args["query"] = m_query;
    args["offset"] = m_next;
    m_msgFirst = false;
    m_msgId = m_engine->request("search.messages", args);
    emit stateChanged();
}

void ChatSearch::remember(const QString &jid)
{
    if (!m_engine)
        return;
    QVariantMap args;
    args["chat"] = jid;
    m_engine->requestQuiet("search.recentAdd", args);
}

void ChatSearch::clearRecent()
{
    if (m_engine)
        m_engine->requestQuiet("search.recentClear", QVariantMap());
    m_results->clear();
    m_chatRows = 0;
    emit stateChanged();
}

// What ContactRow and the strip's tile draw: the tile's initials, and the colour a chat has in every list.
QVariantMap ChatSearch::base(const QVariantMap &chat) const
{
    QVariantMap row;
    row["jid"] = chat["jid"];
    row["kind"] = chat["kind"];
    row["name"] = chat["name"];
    row["initials"] = initials(chat["name"].toString());
    row["color"] = pickColor(chat["jid"].toString(), kSenders, kSenderCount);
    if (!chat["photo"].toString().isEmpty())
        row["photo"] = chat["photo"];
    row["username"] = chat["username"];
    row["member"] = chat["member"];
    return row;
}

// The line under a name: a person's or a bot's @username, a group's members and a channel's subscribers (a 0 count is
// left out), with the @username of a public one after them.
QString ChatSearch::sub(const QVariantMap &chat) const
{
    const QString kind = chat["kind"].toString();
    const QString user = chat["username"].toString().isEmpty() ? QString() : "@" + chat["username"].toString();
    const int count = chat["count"].toInt();
    if (kind == "person")
        return user;
    if (kind == "bot")
        return user.isEmpty() ? tr("bot") : user;
    QString text = count <= 0 ? QString() : kind == "channel" ? tr("%n subscribers", "", count) : tr("%n members", "", count);
    if (!user.isEmpty())
        text += (text.isEmpty() ? "" : ", ") + user;
    return text;
}

// A header row and the chats under it; nothing for no chats.
void ChatSearch::addRows(QVariantList &rows, const QString &header, bool clear, const QVariantList &chats) const
{
    if (chats.isEmpty())
        return;
    QVariantMap head;
    head["header"] = header;
    head["clear"] = clear;
    rows << QVariant(head);
    foreach (const QVariant &c, chats) {
        QVariantMap row = base(c.toMap());
        row["sub"] = sub(c.toMap());
        rows << QVariant(row);
    }
}

// The line under a message found: the text around the word that matched, so the word shows in the one line, as Telegram's
// "…ovviamente sotto claude ma è mega"; the whole query when the text has it, else its first word (Telegram's matching is
// fuzzier than ours).
static QString snippet(const QString &text, const QString &query)
{
    QString t = text;
    t.replace('\n', ' ').replace('\r', ' ').replace('\t', ' ');
    int pos = t.indexOf(query, 0, Qt::CaseInsensitive);
    if (pos < 0)
        pos = t.indexOf(query.section(' ', 0, 0), 0, Qt::CaseInsensitive);
    if (pos > 30) {
        const int space = t.lastIndexOf(' ', pos - 15);
        t = QString(QChar(0x2026)) + t.mid(space >= 0 ? space + 1 : pos - 15);
    }
    return t.left(120);
}

// The message rows go: what follows the chats' rows.
void ChatSearch::dropMessages()
{
    while (m_results->size() > m_chatRows)
        m_results->removeAt(m_results->size() - 1);
}

// A page of search.messages: under the chats, behind a "Messages" header when it is the first page. Each row opens its chat
// at the message (SearchPage.open); the engine gives the chat's name and picture, and the sender's name in a group.
void ChatSearch::takeMessages(const QVariantMap &r)
{
    const bool first = m_msgFirst;
    m_msgId.clear();
    m_next = r["next"].toString();
    if (first)
        dropMessages();
    const QVariantList found = r["messages"].toList();
    if (!found.isEmpty()) {
        QVariantList rows;
        if (first) {
            QVariantMap head;
            head["header"] = tr("Messages");
            head["clear"] = false;
            rows << QVariant(head);
        }
        const QLocale locale;
        const RowClock clock(locale);
        foreach (const QVariant &f, found) {
            const QVariantMap m = f.toMap();
            const QString name = m["name"].toString();
            const QString sender = m["sender"].toString();
            QVariantMap row;
            row["msg"] = true;
            row["jid"] = m["chat"];
            row["id"] = m["id"];
            row["name"] = name;
            row["initials"] = initials(name);
            row["color"] = pickColor(m["chat"].toString(), kSenders, kSenderCount);
            if (!m["photo"].toString().isEmpty())
                row["photo"] = m["photo"];
            row["sub"] = (sender.isEmpty() ? QString() : sender + ": ") + snippet(m["text"].toString(), m_query);
            QVariantMap t;
            clock.place(&t, m["ts"], false);
            row["time"] = t["time"];
            rows << QVariant(row);
        }
        m_results->append(rows);
    }
    emit stateChanged();
}

void ChatSearch::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    if (cmd == "search.messages" && id == m_msgId) {
        takeMessages(result.toMap());
        return;
    }
    if (cmd != "search.chats" || id != m_id)
        return;
    m_id.clear();
    const QVariantMap r = result.toMap();
    QVariantList rows;
    QVariantList top;
    if (m_query.isEmpty()) {
        foreach (const QVariant &c, r["top"].toList()) {
            QVariantMap row = base(c.toMap());
            row["first"] = row["name"].toString().section(' ', 0, 0);
            top << QVariant(row);
        }
        addRows(rows, tr("Recent"), true, r["recent"].toList());
    } else {
        addRows(rows, m_tab == "channels" ? tr("Channels") : m_tab == "apps" ? tr("Apps") : tr("Chats"), false,
                r["local"].toList());
        addRows(rows, tr("Global search"), false, r["global"].toList());
    }
    if (m_query.isEmpty()) {
        m_results->clear();
        m_results->append(rows);
    } else {
        // Only the chats' rows: the messages under them stay (or come after).
        for (int i = 0; i < m_chatRows; ++i)
            m_results->removeAt(0);
        m_results->insert(0, rows);
    }
    m_chatRows = rows.size();
    m_top->clear();
    m_top->append(top);
    m_searched = true;
    emit stateChanged();
}

// The failure carries no id: ours, when there is one out, is the search.chats or the search.messages that failed.
void ChatSearch::onFailure()
{
    if (!m_engine)
        return;
    const QString cmd = m_engine->failure().value("cmd").toString();
    if (cmd == "search.chats" && !m_id.isEmpty()) {
        m_id.clear();
    } else if (cmd == "search.messages" && !m_msgId.isEmpty()) {
        m_msgId.clear();
        if (m_msgFirst)
            dropMessages(); // the ones under are for an older text
    } else {
        return;
    }
    emit stateChanged();
}

static void setPhoto(bb::cascades::ArrayDataModel *model, const QString &jid, const QString &path)
{
    for (int i = 0; i < model->size(); ++i) {
        QVariantMap row = model->value(i).toMap();
        if (row["jid"].toString() == jid) {
            row["photo"] = path;
            model->replace(i, row);
        }
    }
}

// A picture the engine fetched for a person or a bot: replaced in its rows alone, so the lists don't jump.
void ChatSearch::onPhoto(const QString &jid, const QString &path)
{
    setPhoto(m_results, jid, path);
    setPhoto(m_top, jid, path);
}
