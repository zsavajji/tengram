import bb.cascades 1.4
import bb.system 1.2

// The chat page's title bar (PLAN.md §7, step 3.1), moved out of ChatPage to make room: Back (the
// page's action bar is hidden, the composer is ours), the tile, name and status line. chat is a
// ChatListModel row; a tap on the name or the picture emits tapped(), on the arrow back().
TitleBar {
    id: bar
    property variant chat
    property string status
    // A forum topic's name, "" for a chat: the page is that topic's, and the status line is the group's name.
    property string topicName
    // Fetched while open: the chat keeps the row it opened with, so the picture is read from the table as the name is.
    property string photo: bar.chat ? (chats.revision >= 0 && chats.chat(bar.chat.jid).photo) || bar.chat.photo || "" : ""
    property bool hasPhoto: photo != ""
    // How many are in the group, us included (ChatPage reads it from the group info); 0 until known.
    property int members: 0
    signal tapped
    signal back
    kind: TitleBarKind.FreeForm
    // The status line must stay; the default let the bar scroll away with the list (Q10).
    scrollBehavior: TitleBarScrollBehavior.Sticky
    // Sized as the stock Text Messages conversation (Q10, 2026-10-31): an 85 px bar, the
    // picture 81 px square flush at the right, 2 px from the bar's edges and 7 px from the
    // screen's.
    kindProperties: FreeFormTitleBarKindProperties {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            rightPadding: ui.px(7)
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            // Back: the stock arrow's thin "<", in a box the bar's height that carries the margin.
            Container {
                verticalAlignment: VerticalAlignment.Fill
                preferredWidth: ui.du(7)
                layout: DockLayout {}
                gestureHandlers: TapHandler {
                    onTapped: bar.back()
                }
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    imageSource: "asset:///images/back_action.png"
                    preferredWidth: ui.du(6)
                    preferredHeight: ui.du(6)
                    scalingMethod: ScalingMethod.AspectFit
                    filterColor: ui.palette.textOnPlain
                }
            }
            // The info page's tap is on the name and on the picture, not on the whole bar: a tap on
            // the call button reached the bar's handler too, and the contact page came up before
            // every call (Q10, 0.0.1.188). A TapEvent has no target to tell them apart by.
            Container {
                verticalAlignment: VerticalAlignment.Center
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }
                gestureHandlers: TapHandler {
                    onTapped: bar.tapped()
                }
                // Two lines fit 85 px only without the labels' default margins (about 20 px),
                // and below the stock title's 25 px capitals (125 %; 100 % draws 20 px):
                // the name 22 px (110 %), the status 15 px (75 %). Q10, 2026-10-31.
                Label {
                    bottomMargin: 0
                    // Saved in the address book while open: the chat keeps the row it opened with.
                    text: bar.topicName != "" ? bar.topicName
                        : bar.chat ? (chats.revision >= 0 && chats.chat(bar.chat.jid).name) || bar.chat.name : ""
                    textFormat: TextFormat.Plain
                    textStyle.base: nav.emojiStyle
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: 110
                }
                Label {
                    visible: bar.status != ""
                    topMargin: 0
                    text: bar.status
                    textFormat: TextFormat.Plain
                    textStyle.base: nav.emojiStyle
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: 75
                    textStyle.color: ui.palette.secondaryTextOnPlain
                }
            }
            // The chat's picture (chat.photo, the engine's preview file) where the stock app has
            // it, over the initials tile.
            Container {
                verticalAlignment: VerticalAlignment.Center
                leftMargin: ui.du(1.5)
                preferredWidth: ui.px(81)
                preferredHeight: ui.px(81)
                background: bar.chat ? Color.create(bar.chat.color) : Color.Transparent
                layout: DockLayout {}
                gestureHandlers: TapHandler {
                    onTapped: bar.tapped()
                }
                ImageView {
                    visible: bar.hasPhoto
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    imageSource: bar.hasPhoto ? "file://" + bar.photo : ""
                    scalingMethod: ScalingMethod.AspectFill
                    loadEffect: ImageViewLoadEffect.None
                }
                Label {
                    visible: !bar.hasPhoto
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    text: bar.chat ? bar.chat.initials : ""
                    textFormat: TextFormat.Plain
                    textStyle.base: nav.emojiStyle
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: 150
                    textStyle.color: Color.White
                }
            }
        }
    }
}
