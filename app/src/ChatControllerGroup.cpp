#include "ChatController.h"

#include "colors.h"

// The group info page's data: group.info's result with what the page draws added, and a member's
// picture arriving after it.

// Adds what the page draws to each participant: the tile's initials and the colour the name
// has in the chat. The engine adds the photo, when there is one.
void ChatController::takeGroupInfo(QVariantMap info)
{
    QVariantList people;
    foreach (const QVariant &v, info["participants"].toList()) {
        QVariantMap p = v.toMap();
        p["initials"] = initials(p["name"].toString());
        p["color"] = pickColor(p["jid"].toString(), kSenders, kSenderCount);
        people << p;
    }
    info["participants"] = people;
    m_groupInfo = info;
    emit groupInfoChanged();
    const int n = people.size();
    m_participants = n == 1 ? tr("1 participant") : tr("%1 participants").arg(n);
    emit statusChanged();
}

// A member's picture that came after group.info (a photo event): on their row, and the page
// redraws that row.
void ChatController::onPhoto(const QString &jid, const QString &path)
{
    if (m_groupInfo.isEmpty())
        return;
    QVariantList people = m_groupInfo["participants"].toList();
    for (int i = 0; i < people.size(); ++i) {
        QVariantMap p = people.at(i).toMap();
        if (p["jid"].toString() != jid)
            continue;
        p["photo"] = path;
        people.replace(i, p);
        m_groupInfo["participants"] = people;
        emit groupInfoChanged();
        return;
    }
}
