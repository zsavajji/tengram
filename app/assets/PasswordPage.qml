import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0
import "errors.js" as Errors

// Privacy and security > Two-step verification: the password Telegram asks for besides the code on a new device. With
// none set, the form for one; with one set, changing it, the recovery e-mail and turning it off; and while Telegram waits
// for the code it sent to an e-mail, the box for the code. What is shown follows info.password (docs/protocol.md). The
// page asks the engine itself through its own Privacy, which starts from the settings page's info (`seed`) and keeps
// info.password as the replies change it. A change that went through closes the page; PrivacyPage asks again.
Page {
    id: page
    // PrivacyPage's info, set right after createObject().
    property variant seed: ({})
    property variant info: privacy.info
    property variant pw: info.password || ({})
    property bool isSet: !!pw.set
    // The masked address Telegram sent a code to, "" when none is waiting.
    property string pending: pw.pendingEmail || ""
    titleBar: PageBar {
        title: qsTr("Two-step verification")
    }
    onCreationCompleted: {
        // Not a Connections: Cascades' QML has none. The signals live and die with the page.
        privacy.failed.connect(page.actionFailed)
        privacy.done.connect(page.actionDone)
    }
    function toast(text) {
        notice.body = text
        notice.show()
    }
    // The time Telegram wants us to wait, as "3 h" or "40 min".
    function waitText(secs) {
        return secs >= 3600 ? qsTr("%1 h").arg(Math.ceil(secs / 3600)) : qsTr("%1 min").arg(Math.ceil(secs / 60))
    }
    function failText(f) {
        if (f.code == "bad_password") {
            return qsTr("Wrong password")
        } else if (f.code == "bad_email") {
            return qsTr("That e-mail address doesn't work")
        } else if (f.code == "bad_code") {
            return qsTr("Wrong code")
        } else if (f.code == "too_fresh") {
            // The message holds the seconds when Telegram said them.
            var secs = /^\d+$/.test(f.message || "") ? parseInt(f.message, 10) : 0
            if (secs > 0) {
                return qsTr("Telegram allows this change later: try again in %1").arg(waitText(secs))
            }
            return qsTr("Telegram allows this change later. Try again in a while.")
        }
        return Errors.errorText(f)
    }
    function actionFailed(cmd) {
        toast(failText(engine.failure))
    }
    function actionDone(cmd) {
        if (cmd == "password.resend") {
            toast(qsTr("Code sent again"))
        } else if (pending != "") {
            // The change waits for the e-mail's code: the code box is up.
            toast(qsTr("A code was sent to %1").arg(pending))
        } else {
            if (cmd == "password.set") {
                toast(qsTr("Password saved"))
            } else if (cmd == "password.email") {
                toast(qsTr("Recovery e-mail saved"))
            } else if (cmd == "password.code") {
                toast(qsTr("E-mail confirmed"))
            } else if (cmd == "password.off") {
                toast(qsTr("Two-step verification is off"))
            }
            nav.pop()
        }
    }
    // A new password typed twice; the toast says what is wrong, and nothing is sent.
    function newOk(first, second) {
        if (first == "") {
            toast(qsTr("Type the new password"))
            return false
        }
        if (first != second) {
            toast(qsTr("The passwords don't match"))
            return false
        }
        return true
    }
    function setNew() {
        if (newOk(newPass.text, newPass2.text)) {
            privacy.setPassword("", newPass.text, newHint.text, newEmail.text.trim())
        }
    }
    function change() {
        if (newOk(changeNew.text, changeNew2.text)) {
            privacy.setPassword(changeOld.text, changeNew.text, changeHint.text, "")
        }
    }
    function changeEmail() {
        if (emailNew.text.trim() == "") {
            toast(qsTr("That e-mail address doesn't work"))
            return
        }
        privacy.setEmail(emailOld.text, emailNew.text.trim())
    }
    attachedObjects: [
        Privacy {
            id: privacy
            link: engine
            info: page.seed
        },
        SystemDialog {
            id: offDialog
            title: qsTr("Turn off two-step verification?")
            body: qsTr("Telegram will no longer ask for a password when you sign in.")
            confirmButton.label: qsTr("Turn off")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    privacy.passwordOff(offOld.text)
                }
            }
        },
        SystemToast {
            id: notice
        }
    ]
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ScrollView {
            horizontalAlignment: HorizontalAlignment.Fill // filling the page from the start: as wide as its
            verticalAlignment: VerticalAlignment.Fill     // content, it opened narrow and grew (the user, 2026-11-20)
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                // The code Telegram sent to the recovery e-mail, for a password or an address that waits for it.
                Container {
                    visible: page.pending != ""
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(2)
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    Label {
                        multiline: true
                        textFormat: TextFormat.Plain
                        text: qsTr("Enter the code sent to %1").arg(page.pending)
                    }
                    TextField {
                        id: code
                        inputMode: TextFieldInputMode.NumbersAndPunctuation
                        hintText: qsTr("Code")
                        input.submitKey: SubmitKey.Submit
                        input.onSubmitted: privacy.sendCode(code.text.trim())
                    }
                    Container {
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        horizontalAlignment: HorizontalAlignment.Center
                        Button {
                            text: qsTr("Confirm")
                            onClicked: privacy.sendCode(code.text.trim())
                        }
                        Button {
                            leftMargin: ui.du(2)
                            text: qsTr("Send again")
                            onClicked: privacy.resendCode()
                        }
                    }
                }
                // No password yet, and no code to wait for.
                Container {
                    visible: !page.isSet && page.pending == ""
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(2)
                    bottomPadding: ui.du(2)
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    Label {
                        multiline: true
                        textStyle.color: ui.palette.secondaryTextOnPlain
                        text: qsTr("Telegram will ask for this password besides the code when you sign in on a new device.")
                    }
                    TextField {
                        id: newPass
                        inputMode: TextFieldInputMode.Password
                        hintText: qsTr("New password")
                    }
                    TextField {
                        id: newPass2
                        inputMode: TextFieldInputMode.Password
                        hintText: qsTr("Repeat the password")
                    }
                    TextField {
                        id: newHint
                        hintText: qsTr("Hint (optional)")
                    }
                    TextField {
                        id: newEmail
                        inputMode: TextFieldInputMode.EmailAddress
                        hintText: qsTr("Recovery e-mail (optional)")
                    }
                    Label {
                        multiline: true
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: ui.palette.secondaryTextOnPlain
                        text: qsTr("With a recovery e-mail you can reset the password if you forget it: Telegram sends the e-mail a code.")
                    }
                    Button {
                        horizontalAlignment: HorizontalAlignment.Center
                        topMargin: ui.du(2)
                        text: qsTr("Set password")
                        onClicked: page.setNew()
                    }
                }
                // A password is set.
                Container {
                    visible: page.isSet
                    horizontalAlignment: HorizontalAlignment.Fill
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        topPadding: ui.du(2)
                        leftPadding: ui.du(2)
                        rightPadding: ui.du(2)
                        Label {
                            multiline: true
                            text: qsTr("Two-step verification is on.")
                        }
                        Label {
                            visible: !!page.pw.hint
                            multiline: true
                            textFormat: TextFormat.Plain
                            text: qsTr("Hint: %1").arg(page.pw.hint || "")
                            textStyle.base: SystemDefaults.TextStyles.SubtitleText
                            textStyle.color: ui.palette.secondaryTextOnPlain
                        }
                    }
                    Header {
                        title: qsTr("Change password")
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        leftPadding: ui.du(2)
                        rightPadding: ui.du(2)
                        TextField {
                            id: changeOld
                            inputMode: TextFieldInputMode.Password
                            hintText: qsTr("Current password")
                        }
                        TextField {
                            id: changeNew
                            inputMode: TextFieldInputMode.Password
                            hintText: qsTr("New password")
                        }
                        TextField {
                            id: changeNew2
                            inputMode: TextFieldInputMode.Password
                            hintText: qsTr("Repeat the new password")
                        }
                        TextField {
                            id: changeHint
                            hintText: qsTr("Hint (optional)")
                        }
                        Button {
                            horizontalAlignment: HorizontalAlignment.Center
                            topMargin: ui.du(2)
                            text: qsTr("Change password")
                            onClicked: page.change()
                        }
                    }
                    Header {
                        title: qsTr("Recovery e-mail")
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        leftPadding: ui.du(2)
                        rightPadding: ui.du(2)
                        Label {
                            multiline: true
                            text: page.pw.recovery ? qsTr("A recovery e-mail is set.") : qsTr("No recovery e-mail is set.")
                        }
                        TextField {
                            id: emailOld
                            inputMode: TextFieldInputMode.Password
                            hintText: qsTr("Current password")
                        }
                        TextField {
                            id: emailNew
                            inputMode: TextFieldInputMode.EmailAddress
                            hintText: qsTr("New e-mail")
                        }
                        Button {
                            horizontalAlignment: HorizontalAlignment.Center
                            topMargin: ui.du(2)
                            text: qsTr("Set recovery e-mail")
                            onClicked: page.changeEmail()
                        }
                    }
                    Header {
                        title: qsTr("Turn off")
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        bottomPadding: ui.du(2)
                        leftPadding: ui.du(2)
                        rightPadding: ui.du(2)
                        TextField {
                            id: offOld
                            inputMode: TextFieldInputMode.Password
                            hintText: qsTr("Current password")
                        }
                        Button {
                            horizontalAlignment: HorizontalAlignment.Center
                            topMargin: ui.du(2)
                            text: qsTr("Turn off two-step verification")
                            onClicked: offDialog.show()
                        }
                    }
                }
            }
        }
    }
}
