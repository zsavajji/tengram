import bb.cascades 1.4

// ⋮ → Create poll (step 6.6e), out of ChatPage.qml to keep that file short. People's and groups' chats only: the page adds it for those, as Contact info.
ActionItem {
    id: action
    property string jid
    // The forum topic the chat page is in (0: none); the poll is sent into it.
    property int topic: 0
    title: qsTr("Create poll")
    imageSource: "file:///usr/share/icons/ic_accept.png"
    ActionBar.placement: ActionBarPlacement.InOverflow
    onTriggered: {
        var p = pollDef.createObject()
        p.jid = action.jid
        p.topic = action.topic
        nav.push(p)
        p.focusQuestion()
    }
    attachedObjects: [
        ComponentDefinition {
            id: pollDef
            source: "PollPage.qml"
        }
    ]
}
