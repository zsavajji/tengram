import bb.cascades 1.4

// Search in a chat (step 5.10), over the messages: the field, the match it is on out of all of
// them, and ▲ ▼ ✕. Enter, or ▲, goes to the next older match, ▼ to the next newer one. The search
// runs on Telegram's server (find, the page asks the engine's chat.find), over the chat's whole history and
// with Telegram's own matching, as its app does, and not over the cache; the answer comes to take(). It runs on Enter and not
// on each key: a jump can load days of messages, and typing should not start that. Typing drops the last search, so the
// first Enter after it searches again and lands on the newest match.
Container {
    id: bar
    // The chat's jid, set by the page.
    property string jid
    // The ids found, newest first, and the one the page is on (-1 before the first jump).
    property variant matches: []
    property int at: -1
    property bool searched: false
    // The question is out: a spinner shows in the counter's place until take() has the answer. Typing or closing forgets it.
    property bool asking: false
    // The field's keys also reach the page's KeyListener, which would hand them to the composer.
    property bool typing: field.focused
    // Whether ▲ and ▼ have a match their way; one with none is dimmed (Q10, 0.0.1.123: they
    // never changed). Before a search, ▲ (as Enter) searches once there is text. Set from
    // textChanging: the field's text notifies only when it is committed.
    property bool hasText: false
    property bool canOlder: searched ? at + 1 < matches.length : hasText
    property bool canNewer: at > 0
    signal jump(string id)
    // Asks the server for the messages with this text; the page answers with take().
    signal find(string text)
    signal closed()
    visible: false
    horizontalAlignment: HorizontalAlignment.Fill
    background: ui.palette.plain
    // As the reply bar's: the field sat against the screen's edge (Q10, 0.0.1.122).
    leftPadding: ui.du(1.5)
    topPadding: ui.du(0.5)
    bottomPadding: ui.du(0.5)
    layout: StackLayout {
        orientation: LayoutOrientation.LeftToRight
    }
    function open() {
        visible = true
        field.requestFocus()
    }
    // The Q10 has no Escape: the ✕ is the way out. It takes the tint off through closed().
    function close() {
        field.text = ""
        matches = []
        at = -1
        searched = false
        asking = false
        visible = false
        closed()
    }
    // The server's answer: the ids found, newest first. Nothing when the text changed since (asking is off then), so a late
    // answer never lands on another text.
    function take(ids) {
        if (!asking) {
            return
        }
        asking = false
        matches = ids ? ids : []
        searched = true
        at = -1
        older()
    }
    function older() {
        if (field.text.trim() == "") {
            return
        }
        if (!searched) {
            if (!asking) {
                asking = true
                find(field.text.trim())
            }
            return
        }
        if (at + 1 < matches.length) {
            at = at + 1
            jump(matches[at])
        }
    }
    function newer() {
        if (at > 0) {
            at = at - 1
            jump(matches[at])
        }
    }
    TextField {
        id: field
        verticalAlignment: VerticalAlignment.Center
        layoutProperties: StackLayoutProperties {
            spaceQuota: 1
        }
        hintText: qsTr("Search in chat")
        input {
            submitKey: SubmitKey.Search
            // Enter again steps on, and the field keeps the keyboard.
            submitKeyFocusBehavior: SubmitKeyFocusBehavior.Keep
            onSubmitted: bar.older()
        }
        onTextChanging: {
            bar.hasText = text.trim() != ""
            bar.matches = []
            bar.at = -1
            bar.searched = false
            bar.asking = false
        }
    }
    Label {
        verticalAlignment: VerticalAlignment.Center
        leftMargin: ui.du(1)
        visible: !bar.asking
        text: !bar.searched ? "" : bar.matches.length == 0 ? "0/0" : (bar.at + 1) + "/" + bar.matches.length
        textFormat: TextFormat.Plain
        textStyle.color: ui.palette.secondaryTextOnPlain
    }
    // In the counter's place while the server's answer is out (the user, 2026-11-25: a spinner, not "…").
    ActivityIndicator {
        visible: bar.asking
        running: bar.asking
        verticalAlignment: VerticalAlignment.Center
        leftMargin: ui.du(1)
        preferredWidth: ui.du(5)
        preferredHeight: ui.du(5)
    }
    // Each is a touch target the size of an action column (9 du), as the chat list's ✕. The Classic's trackpad reaches them:
    // FocusPolicy.Key, not KeyAndTouch, so a touch leaves the focus, and the keyboard, in the field; one with the focus is lit
    // as the pressed colour, and a click (Enter) does what a tap does.
    TapArea {
        id: olderBtn
        verticalAlignment: VerticalAlignment.Center
        preferredWidth: ui.du(9)
        preferredHeight: ui.du(9)
        layout: DockLayout {
        }
        focusPolicy: FocusPolicy.Key
        background: olderBtn.focused ? Color.create(nav.pressColor) : Color.Transparent
        onClicked: bar.older()
        Label {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            // From the phone's fallback font (DejaVu Sans), as the ✕ is.
            text: "▲"
            textFormat: TextFormat.Plain
            textStyle.color: bar.canOlder ? ui.palette.textOnPlain : ui.palette.secondaryTextOnPlain
            opacity: bar.canOlder ? 1 : 0.4
        }
    }
    TapArea {
        id: newerBtn
        verticalAlignment: VerticalAlignment.Center
        preferredWidth: ui.du(9)
        preferredHeight: ui.du(9)
        layout: DockLayout {
        }
        focusPolicy: FocusPolicy.Key
        background: newerBtn.focused ? Color.create(nav.pressColor) : Color.Transparent
        onClicked: bar.newer()
        Label {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            text: "▼"
            textFormat: TextFormat.Plain
            textStyle.color: bar.canNewer ? ui.palette.textOnPlain : ui.palette.secondaryTextOnPlain
            opacity: bar.canNewer ? 1 : 0.4
        }
    }
    TapArea {
        id: closeBtn
        verticalAlignment: VerticalAlignment.Center
        preferredWidth: ui.du(9)
        preferredHeight: ui.du(9)
        layout: DockLayout {
        }
        focusPolicy: FocusPolicy.Key
        background: closeBtn.focused ? Color.create(nav.pressColor) : Color.Transparent
        onClicked: bar.close()
        Label {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            text: "✕"
            textFormat: TextFormat.Plain
            textStyle.color: ui.palette.secondaryTextOnPlain
        }
    }
}
