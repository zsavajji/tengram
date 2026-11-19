import bb.cascades 1.4

// A toggle of the in-call grid (step 7.4), a cell as the stock Phone app's (Q10 screenshot,
// 2026-11-07): 222 x 90 px, a white icon and its label, on the grey #565656 while pressed or on.
// iconOn and textOn, when given, replace them while on (Mute shows the mic and "Unmute").
// Stacked, it is the stock Z10's cell (240 x 140 px): the icon over its label, both centred.
// Sizes in the Q10's px, scaled by px().
Container {
    id: toggle
    property string icon
    property string text
    property string iconOn: icon
    property string textOn: text
    property bool on: false
    property bool pressed: false
    // The stock Z10's cell: icon over its label, centred.
    property bool stacked: false
    signal tapped
    preferredWidth: stacked ? px(240) : px(222)
    preferredHeight: stacked ? px(140) : px(90)
    leftPadding: stacked ? 0 : px(18)
    topPadding: stacked ? px(14) : 0
    background: toggle.on || toggle.pressed ? Color.create("#565656") : Color.Transparent
    layout: StackLayout {
        orientation: toggle.stacked ? LayoutOrientation.TopToBottom : LayoutOrientation.LeftToRight
    }
    function px(n) {
        return ui.px(n * screenWidth / 720)
    }
    ImageView {
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        preferredWidth: toggle.stacked ? toggle.px(60) : toggle.px(50)
        preferredHeight: toggle.stacked ? toggle.px(60) : toggle.px(50)
        scalingMethod: ScalingMethod.AspectFit
        imageSource: toggle.on ? toggle.iconOn : toggle.icon
    }
    Label {
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        leftMargin: toggle.stacked ? 0 : toggle.px(20)
        topMargin: toggle.stacked ? toggle.px(10) : 0
        preferredWidth: toggle.stacked ? toggle.px(230) : toggle.px(134)
        multiline: true
        text: toggle.on ? toggle.textOn : toggle.text
        textFormat: TextFormat.Plain
        textStyle.fontSize: FontSize.PercentageValue
        textStyle.fontSizeValue: 80
        textStyle.textAlign: toggle.stacked ? TextAlign.Center : TextAlign.Default
        textStyle.color: Color.create("#e8e8e8")
    }
    onTouch: toggle.pressed = event.isDown() || event.isMove()
    onTouchExit: toggle.pressed = false
    gestureHandlers: TapHandler {
        onTapped: toggle.tapped()
    }
}
