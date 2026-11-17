import bb.cascades 1.4
import bb.system 1.2

// A group's info (PLAN.md §7, step 3.5). A tap on a participant opens their contact page (step
// 5.8). An admin also adds people (the action bar), as does anyone when the group lets every
// member add, and removes, promotes and demotes them (a participant's long-press menu). ctl is the chat page's ChatController, which asked the engine
// for group.info; the chat page stays under this one, so ctl outlives it.
Page {
    id: page
    property variant chat
    property variant ctl
    property variant info: ctl ? ctl.groupInfo : {}
    property bool loaded: info.participants !== undefined
    property bool failed: !loaded && engine.failure.cmd == "group.info"
    // The full-size picture the engine fetched with the info; its file name has the picture's id.
    property bool hasPicture: !!info.picture
    // We are one of the group's admins; set by fill().
    property bool amAdmin: false
    // The group's "Add other members" setting, else admins only.
    property bool canAdd: amAdmin || !!info.membersAdd
    // The participant the remove dialog asks about.
    property variant target
    onInfoChanged: fill()
    onCtlChanged: fill()
    // ActionItem has no visible: Add participant is on the bar only for those who may add.
    onCanAddChanged: {
        if (canAdd) {
            addAction(addItem, ActionBarPlacement.OnBar)
        } else {
            removeAction(addItem)
        }
    }
    // As ChatPage: a failure of the page's own requests, once per failure.
    property string failedCmd: engine.failure.cmd || ""
    onFailedCmdChanged: {
        if (failedCmd == "group.participants") {
            changeFailed.body = refusal(engine.failure)
            changeFailed.show()
        }
    }
    titleBar: TitleBar {
        title: qsTr("Group info")
    }
    attachedObjects: [
        ArrayDataModel {
            id: people
        },
        ComponentDefinition {
            id: contactDef
            source: "ContactInfoPage.qml"
        },
        ComponentDefinition {
            id: pickerDef
            source: "NewChatPage.qml"
        },
        ActionItem {
            id: addItem
            title: qsTr("Add participant")
            imageSource: "file:///usr/share/icons/ic_add_contact.png"
            onTriggered: page.addPerson()
        },
        SystemDialog {
            id: removeDialog
            title: qsTr("Remove %1 from the group?").arg(page.target ? page.target.name || qsTr("Unknown") : "")
            confirmButton.label: qsTr("Remove")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    page.ctl.changeParticipant(page.target.jid, "remove")
                }
            }
        },
        SystemToast {
            id: changeFailed
        }
    ]
    // WhatsApp refuses per person: 403 is their privacy settings (on an add), 409 that they're
    // already in.
    function refusal(f) {
        if (f.code == "refused" && f.message == "403") {
            return qsTr("Their privacy settings don't let you add them. Invite them with the group's link from your main phone.")
        }
        if (f.code == "refused" && f.message == "409") {
            return qsTr("They're already in the group.")
        }
        if (f.code == "refused") {
            return qsTr("WhatsApp refused that (%1).").arg(f.message)
        }
        return qsTr("Couldn't do that. Check the connection and try again.")
    }
    // The New chat picker under another title; it goes on the pick, and the list comes again.
    function addPerson() {
        var p = pickerDef.createObject()
        p.title = qsTr("Add participant")
        p.picked.connect(page.personPicked)
        nav.push(p)
        p.focusSearch()
    }
    // main.qml destroys the popped picker.
    function personPicked(person) {
        ctl.changeParticipant(person.jid, "add")
        nav.pop()
    }
    function askRemove(p) {
        target = p
        removeDialog.show()
    }
    // Not our own row. The chat's row when there is a one-to-one chat (participants are keyed LID
    // first, as chats are); else a row built like ContactPicker's, so the page and its Message
    // work for a person with no chat yet. The participant's colour is the sender colour: fine
    // for a tile here.
    function openPerson(p) {
        if (p.me) {
            return
        }
        var c = contactDef.createObject()
        var row = chats.chat(p.jid)
        c.chat = row.jid ? row : {"kind": 0, "jid": p.jid, "name": p.name || p.jid.split("@")[0],
                                  "initials": p.initials, "color": p.color}
        nav.push(c)
    }
    // The same people in the same places (a picture came): only the rows that changed are replaced,
    // so the list keeps its scroll. Another set clears and fills.
    function fill() {
        var ps = info && info.participants ? info.participants : []
        var same = people.size() == ps.length
        var i
        for (i = 0; same && i < ps.length; i++) {
            same = people.value(i).jid == ps[i].jid
        }
        if (same) {
            for (i = 0; i < ps.length; i++) {
                var old = people.value(i)
                if ((old.photo || "") != (ps[i].photo || "") || old.admin != ps[i].admin || old.name != ps[i].name) {
                    people.replace(i, ps[i])
                }
            }
        } else {
            people.clear()
            if (ps.length > 0) {
                people.append(ps)
            }
        }
        var admin = false
        for (i = 0; i < ps.length; i++) {
            admin = admin || (ps[i].me && ps[i].admin)
        }
        amAdmin = admin
    }
    Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        Container {
            visible: page.loaded || !page.failed
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            // The group's tile and subject; the chat's name until the engine answers.
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                leftPadding: ui.du(1.5)
                rightPadding: ui.du(1.5)
                topPadding: ui.du(1.5)
                bottomPadding: ui.du(1.5)
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                Container {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(7)
                    preferredHeight: ui.du(7)
                    background: page.chat ? Color.create(page.chat.color) : Color.Transparent
                    layout: DockLayout {}
                    Label {
                        visible: !page.hasPicture
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        text: page.chat ? page.chat.initials : ""
                        textFormat: TextFormat.Plain
                        textStyle.base: nav.emojiStyle
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: 118
                        textStyle.color: Color.White
                    }
                    ImageView {
                        visible: page.hasPicture
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        imageSource: page.hasPicture ? "file://" + page.info.picture : ""
                        scalingMethod: ScalingMethod.AspectFill
                        loadEffect: ImageViewLoadEffect.None
                        gestureHandlers: TapHandler {
                            onTapped: nav.showPhoto(page.info.picture)
                        }
                    }
                }
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    leftMargin: ui.du(1.5)
                    topMargin: 0
                    bottomMargin: 0
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    multiline: true
                    text: page.info.subject || (page.chat ? page.chat.name : "")
                    textFormat: TextFormat.Plain
                    textStyle.base: nav.emojiStyle
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: nav.bodySize
                }
            }
            // Padding, not margins: margins only space siblings, never from the container's edge.
            Container {
                visible: !!page.info.description
                horizontalAlignment: HorizontalAlignment.Fill
                leftPadding: ui.du(1.5)
                rightPadding: ui.du(1.5)
                bottomPadding: ui.du(1.5)
                Label {
                    horizontalAlignment: HorizontalAlignment.Fill
                    multiline: true
                    text: page.info.description || ""
                    textFormat: TextFormat.Plain
                    textStyle.base: nav.emojiStyle
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: nav.smallSize
                    textStyle.color: ui.palette.secondaryTextOnPlain
                }
            }
            // From info, not people.size(): a method call in a binding isn't re-read when the model fills.
            Header {
                visible: page.loaded
                title: !page.loaded ? "" : page.info.participants.length == 1 ? qsTr("1 participant") : qsTr("%1 participants").arg(page.info.participants.length)
            }
            ListView {
                // The rows reach the shared emoji style and sizes through the view, as the chat list's do.
                property variant emojiStyle: nav.emojiStyle
                property int bodySize: nav.bodySize
                property int smallSize: nav.smallSize
                property string pressColor: nav.pressColor
                // The rows' long-press menu reaches the page through the view, as the message cards' do.
                property bool admin: page.amAdmin
                function change(p, action) {
                    page.ctl.changeParticipant(p.jid, action)
                }
                function askRemove(p) {
                    page.askRemove(p)
                }
                dataModel: people
                onTriggered: page.openPerson(people.data(indexPath))
                listItemComponents: ListItemComponent {
                    type: ""
                    Container {
                        id: row
                        property variant person: ListItemData
                        horizontalAlignment: HorizontalAlignment.Fill
                        background: row.ListItem.active ? Color.create(row.ListItem.view.pressColor) : Color.Transparent
                        // Only an admin changes members, and not themselves: no menu at all
                        // otherwise. The set is the same on every row (rows are recycled).
                        contextMenuHandler: ContextMenuHandler {
                            onPopulating: {
                                if (!row.ListItem.view.admin || row.person.me) {
                                    event.abort()
                                }
                            }
                        }
                        contextActions: [
                            ActionSet {
                                title: row.person.name || qsTr("Unknown")
                                ActionItem {
                                    title: row.person.admin ? qsTr("Dismiss as admin") : qsTr("Make group admin")
                                    imageSource: "file:///usr/share/icons/ic_accept.png"
                                    onTriggered: row.ListItem.view.change(row.person, row.person.admin ? "demote" : "promote")
                                }
                                DeleteActionItem {
                                    title: qsTr("Remove from group")
                                    onTriggered: row.ListItem.view.askRemove(row.person)
                                }
                            }
                        ]
                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            minHeight: ui.du(9.4)
                            leftPadding: ui.du(1.5)
                            rightPadding: ui.du(1.5)
                            layout: StackLayout {
                                orientation: LayoutOrientation.LeftToRight
                            }
                            Container {
                                verticalAlignment: VerticalAlignment.Center
                                preferredWidth: ui.du(7)
                                preferredHeight: ui.du(7)
                                background: Color.create(ListItemData.color)
                                layout: DockLayout {}
                                Label {
                                    horizontalAlignment: HorizontalAlignment.Center
                                    verticalAlignment: VerticalAlignment.Center
                                    text: ListItemData.initials
                                    textFormat: TextFormat.Plain
                                    textStyle.base: row.ListItem.view.emojiStyle
                                    textStyle.fontSize: FontSize.PercentageValue
                                    textStyle.fontSizeValue: 118
                                    textStyle.color: Color.White
                                }
                                ImageView {
                                    visible: !!ListItemData.photo
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    verticalAlignment: VerticalAlignment.Fill
                                    imageSource: ListItemData.photo ? "file://" + ListItemData.photo : ""
                                    scalingMethod: ScalingMethod.AspectFill
                                    loadEffect: ImageViewLoadEffect.None
                                }
                            }
                            Label {
                                verticalAlignment: VerticalAlignment.Center
                                leftMargin: ui.du(1.5)
                                layoutProperties: StackLayoutProperties {
                                    spaceQuota: 1
                                }
                                text: ListItemData.me ? qsTr("You") : ListItemData.name || qsTr("Unknown")
                                textFormat: TextFormat.Plain
                                textStyle.base: row.ListItem.view.emojiStyle
                                textStyle.fontSize: FontSize.PercentageValue
                                textStyle.fontSizeValue: row.ListItem.view.bodySize
                            }
                            Label {
                                visible: ListItemData.admin
                                verticalAlignment: VerticalAlignment.Center
                                leftMargin: ui.du(1.5)
                                text: qsTr("Admin")
                                textStyle.base: row.ListItem.view.emojiStyle
                                textStyle.fontSize: FontSize.PercentageValue
                                textStyle.fontSizeValue: row.ListItem.view.smallSize
                                textStyle.color: ui.palette.primary
                            }
                        }
                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            preferredHeight: ui.px(2)
                            background: ui.palette.plain
                        }
                    }
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
            text: qsTr("Couldn't load the group's info. Check the connection and try again.")
        }
    }
}
