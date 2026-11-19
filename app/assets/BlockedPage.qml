import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0
import "errors.js" as Errors

// Privacy and security > Blocked users: the people on the block list, each as the contact picker draws a person
// (ContactRow); a tap asks to unblock. The page asks the engine itself (blocked.list) through its own Privacy, so the
// answer goes with the page.
Page {
    id: page
    property bool loaded: privacy.blockedLoaded
    property bool failed: !loaded && engine.failure.cmd == "blocked.list"
    // The person the unblock dialog asks about, held until the answer.
    property variant pending: ({})
    titleBar: PageBar {
        title: page.loaded ? qsTr("Blocked users (%1)").arg(privacy.blockedTotal) : qsTr("Blocked users")
    }
    onCreationCompleted: {
        // Not a Connections: Cascades' QML has none. The signal lives and dies with the page.
        privacy.failed.connect(page.actionFailed)
        // Not linked, there is no account to ask about.
        if (engine.linked) {
            privacy.askBlocked()
        }
    }
    function actionFailed(cmd) {
        // The list shows its own failure.
        if (cmd != "blocked.list") {
            failedToast.body = Errors.errorText(engine.failure)
            failedToast.show()
        }
    }
    function askUnblock(person) {
        pending = person
        unblockDialog.title = qsTr("Unblock %1?").arg(person.name)
        unblockDialog.show()
    }
    attachedObjects: [
        Privacy {
            id: privacy
            link: engine
        },
        SystemDialog {
            id: unblockDialog
            confirmButton.label: qsTr("Unblock")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    privacy.unblock(page.pending.jid, page.pending.kind)
                }
            }
        },
        SystemToast {
            id: failedToast
        }
    ]
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ListView {
            // The rows reach the shared emoji style and sizes through the view, as the chat list's do.
            property variant emojiStyle: nav.emojiStyle
            property int bodySize: nav.bodySize
            property int smallSize: nav.smallSize
            property string pressColor: nav.pressColor
            property variant look: settings // the tiles' Shape (Tile.qml)
            dataModel: privacy.blocked
            onTriggered: page.askUnblock(dataModel.data(indexPath))
            listItemComponents: ListItemComponent {
                type: ""
                ContactRow {
                    contact: ListItemData
                }
            }
        }
        Busy {
            running: !page.loaded && !page.failed
            text: qsTr("Loading…")
        }
        Label {
            visible: page.failed
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            multiline: true
            textStyle.textAlign: TextAlign.Center
            textStyle.color: ui.palette.secondaryTextOnPlain
            text: qsTr("Couldn't load the blocked users. Check the connection and try again.")
        }
        Label {
            visible: page.loaded && privacy.blockedTotal == 0
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            multiline: true
            textStyle.textAlign: TextAlign.Center
            textStyle.color: ui.palette.secondaryTextOnPlain
            text: qsTr("Nobody is blocked")
        }
    }
}
