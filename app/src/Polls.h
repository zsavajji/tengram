#ifndef POLLS_H
#define POLLS_H

#include <QVariantList>
#include <QVariantMap>

// The options and counts of a poll (step 6.5e), kept out of MessageKinds.cpp to keep that file
// short. docs/protocol.md has the poll's extra.
namespace Polls {
// Sets in data, from the poll row's extra (options, votes, mine, voted, closed, max): max (0 any
// number, 1 one), mine (our chosen option indices, a list), voted (how many voted), pollClosed and
// options, a list of maps {name, count, voters}: count how many chose it, voters "" (Telegram's polls
// are anonymous here). votesText is the long-press menu's View votes, "Yes (2)", one option a line.
void fill(const QVariantMap &extra, QVariantMap &data);
}

#endif
