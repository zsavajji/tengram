import bb.cascades 1.4

// A tappable line of a settings page (Privacy and security's): the title, a dimmer line under it when `subtitle` has
// text, a 2 px separator, lit while pressed. A tap is the `tapped` signal; the page says what it does. The Classic's trackpad
// reaches it too (the user, 2026-11-20: it couldn't): it takes the focus, is lit while it has it, and a click (Enter) taps it.
TapArea {
    id: row
    property string title
    property string subtitle
    property bool pressed: false
    signal tapped
    horizontalAlignment: HorizontalAlignment.Fill
    focusPolicy: FocusPolicy.KeyAndTouch
    background: pressed || focused ? Color.create(nav.pressColor) : Color.Transparent
    onClicked: row.tapped()
    onTouch: row.pressed = event.isDown() || event.isMove()
    onTouchExit: row.pressed = false
    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        topPadding: ui.du(1.2)
        bottomPadding: ui.du(1.2)
        leftPadding: ui.du(1.5)
        rightPadding: ui.du(1.5)
        Label {
            text: row.title
            textFormat: TextFormat.Plain
            multiline: true
            topMargin: 0
            bottomMargin: 0
        }
        Label {
            visible: row.subtitle != ""
            text: row.subtitle
            textFormat: TextFormat.Plain
            multiline: true
            topMargin: 0
            bottomMargin: 0
            textStyle.base: SystemDefaults.TextStyles.SubtitleText
            textStyle.color: ui.palette.secondaryTextOnPlain
        }
    }
    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        preferredHeight: ui.px(2)
        background: ui.palette.plain
    }
}
