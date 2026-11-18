import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0

// A group with topics (docs/protocol.md, Forum topics): the chat list's rows over the group's topics, drawn by TopicModel
// from chats.db before the engine answers; the engine's topics.list brings them up to date. A tap opens the chat page in
// that topic, Back comes here. chat is a ChatListModel row, set right after createObject().
Page {
    id: page
    property variant chat
    // A file or text shared to the group (main.qml's shareTo), or a message to forward, waiting for the topic it goes in.
    property variant pending: ({})
    // As ChatPage: a failure of the page's own request, once per failure. Not while the engine is down: the list is
    // asked again when it is up.
    property bool failed: engine.failure.cmd == "topics.list" && engine.failure.code != "no_engine"
    onFailedChanged: {
        if (failed) {
            listFailed.show()
        }
    }
    onChatChanged: topics.open(chat.jid)
    // A chat page over this one went: the rows are read again (an unread count, a new order).
    function uncovered() {
        topics.reload()
    }
    function share(path, text) {
        pending = {"path": path, "text": text}
    }
    // Several files from the Share menu (main.qml's shareTo) wait there too, for the topic's chat page to take.
    function openFiles(paths) {
        pending = {"paths": paths}
    }
    // A message to forward ({chat, id}, main.qml's forwardTo) waits in the same place: Back without a tap sends nothing.
    function forward(fwd) {
        pending = {"forward": fwd}
    }
    function openTopic(topic) {
        if (pending.forward) {
            // main.qml sends it to the topic and opens that topic's chat; cleared so a second tap can't send it twice
            var fwd = pending.forward
            pending = {}
            nav.forwardTo(chat, fwd, topic)
            return
        }
        var chatPage = chatDef.createObject()
        chatPage.topic = topic.topic
        chatPage.topicName = topic.name
        chatPage.chat = chat
        nav.push(chatPage)
        chatPage.focusComposer()
        if (pending.path || pending.text) {
            chatPage.share(pending.path, pending.text)
            pending = {}
        } else if (pending.paths) {
            chatPage.openFiles(pending.paths)
            pending = {}
        }
    }
    attachedObjects: [
        TopicModel {
            id: topics
            link: engine
        },
        // Group info's controller: no messages, only the group's info (ChatController.openInfo, from InfoAction).
        ChatController {
            id: infoCtl
            link: engine
        },
        ComponentDefinition {
            id: chatDef
            source: "ChatPage.qml"
        },
        SystemToast {
            id: listFailed
            body: qsTr("Couldn't load the topics")
        }
    ]
    actions: [
        // GroupInfoPage's own title and translations.
        InfoAction {
            title: qsTranslate("GroupInfoPage", "Group info")
            ActionBar.placement: ActionBarPlacement.InOverflow
            chat: page.chat
            chatCtl: infoCtl
        },
        ActionItem {
            title: qsTr("Mark all read")
            imageSource: "file:///usr/share/icons/bb_action_markread.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: topics.readAll()
        }
    ]
    titleBar: TitleBar {
        kind: TitleBarKind.FreeForm
        scrollBehavior: TitleBarScrollBehavior.Sticky
        kindProperties: FreeFormTitleBarKindProperties {
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                layout: DockLayout {}
                // The group's name, which may have emoji: the shared style, at the stock title's 125 %.
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    text: page.chat ? page.chat.name : ""
                    textFormat: TextFormat.Plain
                    textStyle.base: nav.emojiStyle
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: 125
                }
            }
        }
    }
    Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ChatList {
            dataModel: topics
            topicList: true
            emojiStyle: nav.emojiStyle
            bodySize: nav.bodySize
            smallSize: nav.smallSize
            onChatTapped: page.openTopic(chat)
        }
        // Until the first answer, with nothing cached to show; then, if there is still nothing, its note.
        ActivityIndicator {
            property bool waiting: topics.loading && topics.empty && !page.failed
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(8)
            preferredHeight: ui.du(8)
            visible: waiting
            running: waiting
        }
        Label {
            visible: topics.empty && (!topics.loading || page.failed)
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            textStyle.color: ui.palette.secondaryTextOnPlain
            text: qsTr("No topics")
        }
    }
}
