import bb.cascades 1.4

// All of an album's photos and videos, two to a row (step 6.5g), from the card's "+N". members are
// the album's messages' maps, oldest first; view is the chat page's list, whose openMedia opens one
// (a photo in the viewer, pushed over this page, so Back returns here). Both are set right after
// createObject() (nav.showAlbum).
// Nothing here reads an id of the page that made it: it outlives that page's list items.
// ponytail: the page shows the album as it was when opened; reopen it to see a later change.
Page {
    id: page
    property variant view
    property variant members
    // One by one: append() with a whole array might take it as one item.
    onMembersChanged: {
        album.clear()
        for (var i = 0; i < members.length; i++) {
            album.append(members[i])
        }
    }
    titleBar: TitleBar {
        title: qsTr("Album")
    }
    ListView {
        id: grid
        // The tiles reach the chat list through this page's list (ListItem.view).
        function open(msg) {
            page.view.openMedia(msg)
        }
        property string loadingId: page.view ? page.view.mediaLoadingId : ""
        dataModel: ArrayDataModel {
            id: album
        }
        layout: GridListLayout {
            columnCount: 2
            cellAspectRatio: 1
        }
        listItemComponents: [
            ListItemComponent {
                AlbumTile {
                    id: cell
                    member: ListItemData
                    loadingId: cell.ListItem.view.loadingId
                    onTapped: cell.ListItem.view.open(cell.member)
                }
            }
        ]
    }
}
