import bb.cascades 1.4
import tengram 1.0

// The call screen (step 7.4), in the look of the stock Phone app: one Sheet owned by main.qml, over
// everything. It opens on any `call` event that isn't `ended` and closes 2 s after `ended`. While the
// call rings: message and the slide bar (CallRinging); else Speaker, Mute and End call (CallPanel).
// engine.call is {id, chat, out, state, reason, since} (docs/protocol.md, "Calls"). The stage is the
// Q10's 720 px screen, kept to the bottom, so what the stock app measures from there stays where it
// was; px() scales its px to the screen's width (not by du: the Classic is 8 px/du to the Q10's 9,
// which left the stage 640 px wide on a Q20). On a tall screen (the Z10) the stage runs up to the top
// and its contents move down by shift, as the stock app's do; the caller's picture fills from the
// top, and the panel lays its controls out as the stock Z10 grid.
Sheet {
    id: sheet
    property variant call: engine.call
    property string phase: call.state || ""
    property bool ringing: phase == "ringing"
    property bool ended: phase == "ended"
    // The chat's row, read when a call comes in (its name and picture); {} for a chat we don't have.
    property variant who: ({})
    // The proximity sensor and the awake screen are held while the call is live, from dialling to
    // hang-up (and released when the sheet closes, whatever the state).
    property bool held: false
    // At the ear: the screen goes black and takes no touch. Never on the loudspeaker, where the phone
    // is held away.
    property bool atEar: held && proximity.near && !callAudio.speaker && settings.earSensor // off in Settings for a sensor that misreads
    // The call the audio link was started for, so connecting -> active doesn't restart it.
    property string audioFor: ""
    property string clock: "0:00"
    // What wording reads is passed in, so a change in any of it redraws the line.
    property string line: wording(phase, call.reason)
    // The clock shows from the answer to the hang-up, and stays at the end.
    property bool timed: (phase == "active" || ended) && call.since > 0
    // The caller's picture: the chat's preview.
    property bool hasPhoto: !!who.photo
    peekEnabled: false

    // What a tall screen adds above the 720 stage (0 on a square one).
    property real extra: ui.px(Math.max(0, screenHeight - screenWidth))
    // How far the stage's contents rise from the foot of a tall screen, from the stock Z10's frames
    // (2026-11-11): ringing, the name sits at 511 px and the slide bar ends at 1007, the foot left
    // empty; in a call the name sits at 640 and the controls take the bottom 540 px. 0 when square.
    property real lift: Math.min(extra, ringing ? px(274) : px(192))
    // Where the stage's 720 square starts: the Q10's layout moved down this far.
    property real shift: extra - lift
    function px(n) {
        return ui.px(n * screenWidth / 720)
    }

    function wording(phase, reason) {
        switch (phase) {
        case "calling":
            return qsTr("Calling…")
        case "ringing":
            return qsTr("Incoming voice call")
        case "connecting":
            return qsTr("Connecting…")
        case "ended":
            var word = reasonWord(reason)
            return qsTr("Call ended") + (word != "" ? " · " + word : "")
        }
        return ""
    }
    function reasonWord(reason) {
        switch (reason) {
        case "declined":
            return qsTr("Declined")
        case "busy":
            return qsTr("Busy")
        case "no_answer":
            return qsTr("No answer")
        case "missed":
            return qsTr("Missed")
        case "failed":
            return qsTr("Call failed")
        }
        return ""
    }
    // The caller's chat row, for the message button: the row we have, else one built as main.qml's
    // showPerson builds a person's.
    function callerRow() {
        if (who.jid) {
            return who
        }
        var name = who.name || ""
        return {"kind": 0, "jid": call.chat, "name": name, "initials": name.charAt(0).toUpperCase() || "#",
                "color": "#24A1DE"}
    }
    // m:ss from the call's own start, so a sheet opened late (a notification tap) shows the true time.
    function refreshClock() {
        var secs = Math.max(0, Math.floor((new Date().getTime() - call.since) / 1000))
        var s = secs % 60
        clock = Math.floor(secs / 60) + ":" + (s < 10 ? "0" : "") + s
    }
    function syncHolds(wanted) {
        if (wanted && !held) {
            held = true
            proximity.acquire(sheet)
        } else if (!wanted && held) {
            held = false
            proximity.release(sheet)
        }
    }
    onClosed: {
        syncHolds(false)
        callAudio.stop()
        audioFor = ""
    }
    // The state is read from call itself, never from phase, ended or ringing: in Qt 4.8 this
    // handler can run before those bindings catch up with the same change. Reading them left the
    // clock at 0:00 and the sheet on "Call ended" on the Q10 (HyperBerry 0.0.1.181).
    onCallChanged: {
        var st = call.state || ""
        if (st == "") {
            return
        }
        syncHolds(st == "calling" || st == "connecting" || st == "active")
        if (st == "ended") {
            if (call.since > 0) {
                refreshClock()
            }
            ticker.stop()
            closer.start()
            callAudio.stop()
            audioFor = ""
            return
        }
        if ((st == "connecting" || st == "active") && audioFor != call.id) {
            audioFor = call.id
            callAudio.start(call.id)
        }
        closer.stop()
        who = chats.chat(call.chat)
        if (st == "active" && call.since > 0) {
            refreshClock()
            ticker.start()
        } else {
            ticker.stop()
        }
        if (!opened) {
            callAudio.muted = false
            callAudio.speaker = false
            open()
        }
    }
    attachedObjects: [
        Timer {
            id: ticker
            interval: 1000
            onTimeout: sheet.refreshClock()
        },
        Timer {
            id: closer
            interval: 2000
            singleShot: true
            onTimeout: sheet.close()
        }
    ]
    // A Page with no field, so the Q10's keys have nowhere to go; the composer under the sheet is
    // in the window below and gets none (bb10-pitfalls: keys follow the focused control).
    Page {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: Color.Black
            layout: DockLayout {}
            Container {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Bottom
                preferredWidth: sheet.px(720)
                preferredHeight: sheet.px(720) + sheet.extra
                layout: AbsoluteLayout {}
                // Fills a tall screen above the background picture, whose foot is black like the rest
                // below.
                Container {
                    layoutProperties: AbsoluteLayoutProperties {
                        positionX: 0
                        positionY: 0
                    }
                    visible: sheet.shift > 0
                    preferredWidth: sheet.px(720)
                    preferredHeight: sheet.shift
                    background: Color.create("#13222a") // the stock #1b132a in the primary colour's hue (#24A1DE)
                }
                ImageView {
                    layoutProperties: AbsoluteLayoutProperties {
                        positionY: sheet.shift
                    }
                    preferredWidth: sheet.px(720)
                    preferredHeight: sheet.px(720)
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/call_bg.png"
                }
                // The picture fills the area above the controls and fades to black at its foot.
                ImageView {
                    visible: sheet.hasPhoto
                    preferredWidth: sheet.px(720)
                    preferredHeight: (sheet.ringing ? sheet.px(510) : sheet.px(415)) + sheet.shift
                    scalingMethod: ScalingMethod.AspectFill
                    loadEffect: ImageViewLoadEffect.None
                    imageSource: sheet.hasPhoto ? "file://" + sheet.who.photo : ""
                }
                ImageView {
                    visible: sheet.hasPhoto
                    layoutProperties: AbsoluteLayoutProperties {
                        positionY: (sheet.ringing ? sheet.px(250) : sheet.px(155)) + sheet.shift
                    }
                    preferredWidth: sheet.px(720)
                    preferredHeight: sheet.px(260)
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/call_fade.png"
                }
                Label {
                    layoutProperties: AbsoluteLayoutProperties {
                        positionX: sheet.px(30)
                        positionY: (sheet.ringing ? sheet.px(284) : sheet.px(312)) + sheet.shift
                    }
                    preferredWidth: sheet.timed ? sheet.px(450) : sheet.px(650)
                    multiline: false
                    text: sheet.who.name || qsTr("Unknown")
                    textFormat: TextFormat.Plain
                    textStyle.base: nav.emojiStyle
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: sheet.ringing ? 200 : 170
                    textStyle.color: Color.White
                }
                Label {
                    layoutProperties: AbsoluteLayoutProperties {
                        positionX: sheet.px(29)
                        positionY: (sheet.ringing ? sheet.px(360) : sheet.px(372)) + sheet.shift
                    }
                    // Short of the clock when it shows.
                    preferredWidth: sheet.timed ? sheet.px(430) : sheet.px(650)
                    text: sheet.line
                    textFormat: TextFormat.Plain
                    textStyle.color: Color.White
                }
                Label {
                    visible: sheet.timed
                    layoutProperties: AbsoluteLayoutProperties {
                        positionX: sheet.px(467)
                        positionY: sheet.px(373) + sheet.shift
                    }
                    preferredWidth: sheet.px(220)
                    text: sheet.clock
                    textFormat: TextFormat.Plain
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: 95
                    textStyle.textAlign: TextAlign.Right
                    textStyle.color: Color.White
                }
                CallRinging {
                    id: ringControls
                    layoutProperties: AbsoluteLayoutProperties {
                        positionY: sheet.shift
                    }
                    visible: sheet.ringing
                    onAnswered: engine.request("call.answer", {"id": sheet.call.id})
                    onDeclined: engine.request("call.reject", {"id": sheet.call.id})
                    // Declines, then the caller's chat opens with the composer, as a contact page's
                    // Message does, for a chat we may not have yet.
                    onMessage: {
                        engine.request("call.reject", {"id": sheet.call.id})
                        sheet.close()
                        nav.openRow(sheet.callerRow())
                    }
                }
            }
            CallPanel {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Bottom
                visible: !sheet.ringing
                ended: sheet.ended
                lift: sheet.lift
                onHangup: engine.request("call.hangup", {"id": sheet.call.id})
            }
            // The header band, as the stock app's, under no status bar here.
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Top
                preferredHeight: sheet.px(55)
                background: Color.create("#0b1419") // the stock #0f0b19 in the primary colour's hue (#24A1DE)
                // Padding, not a margin: a margin doesn't count from the container's edge.
                leftPadding: sheet.px(30)
                layout: DockLayout {}
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    text: qsTr("Telegram: voice call")
                    textFormat: TextFormat.Plain
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: 85
                    textStyle.color: Color.create("#e8e8e8")
                }
            }
            // At the ear: black, and every touch stops here, so a cheek presses nothing.
            Container {
                visible: sheet.atEar
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                background: Color.Black
                onTouch: {}
            }
        }
    }
}
