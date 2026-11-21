import bb.cascades 1.4
import tengram 1.0

// Settings > Translation: Telegram translates the messages; a Google Cloud Translation API key makes Google do it
// instead. Saved as it changes: a text is saved 300 ms after the last key (start() on a running QTimer starts it over),
// and at once when its field loses the focus or Enter is pressed, so the engine isn't told once per letter.
Page {
    id: page
    titleBar: PageBar {
        title: qsTr("Translation")
    }
    function save() {
        settings.googleKey = googleField.text.trim()
    }
    attachedObjects: [
        Timer {
            id: saveTimer
            interval: 300
            singleShot: true
            onTimeout: page.save()
        }
    ]
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ScrollView {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            Container {
                topPadding: ui.du(2)
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                bottomPadding: ui.du(2)
                TextField {
                    id: googleField
                    hintText: qsTr("Google API key (optional)")
                    text: settings.googleKey
                    input.flags: TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff | TextInputFlag.PredictionOff | TextInputFlag.SpellCheckOff
                    input.onSubmitted: page.save()
                    onTextChanging: saveTimer.start()
                    onFocusedChanged: {
                        if (!focused) {
                            page.save()
                        }
                    }
                }
                Label {
                    topMargin: ui.du(1)
                    multiline: true
                    text: qsTr("Telegram translates the messages. If it limits translations, add a Google Cloud Translation API key: Google then translates in its place.")
                    textStyle.base: SystemDefaults.TextStyles.SubtitleText
                    textStyle.color: ui.palette.secondaryTextOnPlain
                }
            }
        }
    }
}
