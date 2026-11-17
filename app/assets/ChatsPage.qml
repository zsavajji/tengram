import bb.cascades 1.4
import bb.system 1.2

// The chat list (PLAN.md §7, step 2.4): chats.db's chats under date headers, drawn by
// ChatListModel (the context property chats) before the engine answers.
Page {
    id: page
    // The new chat page while it's open, so picking can remove it from the stack.
    property variant picker
    // The new group page while it's open, so creating can remove it from the stack.
    property variant groupPage
    // The filter field is over the list. The letter shortcuts are off meanwhile, or they would
    // eat the letters typed into it (bb10-pitfalls).
    property bool searching: false
    attachedObjects: [
        ComponentDefinition {
            id: chatDef
            source: "ChatPage.qml"
        },
        ComponentDefinition {
            id: newChatDef
            source: "NewChatPage.qml"
        },
        ComponentDefinition {
            id: newGroupDef
            source: "NewGroupPage.qml"
        },
        SystemToast {
            id: groupRefused
        },
        ComponentDefinition {
            id: archivedDef
            source: "ArchivedPage.qml"
        },
        ComponentDefinition {
            id: starredDef
            source: "StarredPage.qml"
        },
        ComponentDefinition {
            id: channelsDef
            source: "ChannelsPage.qml"
        }
    ]
    actions: [
        ActionItem {
            title: qsTr("New chat")
            // The stock Phone app's text message bubble; /usr/share/icons only has BBM's.
            imageSource: "asset:///images/message.png"
            ActionBar.placement: ActionBarPlacement.Signature
            shortcuts: SystemShortcut {
                type: SystemShortcuts.CreateNew
                enabled: !page.searching
            }
            onTriggered: page.newChat()
        },
        ActionItem {
            title: qsTr("New group")
            imageSource: "asset:///images/contacts_action.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: page.newGroup()
        },
        ActionItem {
            title: qsTr("Top")
            imageSource: "asset:///images/top.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: SystemShortcut {
                type: SystemShortcuts.JumpToTop
                enabled: !page.searching
            }
            onTriggered: list.scrollToPosition(ScrollPosition.Beginning, ScrollAnimation.None)
        },
        ActionItem {
            title: qsTr("Bottom")
            imageSource: "asset:///images/bottom.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: SystemShortcut {
                type: SystemShortcuts.JumpToBottom
                enabled: !page.searching
            }
            onTriggered: list.scrollToPosition(ScrollPosition.End, ScrollAnimation.None)
        },
        ActionItem {
            title: qsTr("Search")
            imageSource: "asset:///images/search.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: SystemShortcut {
                type: SystemShortcuts.Search
                enabled: !page.searching
            }
            onTriggered: page.toggleSearch()
        },
        ActionItem {
            title: qsTr("Archived")
            imageSource: "file:///usr/share/icons/bb_action_archive.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: nav.push(archivedDef.createObject())
        },
        ActionItem {
            title: qsTr("Starred messages")
            imageSource: "asset:///images/star_action.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: nav.push(starredDef.createObject())
        },
        ActionItem {
            title: qsTr("Channels")
            imageSource: "asset:///images/megaphone_action.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: nav.push(channelsDef.createObject())
        }
    ]
    function openChat(chat) {
        var chatPage = chatDef.createObject()
        chatPage.chat = chat
        nav.push(chatPage)
        chatPage.focusComposer()
        return chatPage
    }
    // Search again with text in the field goes back to typing; with none, it closes.
    function toggleSearch() {
        if (!searching) {
            searching = true
            search.requestFocus()
        } else if (search.text == "") {
            closeSearch()
        } else {
            search.requestFocus()
        }
    }
    function closeSearch() {
        search.text = ""
        chats.setFilter("")
        searching = false
    }
    function newChat() {
        picker = newChatDef.createObject()
        picker.picked.connect(page.chatPicked)
        nav.push(picker)
        picker.focusSearch()
    }
    function chatPicked(chat) {
        openChat(chat)
        // The picker goes out from under the chat, so Back returns to the list, as in WhatsApp.
        // destroy() is deferred, so destroying the page whose signal is running is safe;
        // main.qml's show() removes and destroys pages the same way.
        var old = picker
        picker = null
        nav.remove(old)
        old.destroy()
    }
    function newGroup() {
        groupPage = newGroupDef.createObject()
        groupPage.created.connect(page.groupCreated)
        nav.push(groupPage)
        groupPage.focusName()
    }
    function groupCreated(jid, refused) {
        // Out from under the chat, as chatPicked does the picker; the engine has the chat in
        // chats.db before it replies, so main.qml opens it.
        var old = groupPage
        groupPage = null
        nav.remove(old)
        old.destroy()
        nav.openRequested(jid)
        if (refused > 0) {
            groupRefused.body = qsTr("%n couldn't be added: their privacy settings don't allow it. Invite them with the group's link from your main phone.", "", refused)
            groupRefused.show()
        }
    }
    titleBar: TitleBar {
        title: engine.connected && engine.conn == "ready" ? qsTr("Chats") : qsTr("Connecting…")
        // Sticky: a bar that scrolls away resizes the list on every frame, and laying out rows
        // that hold several of our font's emoji again each frame made scrolling lag (Q10,
        // 2026-10-31).
        scrollBehavior: TitleBarScrollBehavior.Sticky
    }
    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        // The field and a ✕ that closes it in one tap: the Q10 has no Escape, and the menu's
        // Search was the only other way out (Q10, 2026-11-01).
        Container {
            visible: page.searching
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            TextField {
                id: search
                verticalAlignment: VerticalAlignment.Center
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }
                hintText: qsTr("Search chats")
                input.submitKey: SubmitKey.Search
                onTextChanging: chats.setFilter(text)
                keyListeners: KeyListener {
                    onKeyPressed: {
                        if (event.key == 27) {
                            page.closeSearch()
                        }
                    }
                }
            }
            // A touch target the size of an action column (9 du), the ✕ as the reply bar's.
            Container {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(9)
                preferredHeight: ui.du(9)
                layout: DockLayout {
                }
                gestureHandlers: TapHandler {
                    onTapped: page.closeSearch()
                }
                Label {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    text: "✕"
                    textFormat: TextFormat.Plain
                    textStyle.color: ui.palette.secondaryTextOnPlain
                }
            }
        }
        Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            ChatList {
                id: list
                dataModel: chats
                emojiStyle: nav.emojiStyle
                bodySize: nav.bodySize
                smallSize: nav.smallSize
                onChatTapped: page.openChat(chat)
            }
            // The wait for a new link's chats is on the linking page (main.qml), so an empty list
            // here is an account with no chats, and shows nothing (the user's, 2026-11-01).
            Label {
                visible: chats.empty && page.searching
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                textStyle.color: ui.palette.secondaryTextOnPlain
                text: qsTr("No chat matches.")
            }
        }
    }
}
