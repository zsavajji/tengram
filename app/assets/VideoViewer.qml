import bb.cascades 1.4
import bb.multimedia 1.4
import hyperberry 1.0

// A received video in the app, full screen on black, through MediaPlayer into a ForeignWindowControl as the story
// viewer does. The stock action bar (Back) is overlaid on the picture (ChromeVisibility.Overlay), and above it a bar
// of ours: the timeline (a touch or a drag seeks, a dot marks the place), play/pause and the time. After 3 s of
// playing both go, ours sliding off the bottom, and a tap on the picture brings them back (a tap while they show
// sends them away). A GIF (a video that plays by itself) loops and has only the stock bar. path is the file's
// absolute path, ratio its height over its width and gif the kind, set right after createObject(); start() plays it,
// once the page is pushed. When the player can't play, the system's app gets the file, as every video did before
// this page.
Page {
    id: page
    property string path
    property real ratio: 0.75
    property bool gif: false
    // Read by main.qml's nav: no peeking while this page is on top, or a drag on the timeline slides the page away.
    property bool noPeek: true
    property bool handedOver: false
    // The bars are on screen (or sliding in). The stock bar is its own flag: on the way out ours goes first, behind
    // it, and it follows when ours is gone (slideOut.onEnded).
    property bool barShown: true
    property bool stockShown: true
    // The timeline is under a finger, or was let go and the player hasn't reported its new place yet: the fill
    // stays at `at` (0 to 1) instead of jumping back to the old position.
    property bool scrubbing: false
    property bool settling: false
    property real at: 0
    property bool playing: player.mediaState == MediaState.Started
    // The length in ms, 0 until the player knows it.
    property real dur: player.duration > 0 ? player.duration : 0
    property real frac: page.scrubbing || page.settling ? page.at
                        : page.dur > 0 ? Math.min(player.position / page.dur, 1) : 0
    // The timeline's side margin and its dot's size: the finger's x is measured inside the margin and half a dot, so the
    // dot is under the finger.
    property real side: ui.du(2)
    property real dot: ui.du(3)
    // The stock action bar's height, which our bar stands above: 97 px on the Q10 (a screenshot of the Back / Install bar), 11 du, and a du of slack.
    property real abar: ui.du(12)
    actionBarVisibility: page.stockShown ? ChromeVisibility.Overlay : ChromeVisibility.Hidden

    function clock(ms) {
        var s = Math.floor(ms / 1000)
        var m = Math.floor(s / 60)
        s = s % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }
    // Called by nav.showVideo once the page is pushed. Not on the control's bind: the player creates the window when
    // it prepares, and the control binds to it after, so waiting for the bind would never start anything.
    function start() {
        player.sourceUrl = "file://" + page.path
        page.check(player.play())
        idle.start()
    }
    function check(e) {
        if (e != 0) {
            page.fail("error " + e)
        }
    }
    function fail(why) {
        console.log("chat video: " + why)
        if (!page.handedOver) {
            page.handedOver = true
            opener.openFile(page.path)
        }
    }
    function toggle() {
        page.check(page.playing ? player.pause() : player.play())
        page.wake()
    }
    // The bar is here, and goes again after 3 s.
    function wake() {
        if (!page.barShown) {
            page.barShown = true
            page.stockShown = true
            slideIn.play()
        }
        idle.stop()
        idle.start()
    }
    function hideBar() {
        idle.stop()
        if (page.barShown) {
            page.barShown = false
            if (page.gif) {
                page.stockShown = false
            } else {
                slideOut.play()
            }
        }
    }
    function tapped() {
        if (page.barShown) {
            page.hideBar()
        } else {
            page.wake()
        }
    }
    // A finger on the timeline; the player is asked at the end, not at every move. The drag goes on until the touch
    // ends (up or cancel), wherever the finger drifts: a thumb leaves a thin bar at once, and the bar's touchExit
    // is not an end (it was, in 0.1.0.290, and the dot stopped under the finger).
    function scrub(event, width) {
        if (event.isDown() || event.isMove()) {
            if (event.isDown()) {
                console.log("video scrub: down at " + Math.round(event.windowX) + " of " + Math.round(width))
            }
            page.scrubbing = true
            page.at = Math.max(0, Math.min(1, (event.windowX - page.side - page.dot / 2) / (width - 2 * page.side - page.dot)))
            stale.start()
        } else {
            console.log("video scrub: " + (event.isCancel() ? "cancel" : "up") + " at " + Math.round(page.at * 1000) / 1000)
            page.release()
        }
    }
    // The touch ended: one seek.
    function release() {
        stale.stop()
        if (!page.scrubbing) {
            return
        }
        page.scrubbing = false
        page.settling = true
        if (player.seekTime(Math.round(page.at * page.dur)) != 0) {
            page.settling = false
        }
        page.wake()
    }

    attachedObjects: [
        MediaOpener {
            id: opener
        },
        MediaPlayer {
            id: player
            videoOutput: VideoOutput.PrimaryDisplay
            windowId: surface.windowId
            windowGroupId: surface.windowGroup
            statusInterval: 250
            repeatMode: page.gif ? RepeatMode.Track : RepeatMode.None
            onPositionChanged: page.settling = false
            onError: page.fail("error " + mediaError + " at " + position)
        },
        // A drag whose end never came (no up, no cancel) ends 1.5 s after its last move, so the dot is never left stuck.
        Timer {
            id: stale
            interval: 1500
            singleShot: true
            onTimeout: page.release()
        },
        // 3 s without a touch, while it plays and no finger is on the timeline: the bars go. Paused, they stay.
        Timer {
            id: idle
            interval: 3000
            singleShot: true
            onTimeout: {
                if (page.playing && !page.scrubbing) {
                    page.hideBar()
                }
            }
        }
    ]
    Container {
        background: Color.Black
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        layout: DockLayout {}
        attachedObjects: LayoutUpdateHandler {
            id: area
        }
        // The picture, at its own shape, as big as the screen lets it be.
        ForeignWindowControl {
            id: surface
            windowId: "chatVideo"
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            property real fit: Math.min(area.layoutFrame.width, area.layoutFrame.height / page.ratio)
            preferredWidth: fit
            preferredHeight: fit * page.ratio
            visible: boundToWindow
            updatedProperties: WindowProperty.Size | WindowProperty.Position | WindowProperty.Visible
        }
        // Taps: the bars away and back. Under our bar, which takes its own.
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            gestureHandlers: TapHandler {
                onTapped: page.tapped()
            }
        }
        // Our bar, over the picture, standing above the stock bar: the wrapper holds the stock bar's place as padding (a
        // margin on a docked control wasn't honoured, 0.1.0.291: the stock bar covered the buttons) and slides as one.
        Container {
            id: bar
            property real h: 0
            visible: !page.gif
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Bottom
            bottomPadding: page.abar
            attachedObjects: LayoutUpdateHandler {
                onLayoutFrameChanged: bar.h = layoutFrame.height
            }
            animations: [
                TranslateTransition {
                    id: slideOut
                    toY: bar.h
                    duration: 250
                    // Not if a tap brought the bars back meanwhile.
                    onEnded: {
                        if (!page.barShown) {
                            page.stockShown = false
                        }
                    }
                },
                TranslateTransition {
                    id: slideIn
                    toY: 0
                    duration: 200
                }
            ]
            // The dark band, with the timeline, play/pause and the time.
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                background: Color.create(0.0, 0.0, 0.0, 0.55)
                // The timeline: the whole 8 du band takes the touch, the track is the thin line in it, the fill is the
                // accent and the dot sits at its end. The finger's x is a window x (as CallSlider's), which is the
                // bar's own as it spans the screen.
                Container {
                    id: line
                    property real w: 0
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.du(8)
                    layout: DockLayout {}
                    attachedObjects: LayoutUpdateHandler {
                        onLayoutFrameChanged: line.w = layoutFrame.width
                    }
                    onTouch: page.scrub(event, line.w)
                    onTouchExit: console.log("video scrub: the finger left the bar")
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: page.side
                        rightMargin: page.side
                        preferredHeight: ui.px(6)
                        background: Color.create(1.0, 1.0, 1.0, 0.25)
                    }
                    // The fill, the dot and the rest share the width by quotas: the dot's own width is taken first, so
                    // the fill ends at the dot's middle.
                    // Without implicit layout animations: a change of quota is animated by default, and the dot trailed
                    // the finger (the SDK's note on Control::implicitLayoutAnimationsEnabled).
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: page.side
                        rightMargin: page.side
                        implicitLayoutAnimationsEnabled: false
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        Container {
                            verticalAlignment: VerticalAlignment.Center
                            preferredHeight: ui.px(6)
                            implicitLayoutAnimationsEnabled: false
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: Math.max(page.frac, 0.001)
                            }
                            background: ui.palette.primary
                        }
                        ImageView {
                            verticalAlignment: VerticalAlignment.Center
                            preferredWidth: page.dot
                            preferredHeight: page.dot
                            implicitLayoutAnimationsEnabled: false
                            imageSource: "asset:///images/dot.png"
                            filterColor: ui.palette.primary
                        }
                        Container {
                            implicitLayoutAnimationsEnabled: false
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: Math.max(1 - page.frac, 0.001)
                            }
                        }
                    }
                }
                // Play/pause (a box 10 du square, the touch target) and the time.
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Container {
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: ui.du(10)
                        preferredHeight: ui.du(10)
                        layout: DockLayout {}
                        ImageView {
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment: VerticalAlignment.Center
                            preferredWidth: ui.du(5)
                            preferredHeight: ui.du(5)
                            imageSource: page.playing ? "asset:///images/pause.png" : "asset:///images/play.png"
                            filterColor: ui.palette.primary
                        }
                        gestureHandlers: TapHandler {
                            onTapped: page.toggle()
                        }
                    }
                    Label {
                        verticalAlignment: VerticalAlignment.Center
                        text: page.clock(page.frac * page.dur) + " / " + page.clock(page.dur)
                        textStyle.color: Color.White
                    }
                }
            }
        }
    }
}
