import bb.cascades 1.4

// The Classic's trackpad: up and down moves scroll `list` the swipe's way, as its focus moves, consumed so the focus stays
// where it is (the user, 2026-11-25). The Passport's keyboard sends other events (KeyboardScroll.qml). A list whose
// layout runs bottom to top (the chat's) says so.
TrackpadHandler {
    property variant list
    property bool on: true
    property bool bottomToTop: false
    consumeTrackpadEvents: on ? ConsumeTrackpadEvent.Vertical : ConsumeTrackpadEvent.None
    onTrackpad: {
        if (on && event.isMove() && Math.abs(event.deltaY) > Math.abs(event.deltaX)) {
            // ponytail: gain 3 px per unit is a guess, tune it on the Classic.
            list.scroll(event.deltaY * 3 * (bottomToTop ? -1 : 1), ScrollAnimation.None)
        }
    }
}
