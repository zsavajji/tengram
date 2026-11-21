import bb.cascades 1.4
import bb.system 1.2
import bb.cascades.pickers 1.0
import tengram 1.0

// One chat (PLAN.md §7, step 3.1): a FreeForm title bar with the tile, name and status line, and
// the messages as cards. chat is a ChatListModel row, set right after createObject().
Page {
    id: page
    property variant chat
    // Read by main.qml's nav: animated stickers play only while this page is on top.
    property bool playsStickers: true
    // A forum topic's id and name, set before chat: the page is that topic's messages only (ChatController.open). 0
    // for any other chat.
    property int topic: 0
    property string topicName: ""
    // The composer's TextArea (Composer.qml): text, editor and requestFocus() are used all over this page.
    property variant composer: composerBar.field
    // Back is in the title bar and the composer is ours: nothing left for the bar. openActionMenu()
    // still opens the ⋮ menu with it hidden (Q10).
    actionBarVisibility: ChromeVisibility.Hidden
    // The back action (the Classic's Back key, the user's ask, 2026-11-20): with the emoji picker open it closes the picker and
    // the chat stays; else the page goes, as the stock back does.
    paneProperties: NavigationPaneProperties {
        backButton: ActionItem {
            onTriggered: {
                if (page.picking) {
                    page.picking = false
                } else {
                    nav.pop()
                }
            }
        }
    }
    // Set by a send until our row goes in at the bottom.
    property bool followOwn: false
    // The emoji picker is open, in the keyboard's place; the composer's Emoji button closes it, and the back action (above).
    property bool picking: false
    // The file picked to send, waiting for Send (step 6.4), and MediaOpener.fileInfo's of it.
    property string staged: ""
    property variant stagedInfo: ({})
    // The PNG a staged JPEG was made from ("" for none): Send as file sends it instead.
    property string stagedPng: ""
    // The staged file is a JPEG of the user's own (not one made from a PNG): it can go as a file too.
    property bool stagedJpeg: stagedPng == "" && /\.jpe?g$/i.test(staged)
    // A secret chat's state, read from the chat list as it changes (the chat keeps the row it opened with): 0 not a secret
    // chat, 1 pending (the other side isn't online yet), 2 ready, 3 closed (docs/protocol.md, Secret chats).
    property int secret: page.chat ? (chats.revision >= 0 && chats.chat(page.chat.jid).secret) || page.chat.secret || 0 : 0
    property string failedCmd: engine.failure.cmd || ""
    onFailedCmdChanged: {
        if (failedCmd == "msg.delete") {
            deleteFailed.show()
        } else if (failedCmd == "voice.play") {
            if (playerObj) playerObj.fail(engine.failure.code)
        } else if (failedCmd == "media.get") {
            if (engine.failure.code != "cancelled") {
                media.saving() ? saveFailed.show() : mediaFailed.show()
            }
        } else if (failedCmd == "msg.edit") {
            editFailed.show()
        } else if (failedCmd == "msg.forward") {
            forwardFailed.show()
        } else if (failedCmd == "poll.vote" || failedCmd == "poll.create") {
            page.reactBar() // its own failedCmd words these (ReactBar.qml)
        }
    }
    onChatChanged: {
        ctl.open(chat, topic)
        // The list starts hidden (listHidden): the guard shows it whatever happens, from the open on.
        revealGuard.start()
        // Pinned messages, not in a secret chat (Telegram has no pins there), after Search as before.
        if (!chat.secret) addAction(pinnedAction, ActionBarPlacement.InOverflow)
        // Translation is refused in a secret chat. Every other kind of chat, channels too.
        if (!chat.secret) addAction(translateAction, ActionBarPlacement.InOverflow)
        if (chat.kind != 2) addAction(infoAction, ActionBarPlacement.InOverflow)
        if (chat.kind == 1) addAction(pollAction, ActionBarPlacement.InOverflow) // groups: Telegram refuses polls in a chat with a person
        if (chat.secret) addAction(timerAction, ActionBarPlacement.InOverflow)
        ctl.messages.itemAdded.connect(page.added)
        ctl.messages.newestComing.connect(page.coming)
        ctl.messages.loadingChanged.connect(page.loaded)
        ctl.messages.translateStalled.connect(page.translateStalled)
        ctl.mentionsChanged.connect(page.jumpCounts)
        ctl.unreadFound.connect(page.jump) // the @ and heart buttons' message: loaded if far back, scrolled to and tinted
        media.ready.connect(page.mediaReady)
        media.openFailed.connect(mediaFailed.show)
        media.saved.connect(savedToast.show)
        media.pictureEdited.connect(page.edited) // the editor's file: the send-files page's, else staged as the picker's
    }
    attachedObjects: [
        // Attach (step 6.2): the system's picker, every file type; the engine sends by the extension. One file waits in
        // the staged bar; several open the send-files page (the user's ask, 2026-11-18).
        FilePicker {
            id: attachPicker
            type: FileType.Picture | FileType.Video | FileType.Music | FileType.Document | FileType.Other
            mode: FilePickerMode.PickerMultiple
            title: qsTr("Attach")
            onFileSelected: selectedFiles.length > 1 ? page.openFiles(selectedFiles) : page.stage(selectedFiles[0])
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
                // An open at the first unread: the list's place has held since the last aim, so the cards' heights are in and
                // another aim would move nothing. The reveal waits for the aims, so this is 150 ms of an open (1.0.0.94's log).
                // Only for "unread": a sent photo's card can grow after its first layout, and "bottom" always reads entry 0.
                var at = scrollState.firstVisibleItem.length > 0 ? "" + scrollState.firstVisibleItem[0] : ""
                if (page.aimAt == "unread" && page.aims >= 1 && at == page.aimHeld) {
                    page.endAim()
                    return
                }
                // The same for an open that ends at the bottom (an unread run shorter than the screen, releaseUnread): the list is
                // hidden, sticks to its bottom edge and already rests there, so the reveal need not wait for two more aims
                // (455 ms in 1.0.0.97's log). Only while hidden: a sent photo's aim, on a list in view, runs all three.
                if (page.listHidden && page.aimAt == "bottom" && page.aims >= 1 && scrollState.atBeginning) {
                    page.endAim()
                    return
                }
                page.aimHeld = at
                page.aim()
                page.aims = page.aims + 1
                if (page.aims >= 3) {
                    page.endAim()
                }
            }
        },
        // The older rows go in once the list has left its top edge (releaseUnread).
        Timer {
            id: release
            interval: 100
            singleShot: true
            onTimeout: {
                console.log("chat: released off the edge, atEnd " + scrollState.atEnd)
                ctl.messages.releaseOlder()
                page.revealList()
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
        PollAction { id: pollAction; jid: page.chat ? page.chat.jid : ""; topic: page.topic },
        // Added for secret chats (a secret chat's timer), enabled once it is ready.
        TimerAction { id: timerAction; jid: page.chat ? page.chat.jid : ""; ready: page.secret == 2 },
        // Added for every chat but a secret one (ActionItem has no visible).
        PinnedAction {
            id: pinnedAction
            chatCtl: ctl
            onJump: page.jump(id)
        },
        // Added for every chat but a secret one (ActionItem has no visible). Opened with the language the model has now.
        ActionItem {
            id: translateAction
            title: qsTr("Translate…")
            imageSource: "asset:///images/translate_action.png"
            // The picker is made at its first use, like translateDialog().
            onTriggered: {
                if (!page.pickerObj) {
                    page.pickerObj = pickerDef.createObject(page)
                }
                page.pickerObj.pick(page.chat.jid, ctl.messages ? ctl.messages.translate : "")
            }
        },
        ComponentDefinition {
            id: pickerDef
            TranslatePicker {}
        },
        SystemToast {
            id: translateWarning
        },
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
        // The tint of a quote's jump goes after a moment, unless a search took it over meanwhile.
        Timer {
            id: quoteFlash
            property string target
            interval: 1500
            singleShot: true
            onTimeout: {
                if (list.foundId == target) {
                    list.foundId = ""
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
            id: translateDef
            TranslateDialog {
                chatCtl: ctl
                // The chat's live language (the model re-reads it), not page.chat's, which is as it was at the open.
                defaultLang: ctl.messages && ctl.messages.translate ? ctl.messages.translate : phoneLocale.language
            }
        },
        ComponentDefinition {
            id: stagedDef
            StagedBar {
                info: page.stagedInfo
                canFile: page.stagedPng != "" || page.stagedJpeg
                canEdit: page.stagedPng != "" || /\.(jpe?g|png|gif|bmp|mp4|3gp|mov)$/i.test(page.staged)
                // The original PNG when the staged JPEG was made from one.
                onEdit: media.editPicture(page.stagedPng != "" ? page.stagedPng : page.staged)
                onCancelled: page.unstage()
            }
        },
        ComponentDefinition {
            id: filesDef
            source: "SendFilesPage.qml"
        },
        ComponentDefinition {
            id: stickersDef
            source: "StickersPage.qml"
        },
        SystemToast {
            id: mediaFailed
            body: qsTr("Couldn't open this file")
        },
        SystemToast {
            id: savedToast
            body: qsTr("Saved to Downloads")
        },
        SystemToast {
            id: saveFailed
            body: qsTr("Couldn't save to Downloads")
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
        SystemToast {
            id: forwardFailed
            body: qsTr("Couldn't forward this message")
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
    property variant translateObj
    property variant pickerObj
    // The codes already warned about in this opening of the chat, "|" before each: the warning shows once for each.
    // A string: an object in a variant property is copied at each read, so a key set on it is lost.
    property string warned: ""
    // In-place translation failed (MessageModel retries the ones that may pass, itself). The other codes, offline above all, are
    // not worth a toast.
    function translateStalled(code) {
        var words = code == "rate_limited" ? (settings.googleKey == "" ? qsTr("Telegram is limiting translations. The messages will be translated when it answers again, or add a Google API key in Settings > Translation.")
                                                                      : qsTr("The translation service is busy. The messages will be translated when it answers again."))
                  : code == "no_key" ? qsTr("Set up the translation service in Settings")
                  : code == "unsupported" ? qsTr("This service can't translate into this language") : ""
        if (words == "" || warned.indexOf("|" + code) >= 0) {
            return
        }
        warned += "|" + code
        translateWarning.body = words
        translateWarning.show()
    }
    // The send-files page over this one (openFiles); only meant while nav.top is it (edited).
    property variant filesPage
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
    // The translation dialog, made at its first use; its parent is the page, which it goes with.
    function translateDialog() {
        if (!translateObj) {
            var d = translateDef.createObject(page)
            ctl.translated.connect(d.got)
            ctl.translateFailed.connect(d.failed)
            translateObj = d
        }
        return translateObj
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
        // A text is in the list inside ctl.send (its local row, MessageModel::addLocal), so followOwn is set before: added()
        // reads it as the row goes in, and clears it. A staged file's row comes later, so that one is set after, as before.
        var plain = staged == ""
        var followed = page.followOwn
        if (plain) {
            page.followOwn = !editing // an edit adds no row to follow
        }
        // Cleared only once the engine has it: it writes the row before it replies, so nothing typed is lost.
        if (!(plain ? ctl.send(composer.text, mentionBar.take(composer.text), true) : page.sendStaged(text))) {
            page.followOwn = followed
            notSent.show()
            return
        }
        composer.text = ""
        page.picking = false // sent, the emoji picker closes (the user's ask, 2026-11-17)
        page.unstage()
        ctl.cancelReply()
        if (!plain) {
            page.followOwn = !editing
        }
    }
    // What media.send is given for one file, without the chat, topic, quote and caption (sendStaged and sendFiles). asFile:
    // the PNG itself, which the engine sends as a document by its extension, or the JPEG with `file`.
    function fileArgs(path, png, asFile) {
        var args = {"path": asFile && png != "" ? png : path}
        if (asFile && png == "") {
            args.file = true
        }
        // A photo's thumbnail and size come from here: the engine decodes no picture (step 6.3).
        var info = media.photoInfo(path)
        if (info.width) {
            args.width = info.width
            args.height = info.height
        }
        if (info.thumb) {
            args.thumb = info.thumb
        }
        // A video goes with a play mark on grey for a thumbnail, so the phone that gets it shows a video, not a grey box.
        var ext = path.substring(path.lastIndexOf(".") + 1).toLowerCase()
        if (ext == "mp4" || ext == "3gp" || ext == "mov") {
            var poster = media.videoPoster()
            if (poster) {
                args.thumb = poster
            }
        }
        return args
    }
    // A photo's or a video's caption rides in the message; the other kinds' text follows as a message.
    function sendStaged(text) {
        // From a window of history around the first unread message our row would be dropped (it is beyond the
        // loaded ones): the newest first, as ctl.send does for a text.
        ctl.messages.toLiveEnd()
        // Send as file: the PNG itself, which the engine sends as a document by its extension, or the JPEG with `file`
        // (the user's ask, 2026-11-17); the text follows it.
        var asFile = page.stagedBar().asFile && (stagedPng != "" || stagedJpeg)
        var picture = stagedInfo.picture && !asFile
        var args = fileArgs(staged, stagedPng, asFile)
        args.chat = chat.jid
        if (topic) {
            args.topic = topic
        }
        if (ctl.quoteId()) {
            args.quote = ctl.quoteId()
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
    // Several files, from the send-files page (the user's ask, 2026-11-18): items are its {path, png, info}. Each is a
    // photo or video (class 0), an audio file (1) or a document (2); with group, each class goes as albums of at most
    // 10 (Telegram's own rule: photos and videos mix, documents and audio only with their own kind), the classes in
    // the order they first appear; without it, one message each, in order. A lone file of a class goes as media.send.
    // The first request carries the topic and the quote, and the caption when its first file is a photo or video.
    function sendFiles(items, group, asFile, caption) {
        if (items.length == 0) {
            return
        }
        ctl.messages.toLiveEnd()
        var sends = []
        var order = []
        for (var i = 0; i < items.length; i++) {
            var audio = /\.(mp3|m4a|aac|amr|ogg|wav)$/i.test(items[i].path)
            var kind = !asFile && items[i].info.picture ? 0 : !asFile && audio ? 1 : 2
            sends.push({"kind": kind, "args": fileArgs(items[i].path, items[i].png, asFile)})
            if (order.indexOf(kind) < 0) {
                order.push(kind)
            }
        }
        var chunks = []
        if (!group) {
            for (var j = 0; j < sends.length; j++) {
                chunks.push([sends[j]])
            }
        } else {
            for (var c = 0; c < order.length; c++) {
                var chunk = []
                for (var k = 0; k < sends.length; k++) {
                    if (sends[k].kind == order[c]) {
                        chunk.push(sends[k])
                        if (chunk.length == 10) {
                            chunks.push(chunk)
                            chunk = []
                        }
                    }
                }
                if (chunk.length > 0) {
                    chunks.push(chunk)
                }
            }
        }
        var quote = ctl.quoteId()
        var captioned = sends[0].kind == 0 && caption != ""
        for (var r = 0; r < chunks.length; r++) {
            var req
            if (chunks[r].length == 1) {
                req = chunks[r][0].args
            } else {
                req = {"items": []}
                for (var m = 0; m < chunks[r].length; m++) {
                    req.items.push(chunks[r][m].args)
                }
            }
            req.chat = chat.jid
            if (r == 0) {
                if (topic) {
                    req.topic = topic
                }
                if (quote) {
                    req.quote = quote
                }
                if (captioned) {
                    req.caption = caption
                }
            }
            if (!engine.request(chunks[r].length == 1 ? "media.send" : "media.album", req)) {
                notSent.show()
                return
            }
        }
        if (!captioned && caption != "") {
            // The files carry the reply; the text after them is plain.
            ctl.cancelReply()
            ctl.send(caption)
        }
        composer.text = ""
        ctl.cancelReply()
        page.followOwn = true
        if (filesPage && nav.top == filesPage) {
            nav.pop()
        }
        filesPage = null
    }
    // The editor's file: the send-files page's picture when it is on top, else staged as the picker's.
    function edited(path) {
        if (filesPage && nav.top == filesPage) {
            filesPage.edited(path)
        } else {
            stage(path)
        }
    }
    // Several files go to the send-files page, pushed over this one; one waits in the staged bar as the picker's.
    // The composer's text is the caption. Each goes through the same rule as stage: a PNG as a JPEG, a file that can't
    // be read skipped.
    function openFiles(paths) {
        var items = []
        for (var i = 0; i < paths.length; i++) {
            var f = fileItem(paths[i])
            if (f) {
                items.push(f)
            }
        }
        if (items.length == 1) {
            stage(items[0].png != "" ? items[0].png : items[0].path)
        } else if (items.length > 1) {
            var files = filesDef.createObject()
            files.media = media
            files.caption = composer.text
            files.items = items
            files.send.connect(page.sendFiles)
            filesPage = files
            nav.push(files)
        }
    }
    // The sticker page, pushed over this one from the emoji picker's last cell; a tap there sends and closes it (sendSticker).
    function openStickers() {
        var stickers = stickersDef.createObject()
        stickers.picked.connect(page.sendSticker)
        nav.push(stickers)
    }
    // A sticker is a message of its own: the topic and the quote go with it, as with a file. It goes at once, and the sticker
    // page closes, and the emoji picker under it, as after a send (the user, 2026-11-19). A request that did not go out
    // leaves the page open.
    function sendSticker(file) {
        ctl.messages.toLiveEnd()
        var args = {"chat": chat.jid, "file": file}
        if (topic) {
            args.topic = topic
        }
        if (ctl.quoteId()) {
            args.quote = ctl.quoteId()
        }
        if (!engine.request("sticker.send", args)) {
            notSent.show()
            return
        }
        ctl.cancelReply()
        page.followOwn = true
        nav.pop()
        page.picking = false
    }
    // A file to send as {path, png, info}: a PNG goes as a photo, made a JPEG as the story picker does (the engine sends
    // a .png as a file), with the original in png; info is MediaOpener.fileInfo's. Null for a file that can't be read.
    function fileItem(path) {
        var png = ""
        if (/\.png$/i.test(path)) {
            var jpg = media.asJpeg(path)
            if (jpg) {
                png = path
                path = jpg
            }
        }
        var info = media.fileInfo(path)
        return info.name ? {"path": path, "png": png, "info": info} : null
    }
    // The picker's file waits in the staged bar; what is typed is its caption.
    function stage(path) {
        // The picker can't say which of its tabs the file came from, so every PNG does (and a failed one stays a file).
        var f = fileItem(path)
        if (!f) {
            return
        }
        staged = f.path
        stagedPng = f.png
        page.stagedBar().asFile = false
        stagedInfo = f.info
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
    // A new message, ours or theirs, while the list was at the bottom slides in instead of popping in (the user, 2026-11-20).
    // Before it goes in (coming, MessageModel's newestComing) the list stops sticking to the bottom (slidePin), so the row
    // lands below the screen and nothing on it moves; its card, once laid out, gives its exact height (slideIn, from
    // MessageCard::onLaidOut). A guess moved the rows before the list had the row in: they jumped down, then up (the user's
    // video, 0.1.0.69). Not in a window of history, nor over another aim (the unread divider, a jump). Ours while scrolled
    // up jumps to it, as before.
    property bool slidePin: false
    function coming() {
        var inWindow = ctl.messages && ctl.messages.window
        if (page.placed && scrollState.atBeginning && !inWindow && (aimAt == "" || aimAt == "bottom")) {
            slideAnim.stop()
            list.translationY = 0
            slidePin = true
            slideGuard.start()
        }
    }
    function nextReaction() { ctl.nextReaction() }
    function nextMention() { ctl.nextMention() }
    // The made buttons' counts follow the controller's (assigned, not bound: the delegates make them late).
    function jumpCounts() {
        if (heartSlot.control) { heartSlot.control.count = ctl.reactions }
        if (atSlot.control) { atSlot.control.count = ctl.mentions }
    }
    function added(indexPath) {
        if (indexPath[0] != 0) {
            return
        }
        if (slidePin) {
            followOwn = false
            list.slideId = ctl.messages.idAt(indexPath[0])
            cardsProvider.slideRow(list.slideId) // a card already showing it reports now (MessageCards::slideRow)
        } else if (followOwn) {
            followOwn = false
            list.scrollToPosition(ScrollPosition.Beginning, ScrollAnimation.None)
            startAim("bottom", "")
        }
    }
    // A scroll right after rows go in stops short: the list places them by estimated heights until their cards
    // are made, and a photo's card is far taller than the estimate (HyperBerry's pitfalls; a sent photo was left
    // half below the screen and a media-heavy chat opened below its divider, Classic 0.1.0.9). So the scroll is
    // aimed again every 150 ms, 3 times, as the real heights come in; a touch on the list ends it, and so does an
    // aim at the first unread when the list's place has held since the last aim (reaim).
    // aimAt: "bottom" (our sent row), "unread" (the divider), "item" (aimId, a search or pin jump).
    property string aimAt: ""
    property string aimId: ""
    property int aims: 0
    // The list's first visible entry at the last aim: the unread aim ends when it is the same again (reaim).
    property string aimHeld: ""
    function startAim(what, id) {
        aimAt = what
        aimId = id
        aims = 0
        aimHeld = ""
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
        var unread = aimAt == "unread"
        aimAt = ""
        if (unread) {
            releaseUnread()
        } else {
            revealList()
        }
    }
    // The list is placed out of sight, in every chat (the user, 2026-11-20): a chat opening at its first unread message lagged
    // and the list moved (the jump to the divider, its three aims while the cards' real heights come in, and the older rows
    // going in above it were all seen), and one opening at the bottom drew its first page, then faded, then drew it again,
    // since loaded() hid the list only once the page was in. So the list starts hidden (opacity 0 below) and is shown, fading
    // in, once it is placed: at once by loaded() for a chat with nothing unread, when the aim ends for a jump (endAim) or an
    // unread chat (endAim, the release timer), or after 1.5 s whatever happened (revealGuard, started by onChatChanged). Only
    // the first placement hides: a later load of the same chat (toLiveEnd, a window left for a jump) finds the list showing.
    property bool listHidden: true // EmptyNote's spinner stays on meanwhile
    function hideList() {
        revealAnim.stop()
        revealing = false
        listHidden = true
        list.opacity = 0
        revealGuard.start()
    }
    function revealList() {
        revealGuard.stop()
        listHidden = false
        if (list.opacity < 1 && !revealing) {
            revealing = true
            revealAnim.play()
        }
    }
    property bool revealing: false
    // The divider is the list's top now; the rows above it go in (MessageModel.releaseOlder). A list sitting exactly
    // at that edge keeps it in view when rows are appended there, which pushed the divider to the bottom of the screen
    // (Classic, 0.1.0.12). So the list is moved a few pixels off the edge first and the rows go in a moment later. An
    // unread run shorter than the screen has no room to move: it ends as Telegram's, the newest at the bottom.
    function releaseUnread() {
        console.log("chat: release, atBeginning " + scrollState.atBeginning + ", atEnd " + scrollState.atEnd)
        if (scrollState.atBeginning) {
            ctl.messages.releaseOlder()
            startAim("bottom", "")
        } else {
            list.scroll(4, ScrollAnimation.None)
            release.start()
        }
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
        } else {
            revealList() // the message isn't shown (hidden by a pending delete): nothing to wait for
        }
    }
    property string jumpPending: ""
    // The first page has come in and been placed (loaded): only that placement hides the list.
    property bool firstLoaded: false
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
        var first = !page.firstLoaded
        page.firstLoaded = true
        if (page.jumpPending != "") {
            var id = page.jumpPending
            page.jumpPending = ""
            page.jump(id) // the list is shown when its aim ends, or at once when the message isn't there
        } else {
            var u = ctl.messages.unreadEntry()
            if (u >= 0 && first) {
                hideList() // already hidden: this restarts the guard from here
                list.scrollToItem([u], ScrollAnimation.None)
                startAim("unread", "")
                return // settle starts when the aim ends
            }
            revealList() // nothing unread: the first page is at the bottom already
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
        if (!composerBar.readOnly) composer.requestFocus()
    }
    // On the Q10, typing anywhere on the page goes to the field (step 3.2). A key reaches the page only
    // when the field doesn't have the focus: it goes in at the end, and the field takes over from the
    // next key. Modifiers, Sym and Delete arrive cut to their low byte with a keycap of 0xE000 or more
    // (bb10-pitfalls); control characters (Enter, Backspace) and Ctrl shortcuts aren't text.
    function typeKey(event) {
        // The search field and the composer (Composer.qml) are inside the page, so their keys come here too
        // (Q10, 0.0.1.122; the composer typed letters twice, 0.0.1.231); and a channel has no composer.
        if ((searchObj && searchObj.typing) || composer.focused || composerBar.readOnly) {
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
        topicName: page.topicName
        // While the chat's messages are being translated the line says so: Telegram can take half a minute.
        status: ctl.messages && ctl.messages.translating ? qsTr("Translating…") : ctl.status
        secret: page.secret
        members: ctl.groupInfo.participants ? ctl.groupInfo.participants.length : 0
        onTapped: infoAction.showInfo()
        // Only while this page is on top: a second tap during the slide out popped the chat list too, the last page,
        // and left the app black (Classic, 0.1.0.13).
        onBack: {
            if (nav.top == page) {
                nav.pop()
            }
        }
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
            // The list, and over it the note of an empty chat. No wallpaper (taken off at the user's ask, 2026-11-18): the page's
            // own colour, painted here so that the picture cards' corner masks (MediaBubble) match what is behind the cards.
            Container {
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }
                background: Color.create(nav.pageColor)
                layout: DockLayout {}
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                ListView {
                    id: list
                    // Filled: a DockLayout's child sits at its top left, at its own size, otherwise.
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    // The C++ cards call these functions on the list: the long-press menu's actions, a mention's
                    // tap, the reactions' chips.
                    function reply(msg) {
                        ctl.reply(msg)
                        composer.requestFocus()
                    }
                    function copy(text) {
                        ctl.copy(text)
                    }
                    // Translate: a message already shown translated gives its original, any other its translation.
                    function translate(msg) {
                        if (msg.translated) {
                            page.translateDialog().showOriginal(msg)
                        } else {
                            page.translateDialog().ask(msg)
                        }
                    }
                    // Forward: the Send to page, titled Forward to; its pick sends the message (main.qml's forwardTo).
                    function forward(msg) {
                        nav.forwardPick({"chat": page.chat.jid, "id": msg.id})
                    }
                    // Edit (ours, a text): the composer takes its text, the bar above says so, Send replaces it.
                    function edit(msg) {
                        page.unstage()
                        ctl.edit(msg)
                        composer.text = msg.text
                        composer.requestFocus()
                        composer.editor.cursorPosition = msg.text.length
                    }
                    // A tap on a quote: the quoted message, tinted as a search match for a moment.
                    function openQuote(msg) {
                        page.jump(msg.quoteId)
                        quoteFlash.target = msg.quoteId
                        quoteFlash.start()
                    }
                    // A mention's link in a card: the person's page.
                    function openPerson(jid, name) {
                        infoAction.showPerson(jid, name)
                    }
                    // Reactions (step 5.11): the bar over the composer, and a chip's tap.
                    function react(msg) {
                        page.reactBar().open(msg)
                    }
                    function toggleReaction(id, emoji, mine) {
                        page.reactBar().toggle(id, emoji, mine)
                    }
                    // Polls (step 6.5e): a vote, and the toast of the counts, ReactBar's.
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
                    function seekVoice(msg, ms) {
                        page.player().seek(msg.id, ms)
                    }
                    function playedWave(data, bars) {
                        return ctl.playedWave(data, bars)
                    }
                    // Photos, videos, audio files and documents: the message being fetched, for the bubbles
                    // (MediaBubble.cpp). Always through media.get: it answers at once when the file is here,
                    // and fetches it again when the user moved it out of Downloads. A location and a contact
                    // have no file.
                    property string mediaLoadingId: media.loadingId
                    // How far the fetch is, 0 until bytes arrive, and its text ("1.2 MB / 23 MB"), for the ring.
                    property real mediaProgress: media.progress
                    property string mediaProgressText: media.progressText
                    // The same for the files of our messages being sent, by message id: {progress, text}.
                    property variant mediaUploads: media.uploads
                    property variant joiner: invites // for the cards, which see no context objects
                    function openMedia(msg) {
                        if (msg.media == "location") {
                            maps.choose(msg.lat, msg.lon, msg.place)
                        } else if (msg.media == "contact") {
                            media.importContact(msg.vcard)
                        } else if (mediaUploads[msg.id]) {
                            ctl.deleteForMe(msg.id) // a tap on one being sent stops it: TDLib cancels the upload with the message
                        } else if (media.loadingId == msg.id) {
                            media.cancel() // a tap on the one being fetched stops it
                        } else {
                            page.videoRatio = msg.ratio > 0 ? msg.ratio : 0.75
                            page.videoGif = !!msg.gif
                            media.get(page.chat.jid, msg.id, msg.media)
                        }
                    }
                    // The menu's Download (the user's ask, 2026-11-18): a document or audio file into the phone's
                    // Downloads; savedToast or saveFailed answers.
                    function download(msg) {
                        media.get(page.chat.jid, msg.id, msg.media, true)
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
                    // Round mode (MessageCard.cpp): the cards' corners and the sender's tile.
                    property bool round: settings.round
                    // A quote's background: white at 8 % lifts it off a dark card, black at 5 % off
                    // a bright one.
                    property string quoteTint: settings.theme == 1 ? "#0D000000" : "#14FFFFFF"
                    property string pressColor: nav.pressColor
                    // Telegram's bubble and pill colours (main.qml), read by the C++ cards.
                    property string inColor: nav.inColor
                    property string outColor: nav.outColor
                    property string outMeta: nav.outMeta
                    property string serviceColor: nav.serviceColor
                    // What is behind the cards, for a picture's rounded corners (MessageCard::paintBackground).
                    property string pageColor: nav.pageColor
                    dataModel: ctl.messages
                    // Out of sight until the first placement (page.listHidden); revealList fades it in.
                    opacity: 0
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
                    stickToEdgePolicy: (ctl.messages && ctl.messages.window) || page.slidePin ? ListViewStickToEdgePolicy.None
                                                                                             : ListViewStickToEdgePolicy.Beginning
                    // The new message's slide (page.coming): the row whose card measures itself. In one go the list jumps to
                    // the bottom (the rows move up by the row's height and the gap between cards) and is moved down as far,
                    // so nothing moves on screen; then it comes back up in 110 ms. The composer, after the list, covers what is
                    // moved below it.
                    property string slideId: ""
                    // The row's latest height: the slide starts when it has held for 30 ms (slideSettle): a reused card reports its old height, and its new layout came within 17 ms (0.1.0.74's log).
                    property real slideH: 0
                    function slideMeasured(h) {
                        slideGuard.stop() // the card is there: the slide waits for its height to hold
                        slideH = h
                        slideSettle.start()
                        return true
                    }
                    function slideIn(h) {
                        slideGuard.stop()
                        list.scrollToPosition(ScrollPosition.Beginning, ScrollAnimation.None)
                        list.translationY = h + ui.du(1)
                        slideAnim.fromY = list.translationY
                        slideAnim.play()
                        return true
                    }
                    attachedObjects: [
                        FadeTransition {
                            id: revealAnim
                            target: list
                            fromOpacity: 0
                            toOpacity: 1
                            duration: 120
                            onEnded: page.revealing = false
                        },
                        Timer {
                            id: revealGuard
                            interval: 1500
                            singleShot: true
                            // A model still loading has no rows: the spinner is up and the list stays hidden, so a slow first
                            // page (a window waiting for chat.newer) isn't seen at the bottom before it is placed.
                            onTimeout: {
                                if (ctl.messages && ctl.messages.loading) {
                                    revealGuard.start()
                                } else {
                                    page.revealList()
                                }
                            }
                        },
                        TranslateTransition {
                            id: slideAnim
                            target: list
                            toY: 0
                            duration: 110 // 150 felt slow (the user, 0.1.0.73)
                            easingCurve: StockCurve.CubicOut
                            // The list sticks to the bottom again, put back at it first: a card that grew while the list was
                            // pinned left it a few pixels off, and the next message then found it not at the bottom (0.1.0.72).
                            onEnded: {
                                list.scrollToPosition(ScrollPosition.Beginning, ScrollAnimation.None)
                                page.slidePin = false
                            }
                        },
                        Timer {
                            id: slideSettle
                            interval: 30
                            singleShot: true
                            onTimeout: {
                                if (list.slideId == "") {
                                    return
                                }
                                list.slideId = ""
                                list.slideIn(list.slideH)
                            }
                        },
                        // A new row no card measured (a day's header, a card not made): the list goes to it at once. 1 s: the
                        // first card a chat makes after opening came in after 650 ms, and 300 ms jumped it (0.1.0.72).
                        Timer {
                            id: slideGuard
                            interval: 1000
                            singleShot: true
                            onTimeout: {
                                console.log("chat: slide guard, row '" + list.slideId + "' not measured")
                                list.slideId = ""
                                list.scrollToPosition(ScrollPosition.Beginning, ScrollAnimation.None)
                                page.slidePin = false
                            }
                        },
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
                    listItemProvider: MessageCards {
                        id: cardsProvider
                    }
                }
                // ctl.messages is null until the page opens the chat, hence the first test.
                EmptyNote {
                    model: ctl.messages
                    placing: page.listHidden
                }
                // A window of history (a chat opened far back, a jump) has its newer page on its way: a spinner at the bottom edge, in a
                // Container whose padding is the gap, since a docked control's margins are ignored. The top edge's spinner is a row of the
                // list (MessageModel's loading row, LoadingCard).
                Container {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Bottom
                    bottomPadding: ui.du(1)
                    ActivityIndicator {
                        running: !!ctl.messages && ctl.messages.awaitingNewer && scrollState.atBeginning && !page.listHidden
                        preferredWidth: ui.du(6)
                        preferredHeight: ui.du(6)
                    }
                }
                // The jump buttons (JumpButton.qml), bottom to top as Telegram's: to the newest message, when the list is in a
                // window of history or far above it; to the next unread mention or reply to us (@); to our next message with
                // unread reactions (the heart). The @ and heart buttons are made when their count is above 0 (ControlDelegate), the down
                // button always. The mention and reaction ones show while the chat (or the topic) has any, with
                // the number; a tap jumps to the oldest, which the engine marks read (chat.mention, chat.reaction), so the next
                // tap goes to the next (the user, 2026-11-20). 5 px in from the right and the bottom in both modes (the user,
                // 2026-11-19): a docked control's margins are ignored, so the buttons sit in a transparent Container docked in
                // the corner, whose padding is the gap.
                // ponytail: entries, not pixels (~2 screens on the 720x720 Classic); measure in pixels if it
                // misfires on tall photo runs.
                Container {
                    id: jumps
                    // The list runs bottom to top: entry 0 is the newest, and the first visible item is the
                    // lowest one on screen.
                    property bool far: scrollState.firstVisibleItem.length > 0 && scrollState.firstVisibleItem[0] > 12
                    // Not while the list is placed out of sight (listHidden): the aims move it, `far` flips with them, and the
                    // down button blinked over the spinner in a chat with many unread messages (the user, 1.0.0.98).
                    visible: !page.listHidden
                    horizontalAlignment: HorizontalAlignment.Right
                    verticalAlignment: VerticalAlignment.Bottom
                    rightPadding: ui.px(5)
                    bottomPadding: ui.px(5)
                    ControlDelegate {
                        id: heartSlot
                        source: "JumpButton.qml"
                        delegateActive: !!ctl.messages && ctl.reactions > 0
                        visible: delegateActive
                        topMargin: ui.du(1)
                        bottomMargin: 0
                        onControlChanged: {
                            if (control) {
                                control.image = "asset:///images/heart.png"
                                control.count = ctl.reactions
                                control.tapped.connect(page.nextReaction)
                            }
                        }
                    }
                    ControlDelegate {
                        id: atSlot
                        source: "JumpButton.qml"
                        delegateActive: !!ctl.messages && ctl.mentions > 0
                        visible: delegateActive
                        topMargin: ui.du(1)
                        bottomMargin: 0
                        onControlChanged: {
                            if (control) {
                                control.glyph = "@"
                                control.count = ctl.mentions
                                control.tapped.connect(page.nextMention)
                            }
                        }
                    }
                    JumpButton {
                        visible: !!ctl.messages && (ctl.messages.window || jumps.far)
                        image: "asset:///images/bottom.png"
                        onTapped: {
                            ctl.messages.toLiveEnd()
                            list.scrollToPosition(ScrollPosition.Beginning, ScrollAnimation.Default)
                            page.startAim("bottom", "")
                        }
                    }
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
                        control.stickers.connect(page.openStickers)
                    }
                }
            }
            // Last: the picker above it takes the keyboard's place, as stock.
            Composer {
                id: composerBar
                mic: page.mic
                recording: page.recording
                // A channel; and a secret chat that can't take a message: pending (waiting for the other side) or closed.
                readOnly: page.chat ? page.chat.kind == 3 || page.secret == 1 || page.secret == 3 : false
                notice: page.secret == 1 ? qsTr("Waiting for %1 to get online…").arg(page.chat ? (chats.revision >= 0 && chats.chat(page.chat.jid).name) || page.chat.name : "")
                      : page.secret == 3 ? qsTr("Secret chat cancelled") : ""
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
