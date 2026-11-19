import bb.cascades 1.4

// One row of the address book (NewChatPage, NewGroupPage) or of a group's members:
// the tile the chat will have in the chat list, its picture over it when the row has one, the name,
// a 2 px separator, a check when the contact is chosen and, for the rows that carry a time, when (a story's
// viewers), and a second line under the name when the row has a `sub`. contact is a row of ContactPicker's model; the
// view supplies the emoji style and sizes as the chat list's, and `look: settings` for the tile's Shape (Tile.qml).
Container {
    property variant contact
    id: row
    horizontalAlignment: HorizontalAlignment.Fill
    background: row.ListItem.active ? Color.create(row.ListItem.view.pressColor) : Color.Transparent
    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        minHeight: ui.du(9.4)
        leftPadding: ui.du(1.5)
        rightPadding: ui.du(1.5)
        layout: StackLayout {
            orientation: LayoutOrientation.LeftToRight
        }
        // The picture over the initials, when the row has one (a group's members do, from group.info: the call picker's
        // and the group page's). A disc in Round mode: the view carries `look` (Tile.qml).
        Tile {
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(7)
            preferredHeight: ui.du(7)
            look: row.ListItem.view.look
            tint: row.contact.color
            initials: row.contact.initials
            photo: row.contact.photo || ""
            emojiStyle: row.ListItem.view.emojiStyle
        }
        Container {
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(1.5)
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            Label {
                horizontalAlignment: HorizontalAlignment.Fill
                text: row.contact.name
                textFormat: TextFormat.Plain
                textStyle.base: row.ListItem.view.emojiStyle
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: row.ListItem.view.bodySize
                // The second line sits right under it: the stock margins left a line's gap (the user's screenshot, 2026-11-19).
                bottomMargin: 0
            }
            // A second line, for the rows that have one (the search page's: @username, members).
            Label {
                visible: !!row.contact.sub
                topMargin: 0
                horizontalAlignment: HorizontalAlignment.Fill
                text: row.contact.sub ? row.contact.sub : ""
                textFormat: TextFormat.Plain
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: row.ListItem.view.smallSize
                textStyle.color: ui.palette.secondaryTextOnPlain
            }
        }
        // When, at the right, for the rows that say (a story's viewers).
        Label {
            visible: !!row.contact.time
            verticalAlignment: VerticalAlignment.Center
            text: row.contact.time ? row.contact.time : ""
            textFormat: TextFormat.Plain
            textStyle.fontSize: FontSize.PercentageValue
            textStyle.fontSizeValue: row.ListItem.view.smallSize
            textStyle.color: ui.palette.secondaryTextOnPlain
        }
        ImageView {
            visible: !!row.contact.chosen
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(5)
            preferredHeight: ui.du(5)
            imageSource: "asset:///images/tick_action.png"
            filterColor: ui.palette.primary
        }
    }
    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        preferredHeight: ui.px(2)
        background: ui.palette.plain
    }
}
