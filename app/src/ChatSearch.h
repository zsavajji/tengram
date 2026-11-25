#ifndef CHATSEARCH_H
#define CHATSEARCH_H

#include <bb/cascades/ArrayDataModel>
#include <bb/cascades/DataModel>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class EngineLink;

// The search page's lists (SearchPage.qml; docs/protocol.md, search.chats): Telegram's own search, as its Android app
// has it. Made by QML in the page's attachedObjects, as Privacy is: the request's owner dies with the page, so no
// connection to the engine outlives it.
class ChatSearch : public QObject
{
    Q_OBJECT
    // The EngineLink, set as Privacy's link is.
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    // The tab searched: chats, channels or apps. A change searches again.
    Q_PROPERTY(QString tab READ tab WRITE setTab)
    // What the user typed. A change searches 300 ms after the last one, an empty one at once.
    Q_PROPERTY(QString query READ query WRITE setQuery)
    // The list under the field, a row for each ContactRow draws, {jid, kind, name, initials, color, photo, username,
    // member, sub}, under header rows {header, clear}. A model, so that a photo that arrives later replaces its row alone.
    // With no text: the recent chats, under "Recent" (clear: true); with text: the local results, then the global ones. On
    // the Chats tab with text, Telegram's message search follows them (search.messages), under "Messages": rows {msg: true,
    // jid, id, name, initials, color, photo, sub, time}, `sub` the sender's name and the line around the word found; more()
    // adds the next page at the end.
    Q_PROPERTY(bb::cascades::DataModel *results READ results CONSTANT)
    // The strip of suggested people (or channels, or bots) shown with no text: {jid, kind, name, first, initials, color,
    // photo, username, member}. Empty while there is text.
    Q_PROPERTY(bb::cascades::DataModel *top READ top CONSTANT)
    // A search is out (the chats' or the messages', a next page too); true once a reply came for the current text and tab,
    // the messages' as well; the two models have no rows (empty, hasTop).
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool searched READ searched NOTIFY stateChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY stateChanged)
    Q_PROPERTY(bool hasTop READ hasTop NOTIFY stateChanged)
public:
    explicit ChatSearch(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    QString tab() const { return m_tab; }
    void setTab(const QString &tab);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    bb::cascades::DataModel *results() const { return m_results; }
    bb::cascades::DataModel *top() const { return m_top; }
    bool busy() const { return !m_id.isEmpty() || !m_msgId.isEmpty(); }
    bool searched() const { return m_searched && m_msgId.isEmpty(); }
    bool empty() const { return m_results->size() == 0; }
    bool hasTop() const { return m_top->size() > 0; }

    // Searches now (the page's first search); nothing without an engine.
    Q_INVOKABLE void ask();
    // The next page of the messages, asked as the list reaches its end; nothing when none is left or one is out.
    Q_INVOKABLE void more();
    // The chat was opened from a result: Telegram puts it first among the recent ones.
    Q_INVOKABLE void remember(const QString &jid);
    // "Clear all" of the recent chats.
    Q_INVOKABLE void clearRecent();

signals:
    void stateChanged();

private slots:
    void send();
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onFailure();
    void onEngine();
    void onPhoto(const QString &jid, const QString &path);

private:
    QVariantMap base(const QVariantMap &chat) const;
    QString sub(const QVariantMap &chat) const;
    void addRows(QVariantList &rows, const QString &header, bool clear, const QVariantList &chats) const;
    void takeMessages(const QVariantMap &r);
    void dropMessages();

    QPointer<EngineLink> m_engine;
    QString m_tab;
    QString m_query;
    bb::cascades::ArrayDataModel *m_results;
    bb::cascades::ArrayDataModel *m_top;
    QString m_id; // the search.chats we wait for; "" for none, and what was sent for an older text or tab is dropped
    int m_chatRows; // how many rows at the model's start are search.chats's; the "Messages" header and its rows follow
    QString m_msgId; // the search.messages we wait for; "" for none, dropped as m_id is
    bool m_msgFirst; // the one out is the first page: it replaces the message rows, a next one adds to them
    QString m_next; // the offset of the messages' next page; "" when there is none
    bool m_searched;
    QTimer m_timer;
};

#endif
