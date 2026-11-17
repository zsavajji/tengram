import bb.cascades 1.4

// A row that opens a web address in the browser (About's links): the text in the primary colour,
// lit while pressed.
Container {
    id: row
    property string text
    property string url
    property bool pressed: false
    horizontalAlignment: HorizontalAlignment.Fill
    background: pressed ? Color.create(nav.pressColor) : Color.Transparent
    topPadding: ui.du(1.5)
    leftPadding: ui.du(1.5)
    rightPadding: ui.du(1.5)
    bottomPadding: ui.du(1.5)
    attachedObjects: Invocation {
        id: browser
        query {
            invokeTargetId: "sys.browser"
            invokeActionId: "bb.action.OPEN"
            uri: row.url
            onQueryChanged: browser.query.updateQuery()
        }
    }
    Label {
        multiline: true
        text: row.text
        textFormat: TextFormat.Plain
        textStyle.fontSize: FontSize.PercentageValue
        textStyle.fontSizeValue: nav.bodySize
        textStyle.color: ui.palette.primary
    }
    gestureHandlers: TapHandler {
        onTapped: browser.trigger("bb.action.OPEN")
    }
    onTouch: row.pressed = event.isDown() || event.isMove()
    onTouchExit: row.pressed = false
}
