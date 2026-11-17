#include "Pins.h"

#include "MediaData.h"

#include <bb/data/JsonDataAccess>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariantMap>

using namespace bb::data;

// MessageModel's read-only connection.
static const char *const kDb = "chats";

struct Pin
{
    QString id;     // the pinned message's
    qint64 ts;      // the notice's
    qint64 until;   // unix seconds, 0 when the notice has none
    bool pinned;    // a pin, not an unpin
};

// The line a message is listed by: its text's first line, else what kind it is.
QString Pins::summary(int kind, const QString &text)
{
    QString line = text.trimmed().section('\n', 0, 0).trimmed();
    if (line.isEmpty())
        line = kind == 3 ? QCoreApplication::translate("Pins", "Voice message") : MediaData::kindName(kind);
    if (line.isEmpty())
        line = QCoreApplication::translate("MessageKinds", "Message"); // never "": that means not here
    if (line.size() > 60) {
        int n = 59;
        if (line.at(n - 1).unicode() >= 0xD800 && line.at(n - 1).unicode() < 0xDC00)
            --n; // not between the halves of an emoji
        line = line.left(n) + QString::fromUtf8("…");
    }
    return line;
}

static QString preview(QSqlDatabase &db, const QString &chat, const QString &id)
{
    QSqlQuery q(db);
    q.prepare("SELECT kind, text, deleted FROM messages WHERE chat = ? AND id = ?");
    q.addBindValue(chat);
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return QString();
    if (q.value(2).toInt() != 0)
        return QCoreApplication::translate("MessageModel", "This message was deleted");
    return Pins::summary(q.value(0).toInt(), q.value(1).toString());
}

QVariantList Pins::current(const QString &chat)
{
    QVariantList out;
    QSqlDatabase db = QSqlDatabase::database(kDb, false);
    if (!db.isOpen())
        return out;
    QSqlQuery q(db);
    // kind 13 is a notice; its text is the code.
    q.prepare("SELECT ts, text, extra FROM messages WHERE chat = ? AND kind = 13 "
              "AND text IN ('pin', 'unpin') ORDER BY ts, rowid");
    q.addBindValue(chat);
    if (!q.exec()) {
        qWarning() << "pins:" << q.lastError().text();
        return out;
    }
    // The latest notice of each id, oldest first: a newer one for an id replaces and moves it last.
    // ponytail: a linear scan per notice; a chat has few, an index if one ever has thousands.
    QList<Pin> last;
    while (q.next()) {
        QVariantMap extra;
        JsonDataAccess parser;
        const QVariant doc = parser.loadFromBuffer(q.value(2).toString().toUtf8());
        if (!parser.hasError() && doc.type() == QVariant::Map)
            extra = doc.toMap();
        Pin p;
        p.id = extra.value("msg").toString();
        if (p.id.isEmpty())
            continue;
        p.ts = q.value(0).toLongLong();
        p.until = extra.value("until").toLongLong();
        p.pinned = q.value(1).toString() == "pin";
        for (int i = 0; i < last.size(); ++i) {
            if (last[i].id == p.id) {
                last.removeAt(i);
                break;
            }
        }
        last << p;
    }

    const qint64 now = QDateTime::currentDateTime().toTime_t();
    for (int i = last.size() - 1; i >= 0; --i) {
        const Pin &p = last[i];
        if (!p.pinned || (p.until > 0 ? p.until <= now : p.ts <= now - 7 * 24 * 3600))
            continue;
        QVariantMap pin;
        pin["id"] = p.id;
        pin["preview"] = preview(db, chat, p.id);
        out << pin;
    }
    return out;
}
