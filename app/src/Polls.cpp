#include "Polls.h"

#include <QStringList>

void Polls::fill(const QVariantMap &extra, QVariantMap &data)
{
    const QVariantList names = extra.value("options").toList();
    const QVariantList votes = extra.value("votes").toList();
    QVariantList options;
    QStringList all;
    for (int i = 0; i < names.size(); ++i) {
        const int count = votes.value(i).toInt(); // an extra without votes (an old row) counts 0
        QVariantMap o;
        o["name"] = names[i].toString();
        o["count"] = count;
        o["voters"] = QString();
        options << o;
        all << QString("%1 (%2)").arg(names[i].toString()).arg(count);
    }
    data["votesText"] = all.join("\n");
    data["max"] = extra.value("max").toInt();
    data["mine"] = extra.value("mine").toList();
    data["voted"] = extra.value("voted").toInt();
    data["pollClosed"] = extra.value("closed").toBool();
    data["pollFixed"] = extra.value("fixed").toBool();
    data["options"] = options;
}
