import bb.cascades 1.4
import bb.system 1.2

// A live location's long-press "Live location…" (the card's liveMenu), out of ChatPage.qml to keep that file short. On ours:
// stop it, add time to one with an end, or set the proximity alert; on theirs only the alert, so the distances open at
// once. The alert (location.alert) is on OUR live location in the chat: Telegram tells us when the other person comes that
// close, and refuses it (not_sharing) while we share none there. Not in a channel nor in Saved Messages, where there is no
// one to come near. The root is an invisible Container, in the page's hidden partSlot: a bb.system dialog has no
// attachedObjects to hold the second dialog and the toasts. chat is the page's chat row.
Container {
    id: live
    property variant chat
    visible: false
    // The message the menu was opened on.
    property string msgId
    // What each item of the list does, in its order: 0 stops, -1 opens the distances, any other number adds that many
    // seconds. Assigned whole: a variant's array is copied at each read.
    property variant picks: []
    // The distances, in metres; 0 turns the alert off.
    property variant radii: [0, 100, 250, 500, 1000, 2000, 5000, 10000]
    property bool filled: false
    function open(msg) {
        msgId = msg.id
        var alerts = !!chat && chat.kind != 3 && String(engine.link.me) != String(chat.jid)
        if (!msg.out) {
            if (alerts) {
                alertMenu()
            }
            return
        }
        var p = [0]
        actions.clearList()
        actions.appendItem(qsTr("Stop sharing"))
        if (msg.liveUntil > 0) {
            actions.appendItem(qsTr("Add 15 minutes"))
            actions.appendItem(qsTr("Add 1 hour"))
            actions.appendItem(qsTr("Add 8 hours"))
            p.push(900)
            p.push(3600)
            p.push(28800)
        }
        if (alerts) {
            actions.appendItem(qsTr("Proximity alert…"))
            p.push(-1)
        }
        picks = p
        actions.show()
    }
    // The distances, filled at the first: a SystemListDialog has no onCreationCompleted (ChatList.qml).
    function alertMenu() {
        if (!filled) {
            filled = true
            for (var i = 0; i < radii.length; i++) {
                var r = radii[i]
                distances.appendItem(r == 0 ? qsTr("Off") : r < 1000 ? qsTr("%1 m").arg(r) : qsTr("%1 km").arg(r / 1000))
            }
        }
        distances.show()
    }
    function pick(what) {
        if (what == 0) {
            engine.request("location.stop", {"chat": chat.jid, "id": msgId})
        } else if (what > 0) {
            engine.request("location.extend", {"chat": chat.jid, "id": msgId, "add": what})
        } else {
            alertMenu()
        }
    }
    // A refusal of ours: not_sharing says what to do; any other is one toast.
    property string failedCmd: engine.failure.cmd || ""
    onFailedCmdChanged: {
        if (failedCmd == "location.alert" && engine.failure.code == "not_sharing") {
            notSharing.show()
        } else if (failedCmd == "location.stop" || failedCmd == "location.extend" || failedCmd == "location.alert") {
            changeFailed.show()
        }
    }
    attachedObjects: [
        // A tap chooses (dismissOnSelection, as TimerAction's), so the confirm button goes.
        SystemListDialog {
            id: actions
            title: qsTr("Live location")
            confirmButton.label: ""
            cancelButton.label: qsTr("Cancel")
            dismissOnSelection: true
            onFinished: {
                if ((value == SystemUiResult.ItemSelection || value == SystemUiResult.ConfirmButtonSelection)
                        && selectedIndices.length > 0) {
                    live.pick(live.picks[selectedIndices[0]])
                }
            }
        },
        SystemListDialog {
            id: distances
            title: qsTr("Proximity alert")
            confirmButton.label: ""
            cancelButton.label: qsTr("Cancel")
            dismissOnSelection: true
            onFinished: {
                if ((value == SystemUiResult.ItemSelection || value == SystemUiResult.ConfirmButtonSelection)
                        && selectedIndices.length > 0) {
                    engine.request("location.alert", {"chat": live.chat.jid, "radius": live.radii[selectedIndices[0]]})
                }
            }
        },
        SystemToast {
            id: notSharing
            body: qsTr("Share your live location in this chat first")
        },
        SystemToast {
            id: changeFailed
            body: qsTr("Couldn't change the live location")
        }
    ]
}
