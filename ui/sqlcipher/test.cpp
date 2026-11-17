// Reads the engine's encrypted chats.db the way the UI will: through QtSql, with Qt 4.8's SQLite driver
// built on SQLCipher (QSQLCipherDriver). Usage: sqlciphertest <chats.db> <db.key> "<sql>"
#include "qsql_sqlcipher.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>

#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc == 2 && QString(argv[1]) == "-h") {
        std::printf("usage: sqlciphertest <chats.db> <db.key> \"<sql>\"\n");
        return 0;
    }
    if (argc != 4)
        return 2;
    QFile keyFile(argv[2]);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot read %s\n", argv[2]);
        return 1;
    }
    const QString key = QString::fromLatin1(keyFile.readAll().trimmed());

    QElapsedTimer t;
    t.start();
    QSqlDatabase db = QSqlDatabase::addDatabase(new QSQLCipherDriver, "chats");
    db.setDatabaseName(argv[1]);
    db.setConnectOptions("QSQLITE_OPEN_READONLY");  // as HyperBerry's UI opens it
    if (!db.open()) {
        std::fprintf(stderr, "open: %s\n", qPrintable(db.lastError().text()));
        return 1;
    }
    QSqlQuery q(db);
    // The key is the first statement on the connection. A raw key: no key derivation.
    if (!q.exec(QString("PRAGMA key = \"x'%1'\"").arg(key))) {
        std::fprintf(stderr, "key: %s\n", qPrintable(q.lastError().text()));
        return 1;
    }
    const qint64 opened = t.elapsed();
    if (!q.exec(argv[3])) {
        std::fprintf(stderr, "query: %s\n", qPrintable(q.lastError().text()));
        return 1;
    }
    int rows = 0;
    while (q.next()) {
        QStringList cols;
        for (int i = 0; i < q.record().count(); ++i)
            cols << (q.value(i).isNull() ? "NULL" : q.value(i).toString());
        std::printf("%s\n", qPrintable(cols.join("\t")));
        ++rows;
    }
    std::fprintf(stderr, "%d rows; open+key %lld ms, query %lld ms\n", rows, opened, t.elapsed() - opened);
    return 0;
}
