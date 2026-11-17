import bb.cascades 1.4
import "errors.js" as Errors

// The linking page, the root while not linked (PLAN.md §7): a QR code to scan, or a pairing code
// to type, by what the engine's link state says.
Page {
    id: page
    property bool up: engine.connected
    property string linkState: engine.link.state || ""
    property bool syncing: engine.link.syncing || false
    // asked: this page already requested a code for this engine run. shown: a code was on screen.
    property bool asked: false
    property bool shown: false

    titleBar: TitleBar {
        title: qsTr("Link to WhatsApp")
    }
    actions: [
        // Off once the phone accepted the link, through "Getting your chats…", so no stray press
        // starts a second link (the user's, 2026-11-01). ActionItem has no visible.
        ActionItem {
            title: qsTr("Link with phone number")
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: page.linkState != "linking" && page.linkState != "ready"
            onTriggered: nav.push(numberDef.createObject())
        }
    ]
    attachedObjects: ComponentDefinition {
        id: numberDef
        source: "LinkNumberPage.qml"
    }

    // The engine makes codes only on request; ask once per run, and again after a restart.
    function ask() {
        if (up && linkState == "unlinked" && !asked) {
            asked = true
            engine.request("link.qr", {})
        }
    }
    onUpChanged: {
        if (!up)
            asked = false
        ask()
    }
    onLinkStateChanged: {
        if (linkState == "qr")
            shown = true
        ask()
    }
    onCreationCompleted: {
        if (linkState == "qr")
            shown = true
        ask()
    }

    Container {
        layout: DockLayout {}
        ScrollView {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            Container {
                topPadding: ui.du(2)
                bottomPadding: ui.du(2)
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                horizontalAlignment: HorizontalAlignment.Fill

                Container {
                    visible: page.up && page.linkState == "qr"
                    horizontalAlignment: HorizontalAlignment.Fill
                    Label {
                        multiline: true
                        text: qsTr("On your phone, open WhatsApp, go to Linked devices, tap Link a device, and scan this code.")
                    }
                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Center
                        image: qr.image(engine.link.qr || "", 360)
                        scalingMethod: ScalingMethod.None
                        loadEffect: ImageViewLoadEffect.None
                    }
                }
                Container {
                    visible: page.up && page.linkState == "code"
                    horizontalAlignment: HorizontalAlignment.Fill
                    Label {
                        multiline: true
                        text: qsTr("On your phone, open WhatsApp, go to Linked devices, tap Link a device, then Link with phone number instead, and type this code:")
                    }
                    Label {
                        horizontalAlignment: HorizontalAlignment.Center
                        textStyle.base: SystemDefaults.TextStyles.BigText
                        text: engine.link.code || ""
                    }
                }
                Container {
                    visible: page.up && page.linkState == "unlinked"
                    horizontalAlignment: HorizontalAlignment.Fill
                    Label {
                        visible: page.shown
                        multiline: true
                        text: qsTr("The code expired.")
                    }
                    Button {
                        horizontalAlignment: HorizontalAlignment.Center
                        text: page.shown ? qsTr("Show a new code") : qsTr("Show the code")
                        onClicked: engine.request("link.qr", {})
                    }
                }
                Label {
                    visible: engine.failure.cmd == "link.qr"
                    multiline: true
                    text: Errors.errorText(engine.failure)
                }
            }
        }
        Busy {
            running: !page.up || page.linkState == "linking" || page.syncing
            text: !page.up ? qsTr("Starting…")
                : page.linkState == "ready" ? qsTr("Getting your chats from your phone… This can take a few minutes.")
                : qsTr("Linking… Keep WhatsApp open on your phone.")
        }
    }
}
