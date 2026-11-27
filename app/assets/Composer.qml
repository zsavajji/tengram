import bb.cascades 1.4
import tengram 1.0

// The chat's own composer row (user's choice, 2026-11-08): Emoji | field | Attach | Send | ⋮. The
// system TextInputActionItem it replaces has no layout control: it grew a block taller than its
// text with the icons on their own row, Emoji couldn't go left, and it takes no text style, so
// emoji past Unicode 6 were boxes while typing. Here the field grows a line at a time (to 5) and
// Attach and Send stay on its last line, Emoji and ⋮ centred on the row.
// Enter submits whatever the page makes of it (send, or a new line) and the field keeps the focus,
// so the next message can be typed at once. Send is the touch path to sending (the Q10 has no
// on-screen submit key), and Record while the field is empty, as WhatsApp's: a tap starts, the next
// one sends. The phone's own send and delete icons are read at run time, not shipped.
// The four buttons are reached by the Classic's trackpad: FocusPolicy.Key, not KeyAndTouch, since a touch that took the focus
// would drop the field's (and the keyboard with it, see the field's focusRetentionPolicyFlags). A focused one is lit as the
// pressed colour, and a click (Enter) does what a tap does.
Container {
    id: bar
    property alias field: field
    // Empty field (nothing staged, nobody recording): Send shows Record.
    property bool mic
    property bool recording
    // A channel (chat.kind == 3): nothing to write, only the menu. A secret chat that is pending or closed is the same,
    // with `notice` in the field's place.
    property bool readOnly
    property string notice
    // A channel (the user's ask, 2026-11-25, as Telegram's bar): Search at the left, the mute toggle in the field's place,
    // and, when it has a discussion group (the page's channel.discuss), Discuss before the menu. muted: the chat's now.
    property bool channel: false
    property bool muted: false
    property bool discuss: false
    // A group or channel we are not in (a channel's discussion group opened by Discuss, a public chat opened from Search; the
    // user's ask, 2026-11-25, as Telegram's): Join in the whole row's place but the menu (and a channel's Search and Discuss),
    // until we are in. Set with readOnly.
    property bool join: false
    signal emoji
    signal discard
    signal attach
    signal send
    signal submitted
    // Forwarded: field.text notifies only when committed (as SearchBar's field), this follows the keys.
    signal changing(string text)
    signal menu
    signal discussTapped
    signal searchTapped
    signal muteTapped
    signal joinTapped
    // Telegram's Markdown marks around the field's selection (the engine reads them when the text is sent), from the
    // field's context menu. By the time an item of that menu is tapped the field has no selection any more (the user,
    // 1.0.5.193: the marks went in at the cursor "even when word is selected"; ui.log, 1.0.5.194: "selection 26-26 of 0,
    // kept 0-27 of 27"), so the selection is remembered as it changes (selectionMoved) and taken when the menu opens
    // (menuOpening): the one there, else one lost during the long press that opened the menu. With nothing selected the
    // marks go in at the cursor, which is left between them. Link asks for the text and the address (linkDialog).
    // ponytail: positions and lengths are taken as one unit a UTF-16 unit; if a selection holding an emoji is not wrapped, count code points.
    property int selFrom: 0
    property int selTo: 0
    property string selText: ""
    property double selLost: 0 // when that selection went (ms), 0 while it stands
    property int markFrom: 0
    property int markTo: 0
    property string markText: "" // what the menu's items wrap, "" for nothing
    function selectionMoved() {
        var e = field.editor
        var text = e.selectedText
        // The start, the end and the text change one after the other: only a state in which they agree is kept.
        if (text != "" && e.selectionEnd - e.selectionStart == text.length) {
            selFrom = e.selectionStart
            selTo = e.selectionEnd
            selText = text
            selLost = 0
        } else if (text == "" && selText != "" && selLost == 0) {
            selLost = new Date().getTime()
        }
    }
    function menuOpening() {
        selectionMoved()
        var kept = selText != "" && (selLost == 0 || new Date().getTime() - selLost < 2000)
        markFrom = kept ? selFrom : 0
        markTo = kept ? selTo : 0
        markText = kept ? selText : ""
    }
    // `words` in the place of the selection the menu was opened on, at the cursor when there was none; the cursor then
    // goes `back` before their end. Not at once: set right after the insert it stayed at the end (the user, 1.0.5.194:
    // a link's address was typed after its brackets).
    property int cursorAt: 0
    function put(words, back) {
        var e = field.editor
        var from = e.cursorPosition
        if (markText != "") {
            from = markFrom
            e.setSelection(markFrom, markTo)
        }
        e.insertPlainText(words) // takes the selection's place
        if (back > 0) {
            cursorAt = from + words.length - back
            cursorTimer.start()
        }
        markText = ""
    }
    function mark(open, close) {
        if (field.editor.selectedText != "") {
            menuOpening() // still selected: that one
        }
        put(open + markText + close, markText == "" ? close.length : 0)
    }
    // Link: the dialog starts with the selection as the link's text.
    function askLink() {
        if (field.editor.selectedText != "") {
            menuOpening()
        }
        linkText.text = markText.trim()
        linkUrl.text = ""
        linkDialog.open()
        if (linkText.text == "") {
            linkText.requestFocus()
        } else {
            linkUrl.requestFocus()
        }
    }
    // Add: "[text](address)", the address alone without a text; nothing without an address. The blanks the selection
    // began and ended with (a selected word takes the space after it) stay outside the link.
    function linkDone() {
        var label = linkText.text.trim()
        var url = linkUrl.text.trim()
        linkDialog.close()
        if (url != "") {
            put(/^\s*/.exec(markText)[0] + (label == "" ? url : "[" + label + "](" + url + ")") + /\s*$/.exec(markText)[0], 0)
        }
        markText = ""
        field.requestFocus()
    }
    onCreationCompleted: {
        field.editor.selectedTextChanged.connect(bar.selectionMoved)
        field.editor.selectionStartChanged.connect(bar.selectionMoved)
        field.editor.selectionEndChanged.connect(bar.selectionMoved)
    }
    attachedObjects: [
        Timer {
            id: cursorTimer
            interval: 100
            singleShot: true
            onTimeout: field.editor.cursorPosition = bar.cursorAt
        },
        // A link's text and address (the user, 2026-11-27: "a dialog to specify link and label"): a card at the top of a
        // dimmed page, clear of the keyboard, as TranslateDialog's.
        Dialog {
            id: linkDialog
            Container {
                layout: DockLayout {}
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    background: Color.create("#B3000000")
                    gestureHandlers: TapHandler {
                        onTapped: linkDialog.close()
                    }
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Top
                    topMargin: ui.du(4)
                    leftMargin: ui.du(2)
                    rightMargin: ui.du(2)
                    background: Color.create(nav.pageColor)
                    topPadding: ui.du(2)
                    bottomPadding: ui.du(2)
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    Label {
                        text: qsTr("Link")
                        textStyle.fontWeight: FontWeight.Bold
                    }
                    TextField {
                        id: linkText
                        hintText: qsTr("Text")
                        textStyle.base: nav.emojiStyle
                    }
                    TextField {
                        id: linkUrl
                        hintText: qsTr("Address")
                        inputMode: TextFieldInputMode.Url
                        input {
                            submitKey: SubmitKey.Done
                            onSubmitted: bar.linkDone()
                        }
                    }
                    Container {
                        topMargin: ui.du(2)
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        Button {
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                            text: qsTr("Cancel")
                            onClicked: linkDialog.close()
                        }
                        Button {
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                            text: qsTr("Add")
                            onClicked: bar.linkDone()
                        }
                    }
                }
            }
        }
    ]
    // On the Passport the row is a quarter bigger, text and icons (the user, 2026-11-14, from a
    // Passport screenshot: both looked small on its 120 du of width), and on the Classic a fifth:
    // it is the Q10's 720 x 720 at 8 px a du, not 9, which is how it is told from the Q10 here.
    // The other phones keep 1.
    // ponytail: the 72 and 81 px icons are scaled up on the Passport; draw them at 2x if they look soft.
    property real big: screenWidth >= 1440 ? 1.25
                       : screenWidth == 720 && screenHeight == 720 && ui.du(1) < 9 ? 1.2 : 1
    horizontalAlignment: HorizontalAlignment.Fill
    // A 1 px grey line on top divides the row from the list (the user, 2026-11-08).
    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        preferredHeight: ui.px(1)
        minHeight: ui.px(1)
        background: Color.Gray
    }
    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        // The stock bar's colours (docs/plan/ui.md).
        background: Color.create(settings.theme == 1 ? "#f0f0f0" : "#282828")
        layout: StackLayout {
            orientation: LayoutOrientation.LeftToRight
        }
        // Emoji, or Discard while recording.
        TapArea {
            id: emojiBtn
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(9 * bar.big)
            minHeight: ui.du(9 * bar.big)
            visible: !bar.readOnly
            layout: DockLayout {}
            focusPolicy: FocusPolicy.Key
            background: emojiBtn.focused ? Color.create(nav.pressColor) : Color.Transparent
            onClicked: bar.recording ? bar.discard() : bar.emoji()
            ImageView {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(8 * bar.big)
                preferredHeight: ui.du(8 * bar.big)
                scalingMethod: ScalingMethod.AspectFit
                imageSource: bar.recording ? "file:///usr/share/icons/bb_action_delete.png"
                                           : "asset:///images/emoji.png"
                filterColor: ui.palette.textOnPlain
            }
        }
        // The field with Attach and Send inside it, as the stock bar's black box. A channel has neither it nor Emoji,
        // only the menu, at the right (the user, 2026-11-17).
        Container {
            visible: !bar.readOnly
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            verticalAlignment: VerticalAlignment.Fill
            background: Color.create(settings.theme == 1 ? "#ffffff" : "#000000")
            topPadding: ui.du(0.5)
            bottomPadding: ui.du(0.5)
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            TextArea {
                id: field
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }
                verticalAlignment: VerticalAlignment.Center
                hintText: qsTr("Message")
                textFormat: TextFormat.Plain
                // The point of this row: the system composer couldn't take the emoji style.
                textStyle.base: nav.emojiStyle
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: 100 * bar.big
                // Our colour, not the theme's: with no field background the selection blue goes navy
                // over the black box and the theme drew the selected text dark on it (Q10, 0.0.1.232).
                // The theme's background fixed that but is grey (0.0.1.233, the user: remove it).
                textStyle.color: ui.palette.textOnPlain
                backgroundVisible: false
                focusHighlightEnabled: false
                // A tap on Send, Attach or Emoji (not focusable) leaves the focus here, so the next key
                // still goes through the input method (bb10-pitfalls).
                focusRetentionPolicyFlags: FocusRetentionPolicy.LoseToFocusable
                autoSize.maxLineCount: 5
                input {
                    submitKey: SubmitKey.Send
                    submitKeyFocusBehavior: SubmitKeyFocusBehavior.Keep
                    onSubmitted: bar.submitted()
                }
                onTextChanging: bar.changing(text)
                // In the field's long-press menu (the user, 2026-11-27); the icons are scripts/gen-format-icons.py's.
                contextMenuHandler: ContextMenuHandler {
                    onPopulating: bar.menuOpening()
                }
                contextActions: [
                    ActionSet {
                        ActionItem {
                            title: qsTr("Bold")
                            imageSource: "asset:///images/format_bold.png"
                            onTriggered: bar.mark("**", "**")
                        }
                        ActionItem {
                            title: qsTr("Italic")
                            imageSource: "asset:///images/format_italic.png"
                            onTriggered: bar.mark("__", "__")
                        }
                        ActionItem {
                            title: qsTr("Strikethrough")
                            imageSource: "asset:///images/format_strike.png"
                            onTriggered: bar.mark("~~", "~~")
                        }
                        ActionItem {
                            title: qsTr("Spoiler")
                            imageSource: "asset:///images/format_spoiler.png"
                            onTriggered: bar.mark("||", "||")
                        }
                        ActionItem {
                            title: qsTr("Monospace")
                            imageSource: "asset:///images/format_mono.png"
                            onTriggered: bar.mark("`", "`")
                        }
                        ActionItem {
                            title: qsTr("Link")
                            imageSource: "asset:///images/format_link.png"
                            onTriggered: bar.askLink()
                        }
                    }
                ]
            }
            TapArea {
                id: attachBtn
                verticalAlignment: VerticalAlignment.Bottom
                preferredWidth: ui.du(8 * bar.big)
                minHeight: ui.du(8 * bar.big)
                layout: DockLayout {}
                focusPolicy: FocusPolicy.Key
                background: attachBtn.focused ? Color.create(nav.pressColor) : Color.Transparent
                onClicked: bar.attach()
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(6 * bar.big)
                    preferredHeight: ui.du(6 * bar.big)
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/attach_action.png"
                    filterColor: ui.palette.textOnPlain
                }
            }
            TapArea {
                id: sendBtn
                verticalAlignment: VerticalAlignment.Bottom
                preferredWidth: ui.du(8 * bar.big)
                minHeight: ui.du(8 * bar.big)
                layout: DockLayout {}
                focusPolicy: FocusPolicy.Key
                background: sendBtn.focused ? Color.create(nav.pressColor) : Color.Transparent
                onClicked: bar.send()
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(6 * bar.big)
                    preferredHeight: ui.du(6 * bar.big)
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: bar.mic ? "asset:///images/record.png"
                                         : "file:///usr/share/icons/bb_action_sendtextmessage.png"
                    filterColor: ui.palette.textOnPlain
                }
            }
        }
        // A channel's Search, where Emoji is in a chat that takes messages.
        TapArea {
            id: searchBtn
            visible: bar.readOnly && bar.channel
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(9 * bar.big)
            minHeight: ui.du(9 * bar.big)
            layout: DockLayout {}
            focusPolicy: FocusPolicy.Key
            background: searchBtn.focused ? Color.create(nav.pressColor) : Color.Transparent
            onClicked: bar.searchTapped()
            ImageView {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(6 * bar.big)
                preferredHeight: ui.du(6 * bar.big)
                scalingMethod: ScalingMethod.AspectFit
                imageSource: "asset:///images/search.png"
                filterColor: ui.palette.textOnPlain
            }
        }
        // A channel's mute toggle, in the field's place: the stock Button (a pill or box of our own read as a label, the user,
        // 2026-11-25). A channel's bar has no text field, so the focus a touch gives it takes no keyboard away.
        Button {
            visible: bar.readOnly && bar.channel && !bar.join
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Center
            text: bar.muted ? qsTr("Unmute notifications") : qsTr("Mute notifications")
            onClicked: bar.muteTapped()
        }
        Button {
            visible: bar.join
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Center
            text: qsTr("Join")
            color: ui.palette.primary
            onClicked: bar.joinTapped()
        }
        // In a secret chat that can't take a message yet, or any more, it says why.
        Container {
            visible: bar.readOnly && !bar.channel && !bar.join
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            verticalAlignment: VerticalAlignment.Center
            leftPadding: ui.du(1.5)
            Label {
                visible: bar.notice != ""
                horizontalAlignment: HorizontalAlignment.Fill
                multiline: true
                text: bar.notice
                textFormat: TextFormat.Plain
                textStyle.base: nav.emojiStyle
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: 90
                textStyle.color: ui.palette.secondaryTextOnPlain
            }
        }
        // Discuss (the user's ask, 2026-11-25: on this bar, the double bubble): a channel's discussion group, where its
        // posts' comments are. Only in a channel that has one.
        TapArea {
            id: discussBtn
            visible: bar.readOnly && bar.discuss
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(9 * bar.big)
            minHeight: ui.du(9 * bar.big)
            layout: DockLayout {}
            focusPolicy: FocusPolicy.Key
            background: discussBtn.focused ? Color.create(nav.pressColor) : Color.Transparent
            onClicked: bar.discussTapped()
            ImageView {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(6 * bar.big)
                preferredHeight: ui.du(6 * bar.big)
                scalingMethod: ScalingMethod.AspectFit
                imageSource: "asset:///images/discuss.png"
                filterColor: ui.palette.textOnPlain
            }
        }
        // The action menu (Search, Pinned, info, ...); always live, a channel needs it too.
        TapArea {
            id: menuBtn
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(9 * bar.big)
            minHeight: ui.du(9 * bar.big)
            layout: DockLayout {}
            focusPolicy: FocusPolicy.Key
            background: menuBtn.focused ? Color.create(nav.pressColor) : Color.Transparent
            onClicked: bar.menu()
            ImageView {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(6 * bar.big)
                preferredHeight: ui.du(6 * bar.big)
                scalingMethod: ScalingMethod.AspectFit
                imageSource: "asset:///images/overflow_action.png"
                filterColor: ui.palette.textOnPlain
            }
        }
    }
}
