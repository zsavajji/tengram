import bb.cascades 1.4
import bb.multimedia 1.4

// The chat page's one voice-note player (step 4.3b). Starting a note stops the one playing; the
// page's destruction ends it. The bubbles read playingId, loadingId, paused, posMs and durMs
// through the page's list. While a note plays
// the proximity sensor is held, and at the ear the note moves to the handset (step 7.5).
MediaPlayer {
    id: player
    property string jid
    property string playingId: ""
    property string loadingId: ""
    // The voice.play request in flight: a later tap replaces it, and its reply is dropped.
    property string req: ""
    // Where a seek on a note not yet playing goes, once its file is ready (ready()): the tap starts it there.
    property int pendingMs: 0
    property bool paused: mediaState == MediaState.Paused
    property int posMs: Number(position) || 0
    property int durMs: Number(duration) || 0
    // Playing now: the sensor's acquire/release follow this one flag, so they stay paired.
    property bool held: playingId != "" && !paused
    // The handle may change while playing ("seamlessly", MediaPlayer.hpp); 0 is the default output.
    property bool atEar: held && proximity.near && settings.earSensor // off in Settings for a sensor that misreads
    // Held by the player itself, so its destruction with the chat page releases it (Proximity).
    onHeldChanged: held ? proximity.acquire(player) : proximity.release(player)
    onAtEarChanged: audioManagerHandle = atEar ? proximity.earHandle() : 0
    // Anything but a failed download, which the bubble shows as retry.
    signal failed
    // No engine: the request never went.
    signal offline
    statusInterval: 100
    onPlaybackCompleted: {
        stop()
        playingId = ""
    }

    function toggle(id) {
        if (playingId == id) {
            if (paused) {
                play()
            } else {
                pause()
            }
            return
        }
        stop()
        playingId = ""
        pendingMs = 0 // a seek waiting on another note is not this one's
        req = engine.request("voice.play", { "chat": jid, "id": id })
        if (req == "") {
            offline()
            return
        }
        loadingId = id
    }
    // A touch on a bubble's waveform: the note playing or paused moves there; another starts there, as a tap
    // starts it (toggle), and pendingMs holds the point only while that request is in flight.
    function seek(id, ms) {
        if (playingId == id) {
            seekTime(ms)
            return
        }
        toggle(id)
        pendingMs = loadingId == id ? ms : 0
    }
    // ChatController's voiceReady: the WAV of the request in flight.
    function ready(id, path) {
        if (id != req || loadingId == "") {
            return
        }
        req = ""
        sourceUrl = "file://" + path
        playingId = loadingId
        loadingId = ""
        var at = pendingMs
        pendingMs = 0
        if (play() != MediaError.None) {
            playingId = ""
            failed()
        } else if (at > 0) {
            seekTime(at)
        }
    }
    function fail(code) {
        loadingId = ""
        req = ""
        pendingMs = 0
        if (code != "download_failed") {
            failed()
        }
    }
}
