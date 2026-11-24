#ifndef CHATSDB_H
#define CHATSDB_H

#include <QObject>
#include <QSqlDatabase>
#include <QString>

class EngineLink;

// The UI's one way into chats.db, which is SQLCipher (docs/protocol.md, "Files in the data folder"):
// every model reads it through here, each under its own connection name.
namespace ChatsDb {

// The named connection, open and keyed, or a closed one when that isn't possible yet: the engine
// hasn't made db.key or chats.db (before the first launch's end), or the key doesn't open it. The
// callers then return and try again at the next event. A connection is never left open without its
// key. The connection is read-only: only the engine writes chats.db. A connection belongs to the thread that first
// opens it (Qt's rule), so each thread uses its own connection name; open() itself may be called from any thread.
QSqlDatabase open(const QString &connectionName);

// The engine (re)connected: it may have deleted and rebuilt chats.db (another schema version, a new
// key), which a connection kept open would read for ever. Every connection is closed and opened
// again by its next open().
void engineReconnected();

}

// Calls ChatsDb::engineReconnected() whenever the engine link connects. main.cpp makes it before the
// models, so that it runs before their own connectedChanged slots.
class ChatsDbReconnect : public QObject
{
    Q_OBJECT
public:
    ChatsDbReconnect(EngineLink *engine, QObject *parent = 0);

private slots:
    void onConnectedChanged();

private:
    EngineLink *m_engine;
};

#endif
