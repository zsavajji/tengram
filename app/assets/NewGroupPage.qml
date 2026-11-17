import bb.cascades 1.4
import bb.system 1.2
import hyperberry 1.0

// New group (step 6.6): name it, tick the contacts, Create. created fires once WhatsApp made the
// group and the engine has its chat in chats.db; ChatsPage opens it.
Page {
    id: page
    signal created(string jid, int refused)
    property bool creating: false
    property string failedCmd: engine.failure.cmd || ""
    onFailedCmdChanged: {
        if (failedCmd == "group.create") {
            creating = false
            failed.body = engine.failure.code == "name_too_long" ? qsTr("WhatsApp won't take a name that long") : qsTr("Couldn't create the group. Check the connection and try again.")
            failed.show()
        }
    }
    titleBar: TitleBar {
        title: qsTr("New group")
    }
    actions: ActionItem {
        title: qsTr("Create")
        imageSource: "asset:///images/tick_action.png"
        ActionBar.placement: ActionBarPlacement.OnBar
        enabled: !page.creating
        onTriggered: page.create()
    }
    attachedObjects: [
        ContactPicker {
            id: picker
            link: engine
            onGroupCreated: page.created(jid, refused)
        },
        SystemToast {
            id: incomplete
            body: qsTr("A group needs a name and someone in it")
        },
        SystemToast {
            id: notSent
            body: qsTr("Not sent: HyperBerry is reconnecting. Try again in a moment.")
        },
        SystemToast {
            id: failed
        }
    ]
    onCreationCompleted: picker.load()
    // Called after the push, so on the Q10 typing names the group at once (as NewChatPage.focusSearch).
    function focusName() {
        name.requestFocus()
    }
    function create() {
        if (name.text.trim() == "" || picker.chosenCount == 0) {
            incomplete.show()
            return
        }
        if (picker.createGroup(name.text.trim()) == "") {
            notSent.show()
            return
        }
        creating = true
    }
    Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            // Padding, not margins: margins only space siblings, never from the container's edge.
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                leftPadding: ui.du(1.5)
                rightPadding: ui.du(1.5)
                topPadding: ui.du(1.5)
                bottomPadding: ui.du(1.5)
                TextField {
                    id: name
                    hintText: qsTr("Group name")
                }
                TextField {
                    id: search
                    hintText: qsTr("Search contacts")
                    input.submitKey: SubmitKey.Search
                    onTextChanging: picker.filter(text)
                }
                Label {
                    visible: picker.chosenCount > 0
                    textStyle.color: ui.palette.secondaryTextOnPlain
                    text: qsTr("%1 chosen").arg(picker.chosenCount)
                }
            }
            ListView {
                // The rows reach the shared emoji style and sizes through the view, as the chat list's do.
                property variant emojiStyle: nav.emojiStyle
                property int bodySize: nav.bodySize
                property int smallSize: nav.smallSize
                property string pressColor: nav.pressColor
                dataModel: picker.model
                onTriggered: picker.toggle(indexPath)
                listItemComponents: ListItemComponent {
                    type: ""
                    ContactRow {
                        contact: ListItemData
                    }
                }
            }
        }
        Busy {
            running: !picker.loaded
            text: qsTr("Loading…")
        }
        Busy {
            running: page.creating
            text: qsTr("Creating the group…")
        }
        Label {
            visible: picker.loaded && picker.shown == 0
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            multiline: true
            textStyle.textAlign: TextAlign.Center
            textStyle.color: ui.palette.secondaryTextOnPlain
            text: picker.total == 0 ? qsTr("No contacts yet. Your phone sends them shortly after linking.") : qsTr("No contact matches.")
        }
    }
}
