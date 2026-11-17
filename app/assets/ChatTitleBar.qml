import bb.cascades 1.4
import bb.system 1.2

// The chat page's title bar (PLAN.md §7, step 3.1), moved out of ChatPage to make room: Back (the
// page's action bar is hidden, the composer is ours), the tile, name and status line. chat is a
// ChatListModel row; a tap on the name or the picture emits tapped(), on the arrow back().
TitleBar {
    id: bar
    property variant chat
    property string status
    // Fetched while open: the chat keeps the row it opened with, so the picture is read from the table as the name is.
    property string photo: bar.chat ? (chats.revision >= 0 && chats.chat(bar.chat.jid).photo) || bar.chat.photo || "" : ""
    property bool hasPhoto: photo != ""
    // How many are in the group, us included (ChatPage reads it from the group info); 0 until known.
    property int members: 0
    // A call button (step 7.4) for a person's chat, not our own, and (step 7d.G4) for a group of 3
    // or more (a group of two has no call); channels and broadcasts are other kinds. The user part
    // of our JID (before ':' and '@') against the chat's.
    property bool canCall: !!bar.chat && engine.linked
        && ((bar.chat.kind == 0 && String(engine.link.me || "").split("@")[0].split(":")[0] != bar.chat.jid.split("@")[0])
            || (bar.chat.kind == 1 && bar.members >= 3))
    signal tapped
    signal back
    // The call button on a group: the page picks who to ring.
    signal groupCall
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
                    text: bar.chat ? (chats.revision >= 0 && chats.chat(bar.chat.jid).name) || bar.chat.name : ""
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
            // The call button, outside the two parts that open the info page: on a person it asks audio
            // or video in a dialog of buttons (the user's ask, 2026-11-11); on a group it emits
            // groupCall (audio only). call.start's failure is worded by main.qml's toast.
            Container {
                visible: bar.canCall
                verticalAlignment: VerticalAlignment.Center
                leftPadding: ui.du(1.5)
                rightPadding: ui.du(1.5)
                minHeight: ui.px(81)
                layout: DockLayout {}
                gestureHandlers: TapHandler {
                    onTapped: {
                        if (bar.chat.kind == 1) {
                            bar.groupCall()
                        } else {
                            callKind.show()
                        }
                    }
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
