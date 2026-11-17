import bb.cascades 1.4
import bb.system 1.2

// The app's root (PLAN.md §7): the NavigationPane `nav` of every earlier step (pages reach it by that id,
// and everything that pushes onto it), with the application menu. It shows the chat list when linked, else the
// linking page: not linked, the root is the linking page, not a sheet.
NavigationPane {
    id: nav
    Menu.definition: MenuDefinition {
        settingsAction: SettingsActionItem {
            onTriggered: nav.push(settingsDef.createObject())
        }
        // Beside Settings (step 5.9). Off while unlinked: there is no profile to show.
        actions: [
            ActionItem {
                title: qsTr("Profile")
                imageSource: "asset:///images/contacts_action.png"
                enabled: engine.linked
                onTriggered: nav.push(profileDef.createObject())
            },
            // The app as a profile: logo, name, version, what it is (AboutPage.qml).
            ActionItem {
                title: qsTr("About")
                imageSource: "asset:///images/help_action.png"
                onTriggered: nav.push(aboutDef.createObject())
            }
        ]
    }
    // A new link stays on the linking page while the engine is syncing its recent chats, so the
    // chat list opens full, or empty for an account with none (the user's, 2026-11-01).
    property bool linked: engine.linked && !engine.link.syncing
    // The one shared emoji style (PLAN.md §7): a FontFaceRule's font reaches only the styles
    // built on it, and each rule loads the font again. Pages take it as nav.emojiStyle, list
    // items through a property on their ListView.
    property variant emojiStyle: emoji.style
    // A page that needs every horizontal drag (the video viewer's timeline: its noPeek) turns peeking off while it
    // is on top: it slid the page away under the finger. What this pane had is put back when the page goes.
    property bool noPeek: false
    property bool peekWas: true
    onTopChanged: {
        nav.noPeek = !!nav.top && !!nav.top.noPeek
        // A page that has a chat over it catches up when it is the top again.
        if (nav.top && nav.top.uncovered) {
            nav.top.uncovered()
        }
    }
    onNoPeekChanged: {
        if (nav.noPeek) {
            nav.peekWas = nav.peekEnabled
        }
        nav.peekEnabled = nav.noPeek ? false : nav.peekWas
    }
    // Text sizes in percent of the emoji style at 100 % (capitals 20 px on the Q10), in one place,
    // scaled by the text size setting. body: messages and chat names, the stock Text Messages
    // body's 21 px; small: previews, senders, quotes and service lines. List items reach them
    // through properties on their ListView, as they reach emojiStyle.
    property int bodySize: Math.round(105 * settings.textScale)
    property int smallSize: Math.round(85 * settings.textScale)
    // A pressed row, card or tappable line (the user's ask, 2026-11-02: rows gave no feedback,
    // the action bar does): a step off ui.palette.plain (#323232 dark, #F0F0F0 bright in the
    // 10.3 palette), so it shows on the message cards as well as on the bare page. List items
    // reach it through a property on their ListView, as they reach emojiStyle.
    property string pressColor: settings.theme == 1 ? "#DCDCDC" : "#464646"
    // The chat a notification tap asked for, until the chat list is up to open it.
    property string pendingChat
    // The Share menu's {path, text}, held like pendingChat until the chat list is up.
    property variant pendingShare: ({})
    attachedObjects: [
        ComponentDefinition {
            id: chatsDef
            source: "ChatsPage.qml"
        },
        ComponentDefinition {
            id: linkDef
            source: "LinkPage.qml"
        },
        ComponentDefinition {
            id: settingsDef
            source: "SettingsPage.qml"
        },
        ComponentDefinition {
            id: profileDef
            source: "ProfilePage.qml"
        },
        ComponentDefinition {
            id: aboutDef
            source: "AboutPage.qml"
        },
        ComponentDefinition {
            id: coverDef
            source: "cover.qml"
        },
        ComponentDefinition {
            id: viewerDef
            source: "PhotoViewer.qml"
        },
        ComponentDefinition {
            id: videoDef
            source: "VideoViewer.qml"
        },
        ComponentDefinition {
            id: albumDef
            source: "AlbumPage.qml"
        },
        ComponentDefinition {
            id: shareDef
            source: "SharePage.qml"
        },
        TextStyleDefinition {
            id: emoji
            fontFamily: "sans-serif, HBEmoji"
            rules: FontFaceRule {
                source: "asset:///fonts/HyperBerryEmoji.ttf"
                fontFamily: "HBEmoji"
            }
        },
        // A group or channel invite (steps 6.6f, 6.6g): filled by askToJoin.
        SystemDialog {
            id: joinDialog
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    invites.accept()
                }
            }
        },
        SystemToast {
            id: inviteFailed
        }
    ]
    onPopTransitionEnded: page.destroy()
    onLinkedChanged: show()
    onCreationCompleted: {
        // Not a Connections: Cascades' QML has none.
        openRequests.openChat.connect(nav.openRequested)
        openRequests.shared.connect(nav.shareRequested)
        invites.offered.connect(nav.askToJoin)
        invites.joined.connect(nav.openRequested)
        // setCover, as the skills have it: cover belongs to Application, not to a page.
        Application.setCover(coverDef.createObject())
        show()
    }

    property string failedCmd: engine.failure.cmd || ""
    onFailedCmdChanged: {
        if (failedCmd == "invite.info" || failedCmd == "invite.accept" || failedCmd == "group.join") {
            var code = engine.failure.code
            inviteFailed.body = code == "invite_invalid" ? qsTr("This invite doesn't work any more")
                              : code == "invite_not_allowed" ? qsTr("Telegram won't let this number join by link. If an admin removed it, an admin has to add it back.")
                              : qsTr("Couldn't open this invite. Try again in a moment.")
            inviteFailed.show()
        }
    }

    function askToJoin(kind, name, count) {
        var channel = kind == "channel"
        joinDialog.title = channel ? qsTr("Follow this channel?") : qsTr("Join this group?")
        var size = count > 0 ? (channel ? qsTr("%1 followers") : qsTr("%1 members")).arg(count) : ""
        joinDialog.body = size == "" ? name : name + "\n" + size
        joinDialog.confirmButton.label = channel ? qsTr("Follow") : qsTr("Join")
        joinDialog.show()
    }

    // A page swap, so there's no Back to the other root: the new page goes in at the bottom
    // and every page above it (link number page, a chat) goes out.
    // Asked for twice at a start, by linked's first value and by onCreationCompleted, which made the
    // chat list twice (Q10, 0.1.1.307): the second one finds its page there.
    property bool shownLinked: false
    function show() {
        if (count() > 0 && shownLinked == linked) {
            openPending()
            return
        }
        shownLinked = linked
        var page = (linked ? chatsDef : linkDef).createObject()
        if (count() == 0) {
            push(page)
            return
        }
        insert(0, page)
        while (count() > 1) {
            var old = at(1)
            remove(old)
            old.destroy()
        }
        openPending()
    }

    function openRequested(jid) {
        pendingChat = jid
        openPending()
    }

    function shareRequested(path, text) {
        pendingShare = {"path": path, "text": text}
        openPending()
    }

    // A share opens the Send to page over the list, Back drops it; a chat replaces whatever
    // is above the list, so Back goes to the list.
    function openPending() {
        if (!linked || count() == 0)
            return
        if (pendingShare.path || pendingShare.text) {
            var picker = shareDef.createObject()
            picker.share = pendingShare
            pendingShare = {}
            backToList()
            push(picker)
        }
        if (pendingChat == "")
            return
        var chat = chats.chat(pendingChat)
        pendingChat = ""
        if (!chat.jid)
            return
        backToList()
        at(0).openChat(chat)
    }

    // A contact page's Message (step 5.8): a chat list row, for a chat that may not exist yet
    // (openRequested only opens existing chats). Like openPending, Back goes to the list.
    function openRow(row) {
        backToList()
        at(0).openChat(row)
    }

    // SharePage's pick: the Send to page goes out (destroy() is deferred, so it's safe from its
    // own signal), and the chat opens with the file staged and the text in the composer.
    function shareTo(chat, share) {
        backToList()
        at(0).openChat(chat).share(share.path, share.text)
    }

    // A photo full screen (step 6.1): a received one, or an info page's picture.
    function showPhoto(path) {
        var viewer = viewerDef.createObject()
        viewer.path = path
        push(viewer)
    }

    // A received video in the app: ratio is its height over its width, gif a video that plays by itself.
    function showVideo(path, ratio, gif) {
        var viewer = videoDef.createObject()
        viewer.path = path
        viewer.ratio = ratio
        viewer.gif = gif
        push(viewer)
        viewer.start()
    }

    // An album's page (step 6.5g): members are its messages' maps, list the chat's ListView, whose
    // openMedia opens one. Here and not in the card: a list item sees no nav, and a page made by a
    // card that is recycled meanwhile would lose its bindings.
    function showAlbum(list, members) {
        var album = albumDef.createObject()
        album.view = list
        album.members = members
        push(album)
    }

    // Every page above the list goes out. Deleting a chat (step 5.8b) uses it too.
    function backToList() {
        while (count() > 1) {
            var old = at(count() - 1)
            remove(old)
            old.destroy()
        }
    }
}
