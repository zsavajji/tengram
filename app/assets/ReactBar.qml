import bb.cascades 1.4
import bb.system 1.2

// React to a message (step 5.11), a bar over the composer that the card's long-press menu opens
// (the user's choice, 2026-11-01): six quick emoji, ＋ for the full picker and ✕ to close. The six
// are TDLib's top reactions for the message (msg.reactions, asked when the bar opens; the bar shows
// when the answer is here), because TDLib refuses a reaction the chat or message doesn't allow
// ("The reaction isn't available for the message", the user, 2026-11-22). Our reactions on the
// message are underlined; tapping one again takes it back, and any other tap adds it. Either way
// the bar closes. The chips under the card (MessageCard) add or take back one reaction with
// toggle(). The engine's answer comes as the message changing (extra.reactions), which
// MessageModel reads.
Container {
    id: bar
    // The chat's jid, set by the page.
    property string jid
    // The page's ChatController, which asks for the allowed reactions; the page connects its reactionsAvailable and
    // reactionsFailed to got and failed (as TranslateDialog's).
    property variant chatCtl
    // The message being reacted to (a "msg" row of MessageModel); null when the bar is closed.
    property variant msg
    // The full picker is open under the six.
    property bool picking: false
    // The six cells, to move the underline.
    property variant cells: []
    // Every emoji the message allows, without U+FE0F (plain()); react() refuses the others.
    property variant allowed: []
    // The allowed reactions are here: the bar shows only then.
    property bool ready: false
    visible: !!msg && ready
    horizontalAlignment: HorizontalAlignment.Fill
    layout: StackLayout {
        orientation: LayoutOrientation.TopToBottom
    }
    // Telegram's reactions have no emoji selector (U+FE0F): ours has it ("❤️"), the message's list has not ("❤").
    function plain(emoji) {
        return emoji.replace(/\uFE0F/g, "")
    }
    function open(m) {
        msg = m
        picking = false
        ready = false
        allowed = []
        if (!chatCtl.reactionsFor(m.id)) {
            notSent.show()
            close()
        }
    }
    // The controller's answer: the six are the top reactions (Telegram's own bar), the first six of all when it has none.
    function got(msgId, top, all) {
        if (!msg || msg.id != msgId) {
            return
        }
        var shown = top.length > 0 ? top : all
        var n = Math.min(shown.length, cells.length)
        for (var i = 0; i < cells.length; i++) {
            cells[i].visible = i < n
            if (i < n) {
                cells[i].emoji = shown[i]
                cells[i].mine = msg.myReactions.indexOf(plain(shown[i])) >= 0
            }
        }
        var list = []
        for (var j = 0; j < all.length; j++) {
            list.push(plain(all[j]))
        }
        allowed = list
        ready = true
        if (list.length == 0) {
            say(qsTr("Reactions are off for this message"))
            close()
        }
    }
    function failed(msgId) {
        if (!msg || msg.id != msgId) {
            return
        }
        close()
        say(qsTr("Reaction not sent"))
    }
    function close() {
        msg = null
        picking = false
    }
    // Ours again is taking it back.
    // ponytail: the full picker still lists emoji the message refuses; a pick outside TDLib's list says so.
    // Filter the picker when that is felt.
    function react(emoji) {
        var e = plain(emoji)
        if (allowed.indexOf(e) < 0) {
            say(qsTr("That reaction isn't allowed here"))
            return
        }
        var mine = msg.myReactions.indexOf(e) >= 0
        if (!mine) {
            emojis.use(emoji)
        }
        toggle(msg.id, emoji, mine)
        close()
    }
    // A chip under a card, or react(): `mine` takes our reaction of that emoji back, else it is added.
    function toggle(id, emoji, mine) {
        if (!engine.request("msg.react", {"chat": jid, "id": id, "emoji": emoji, "remove": mine})) {
            notSent.show()
        }
    }
    // A toast with any text: the poll's counts (step 6.5e) and a vote or reaction that didn't go, which
    // sit here because ChatPage.qml is at its limit of 500 lines.
    function say(text) {
        whoToast.body = text
        whoToast.show()
    }
    property string failedCmd: engine.failure.cmd || ""
    onFailedCmdChanged: {
        if (failedCmd == "msg.react") {
            say(qsTr("Reaction not sent"))
        } else if (failedCmd == "poll.vote") {
            say(qsTr("Vote not sent"))
        } else if (failedCmd == "poll.create") {
            say(qsTr("Poll not sent"))
        }
    }
    onPickingChanged: {
        if (!picking) {
            return
        }
        // ponytail: a second picker instance beside the composer's, made only when ＋ is used;
        // share the page's if the memory (the engine is capped near 30 MB) or the first open's
        // time shows on the Q10.
        pickerSlot.delegateActive = true
        emojis.refresh()
    }
    onCreationCompleted: {
        var all = ["👍", "❤️", "😂", "😮", "😢", "🙏"]
        var made = []
        for (var i = 0; i < all.length; i++) {
            var c = cellDef.createObject()
            c.emoji = all[i]
            c.tapped.connect(bar.react)
            row.insert(i, c)
            made.push(c)
        }
        cells = made
    }
    attachedObjects: [
        ComponentDefinition {
            id: cellDef
            // A touch target the size of an action column (9 du), as the search bar's. This and the bar's two buttons below are
            // reached by the Classic's trackpad: FocusPolicy.Key, not KeyAndTouch, so a touch leaves the focus (the composer's
            // keyboard) where it is; the one with the focus is lit as the pressed colour, and a click (Enter) does what a tap does.
            TapArea {
                id: cell
                property string emoji
                property alias mine: underline.visible
                signal tapped(string emoji)
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(9)
                preferredHeight: ui.du(9)
                layout: DockLayout {
                }
                focusPolicy: FocusPolicy.Key
                background: cell.focused ? Color.create(nav.pressColor) : Color.Transparent
                onClicked: cell.tapped(cell.emoji)
                Label {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    text: cell.emoji
                    textFormat: TextFormat.Plain
                    textStyle.base: nav.emojiStyle
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: Math.round(nav.bodySize * 1.3)
                }
                Container {
                    id: underline
                    visible: false
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Bottom
                    bottomMargin: ui.du(1)
                    preferredWidth: ui.du(5)
                    preferredHeight: ui.px(2)
                    background: ui.palette.primary
                }
            }
        },
        SystemToast {
            id: notSent
            body: qsTr("Not sent: Tengram is reconnecting. Try again in a moment.")
        },
        // say()'s toast.
        SystemToast {
            id: whoToast
        }
    ]
    // The row alone is grey: the full picker under it sits on the page, as the composer's does
    // (Q10, 0.0.1.127: it was grey).
    Container {
        id: row
        horizontalAlignment: HorizontalAlignment.Fill
        background: ui.palette.plain
        // As the reply and search bars': the first tap target sat against the screen's edge.
        leftPadding: ui.du(1.5)
        layout: StackLayout {
            orientation: LayoutOrientation.LeftToRight
        }
        // The six emoji are inserted before these two.
        TapArea {
            id: plusBtn
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(9)
            preferredHeight: ui.du(9)
            layout: DockLayout {
            }
            focusPolicy: FocusPolicy.Key
            background: plusBtn.focused ? Color.create(nav.pressColor) : Color.Transparent
            onClicked: bar.picking = !bar.picking
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                text: "＋"
                textFormat: TextFormat.Plain
                textStyle.color: ui.palette.secondaryTextOnPlain
            }
        }
        TapArea {
            id: closeBtn
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(9)
            preferredHeight: ui.du(9)
            layout: DockLayout {
            }
            focusPolicy: FocusPolicy.Key
            background: closeBtn.focused ? Color.create(nav.pressColor) : Color.Transparent
            onClicked: bar.close()
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                text: "✕"
                textFormat: TextFormat.Plain
                textStyle.color: ui.palette.secondaryTextOnPlain
            }
        }
    }
    // Created at the first ＋ and kept (delegateActive stays true), as the page's is.
    ControlDelegate {
        id: pickerSlot
        source: "EmojiPicker.qml"
        delegateActive: false
        visible: bar.picking
        horizontalAlignment: HorizontalAlignment.Fill
        onControlChanged: {
            if (control) {
                control.picked.connect(bar.react)
            }
        }
    }
}
