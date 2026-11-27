import bb.cascades 1.4

// ⋮ → Location: the send-location page (LocationPage.qml), out of ChatPage.qml to keep that file short. Added by the page for
// every chat we write in. The location goes into the page's topic or thread and replies to the message the composer
// replies to; `sent` comes once it went, so the page drops the reply and follows our row.
ActionItem {
    id: action
    property string jid
    property int topic: 0
    property string thread: ""
    // The chat page's ChatController. Not named ctl: in `ctl: ctl` QML would read this property.
    property variant chatCtl
    signal sent()
    title: qsTr("Location")
    imageSource: "file:///usr/share/icons/ic_map_all.png"
    ActionBar.placement: ActionBarPlacement.InOverflow
    onTriggered: {
        var p = locationDef.createObject()
        p.jid = action.jid
        p.topic = action.topic
        p.thread = action.thread
        p.quote = action.chatCtl.quoteId()
        p.sent.connect(action.sent)
        nav.push(p)
        p.open() // after the properties: the map is asked for this chat
    }
    attachedObjects: [
        ComponentDefinition {
            id: locationDef
            source: "LocationPage.qml"
        }
    ]
}
