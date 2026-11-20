import bb.cascades 1.4

// The emoji picker (PLAN.md §7, step 4.2): a strip of the categories over a grid of three rows.
// Cells are PNGs, never text: scrolling shows emoji not seen before, which churned the glyph
// cache (step 0.7). emojis is the EmojiModel; picked is emitted per tap.
Container {
    id: picker
    signal picked(string emoji)
    // The strip's last cell: the sticker page (StickersPage.qml). The chat opens it.
    signal stickers()
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
        // 64 px, ten across the Q10's 720 beside the Stickers button: the icon 54 px, the group under the list's first
        // item underlined in the primary colour. This and the Stickers button are reached by the Classic's trackpad:
        // FocusPolicy.Key, not KeyAndTouch, so a touch leaves the focus (the composer's keyboard) where it is; the one with the
        // focus is lit as the pressed colour, and a click (Enter) does what a tap does. The grid's cells are the list's own.
        TapArea {
            id: cell
            property int index
            property alias image: icon.imageSource
            property alias marked: underline.visible
            signal tapped(int index)
            preferredWidth: ui.px(64)
            preferredHeight: ui.px(72)
            layout: DockLayout {}
            focusPolicy: FocusPolicy.Key
            background: cell.focused ? Color.create(nav.pressColor) : Color.Transparent
            onClicked: cell.tapped(cell.index)
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
    // The first row: the groups, then the Stickers button fixed at its end, apart from them (the user, 2026-11-20): it opens
    // the sticker page (the chat does, on stickers()), it isn't a group of this grid.
    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        layout: StackLayout {
            orientation: LayoutOrientation.LeftToRight
        }
        Container {
            id: strip
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
        }
        Container {
            preferredWidth: ui.px(2)
            preferredHeight: ui.px(48)
            verticalAlignment: VerticalAlignment.Center
            background: ui.palette.secondaryTextOnPlain
            opacity: 0.4
        }
        TapArea {
            id: stickersBtn
            preferredWidth: ui.px(72)
            preferredHeight: ui.px(72)
            layout: DockLayout {}
            focusPolicy: FocusPolicy.Key
            background: stickersBtn.focused ? Color.create(nav.pressColor) : Color.Transparent
            onClicked: picker.stickers()
            ImageView {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.px(54)
                preferredHeight: ui.px(54)
                // Telegram's sticker mark, drawn by scripts/gen-sticker-icon.py: white, tinted as the theme's quiet text.
                imageSource: "asset:///images/stickers.png"
                filterColor: ui.palette.secondaryTextOnPlain
            }
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
