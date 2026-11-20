import bb.cascades 1.4

// The archived chats (step 5.3): the chat list's rows and menu over archivedChats, whose
// "Unarchive" brings a chat back.
Page {
    id: page
    titleBar: PageBar {
        title: qsTr("Archived")
        scrollBehavior: TitleBarScrollBehavior.Sticky
    }
    function openChat(chat) {
        if (chat.forum) {
            nav.at(0).openChat(chat) // the chat list's page makes the topics page
            return
        }
        var chatPage = nav.at(0).takeChatPage()
        chatPage.chat = chat
        nav.push(chatPage)
        chatPage.focusComposer()
    }
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ChatList {
            dataModel: archivedChats
            archivedList: true
            emojiStyle: nav.emojiStyle
            bodySize: nav.bodySize
            smallSize: nav.smallSize
            onChatTapped: page.openChat(chat)
        }
        Label {
            visible: archivedChats.empty
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            textStyle.color: ui.palette.secondaryTextOnPlain
            text: qsTr("No archived chats")
        }
    }
}
