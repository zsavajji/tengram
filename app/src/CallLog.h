#ifndef CALLLOG_H
#define CALLLOG_H

#include <bb/system/LocaleHandler>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class EngineLink;

// The Calls page's list, made by QML in the page's attachedObjects: the request's owner dies with the
// page, so no connection to the engine outlives it.
class CallLog : public QObject
{
    Q_OBJECT
    // The EngineLink, set as Sessions' link is.
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    // The calls the engine has given so far, a row for each with its lines ready for the page, newest first:
    // {chat, name, out, missed, video, detail, time}. Empty until the first page comes.
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    // True once a list has come.
    Q_PROPERTY(bool loaded READ loaded NOTIFY listChanged)
    // True while the last reply said there is more history after it (askMore() gets it).
    Q_PROPERTY(bool more READ more NOTIFY listChanged)
    // The index of the first row the last reply added: 0 when it replaced the list, else the old size. The rows
    // before it are unchanged.
    Q_PROPERTY(int from READ from NOTIFY listChanged)
public:
    explicit CallLog(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    QVariantList list() const { return m_list; }
    bool loaded() const { return m_loaded; }
    bool more() const { return m_more; }
    int from() const { return m_from; }

    // Asks the engine for the newest calls (calls.list); its reply replaces the list. Nothing without an engine.
    Q_INVOKABLE void ask();
    // Asks for the page after the list's end; its reply is added to the list. Nothing unless more is true and no
    // request is pending. (Not named more(): QML would find the property, not the function.)
    Q_INVOKABLE void askMore();

signals:
    void listChanged();

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onFailure();
    void onConnected();

private:
    QVariantMap row(const QVariantMap &call) const;
    QString whenText(qint64 stamp) const;

    QPointer<EngineLink> m_engine;
    QVariantList m_list;
    bool m_loaded;
    bool m_wanted; // the page asked; asked again when the engine connects, until it answers
    bool m_more; // the last reply had a next page
    int m_from; // the first row the last reply added
    QString m_next; // the offset of the next page, the last reply's
    QHash<QString, QString> m_sent; // the requests we wait for: id to cmd
    QSet<QString> m_moreIds; // the ids in m_sent that ask for a next page; the others ask for the newest
    bb::system::LocaleHandler m_region;
};

#endif
