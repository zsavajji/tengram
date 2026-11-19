import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0
import "errors.js" as Errors

// Privacy and security > Connected websites: the sites we signed in to with Telegram, each with its bot, browser, place
// and last use; a tap asks to disconnect it, and the action to disconnect them all. The page asks the engine itself
// (websites.list) through its own Privacy, so the answer goes with the page.
Page {
    id: page
    property variant list: privacy.websites
    property bool loaded: privacy.websitesLoaded
    property bool failed: !loaded && engine.failure.cmd == "websites.list"
    // The website the disconnect dialog asks about, held until the answer.
    property variant pending: ({})
    titleBar: PageBar {
        title: qsTr("Connected websites")
    }
    onListChanged: fill()
    onCreationCompleted: {
        // Not a Connections: Cascades' QML has none. The signal lives and dies with the page.
        privacy.failed.connect(page.actionFailed)
        // Not linked, there is no account to ask about.
        if (engine.linked) {
            privacy.askWebsites()
        }
    }
    function actionFailed(cmd) {
        // The list shows its own failure.
        if (cmd != "websites.list") {
            failedToast.body = Errors.errorText(engine.failure)
            failedToast.show()
        }
    }
    function askDisconnect(site) {
        pending = site
        disconnectDialog.title = qsTr("Disconnect %1?").arg(site.domain)
        disconnectDialog.show()
    }
    function fill() {
        rows.clear()
        rows.append(list)
    }
    actions: [
        // Nothing to disconnect when there are none.
        DeleteActionItem {
            title: qsTr("Disconnect all")
            enabled: page.list.length > 0
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: disconnectAllDialog.show()
        }
    ]
    attachedObjects: [
        Privacy {
            id: privacy
            link: engine
        },
        SystemDialog {
            id: disconnectDialog
            confirmButton.label: qsTr("Disconnect")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    privacy.disconnectSite(page.pending.id)
                }
            }
        },
        SystemDialog {
            id: disconnectAllDialog
            title: qsTr("Disconnect all websites?")
            body: qsTr("You will be signed out of every website you logged in to with Telegram.")
            confirmButton.label: qsTr("Disconnect all")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    privacy.disconnectAll()
                }
            }
        },
        SystemToast {
            id: failedToast
        }
    ]
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ListView {
            // The rows reach the press colour through the view, as the chat list's do.
            property string pressColor: nav.pressColor
            dataModel: ArrayDataModel {
                id: rows
            }
            onTriggered: page.askDisconnect(dataModel.data(indexPath))
            listItemComponents: ListItemComponent {
                type: ""
                // A website: its domain, then its bot, browser and system, and place and day, each when it has text.
                Container {
                    id: item
                    horizontalAlignment: HorizontalAlignment.Fill
                    background: item.ListItem.active ? Color.create(item.ListItem.view.pressColor) : Color.Transparent
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        topPadding: ui.du(1.2)
                        bottomPadding: ui.du(1.2)
                        leftPadding: ui.du(1.5)
                        rightPadding: ui.du(1.5)
                        Label {
                            text: ListItemData.domain
                            textFormat: TextFormat.Plain
                            textStyle.fontWeight: FontWeight.Bold
                            topMargin: 0
                            bottomMargin: 0
                        }
                        Label {
                            visible: ListItemData.bot != ""
                            text: ListItemData.bot
                            textFormat: TextFormat.Plain
                            multiline: true
                            topMargin: 0
                            bottomMargin: 0
                            textStyle.base: SystemDefaults.TextStyles.SubtitleText
                            textStyle.color: ui.palette.secondaryTextOnPlain
                        }
                        Label {
                            visible: ListItemData.device != ""
                            text: ListItemData.device
                            textFormat: TextFormat.Plain
                            multiline: true
                            topMargin: 0
                            bottomMargin: 0
                            textStyle.base: SystemDefaults.TextStyles.SubtitleText
                            textStyle.color: ui.palette.secondaryTextOnPlain
                        }
                        Label {
                            visible: ListItemData.place != ""
                            text: ListItemData.place
                            textFormat: TextFormat.Plain
                            multiline: true
                            topMargin: 0
                            bottomMargin: 0
                            textStyle.base: SystemDefaults.TextStyles.SubtitleText
                            textStyle.color: ui.palette.secondaryTextOnPlain
                        }
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        preferredHeight: ui.px(2)
                        background: ui.palette.plain
                    }
                }
            }
        }
        Busy {
            running: !page.loaded && !page.failed
            text: qsTr("Loading…")
        }
        Label {
            visible: page.failed
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            multiline: true
            textStyle.textAlign: TextAlign.Center
            textStyle.color: ui.palette.secondaryTextOnPlain
            text: qsTr("Couldn't load the websites. Check the connection and try again.")
        }
        Label {
            visible: page.loaded && page.list.length == 0
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            multiline: true
            textStyle.textAlign: TextAlign.Center
            textStyle.color: ui.palette.secondaryTextOnPlain
            text: qsTr("No websites")
        }
    }
}
