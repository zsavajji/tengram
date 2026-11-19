import bb.cascades 1.4

// A toggle of the in-call grid (step 7.4), a cell as the stock Phone app's (Q10 screenshot,
// 2026-11-07): 222 x 90 px, a white icon and its label, on the grey #565656 while pressed or on.
// iconOn and textOn, when given, replace them while on (Mute shows the mic and "Unmute").
// Stacked, it is the stock Z10's cell (240 x 140 px): the icon over its label, both centred.
// Sizes in the Q10's px, scaled by px().
// In Round mode (the user's ask, 2026-11-19) the grey is a disc behind the icon, not the whole cell, which is not a
// square: the cell's content sits in a Container of its own, over the disc.
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
    background: !settings.round && (toggle.on || toggle.pressed) ? Color.create("#565656") : Color.Transparent
    layout: DockLayout {}
    function px(n) {
        return ui.px(n * screenWidth / 720)
    }
    // Centred on the icon: at the left of the wide cell (its icon's centre is 43 px from the edge) and at the top of the
    // stacked one (44 px), where a disc of 88 px has no margin to need (a margin doesn't count from a dock's edge).
    RoundFill {
        visible: settings.round && (toggle.on || toggle.pressed)
        horizontalAlignment: toggle.stacked ? HorizontalAlignment.Center : HorizontalAlignment.Left
        verticalAlignment: toggle.stacked ? VerticalAlignment.Top : VerticalAlignment.Center
        preferredWidth: toggle.stacked ? toggle.px(88) : toggle.px(86)
        preferredHeight: toggle.stacked ? toggle.px(88) : toggle.px(86)
        filterColor: Color.create("#565656")
    }
    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        leftPadding: toggle.stacked ? 0 : toggle.px(18)
        topPadding: toggle.stacked ? toggle.px(14) : 0
        layout: StackLayout {
            orientation: toggle.stacked ? LayoutOrientation.TopToBottom : LayoutOrientation.LeftToRight
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
    }
    onTouch: toggle.pressed = event.isDown() || event.isMove()
    onTouchExit: toggle.pressed = false
    gestureHandlers: TapHandler {
        onTapped: toggle.tapped()
    }
}
