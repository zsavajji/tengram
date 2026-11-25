import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0

// A person's info (step 5.8), read-only but for the mute toggle and, in the action menu, the
// account actions (step 5.8b). chat is a chat list row, set right after createObject(), or a row
// built like the contact picker's for a person with no chat yet (a group's participant). The page
// asks the engine itself (contact.info) through its own ContactInfo, so the answer goes with the
// page.
Page {
    id: page
    property variant chat
    property variant info: contact.info
    property bool loaded: info.groups !== undefined
    property bool failed: !loaded && engine.failure.cmd == "contact.info"
    // The full-size picture the engine fetched with the info; its file name has the picture's id.
    property bool hasPicture: !!info.picture
    // From info, not a method call: a binding isn't re-read when a method's result changes.
    property int groupCount: loaded ? info.groups.length : 0
    property bool muted: false
    // The chat exists in chats.db. A person from a group, or the picker, may have none yet: then
    // there is nothing to mute, export, clear or delete.
    property bool hasChat: false
    // As ChatPage: a failure of the page's own requests, once per failure.
    property string failedCmd: engine.failure.cmd || ""
    // Start secret chat is asked once at a time: a second ask would make a second chat.
    property bool starting: false
    onFailedCmdChanged: {
        if (failedCmd == "contact.block" || failedCmd == "chat.export") {
            failedToast.show()
        } else if (failedCmd == "secret.start") {
            starting = false
            secretFailed.show()
        }
    }
    onChatChanged: {
        // !!: a contact picked with no chat yet has no muted, and undefined fails a bool.
        muted = !!chat.muted
        hasChat = !!chats.chat(chat.jid).jid
        contact.ask(chat.jid)
        // Not for our own chat, Saved Messages (its id is our user id, link.me), which has no one to share a secret with.
        // ponytail: a bot is offered it too (the page doesn't know it is one); Telegram's answer is the toast.
        if (String(engine.link.me) != String(chat.jid)) {
            addAction(voiceAction, ActionBarPlacement.OnBar)
            addAction(videoAction, ActionBarPlacement.OnBar)
            addAction(secretAction, ActionBarPlacement.InOverflow)
        }
    }
    onInfoChanged: fill()
    // Not a Connections: Cascades' QML has none. Both live and die with the page.
    onCreationCompleted: {
        contact.exported.connect(page.exported)
        contact.secretStarted.connect(page.secretStarted)
    }
    titleBar: PageBar {
        title: qsTr("Contact info")
    }
    actions: [
        // Opens the person's chat, which may not exist yet (nav.openRow).
        ActionItem {
            title: qsTr("Message")
            imageSource: "file:///usr/share/icons/bb_action_sendtextmessage.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: nav.openRow(page.chat)
        },
        ActionItem {
            title: qsTr("Export chat")
            imageSource: "asset:///images/save_action.png"
            enabled: page.hasChat
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                contact.exportChat()
            }
        },
        ActionItem {
            title: qsTr("Clear chat")
            imageSource: "file:///usr/share/icons/bb_action_delete.png"
            enabled: page.hasChat
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                clearDialog.show()
            }
        },
        // Unblock asks nothing: it undoes the block, which is the confirmation.
        ActionItem {
            title: page.info.blocked ? qsTr("Unblock") : qsTr("Block")
            imageSource: "asset:///images/block_action.png"
            enabled: page.loaded
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                if (page.info.blocked) {
                    contact.block(false)
                } else {
                    blockDialog.show()
                }
            }
        },
        DeleteActionItem {
            title: qsTr("Delete chat")
            enabled: page.hasChat
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                deleteDialog.show()
            }
        }
    ]
    attachedObjects: [
        ContactInfo {
            id: contact
            link: engine
        },
        // The calls, as HyperBerry's contact page has them; added in onChatChanged for a person other than ourselves
        // (the engine would answer bad_chat). call.start's failure is worded by main.qml's toast.
        ActionItem {
            id: voiceAction
            title: qsTr("Voice call")
            imageSource: "file:///usr/share/icons/ic_phone.png"
            enabled: engine.linked
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: engine.request("call.start", {"chat": page.chat.jid})
        },
        ActionItem {
            id: videoAction
            title: qsTr("Video call")
            imageSource: "file:///usr/share/icons/ic_chat_video.png"
            enabled: engine.linked
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: engine.request("call.start", {"chat": page.chat.jid, "video": true})
        },
        // Added in onChatChanged for a person other than ourselves (ActionItem has no visible). A secret chat's page
        // names the chat itself (the person's id isn't its jid): a closed one is started again from it.
        ActionItem {
            id: secretAction
            title: qsTr("Start secret chat")
            imageSource: "asset:///images/lock_action.png"
            enabled: !page.starting
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.starting = true
                contact.startSecret(page.chat.secret > 0)
            }
        },
        // A group in common: the same row as a participant on the group info page.
        ComponentDefinition {
            id: groupDef
            TapArea {
                id: row
                property variant group
                // The group's picture from the chat list (chats.db), over its initials; none when we left it.
                property string photo: row.group ? chats.chat(row.group.jid).photo || "" : ""
                property bool pressed: false
                horizontalAlignment: HorizontalAlignment.Fill
                // The Classic's trackpad reaches the row: it takes the focus, lit as while pressed, and a click (Enter) opens the group.
                focusPolicy: FocusPolicy.KeyAndTouch
                background: pressed || focused ? Color.create(nav.pressColor) : Color.Transparent
                onClicked: nav.openRequested(row.group.jid)
                onTouch: row.pressed = event.isDown() || event.isMove()
                onTouchExit: row.pressed = false
                // A 7 du tile in a 9.4 du row; on the Passport (1440 px), unless Compact, 9 in 11.4 (the user, 2026-11-25: small
                // there).
                Container {
                    id: groupLine
                    property bool big: screenWidth >= 1440 && !settings.compact
                    horizontalAlignment: HorizontalAlignment.Fill
                    minHeight: ui.du(big ? 11.4 : 9.4)
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Tile {
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: ui.du(groupLine.big ? 9 : 7)
                        preferredHeight: ui.du(groupLine.big ? 9 : 7)
                        tint: row.group ? row.group.color : ""
                        initials: row.group ? row.group.initials : ""
                        photo: row.photo
                        emojiStyle: nav.emojiStyle
                    }
                    Label {
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(1.5)
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        text: row.group ? row.group.name : ""
                        textFormat: TextFormat.Plain
                        textStyle.base: nav.emojiStyle
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: nav.bodySize
                    }
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.px(2)
                    background: ui.palette.plain
                }
            }
        },
        SystemDialog {
            id: clearDialog
            title: qsTr("Clear this chat?")
            // Not "from this phone": the history goes from the account, on every device; an active secret chat's on both sides.
            body: page.chat && (page.chat.secret == 1 || page.chat.secret == 2)
                  ? qsTr("Its messages will be deleted for both of you.")
                  : qsTr("Its messages will be deleted for you on all your devices. The others in the chat keep theirs.")
            confirmButton.label: qsTr("Clear")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    engine.request("chat.clear", {"chat": page.chat.jid})
                }
            }
        },
        SystemDialog {
            id: blockDialog
            title: qsTr("Block %1?").arg(page.chat ? page.chat.name : "")
            body: qsTr("Blocked contacts can't call you or send you messages.")
            confirmButton.label: qsTr("Block")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    contact.block(true)
                }
            }
        },
        SystemDialog {
            id: deleteDialog
            title: qsTr("Delete this chat?")
            body: page.chat && (page.chat.secret == 1 || page.chat.secret == 2)
                  ? qsTr("The secret chat ends, and its messages are deleted for both of you.")
                  : qsTr("Its messages will be deleted for you on all your devices, and the chat leaves your list.")
            confirmButton.label: qsTr("Delete")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    engine.request("chat.delete", {"chat": page.chat.jid})
                    // The chat page under this one goes too: the chat is gone.
                    nav.backToList()
                }
            }
        },
        SystemToast {
            id: exportedToast
        },
        SystemToast {
            id: secretFailed
            body: qsTr("Couldn't start a secret chat")
        },
        SystemToast {
            id: failedToast
            body: qsTr("Couldn't do that. Check the connection and try again.")
        }
    ]
    // ponytail: on mutes always; the list's menu offers 8 hours and a week too.
    function setMuted(on) {
        if (on == muted) {
            return
        }
        muted = on
        engine.request("chat.set", {"chat": chat.jid, "mute": on ? -1 : 0})
    }
    // The secret chat is in chats.db: it opens, over the list (the pages above it go), as a notification's tap does.
    function secretStarted(jid) {
        nav.openRequested(jid)
    }
    // The body is set when the export is done: it names the file.
    function exported(path) {
        exportedToast.body = qsTr("Saved to Downloads: %1").arg(path.substring(path.lastIndexOf("/") + 1))
        exportedToast.show()
    }
    function fill() {
        groupsBox.removeAll()
        if (info && info.groups) {
            for (var i = 0; i < info.groups.length; i++) {
                var row = groupDef.createObject()
                row.group = info.groups[i]
                groupsBox.add(row)
            }
        }
    }
    // One scrolling column, groups included: a ListView inside a ScrollView misbehaves.
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ScrollView {
            horizontalAlignment: HorizontalAlignment.Fill // filling the page from the start: as wide as its
            verticalAlignment: VerticalAlignment.Fill     // content, it opened narrow and grew (the user, 2026-11-20)
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                // A ScrollView's content is as wide as its widest child, not the screen: with only the error text
                // shown, everything centred in its width, off to the left (the user's screenshot, 2026-11-19).
                preferredWidth: ui.px(screenWidth)
                // The tile, name and number; the chat's own until the engine answers.
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(3)
                    // Air under the number, which sat on the About header (the user's screenshot, 2026-11-19).
                    bottomPadding: ui.du(2)
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    // A disc in Round mode, the picture asked at 400 px for its 20 du (the user, 2026-11-19).
                    // The picture opens full size on a tap, and on the Classic's trackpad: focusable only when there is one.
                    Tile {
                        id: bigPhoto
                        horizontalAlignment: HorizontalAlignment.Center
                        preferredWidth: ui.du(20)
                        preferredHeight: ui.du(20)
                        focusPolicy: page.hasPicture ? FocusPolicy.KeyAndTouch : FocusPolicy.None
                        opacity: bigPhoto.focused ? 0.6 : 1
                        tint: page.chat ? page.chat.color : ""
                        initials: page.chat ? page.chat.initials : ""
                        photo: page.hasPicture ? page.info.picture : ""
                        emojiStyle: nav.emojiStyle
                        initialsSize: 300
                        photoSize: 400
                        gestureHandlers: TapHandler {
                            onTapped: {
                                if (page.hasPicture) {
                                    nav.showPhoto(page.info.picture)
                                }
                            }
                        }
                        keyListeners: KeyListener {
                            onKeyReleased: {
                                if (event.key == 13 || event.key == 10) { // Enter: the trackpad's click
                                    nav.showPhoto(page.info.picture)
                                }
                            }
                        }
                    }
                    Label {
                        horizontalAlignment: HorizontalAlignment.Center
                        multiline: true
                        textStyle.textAlign: TextAlign.Center
                        // Saved in the address book while open: the chat keeps the row it opened with.
                        text: page.chat ? (chats.revision >= 0 && chats.chat(page.chat.jid).name) || page.chat.name : ""
                        textFormat: TextFormat.Plain
                        textStyle.base: nav.emojiStyle
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: nav.bodySize * 1.3
                    }
                    // In the primary colour, as a link: a tap saves the number in the phone's contacts.
                    // The Classic's trackpad reaches the number too: it takes the focus, dimmed, and a click (Enter) saves it.
                    Label {
                        id: phoneLink
                        visible: !!page.info.phone
                        horizontalAlignment: HorizontalAlignment.Center
                        focusPolicy: FocusPolicy.KeyAndTouch
                        opacity: phoneLink.focused ? 0.5 : 1
                        text: page.info.phone || ""
                        textFormat: TextFormat.Plain
                        textStyle.base: nav.emojiStyle
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: nav.bodySize
                        textStyle.color: ui.palette.primary
                        gestureHandlers: TapHandler {
                            onTapped: contact.saveNumber(page.info.phone)
                        }
                        keyListeners: KeyListener {
                            onKeyReleased: {
                                if (event.key == 13 || event.key == 10) { // Enter: the trackpad's click
                                    contact.saveNumber(page.info.phone)
                                }
                            }
                        }
                    }
                }
                Busy {
                    running: !page.loaded && !page.failed
                    topPadding: ui.du(4)
                    text: qsTr("Loading…")
                }
                Label {
                    visible: page.failed
                    horizontalAlignment: HorizontalAlignment.Center
                    topMargin: ui.du(3)
                    multiline: true
                    textStyle.textAlign: TextAlign.Center
                    textStyle.color: ui.palette.secondaryTextOnPlain
                    text: qsTr("Couldn't load the contact's info. Check the connection and try again.")
                }
                Header {
                    visible: page.loaded && !!page.info.about
                    title: qsTr("About")
                }
                // Padding, not margins: margins only space siblings, never from the container's edge.
                Container {
                    visible: page.loaded && !!page.info.about
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(1.5)
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    bottomPadding: ui.du(1.5)
                    Label {
                        horizontalAlignment: HorizontalAlignment.Fill
                        multiline: true
                        text: page.info.about || ""
                        textFormat: TextFormat.Plain
                        textStyle.base: nav.emojiStyle
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: nav.bodySize
                    }
                }
                Container {
                    visible: page.hasChat
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(1.5)
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    bottomPadding: ui.du(1.5)
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Label {
                        verticalAlignment: VerticalAlignment.Center
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        text: qsTr("Mute notifications")
                    }
                    ToggleButton {
                        verticalAlignment: VerticalAlignment.Center
                        checked: page.muted
                        onCheckedChanged: page.setMuted(checked)
                    }
                }
                Header {
                    visible: page.groupCount > 0
                    title: page.groupCount == 1 ? qsTr("1 group in common") : qsTr("%1 groups in common").arg(page.groupCount)
                }
                Container {
                    id: groupsBox
                    horizontalAlignment: HorizontalAlignment.Fill
                }
            }
        }
    }
}
