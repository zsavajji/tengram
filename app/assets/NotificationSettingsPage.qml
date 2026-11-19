import bb.cascades 1.4

// Settings > Notifications: saved as they change. Each lives in `settings`.
Page {
    id: page
    titleBar: TitleBar {
        title: qsTr("Notifications")
    }
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ScrollView {
            Container {
                topPadding: ui.du(2)
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                bottomPadding: ui.du(2)
                Container {
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Container {
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        verticalAlignment: VerticalAlignment.Center
                        Label {
                            text: qsTr("Notification preview")
                            bottomMargin: 0
                        }
                        Label {
                            text: qsTr("Off, a notification says \"New message\" instead of the text.")
                            multiline: true
                            topMargin: 0
                            textStyle.base: SystemDefaults.TextStyles.SubtitleText
                            textStyle.color: ui.palette.secondaryTextOnPlain
                        }
                    }
                    ToggleButton {
                        verticalAlignment: VerticalAlignment.Center
                        checked: settings.notifyPreview
                        onCheckedChanged: settings.notifyPreview = checked
                    }
                }
                // The engine's delivery mode while the app is closed (prefs.set mode, docs/protocol.md): with the app in
                // front it is always instant.
                DropDown {
                    topMargin: ui.du(3)
                    title: qsTr("Background delivery")
                    options: [
                        Option {
                            text: qsTr("Automatic")
                            description: qsTr("Every 5 minutes while Telegram is open on another device, else instant")
                            selected: settings.deliveryMode == "auto"
                        },
                        Option {
                            text: qsTr("Instant")
                            description: qsTr("Always connected: messages at once, more battery")
                            selected: settings.deliveryMode == "instant"
                        },
                        Option {
                            text: qsTr("Every 5 minutes")
                            description: qsTr("Checks for messages every 5 minutes: less battery")
                            selected: settings.deliveryMode == "batched"
                        }
                    ]
                    onSelectedIndexChanged: {
                        if (selectedIndex >= 0) {
                            settings.deliveryMode = ["auto", "instant", "batched"][selectedIndex]
                        }
                    }
                }
                Container {
                    topMargin: ui.du(3)
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Container {
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        verticalAlignment: VerticalAlignment.Center
                        Label {
                            text: qsTr("Start at boot")
                            bottomMargin: 0
                        }
                        Label {
                            text: qsTr("Brings Tengram back within 6 minutes of a reboot, or if it stops, with the app closed.")
                            multiline: true
                            topMargin: 0
                            textStyle.base: SystemDefaults.TextStyles.SubtitleText
                            textStyle.color: ui.palette.secondaryTextOnPlain
                        }
                    }
                    ToggleButton {
                        verticalAlignment: VerticalAlignment.Center
                        checked: settings.startAtBoot
                        onCheckedChanged: settings.startAtBoot = checked
                    }
                }
            }
        }
    }
}
