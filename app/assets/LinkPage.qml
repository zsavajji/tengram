import bb.cascades 1.4
import "errors.js" as Errors

// The linking page, pushed over LinkNumberPage (PLAN.md §7), for the step after the number or for the QR code: a QR code to scan, the code Telegram
// sent to type, the two-step password, or the sign-up of a number new to Telegram (a login e-mail and its
// code, a name, or a Premium purchase that can't be made here), by what the engine's link state says.
Page {
    id: page
    property bool up: engine.connected
    property string linkState: engine.link.state || ""
    property bool syncing: engine.link.syncing || false
    // asked: this page already requested a code for this engine run. shown: a code was on screen.
    property bool asked: false
    // Set by LinkNumberPage's QR button; only then does this page ask Telegram for a QR code.
    property bool wantQr: false
    property bool shown: false
    // No login on an engine under the stock 30 MB headless cap (the user, 2026-11-17): the first sync would
    // die against it. memMB is the engine's limit at its start (0: none); the memory addon raises it to 128,
    // and the engine it restarts reports that. Until then nothing here asks Telegram for anything.
    property bool memBlocked: (engine.link.memMB || 0) > 0 && engine.link.memMB < 128

    titleBar: PageBar {
        title: qsTr("Link to Telegram")
    }
    // The step's own action at the bottom (the user, 2026-11-21): Log in for the code and the password, Next for the login
    // e-mail and its code, Sign up for a new account. No other action: Back goes to the number page, which also has the
    // QR code. ActionItem has no visible, so showStep() adds it the first time a state has a step and it stays, off where
    // there is none (the QR code, a Premium purchase) and while a step is checked.
    property bool hasStep: linkState == "code" || linkState == "password" || linkState == "email" || linkState == "emailCode"
                           || linkState == "register"
    attachedObjects: ActionItem {
        id: stepAction
        title: page.linkState == "register" ? qsTr("Sign up")
             : page.linkState == "email" || page.linkState == "emailCode" ? qsTr("Next") : qsTr("Log in")
        imageSource: page.linkState == "email" || page.linkState == "emailCode" ? "asset:///images/next_action.png"
                                                                                  : "asset:///images/tick_action.png"
        ActionBar.placement: ActionBarPlacement.Signature
        enabled: page.up && !page.memBlocked && page.hasStep
        onTriggered: page.step()
    }
    function showStep() {
        if (hasStep && actionCount() == 0)
            addAction(stepAction, ActionBarPlacement.Signature)
    }
    function step() {
        if (linkState == "code")
            sendCode()
        else if (linkState == "password")
            sendPassword()
        else if (linkState == "email")
            sendEmail()
        else if (linkState == "emailCode")
            sendEmailCode()
        else if (linkState == "register")
            signUp()
    }
    onWantQrChanged: ask()

    // The engine makes codes only on request; ask once per run, and again after a restart.
    function ask() {
        if (wantQr && up && !memBlocked && linkState == "unlinked" && !asked) {
            asked = true
            engine.request("link.qr", {})
        }
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
    function sendEmail() {
        if (emailField.text == "")
            return
        engine.request("link.email", {"email": emailField.text})
    }
    function sendEmailCode() {
        if (emailCodeField.text == "")
            return
        engine.request("link.emailCode", {"code": emailCodeField.text})
    }
    function signUp() {
        if (firstField.text == "")
            return
        engine.request("link.register", {"first": firstField.text, "last": lastField.text})
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
        if (linkState == "email")
            emailField.text = ""
        if (linkState == "emailCode")
            emailCodeField.text = ""
        if (linkState == "register") {
            firstField.text = ""
            lastField.text = ""
        }
        showStep()
        ask()
    }
    onCreationCompleted: {
        if (linkState == "qr")
            shown = true
        showStep()
        ask()
    }

    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
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
                    Label {
                        visible: engine.failure.cmd == "link.password"
                        multiline: true
                        text: Errors.errorText(engine.failure)
                    }
                }
                Container {
                    visible: page.up && !page.memBlocked && page.linkState == "email"
                    horizontalAlignment: HorizontalAlignment.Fill
                    Label {
                        multiline: true
                        text: qsTr("Telegram needs an e-mail address for this account, to send you a code there.")
                    }
                    TextField {
                        id: emailField
                        inputMode: TextFieldInputMode.EmailAddress
                        hintText: qsTr("E-mail address")
                        input.submitKey: SubmitKey.Submit
                        input.onSubmitted: page.sendEmail()
                    }
                    Label {
                        visible: engine.failure.cmd == "link.email"
                        multiline: true
                        text: Errors.errorText(engine.failure)
                    }
                }
                Container {
                    visible: page.up && !page.memBlocked && page.linkState == "emailCode"
                    horizontalAlignment: HorizontalAlignment.Fill
                    Label {
                        multiline: true
                        text: qsTr("Type the code Telegram sent to %1.").arg(engine.link.pattern || "")
                    }
                    TextField {
                        id: emailCodeField
                        inputMode: TextFieldInputMode.NumbersAndPunctuation
                        hintText: qsTr("Code")
                        input.submitKey: SubmitKey.Submit
                        input.onSubmitted: page.sendEmailCode()
                    }
                    Label {
                        visible: engine.failure.cmd == "link.emailCode"
                        multiline: true
                        text: Errors.errorText(engine.failure)
                    }
                }
                Container {
                    visible: page.up && !page.memBlocked && page.linkState == "register"
                    horizontalAlignment: HorizontalAlignment.Fill
                    Label {
                        multiline: true
                        text: qsTr("This number isn't on Telegram yet. Type your name to sign up.")
                    }
                    TextField {
                        id: firstField
                        hintText: qsTr("First name")
                        input.submitKey: SubmitKey.Submit
                        input.onSubmitted: page.signUp()
                    }
                    TextField {
                        id: lastField
                        hintText: qsTr("Last name (optional)")
                        input.submitKey: SubmitKey.Submit
                        input.onSubmitted: page.signUp()
                    }
                    Label {
                        visible: engine.failure.cmd == "link.register"
                        multiline: true
                        text: Errors.errorText(engine.failure)
                    }
                    Label {
                        multiline: true
                        text: qsTr("By signing up you accept Telegram's Terms of Service:")
                    }
                    Label {
                        visible: (engine.link.minAge || 0) > 0
                        multiline: true
                        text: qsTr("You must be at least %1 years old to use Telegram.").arg(engine.link.minAge)
                    }
                    Label {
                        multiline: true
                        textFormat: TextFormat.Plain
                        text: engine.link.terms || ""
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                }
                Container {
                    visible: page.up && !page.memBlocked && page.linkState == "premium"
                    horizontalAlignment: HorizontalAlignment.Fill
                    Label {
                        multiline: true
                        text: qsTr("To sign up with this number, Telegram asks for a Telegram Premium purchase from the App Store or Google Play, which a BlackBerry can't make. Sign up on another phone first, then link this BlackBerry with its QR code.")
                    }
                    Button {
                        horizontalAlignment: HorizontalAlignment.Center
                        text: qsTr("Show a QR code instead")
                        onClicked: engine.request("link.qr", {})
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
