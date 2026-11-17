import bb.cascades 1.4

// Settings > Appearance: saved as they change. Each lives in `settings`.
Page {
    id: page
    titleBar: TitleBar {
        title: qsTr("Appearance")
    }
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
            DropDown {
                title: qsTr("Interface")
                options: [
                    Option { text: qsTr("Normal"); selected: !settings.compact },
                    Option { text: qsTr("Compact"); selected: settings.compact }
                ]
                onSelectedIndexChanged: settings.compact = selectedIndex == 1
            }
        }
    }
}
