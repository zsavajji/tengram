import bb.multimedia 1.4

// The chat page's voice-note recorder (step 4.4b). ChatController owns the request side, so a page
// that goes away mid-recording still cancels. The recording is stopped (reset() finishes the
// file) before voice.send, so the engine finds the tail on disk.
AudioRecorder {
    id: rec
    property bool recording: false
    // Record failed to start: nothing is running.
    signal failed
    // m:ss, for the composer's status line. A function: Qt 4.8 refuses a binding written as a
    // block with a var in it (0.0.1.77 couldn't open a chat).
    property string clock: mmss(duration)
    statusInterval: 200

    function mmss(ms) {
        var s = Math.floor((Number(ms) || 0) / 1000)
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }
    function start() {
        var path = ctl.startVoice()
        if (path == "") {
            failed()
            return
        }
        outputUrl = "file://" + path
        if (record() != MediaError.None) {
            ctl.cancelVoice()
            failed()
            return
        }
        recording = true
    }
    function finish() {
        var ms = Number(duration) || 0
        reset()
        recording = false
        // Under a second is a mis-tap: nothing is sent.
        if (ms < 1000) {
            ctl.cancelVoice()
        } else {
            ctl.sendVoice()
        }
    }
    function discard() {
        reset()
        recording = false
        ctl.cancelVoice()
    }
    // ChatController dropped the recording (minimised): the engine knows already.
    function dropped() {
        reset()
        recording = false
    }
}
