import bb.cascades 1.4

// The first page while not linked, the root then (main.qml's show; the user, 2026-11-21): the logo big, the name and what
// Tengram is. Start messaging (with a ">") opens the phone number page (LinkNumberPage.qml), the main route, whose
// Log in with QR code button is the alternative. A login under way when the app was reopened goes on to the linking page at once.
// Back from there comes here.
Page {
    id: page
    actions: [
        ActionItem {
            title: qsTr("Start messaging")
            imageSource: "asset:///images/next_action.png"
            ActionBar.placement: ActionBarPlacement.Signature
            onTriggered: page.next()
        }
    ]
    attachedObjects: ComponentDefinition {
        id: numberDef
        source: "LinkNumberPage.qml"
    }
    // Once: a second tap while the linking page slides in would push another.
    function next() {
        if (nav.top != page)
            return
        var number = numberDef.createObject()
        nav.push(number)
        var s = engine.link.state
        if (s == "qr" || s == "code" || s == "password" || s == "email" || s == "emailCode" || s == "register" || s == "premium")
            number.openLink(false)
    }

    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        layout: DockLayout {}
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Center
            leftPadding: ui.du(2)
            rightPadding: ui.du(2)
            // 32 du is icon-288.png's own size on the Q10 (scripts/gen-icon.py).
            ImageView {
                horizontalAlignment: HorizontalAlignment.Center
                preferredWidth: ui.du(32)
                preferredHeight: ui.du(32)
                scalingMethod: ScalingMethod.AspectFit
                loadEffect: ImageViewLoadEffect.None
                imageSource: "asset:///images/icon-288.png"
            }
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                topMargin: ui.du(3)
                bottomMargin: 0
                text: "Tengram"
                textFormat: TextFormat.Plain
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: nav.bodySize * 2
                textStyle.fontWeight: FontWeight.W500
            }
            Label {
                horizontalAlignment: HorizontalAlignment.Fill
                topMargin: ui.du(1)
                multiline: true
                text: qsTr("A client for Telegram for BlackBerry 10")
                textFormat: TextFormat.Plain
                textStyle.textAlign: TextAlign.Center
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: nav.bodySize
                textStyle.color: ui.palette.secondaryTextOnPlain
            }
        }
    }
}
