import bb.cascades 1.4

// Where a file or text shared to the app goes (the phone's Share menu): the chat list, where a tap
// opens the chat with the file staged and the text in the composer (main.qml's shareTo). Back
// drops the share. share is OpenRequests.shared's {path, text}, or {paths} (sharedFiles), set right after createObject().
Page {
    id: page
    property variant share
    // A message to forward, {chat, id}, instead of a share: a tap sends it to the chat (a forum asks for its topic first).
    property variant forward
    titleBar: TitleBar {
        title: page.forward ? qsTr("Forward to") : qsTr("Send to")
        scrollBehavior: TitleBarScrollBehavior.Sticky
    }
    // The field filters the chat list's own model (the user's ask, 2026-11-17): main.qml clears it when this page goes.
    property bool filtersChats: true
    function focusSearch() {
        search.requestFocus()
    }
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        TextField {
            id: search
            hintText: qsTr("Search chats")
            input.submitKey: SubmitKey.Search
            onTextChanging: chats.setFilter(text)
        }
        ChatList {
            dataModel: chats
            emojiStyle: nav.emojiStyle
            bodySize: nav.bodySize
            smallSize: nav.smallSize
            onChatTapped: page.forward ? nav.forwardTo(chat, page.forward) : nav.shareTo(chat, page.share)
        }
    }
}
