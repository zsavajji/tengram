import bb.cascades 1.4
import bb.system 1.2

// React to a message (step 5.11), a bar over the composer that the card's long-press menu opens
// (the user's choice, 2026-11-01): six quick emoji, ＋ for the full picker and ✕ to close. Our
// reaction on the message is underlined; tapping it again takes it back, and any other tap
// reacts. Either way the bar closes. The engine's answer comes as the message changing, which
// MessageModel reads (the chip under the card).
Container {
    id: bar
    // The chat's jid, set by the page.
    property string jid
    // The message being reacted to (a "msg" row of MessageModel); null when the bar is closed.
    property variant msg
    // The full picker is open under the six.
    property bool picking: false
    // The six cells, to move the underline.
    property variant cells: []
    visible: !!msg
    horizontalAlignment: HorizontalAlignment.Fill
    layout: StackLayout {
        orientation: LayoutOrientation.TopToBottom
    }
    function open(m) {
        msg = m
        picking = false
        for (var i = 0; i < cells.length; i++) {
            cells[i].mine = !!m && m.myReaction == cells[i].emoji
        }
    }
    function close() {
        msg = null
        picking = false
    }
    // Ours again is taking it back: an empty emoji.
    function react(emoji) {
        var e = emoji == msg.myReaction ? "" : emoji
        if (e != "") {
            emojis.use(emoji)
        }
        if (!engine.request("msg.react", {"chat": jid, "id": msg.id, "emoji": e})) {
            notSent.show()
        }
        close()
    }
    function who(id) {
        say(chats.reactionsText(jid, id))
    }
    // The who-reacted toast with any text: who voted for a poll's option (step 6.5e) and a vote that
    // didn't go, which sit here because ChatPage.qml is at its limit of 500 lines.
    function say(text) {
        whoToast.body = text
        whoToast.show()
    }
    property string failedCmd: engine.failure.cmd || ""
    onFailedCmdChanged: {
        if (failedCmd == "poll.vote") {
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
            // A touch target the size of an action column (9 du), as the search bar's.
            Container {
                id: cell
                property string emoji
                property alias mine: underline.visible
                signal tapped(string emoji)
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(9)
                preferredHeight: ui.du(9)
                layout: DockLayout {
                }
                gestureHandlers: TapHandler {
                    onTapped: cell.tapped(cell.emoji)
                }
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
            body: qsTr("Not sent: HyperBerry is reconnecting. Try again in a moment.")
        },
        // Who reacted, one per line (ChatListModel.reactionsText); say() puts other text in it.
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
        Container {
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(9)
            preferredHeight: ui.du(9)
            layout: DockLayout {
            }
            gestureHandlers: TapHandler {
                onTapped: bar.picking = !bar.picking
            }
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                text: "＋"
                textFormat: TextFormat.Plain
                textStyle.color: ui.palette.secondaryTextOnPlain
            }
        }
        Container {
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(9)
            preferredHeight: ui.du(9)
            layout: DockLayout {
            }
            gestureHandlers: TapHandler {
                onTapped: bar.close()
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
