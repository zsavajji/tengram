import bb.cascades 1.4
import bb.system 1.2

// Settings > Data and storage: saved as they change. Each lives in `settings`.
Page {
    id: page
    property string cacheText: ""
    titleBar: TitleBar {
        title: qsTr("Data and storage")
    }
    // Read when the page opens and after a clear; the walk of data/cache/ is a few files.
    onCreationCompleted: cacheText = settings.cacheSize()
    attachedObjects: [
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
        }
    }
}
