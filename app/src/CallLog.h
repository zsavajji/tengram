#ifndef CALLLOG_H
#define CALLLOG_H

#include <bb/system/LocaleHandler>
#include <QHash>
#include <QObject>
#include <QPointer>
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
    // The last calls.list result, a row for each call with its lines ready for the page, newest first:
    // {chat, name, out, missed, detail, time}. Empty until it comes.
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    // True once a list has come.
    Q_PROPERTY(bool loaded READ loaded NOTIFY listChanged)
public:
    explicit CallLog(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    QVariantList list() const { return m_list; }
    bool loaded() const { return m_loaded; }

    // Asks the engine for the calls (calls.list); nothing without an engine.
    Q_INVOKABLE void ask();

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
    QHash<QString, QString> m_sent; // the requests we wait for: id to cmd
    bb::system::LocaleHandler m_region;
};

#endif
