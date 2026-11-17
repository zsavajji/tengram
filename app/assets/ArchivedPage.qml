import bb.cascades 1.4

// The archived chats (step 5.3): the chat list's rows and menu over archivedChats, whose
// "Unarchive" brings a chat back.
Page {
    id: page
    attachedObjects: [
        ComponentDefinition {
            id: chatDef
            source: "ChatPage.qml"
        }
    ]
    titleBar: TitleBar {
        title: qsTr("Archived")
        scrollBehavior: TitleBarScrollBehavior.Sticky
    }
    function openChat(chat) {
        var chatPage = chatDef.createObject()
        chatPage.chat = chat
        nav.push(chatPage)
        chatPage.focusComposer()
    }
    Container {
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
