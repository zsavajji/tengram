import bb.cascades 1.4
import bb.system 1.2

// ⋮ → Pinned message (step 6.5h), out of ChatPage.qml to keep that file short. The chat's pins come
// from ChatController.pins(), newest first: none says so in a toast, one is jumped to, several are
// listed by their previews to pick from. It is always in the menu: ActionItem has no visible, and
// following the pins as they change isn't worth it.
ActionItem {
    id: action
    // The chat page's ChatController. Not named ctl: in `ctl: ctl` QML would read this property.
    property variant chatCtl
    // The page scrolls to the message and tints it (ChatPage.jump).
    signal jump(string id)
    title: qsTr("Pinned message")
    imageSource: "file:///usr/share/icons/bb_action_flag.png"
    ActionBar.placement: ActionBarPlacement.InOverflow
    onTriggered: {
        var pins = chatCtl.pins()
        if (pins.length == 0) {
            noPins.show()
        } else if (pins.length == 1) {
            go(pins[0])
        } else {
            // Filled here: a SystemListDialog has no onCreationCompleted (ChatList.qml).
            chooser.pins = pins
            chooser.clearList()
            for (var i = 0; i < pins.length; i++) {
                // A message this phone doesn't hold has no preview; a tap on "…" says so.
                chooser.appendItem(pins[i].preview || "…")
            }
            chooser.show()
        }
    }
    // The preview is "" when chats.db doesn't hold the message; reveal is -1 when it can't be shown.
    function go(pin) {
        if (pin.preview == "" || chatCtl.messages.reveal(pin.id) < 0) {
            notHere.show()
        } else {
            jump(pin.id)
        }
    }
    attachedObjects: [
        SystemToast {
            id: noPins
            body: qsTr("No pinned messages")
        },
        SystemToast {
            id: notHere
            body: qsTr("This pinned message isn't on this phone")
        },
        // A tap chooses (dismissOnSelection, as MapChooser's), so the confirm button goes: no label, no button.
        SystemListDialog {
            id: chooser
            property variant pins: []
            title: qsTr("Pinned messages")
            confirmButton.label: ""
            cancelButton.label: qsTr("Cancel")
            dismissOnSelection: true
            onFinished: {
                if ((value == SystemUiResult.ItemSelection || value == SystemUiResult.ConfirmButtonSelection)
                        && selectedIndices.length > 0) {
                    action.go(pins[selectedIndices[0]])
                }
            }
        }
    ]
}
