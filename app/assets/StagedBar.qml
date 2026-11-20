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
    // A button, not a toggle: always grey, as As file's off. A pill in Round mode (the user's ask, 2026-11-19), as As file is.
    // The three buttons are reached by the Classic's trackpad: FocusPolicy.Key, not KeyAndTouch, so a touch leaves the focus (and
    // the keyboard, for the caption) in the composer; the one with the focus is darker, and a click (Enter) taps it.
    Container {
        id: editBtn
        visible: canEdit
        verticalAlignment: VerticalAlignment.Center
        leftMargin: ui.du(1)
        focusPolicy: FocusPolicy.Key
        background: settings.round ? Color.Transparent : Color.create(editBtn.focused ? "#80808080" : "#33808080")
        layout: DockLayout {}
        gestureHandlers: TapHandler {
            onTapped: edit()
        }
        keyListeners: KeyListener {
            onKeyReleased: {
                if (event.key == 13 || event.key == 10) { // Enter: the trackpad's click
                    edit()
                }
            }
        }
        RoundFill {
            pill: true
            filterColor: Color.create(editBtn.focused ? "#80808080" : "#33808080")
        }
        Container {
            leftPadding: ui.du(1.5)
            rightPadding: ui.du(1.5)
            topPadding: ui.du(1)
            bottomPadding: ui.du(1)
            Label {
                text: qsTr("Edit")
                textFormat: TextFormat.Plain
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: nav.smallSize
                textStyle.color: ui.palette.textOnPlain
            }
        }
    }
    // A PNG goes as a photo unless this is on (the page clears it for each file). A square toggle, as the call screen's,
    // not the stock check box under the name (the user, 2026-11-17: ugly): grey off, the accent on.
    Container {
        id: fileBtn
        visible: canFile
        verticalAlignment: VerticalAlignment.Center
        leftMargin: ui.du(1)
        focusPolicy: FocusPolicy.Key
        // Focused: the grey darker; the accent (on) dimmed.
        opacity: bar.asFile && fileBtn.focused ? 0.7 : 1
        background: settings.round ? Color.Transparent : bar.asFile ? ui.palette.primary : Color.create(fileBtn.focused ? "#80808080" : "#33808080")
        layout: DockLayout {}
        gestureHandlers: TapHandler {
            onTapped: bar.asFile = !bar.asFile
        }
        keyListeners: KeyListener {
            onKeyReleased: {
                if (event.key == 13 || event.key == 10) { // Enter: the trackpad's click
                    bar.asFile = !bar.asFile
                }
            }
        }
        RoundFill {
            pill: true
            filterColor: bar.asFile ? ui.palette.primary : Color.create(fileBtn.focused ? "#80808080" : "#33808080")
        }
        Container {
            leftPadding: ui.du(1.5)
            rightPadding: ui.du(1.5)
            topPadding: ui.du(1)
            bottomPadding: ui.du(1)
            Label {
                text: qsTr("As file")
                textFormat: TextFormat.Plain
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: nav.smallSize
                textStyle.color: bar.asFile ? Color.White : ui.palette.textOnPlain
            }
        }
    }
    // The cross has its own padding so the tap has a finger's size.
    Container {
        id: dropBtn
        verticalAlignment: VerticalAlignment.Center
        leftPadding: ui.du(1.5)
        rightPadding: ui.du(0.5)
        topPadding: ui.du(1)
        bottomPadding: ui.du(1)
        focusPolicy: FocusPolicy.Key
        background: dropBtn.focused ? Color.create("#33808080") : Color.Transparent
        gestureHandlers: TapHandler {
            onTapped: cancelled()
        }
        keyListeners: KeyListener {
            onKeyReleased: {
                if (event.key == 13 || event.key == 10) { // Enter: the trackpad's click
                    cancelled()
                }
            }
        }
        Label {
            text: "✕"
            textFormat: TextFormat.Plain
            textStyle.base: nav.emojiStyle
            textStyle.color: ui.palette.secondaryTextOnPlain
        }
    }
}
