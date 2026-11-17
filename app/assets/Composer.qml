import bb.cascades 1.4

// The chat's own composer row (user's choice, 2026-11-08): Emoji | field | Attach | Send | ⋮. The
// system TextInputActionItem it replaces has no layout control: it grew a block taller than its
// text with the icons on their own row, Emoji couldn't go left, and it takes no text style, so
// emoji past Unicode 6 were boxes while typing. Here the field grows a line at a time (to 5) and
// Attach and Send stay on its last line, Emoji and ⋮ centred on the row.
// Enter submits whatever the page makes of it (send, or a new line) and the field keeps the focus,
// so the next message can be typed at once. Send is the touch path to sending (the Q10 has no
// on-screen submit key), and Record while the field is empty, as WhatsApp's: a tap starts, the next
// one sends. The phone's own send and delete icons are read at run time, not shipped.
Container {
    id: bar
    property alias field: field
    // Empty field (nothing staged, nobody recording): Send shows Record.
    property bool mic
    property bool recording
    // A channel (chat.kind == 3): nothing to write, only the menu.
    property bool readOnly
    signal emoji
    signal discard
    signal attach
    signal send
    signal submitted
    // Forwarded: field.text notifies only when committed (as SearchBar's field), this follows the keys.
    signal changing(string text)
    signal menu
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
        Container {
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(9 * bar.big)
            minHeight: ui.du(9 * bar.big)
            opacity: bar.readOnly ? 0.4 : 1
            layout: DockLayout {}
            gestureHandlers: TapHandler {
                onTapped: if (!bar.readOnly) bar.recording ? bar.discard() : bar.emoji()
            }
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
        // The field with Attach and Send inside it, as the stock bar's black box.
        Container {
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
                enabled: !bar.readOnly
                autoSize.maxLineCount: 5
                input {
                    submitKey: SubmitKey.Send
                    submitKeyFocusBehavior: SubmitKeyFocusBehavior.Keep
                    onSubmitted: bar.submitted()
                }
                onTextChanging: bar.changing(text)
            }
            Container {
                verticalAlignment: VerticalAlignment.Bottom
                preferredWidth: ui.du(8 * bar.big)
                minHeight: ui.du(8 * bar.big)
                opacity: bar.readOnly ? 0.4 : 1
                layout: DockLayout {}
                gestureHandlers: TapHandler {
                    onTapped: if (!bar.readOnly) bar.attach()
                }
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
            Container {
                verticalAlignment: VerticalAlignment.Bottom
                preferredWidth: ui.du(8 * bar.big)
                minHeight: ui.du(8 * bar.big)
                opacity: bar.readOnly ? 0.4 : 1
                layout: DockLayout {}
                gestureHandlers: TapHandler {
                    onTapped: if (!bar.readOnly) bar.send()
                }
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
        // The action menu (Search, Pinned, info, ...); always live, a channel needs it too.
        Container {
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(9 * bar.big)
            minHeight: ui.du(9 * bar.big)
            layout: DockLayout {}
            gestureHandlers: TapHandler {
                onTapped: bar.menu()
            }
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
