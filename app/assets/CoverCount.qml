import bb.cascades 1.4

// One column of the Passport's short Active Frame (PassportCover.qml): a kind's icon over its count, the accent when
// there is something, quiet when 0.
Container {
    property int count: 0
    property string icon
    property variant quiet
    property variant accent
    layoutProperties: StackLayoutProperties {
        spaceQuota: 1
    }
    verticalAlignment: VerticalAlignment.Center
    leftMargin: 0
    rightMargin: 0
    ImageView {
        horizontalAlignment: HorizontalAlignment.Center
        imageSource: icon
        filterColor: count > 0 ? accent : quiet
        preferredWidth: 64
        preferredHeight: 64
        scalingMethod: ScalingMethod.AspectFit
        bottomMargin: 4
    }
    Label {
        horizontalAlignment: HorizontalAlignment.Center
        text: count
        textStyle.base: SystemDefaults.TextStyles.BodyText
        textStyle.fontSize: FontSize.PercentageValue
        textStyle.fontSizeValue: count > 99 ? 130 : 170
        textStyle.color: count > 0 ? accent : quiet
        topMargin: 0
        bottomMargin: 0
    }
}
