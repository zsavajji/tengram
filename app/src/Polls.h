#ifndef POLLS_H
#define POLLS_H

#include <QString>
#include <QVariantList>
#include <QVariantMap>

// The options, counts and voters of a poll (step 6.5e), kept out of MessageModel.cpp to keep that file
// short. docs/protocol.md has poll_votes.
namespace Polls {
// Reads poll_votes of the poll `id` in `chat` (the "chats" connection MessageModel has open) and sets
// in data: max (as given: 0 any number, 1 one), mine (our chosen option indices, a list), voted (how
// many voted) and options, a list of maps {name, count, voters}: names are the poll's option names,
// count how many chose it and voters who, one per line ("You" for us, else the voter's name, else the
// part of their key before "@"). A vote's index outside names is left out.
void fill(const QString &chat, const QString &id, const QVariantList &names, int max,
          QVariantMap &data);
}

#endif
