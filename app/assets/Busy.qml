import bb.cascades 1.4

// A wait in the middle of the space it's given (user, 2026-10-31): a large activity indicator
// over a line that says what's happening. Put it in a DockLayout.
Container {
    id: busy
    property alias text: label.text
    property bool running: false
    visible: running
    horizontalAlignment: HorizontalAlignment.Fill
    verticalAlignment: VerticalAlignment.Center
    leftPadding: ui.du(4)
    rightPadding: ui.du(4)
    ActivityIndicator {
        horizontalAlignment: HorizontalAlignment.Center
        running: busy.running
        preferredWidth: ui.du(12)
        preferredHeight: ui.du(12)
    }
    Label {
        id: label
        horizontalAlignment: HorizontalAlignment.Center
        multiline: true
        textStyle.textAlign: TextAlign.Center
        textStyle.color: ui.palette.secondaryTextOnPlain
    }
}
