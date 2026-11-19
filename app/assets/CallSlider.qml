import bb.cascades 1.4

// The slide-to-answer bar of the ringing call (step 7.4), as the stock Phone app's: the knob is
// dragged to the handset on the left (in the primary colour) to answer, to the red one on the right to
// decline, and slides back to the middle if let go short of either. The track (its outline, the red
// handset, the chevrons) is one image. Measured in the Q10's 720 px; px() scales them.
Container {
    id: slider
    signal answered
    signal declined
    // Where the finger went down, in window px: the knob follows its distance from here, not the
    // finger's place on the knob, so it doesn't jump.
    property real startX: 0
    // The knob's offset while dragged, kept here rather than read back from translationX.
    property real at: 0
    property bool dragging: false
    // The knob's centre to an end's icon is 260 px; it counts within 40 px of it.
    property real reach: px(260)
    property real grab: px(40)
    preferredWidth: px(665)
    preferredHeight: px(148)
    layout: AbsoluteLayout {}
    function px(n) {
        return ui.px(n * screenWidth / 720)
    }
    ImageView {
        preferredWidth: slider.px(665)
        preferredHeight: slider.px(148)
        scalingMethod: ScalingMethod.AspectFit
        imageSource: "asset:///images/call_track.png"
    }
    // The phone's own handset, tinted with the primary colour (the track holds only the red one); 66 px centred
    // on the left end's icon.
    ImageView {
        layoutProperties: AbsoluteLayoutProperties {
            positionX: slider.px(40)
            positionY: slider.px(40)
        }
        preferredWidth: slider.px(66)
        preferredHeight: slider.px(66)
        scalingMethod: ScalingMethod.AspectFit
        imageSource: "file:///usr/share/icons/ic_phone.png"
        filterColor: ui.palette.primary // the accent, the user's ask (2026-11-07)
    }
    ImageView {
        id: knob
        layoutProperties: AbsoluteLayoutProperties {
            positionX: slider.px(278)
            positionY: slider.px(17)
        }
        preferredWidth: slider.px(110)
        preferredHeight: slider.px(110)
        scalingMethod: ScalingMethod.AspectFit
        imageSource: "asset:///images/call_knob.png"
        // No implicit animation of translationX, which would leave the knob trailing the finger;
        // the slide back is the explicit one below.
        attachedObjects: ImplicitAnimationController {
            propertyName: "translationX"
            enabled: false
        }
        animations: TranslateTransition {
            id: back
            toX: 0
            duration: 150
        }
    }
    // The whole track takes the drag, not the knob: on the Q10 (0.0.1.184) the knob alone locked
    // under the finger. Touches on the images bubble up to here, wherever on the track the finger
    // goes. The knob follows the finger's distance from where it went down; the 220 px needed to
    // reach an end are the guard against a brush in the pocket.
    onTouch: {
        if (event.isDown()) {
            dragging = true
            back.stop()
            startX = event.windowX
            console.log("call slider: down")
        } else if (event.isMove() && dragging) {
            at = Math.max(-reach, Math.min(reach, event.windowX - startX))
            knob.translationX = at
            // An end counts as soon as the knob reaches it, not on release: on the Q10 (0.0.1.185)
            // every slide ended in a cancel, never a release, so nothing was ever answered.
            if (at <= -(reach - grab)) {
                console.log("call slider: answered")
                letGo()
                answered()
            } else if (at >= reach - grab) {
                console.log("call slider: declined")
                letGo()
                declined()
            }
        } else if ((event.isUp() || event.isCancel()) && dragging) {
            console.log("call slider: let go at " + Math.round(at) + (event.isCancel() ? " (cancelled)" : ""))
            letGo()
        }
    }
    // The drag ends and the knob slides back to the middle.
    function letGo() {
        dragging = false
        at = 0
        back.play()
    }
}
