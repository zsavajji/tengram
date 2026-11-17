import bb.cascades 1.4

// The file picked to send, above the composer (step 6.4), in the reply bar's style: a small
// picture or a paperclip, the name over the size, and a cross that drops it. info is
// MediaOpener.fileInfo's; what is typed in the composer is the file's caption.
Container {
    id: bar
    property variant info
    // The picture is a PNG made a JPEG: the bar offers to send the PNG as a file instead.
    property bool canFile
    property bool asFile: false
    // The file is a picture the system's editor can open: the bar offers Edit, and the page restages the result.
    property bool canEdit
    signal edit()
    signal cancelled()
    visible: !!info && !!info.name
    horizontalAlignment: HorizontalAlignment.Fill
    background: ui.palette.plain
    leftPadding: ui.du(1.5)
    rightPadding: ui.du(1.5)
    topPadding: ui.du(1)
    bottomPadding: ui.du(1)
    layout: StackLayout {
        orientation: LayoutOrientation.LeftToRight
    }
    // The picture, for a .jpg; set in onInfoChanged so the Image is never assigned undefined.
    ImageView {
        id: thumb
        visible: !!info && !!info.image
        verticalAlignment: VerticalAlignment.Center
        preferredWidth: ui.du(7)
        preferredHeight: ui.du(7)
        minWidth: ui.du(7)
        minHeight: ui.du(7)
        rightMargin: ui.du(1)
        scalingMethod: ScalingMethod.AspectFill
        loadEffect: ImageViewLoadEffect.None
    }
    onInfoChanged: {
        if (info && info.image) {
            thumb.image = info.image
        }
    }
    Label {
        visible: !thumb.visible
        verticalAlignment: VerticalAlignment.Center
        rightMargin: ui.du(1)
        // U+1F4CE, the paperclip.
        text: "📎"
        textFormat: TextFormat.Plain
        textStyle.base: nav.emojiStyle
        textStyle.fontSize: FontSize.PercentageValue
        textStyle.fontSizeValue: 150
    }
    Container {
        verticalAlignment: VerticalAlignment.Center
        layoutProperties: StackLayoutProperties {
            spaceQuota: 1
        }
        Label {
            bottomMargin: 0
            multiline: false
            text: info ? info.name : ""
            textFormat: TextFormat.Plain
            textStyle.base: nav.emojiStyle
            textStyle.fontSize: FontSize.PercentageValue
            textStyle.fontSizeValue: nav.bodySize
        }
        Label {
            topMargin: 0
            bottomMargin: 0
            multiline: false
            text: info ? info.sizeText : ""
            textFormat: TextFormat.Plain
            textStyle.base: nav.emojiStyle
            textStyle.fontSize: FontSize.PercentageValue
            textStyle.fontSizeValue: nav.smallSize
            textStyle.color: ui.palette.secondaryTextOnPlain
        }
    }
    // A button, not a toggle: always grey, as As file's off.
    Container {
        visible: canEdit
        verticalAlignment: VerticalAlignment.Center
        leftMargin: ui.du(1)
        leftPadding: ui.du(1.5)
        rightPadding: ui.du(1.5)
        topPadding: ui.du(1)
        bottomPadding: ui.du(1)
        background: Color.create("#33808080")
        gestureHandlers: TapHandler {
            onTapped: edit()
        }
        Label {
            text: qsTr("Edit")
            textFormat: TextFormat.Plain
            textStyle.fontSize: FontSize.PercentageValue
            textStyle.fontSizeValue: nav.smallSize
            textStyle.color: ui.palette.textOnPlain
        }
    }
    // A PNG goes as a photo unless this is on (the page clears it for each file). A square toggle, as the call screen's,
    // not the stock check box under the name (the user, 2026-11-17: ugly): grey off, the accent on.
    Container {
        visible: canFile
        verticalAlignment: VerticalAlignment.Center
        leftMargin: ui.du(1)
        leftPadding: ui.du(1.5)
        rightPadding: ui.du(1.5)
        topPadding: ui.du(1)
        bottomPadding: ui.du(1)
        background: bar.asFile ? ui.palette.primary : Color.create("#33808080")
        gestureHandlers: TapHandler {
            onTapped: bar.asFile = !bar.asFile
        }
        Label {
            text: qsTr("As file")
            textFormat: TextFormat.Plain
            textStyle.fontSize: FontSize.PercentageValue
            textStyle.fontSizeValue: nav.smallSize
            textStyle.color: bar.asFile ? Color.White : ui.palette.textOnPlain
        }
    }
    // The cross has its own padding so the tap has a finger's size.
    Container {
        verticalAlignment: VerticalAlignment.Center
        leftPadding: ui.du(1.5)
        rightPadding: ui.du(0.5)
        topPadding: ui.du(1)
        bottomPadding: ui.du(1)
        gestureHandlers: TapHandler {
            onTapped: cancelled()
        }
        Label {
            text: "✕"
            textFormat: TextFormat.Plain
            textStyle.base: nav.emojiStyle
            textStyle.color: ui.palette.secondaryTextOnPlain
        }
    }
}
