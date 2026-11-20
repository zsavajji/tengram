import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0

// A channel's info (step 6.6h): picture, name, followers, description, and Unfollow. chat is a chat
// list row, set right after createObject(). The page asks the engine itself (channel.info) through
// its own ContactInfo, so the answer goes with the page.
Page {
    id: page
    property variant chat
    property variant info: channel.info
    property bool loaded: info.followers !== undefined
    property bool failed: !loaded && engine.failure.cmd == "channel.info"
    // The full size once the engine answers; the list's preview until then, or when there is no full one.
    property string picture: info.picture || (chat && chat.photo) || ""
    property bool hasPicture: picture != ""
    onChatChanged: channel.ask(chat.jid)
    titleBar: PageBar {
        title: qsTr("Channel info")
    }
    actions: [
        ActionItem {
            title: qsTr("Unfollow")
            imageSource: "file:///usr/share/icons/bb_action_delete.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: unfollowDialog.show()
        }
    ]
    attachedObjects: [
        ContactInfo {
            id: channel
            link: engine
            command: "channel.info"
        },
        SystemDialog {
            id: unfollowDialog
            title: qsTr("Unfollow this channel?")
            body: page.chat ? page.chat.name : ""
            confirmButton.label: qsTr("Unfollow")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    engine.request("channel.unfollow", {"chat": page.chat.jid})
                    // This page and the chat page under it go: the chat is gone. The user lands on
                    // whatever is below (the chat list), as backToList in main.qml.
                    for (var i = 0; i < 2 && nav.count() > 1; i++) {
                        var old = nav.at(nav.count() - 1)
                        nav.remove(old)
                        old.destroy()
                    }
                }
            }
        }
    ]
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
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(3)
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
                        photo: page.picture
                        emojiStyle: nav.emojiStyle
                        initialsSize: 300
                        photoSize: 400
                        gestureHandlers: TapHandler {
                            onTapped: {
                                if (page.hasPicture) {
                                    nav.showPhoto(page.picture)
                                }
                            }
                        }
                        keyListeners: KeyListener {
                            onKeyReleased: {
                                if (event.key == 13 || event.key == 10) { // Enter: the trackpad's click
                                    nav.showPhoto(page.picture)
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
                    Label {
                        visible: page.loaded
                        horizontalAlignment: HorizontalAlignment.Center
                        // 0 until channel.info answers: the hidden label is still evaluated, and qsTr's
                        // count must be a number (undefined logged an error on every open, 0.0.1.178).
                        text: qsTr("%Ln followers", "", page.info.followers || 0)
                        textStyle.textAlign: TextAlign.Center
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: nav.bodySize
                        textStyle.color: ui.palette.secondaryTextOnPlain
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
                    text: qsTr("Couldn't load the channel's info. Check the connection and try again.")
                }
                Header {
                    visible: page.loaded && !!page.info.description
                    title: qsTr("Description")
                }
                // Padding, not margins: margins only space siblings, never from the container's edge.
                Container {
                    visible: page.loaded && !!page.info.description
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(1.5)
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    bottomPadding: ui.du(1.5)
                    Label {
                        horizontalAlignment: HorizontalAlignment.Fill
                        multiline: true
                        text: page.info.description || ""
                        // Active, so a link in it can be tapped.
                        content.flags: TextContentFlag.ActiveText
                        textFormat: TextFormat.Plain
                        textStyle.base: nav.emojiStyle
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: nav.bodySize
                    }
                }
            }
        }
    }
}
