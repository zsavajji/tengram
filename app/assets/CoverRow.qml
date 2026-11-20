import bb.cascades 1.4

// One row of the Active Frame (cover.qml), as HyperBerry's: a big count, its label and a line of detail. The text
// column takes what the count leaves (spaceQuota 1), so a long detail is elided at the frame's edge, not run past it.
// The detail takes the emoji style, so names' emoji draw (they do, in a cover: 0.0.1.76). A count of 0 is quiet.
Container {
    property int count: 0
    property string label
    property string detail
    property variant ink
    property variant quiet
    property variant accent
    property variant emojiStyle
    layoutProperties: StackLayoutProperties {
        spaceQuota: 1
    }
    leftPadding: 10
    rightPadding: 10
    topMargin: 0
    bottomMargin: 0
    layout: StackLayout {
        orientation: LayoutOrientation.LeftToRight
    }
    Container {
        preferredWidth: 46
        verticalAlignment: VerticalAlignment.Center
        leftMargin: 0
        rightMargin: 0
        Label {
            horizontalAlignment: HorizontalAlignment.Center
            text: count
            textStyle.base: SystemDefaults.TextStyles.BodyText
            textStyle.fontSize: FontSize.PercentageValue
            // Three digits would not fit the column.
            textStyle.fontSizeValue: count > 99 ? 95 : 140
            textStyle.color: count > 0 ? accent : quiet
            topMargin: 0
            bottomMargin: 0
        }
    }
    Container {
        layoutProperties: StackLayoutProperties {
            spaceQuota: 1
        }
        verticalAlignment: VerticalAlignment.Center
        leftMargin: 0
        rightMargin: 0
        Label {
            text: label
            textStyle.base: SystemDefaults.TextStyles.SmallText
            textStyle.fontSize: FontSize.PercentageValue
            textStyle.fontSizeValue: 65
            textStyle.color: quiet
            topMargin: 0
            bottomMargin: 0
        }
        Label {
            text: count > 0 ? detail : qsTr("None")
            textStyle.base: emojiStyle
            textStyle.fontSize: FontSize.PercentageValue
            textStyle.fontSizeValue: 55
            textStyle.fontWeight: FontWeight.Bold
            textStyle.color: ink
            multiline: false
            topMargin: 2
            bottomMargin: 0
        }
    }
}
