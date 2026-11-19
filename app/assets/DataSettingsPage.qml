import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0
import "errors.js" as Errors

// Settings > Data and storage, laid out as Telegram Android's page: the disk and network usage, the automatic download,
// the media kept on the phone, the copy to the gallery and the app cache. The settings are saved as they change, each in
// `settings`, which sends them to the engine; the usage figures come from the engine (storage.get, network.get) through
// the page's own Storage, so the answer goes with the page.
Page {
    id: page
    property string cacheText: ""
    // storage.get's {files, count, db, logs} and network.get's {since, mobile, wifi, other}; empty until they come.
    property variant used: storage.storage
    property variant net: storage.network
    property bool loaded: used.files !== undefined
    property bool netLoaded: net.since !== undefined
    property bool failed: !loaded && engine.failure.cmd == "storage.get"
    // The automatic download's other settings only count while it is on.
    property bool autoOn: settings.autoNet != "none"
    // True while the drop-downs are set from `settings`, which they would send back as a change. Also true until the
    // page is made, whose drop-downs choose their first option on their own.
    property bool filling: true
    titleBar: TitleBar {
        title: qsTr("Data and storage")
    }
    onCreationCompleted: {
        // Not a Connections: Cascades' QML has none. The signals live and die with the page.
        storage.failed.connect(page.actionFailed)
        storage.done.connect(page.actionDone)
        fill()
        // Read when the page opens and after a clear; the walk of data/cache/ is a few files.
        cacheText = settings.cacheSize()
        // Not linked, there is no account to ask about.
        if (engine.linked) {
            storage.ask()
        }
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
    // Sets the drop-downs from `settings`. A value that is not in a list (a settings file edited by hand) leaves the
    // drop-down as it is.
    function fill() {
        filling = true
        var at = indexIn(["none", "wifi", "all"], settings.autoNet)
        if (at >= 0) {
            netDrop.selectedIndex = at
        }
        at = indexIn(videoSizes, settings.autoVideoMB)
        if (at >= 0) {
            videoDrop.selectedIndex = at
        }
        at = indexIn(fileSizes, settings.autoFileMB)
        if (at >= 0) {
            fileDrop.selectedIndex = at
        }
        at = indexIn(keepSizes, settings.keepMB)
        if (at >= 0) {
            keepDrop.selectedIndex = at
        }
        // 0 is Automatic, then the sizes from the largest down: the option's place is the setting.
        qualityDrop.selectedIndex = settings.videoQuality
        filling = false
    }
    // The choices of the size drop-downs, in MB; 0 is Never (the downloads) or No limit (the media kept).
    property variant videoSizes: [0, 1, 5, 10, 15, 50, 100]
    property variant fileSizes: [0, 1, 3, 10, 50, 100]
    property variant keepSizes: [512, 1024, 2048, 5120, 10240, 0]
    function actionDone(cmd) {
        if (cmd == "storage.clear") {
            toast.body = qsTr("Media cleared: %1 freed").arg(storage.freed)
            toast.show()
        }
    }
    function actionFailed(cmd) {
        if (cmd == "storage.get" || cmd == "network.get") {
            // The page shows it by itself, and an engine that is down is asked again when it is up.
            return
        }
        toast.body = Errors.errorText(engine.failure)
        toast.show()
    }
    // The lines of the data used. Functions, not bindings on net.mobile: that is not there until network.get has come.
    function mobileText() {
        return !netLoaded ? "" : qsTr("Mobile: received %1, sent %2").arg(storage.bytesText(net.mobile.received)).arg(storage.bytesText(net.mobile.sent))
    }
    function wifiText() {
        return !netLoaded ? "" : qsTr("Wi-Fi: received %1, sent %2").arg(storage.bytesText(net.wifi.received)).arg(storage.bytesText(net.wifi.sent))
    }
    attachedObjects: [
        Storage {
            id: storage
            link: engine
        },
        SystemToast {
            id: toast
        },
        SystemDialog {
            id: clearDialog
            title: qsTr("Clear media?")
            body: qsTr("Downloaded photos, videos and files are removed from the phone. They stay in Telegram and download again when opened.")
            confirmButton.label: qsTr("Clear")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    storage.clear()
                }
            }
        },
        SystemDialog {
            id: resetDialog
            title: qsTr("Reset data usage?")
            body: qsTr("The count of data sent and received starts again from now.")
            confirmButton.label: qsTr("Reset")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    storage.reset()
                }
            }
        }
    ]
    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            bottomPadding: ui.du(2)
            Header {
                title: qsTr("Disk and network usage")
            }
            Busy {
                running: !page.loaded && !page.failed
                topPadding: ui.du(2)
                bottomPadding: ui.du(2)
                text: qsTr("Loading…")
            }
            Container {
                visible: page.failed
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(2)
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                Label {
                    horizontalAlignment: HorizontalAlignment.Center
                    multiline: true
                    textStyle.textAlign: TextAlign.Center
                    textStyle.color: ui.palette.secondaryTextOnPlain
                    text: qsTr("Couldn't load the storage figures. Check the connection and try again.")
                }
                Button {
                    horizontalAlignment: HorizontalAlignment.Center
                    text: qsTr("Retry")
                    onClicked: storage.ask()
                }
            }
            Container {
                visible: page.loaded
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(1)
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Container {
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        verticalAlignment: VerticalAlignment.Center
                        Label {
                            text: qsTr("Storage used")
                            bottomMargin: 0
                        }
                        Label {
                            // What Telegram keeps of the photos, videos and files opened or downloaded, stickers and
                            // profile pictures with them.
                            text: storage.bytesText(page.used.files) + " · " + qsTr("%Ln files", "", page.used.count || 0)
                            multiline: true
                            topMargin: 0
                            textStyle.base: SystemDefaults.TextStyles.SubtitleText
                            textStyle.color: ui.palette.secondaryTextOnPlain
                        }
                    }
                    Button {
                        verticalAlignment: VerticalAlignment.Center
                        text: qsTr("Clear media")
                        onClicked: clearDialog.show()
                    }
                }
                Label {
                    topMargin: ui.du(1)
                    multiline: true
                    text: qsTr("Telegram's database: %1").arg(storage.bytesText(page.used.db))
                    textStyle.base: SystemDefaults.TextStyles.SubtitleText
                    textStyle.color: ui.palette.secondaryTextOnPlain
                }
            }
            Container {
                visible: page.netLoaded
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
                        text: qsTr("Data used")
                        bottomMargin: 0
                    }
                    Label {
                        text: page.mobileText()
                        multiline: true
                        topMargin: 0
                        bottomMargin: 0
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                    Label {
                        text: page.wifiText()
                        multiline: true
                        topMargin: 0
                        bottomMargin: 0
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                    Label {
                        text: page.netLoaded ? qsTr("Since %1").arg(storage.dateText(page.net.since)) : ""
                        multiline: true
                        topMargin: 0
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                }
                Button {
                    verticalAlignment: VerticalAlignment.Center
                    text: qsTr("Reset")
                    onClicked: resetDialog.show()
                }
            }
            Header {
                title: qsTr("Automatic media download")
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                DropDown {
                    id: netDrop
                    title: qsTr("Download automatically")
                    options: [
                        Option { text: qsTr("Off") },
                        Option { text: qsTr("Wi-Fi only") },
                        Option { text: qsTr("Wi-Fi and mobile data") }
                    ]
                    onSelectedIndexChanged: {
                        if (!page.filling && selectedIndex >= 0) {
                            settings.autoNet = ["none", "wifi", "all"][selectedIndex]
                        }
                    }
                }
                ToggleRow {
                    title: qsTr("Photos")
                    active: page.autoOn
                    checked: settings.autoPhotos
                    onToggled: settings.autoPhotos = on
                }
                DropDown {
                    id: videoDrop
                    enabled: page.autoOn
                    title: qsTr("Videos up to")
                    options: [
                        Option { text: qsTr("Never") },
                        Option { text: "1 MB" },
                        Option { text: "5 MB" },
                        Option { text: "10 MB" },
                        Option { text: "15 MB" },
                        Option { text: "50 MB" },
                        Option { text: "100 MB" }
                    ]
                    onSelectedIndexChanged: {
                        if (!page.filling && selectedIndex >= 0) {
                            settings.autoVideoMB = page.videoSizes[selectedIndex]
                        }
                    }
                }
                DropDown {
                    id: fileDrop
                    enabled: page.autoOn
                    title: qsTr("Files up to")
                    options: [
                        Option { text: qsTr("Never") },
                        Option { text: "1 MB" },
                        Option { text: "3 MB" },
                        Option { text: "10 MB" },
                        Option { text: "50 MB" },
                        Option { text: "100 MB" }
                    ]
                    onSelectedIndexChanged: {
                        if (!page.filling && selectedIndex >= 0) {
                            settings.autoFileMB = page.fileSizes[selectedIndex]
                        }
                    }
                }
                Label {
                    multiline: true
                    text: qsTr("Voice messages and round videos download whenever automatic download is on.")
                    textStyle.base: SystemDefaults.TextStyles.SubtitleText
                    textStyle.color: ui.palette.secondaryTextOnPlain
                }
            }
            Header {
                title: qsTr("Media kept on the phone")
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                DropDown {
                    id: keepDrop
                    title: qsTr("Keep up to")
                    options: [
                        Option { text: "512 MB" },
                        Option { text: "1 GB" },
                        Option { text: "2 GB" },
                        Option { text: "5 GB" },
                        Option { text: "10 GB" },
                        Option { text: qsTr("No limit") }
                    ]
                    onSelectedIndexChanged: {
                        if (!page.filling && selectedIndex >= 0) {
                            settings.keepMB = page.keepSizes[selectedIndex]
                        }
                    }
                }
                Label {
                    multiline: true
                    text: qsTr("The oldest unused media are removed above this. They stay in Telegram.")
                    textStyle.base: SystemDefaults.TextStyles.SubtitleText
                    textStyle.color: ui.palette.secondaryTextOnPlain
                }
            }
            Header {
                title: qsTr("Save to gallery")
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                ToggleRow {
                    title: qsTr("Private chats")
                    checked: settings.galleryPrivate
                    onToggled: settings.galleryPrivate = on
                }
                ToggleRow {
                    title: qsTr("Groups")
                    checked: settings.galleryGroups
                    onToggled: settings.galleryGroups = on
                }
                ToggleRow {
                    title: qsTr("Channels")
                    checked: settings.galleryChannels
                    onToggled: settings.galleryChannels = on
                }
                Label {
                    topMargin: ui.du(1)
                    multiline: true
                    text: qsTr("New photos and videos you receive are copied to the Tengram album in Pictures and Videos.")
                    textStyle.base: SystemDefaults.TextStyles.SubtitleText
                    textStyle.color: ui.palette.secondaryTextOnPlain
                }
            }
            Header {
                title: qsTr("Calls")
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                // Automatic follows the network: the picture sharpens when it can and softens when it can't. The others
                // keep their size whatever the network does; the bit rate follows it either way.
                DropDown {
                    id: qualityDrop
                    title: qsTr("Video calls")
                    options: [
                        Option { text: qsTr("Automatic") },
                        Option { text: qsTr("High (1280x720)") },
                        Option { text: qsTr("Medium (640x480)") },
                        Option { text: qsTr("Low (320x240)") }
                    ]
                    onSelectedIndexChanged: {
                        if (!page.filling && selectedIndex >= 0) {
                            settings.videoQuality = selectedIndex
                        }
                    }
                }
            }
            Header {
                title: qsTr("App cache")
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                // What data/cache/ may hold. The engine prunes it, the oldest first (step 8.3).
                DropDown {
                    title: qsTr("Cache limit")
                    options: [
                        Option { text: "64 MB"; selected: settings.cacheLimit == 64 },
                        Option { text: "128 MB"; selected: settings.cacheLimit == 128 },
                        Option { text: "256 MB"; selected: settings.cacheLimit == 256 },
                        Option { text: "512 MB"; selected: settings.cacheLimit == 512 }
                    ]
                    onSelectedIndexChanged: {
                        if (selectedIndex >= 0) {
                            settings.cacheLimit = [64, 128, 256, 512][selectedIndex]
                        }
                    }
                }
                Container {
                    topMargin: ui.du(2)
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Label {
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        verticalAlignment: VerticalAlignment.Center
                        text: qsTr("Cache: %1").arg(page.cacheText)
                    }
                    Button {
                        verticalAlignment: VerticalAlignment.Center
                        text: qsTr("Clear cache")
                        onClicked: {
                            // Nothing in the cache is lost, so no confirmation.
                            var freed = settings.clearCache()
                            page.cacheText = settings.cacheSize()
                            toast.body = qsTr("Cache cleared: %1 freed").arg(freed)
                            toast.show()
                        }
                    }
                }
                Label {
                    topMargin: ui.du(1)
                    multiline: true
                    text: qsTr("Voice notes' decoded audio and photo previews; they're made again when needed.")
                    textStyle.base: SystemDefaults.TextStyles.SubtitleText
                    textStyle.color: ui.palette.secondaryTextOnPlain
                }
            }
        }
    }
}
