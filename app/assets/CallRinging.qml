import bb.cascades 1.4

// The controls of a ringing call (step 7.4): on a video call, whether to answer with our video (the
// user's idea, 2026-11-07, where the stock app's silence button was); answer with a message (which
// declines); and the slide bar. Over the call screen's 720 px stage, whose size px() scales.
Container {
    id: ringing
    // The call offers video; answering sends ours while withVideo (the sheet sets it on as offered).
    property bool video: false
    property bool withVideo: true
    signal message
    signal answered
    signal declined
    preferredWidth: px(720)
    preferredHeight: px(720)
    layout: AbsoluteLayout {}
    function px(n) {
        return ui.px(n * screenWidth / 720)
    }
    // A square button: its box and the white glyph on it.
    Container {
        layoutProperties: AbsoluteLayoutProperties {
            positionX: ringing.px(308)
            positionY: ringing.px(419)
        }
        visible: ringing.video
        layout: DockLayout {}
        ImageView {
            preferredWidth: ringing.px(104)
            preferredHeight: ringing.px(104)
            scalingMethod: ScalingMethod.AspectFit
            imageSource: "asset:///images/call_btn.png"
        }
        ImageView {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ringing.px(50)
            preferredHeight: ringing.px(50)
            scalingMethod: ScalingMethod.AspectFit
            imageSource: ringing.withVideo ? "asset:///images/call_video.png" : "asset:///images/call_video_off.png"
        }
        gestureHandlers: TapHandler {
            onTapped: ringing.withVideo = !ringing.withVideo
        }
    }
    // The message button, a square as well.
    Container {
        layoutProperties: AbsoluteLayoutProperties {
            positionX: ringing.px(588)
            positionY: ringing.px(419)
        }
        layout: DockLayout {}
        ImageView {
            preferredWidth: ringing.px(104)
            preferredHeight: ringing.px(104)
            scalingMethod: ScalingMethod.AspectFit
            imageSource: "asset:///images/call_btn.png"
        }
        ImageView {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ringing.px(50)
            preferredHeight: ringing.px(50)
            scalingMethod: ScalingMethod.AspectFit
            imageSource: "asset:///images/call_message.png"
        }
        gestureHandlers: TapHandler {
            onTapped: ringing.message()
        }
    }
    CallSlider {
        layoutProperties: AbsoluteLayoutProperties {
            positionX: ringing.px(27)
            positionY: ringing.px(554)
        }
        onAnswered: ringing.answered()
        onDeclined: ringing.declined()
    }
}
