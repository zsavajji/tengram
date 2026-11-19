import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0
import "errors.js" as Errors

// Settings > Devices: the devices the account is signed in on, this one first, and the way to end the others' sessions
// or to sign Telegram Desktop in by its QR code (QrScanPage). The page asks the engine itself (sessions.list) through
// its own Sessions, so the answer goes with the page.
Page {
    id: page
    property variant list: sessions.list
    property bool loaded: sessions.loaded
    property bool failed: !loaded && engine.failure.cmd == "sessions.list"
    // The session the terminate dialog asks about, held until the answer.
    property variant pending: ({})
    titleBar: TitleBar {
        title: qsTr("Devices")
    }
    onListChanged: fill()
    onCreationCompleted: {
        // Not a Connections: Cascades' QML has none. The signal lives and dies with the page.
        sessions.failed.connect(page.actionFailed)
        // Not linked, there is no account to ask about.
        if (engine.linked) {
            sessions.ask()
        }
    }
    // The page over this one went (main.qml): the scan page, after a device was linked. The push of this page counts
    // too, with the first list still to come.
    function uncovered() {
        if (sessions.loaded) {
            sessions.ask()
        }
    }
    function actionFailed() {
        failedToast.body = Errors.errorText(engine.failure)
        failedToast.show()
    }
    function askTerminate(session) {
        pending = session
        terminateDialog.body = session.device ? session.title + "\n" + session.device : session.title
        terminateDialog.show()
    }
    function fill() {
        currentBox.removeAll()
        othersBox.removeAll()
        for (var i = 0; i < list.length; i++) {
            var row = sessionDef.createObject()
            row.session = list[i]
            var box = list[i].current ? currentBox : othersBox
            box.add(row)
        }
    }
    actions: [
        ActionItem {
            title: qsTr("Link desktop device")
            imageSource: "asset:///images/plus_action.png"
            ActionBar.placement: ActionBarPlacement.Signature
            onTriggered: nav.push(scanDef.createObject())
        },
        // Nothing to end when this device is the only one.
        DeleteActionItem {
            title: qsTr("Terminate all other sessions")
            enabled: sessions.others > 0
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: terminateAllDialog.show()
        }
    ]
    attachedObjects: [
        Sessions {
            id: sessions
            link: engine
        },
        ComponentDefinition {
            id: scanDef
            source: "QrScanPage.qml"
        },
        // A session: its three lines, and a tap on another device's asks to end it.
        ComponentDefinition {
            id: sessionDef
            Container {
                id: row
                property variant session
                property bool pressed: false
                horizontalAlignment: HorizontalAlignment.Fill
                background: pressed ? Color.create(nav.pressColor) : Color.Transparent
                gestureHandlers: TapHandler {
                    onTapped: {
                        // This device has no Terminate: that is a log out, which is the Account page's.
                        if (!row.session.current) {
                            page.askTerminate(row.session)
                        }
                    }
                }
                onTouch: row.pressed = !row.session.current && (event.isDown() || event.isMove())
                onTouchExit: row.pressed = false
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(1.2)
                    bottomPadding: ui.du(1.2)
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    Label {
                        text: row.session ? row.session.title : ""
                        textFormat: TextFormat.Plain
                        textStyle.fontWeight: FontWeight.Bold
                        topMargin: 0
                        bottomMargin: 0
                    }
                    Label {
                        visible: !!row.session && row.session.device != ""
                        text: row.session ? row.session.device : ""
                        textFormat: TextFormat.Plain
                        multiline: true
                        topMargin: 0
                        bottomMargin: 0
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                    Label {
                        visible: !!row.session && row.session.place != ""
                        text: row.session ? row.session.place : ""
                        textFormat: TextFormat.Plain
                        multiline: true
                        topMargin: 0
                        bottomMargin: 0
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                    // A login nobody confirmed from another device.
                    Label {
                        visible: !!row.session && row.session.unconfirmed
                        text: qsTr("New login")
                        topMargin: 0
                        bottomMargin: 0
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: ui.palette.primary
                    }
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.px(2)
                    background: ui.palette.plain
                }
            }
        },
        SystemDialog {
            id: terminateDialog
            title: qsTr("Terminate this session?")
            confirmButton.label: qsTr("Terminate")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    sessions.terminate(page.pending.id)
                }
            }
        },
        SystemDialog {
            id: terminateAllDialog
            title: qsTr("Terminate all other sessions")
            body: qsTr("Are you sure you want to terminate all other sessions?")
            confirmButton.label: qsTr("Terminate")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    sessions.terminateOthers()
                }
            }
        },
        SystemToast {
            id: failedToast
        }
    ]
    // One scrolling column: a ListView holds a handful of rows no better.
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ScrollView {
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                Busy {
                    running: !page.loaded && !page.failed
                    topPadding: ui.du(4)
                    text: qsTr("Loading…")
                }
                Label {
                    visible: page.failed
                    horizontalAlignment: HorizontalAlignment.Center
                    topMargin: ui.du(3)
                    multiline: true
                    textStyle.textAlign: TextAlign.Center
                    textStyle.color: ui.palette.secondaryTextOnPlain
                    text: qsTr("Couldn't load the devices. Check the connection and try again.")
                }
                Header {
                    visible: page.loaded
                    title: qsTr("This device")
                }
                Container {
                    id: currentBox
                    horizontalAlignment: HorizontalAlignment.Fill
                }
                Header {
                    visible: sessions.others > 0
                    title: qsTr("Active sessions")
                }
                Container {
                    id: othersBox
                    horizontalAlignment: HorizontalAlignment.Fill
                }
            }
        }
    }
}
