import bb.cascades 1.4

// Where a file or text shared to the app goes (the phone's Share menu): the chat list, where a tap
// opens the chat with the file staged and the text in the composer (main.qml's shareTo). Back
// drops the share. share is OpenRequests.shared's {path, text}, set right after createObject().
Page {
    id: page
    property variant share
    titleBar: TitleBar {
        title: qsTr("Send to")
        scrollBehavior: TitleBarScrollBehavior.Sticky
    }
    ChatList {
        dataModel: chats
        emojiStyle: nav.emojiStyle
        bodySize: nav.bodySize
        smallSize: nav.smallSize
        onChatTapped: nav.shareTo(chat, page.share)
    }
}
