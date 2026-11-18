import bb.cascades 1.4
import bb.system 1.2

// ⋮ → Self-destruct timer, in a secret chat (docs/protocol.md, chat.ttl), out of ChatPage.qml to keep that file short. A
// tap on a time sets it for the messages sent from then on, 0 turns it off; the timer shows in the chat as a service
// message, which the engine writes. Added by the page for secret chats only (ActionItem has no visible), and enabled
// while the chat is ready: a pending or a closed one can't take it.
ActionItem {
    id: action
    property string jid
    property bool ready: false
    title: qsTr("Self-destruct timer")
    imageSource: "asset:///images/timer_action.png"
    ActionBar.placement: ActionBarPlacement.InOverflow
    enabled: ready
    // Telegram's own choices, in seconds: off, 1 to 5 s, 10, 15, 30 s, 1 min, 1 h, 1 day, 1 week.
    property variant times: [0, 1, 2, 3, 4, 5, 10, 15, 30, 60, 3600, 86400, 604800]
    property bool filled: false
    onTriggered: {
        // Filled here: a SystemListDialog has no onCreationCompleted (ChatList.qml).
        if (!filled) {
            filled = true
            chooser.appendItem(qsTr("Off"))
            for (var i = 1; i < times.length; i++) {
                var t = times[i]
                chooser.appendItem(t < 60 ? qsTr("%1 s").arg(t) : t < 3600 ? qsTr("%1 min").arg(t / 60)
                                   : t < 86400 ? qsTr("%1 h").arg(t / 3600) : qsTr("%1 d").arg(t / 86400))
            }
        }
        chooser.show()
    }
    property string failedCmd: engine.failure.cmd || ""
    onFailedCmdChanged: {
        if (failedCmd == "chat.ttl") {
            ttlFailed.show()
        }
    }
    attachedObjects: [
        SystemToast {
            id: ttlFailed
            body: qsTr("Couldn't set the timer")
        },
        // A tap chooses (dismissOnSelection, as PinnedAction's), so the confirm button goes.
        SystemListDialog {
            id: chooser
            title: qsTr("Self-destruct timer")
            confirmButton.label: ""
            cancelButton.label: qsTr("Cancel")
            dismissOnSelection: true
            onFinished: {
                if ((value == SystemUiResult.ItemSelection || value == SystemUiResult.ConfirmButtonSelection)
                        && selectedIndices.length > 0) {
                    engine.request("chat.ttl", {"chat": action.jid, "secs": action.times[selectedIndices[0]]})
                }
            }
        }
    ]
}
