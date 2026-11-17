import bb.cascades 1.4

// Settings > Chats: saved as they change. Each lives in `settings`.
Page {
    id: page
    titleBar: TitleBar {
        title: qsTr("Chats")
    }
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
        }
    }
}
