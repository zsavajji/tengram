import bb.cascades 1.4
import bb.system 1.2

// The chat page's title bar (PLAN.md §7, step 3.1), moved out of ChatPage to make room: Back (the
// page's action bar is hidden, the composer is ours), the tile, name and status line. chat is a
// ChatListModel row; a tap on the name or the picture emits tapped(), on the arrow back().
TitleBar {
    id: bar
    property variant chat
    property string status
    // The chat's secret state (ChatPage.secret): 0 for no secret chat, else its lock and green name show.
    property int secret: 0
    // A forum topic's name, "" for a chat: the page is that topic's, and the status line is the group's name.
    property string topicName
    // Fetched while open: the chat keeps the row it opened with, so the picture is read from the table as the name is.
    property string photo: bar.chat ? (chats.revision >= 0 && chats.chat(bar.chat.jid).photo) || bar.chat.photo || "" : ""
    property bool hasPhoto: photo != ""
    // How many are in the group, us included (ChatPage reads it from the group info); 0 until known.
    property int members: 0
    // A call button (step 7.4) for a person's chat, not our own: Saved Messages has our user id (link.me) for its id. A
    // secret chat is a person's too, and calls them.
    property bool canCall: !!bar.chat && engine.linked && !!engine.link.me && bar.chat.kind == 0
        && String(engine.link.me) != String(bar.chat.jid)
    signal tapped
    signal back
    kind: TitleBarKind.FreeForm
    // The status line must stay; the default let the bar scroll away with the list (Q10).
    scrollBehavior: TitleBarScrollBehavior.Sticky
    // An 85 px bar (the stock Text Messages conversation's, Q10, 2026-10-31), the picture 69 px square at the right,
    // 8 px from the bar's top, bottom and the screen's edge (the user, 2026-11-19: at 81 px, 2 px from the edges, it
    // touched them).
    kindProperties: FreeFormTitleBarKindProperties {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            // Telegram's title bar colour, with white text on it (the user's ask, 2026-11-18).
            background: Color.create(nav.headerColor)
            rightPadding: ui.px(8)
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
                    filterColor: Color.White
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
                // A secret chat's name has its lock before it, both in the secret green (the chat list's).
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    ImageView {
                        visible: bar.secret > 0
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: 0
                        rightMargin: ui.du(0.5)
                        preferredWidth: ui.du(2.4)
                        preferredHeight: ui.du(2.4)
                        scalingMethod: ScalingMethod.AspectFit
                        imageSource: "asset:///images/lock.png"
                        filterColor: Color.create(nav.headerSecret)
                    }
                    Label {
                        bottomMargin: 0
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        // Saved in the address book while open: the chat keeps the row it opened with.
                        text: bar.topicName != "" ? bar.topicName
                            : bar.chat ? (chats.revision >= 0 && chats.chat(bar.chat.jid).name) || bar.chat.name : ""
                        textFormat: TextFormat.Plain
                        textStyle.base: nav.emojiStyle
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: 110
                        textStyle.color: bar.secret > 0 ? Color.create(nav.headerSecret) : Color.White
                    }
                }
                Label {
                    visible: bar.status != ""
                    topMargin: 0
                    text: bar.status
                    textFormat: TextFormat.Plain
                    textStyle.base: nav.emojiStyle
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: 75
                    textStyle.color: Color.create(nav.headerQuiet)
                }
            }
            // The call button, outside the two parts that open the info page: it asks audio or video in a dialog of
            // buttons (the user's ask, 2026-11-19). call.start's failure is worded by main.qml's toast.
            Container {
                visible: bar.canCall
                verticalAlignment: VerticalAlignment.Center
                leftPadding: ui.du(1.5)
                rightPadding: ui.du(1.5)
                minHeight: ui.px(81)
                layout: DockLayout {}
                gestureHandlers: TapHandler {
                    onTapped: callKind.show()
                }
                attachedObjects: SystemDialog {
                    id: callKind
                    title: qsTr("Call")
                    confirmButton.label: qsTr("Audio only")
                    customButton.label: qsTr("Video")
                    cancelButton.label: qsTr("Cancel")
                    onFinished: {
                        if (value == SystemUiResult.ConfirmButtonSelection) {
                            engine.request("call.start", {"chat": bar.chat.jid})
                        } else if (value == SystemUiResult.CustomButtonSelection) {
                            engine.request("call.start", {"chat": bar.chat.jid, "video": true})
                        }
                    }
                }
                // The stock white handset, tinted with the primary colour as the bar's other live
                // marks are; 45 px in an 81 px bar.
                ImageView {
                    verticalAlignment: VerticalAlignment.Center
                    imageSource: "file:///usr/share/icons/ic_phone.png"
                    filterColor: ui.palette.primary
                    preferredWidth: ui.du(5)
                    preferredHeight: ui.du(5)
                    scalingMethod: ScalingMethod.AspectFit
                }
            }
            // The chat's picture (chat.photo, the engine's preview file) where the stock app has
            // it, over the initials tile; a disc in Round mode (Tile.qml).
            Tile {
                verticalAlignment: VerticalAlignment.Center
                leftMargin: ui.du(1.5)
                preferredWidth: ui.px(69)
                preferredHeight: ui.px(69)
                tint: bar.chat ? bar.chat.color : ""
                initials: bar.chat ? bar.chat.initials : ""
                photo: bar.photo
                emojiStyle: nav.emojiStyle
                initialsSize: 150
                gestureHandlers: TapHandler {
                    onTapped: bar.tapped()
                }
            }
        }
    }
}
