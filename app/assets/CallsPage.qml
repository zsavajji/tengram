import bb.cascades 1.4
import tengram 1.0

// Chats > Calls: Telegram's call log, newest first. A tap calls the person back as the call was (a video call back with
// video); a long press offers Call, Video call and Open chat. The page asks the engine itself (calls.list) through its
// own CallLog, so the answer goes with the page, and asks again when a call ends while it is open. It shows the newest 15
// calls first and asks for 30 more each time the list is scrolled to its end.
Page {
    id: page
    property variant list: calllog.list
    property bool loaded: calllog.loaded
    property variant call: engine.call
    titleBar: PageBar {
        title: qsTr("Calls")
    }
    onListChanged: fill()
    // TDLib adds the call's message right after the call ends. Read from call itself, as CallSheet's handler does.
    onCallChanged: {
        if ((call.state || "") == "ended") {
            refresher.start()
        }
    }
    onCreationCompleted: {
        // Not linked, there is no account to ask about.
        if (engine.linked) {
            calllog.ask()
        }
    }
    // The rows: the person's chat when we have it (its name and picture), else what the call log says. The newest page
    // replaces them (from is 0), a next page adds its rows at the end.
    function fill() {
        var first = 0
        if (calllog.from == 0) {
            calls.clear()
        } else {
            first = calls.size()
        }
        for (var i = first; i < list.length; i++) {
            var chat = chats.chat(list[i].chat)
            var name = chat.name || list[i].name || ""
            var title = name || qsTr("Unknown")
            calls.append({"chat": list[i].chat, "name": name, "title": title,
                          "detail": list[i].detail, "video": list[i].video, "time": list[i].time,
                          "photo": chat.photo || "", "initials": chat.initials || title.charAt(0).toUpperCase() || "#",
                          "color": chat.color || "#24A1DE"})
        }
    }
    // The chat's row for nav.openRow: the row we have, else one built as main.qml's showPerson builds a person's.
    function callerRow(entry) {
        var who = chats.chat(entry.chat)
        if (who.jid) {
            return who
        }
        return {"kind": 0, "jid": entry.chat, "name": entry.name, "initials": entry.name.charAt(0).toUpperCase() || "#",
                "color": "#24A1DE"}
    }
    attachedObjects: [
        CallLog {
            id: calllog
            link: engine
        },
        Timer {
            id: refresher
            interval: 1000
            singleShot: true
            onTimeout: {
                if (engine.linked) {
                    calllog.ask()
                }
            }
        }
    ]
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ListView {
            // Filling the page: in a dock, a list without an alignment sat small in its corner and never grew (the user,
            // 2026-11-20).
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            // The rows' menu and tap reach the engine and the page through the view, as the message cards' do.
            function dial(chat, video) {
                engine.request("call.start", {"chat": chat, "video": !!video})
            }
            function openChat(entry) {
                nav.openRow(page.callerRow(entry))
            }
            dataModel: ArrayDataModel {
                id: calls
            }
            // At the end of the list: the next page, if there is one.
            attachedObjects: ListScrollStateHandler {
                onAtEndChanged: {
                    if (atEnd && calllog.more) {
                        calllog.askMore()
                    }
                }
            }
            onTriggered: {
                var entry = dataModel.data(indexPath)
                dial(entry.chat, entry.video)
            }
            // What the rows read through the view: a list item's context has no nav or settings.
            property variant emojiStyle: nav.emojiStyle
            property int bodySize: nav.bodySize
            property int smallSize: nav.smallSize
            property string pressColor: nav.pressColor
            property variant look: settings // the tiles' Shape (Tile.qml)
            // A row as the chat list's (ChatRows.cpp, Normal): 2 du in from the sides, a 9 du picture in a 12.6 du row, the name over
            // the call and the time at the right. A StandardListItem put its picture against the screen's edge, with no
            // way to pad it (the user's screenshot, 2026-11-19).
            listItemComponents: ListItemComponent {
                Container {
                    id: item
                    property variant entry: ListItemData
                    horizontalAlignment: HorizontalAlignment.Fill
                    background: item.ListItem.active ? Color.create(item.ListItem.view.pressColor) : Color.Transparent
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        minHeight: ui.du(12.6)
                        leftPadding: ui.du(2)
                        rightPadding: ui.du(2)
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        Tile {
                            verticalAlignment: VerticalAlignment.Center
                            preferredWidth: ui.du(9)
                            preferredHeight: ui.du(9)
                            look: item.ListItem.view.look
                            tint: item.entry.color
                            initials: item.entry.initials
                            photo: item.entry.photo
                            emojiStyle: item.ListItem.view.emojiStyle
                        }
                        Container {
                            verticalAlignment: VerticalAlignment.Center
                            leftMargin: ui.du(1.5)
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                            Label {
                                bottomMargin: 0
                                text: item.entry.title
                                textFormat: TextFormat.Plain
                                textStyle.base: item.ListItem.view.emojiStyle
                                textStyle.fontSize: FontSize.PercentageValue
                                textStyle.fontSizeValue: item.ListItem.view.bodySize
                            }
                            Label {
                                topMargin: 0
                                text: item.entry.detail
                                textFormat: TextFormat.Plain
                                textStyle.fontSize: FontSize.PercentageValue
                                textStyle.fontSizeValue: item.ListItem.view.smallSize
                                textStyle.color: ui.palette.secondaryTextOnPlain
                            }
                        }
                        Label {
                            verticalAlignment: VerticalAlignment.Center
                            text: item.entry.time
                            textFormat: TextFormat.Plain
                            textStyle.fontSize: FontSize.PercentageValue
                            textStyle.fontSizeValue: item.ListItem.view.smallSize
                            textStyle.color: ui.palette.secondaryTextOnPlain
                        }
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        preferredHeight: ui.px(2)
                        background: ui.palette.plain
                    }
                    contextActions: [
                        ActionSet {
                            title: item.entry.title
                            ActionItem {
                                title: qsTr("Call")
                                imageSource: "file:///usr/share/icons/ic_phone.png"
                                onTriggered: item.ListItem.view.dial(item.entry.chat, false)
                            }
                            ActionItem {
                                title: qsTr("Video call")
                                imageSource: "file:///usr/share/icons/ic_chat_video.png"
                                onTriggered: item.ListItem.view.dial(item.entry.chat, true)
                            }
                            ActionItem {
                                title: qsTr("Open chat")
                                imageSource: "asset:///images/message.png"
                                onTriggered: item.ListItem.view.openChat(item.entry)
                            }
                        }
                    ]
                }
            }
        }
        Label {
            visible: page.loaded && page.list.length == 0
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            textStyle.color: ui.palette.secondaryTextOnPlain
            text: qsTr("No calls yet")
        }
    }
}
