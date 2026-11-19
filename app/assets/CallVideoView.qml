import bb.cascades 1.4

// The video of a call, over the call screen's stage and under its controls (CallPanel): the peer's
// picture fills the sheet, our own camera sits in a corner, and a dark band across the top carries the
// name and the clock while the controls show. A tap anywhere in it, the picture included, shows or hides
// the controls. The two native windows are CallVideo's (callVideo.start takes the ids below); they sit
// under Cascades' hole, so this view and every ancestor of the controls must have no background of
// their own, or the hole is painted over (HyperBerry 0.0.1.190). Sizes in the Q10's px, scaled by
// sheet.px(); the picture's in the frame's own px. HyperBerry's version had a grid of up to four
// pictures for group calls; Telegram's calls are 1:1, so this is its 1:1 form (HyperBerry 230674d).
Container {
    id: view
    // The window id of the peer's picture, in a list as callVideo.start takes it (the first is used).
    property variant remoteIds: ["tengramCallVideo0"]
    property alias finderId: finder.windowId
    horizontalAlignment: HorizontalAlignment.Fill
    verticalAlignment: VerticalAlignment.Fill
    // Off while it rings, so the ringing controls under it get their touches; else on even for a
    // voice call, so the windows are bound when video starts.
    visible: !sheet.ringing
    layout: DockLayout {}
    // The children's taps reach this one, and the controls aren't in here.
    gestureHandlers: TapHandler {
        onTapped: sheet.toggleControls()
    }
    // The peer's picture fills the frame: the control is the frame's size and CallVideo crops the
    // picture to its aspect (Screen's source viewport). A control bigger than the frame was squeezed
    // into it by Cascades, not cropped (HyperBerry 0.0.1.203). ponytail: fill crops; fit would letterbox.
    ForeignWindowControl {
        id: remote
        windowId: view.remoteIds[0]
        updatedProperties: WindowProperty.Size | WindowProperty.Position | WindowProperty.Visible
        visible: sheet.showPicture
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        attachedObjects: LayoutUpdateHandler {
            onLayoutFrameChanged: {
                if (layoutFrame.width > 0 && layoutFrame.height > 0) {
                    callVideo.setViewSize(layoutFrame.width, layoutFrame.height)
                }
            }
        }
    }
    // The name and the clock, as the stock app's video call: white on a dark band across the top.
    Container {
        visible: sheet.isVideo && sheet.controlsShown
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Top
        preferredWidth: sheet.px(720)
        preferredHeight: sheet.px(260)
        layout: AbsoluteLayout {}
        // The fade is dark at its foot: turned over, at the top.
        ImageView {
            rotationZ: 180
            preferredWidth: sheet.px(720)
            preferredHeight: sheet.px(260)
            scalingMethod: ScalingMethod.AspectFit
            loadEffect: ImageViewLoadEffect.None
            imageSource: "asset:///images/call_fade.png"
        }
        // The name's centre at y 135.
        Container {
            layoutProperties: AbsoluteLayoutProperties {
                positionY: sheet.px(95)
            }
            preferredWidth: sheet.px(720)
            preferredHeight: sheet.px(80)
            layout: DockLayout {}
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: sheet.px(640)
                multiline: false
                text: sheet.who.name || qsTr("Unknown")
                textFormat: TextFormat.Plain
                textStyle.base: nav.emojiStyle
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: 150
                textStyle.textAlign: TextAlign.Center
                textStyle.color: Color.White
            }
        }
        // The clock, else the call's state; centre at y 205.
        Container {
            layoutProperties: AbsoluteLayoutProperties {
                positionY: sheet.px(175)
            }
            preferredWidth: sheet.px(720)
            preferredHeight: sheet.px(60)
            layout: DockLayout {}
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: sheet.px(640)
                multiline: false
                text: sheet.timed ? sheet.clock : sheet.line
                textFormat: TextFormat.Plain
                textStyle.textAlign: TextAlign.Center
                textStyle.color: Color.White
            }
        }
    }
    // Our camera, small and in the lower right, a plain rectangle (no rounded corners); the camera
    // library makes its window. It sits just above the panel (y 415 on the stage) while the controls
    // show, and drops to the foot when they hide. The stage is the Q10's 720 px, kept to the bottom as
    // the sheet's.
    Container {
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Bottom
        preferredWidth: sheet.px(720)
        preferredHeight: sheet.px(720)
        layout: AbsoluteLayout {}
        ForeignWindowControl {
            id: finder
            windowId: "tengramCallFinder"
            updatedProperties: WindowProperty.Size | WindowProperty.Position | WindowProperty.Visible
            // Hidden at the ear: the window is above Cascades, so the sheet's black can't cover it.
            visible: sheet.isVideo && !!sheet.call.videoOurs && !sheet.atEar && !sheet.previewHidden
            layoutProperties: AbsoluteLayoutProperties {
                positionX: sheet.px(502)
                positionY: sheet.controlsShown ? sheet.px(248) : sheet.px(552)
            }
            preferredWidth: sheet.px(200)
            // 4:3, or 16:9 while our camera runs 1280x720 (the window takes the control's size: a wrong shape squashes us).
            preferredHeight: callVideo.wide ? sheet.px(113) : sheet.px(150)
        }
    }
}
