import bb.cascades 1.4
import bb.system 1.2

// Settings (step 5.4), saved as they change, so there's no Save button. Each lives in `settings`.
Page {
    id: page
    property string cacheText: ""
    titleBar: TitleBar {
        title: qsTr("Settings")
    }
    // Read when the page opens and after a clear; the walk of data/cache/ is a few files.
    onCreationCompleted: cacheText = settings.cacheSize()
    attachedObjects: [
        SystemDialog {
            id: unlinkDialog
            title: qsTr("Unlink this device?")
            body: qsTr("This device is removed from your linked devices and its chats are deleted here.")
            confirmButton.label: qsTr("Unlink")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection)
                    engine.request("link.logout", {})
            }
        },
        SystemToast {
            id: clearedToast
        }
    ]
    ScrollView {
        Container {
            topPadding: ui.du(2)
            leftPadding: ui.du(2)
            rightPadding: ui.du(2)
            bottomPadding: ui.du(2)
            DropDown {
                title: qsTr("Theme")
                options: [
                    Option { text: qsTr("Dark"); selected: settings.theme == 0 },
                    Option { text: qsTr("Bright"); selected: settings.theme == 1 }
                ]
                onSelectedIndexChanged: settings.theme = selectedIndex
            }
            DropDown {
                title: qsTr("Text size")
                options: [
                    Option { text: qsTr("Small"); selected: settings.textSize == 0 },
                    Option { text: qsTr("Normal"); selected: settings.textSize == 1 },
                    Option { text: qsTr("Large"); selected: settings.textSize == 2 }
                ]
                onSelectedIndexChanged: settings.textSize = selectedIndex
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
                        text: qsTr("Enter sends")
                        bottomMargin: 0
                    }
                    Label {
                        text: qsTr("Off, Enter starts a new line; the Send action still sends.")
                        multiline: true
                        topMargin: 0
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                }
                ToggleButton {
                    verticalAlignment: VerticalAlignment.Center
                    checked: settings.enterSends
                    onCheckedChanged: settings.enterSends = checked
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
                        text: qsTr("Brings HyperBerry back within 6 minutes of a reboot, or if it stops, with the app closed.")
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
                        text: qsTr("Proximity sensor")
                        bottomMargin: 0
                    }
                    Label {
                        text: qsTr("Off, voice notes stay on the speaker and the screen stays on in calls. For a faulty sensor.")
                        multiline: true
                        topMargin: 0
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                }
                ToggleButton {
                    verticalAlignment: VerticalAlignment.Center
                    checked: settings.earSensor
                    onCheckedChanged: settings.earSensor = checked
                }
            }
            Header {
                topMargin: ui.du(3)
                title: qsTr("Storage")
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
                        text: qsTr("Download on mobile data")
                        bottomMargin: 0
                    }
                    Label {
                        text: qsTr("Off, photos and voice notes wait for a tap unless you're on Wi-Fi.")
                        multiline: true
                        topMargin: 0
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                }
                ToggleButton {
                    verticalAlignment: VerticalAlignment.Center
                    checked: settings.mediaOnMobile
                    onCheckedChanged: settings.mediaOnMobile = checked
                }
            }
            // What data/cache/ may hold. The engine prunes it, the oldest first (step 8.3).
            DropDown {
                topMargin: ui.du(2)
                title: qsTr("Cache limit")
                options: [
                    Option { text: "64 MB"; selected: settings.cacheLimit == 64 },
                    Option { text: "128 MB"; selected: settings.cacheLimit == 128 },
                    Option { text: "256 MB"; selected: settings.cacheLimit == 256 },
                    Option { text: "512 MB"; selected: settings.cacheLimit == 512 }
                ]
                onSelectedIndexChanged: {
                    if (selectedIndex >= 0) {
                        settings.cacheLimit = [64, 128, 256, 512][selectedIndex]
                    }
                }
            }
            Container {
                topMargin: ui.du(2)
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                Label {
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    verticalAlignment: VerticalAlignment.Center
                    text: qsTr("Cache: %1").arg(page.cacheText)
                }
                Button {
                    verticalAlignment: VerticalAlignment.Center
                    text: qsTr("Clear cache")
                    onClicked: {
                        // Nothing in the cache is lost, so no confirmation.
                        var freed = settings.clearCache()
                        page.cacheText = settings.cacheSize()
                        clearedToast.body = qsTr("Cache cleared: %1 freed").arg(freed)
                        clearedToast.show()
                    }
                }
            }
            Label {
                topMargin: ui.du(1)
                multiline: true
                text: qsTr("Voice notes' decoded audio and photo previews; they're made again when needed.")
                textStyle.base: SystemDefaults.TextStyles.SubtitleText
                textStyle.color: ui.palette.secondaryTextOnPlain
            }
            // Last: the one action that ends the link.
            Button {
                topMargin: ui.du(4)
                horizontalAlignment: HorizontalAlignment.Fill
                text: qsTr("Unlink this device")
                enabled: engine.linked
                onClicked: unlinkDialog.show()
            }
        }
    }
}
