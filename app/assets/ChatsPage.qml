import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0

// The chat list (PLAN.md §7, step 2.4): chats.db's chats under date headers, drawn by
// ChatListModel (the context property chats) before the engine answers.
Page {
    id: page
    // The new chat page while it's open, so picking can remove it from the stack.
    property variant picker
    // The new group page while it's open, so creating can remove it from the stack.
    property variant groupPage
    // The next chat page, made ahead (the user, 2026-11-20: a faster open). Building ChatPage.qml was the wait between a
    // tap and the push, and a page does nothing until its chat is set, so one is made while the list is the top page
    // again (uncovered, from main.qml's nav) and takeChatPage hands it to the next open. Costs one idle page of memory.
    property variant spareChat
    // False until the first uncovered(): the app's start is left alone for 2 s, later pops make the page after 700 ms, once the pop's animation is over.
    property bool spareLater: false
    attachedObjects: [
        // Makes the spare chat page a moment after the list is uncovered.
        Timer {
            id: spareTimer
            singleShot: true
            onTimeout: page.makeSpare()
        },
        ComponentDefinition {
            id: chatDef
            source: "ChatPage.qml"
        },
        ComponentDefinition {
            id: topicsDef
            source: "TopicsPage.qml"
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
            id: callsDef
            source: "CallsPage.qml"
        },
        ComponentDefinition {
            id: searchDef
            source: "SearchPage.qml"
        }
    ]
    actions: [
        // On the bar (the user's ask, 2026-11-17): Search left of New chat, New group right of it;
        // the on-bar actions take the sides in the order they are declared.
        ActionItem {
            title: qsTr("Search")
            imageSource: "asset:///images/search.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            shortcuts: SystemShortcut {
                type: SystemShortcuts.Search
            }
            onTriggered: page.openSearch()
        },
        ActionItem {
            title: qsTr("New chat")
            // The stock Phone app's text message bubble; /usr/share/icons only has BBM's.
            imageSource: "asset:///images/message.png"
            ActionBar.placement: ActionBarPlacement.Signature
            shortcuts: SystemShortcut {
                type: SystemShortcuts.CreateNew
            }
            onTriggered: page.newChat()
        },
        ActionItem {
            title: qsTr("New group")
            imageSource: "asset:///images/contacts_action.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: page.newGroup()
        },
        ActionItem {
            title: qsTr("Top")
            imageSource: "asset:///images/top.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: SystemShortcut {
                type: SystemShortcuts.JumpToTop
            }
            onTriggered: list.scrollToPosition(ScrollPosition.Beginning, ScrollAnimation.None)
        },
        ActionItem {
            title: qsTr("Bottom")
            imageSource: "asset:///images/bottom.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: SystemShortcut {
                type: SystemShortcuts.JumpToBottom
            }
            onTriggered: list.scrollToPosition(ScrollPosition.End, ScrollAnimation.None)
        },
        ActionItem {
            title: qsTr("Archived")
            imageSource: "file:///usr/share/icons/bb_action_archive.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: nav.push(archivedDef.createObject())
        },
        ActionItem {
            title: qsTr("Calls")
            imageSource: "file:///usr/share/icons/ic_phone.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: nav.push(callsDef.createObject())
        },
        // Telegram's chat with ourselves: its id is our user id (link.me); the engine names its row Saved Messages.
        // The row may not be in chats.db yet (a chat never used), so the page gets a bare chat then.
        ActionItem {
            title: qsTr("Saved Messages")
            imageSource: "file:///usr/share/icons/bb_action_markassaved.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: engine.linked && !!engine.link.me
            onTriggered: page.openSaved()
        }
    ]
    function openSaved() {
        var me = String(engine.link.me)
        var chat = chats.chat(me)
        if (!chat.jid) {
            chat = {"jid": me, "kind": 0, "name": qsTr("Saved Messages")}
        }
        openChat(chat)
    }
    // A group with topics opens its list of topics first (TopicsPage), which opens the chat in one; every way into
    // a chat comes here (main.qml), so a notification, a search and the Share menu land there too.
    function openChat(chat) {
        if (chat.forum) {
            var topicsPage = topicsDef.createObject()
            topicsPage.chat = chat
            nav.push(topicsPage)
            return topicsPage
        }
        var chatPage = page.takeChatPage()
        chatPage.chat = chat
        nav.push(chatPage)
        chatPage.focusComposer()
        return chatPage
    }
    // A chat page for a chat about to open: the one made ahead, else a new one (the first open after a start, or two opens
    // in a row). Every way into a chat takes its page here (openChat, ArchivedPage, TopicsPage).
    function takeChatPage() {
        var chatPage = page.spareChat
        if (chatPage) {
            page.spareChat = null
        } else {
            chatPage = chatDef.createObject()
        }
        return chatPage
    }
    // Makes the spare chat page if there is none. With nav as its parent, as the call sheet is made (main.qml): an object
    // made with no parent is the script engine's, which may collect it while it waits here in a property.
    function makeSpare() {
        if (page.spareChat) {
            return
        }
        page.spareChat = chatDef.createObject(nav)
    }
    // Called by main.qml's nav each time this page is the top one (its onTopChanged): the next chat page is made 700 ms
    // later, once the pop's animation is over (it ends about 350 ms after it starts): making it during the animation stuttered
    // it, as the page holds the UI thread 92-260 ms (ui.log, 1.0.0.97; the user, 2026-11-20). The first time is the app's
    // start, which is left alone for 2 s.
    function uncovered() {
        spareTimer.interval = page.spareLater ? 700 : 2000
        page.spareLater = true
        spareTimer.start()
    }
    // Telegram's search, on a page of its own (the user's ask, 2026-11-19); the filter is the Send to page's now.
    function openSearch() {
        var searchPage = searchDef.createObject()
        nav.push(searchPage)
        searchPage.focusSearch()
    }
    // main.qml calls it as the Send to page goes: that page's field filtered the chat list's model.
    function closeSearch() {
        chats.setFilter("")
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
        id: listBar
        // The engine is down, starting (TDLib opening: "linking") or not yet through to Telegram: the list stays,
        // and the bar says so with a spinner (the user, 2026-11-17), instead of the linking page.
        property bool waiting: !engine.connected || engine.conn != "ready" || (engine.link.state || "") == "linking"
        kind: TitleBarKind.FreeForm
        // Sticky: a bar that scrolls away resizes the list on every frame, and laying out rows
        // that hold several of our font's emoji again each frame made scrolling lag (Q10,
        // 2026-10-31).
        scrollBehavior: TitleBarScrollBehavior.Sticky
        kindProperties: FreeFormTitleBarKindProperties {
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                // The chat page's colour, with white text on it (the user's ask, 2026-11-19).
                background: Color.create(nav.headerColor)
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    text: listBar.waiting ? qsTr("Connecting…") : "Tengram" // the app's name, not translated
                    textStyle.base: SystemDefaults.TextStyles.TitleText
                    textStyle.color: Color.White
                }
                // An ActivityIndicator hides itself while stopped: running, not visible.
                ActivityIndicator {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(5)
                    preferredHeight: ui.du(5)
                    running: listBar.waiting
                }
            }
        }
    }
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
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
            // here is an account with no chats: it says so (the user, 2026-11-19; HyperBerry showed nothing).
            Label {
                visible: chats.empty
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                textStyle.color: ui.palette.secondaryTextOnPlain
                text: qsTr("No chats yet")
            }
        }
    }
}
