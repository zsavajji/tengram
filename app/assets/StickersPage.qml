import bb.cascades 1.4
import tengram 1.0

// The sticker page (docs/stickers-plan.md, G4), opened from the emoji picker's last cell: a strip of the groups over a grid
// of four columns, Recent, Favourites, then the installed sets. The whole of it is StickerGrid, in C++ (StickerPicker holds the
// data, one for the app). A tap on a sticker sends it and closes the page (the chat does both: picked); a long press shows it
// large over a dimmed page with Send and Add to or Remove from favourites. Nothing on the page animates (the user, 2026-11-20).
Page {
    id: page
    // A tap on a sticker, or Send in the preview: its file, which sticker.send takes.
    signal picked(string file)
    titleBar: PageBar {
        title: qsTr("Stickers")
    }
    content: StickerGrid {
        link: engine
        pageColor: nav.pageColor
        pressColor: nav.pressColor
        emojiStyle: nav.emojiStyle
        onPicked: page.picked(file)
    }
}
