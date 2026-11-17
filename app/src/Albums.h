#ifndef ALBUMS_H
#define ALBUMS_H

#include <string>
#include <vector>

// Which shown messages are one album (step 6.5g), WhatsApp's own display rule, so it needs no engine
// work and groups albums stored before this step too. Plain C++ and no Qt, so a host-side check can
// run it.
namespace Albums {
// What the rule reads of one shown message.
struct Row
{
    long long ts;
    int day;            // its local day, any number that is the same for the same day
    int kind;           // messages.kind
    bool deleted;
    bool text;          // has a caption (what the card shows as text)
    bool quote;         // quotes a message
    bool spoiler;       // under its cover: never in an album
    bool forwarded;     // shows its "Forwarded" line: never in an album
    bool out;
    std::string sender; // messages.sender: in a group, who sent it
};
struct Run
{
    int start; // index of its first row in the rows given
    int count;
};
// The albums among rows (shown messages, oldest first): runs of 4 or more consecutive rows that are each a
// photo or a video (kind 1 or 2), not deleted, with no caption and no quote, all ours or all from
// the same sender, each at most 60 s after the one before and on the same day. Anything else breaks
// the run. The runs come oldest first and don't overlap.
std::vector<Run> find(const std::vector<Row> &rows);
}

#endif
