import bb.cascades 1.4

// A Container that is tapped: by a finger, or by the Classic's trackpad click (Enter) while it has the focus. Both are
// clicked(). The focus policy and the look of the focused state are the user's own: they differ (Key in the composer,
// so a touch leaves the keyboard up; KeyAndTouch elsewhere).
Container {
    id: area
    signal clicked()
    gestureHandlers: TapHandler {
        onTapped: area.clicked()
    }
    keyListeners: KeyListener {
        onKeyReleased: {
            if (event.key == 13 || event.key == 10) { // Enter: the trackpad's click
                area.clicked()
            }
        }
    }
}
