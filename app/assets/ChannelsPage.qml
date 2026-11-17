import bb.cascades 1.4
import bb.system 1.2

// The channels followed (step 6.6g): the chat list's rows and menu over channelChats, whose menu
// has Unfollow. Opening the page asks the engine for news. There's no directory to search, so
// Follow takes a link, which the same dialog as a tapped one asks to confirm.
Page {
    id: page
    attachedObjects: [
        ComponentDefinition {
            id: chatDef
            source: "ChatPage.qml"
        },
        SystemPrompt {
            id: followPrompt
            title: qsTr("Follow a channel")
            body: qsTr("Paste the channel's link")
            inputField.emptyText: "https://whatsapp.com/channel/…"
            confirmButton.label: qsTr("Next")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection && !invites.openLink(inputFieldTextEntry())) {
                    notLink.show()
                }
            }
        },
        SystemToast {
            id: notLink
            body: qsTr("That isn't a channel link")
        }
    ]
    titleBar: TitleBar {
        title: qsTr("Channels")
        scrollBehavior: TitleBarScrollBehavior.Sticky
    }
    actions: [
        ActionItem {
            title: qsTr("Follow")
            imageSource: "asset:///images/plus_action.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: followPrompt.show()
        }
    ]
    onCreationCompleted: engine.request("channels.sync", {})
    function openChat(chat) {
        var chatPage = chatDef.createObject()
        chatPage.chat = chat
        nav.push(chatPage)
    }
    Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ChatList {
            dataModel: channelChats
            channelList: true
            emojiStyle: nav.emojiStyle
            bodySize: nav.bodySize
            smallSize: nav.smallSize
            onChatTapped: page.openChat(chat)
        }
        Label {
            visible: channelChats.empty
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            multiline: true
            textStyle.color: ui.palette.secondaryTextOnPlain
            text: qsTr("No channels. Follow one by its link, or on your phone.")
        }
    }
}
