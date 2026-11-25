import bb.cascades 1.4

// The Passport's touch keyboard: an up or down swipe on the keys scrolls `list`, the content following the finger as on
// the screen (the user, 2026-11-25). It sends TouchKeyboardEvents, not trackpad ones, from the focused control up to the
// page's root, so the handler sits on that control or around it. A key's own touch moves less than the slop and scrolls
// nothing; a sideways swipe (the keyboard's delete-word) neither. A list whose layout runs bottom to top (the chat's)
// says so.
TouchKeyboardHandler {
    property variant list
    property bool on: true
    property bool bottomToTop: false
    property real lastX
    property real lastY
    property bool moving: false
    onTouch: {
        if (event.touchType == TouchType.Down) {
            lastX = event.screenX
            lastY = event.screenY
            moving = false
            return
        }
        if (!on || event.touchType != TouchType.Move) {
            return
        }
        var dy = lastY - event.screenY // above 0: the finger went up
        if (!moving) {
            if (Math.abs(dy) < 20 || Math.abs(dy) < Math.abs(lastX - event.screenX)) {
                return
            }
            moving = true
        }
        lastY = event.screenY
        // ponytail: gain 3 is a guess, tune it on the Passport.
        list.scroll(dy * 3 * (bottomToTop ? -1 : 1), ScrollAnimation.None)
    }
}
