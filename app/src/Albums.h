#ifndef ALBUMS_H
#define ALBUMS_H

#include <string>
#include <vector>

// Which shown messages are one album: Telegram's own, by the album id the engine keeps (extra.album). It
// replaced HyperBerry's WhatsApp rule (4 or more photos within a minute), which never grouped an album of 2 or 3,
// nor one with a caption (the user, 2026-11-18). Plain C++ and no Qt.
namespace Albums {
// What the rule reads of one shown message.
struct Row
{
    int kind;           // messages.kind
    bool deleted;
    bool spoiler;       // under its cover: never in an album
    std::string album;  // Telegram's album id, "" for none
};
struct Run
{
    int start; // index of its first row in the rows given
    int count;
};
// The albums among rows (shown messages, oldest first): runs of 2 or more consecutive rows of one album id that are
// each a photo or a video (kind 1 or 2), not deleted, not under a spoiler. Anything else breaks the run (an album of
// documents stays one row each). The runs come oldest first and don't overlap.
std::vector<Run> find(const std::vector<Row> &rows);
}

#endif
