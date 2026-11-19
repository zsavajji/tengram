import bb.cascades 1.4
import tengram 1.0

// Chats > Calls: Telegram's call log, newest first. A tap calls the person back as the call was (a video call back with
// video); a long press offers Call, Video call and Open chat. The page asks the engine itself (calls.list) through its
// own CallLog, so the answer goes with the page, and asks again when a call ends while it is open.
Page {
    id: page
    property variant list: calllog.list
    property bool loaded: calllog.loaded
    property variant call: engine.call
    // Round mode (the user's ask, 2026-11-19): the rows' pictures are the round copies (a StandardListItem's image can't be
    // clipped, so they are made as the rows are filled), and fill() runs again when the Shape changes.
    property bool round: settings.round
    onRoundChanged: fill()
    titleBar: TitleBar {
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
    // The rows: the person's chat when we have it (its name and picture), else what the call log says.
    function fill() {
        calls.clear()
        for (var i = 0; i < list.length; i++) {
            var chat = chats.chat(list[i].chat)
            var name = chat.name || list[i].name || ""
            calls.append({"chat": list[i].chat, "name": name, "title": name || qsTr("Unknown"),
                          "detail": list[i].detail, "video": list[i].video, "time": list[i].time,
                          "photo": chat.photo ? "file://" + (page.round ? (settings.roundPhoto(chat.photo) || chat.photo) : chat.photo) : ""})
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
            onTriggered: {
                var entry = dataModel.data(indexPath)
                dial(entry.chat, entry.video)
            }
            listItemComponents: ListItemComponent {
                StandardListItem {
                    id: item
                    property variant entry: ListItemData
                    title: ListItemData.title
                    description: ListItemData.detail
                    status: ListItemData.time
                    imageSource: ListItemData.photo
                    imageSpaceReserved: true
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
