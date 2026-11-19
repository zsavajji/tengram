import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0
import "errors.js" as Errors

// Settings > Privacy and security, laid out as Telegram Android's page: security (the two-step verification, auto-delete,
// the blocked users, the devices), who sees what, the account's time to live, bots and websites, and contacts. The page
// asks the engine itself (privacy.get) through its own Privacy, so the answer goes with the page; the password, blocked
// users and websites pages each ask for theirs, and this one asks again when they are closed (uncovered).
Page {
    id: page
    property variant info: privacy.info
    // The rules are always in a reply that has come; the other fields are left out one by one when Telegram refused them.
    property bool loaded: info.rules !== undefined
    property bool failed: !loaded && engine.failure.cmd == "privacy.get"
    // True while the drop-downs and the switch are set from info, which they would send back as a change.
    property bool filling: false
    // The rows of the privacy rules, made in onCreationCompleted.
    property variant ruleRows: []
    titleBar: TitleBar {
        title: qsTr("Privacy and security")
    }
    onInfoChanged: fill()
    onCreationCompleted: {
        // Not a Connections: Cascades' QML has none. The signals live and die with the page.
        privacy.failed.connect(page.actionFailed)
        privacy.done.connect(page.actionDone)
        var rules = [
            { key: "phone", title: qsTr("Phone number") },
            { key: "phoneFind", title: qsTr("Who can find me by my number") },
            { key: "status", title: qsTr("Last seen & online") },
            { key: "photo", title: qsTr("Profile photo") },
            { key: "forwards", title: qsTr("Forwarded messages") },
            { key: "calls", title: qsTr("Calls") },
            { key: "birthday", title: qsTr("Birthday") },
            { key: "gifts", title: qsTr("Gifts") },
            { key: "bio", title: qsTr("Bio") },
            { key: "music", title: qsTr("Saved music") },
            { key: "invites", title: qsTr("Invites") }
        ]
        var made = []
        for (var i = 0; i < rules.length; i++) {
            // Who finds us by our number is Everybody or My contacts only.
            var row = (rules[i].key == "phoneFind" ? findDef : ruleDef).createObject()
            row.key = rules[i].key
            row.heading = rules[i].title
            rulesBox.add(row)
            made.push(row)
        }
        ruleRows = made
        // Not linked, there is no account to ask about.
        if (engine.linked) {
            privacy.ask()
        }
    }
    // The page over this one went (main.qml): the password, blocked users or websites page, which may have changed what
    // is shown here. The push of this page counts too, with the first answer still to come.
    function uncovered() {
        if (loaded) {
            privacy.ask()
        }
    }
    function actionDone(cmd) {
        if (cmd == "payments.clear") {
            doneToast.body = qsTr("Payment and shipping info cleared")
            doneToast.show()
        } else if (cmd == "contacts.clearImported") {
            doneToast.body = qsTr("Synced contacts deleted")
            doneToast.show()
        }
    }
    function actionFailed(cmd) {
        if (cmd == "privacy.get") {
            // The page shows it by itself, and an engine that is down is asked again when it is up.
            return
        }
        failedToast.body = Errors.errorText(engine.failure)
        failedToast.show()
    }
    // The place of value in list, -1 when it is not there.
    function indexIn(list, value) {
        for (var i = 0; i < list.length; i++) {
            if (list[i] == value) {
                return i
            }
        }
        return -1
    }
    // Sets the drop-downs and the switch from info. After a failed change info comes again, and this puts the drop-down
    // that was changed back.
    function fill() {
        if (!loaded) {
            return
        }
        filling = true
        var phoneShown = info.rules.phone === undefined || info.rules.phone == "everybody"
        for (var i = 0; i < ruleRows.length; i++) {
            var row = ruleRows[i]
            var at = indexIn(["everybody", "contacts", "nobody"], info.rules[row.key])
            if (at >= 0) {
                row.selectedIndex = at
            }
            row.exceptions = !!info.exceptions[row.key]
            // Android asks "who can find me by my number" only when the number is not shown to everybody.
            row.visible = row.key != "phoneFind" || !phoneShown
        }
        var deleteAt = indexIn([0, 86400, 604800, 2678400], info.autoDelete)
        if (deleteAt >= 0) {
            autoDelete.selectedIndex = deleteAt
        }
        var ttlAt = indexIn([30, 90, 180, 365, 548, 730], info.ttlDays)
        if (ttlAt >= 0) {
            ttl.selectedIndex = ttlAt
        }
        topChats.checked = !!info.topChats
        filling = false
    }
    function ruleChosen(key, index) {
        var value = ["everybody", "contacts", "nobody"][index]
        if (!filling && loaded && index >= 0 && info.rules[key] != value) {
            privacy.setRule(key, value)
        }
    }
    attachedObjects: [
        Privacy {
            id: privacy
            link: engine
        },
        ComponentDefinition {
            id: passwordDef
            source: "PasswordPage.qml"
        },
        ComponentDefinition {
            id: blockedDef
            source: "BlockedPage.qml"
        },
        ComponentDefinition {
            id: devicesDef
            source: "DevicesPage.qml"
        },
        ComponentDefinition {
            id: websitesDef
            source: "WebsitesPage.qml"
        },
        // One privacy rule: its drop-down, and under it the note when Telegram has people in an exception list.
        ComponentDefinition {
            id: ruleDef
            Container {
                id: rule
                property string key
                property alias heading: ruleDropDown.title
                property alias selectedIndex: ruleDropDown.selectedIndex
                property alias exceptions: ruleNote.visible
                horizontalAlignment: HorizontalAlignment.Fill
                bottomPadding: ui.du(2)
                DropDown {
                    id: ruleDropDown
                    options: [
                        Option { text: qsTr("Everybody") },
                        Option { text: qsTr("My contacts") },
                        Option { text: qsTr("Nobody") }
                    ]
                    onSelectedIndexChanged: page.ruleChosen(rule.key, selectedIndex)
                }
                Label {
                    id: ruleNote
                    visible: false
                    multiline: true
                    topMargin: 0
                    text: qsTr("+ exceptions set in another Telegram app")
                    textStyle.base: SystemDefaults.TextStyles.SubtitleText
                    textStyle.color: ui.palette.secondaryTextOnPlain
                }
            }
        },
        // The same without Nobody: Telegram has no third answer to who finds us by our number.
        ComponentDefinition {
            id: findDef
            Container {
                id: find
                property string key
                property alias heading: findDropDown.title
                property alias selectedIndex: findDropDown.selectedIndex
                property alias exceptions: findNote.visible
                horizontalAlignment: HorizontalAlignment.Fill
                bottomPadding: ui.du(2)
                DropDown {
                    id: findDropDown
                    options: [
                        Option { text: qsTr("Everybody") },
                        Option { text: qsTr("My contacts") }
                    ]
                    onSelectedIndexChanged: page.ruleChosen(find.key, selectedIndex)
                }
                Label {
                    id: findNote
                    visible: false
                    multiline: true
                    topMargin: 0
                    text: qsTr("+ exceptions set in another Telegram app")
                    textStyle.base: SystemDefaults.TextStyles.SubtitleText
                    textStyle.color: ui.palette.secondaryTextOnPlain
                }
            }
        },
        SystemDialog {
            id: paymentsDialog
            title: qsTr("Clear payment and shipping info?")
            body: qsTr("The shipping info and payment cards you saved in Telegram's bots are deleted.")
            confirmButton.label: qsTr("Clear")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    privacy.clearPayments()
                }
            }
        },
        SystemDialog {
            id: importedDialog
            title: qsTr("Delete synced contacts?")
            body: qsTr("Telegram forgets the address book it got from this phone. The people already in your contacts stay.")
            confirmButton.label: qsTr("Delete")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    privacy.clearImported()
                }
            }
        },
        SystemToast {
            id: doneToast
        },
        SystemToast {
            id: failedToast
        }
    ]
    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            Busy {
                running: !page.loaded && !page.failed
                topPadding: ui.du(4)
                text: qsTr("Loading…")
            }
            Container {
                visible: page.failed
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(3)
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                Label {
                    horizontalAlignment: HorizontalAlignment.Center
                    multiline: true
                    textStyle.textAlign: TextAlign.Center
                    textStyle.color: ui.palette.secondaryTextOnPlain
                    text: qsTr("Couldn't load your privacy settings. Check the connection and try again.")
                }
                Button {
                    horizontalAlignment: HorizontalAlignment.Center
                    text: qsTr("Retry")
                    onClicked: privacy.ask()
                }
            }
            Container {
                visible: page.loaded
                horizontalAlignment: HorizontalAlignment.Fill
                Header {
                    title: qsTr("Security")
                }
                // Without Telegram's answer there is no state to open.
                SettingRow {
                    visible: page.info.password !== undefined
                    title: qsTr("Two-step verification")
                    subtitle: !page.info.password ? "" : page.info.password.set ? qsTr("On") : qsTr("Off")
                    onTapped: {
                        var p = passwordDef.createObject()
                        // The page starts from what this one has, and asks for nothing.
                        p.seed = page.info
                        nav.push(p)
                    }
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(1)
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    DropDown {
                        id: autoDelete
                        title: qsTr("Auto-delete messages")
                        options: [
                            Option { text: qsTr("Off") },
                            Option { text: qsTr("1 day") },
                            Option { text: qsTr("1 week") },
                            Option { text: qsTr("1 month") }
                        ]
                        onSelectedIndexChanged: {
                            var secs = [0, 86400, 604800, 2678400][selectedIndex]
                            if (!page.filling && page.loaded && selectedIndex >= 0 && page.info.autoDelete != secs) {
                                privacy.setAutoDelete(secs)
                            }
                        }
                    }
                }
                SettingRow {
                    visible: page.info.blocked !== undefined
                    title: qsTr("Blocked users")
                    subtitle: page.info.blocked > 0 ? String(page.info.blocked) : qsTr("None")
                    onTapped: nav.push(blockedDef.createObject())
                }
                SettingRow {
                    title: qsTr("Devices")
                    onTapped: nav.push(devicesDef.createObject())
                }
                Header {
                    title: qsTr("Privacy")
                }
                // The rules' rows are added under this line (onCreationCompleted).
                Container {
                    id: rulesBox
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(1)
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    Label {
                        text: qsTr("Who can see my:")
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                }
                Header {
                    title: qsTr("Delete my account")
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(1)
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    DropDown {
                        id: ttl
                        title: qsTr("If away for")
                        options: [
                            Option { text: qsTr("1 month") },
                            Option { text: qsTr("3 months") },
                            Option { text: qsTr("6 months") },
                            Option { text: qsTr("12 months") },
                            Option { text: qsTr("18 months") },
                            Option { text: qsTr("24 months") }
                        ]
                        onSelectedIndexChanged: {
                            var days = [30, 90, 180, 365, 548, 730][selectedIndex]
                            if (!page.filling && page.loaded && selectedIndex >= 0 && page.info.ttlDays != days) {
                                privacy.setTtl(days)
                            }
                        }
                    }
                    Label {
                        topMargin: 0
                        multiline: true
                        text: qsTr("If you don't come online at least once in this period, your account will be deleted along with all messages and contacts.")
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                }
                Header {
                    title: qsTr("Bots and websites")
                }
                SettingRow {
                    title: qsTr("Clear payment and shipping info")
                    onTapped: paymentsDialog.show()
                }
                SettingRow {
                    title: qsTr("Connected websites")
                    onTapped: nav.push(websitesDef.createObject())
                }
                Header {
                    title: qsTr("Contacts")
                }
                SettingRow {
                    visible: page.info.imported !== undefined
                    title: qsTr("Delete synced contacts")
                    subtitle: qsTr("%Ln contacts", "", page.info.imported || 0)
                    onTapped: importedDialog.show()
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(2)
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Container {
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        verticalAlignment: VerticalAlignment.Center
                        Label {
                            text: qsTr("Sync contacts")
                            bottomMargin: 0
                        }
                        Label {
                            text: qsTr("Off, your address book stays on this phone. On, Tengram sends its names and numbers to Telegram, so the people you know there become your contacts.")
                            multiline: true
                            topMargin: 0
                            textStyle.base: SystemDefaults.TextStyles.SubtitleText
                            textStyle.color: ui.palette.secondaryTextOnPlain
                        }
                    }
                    // Off by default: the address book goes to Telegram only when the user turns this on (ContactBook).
                    ToggleButton {
                        verticalAlignment: VerticalAlignment.Center
                        checked: settings.syncContacts
                        onCheckedChanged: settings.syncContacts = checked
                    }
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(2)
                    bottomPadding: ui.du(2)
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Container {
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        verticalAlignment: VerticalAlignment.Center
                        Label {
                            text: qsTr("Suggest frequent contacts")
                            bottomMargin: 0
                        }
                        Label {
                            text: qsTr("Show the people you message most at the top of the search.")
                            multiline: true
                            topMargin: 0
                            textStyle.base: SystemDefaults.TextStyles.SubtitleText
                            textStyle.color: ui.palette.secondaryTextOnPlain
                        }
                    }
                    ToggleButton {
                        id: topChats
                        verticalAlignment: VerticalAlignment.Center
                        onCheckedChanged: {
                            if (!page.filling && page.loaded && !!page.info.topChats != checked) {
                                privacy.setTopChats(checked)
                            }
                        }
                    }
                }
            }
        }
    }
}
