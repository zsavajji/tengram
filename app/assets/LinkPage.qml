import bb.cascades 1.4
import "errors.js" as Errors

// The linking page, the root while not linked (PLAN.md §7): a QR code to scan, the code Telegram
// sent to type, or the two-step password, by what the engine's link state says.
Page {
    id: page
    property bool up: engine.connected
    property string linkState: engine.link.state || ""
    property bool syncing: engine.link.syncing || false
    // asked: this page already requested a code for this engine run. shown: a code was on screen.
    property bool asked: false
    property bool shown: false
    // No login on an engine under the stock 30 MB headless cap (the user, 2026-11-17): the first sync would
    // die against it. memMB is the engine's limit at its start (0: none); the memory addon raises it to 128,
    // and the engine it restarts reports that. Until then nothing here asks Telegram for anything.
    property bool memBlocked: (engine.link.memMB || 0) > 0 && engine.link.memMB < 128

    titleBar: TitleBar {
        title: qsTr("Link to Telegram")
    }
    actions: [
        // Off once the phone accepted the link, through "Getting your chats…", so no stray press
        // starts a second link (the user's, 2026-11-01). ActionItem has no visible.
        ActionItem {
            title: qsTr("Link with phone number")
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: !page.memBlocked && page.linkState != "linking" && page.linkState != "ready"
            onTriggered: page.openNumber()
        }
    ]
    attachedObjects: ComponentDefinition {
        id: numberDef
        source: "LinkNumberPage.qml"
    }

    // The engine makes codes only on request; ask once per run, and again after a restart.
    function ask() {
        if (up && !memBlocked && linkState == "unlinked" && !asked) {
            asked = true
            engine.request("link.qr", {})
        }
    }
    // TDLib takes no phone number while a QR code waits; a logout there ends nothing, there is no
    // session yet. The state goes back to "unlinked" and the number page can then ask.
    // asked: the "unlinked" that follows must not ask for a QR code under the number page.
    function openNumber() {
        asked = true
        if (linkState == "qr")
            engine.request("link.logout", {})
        nav.push(numberDef.createObject())
    }
    // Where the code went, by the type TDLib reports.
    function codeWhere() {
        var phone = engine.link.phone || ""
        if (phone != "" && phone.charAt(0) != "+")
            phone = "+" + phone
        var t = engine.link.codeType
        if (t == "app")
            return qsTr("Telegram sent the code to the Telegram app on your other devices, not by SMS. Look in the chat named Telegram.")
        if (t == "sms")
            return qsTr("Telegram sent the code by SMS to %1.").arg(phone)
        if (t == "call")
            return qsTr("Telegram will call %1 and tell you the code.").arg(phone)
        return qsTr("Type the code Telegram sent to %1.").arg(phone)
    }
    function sendCode() {
        if (codeField.text == "")
            return
        engine.request("link.code", {"code": codeField.text})
    }
    function sendPassword() {
        if (passwordField.text == "")
            return
        engine.request("link.password", {"password": passwordField.text})
    }
    onUpChanged: {
        if (!up)
            asked = false
        ask()
    }
    onMemBlockedChanged: ask()
    onLinkStateChanged: {
        if (linkState == "qr")
            shown = true
        if (linkState == "code")
            codeField.text = ""
        if (linkState == "password")
            passwordField.text = ""
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

                // The engine under the 30 MB cap: install the memory addon first (main.qml asks too). The same
                // install also helps when the addon is there but hasn't patched yet (about 4 minutes after a
                // restart): it patches now and restarts the engine.
                Container {
                    visible: page.up && page.memBlocked
                    horizontalAlignment: HorizontalAlignment.Fill
                    Label {
                        multiline: true
                        text: memPatch.rootAvailable
                              ? qsTr("Tengram's engine has 30 MB of memory, too little to sign in and load your chats. Install the memory addon first.")
                              : memPatch.headline
                    }
                    Button {
                        horizontalAlignment: HorizontalAlignment.Center
                        visible: memPatch.rootAvailable
                        enabled: !memPatch.busy
                        text: memPatch.busy ? qsTr("Installing…") : qsTr("Install the memory addon")
                        onClicked: memPatch.install()
                    }
                }
                Container {
                    visible: page.up && !page.memBlocked && page.linkState == "qr"
                    horizontalAlignment: HorizontalAlignment.Fill
                    Label {
                        multiline: true
                        text: qsTr("On your phone, open Telegram, go to Settings, then Devices, tap Link Desktop Device, and scan this code.")
                    }
                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Center
                        image: qr.image(engine.link.qr || "", 360)
                        scalingMethod: ScalingMethod.None
                        loadEffect: ImageViewLoadEffect.None
                    }
                }
                Container {
                    visible: page.up && !page.memBlocked && page.linkState == "code"
                    horizontalAlignment: HorizontalAlignment.Fill
                    Label {
                        multiline: true
                        text: page.codeWhere()
                    }
                    TextField {
                        id: codeField
                        inputMode: TextFieldInputMode.NumbersAndPunctuation
                        hintText: qsTr("Code")
                        input.submitKey: SubmitKey.Submit
                        input.onSubmitted: page.sendCode()
                    }
                    Button {
                        horizontalAlignment: HorizontalAlignment.Center
                        text: qsTr("Log in")
                        onClicked: page.sendCode()
                    }
                    Button {
                        horizontalAlignment: HorizontalAlignment.Center
                        text: qsTr("Show a QR code instead")
                        onClicked: engine.request("link.qr", {})
                    }
                    Label {
                        visible: engine.failure.cmd == "link.code"
                        multiline: true
                        text: Errors.errorText(engine.failure)
                    }
                }
                Container {
                    visible: page.up && !page.memBlocked && page.linkState == "password"
                    horizontalAlignment: HorizontalAlignment.Fill
                    Label {
                        multiline: true
                        text: qsTr("Your account has a two-step verification password.")
                    }
                    Label {
                        visible: (engine.link.hint || "") != ""
                        multiline: true
                        textFormat: TextFormat.Plain
                        text: qsTr("Hint: %1").arg(engine.link.hint || "")
                    }
                    TextField {
                        id: passwordField
                        inputMode: TextFieldInputMode.Password
                        hintText: qsTr("Password")
                        input.submitKey: SubmitKey.Submit
                        input.onSubmitted: page.sendPassword()
                    }
                    Button {
                        horizontalAlignment: HorizontalAlignment.Center
                        text: qsTr("Log in")
                        onClicked: page.sendPassword()
                    }
                    Label {
                        visible: engine.failure.cmd == "link.password"
                        multiline: true
                        text: Errors.errorText(engine.failure)
                    }
                }
                Container {
                    visible: page.up && !page.memBlocked && page.linkState == "unlinked"
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
                    visible: engine.failure.cmd == "link.qr" || engine.failure.cmd == "link.logout"
                    multiline: true
                    text: Errors.errorText(engine.failure)
                }
            }
        }
        Busy {
            running: !page.up || page.linkState == "linking" || page.syncing
            text: !page.up ? qsTr("Starting…")
                : page.syncing ? qsTr("Getting your chats… This can take a minute.")
                : qsTr("Checking…")
        }
    }
}
