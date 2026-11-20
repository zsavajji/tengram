import bb.cascades 1.4
import bb.system 1.2
import "errors.js" as Errors

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
            },
            // Every chat read, Archived too (chat.readAll), after a yes: it can't be undone.
            ActionItem {
                title: qsTranslate("TopicsPage", "Mark all read") // its translations
                imageSource: "file:///usr/share/icons/bb_action_markread.png"
                enabled: engine.linked
                onTriggered: readAllDialog.show()
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
        // Animated stickers draw only while a chat page is on top (ChatPage's playsStickers), as noPeek is read.
        settings.coverStickers(!(nav.top && nav.top.playsStickers))
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
    // The page's background (the user's ask, 2026-11-19): white on the Bright theme (the stock page, as the composer's field
    // has it), on the Dark one pure black or, with Settings > Appearance > Dark background on Grey, #181818 like Android's. The
    // chat page paints it, and the chat's cards read it through the list to round a picture's corners onto what is behind them.
    property string pageColor: settings.theme == 1 ? "#ffffff" : settings.darkGrey ? "#181818" : "#000000"
    // A secret chat's lock and name: Telegram's secret green (#00A65A) on the dark theme, a darker one on the bright
    // (4.6:1 on white; #00A65A is 3.1:1). The chat list and the chat page's title bar.
    property string secretColor: settings.theme == 1 ? "#00874A" : "#00A65A"
    // Telegram's chat colours (the user's ask, 2026-11-18); settings.theme 1 is Bright. The card helpers in CardStyle.h
    // read the four bubble and pill ones through properties on the chat page's list.
    // Theirs. Bright: a cool light grey (the user, 2026-11-19: Telegram's white was white on our white page, which has no
    // wallpaper behind it as Telegram's has)
    property string inColor: settings.theme == 1 ? "#E3E8ED" : "#182533"
    // Ours. Bright: a light blue, the bars' blue family, not Telegram's green (the user, 2026-11-19)
    property string outColor: settings.theme == 1 ? "#D5E6F8" : "#2B5278"
    // The time, ticks and quiet text on our bubbles. Bright: a blue-grey for the light blue (5:1), as Telegram's green
    // was for its green
    property string outMeta: settings.theme == 1 ? "#386392" : "#7DA8D3"
    // Telegram's date and service pills (ARGB, translucent); their text is white
    property string serviceColor: settings.theme == 1 ? "#806E8A9C" : "#CC1D2B3A"
    // The title bar of the chat list and the chat, in both themes: our Dark bubble's blue (outColor's #2B5278) 15 % darker
    // (the user, 2026-11-19: Telegram's #517DA2 and then the accent's #1B79A7 were too light; Dark had #17212B)
    property string headerColor: "#254666"
    // The status line in the chat's title bar: Telegram's on its blue bar, which reads on this one in both themes (Dark's
    // #7F91A4 was for its slate bar and doesn't read on blue, the user, 2026-11-19)
    property string headerQuiet: "#D7E6F2"
    // The secret chat lock and name on that title bar: the light green, as the status line, on the blue bar in both themes
    property string headerSecret: "#A6F0BC"
    // The chat a notification tap asked for, until the chat list is up to open it; pendingMsg is the message a Telegram
    // link named in it, "" for none.
    property string pendingChat
    property string pendingMsg
    // The Share menu's {path, text}, or {paths} for several files, held like pendingChat until the chat list is up.
    property variant pendingShare: ({})
    // The sheet's own `call` reads the event that made it, so it opens for it as it did for the others.
    property variant callSheet
    property variant liveCall: engine.call
    onLiveCallChanged: {
        if (!callSheet && engine.call.state) {
            callSheet = callSheetDef.createObject(nav)
        }
    }
    attachedObjects: [
        // The call screen, over everything (step 7.4); it opens itself on a `call` event. Made at the first call event that
        // has a state, not at the start: it and its four pieces are QML read and built before the first frame for a screen
        // nobody had asked for (HyperBerry's Q10, 0.1.1.308).
        ComponentDefinition {
            id: callSheetDef
            source: "CallSheet.qml"
        },
        SystemToast {
            id: callFailed
        },
        SystemDialog {
            id: readAllDialog
            title: qsTr("Mark all chats as read?")
            body: qsTr("Every chat, archived ones included, will show no unread messages.")
            confirmButton.label: qsTranslate("TopicsPage", "Mark all read")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    engine.request("chat.readAll", {})
                }
            }
        },
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
        // A person's page, for a Telegram link to a user or a bot (showPerson).
        ComponentDefinition {
            id: personDef
            source: "ContactInfoPage.qml"
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
        },
        // The engine runs under the stock 30 MB headless cap (memMB in the link body): offer the headless-mem
        // addon (MemoryPatch.cpp, docs/headless-cap.md). One button: the engine does not fit without it, so no
        // cancel (an empty label leaves a SystemDialog button out). A dialog closed any other way (Back, a tap
        // outside) comes back at the next link event.
        SystemDialog {
            id: memDialog
            title: qsTr("More memory for Tengram")
            body: qsTr("BlackBerry 10 gives an app's background service at most 30 MB of memory. Tengram's service, which keeps your chats in sync and brings your notifications, needs more.\n\nThis patch raises that limit to 128 MB. Each time the phone starts, a few minutes in, it changes that one number in the system's memory. No system file is changed, and it only affects apps that ask for the largest background size. Only the patch runs as root.\n\nAfter the install, Tengram closes once: open it again. Without the patch, Tengram can't work.")
            confirmButton.label: qsTr("Install")
            cancelButton.label: ""
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    // Nothing else shows while the root shell works (about 5 s, up to 25).
                    memToast.body = qsTr("Installing the memory addon…")
                    memToast.show()
                    memPatch.install()
                } else {
                    nav.memAsked = false
                }
            }
        },
        // Without root the addon can't be installed: say so, once. memPatch.headline holds the words from the start.
        SystemDialog {
            id: rootDialog
            title: qsTr("More memory for Tengram")
            body: memPatch.headline
            cancelButton.label: ""
        },
        // After a successful install: the navigator keeps the 30 MB limit until no process of the app is left,
        // so Tengram closes once (MemoryPatch::closeAll). One button, as memDialog.
        SystemDialog {
            id: closeDialog
            title: qsTr("More memory for Tengram")
            body: memPatch.headline
            confirmButton.label: qsTr("Close Tengram")
            cancelButton.label: ""
            onFinished: memPatch.closeAll()
        },
        SystemToast {
            id: memToast
        }
    ]
    onPopTransitionEnded: {
        if (page) {
            if (page.filtersChats) {
                at(0).closeSearch() // the share page's field filtered the chat list's model
            }
            page.destroy()
        }
    }
    onLinkedChanged: show()
    onCreationCompleted: {
        // A call already known when the QML was built (EngineLink::awaitLink: the app opened for a ringing call).
        if (!callSheet && engine.call.state) {
            callSheet = callSheetDef.createObject(nav)
        }
        // Not a Connections: Cascades' QML has none.
        openRequests.openChat.connect(nav.openRequested)
        openRequests.shared.connect(nav.shareRequested)
        openRequests.sharedFiles.connect(nav.sharedFilesRequested)
        invites.offered.connect(nav.askToJoin)
        invites.joined.connect(nav.openRequested)
        invites.person.connect(nav.showPerson)
        engine.linkChanged.connect(nav.checkMemory)
        memPatch.finished.connect(nav.memFinished)
        // setCover, as the skills have it: cover belongs to Application, not to a page.
        Application.setCover(coverDef.createObject())
        show()
        checkMemory()
    }

    // The memory dialog and the no-root one are each shown once per run (a failed or dismissed memDialog is asked again).
    property bool memAsked: false
    property bool rootAsked: false
    // Every link event carries memMB, the engine's RLIMIT_DATA at its start: 30 is the stock headless cap. Nothing for
    // 0 (no limit, or unread), for 128 and up, or while the addon is installed: after a reboot it patches authman
    // about 4 minutes in, and an engine started before then reports 30 with the addon already there.
    function checkMemory() {
        var mb = engine.link.memMB
        if (!engine.connected || !(mb > 0 && mb < 128) || memPatch.installed || memPatch.busy) {
            return
        }
        if (!memPatch.rootAvailable) {
            if (!rootAsked) {
                rootAsked = true
                rootDialog.show()
            }
        } else if (!memAsked) {
            memAsked = true
            memDialog.show()
        }
    }

    function memFinished(ok) {
        if (ok) {
            closeDialog.show()
            return
        }
        memToast.body = memPatch.headline
        memToast.show()
        memAsked = false
    }

    property string failedCmd: engine.failure.cmd || ""
    onFailedCmdChanged: {
        if (failedCmd == "invite.info" || failedCmd == "invite.accept" || failedCmd == "group.join" || failedCmd == "link.open") {
            var code = engine.failure.code
            inviteFailed.body = code == "invite_invalid" ? qsTr("This invite doesn't work any more")
                              : code == "link_invalid" ? qsTr("This link doesn't lead anywhere")
                              : code == "invite_not_allowed" ? qsTr("Telegram won't let this number join by link. If an admin removed it, an admin has to add it back.")
                              : code == "request_sent" ? qsTr("Request sent: an admin of the group has to let you in.")
                              : qsTr("Couldn't open this invite. Try again in a moment.")
            inviteFailed.show()
        }
    }

    // Not failedCmd: a call is started from a chat's title bar.
    property string callFailure: engine.failure.cmd || ""
    onCallFailureChanged: {
        if (callFailure == "call.start") {
            callFailed.body = Errors.callError(engine.failure)
            callFailed.show()
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

    // msg, when there is one, is a message of the chat to scroll to; a notification and the rest give none.
    function openRequested(jid, msg) {
        pendingChat = jid
        pendingMsg = msg || ""
        openPending()
    }

    // A Telegram link to a user or a bot: the person's page, over what is on screen (Back returns). The row is the
    // one-to-one chat's, else one built as InfoAction.showPerson builds it.
    function showPerson(jid, name) {
        var row = chats.chat(jid)
        if (!row.jid) {
            row = {"kind": 0, "jid": jid, "name": name, "initials": name.charAt(0).toUpperCase() || "#",
                   "color": "#087099"}
        }
        var c = personDef.createObject()
        c.chat = row
        push(c)
    }

    function shareRequested(path, text) {
        pendingShare = {"path": path, "text": text}
        openPending()
    }

    // Several files from the Share menu (the user's ask, 2026-11-18): the same page, the chat gets them in its send-files page.
    function sharedFilesRequested(paths) {
        pendingShare = {"paths": paths}
        openPending()
    }

    // A share opens the Send to page over the list, Back drops it; a chat replaces whatever
    // is above the list, so Back goes to the list.
    function openPending() {
        if (!linked || count() == 0)
            return
        if (pendingShare.path || pendingShare.paths || pendingShare.text) {
            var picker = shareDef.createObject()
            picker.share = pendingShare
            pendingShare = {}
            backToList()
            push(picker)
            picker.focusSearch()
        }
        if (pendingChat == "")
            return
        var chat = chats.chat(pendingChat)
        var msg = pendingMsg
        pendingChat = ""
        pendingMsg = ""
        if (!chat.jid)
            return
        backToList()
        // A topics page (a forum) has no jump: it just opens.
        var opened = at(0).openChat(chat)
        if (msg != "" && opened.jump) {
            opened.jump(msg)
        }
    }

    // A contact page's Message (step 5.8): a chat list row, for a chat that may not exist yet
    // (openRequested only opens existing chats). Like openPending, Back goes to the list.
    function openRow(row) {
        backToList()
        at(0).openChat(row)
    }

    // SharePage's pick: the Send to page goes out (destroy() is deferred, so it's safe from its
    // own signal), and the chat opens with the file staged and the text in the composer, or with the send-files page
    // over it for several files.
    function shareTo(chat, share) {
        backToList()
        var opened = at(0).openChat(chat)
        if (share.paths) {
            opened.openFiles(share.paths)
        } else {
            opened.share(share.path, share.text)
        }
    }

    // The Send to page over the chat, for a message to forward; SharePage's pick comes back as forwardTo.
    function forwardPick(fwd) {
        var picker = shareDef.createObject()
        picker.forward = fwd
        push(picker)
        picker.focusSearch()
    }

    // SharePage's pick for a forward: the engine sends the message, and the chat opens as after a share. A forum has
    // no chat to open yet: its topics page goes over the Forward to page (Back picks again), holding the forward;
    // a tap on a topic (TopicsPage.openTopic) comes back here with the topic, and that topic's chat opens.
    function forwardTo(chat, fwd, topic) {
        if (chat.forum && !topic) {
            at(0).openChat(chat).forward(fwd)
            return
        }
        var args = {"chat": fwd.chat, "id": fwd.id, "to": chat.jid}
        if (topic) {
            args.topic = topic.topic
        }
        engine.request("msg.forward", args)
        backToList()
        var opened = at(0).openChat(chat)
        if (topic) {
            opened.openTopic(topic)
        }
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
            if (old.filtersChats) {
                at(0).closeSearch()
            }
            old.destroy()
        }
    }
}
