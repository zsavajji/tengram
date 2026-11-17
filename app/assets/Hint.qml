import bb.cascades 1.4
import hyperberry 1.0

// A first-time hint in the look of the BB10 lock screen's yellow tutorial hints (read off the Q10;
// the system's own are not readable by apps, so this is redrawn): a square-cornered box nearly the
// screen's width, #DCD427 at 95 % with a dark frame for a shadow, a white disc with an italic "i" in
// #66661F at its left, dark text, and white double chevrons below it pointing the gesture's way.
// The owner places it (it fills the width, so only the vertical alignment matters) and calls show(ms);
// it hides after ms, or at a tap (dismissed), or at hide(). Sizes in the Q10's px, scaled to the screen.
Container {
    id: hint
    property string text
    // The chevrons' turn in degrees: they point up as drawn, 90 points them right (a swipe from the
    // left edge), 270 left.
    property real arrowRotation: 0
    // The chevrons sit at the left edge instead of the middle.
    property bool arrowAtLeft: false
    signal dismissed
    visible: false
    horizontalAlignment: HorizontalAlignment.Fill
    function px(n) {
        return ui.px(n * screenWidth / 720)
    }
    function show(ms) {
        hider.stop()
        hider.interval = ms
        visible = true
        hider.start()
    }
    function hide() {
        hider.stop()
        visible = false
    }
    attachedObjects: Timer {
        id: hider
        singleShot: true
        onTimeout: hint.hide()
    }
    gestureHandlers: TapHandler {
        onTapped: {
            hint.hide()
            hint.dismissed()
        }
    }
    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        leftPadding: hint.px(24)
        rightPadding: hint.px(24)
        // The shadow: a darker frame, 4 px round and 2 px lower. ponytail: a soft blur would need an
        // image; add it if the Q10 shows this one as too hard.
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            leftPadding: ui.px(4)
            rightPadding: ui.px(4)
            topPadding: ui.px(2)
            bottomPadding: ui.px(6)
            background: Color.create("#33000000")
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: hint.px(24)
                bottomPadding: hint.px(24)
                leftPadding: hint.px(24)
                rightPadding: hint.px(24)
                background: Color.create("#f2dcd427")
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                ImageView {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: hint.px(48)
                    preferredHeight: hint.px(48)
                    scalingMethod: ScalingMethod.AspectFit
                    loadEffect: ImageViewLoadEffect.None
                    imageSource: "asset:///images/call_hint_info.png"
                }
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    leftMargin: hint.px(20)
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    multiline: true
                    text: hint.text
                    textFormat: TextFormat.Plain
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: 110
                    textStyle.color: Color.create("#262626")
                }
            }
        }
    }
    // The image points up; rotationZ turns it, and its square box keeps the same space when turned.
    ImageView {
        horizontalAlignment: hint.arrowAtLeft ? HorizontalAlignment.Left : HorizontalAlignment.Center
        topMargin: hint.px(8)
        preferredWidth: hint.px(110)
        preferredHeight: hint.px(110)
        rotationZ: hint.arrowRotation
        scalingMethod: ScalingMethod.AspectFit
        loadEffect: ImageViewLoadEffect.None
        imageSource: "asset:///images/call_hint_chevrons.png"
    }
}
