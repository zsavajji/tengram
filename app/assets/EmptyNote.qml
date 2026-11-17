import bb.cascades 1.4

// What shows over a chat's list while it has no cards: a spinner until the first page is in, then
// the note of an empty chat. model is the chat's MessageModel, null until the page opens the chat.
// Moved out of ChatPage.qml for step 6.5d.
Container {
    id: note
    property variant model
    property bool loading: !model || model.loading
    visible: loading || model.empty
    horizontalAlignment: HorizontalAlignment.Center
    verticalAlignment: VerticalAlignment.Center
    ActivityIndicator {
        visible: note.loading
        running: note.loading
        preferredWidth: ui.du(8)
        preferredHeight: ui.du(8)
    }
    Label {
        visible: !note.loading
        multiline: false
        text: qsTr("No messages here")
        textFormat: TextFormat.Plain
        textStyle.base: nav.emojiStyle
        textStyle.fontSize: FontSize.PercentageValue
        textStyle.fontSizeValue: nav.bodySize
        textStyle.color: ui.palette.secondaryTextOnPlain
    }
}
