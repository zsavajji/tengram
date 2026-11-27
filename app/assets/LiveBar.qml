import bb.cascades 1.4

// At the chat page's top while we share our live location in this chat (the engine's `live` event, EngineLink.sharingIn):
// a line that says so and Stop, which ends it (location.stop). A failure is LiveActions.qml's toast. No clock: the card's
// label says how long it has left. The Classic's trackpad reaches Stop: FocusPolicy.Key, not KeyAndTouch, so a touch leaves
// the composer's keyboard up; the focused pill is darker, and a click (Enter) taps it.
Container {
    id: bar
    property string jid
    // {chat, id, until, alert}, empty when we share nothing here.
    property variant sharing: engine.sharingRevision >= 0 && bar.jid != "" ? engine.sharingIn(bar.jid) : ({})
    visible: !!sharing.id
    horizontalAlignment: HorizontalAlignment.Fill
    background: Color.create(nav.inColor)
    leftPadding: ui.du(2)
    rightPadding: ui.du(2)
    topPadding: ui.du(1)
    bottomPadding: ui.du(1)
    layout: StackLayout {
        orientation: LayoutOrientation.LeftToRight
    }
    Label {
        verticalAlignment: VerticalAlignment.Center
        layoutProperties: StackLayoutProperties {
            spaceQuota: 1
        }
        multiline: false
        text: qsTr("You are sharing your live location")
        textFormat: TextFormat.Plain
        textStyle.fontSize: FontSize.PercentageValue
        textStyle.fontSizeValue: nav.smallSize
        textStyle.color: ui.palette.textOnPlain
    }
    // A pill in Round mode, a square otherwise, as the staged bar's buttons.
    TapArea {
        id: stopBtn
        verticalAlignment: VerticalAlignment.Center
        leftMargin: ui.du(1)
        focusPolicy: FocusPolicy.Key
        background: settings.round ? Color.Transparent : Color.create(stopBtn.focused ? "#80808080" : "#33808080")
        layout: DockLayout {}
        onClicked: engine.request("location.stop", {"chat": bar.jid, "id": bar.sharing.id})
        RoundFill {
            pill: true
            filterColor: Color.create(stopBtn.focused ? "#80808080" : "#33808080")
        }
        Container {
            leftPadding: ui.du(1.5)
            rightPadding: ui.du(1.5)
            topPadding: ui.du(1)
            bottomPadding: ui.du(1)
            Label {
                text: qsTr("Stop")
                textFormat: TextFormat.Plain
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: nav.smallSize
                textStyle.color: ui.palette.textOnPlain
            }
        }
    }
}
