import bb.cascades 1.4
import bb.cascades.pickers 1.0
import bb.system 1.2
import tengram 1.0

// My profile (step 5.9), from the swipe-down menu's Profile, laid out as ContactInfoPage: the
// picture, name and number, then the about. A tap on the picture, the name or the about edits it.
// The page asks the engine itself through its own Profile, so the answer goes with the page.
Page {
    id: page
    property variant info: profile.info
    property bool loaded: info.phone !== undefined
    property bool failed: !loaded && engine.failure.cmd == "profile.get"
    property bool hasPicture: !!info.picture
    // Bumped on each reply: the picture's path is always the same, and the ImageView would
    // otherwise show the first one it loaded.
    property int rev: 0
    onInfoChanged: rev++
    titleBar: TitleBar {
        title: qsTr("Profile")
    }
    onCreationCompleted: {
        // Not a Connections: Cascades' QML has none. The signal lives and dies with the page.
        profile.failed.connect(profileFailedToast.show)
        // Not linked, there is no profile to ask for.
        if (engine.linked) {
            profile.ask()
        }
    }
    function changePhoto() {
        if (!loaded) {
            return
        }
        photoDialog.clearList()
        photoDialog.appendItem(qsTr("Change photo"), true, true)
        if (hasPicture) {
            photoDialog.appendItem(qsTr("Remove photo"))
        }
        photoDialog.show()
    }
    function editName() {
        namePrompt.inputField.defaultText = info.name || ""
        namePrompt.show()
    }
    function editAbout() {
        aboutPrompt.inputField.defaultText = info.about || ""
        aboutPrompt.show()
    }
    attachedObjects: [
        Profile {
            id: profile
            link: engine
        },
        // The system's picker with its crop tool; Profile makes the 640x640 JPEG from the crop.
        FilePicker {
            id: photoPicker
            type: FileType.Picture
            mode: FilePickerMode.Picker
            imageCropEnabled: true
            title: qsTr("Change photo")
            onFileSelected: {
                if (!profile.setPhoto(selectedFiles[0])) {
                    unreadableToast.show()
                }
            }
        },
        SystemListDialog {
            id: photoDialog
            title: qsTr("Profile photo")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value != SystemUiResult.ConfirmButtonSelection) {
                    return
                }
                if (selectedIndices[0] == 0) {
                    photoPicker.open()
                } else {
                    profile.removePhoto()
                }
            }
        },
        SystemPrompt {
            id: namePrompt
            title: qsTr("Your name")
            inputField.maximumLength: 25
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    profile.setName(inputFieldTextEntry())
                }
            }
        },
        SystemPrompt {
            id: aboutPrompt
            title: qsTr("About")
            inputField.maximumLength: 139
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    profile.setAbout(inputFieldTextEntry())
                }
            }
        },
        SystemToast {
            id: profileFailedToast
            body: qsTr("Couldn't update your profile. Check the connection and try again.")
        },
        SystemToast {
            id: unreadableToast
            body: qsTr("Couldn't read that picture.")
        },
        SystemDialog {
            id: logoutDialog
            title: qsTr("Log out?")
            body: qsTr("Tengram logs out of Telegram on this phone and deletes its chats here. Your chats stay on Telegram.")
            confirmButton.label: qsTr("Log out")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection)
                    engine.request("link.logout", {})
            }
        }
    ]
    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            // The picture or the tile, the name and the number.
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(3)
                leftPadding: ui.du(1.5)
                rightPadding: ui.du(1.5)
                // A disc in Round mode, the picture asked at 400 px for its 20 du (the user, 2026-11-19); before the engine answers it
                // is the stock plain grey, as the palette's (#323232 dark, #F0F0F0 bright).
                Tile {
                    horizontalAlignment: HorizontalAlignment.Center
                    preferredWidth: ui.du(20)
                    preferredHeight: ui.du(20)
                    tint: page.loaded ? page.info.color : settings.theme == 1 ? "#F0F0F0" : "#323232"
                    initials: page.loaded ? page.info.initials : ""
                    photo: page.hasPicture ? page.info.picture : ""
                    reload: "?" + page.rev
                    emojiStyle: nav.emojiStyle
                    initialsSize: 300
                    photoSize: 400
                    gestureHandlers: TapHandler {
                        onTapped: page.changePhoto()
                    }
                }
                // The name and its hint are one target.
                Container {
                    id: nameBox
                    property bool pressed: false
                    visible: page.loaded
                    horizontalAlignment: HorizontalAlignment.Fill
                    background: pressed ? Color.create(nav.pressColor) : Color.Transparent
                    Label {
                        horizontalAlignment: HorizontalAlignment.Center
                        bottomMargin: 0
                        multiline: true
                        textStyle.textAlign: TextAlign.Center
                        text: page.info.name || ""
                        textFormat: TextFormat.Plain
                        textStyle.base: nav.emojiStyle
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: nav.bodySize * 1.3
                    }
                    Label {
                        horizontalAlignment: HorizontalAlignment.Center
                        topMargin: 0
                        text: qsTr("Tap to edit")
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                    gestureHandlers: TapHandler {
                        onTapped: page.editName()
                    }
                    onTouch: nameBox.pressed = event.isDown() || event.isMove()
                    onTouchExit: nameBox.pressed = false
                }
                // Read-only: the number is the account's.
                Label {
                    visible: page.loaded
                    horizontalAlignment: HorizontalAlignment.Center
                    text: page.info.phone || ""
                    textFormat: TextFormat.Plain
                    textStyle.base: nav.emojiStyle
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
                text: qsTr("Couldn't load your profile. Check the connection and try again.")
            }
            // Shown even when empty, so there is always something to tap.
            Header {
                visible: page.loaded
                title: qsTr("About")
            }
            // Padding, not margins: margins only space siblings, never from the container's edge.
            Container {
                id: aboutBox
                property bool pressed: false
                visible: page.loaded
                horizontalAlignment: HorizontalAlignment.Fill
                background: pressed ? Color.create(nav.pressColor) : Color.Transparent
                topPadding: ui.du(1.5)
                leftPadding: ui.du(1.5)
                rightPadding: ui.du(1.5)
                bottomPadding: ui.du(1.5)
                Label {
                    horizontalAlignment: HorizontalAlignment.Fill
                    bottomMargin: 0
                    multiline: true
                    text: page.info.about || ""
                    textFormat: TextFormat.Plain
                    textStyle.base: nav.emojiStyle
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: nav.bodySize
                }
                Label {
                    topMargin: 0
                    text: qsTr("Tap to edit")
                    textStyle.base: SystemDefaults.TextStyles.SmallText
                    textStyle.color: ui.palette.secondaryTextOnPlain
                }
                gestureHandlers: TapHandler {
                    onTapped: page.editAbout()
                }
                onTouch: aboutBox.pressed = event.isDown() || event.isMove()
                onTouchExit: aboutBox.pressed = false
            }
            // Last: the one action that ends the link. In a padded container, since margins never
            // space from the edge of the page's own container.
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topMargin: ui.du(4)
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                bottomPadding: ui.du(2)
                Button {
                    horizontalAlignment: HorizontalAlignment.Fill
                    text: qsTr("Log out")
                    enabled: engine.linked
                    onClicked: logoutDialog.show()
                }
            }
        }
    }
}
