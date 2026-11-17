#include "Polls.h"

#include <QCoreApplication>
#include <QDebug>
#include <QList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

// MessageModel's read-only connection, opened by the select that is reading the row.
static const char *const kDb = "chats";

void Polls::fill(const QString &chat, const QString &id, const QVariantList &names, int max,
                 QVariantMap &data)
{
    const int n = names.size();
    QList<int> counts;
    QList<QStringList> voters;
    for (int i = 0; i < n; ++i) {
        counts << 0;
        voters << QStringList();
    }
    QVariantList mine;
    int voted = 0;

    QSqlDatabase db = QSqlDatabase::database(kDb, false);
    if (db.isOpen()) {
        QSqlQuery q(db);
        q.prepare("SELECT sender, sender_name, options FROM poll_votes WHERE chat = ? AND msg = ? "
                  "ORDER BY ts, sender");
        q.addBindValue(chat);
        q.addBindValue(id);
        if (!q.exec())
            qWarning() << "poll_votes:" << q.lastError().text();
        while (q.next()) { // a failed exec has none
            const QString sender = q.value(0).toString();
            QString who = q.value(1).toString();
            if (sender.isEmpty())
                who = QCoreApplication::translate("Polls", "You");
            else if (who.isEmpty())
                who = sender.section('@', 0, 0);
            ++voted;
            QList<int> seen;
            foreach (const QString &s, q.value(2).toString().split(',', QString::SkipEmptyParts)) {
                bool ok = false;
                const int i = s.toInt(&ok);
                if (!ok || i < 0 || i >= n || seen.contains(i))
                    continue;
                seen << i;
                ++counts[i];
                voters[i] << who;
                if (sender.isEmpty())
                    mine << i;
            }
        }
    }

    QVariantList options;
    QStringList all; // the long-press menu's View votes: "Si (2): You, Andrea", one option a line
    for (int i = 0; i < n; ++i) {
        QVariantMap o;
        o["name"] = names[i].toString();
        o["count"] = counts[i];
        o["voters"] = voters[i].join("\n");
        options << o;
        all << QString("%1 (%2)").arg(names[i].toString()).arg(counts[i])
                   + (voters[i].isEmpty() ? QString() : ": " + voters[i].join(", "));
    }
    data["votesText"] = all.join("\n");
    data["max"] = max;
    data["mine"] = mine;
    data["voted"] = voted;
    data["options"] = options;
}
