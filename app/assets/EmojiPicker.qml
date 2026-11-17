import bb.cascades 1.4

// The emoji picker (PLAN.md §7, step 4.2): a strip of the categories over a grid of three rows.
// Cells are PNGs, never text: scrolling shows emoji not seen before, which churned the glyph
// cache (step 0.7). emojis is the EmojiModel; picked is emitted per tap.
Container {
    id: picker
    signal picked(string emoji)
    horizontalAlignment: HorizontalAlignment.Fill
    layout: StackLayout {
        orientation: LayoutOrientation.TopToBottom
    }
    // The strip's cells, to move the underline.
    property variant cells: []
    function go(k) {
        list.scrollToItem([k], ScrollAnimation.None)
    }
    function mark(k) {
        for (var i = 0; i < cells.length; i++) {
            cells[i].marked = (i == k)
        }
    }
    onCreationCompleted: {
        var groups = emojis.groups()
        var made = []
        for (var i = 0; i < groups.length; i++) {
            var c = cellDef.createObject()
            c.index = i
            c.image = groups[i].image
            c.tapped.connect(picker.go)
            strip.add(c)
            made.push(c)
        }
        cells = made
        mark(0)
    }
    attachedObjects: ComponentDefinition {
        id: cellDef
        // 72 px, ten across the Q10's 720: the icon 54 px, the group under the list's first item
        // underlined in the primary colour.
        Container {
            id: cell
            property int index
            property alias image: icon.imageSource
            property alias marked: underline.visible
            signal tapped(int index)
            preferredWidth: ui.px(72)
            preferredHeight: ui.px(72)
            layout: DockLayout {}
            gestureHandlers: TapHandler {
                onTapped: cell.tapped(cell.index)
            }
            ImageView {
                id: icon
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.px(54)
                preferredHeight: ui.px(54)
            }
            Container {
                id: underline
                visible: false
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Bottom
                preferredHeight: ui.px(5)
                background: ui.palette.primary
            }
        }
    }
    Container {
        id: strip
        layout: StackLayout {
            orientation: LayoutOrientation.LeftToRight
        }
    }
    ListView {
        id: list
        preferredHeight: ui.px(240)
        dataModel: emojis
        // headerMode must be set: a grid's default (None) shows only the groups, as cells, and
        // never their emoji (Q10, 2026-10-31). StackListLayout's default is Standard.
        layout: GridListLayout {
            columnCount: 9
            cellAspectRatio: 1.0
            headerMode: ListHeaderMode.Standard
        }
        attachedObjects: ListScrollStateHandler {
            onFirstVisibleItemChanged: {
                if (firstVisibleItem.length > 0) {
                    picker.mark(firstVisibleItem[0])
                }
            }
        }
        // An empty Recent shows just its header.
        listItemComponents: [
            ListItemComponent {
                type: "header"
                Header {
                    title: ListItemData
                }
            },
            ListItemComponent {
                type: "item"
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    layout: DockLayout {}
                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: ui.px(54)
                        preferredHeight: ui.px(54)
                        imageSource: ListItemData.image
                    }
                }
            }
        ]
        onTriggered: {
            if (indexPath.length == 2) {
                picker.picked(dataModel.data(indexPath).emoji)
            }
        }
    }
}
