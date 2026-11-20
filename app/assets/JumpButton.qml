import bb.cascades 1.4

// One of the chat's jump buttons in the bottom-right corner (ChatPage): to the newest message, to the next unread mention
// (@), to our next message with unread reactions (the heart). Square and opaque, as the cards are; a disc in Round mode
// (the user's ask, 2026-11-19). The glyph is an image (`image`, white, tinted) or a bold character (`glyph`). `count`, when
// above 0, is a small badge on the top-right corner (the user, 2026-11-20). The Classic's trackpad reaches it: it takes
// the focus, in the pressed colour, and a click (Enter) taps it.
Container {
    id: btn
    property string image: ""
    property string glyph: ""
    property int count: 0
    signal tapped()
    preferredWidth: ui.du(11)
    preferredHeight: ui.du(11)
    topMargin: ui.du(1)
    bottomMargin: 0
    focusPolicy: FocusPolicy.KeyAndTouch
    background: settings.round ? Color.Transparent : Color.create(btn.focused ? nav.pressColor : nav.inColor)
    layout: DockLayout {
    }
    gestureHandlers: TapHandler {
        onTapped: btn.tapped()
    }
    keyListeners: KeyListener {
        onKeyReleased: {
            if (event.key == 13 || event.key == 10) { // Enter: the trackpad's click
                btn.tapped()
            }
        }
    }
    RoundFill {
        filterColor: Color.create(btn.focused ? nav.pressColor : nav.inColor)
    }
    ImageView {
        visible: btn.image != ""
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        preferredWidth: ui.du(5)
        preferredHeight: ui.du(5)
        imageSource: btn.image
        filterColor: ui.palette.textOnPlain
    }
    Label {
        visible: btn.glyph != ""
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        text: btn.glyph
        textStyle.base: SystemDefaults.TextStyles.TitleText
        textStyle.fontWeight: FontWeight.Bold
        textStyle.color: ui.palette.textOnPlain
    }
    // The badge: a pill in Round mode (pill_s.amd, as the chat list's), a square otherwise; one digit is a disc.
    Container {
        visible: btn.count > 0
        horizontalAlignment: HorizontalAlignment.Right
        verticalAlignment: VerticalAlignment.Top
        minWidth: ui.du(3.6)
        minHeight: ui.du(3.6)
        background: settings.round ? Color.Transparent : ui.palette.primary
        layout: DockLayout {
        }
        ImageView {
            visible: settings.round
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            imageSource: "asset:///images/pill_s.amd"
            scalingMethod: ScalingMethod.Fill
            loadEffect: ImageViewLoadEffect.None
            filterColor: ui.palette.primary
        }
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Center
            leftPadding: ui.du(0.6)
            rightPadding: ui.du(0.6)
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                text: btn.count > 99 ? "99+" : btn.count
                textStyle.base: SystemDefaults.TextStyles.SmallText
                textStyle.fontWeight: FontWeight.Bold
                textStyle.textAlign: TextAlign.Center
                textStyle.color: Color.White
            }
        }
    }
}
