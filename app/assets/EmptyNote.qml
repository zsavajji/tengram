import bb.cascades 1.4

// What shows over a chat's list while it has no cards: a spinner until the first page is in, then
// the note of an empty chat. model is the chat's MessageModel, null until the page opens the chat.
// Moved out of ChatPage.qml for step 6.5d. placing: the page is placing the list out of sight (ChatPage's listHidden: from
// the open until the first placement is done): the spinner stays on until it shows, the same one, so opening a chat is one
// wait (the user, 2026-11-20), and an empty chat says so once the list is placed.
Container {
    id: note
    property variant model
    property bool placing: false
    property bool loading: !model || model.loading || placing
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
