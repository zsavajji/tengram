import bb.cascades 1.4

// ⋮ → Create poll (step 6.6e), out of ChatPage.qml to keep that file short. People's and groups' chats only: the page adds it for those, as Contact info.
ActionItem {
    id: action
    property string jid
    title: qsTr("Create poll")
    imageSource: "file:///usr/share/icons/ic_accept.png"
    ActionBar.placement: ActionBarPlacement.InOverflow
    onTriggered: {
        var p = pollDef.createObject()
        p.jid = action.jid
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
