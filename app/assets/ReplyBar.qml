import bb.cascades 1.4

// The message being replied to, or being edited, above the action bar (moved out of
// ChatPage.qml for step 6.4). A tap cancels. Drawn as the quote in a card is: a primary-colour
// bar, the name, one line of the text.
Container {
    property string name
    property string quote
    property bool editing
    signal cancelled()
    visible: name != ""
    horizontalAlignment: HorizontalAlignment.Fill
    background: ui.palette.plain
    leftPadding: ui.du(1.5)
    rightPadding: ui.du(1.5)
    topPadding: ui.du(1)
    bottomPadding: ui.du(1)
    layout: StackLayout {
        orientation: LayoutOrientation.LeftToRight
    }
    gestureHandlers: TapHandler {
        onTapped: cancelled()
    }
    Container {
        verticalAlignment: VerticalAlignment.Fill
        preferredWidth: ui.du(0.5)
        background: ui.palette.primary
    }
    Container {
        leftMargin: ui.du(1)
        layoutProperties: StackLayoutProperties {
            spaceQuota: 1
        }
        Label {
            bottomMargin: 0
            text: editing ? qsTr("Edit message") : qsTr("Replying to %1").arg(name)
            textFormat: TextFormat.Plain
            textStyle.base: nav.emojiStyle
            textStyle.fontSize: FontSize.PercentageValue
            textStyle.fontSizeValue: nav.smallSize
            textStyle.fontWeight: FontWeight.Bold
            textStyle.color: ui.palette.primary
        }
        Label {
            topMargin: 0
            bottomMargin: 0
            multiline: false
            // Without U+20E3, a keycap's frame, as the message card's body (MessageCard.cpp).
            text: quote.replace(/⃣/g, "")
            textFormat: TextFormat.Plain
            textStyle.base: nav.emojiStyle
            textStyle.fontSize: FontSize.PercentageValue
            textStyle.fontSizeValue: nav.smallSize
            textStyle.color: ui.palette.secondaryTextOnPlain
        }
    }
    Label {
        verticalAlignment: VerticalAlignment.Center
        leftMargin: ui.du(1)
        text: "✕"
        textFormat: TextFormat.Plain
        textStyle.base: nav.emojiStyle
        textStyle.color: ui.palette.secondaryTextOnPlain
    }
}
