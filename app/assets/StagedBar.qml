import bb.cascades 1.4

// The file picked to send, above the composer (step 6.4), in the reply bar's style: a small
// picture or a paperclip, the name over the size, and a cross that drops it. info is
// MediaOpener.fileInfo's; what is typed in the composer is the file's caption.
Container {
    property variant info
    // The picture is a PNG made a JPEG: the bar offers to send the PNG as a file instead.
    property bool canFile
    property alias asFile: fileBox.checked
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
        // A PNG goes as a photo unless this is ticked (the page clears it for each file).
        CheckBox {
            id: fileBox
            visible: canFile
            text: qsTr("Send as file")
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
