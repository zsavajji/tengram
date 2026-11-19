import bb.cascades 1.4

// The controls of a call that isn't ringing (step 7.4): the black panel with Speaker and Mute (a Telegram
// call has no keypad, note or second call), and the red End call bar. Over the call screen's 720 px stage,
// whose size px() scales.
Container {
    id: panel
    property bool ended: false
    property bool pressed: false
    // How far a tall screen's panel grows up into the stage, from CallSheet (0 on a square screen).
    property real lift: 0
    property bool tall: lift > 0
    signal hangup
    preferredWidth: px(720)
    preferredHeight: px(720)
    layout: AbsoluteLayout {}
    function px(n) {
        return ui.px(n * screenWidth / 720)
    }
    Container {
        layoutProperties: AbsoluteLayoutProperties {
            positionY: panel.tall ? panel.px(223) : panel.px(415)
        }
        preferredWidth: panel.px(720)
        preferredHeight: panel.tall ? panel.px(497) : panel.px(305)
        background: Color.Black
    }
    // The stock cells: 222 x 90 px at x 18 and 249, from y 432. On a tall screen they are the
    // stock Z10's grid, 240 x 140 at x 0 and 240, from y 240, with End call a 696 x 181 bar at y 530,
    // handset over label.
    CallToggle {
        stacked: panel.tall
        layoutProperties: AbsoluteLayoutProperties {
            positionX: panel.tall ? 0 : panel.px(18)
            positionY: panel.tall ? panel.px(240) : panel.px(432)
        }
        icon: "asset:///images/call_speaker.png"
        text: qsTr("Speaker")
        on: callAudio.speaker
        onTapped: callAudio.speaker = !callAudio.speaker
    }
    CallToggle {
        stacked: panel.tall
        layoutProperties: AbsoluteLayoutProperties {
            positionX: panel.tall ? panel.px(240) : panel.px(249)
            positionY: panel.tall ? panel.px(240) : panel.px(432)
        }
        icon: "asset:///images/call_mute.png"
        text: qsTr("Mute")
        iconOn: "asset:///images/call_unmute.png"
        textOn: qsTr("Unmute")
        on: callAudio.muted
        onTapped: callAudio.muted = !callAudio.muted
    }
    Container {
        layoutProperties: AbsoluteLayoutProperties {
            positionX: panel.tall ? panel.px(12) : panel.px(18)
            positionY: panel.tall ? panel.px(530) : panel.px(610)
        }
        preferredWidth: panel.tall ? panel.px(696) : panel.px(684)
        preferredHeight: panel.tall ? panel.px(181) : panel.px(93)
        enabled: !panel.ended
        opacity: panel.ended ? 0.5 : 1
        background: panel.pressed ? Color.create("#b3181a") : Color.create("#ff3334")
        // Padding and a row, not margins in a DockLayout: margins only space siblings, never from
        // the container's edge (pitfalls.md), which left icon and label on top of each other at the
        // bar's edge on the Q10 (0.0.1.187). The stock handset at x 32 and the label at x 114.
        leftPadding: panel.tall ? 0 : panel.px(14)
        topPadding: panel.tall ? panel.px(48) : 0
        layout: StackLayout {
            orientation: panel.tall ? LayoutOrientation.TopToBottom : LayoutOrientation.LeftToRight
        }
        ImageView {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: panel.px(62)
            preferredHeight: panel.px(22)
            scalingMethod: ScalingMethod.AspectFit
            imageSource: "asset:///images/call_end.png"
        }
        Label {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            leftMargin: panel.tall ? 0 : panel.px(20)
            topMargin: panel.tall ? panel.px(24) : 0
            text: qsTr("End call")
            textFormat: TextFormat.Plain
            textStyle.fontSize: FontSize.PercentageValue
            textStyle.fontSizeValue: 80
            textStyle.color: Color.White
        }
        onTouch: panel.pressed = event.isDown() || event.isMove()
        onTouchExit: panel.pressed = false
        gestureHandlers: TapHandler {
            onTapped: panel.hangup()
        }
    }
}
