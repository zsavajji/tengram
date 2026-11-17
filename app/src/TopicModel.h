#ifndef TOPICMODEL_H
#define TOPICMODEL_H

#include <bb/cascades/DataModel>
#include <bb/system/LocaleHandler>
#include <QList>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantMap>

class EngineLink;

// The topics of one forum (docs/protocol.md, Forum topics): the chat's rows of chats.db's `topics`, pinned first and then by
// TDLib's order, the General topic left out when it is hidden. The model is flat ([i]) and every item is "item": a row of
// ChatRows (the chat list's), a map of topic, name, initials ("#" for General, else the name's first letter), color
// ("#RRGGBB", "" for none: the row takes the accent), preview, time (as a pinned chat's in the list: the clock today, the
// day within a week, else the date), unread, muted, pinned, closed, and out and photo, which a topic has none of.
// Made by QML in the page's attachedObjects, so it dies with the page.
class TopicModel : public bb::cascades::DataModel
{
    Q_OBJECT
    // The EngineLink, set as ChatController's link is.
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    // True from open() until topics.list has answered: the page shows a spinner while there is nothing to list.
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // True when there is no topic to show.
    Q_PROPERTY(bool empty READ empty NOTIFY emptyChanged)
public:
    explicit TopicModel(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    bool loading() const { return m_loading; }
    bool empty() const { return m_rows.isEmpty(); }

    int childCount(const QVariantList &indexPath);
    bool hasChildren(const QVariantList &indexPath);
    QString itemType(const QVariantList &indexPath);
    QVariant data(const QVariantList &indexPath);

    // Shows the cached topics of the forum (a chat list row's jid) and asks the engine for its list (topics.list).
    Q_INVOKABLE void open(const QString &chat);
    // Marks every topic with unread messages read (chat.read with its topic).
    Q_INVOKABLE void readAll();

signals:
    void loadingChanged();
    void emptyChanged();

public slots:
    // Reads the rows again (the page's uncovered() calls it too).
    void reload();

private slots:
    void onTopics(const QString &chat);
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onConnected();

private:
    void ask();

    QPointer<EngineLink> m_engine;
    QString m_chat;
    QString m_id; // the topics.list request we wait for
    bool m_loading;
    QList<QVariantMap> m_rows;
    QTimer m_soon; // the engine sends a topics event every 50 ms while it lists: one read for a burst
    bb::system::LocaleHandler m_region;
};

#endif
