import bb.cascades 1.4
import bb.cascades.multimedia 1.0
import bb.system 1.2
import tengram 1.0
import "errors.js" as Errors

// Links Telegram Desktop (Settings > Devices): the rear camera looks for the QR code its login screen shows, a
// tg://login?token= link, and the first one found is sent to the engine (sessions.confirmQr). Back to the devices page
// when it signs the device in; after a failure the camera looks again.
Page {
    id: page
    // A link is on its way to the engine: no more codes are looked at until it answers.
    property bool sending: false
    // When "Not a Telegram login code" was last shown (ms since the epoch): a code that is not one is seen on every frame.
    property real lastNote: 0
    titleBar: TitleBar {
        title: qsTr("Link desktop device")
    }
    onCreationCompleted: {
        // Not a Connections: Cascades' QML has none. Both live and die with the page.
        sessions.confirmed.connect(page.linked)
        sessions.failed.connect(page.refused)
        viewfinder.open(CameraUnit.Rear)
    }
    function scanned(data) {
        if (sending) {
            return
        }
        if (data.indexOf("tg://login?token=") != 0) {
            var now = Date.now()
            if (now - lastNote >= 2000) {
                lastNote = now
                notLoginToast.show()
            }
            return
        }
        sending = true
        sessions.confirmQr(data)
    }
    function linked() {
        linkedToast.show()
        // Before the page goes: the camera is free for the next app at once.
        viewfinder.close()
        nav.pop()
    }
    // The same code may be on screen still: a pause before it is read again.
    function refused() {
        failedToast.body = Errors.errorText(engine.failure)
        failedToast.show()
        retry.start()
    }
    attachedObjects: [
        Sessions {
            id: sessions
            link: engine
        },
        BarcodeDetector {
            camera: viewfinder
            formats: BarcodeFormat.QrCode
            onDetected: page.scanned(data)
        },
        Timer {
            id: retry
            interval: 2000
            singleShot: true
            onTimeout: page.sending = false
        },
        SystemToast {
            id: linkedToast
            body: qsTr("Device linked")
        },
        SystemToast {
            id: notLoginToast
            body: qsTr("Not a Telegram login code")
        },
        SystemToast {
            id: failedToast
        },
        SystemToast {
            id: cameraToast
            body: qsTr("Couldn't open the camera")
        }
    ]
    Container {
        background: Color.Black
        layout: DockLayout {}
        Camera {
            id: viewfinder
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            onCameraOpened: viewfinder.startViewfinder()
            onCameraOpenFailed: cameraToast.show()
            // Another app took the camera, or this one went to the background: it is opened again when it is free.
            onCameraResourceReleased: viewfinder.close()
            onCameraResourceAvailable: viewfinder.open(CameraUnit.Rear)
        }
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Bottom
            topPadding: ui.du(1.5)
            bottomPadding: ui.du(1.5)
            leftPadding: ui.du(1.5)
            rightPadding: ui.du(1.5)
            background: Color.create("#99000000")
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                multiline: true
                textStyle.textAlign: TextAlign.Center
                textStyle.color: Color.White
                text: qsTr("Scan the QR code shown by Telegram Desktop (Settings → Devices → Link Desktop Device)")
            }
        }
    }
}
