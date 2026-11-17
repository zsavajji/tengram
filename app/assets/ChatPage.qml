import bb.cascades 1.4
import bb.system 1.2
import bb.cascades.pickers 1.0
import tengram 1.0

// One chat (PLAN.md §7, step 3.1): a FreeForm title bar with the tile, name and status line, and
// the messages as cards. chat is a ChatListModel row, set right after createObject().
Page {
    id: page
    property variant chat
    // The composer's TextArea (Composer.qml): text, editor and requestFocus() are used all over this page.
    property variant composer: composerBar.field
    // Back is in the title bar and the composer is ours: nothing left for the bar. openActionMenu()
    // still opens the ⋮ menu with it hidden (Q10).
    actionBarVisibility: ChromeVisibility.Hidden
    // Set by a send until our row goes in at the bottom.
    property bool followOwn: false
    // The emoji picker is open, in the keyboard's place; only the composer's Emoji button closes it.
    property bool picking: false
    // The file picked to send, waiting for Send (step 6.4), and MediaOpener.fileInfo's of it.
    property string staged: ""
    property variant stagedInfo: ({})
    // The PNG a staged JPEG was made from ("" for none): Send as file sends it instead.
    property string stagedPng: ""
    property string failedCmd: engine.failure.cmd || ""
    onFailedCmdChanged: {
        if (failedCmd == "msg.delete") {
            deleteFailed.show()
        } else if (failedCmd == "voice.play") {
            if (playerObj) playerObj.fail(engine.failure.code)
        } else if (failedCmd == "media.get") {
            mediaFailed.show()
        } else if (failedCmd == "msg.edit") {
            editFailed.show()
        } else if (failedCmd == "poll.vote" || failedCmd == "poll.create") {
            page.reactBar() // its own failedCmd words these (ReactBar.qml)
        }
    }
    onChatChanged: {
        ctl.open(chat)
        if (chat.kind != 2) addAction(infoAction, ActionBarPlacement.InOverflow)
        if (chat.kind <= 1) addAction(pollAction, ActionBarPlacement.InOverflow)
        ctl.messages.itemAdded.connect(page.added)
        ctl.messages.loadingChanged.connect(page.loaded)
        media.ready.connect(page.mediaReady)
        media.openFailed.connect(mediaFailed.show)
    }
    attachedObjects: [
        // Attach (step 6.2): the system's picker, every file type; the engine sends by the extension.
        FilePicker {
            id: attachPicker
            type: FileType.Picture | FileType.Video | FileType.Music | FileType.Document | FileType.Other
            mode: FilePickerMode.Picker
            title: qsTr("Attach")
            onFileSelected: page.stage(selectedFiles[0])
        },
        ChatController {
            id: ctl
            link: engine
        },
        // 300 ms after the list is positioned (loaded()): the list's own state goes to the controller, which
        // doesn't mark the chat read before. An atBeginningChanged alone isn't enough: a chat whose rows all fit
        // on screen never fires one.
        Timer {
            id: settle
            interval: 300
            singleShot: true
            onTimeout: {
                page.placed = true
                ctl.setAtBottom(scrollState.atBeginning)
            }
        },
        // Aims a scroll again while the cards' heights come in (page.aim).
        Timer {
            id: reaim
            interval: 150
            singleShot: false
            onTimeout: {
                page.aim()
                page.aims = page.aims + 1
                if (page.aims >= 3) {
                    page.endAim()
                }
            }
        },
        // A received photo, video, audio file or document (step 6.1): MediaOpener fetches the file
        // and hands it to the system; a photo opens in our viewer instead (nav.showPhoto).
        MediaOpener {
            id: media
            link: engine
        },
        // A location (step 6.5c): asks which maps app opens it, every time.
        MapChooser {
            id: maps
        },
        // Added for people, groups and channels (ActionItem has no visible).
        InfoAction {
            id: infoAction
            title: !page.chat ? "" : page.chat.kind == 0 ? qsTr("Contact info") : page.chat.kind == 3 ? qsTr("Channel info") : qsTr("Group info")
            chat: page.chat
            chatCtl: ctl
        },
        PollAction { id: pollAction; jid: page.chat ? page.chat.jid : "" },
        SystemToast {
            id: notSent
            body: qsTr("Not sent: Tengram is reconnecting. Try again in a moment.")
        },
        // The player, the recorder and the search, reaction and staged bars are made at their first use, by
        // the functions of their names: together they were 135 of the 215 ms a chat's page took to build
        // before it could slide in, at every open (Q10, 0.1.1.313).
        // Each in a Container that is never shown: a definition makes only Cascades' own objects ("root object
        // must inherit from bb::cascades::BaseObject"), which a MediaPlayer and an AudioRecorder aren't, so made
        // bare they came back null and a voice note's tap did nothing (Q10, 0.1.1.314 and 315).
        ComponentDefinition {
            id: playerDef
            Container {
                property alias part: voicePlayer
                visible: false
                attachedObjects: VoicePlayer {
                    id: voicePlayer
                    jid: page.chat ? page.chat.jid : ""
                    onFailed: voiceFailed.show()
                    onOffline: notSent.show()
                }
            }
        },
        ComponentDefinition {
            id: recorderDef
            Container {
                property alias part: voiceRecorder
                visible: false
                attachedObjects: Recorder {
                    id: voiceRecorder
                    onFailed: notSent.show()
                }
            }
        },
        ComponentDefinition {
            id: searchDef
            SearchBar {
                jid: page.chat ? page.chat.jid : ""
                onJump: page.jump(id)
                onClosed: list.foundId = ""
            }
        },
        ComponentDefinition {
            id: reactDef
            ReactBar {
                jid: page.chat ? page.chat.jid : ""
            }
        },
        ComponentDefinition {
            id: stagedDef
            StagedBar {
                info: page.stagedInfo
                canFile: page.stagedPng != ""
                onCancelled: page.unstage()
            }
        },
        SystemToast {
            id: mediaFailed
            body: qsTr("Couldn't open this file")
        },
        SystemToast {
            id: voiceFailed
            body: qsTr("Couldn't play this voice note")
        },
        SystemToast {
            id: deleteFailed
            body: qsTr("Not deleted: Tengram is reconnecting. Try again in a moment.")
        },
        SystemToast {
            id: editFailed
            body: qsTr("Couldn't edit this message")
        },
        // Delete for me: the card is already hidden, the request goes when the toast closes. Several
        // deletes while it shows join one batch (show() on a showing toast keeps it up), and Undo
        // takes them all back.
        SystemToast {
            id: deletedToast
            body: qsTr("Message deleted")
            button.label: qsTr("Undo")
            autoUpdateEnabled: true
            onFinished: {
                if (value == SystemUiResult.ButtonSelection) {
                    ctl.undoDelete()
                } else {
                    ctl.commitDelete()
                }
            }
        },
        RevokeDialog {
            id: revokeDialog
            chatCtl: ctl
            onNotSent: deleteFailed.show()
        }
    ]
    actions: [
        ActionItem {
            title: qsTr("Search")
            imageSource: "asset:///images/search.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: page.searchBar().open()
        },
        PinnedAction {
            chatCtl: ctl
            onJump: page.jump(id)
        }
    ]
    keyListeners: KeyListener {
        onKeyPressed: page.typeKey(event)
    }
    // The first open creates the picker (about 1,400 cells' worth of model, none of them controls).
    onPickingChanged: {
        if (!picking) {
            return
        }
        if (!pickerSlot.delegateActive) {
            pickerSlot.delegateActive = true
        }
        emojis.refresh()
    }
    // Inserts at the cursor and keeps the field focused, so the Q10 keyboard still types into it.
    function picked(emoji) {
        composer.editor.insertPlainText(emoji)
        emojis.use(emoji)
        composer.requestFocus()
    }
    property variant playerObj
    // A voice note plays at the ear (VoicePlayer.atEar: the sensor near, and on in Settings).
    property bool atEar: page.playerObj ? page.playerObj.atEar : false
    property variant recorderObj
    property variant searchObj
    property variant reactObj
    property variant stagedObj
    function player() {
        if (!playerObj) {
            var holder = playerDef.createObject()
            partSlot.add(holder) // the page's, so it goes with the page
            var p = holder.part
            ctl.voiceReady.connect(p.ready)
            playerObj = p
        }
        return playerObj
    }
    function recorder() {
        if (!recorderObj) {
            var holder = recorderDef.createObject()
            partSlot.add(holder)
            var r = holder.part
            ctl.voiceCancelled.connect(r.dropped)
            recorderObj = r
        }
        return recorderObj
    }
    function searchBar() {
        if (!searchObj) {
            var b = searchDef.createObject()
            searchSlot.add(b)
            searchObj = b
        }
        return searchObj
    }
    function reactBar() {
        if (!reactObj) {
            var b = reactDef.createObject()
            reactSlot.add(b)
            reactObj = b
        }
        return reactObj
    }
    function stagedBar() {
        if (!stagedObj) {
            var b = stagedDef.createObject()
            stagedSlot.add(b)
            stagedObj = b
        }
        return stagedObj
    }
    property bool typed: false
    property bool recording: !!recorderObj && recorderObj.recording
    property bool mic: !recording && staged == "" && !typed
    // A staged file goes instead of a text (step 6.4).
    function send() {
        var text = composer.text.trim()
        if (staged == "" && text == "") {
            return
        }
        var editing = ctl.editing
        // Cleared only once the engine has it: it writes the row before it replies, so nothing typed is lost.
        if (!(staged == "" ? ctl.send(composer.text, mentionBar.take(composer.text)) : page.sendStaged(text))) {
            notSent.show()
            return
        }
        composer.text = ""
        page.unstage()
        ctl.cancelReply()
        page.followOwn = !editing // an edit adds no row to follow
    }
    // A photo's or a video's caption rides in the message; the other kinds' text follows as a message.
    function sendStaged(text) {
        // From a window of history around the first unread message our row would be dropped (it is beyond the
        // loaded ones): the newest first, as ctl.send does for a text.
        ctl.messages.toLiveEnd()
        // Send as file: the PNG itself, which the engine sends as a document; the text follows it.
        var asFile = page.stagedBar().asFile && stagedPng != ""
        var picture = stagedInfo.picture && !asFile
        var args = {"chat": chat.jid, "path": asFile ? stagedPng : staged}
        if (ctl.quoteId()) {
            args.quote = ctl.quoteId()
        }
        // A photo's thumbnail and size come from here: the engine decodes no picture (step 6.3).
        var info = media.photoInfo(staged)
        if (info.width) {
            args.width = info.width
            args.height = info.height
        }
        if (info.thumb) {
            args.thumb = info.thumb
        }
        // A video goes with a play mark on grey for a thumbnail, so the phone that gets it shows a video, not a grey box.
        var ext = staged.substring(staged.lastIndexOf(".") + 1).toLowerCase()
        if (ext == "mp4" || ext == "3gp" || ext == "mov") {
            var poster = media.videoPoster()
            if (poster) {
                args.thumb = poster
            }
        }
        if (picture && text) {
            args.caption = text
        }
        if (!engine.request("media.send", args)) {
            return false
        }
        if (!picture && text) {
            // The file carries the reply; the text after it is plain.
            ctl.cancelReply()
            ctl.send(text)
        }
        return true
    }
    // The picker's file waits in the staged bar; what is typed is its caption.
    function stage(path) {
        // A PNG goes as a photo, made a JPEG as the story picker does: the engine sends a .png as a file.
        // The picker can't say which of its tabs the file came from, so every PNG does (and a failed one stays a file).
        var png = ""
        if (/\.png$/i.test(path)) {
            var jpg = media.asJpeg(path)
            if (jpg) {
                png = path
                path = jpg
            }
        }
        var info = media.fileInfo(path)
        if (!info.name) {
            return
        }
        staged = path
        stagedPng = png
        page.stagedBar().asFile = false
        stagedInfo = info
        composer.requestFocus()
    }
    // A file or text from the phone's Share menu (SharePage.qml): staged as the picker's file, the
    // text in the composer, both waiting for Send.
    function share(path, text) {
        if (text) {
            composer.text = text
        }
        if (path) {
            stage(path)
        }
    }
    // Drops the file; the composer keeps its text.
    function unstage() {
        staged = ""
        stagedPng = ""
        stagedInfo = {}
    }
    // Our own message is followed even when the list was scrolled up. Scrolling once the row is in:
    // a scroll before it, at send, left the new card below the screen, while an incoming one at the
    // bottom was followed by the stick-to-edge policy (Q10, 2026-10-31).
    function added(indexPath) {
        if (followOwn && indexPath[0] == 0) {
            followOwn = false
            list.scrollToPosition(ScrollPosition.Beginning, ScrollAnimation.None)
            startAim("bottom", "")
        }
    }
    // A scroll right after rows go in stops short: the list places them by estimated heights until their cards
    // are made, and a photo's card is far taller than the estimate (HyperBerry's pitfalls; a sent photo was left
    // half below the screen and a media-heavy chat opened below its divider, Classic 0.1.0.9). So the scroll is
    // aimed again every 150 ms, 3 times, as the real heights come in; a touch on the list ends it.
    // aimAt: "bottom" (our sent row), "unread" (the divider), "item" (aimId, a search or pin jump).
    property string aimAt: ""
    property string aimId: ""
    property int aims: 0
    function startAim(what, id) {
        aimAt = what
        aimId = id
        aims = 0
        reaim.start()
    }
    function aim() {
        if (aimAt == "bottom") {
            list.scrollToPosition(ScrollPosition.Beginning, ScrollAnimation.None)
        } else {
            var i = aimAt == "unread" ? ctl.messages.unreadEntry() : ctl.messages.reveal(aimId)
            console.log("chat: aim " + aimAt + " at entry " + i + " of " + ctl.messages.childCount([]))
            if (i >= 0) {
                list.scrollToItem([i], ScrollAnimation.None)
            }
        }
    }
    // The settle timer waits for the aim after a chat opens: it reports where the list ended up.
    function endAim() {
        reaim.stop()
        if (aimAt != "" && aimAt != "bottom" && !page.placed) {
            settle.start()
        }
        // The divider is at the top now: the rows above it go in (MessageModel.releaseOlder), which doesn't move it.
        if (aimAt == "unread") {
            ctl.messages.releaseOlder()
        }
        aimAt = ""
    }
    // A search match (step 5.10): its day is loaded if it wasn't, then it's scrolled to and tinted.
    function jump(id) {
        list.foundId = id
        // A chat just opened has no messages for 50 ms (the first page comes after the push): wait for it.
        if (ctl.messages.loading) {
            page.jumpPending = id
            return
        }
        var i = ctl.messages.reveal(id)
        if (i >= 0) {
            list.scrollToItem([i], ScrollAnimation.None)
            startAim("item", id)
        } else if (ctl.messages.loading) {
            // A window far back that doesn't hold it: the model left it for the regular chat and loads until
            // chat.open answers; loaded() jumps then.
            page.jumpPending = id
        }
    }
    property string jumpPending: ""
    // True once the settle timer has reported where the list is: until then the list's own atBeginningChanged
    // is ignored, since it may fire for the first layout, before the scroll below has taken effect, and the
    // chat would be read at once.
    property bool placed: false
    // The first page is in. A search or notification jump comes first; else a chat with unread messages
    // opens at the divider (Telegram's: the first unread message is shown, not the newest), at the top of the
    // screen (user, 2026-11-17): scrollToItem puts an item at the top, as its documentation says, once the
    // heights are real (the aim above); a short unread run stops at the bottom instead, as Telegram's does. The
    // search jump (HyperBerry 5.10) lands on its item. A chat with nothing unread is left at the bottom.
    function loaded() {
        if (ctl.messages.loading) {
            return
        }
        if (page.jumpPending != "") {
            var id = page.jumpPending
            page.jumpPending = ""
            page.jump(id)
        } else {
            var u = ctl.messages.unreadEntry()
            if (u >= 0) {
                list.scrollToItem([u], ScrollAnimation.None)
                startAim("unread", "")
                return // settle starts when the aim ends
            }
        }
        if (aimAt == "") {
            settle.start()
        }
    }
    // The shape and kind of the video being fetched (openMedia sets them): a newer get replaces an older, so these are
    // the ones that land.
    property real videoRatio: 0.75
    property bool videoGif: false
    // A photo and a video open in our viewers, anything else in the system's app for its type.
    function mediaReady(msgId, kind, path) {
        if (kind == "photo") {
            nav.showPhoto(path)
        } else if (kind == "video") {
            nav.showVideo(path, page.videoRatio, page.videoGif)
        } else if (!media.openFile(path)) {
            mediaFailed.show()
        }
    }
    // Called by ChatsPage once the page is pushed: every key, the first included, goes through the
    // input method (auto-capitals, Alt and Shift), which a KeyListener bypasses (bb10-pitfalls).
    function focusComposer() {
        if (chat.kind != 3) composer.requestFocus()
    }
    // On the Q10, typing anywhere on the page goes to the field (step 3.2). A key reaches the page only
    // when the field doesn't have the focus: it goes in at the end, and the field takes over from the
    // next key. Modifiers, Sym and Delete arrive cut to their low byte with a keycap of 0xE000 or more
    // (bb10-pitfalls); control characters (Enter, Backspace) and Ctrl shortcuts aren't text.
    function typeKey(event) {
        // The search field and the composer (Composer.qml) are inside the page, so their keys come here too
        // (Q10, 0.0.1.122; the composer typed letters twice, 0.0.1.231); and a channel has no composer.
        if ((searchObj && searchObj.typing) || composer.focused || page.chat.kind == 3) {
            return
        }
        var c = event.unicode
        if (event.keycap >= 0xE000 || event.ctrlPressed || c.length == 0 || c < " " || c == "\u007f") {
            return
        }
        composer.text = composer.text + c
        composer.requestFocus()
        composer.editor.cursorPosition = composer.text.length
    }
    titleBar: ChatTitleBar {
        visibility: page.atEar ? ChromeVisibility.Hidden : ChromeVisibility.Visible
        chat: page.chat
        status: ctl.status
        members: ctl.groupInfo.participants ? ctl.groupInfo.participants.length : 0
        onTapped: infoAction.showInfo()
        onBack: nav.pop()
    }
    // At the ear, a voice note playing: the screen goes black and takes no touch, as the call screen does (the
    // user's ask, 2026-11-16); the title bar hides, so the black covers it all. The action bar is hidden already.
    Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            layout: StackLayout {
                orientation: LayoutOrientation.TopToBottom
            }
            Container {
                id: searchSlot
                horizontalAlignment: HorizontalAlignment.Fill
            }
            // Holds the player's and the recorder's containers; nothing in it shows.
            Container {
                id: partSlot
                visible: false
            }
            // The list, and over it the note of an empty chat.
            Container {
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }
                layout: DockLayout {}
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                ListView {
                    id: list
                    // Filled: a DockLayout's child sits at its top left, at its own size, otherwise.
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    // The C++ cards call these functions on the list: the long-press menu's actions, a mention's
                    // tap, the reactions' chip.
                    function reply(msg) {
                        ctl.reply(msg)
                        composer.requestFocus()
                    }
                    function copy(text) {
                        ctl.copy(text)
                    }
                    // Edit (ours, a text): the composer takes its text, the bar above says so, Send replaces it.
                    function edit(msg) {
                        page.unstage()
                        ctl.edit(msg)
                        composer.text = msg.text
                        composer.requestFocus()
                        composer.editor.cursorPosition = msg.text.length
                    }
                    // A mention's link in a card: the person's page.
                    function openPerson(jid, name) {
                        infoAction.showPerson(jid, name)
                    }
                    // Reactions (step 5.11): the bar over the composer, and the chip's who-reacted toast.
                    function react(msg) {
                        page.reactBar().open(msg)
                    }
                    function whoReacted(id) {
                        page.reactBar().who(id)
                    }
                    // Polls (step 6.5e): a vote, and the toast of who chose an option, ReactBar's.
                    function vote(id, options) {
                        engine.request("poll.vote", {"chat": page.chat.jid, "id": id, "options": options})
                    }
                    function showVoters(text) {
                        page.reactBar().say(text)
                    }
                    // Voice notes: the player's state, for the bubbles (VoicePlayer.qml).
                    property string playingId: page.playerObj ? page.playerObj.playingId : ""
                    // The search match, tinted (SearchBar.qml).
                    property string foundId
                    property string loadingId: page.playerObj ? page.playerObj.loadingId : ""
                    property bool paused: page.playerObj ? page.playerObj.paused : false
                    property int posMs: page.playerObj ? page.playerObj.posMs : 0
                    property int durMs: page.playerObj ? page.playerObj.durMs : 0
                    function playVoice(msg) {
                        page.player().toggle(msg.id)
                    }
                    function playedWave(data, bars) {
                        return ctl.playedWave(data, bars)
                    }
                    // Photos, videos, audio files and documents: the message being fetched, for the bubbles
                    // (MediaBubble.cpp). Always through media.get: it answers at once when the file is here,
                    // and fetches it again when the user moved it out of Downloads. A location and a contact
                    // have no file.
                    property string mediaLoadingId: media.loadingId
                    property variant joiner: invites // for the cards, which see no context objects
                    function openMedia(msg) {
                        if (msg.media == "location") {
                            maps.choose(msg.lat, msg.lon, msg.place)
                        } else if (msg.media == "contact") {
                            media.importContact(msg.vcard)
                        } else {
                            page.videoRatio = msg.ratio > 0 ? msg.ratio : 0.75
                            page.videoGif = !!msg.gif
                            media.get(page.chat.jid, msg.id, msg.media)
                        }
                    }
                    function showAlbum(members) {
                        nav.showAlbum(list, members)
                    }
                    function deleteForMe(id) {
                        ctl.deleteForMe(id)
                        deletedToast.show()
                    }
                    function deleteForEveryone(id) {
                        revokeDialog.msgId = id
                        revokeDialog.show()
                    }
                    // The cards reach the shared emoji style (main.qml) through ListItem.view.
                    property variant emojiStyle: nav.emojiStyle
                    property int bodySize: nav.bodySize
                    property int smallSize: nav.smallSize
                    property real textScale: settings.textScale
                    // A quote's background: white at 8 % lifts it off a dark card, black at 5 % off
                    // a bright one.
                    property string quoteTint: settings.theme == 1 ? "#0D000000" : "#14FFFFFF"
                    property string pressColor: nav.pressColor
                    dataModel: ctl.messages
                    // The user's own scroll wins over an aim still running (page.aim).
                    onTouch: {
                        if (page.aimAt != "") {
                            page.endAim()
                        }
                    }
                    // Bottom to top: the model is newest first, so the list opens at the newest message
                    // with no scroll call, and an older page is appended at the visual top without
                    // moving the rows on screen.
                    layout: StackListLayout {
                        orientation: LayoutOrientation.BottomToTop
                    }
                    // The newest card keeps the cards' gap above the composer.
                    bottomPadding: ui.du(1.5)
                    // The beginning is the bottom: a new message is followed only when the list was
                    // already there. Not in a window of history around the first unread message
                    // (ctl.messages.window): its newer pages are inserted at the bottom (index 0), where the
                    // policy would drag the view to them; None is the enum's "no particular behaviour".
                    stickToEdgePolicy: ctl.messages && ctl.messages.window ? ListViewStickToEdgePolicy.None
                                                                          : ListViewStickToEdgePolicy.Beginning
                    attachedObjects: [
                        ListScrollStateHandler {
                            id: scrollState
                            // Older pages are appends, so asking on every change can't cascade.
                            onFirstVisibleItemChanged: ctl.nearTop(firstVisibleItem)
                            // The list runs bottom to top, so its beginning is the bottom. Once placed (see the
                            // settle timer): the controller starts out not at the bottom.
                            onAtBeginningChanged: {
                                if (page.placed) {
                                    ctl.setAtBottom(atBeginning)
                                }
                            }
                        }
                    ]
                    // The cards are C++ (MessageCards.cpp, step 8.6): they reach the page through these functions and properties.
                    listItemProvider: MessageCards {}
                }
                // ctl.messages is null until the page opens the chat, hence the first test.
                EmptyNote {
                    model: ctl.messages
                }
            }
            Container {
                id: reactSlot
                horizontalAlignment: HorizontalAlignment.Fill
            }
            MentionBar {
                id: mentionBar
                people: page.chat && page.chat.kind == 1 && ctl.groupInfo.participants ? ctl.groupInfo.participants : []
                field: composer
            }
            ReplyBar {
                name: ctl.replyName
                quote: ctl.replyText
                editing: ctl.editing
                // A cancelled edit takes its text out of the composer, as WhatsApp does.
                onCancelled: {
                    if (ctl.editing) {
                        composer.text = ""
                    }
                    ctl.cancelReply()
                }
            }
            Container {
                id: stagedSlot
                horizontalAlignment: HorizontalAlignment.Fill
            }
            // Typing while recording leaves the recording alone: the text stays for later, and Send
            // finishes the recording first.
            RecordingBar {
                visible: page.recording
                clock: page.recorderObj ? page.recorderObj.clock : ""
            }
            // Created at the first open and kept (delegateActive stays true).
            ControlDelegate {
                id: pickerSlot
                source: "EmojiPicker.qml"
                delegateActive: false
                visible: page.picking
                horizontalAlignment: HorizontalAlignment.Fill
                onControlChanged: {
                    if (control) {
                        control.picked.connect(page.picked)
                    }
                }
            }
            // Last: the picker above it takes the keyboard's place, as stock.
            Composer {
                id: composerBar
                mic: page.mic
                recording: page.recording
                readOnly: page.chat ? page.chat.kind == 3 : false
                onEmoji: page.picking = !page.picking
                onDiscard: page.recorder().discard()
                onAttach: attachPicker.open()
                onSend: page.mic ? page.recorder().start() : page.recording ? page.recorder().finish() : page.send()
                // Off, Enter is a new line: the key still submits, so the field keeps its focus.
                onSubmitted: settings.enterSends ? page.send() : composer.editor.insertPlainText("\n")
                // The field's text notifies only when committed, so mic follows this.
                onChanging: {
                    page.typed = /\S/.test(text)
                    mentionBar.find(text)
                    ctl.composing(text)
                }
                onMenu: page.openActionMenu()
            }
        }
        Container {
            visible: page.atEar
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: Color.Black
            onTouch: {}
        }
    }
}
