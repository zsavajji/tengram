#ifndef ALBUMBUBBLE_H
#define ALBUMBUBBLE_H

#include <bb/cascades/CustomControl>
#include <QString>
#include <QVariantMap>

namespace bb {
namespace cascades {
class ListView;
class UIConfig;
}
}

class AlbumTile;

// An album in a card (step 6.5g), mirroring AlbumBubble.qml (gone since step 8.6): 4 or more photos and videos sent
// together (MessageModel makes the entry; Albums.h has the rule) as four square tiles, two by
// two, across the card's width inside its side padding. With more than four, the fourth tile is
// darkened with "+N", N the album's size less 3, and opens the album's page (the list's
// showAlbum); the others open their member as its own row would (the list's openMedia). The four
// tiles are fixed and each takes its picture from the current msg, so a recycled row never keeps
// the previous row's. msg is the album's map: the first member's plus album (the members' maps,
// oldest first) and albumCount.
// ponytail: the long-press menu, the reactions and the search tint are the first photo's (msg is
// its map), so Delete and Reply act on that one only; per-member menus if that is missed.
class AlbumBubble : public bb::cascades::CustomControl
{
    Q_OBJECT
public:
    explicit AlbumBubble(bb::cascades::ListView *list);

    void show(const QVariantMap &msg);

private slots:
    void refresh();
    void onTap();

private:
    QVariantMap memberAt(int i) const;

    bb::cascades::ListView *m_list;
    bb::cascades::UIConfig *m_ui;
    QVariantMap m_msg;
    AlbumTile *m_tiles[4];
    float m_size; // a tile's side, set by show()
};

#endif
