#include "ChatsDb.h"

#include "EngineLink.h"
#include "qsql_sqlcipher.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>

namespace {

// Raised by engineReconnected(). A connection opened under an older number is stale.
int generation = 0;
// By connection name: the generation it was last opened in.
QHash<QString, int> openedIn;

// db.key: 64 hex characters, "" when the file is missing or isn't that.
QString readKey()
{
    QFile file(QDir::homePath() + "/db.key");
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QString key = QString::fromLatin1(file.readAll().trimmed());
    if (key.size() != 64)
        return QString();
    for (int i = 0; i < key.size(); ++i) {
        const ushort c = key.at(i).unicode();
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return QString();
    }
    return key;
}

// The key is the first statement on the connection, and a raw key (no key derivation). A wrong or missing
// key raises no error there: the first real statement fails with "file is not a database", so one SELECT
// proves it. The queries end here, before the caller may close the connection.
bool keyed(QSqlDatabase &db, const QString &key)
{
    QSqlQuery q(db);
    if (!q.exec("PRAGMA key = \"x'" + key + "'\"")) {
        qWarning() << "chats.db: key:" << q.lastError().text();
        return false;
    }
    if (!q.exec("SELECT count(*) FROM sqlite_master") || !q.next()) {
        qWarning() << "chats.db: the key does not open it:" << q.lastError().text();
        return false;
    }
    return true;
}

}

QSqlDatabase ChatsDb::open(const QString &connectionName)
{
    if (!QSqlDatabase::contains(connectionName)) {
        // The driver is Qt 4.8's SQLite one on SQLCipher (QSQLCipherDriver: the phone's QtSql has a
        // QSQLiteDriver already); the connection takes it over.
        QSqlDatabase added = QSqlDatabase::addDatabase(new QSQLCipherDriver, connectionName);
        added.setDatabaseName(QDir::homePath() + "/chats.db");
        added.setConnectOptions("QSQLITE_OPEN_READONLY");
    }
    QSqlDatabase db = QSqlDatabase::database(connectionName, false);
    if (db.isOpen() && openedIn.value(connectionName, -1) != generation)
        db.close();
    if (db.isOpen())
        return db;
    const QString key = readKey();
    if (key.isEmpty() || !db.open())
        return db;
    if (!keyed(db, key)) {
        db.close();
        return db;
    }
    openedIn.insert(connectionName, generation);
    return db;
}

void ChatsDb::engineReconnected()
{
    ++generation;
}

ChatsDbReconnect::ChatsDbReconnect(EngineLink *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    connect(engine, SIGNAL(connectedChanged()), this, SLOT(onConnectedChanged()));
}

void ChatsDbReconnect::onConnectedChanged()
{
    if (m_engine->connected())
        ChatsDb::engineReconnected();
}
