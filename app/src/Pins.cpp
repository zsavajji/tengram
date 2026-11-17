#include "Pins.h"

#include "ChatsDb.h"
#include "MediaData.h"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariantMap>

// MessageModel's read-only connection (ChatsDb opens it, if the select that is reading the row has not).
static const char *const kDb = "chats";

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

QVariantList Pins::previews(const QString &chat, const QStringList &ids)
{
    QVariantList out;
    QSqlDatabase db = ChatsDb::open(kDb);
    foreach (const QString &id, ids) {
        QVariantMap pin;
        pin["id"] = id;
        pin["preview"] = db.isOpen() ? preview(db, chat, id) : QString();
        out << pin;
    }
    return out;
}
