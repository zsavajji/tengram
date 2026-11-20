import bb.cascades 1.4

// The controls of a call that isn't ringing (step 7.4): the black panel with Speaker, Mute, Video,
// Switch camera and Hide preview (a Telegram call has no keypad, note or second call; Video turns a voice
// call into a video one), and the red End call bar. Over the call screen's 720 px stage, whose size px()
// scales. On a video call the panel is see-through, over the peer's picture. In Round mode (the user's ask, 2026-11-19) the
// End call bar is a pill (rounded ends at their 20 px radius: it is a wide bar, not a square) and the cells are discs
// behind their icons (CallToggle).
Container {
    id: panel
    property bool ended: false
    // The call has video: the panel lets the picture show through.
    property bool video: false
    // We send our video.
    property bool videoOn: false
    // Our own preview is hidden (the sheet's previewHidden).
    property bool previewHidden: false
    property bool pressed: false
    // How far a tall screen's panel grows up into the stage, from CallSheet (0 on a square screen).
    property real lift: 0
    property bool tall: lift > 0
    signal hangup
    signal videoTapped
    signal switchTapped
    signal previewTapped
    // Any control was tapped: the call screen holds off hiding them.
    signal touched
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
        background: panel.video ? Color.create("#99000000") : Color.Black
    }
    // The stock cells: 222 x 90 px at x 18, 249 and 480, from y 432. On a tall screen they are the
    // stock Z10's grid, 240 x 140 at x 0, 240 and 480, rows from y 240 and 390, with End call a
    // 696 x 181 bar at y 530, handset over label.
    CallToggle {
        stacked: panel.tall
        layoutProperties: AbsoluteLayoutProperties {
            positionX: panel.tall ? 0 : panel.px(18)
            positionY: panel.tall ? panel.px(240) : panel.px(432)
        }
        icon: "asset:///images/call_speaker.png"
        text: qsTr("Speaker")
        on: callAudio.speaker
        onTapped: {
            callAudio.speaker = !callAudio.speaker
            panel.touched()
        }
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
        onTapped: {
            callAudio.muted = !callAudio.muted
            panel.touched()
        }
    }
    // Lit while our video is off, as Mute is while muted.
    CallToggle {
        stacked: panel.tall
        layoutProperties: AbsoluteLayoutProperties {
            positionX: panel.px(480)
            positionY: panel.tall ? panel.px(240) : panel.px(432)
        }
        icon: "asset:///images/call_video.png"
        text: qsTr("Stop video")
        iconOn: "asset:///images/call_video_off.png"
        textOn: qsTr("Start video")
        on: !panel.videoOn
        onTapped: {
            panel.videoTapped()
            panel.touched()
        }
    }
    // Needs our camera: dimmed and inert while we send nothing.
    CallToggle {
        stacked: panel.tall
        layoutProperties: AbsoluteLayoutProperties {
            positionX: panel.tall ? 0 : panel.px(18)
            positionY: panel.tall ? panel.px(390) : panel.px(520)
        }
        enabled: panel.videoOn
        opacity: panel.videoOn ? 1 : 0.5
        icon: "asset:///images/call_switch.png"
        text: qsTr("Switch camera")
        onTapped: {
            panel.switchTapped()
            panel.touched()
        }
    }
    // Hides or shows our own preview; our video still goes out. Lit while hidden, as Mute is while
    // muted; dimmed and inert while we send nothing, as Switch camera.
    CallToggle {
        stacked: panel.tall
        layoutProperties: AbsoluteLayoutProperties {
            positionX: panel.tall ? panel.px(240) : panel.px(249)
            positionY: panel.tall ? panel.px(390) : panel.px(520)
        }
        enabled: panel.videoOn
        opacity: panel.videoOn ? 1 : 0.5
        icon: "asset:///images/call_preview.png"
        text: qsTr("Hide preview")
        iconOn: "asset:///images/call_preview_off.png"
        textOn: qsTr("Show preview")
        on: panel.previewHidden
        onTapped: {
            panel.previewTapped()
            panel.touched()
        }
    }
    // The End call bar. The Classic's trackpad reaches it: it takes the focus, dark red as while pressed, and a click (Enter) ends the call.
    Container {
        id: endBar
        layoutProperties: AbsoluteLayoutProperties {
            positionX: panel.tall ? panel.px(12) : panel.px(18)
            positionY: panel.tall ? panel.px(530) : panel.px(610)
        }
        preferredWidth: panel.tall ? panel.px(696) : panel.px(684)
        preferredHeight: panel.tall ? panel.px(181) : panel.px(93)
        enabled: !panel.ended
        opacity: panel.ended ? 0.5 : 1
        focusPolicy: FocusPolicy.KeyAndTouch
        background: settings.round ? Color.Transparent : panel.pressed || endBar.focused ? Color.create("#b3181a") : Color.create("#ff3334")
        layout: DockLayout {}
        RoundFill {
            pill: true
            filterColor: panel.pressed || endBar.focused ? Color.create("#b3181a") : Color.create("#ff3334")
        }
        // The content in a Container of its own, over the pill. Padding and a row, not margins in a DockLayout: margins only
        // space siblings, never from the container's edge (pitfalls.md), which left icon and label on top of each other at
        // the bar's edge on the Q10 (0.0.1.187). The stock handset at x 32 and the label at x 114.
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
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
        }
        onTouch: panel.pressed = event.isDown() || event.isMove()
        onTouchExit: panel.pressed = false
        gestureHandlers: TapHandler {
            onTapped: {
                panel.hangup()
                panel.touched()
            }
        }
        keyListeners: KeyListener {
            onKeyReleased: {
                if (event.key == 13 || event.key == 10) { // Enter: the trackpad's click
                    panel.hangup()
                    panel.touched()
                }
            }
        }
    }
}
