import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0

// Search (the user's ask, 2026-11-19): Telegram's own search, as its Android app has it, in place of the chat list's
// filter. Tabs Chats, Channels and Apps in the title bar. With no text, the people the user writes to most in a strip and
// the chats found before ("Recent"); with text, the chats Tengram knows and then Telegram's "Global search". The tiles
// are squares, as everywhere, or discs in Round mode (Tile.qml).
Page {
    id: page
    // The text in the field. Not the field's own `text`, which does not tell a binding while the user types.
    property string typed: ""
    // Declared before the title bar, so that `search` exists when the bar first says which tab is selected.
    attachedObjects: [
        ChatSearch {
            id: search
            link: engine
        },
        SystemToast {
            id: noOpen
            body: qsTr("This chat can't be opened from here")
        }
    ]
    titleBar: TitleBar {
        kind: TitleBarKind.Segmented
        options: [
            Option { text: qsTr("Chats"); value: "chats"; selected: true },
            Option { text: qsTr("Channels"); value: "channels" },
            Option { text: qsTr("Apps"); value: "apps" }
        ]
        onSelectedValueChanged: search.tab = selectedValue
    }
    onCreationCompleted: search.ask()
    // Called after the push, so on the Q10 typing searches at once (as NewChatPage.focusSearch).
    function focusSearch() {
        field.requestFocus()
    }
    function clearField() {
        field.text = ""
        typed = ""
        search.query = ""
    }
    // A chat from the lists. One we are in opens from the chat list; a person or a bot opens as a contact page's Message
    // does (the chat may not exist yet); a public group or channel we are not in goes through its link, which asks to
    // join (main.qml's askToJoin).
    function open(r) {
        search.remember(r.jid)
        if (r.member) {
            nav.openRequested(r.jid)
        } else if (r.kind == "person" || r.kind == "bot") {
            nav.openRow({"jid": r.jid, "kind": 0, "name": r.name, "initials": r.initials, "color": r.color,
                         "photo": r.photo || ""})
        } else if (r.username) {
            invites.openLink("https://t.me/" + r.username)
        } else {
            noOpen.show()
        }
    }
    Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            // The field and a ✕ that empties it in one tap: the Q10 has no Escape.
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                TextField {
                    id: field
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    hintText: qsTr("Search")
                    input.submitKey: SubmitKey.Search
                    onTextChanging: {
                        page.typed = text
                        search.query = text
                    }
                }
                // A touch target the size of an action column (9 du), the ✕ as the reply bar's.
                Container {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(9)
                    preferredHeight: ui.du(9)
                    layout: DockLayout {
                    }
                    gestureHandlers: TapHandler {
                        onTapped: page.clearField()
                    }
                    Label {
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        text: "✕"
                        textFormat: TextFormat.Plain
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                }
            }
            // The people the user writes to most: a square tile and the first name, a tap opens the chat.
            ListView {
                property variant emojiStyle: nav.emojiStyle
                property int smallSize: nav.smallSize
                property string pressColor: nav.pressColor
                property variant look: settings // the tiles' Shape (Tile.qml)
                visible: page.typed == "" && search.hasTop
                horizontalAlignment: HorizontalAlignment.Fill
                preferredHeight: ui.du(15)
                dataModel: search.top
                layout: StackListLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                scrollIndicatorMode: ScrollIndicatorMode.None
                onTriggered: page.open(dataModel.data(indexPath))
                listItemComponents: ListItemComponent {
                    type: ""
                    Container {
                        id: cell
                        preferredWidth: ui.du(12)
                        topPadding: ui.du(1)
                        bottomPadding: ui.du(1)
                        background: cell.ListItem.active ? Color.create(cell.ListItem.view.pressColor) : Color.Transparent
                        Tile {
                            horizontalAlignment: HorizontalAlignment.Center
                            preferredWidth: ui.du(9)
                            preferredHeight: ui.du(9)
                            look: cell.ListItem.view.look
                            tint: ListItemData.color
                            initials: ListItemData.initials
                            photo: ListItemData.photo || ""
                            emojiStyle: cell.ListItem.view.emojiStyle
                        }
                        Label {
                            horizontalAlignment: HorizontalAlignment.Fill
                            text: ListItemData.first
                            textFormat: TextFormat.Plain
                            textStyle.base: cell.ListItem.view.emojiStyle
                            textStyle.fontSize: FontSize.PercentageValue
                            textStyle.fontSizeValue: cell.ListItem.view.smallSize
                            textStyle.textAlign: TextAlign.Center
                        }
                    }
                }
            }
            ListView {
                // The rows reach the shared emoji style and sizes through the view, as the chat list's do.
                property variant emojiStyle: nav.emojiStyle
                property int bodySize: nav.bodySize
                property int smallSize: nav.smallSize
                property string pressColor: nav.pressColor
                property variant look: settings // the tiles' Shape (Tile.qml)
                dataModel: search.results
                // A list item sees the page's ids no more than its `nav`: "Clear all" comes through the view.
                function clearRecent() {
                    search.clearRecent()
                }
                function itemType(data, indexPath) {
                    return data.header ? "header" : ""
                }
                onTriggered: {
                    var r = dataModel.data(indexPath)
                    if (!r.header) {
                        page.open(r)
                    }
                }
                listItemComponents: [
                    ListItemComponent {
                        type: "header"
                        Container {
                            id: head
                            horizontalAlignment: HorizontalAlignment.Fill
                            leftPadding: ui.du(1.5)
                            rightPadding: ui.du(1.5)
                            topPadding: ui.du(1.5)
                            bottomPadding: ui.du(0.5)
                            layout: StackLayout {
                                orientation: LayoutOrientation.LeftToRight
                            }
                            Label {
                                verticalAlignment: VerticalAlignment.Center
                                layoutProperties: StackLayoutProperties {
                                    spaceQuota: 1
                                }
                                text: ListItemData.header
                                textFormat: TextFormat.Plain
                                textStyle.fontSize: FontSize.PercentageValue
                                textStyle.fontSizeValue: head.ListItem.view.smallSize
                                textStyle.color: ui.palette.secondaryTextOnPlain
                            }
                            Label {
                                visible: !!ListItemData.clear
                                verticalAlignment: VerticalAlignment.Center
                                text: qsTr("Clear all")
                                textFormat: TextFormat.Plain
                                textStyle.fontSize: FontSize.PercentageValue
                                textStyle.fontSizeValue: head.ListItem.view.smallSize
                                textStyle.color: ui.palette.primary
                                gestureHandlers: TapHandler {
                                    onTapped: head.ListItem.view.clearRecent()
                                }
                            }
                        }
                    },
                    ListItemComponent {
                        type: ""
                        ContactRow {
                            contact: ListItemData
                        }
                    }
                ]
            }
        }
        Busy {
            running: search.busy && search.empty && !search.hasTop
            text: qsTr("Searching…")
        }
        Label {
            visible: search.searched && page.typed != "" && search.empty
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            multiline: true
            textStyle.textAlign: TextAlign.Center
            textStyle.color: ui.palette.secondaryTextOnPlain
            text: qsTr("No results")
        }
    }
}
