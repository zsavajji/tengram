import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0

// Send location (⋮ → Location, LocationAction.qml): a map drawn by Telegram (MapPicker, map.get) with a pin at its centre.
// It opens where the phone last was, at once, and moves to where the phone is when the GPS finds it, which indoors may be
// never (the user, 2026-11-27): the page says that it is still looking and is used meanwhile. Dragging the map moves the
// point under the pin; + and − zoom, the third button goes back to the phone's position; the field at the top finds an
// address or a place by name (OpenStreetMap's search, through the engine) and a tap on a result takes the map there.
// Send location sends the point under the pin (the phone's own, with its accuracy, until the map is moved); Share live sends
// the phone's position as a live location for a time picked from a list, once the phone is found. A refusal shows as a
// toast on the chat page, which this page has gone back to by then; a send that did not go out (the engine is down) keeps
// the page with a toast.
Page {
    id: page
    property string jid
    property int topic: 0
    property string thread: ""
    // The message the location replies to, "" for none.
    property string quote: ""
    signal sent()
    // Read by main.qml's nav: no peeking while this page is on top. A drag of the map from left to right was taken for the
    // swipe that slides the page away, and closed it (the user, 2026-11-27); the bar's back arrow leaves the page.
    property bool noPeek: true
    // The box's size, for the drag's share of the picture (its LayoutUpdateHandler).
    property real boxW: 0
    property real boxH: 0
    // A drag: where the finger went down, and the offset the picture keeps from the drags before this one, until the new
    // centre's map takes its place (picker's pictureChanged puts it back).
    property bool dragging: false
    property real downX: 0
    property real downY: 0
    property real baseX: 0
    property real baseY: 0
    // The map the view shows now, "" for none yet: picker.picture once its file is loaded (the tracker below). The pin, the
    // chip and the map's box follow this one, so nothing is drawn over a map that is not there yet.
    property string shownPicture: ""
    // What goes on or went wrong, said under the map (or in the middle of the page while there is no map): a problem, with
    // its one button, or that the phone's position is still being looked for. With no map to look at, or after 30 s, it also
    // says why it takes long and what to do meanwhile (a phone that has never had a position here starts with no map: the
    // user's Q10, 2026-11-27, indoors).
    property string noteText: picker.problem == "location_off" ? qsTr("Location Services are off. Turn them on to send where you are.")
                              : picker.problem == "no_fix" ? qsTr("Couldn't ask for your position. Try again in a moment.")
                              : picker.problem == "map" ? qsTr("Couldn't load the map.")
                              : !picker.searching || picker.located ? ""
                              : picker.slow || page.shownPicture == "" ? qsTr("Looking for your position. It comes from GPS satellites alone, which takes a few minutes and a view of the sky. Meanwhile you can search for an address above, or move the map.")
                              : qsTr("Looking for your position…")
    // The new centre's map is drawn: the picture goes back where it belongs, at the moment it changes (put back when the
    // answer came, the old map jumped to the middle and the new one took its place a moment later).
    function settle() {
        page.baseX = 0
        page.baseY = 0
        if (!page.dragging) {
            map.translationX = 0
            map.translationY = 0
        }
    }
    // The live periods' list is filled at its first show: a SystemListDialog has no onCreationCompleted (ChatList.qml).
    property bool periodsFilled: false
    titleBar: PageBar {
        title: qsTr("Location")
    }
    // Called by LocationAction once jid is set: the map's request names the chat.
    function open() {
        picker.start(page.jid)
    }
    function send() {
        if (!picker.sendPoint(page.jid, page.topic, page.thread, page.quote)) {
            offline.show()
            return
        }
        page.sent()
        nav.pop()
    }
    function sendLive(period) {
        if (!picker.sendLive(page.jid, page.topic, page.thread, page.quote, period)) {
            offline.show()
            return
        }
        page.sent()
        nav.pop()
    }
    function askPeriod() {
        if (!page.periodsFilled) {
            page.periodsFilled = true
            periods.appendItem(qsTr("15 minutes"))
            periods.appendItem(qsTr("1 hour"))
            periods.appendItem(qsTr("8 hours"))
            periods.appendItem(qsTr("Until I turn it off"))
        }
        periods.show()
    }
    actions: [
        ActionItem {
            title: qsTr("Send location")
            imageSource: "file:///usr/share/icons/bb_action_sendtextmessage.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: page.shownPicture != ""
            onTriggered: page.send()
        },
        ActionItem {
            title: qsTr("Share live")
            imageSource: "file:///usr/share/icons/ic_map_all.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: picker.located
            onTriggered: page.askPeriod()
        }
    ]
    attachedObjects: [
        MapPicker {
            id: picker
            link: engine
            // The same file again (a drag back to a centre it has shown) loads nothing new: it is drawn already.
            onPictureChanged: {
                if (picker.picture == page.shownPicture) {
                    page.settle()
                }
            }
            // What the search found, in the list's model.
            onPlacesChanged: {
                placesModel.clear()
                placesModel.append(picker.places)
            }
        },
        // Loads the new map's file; the view takes the picture only once it is ready, and is moved back in that same step.
        // A file that doesn't load leaves the old map where the drag left it, which is still right for the point under the pin.
        ImageTracker {
            id: tracker
            imageSource: picker.picture == "" ? "" : "file://" + picker.picture
            onStateChanged: {
                if (state == ResourceState.Loaded) {
                    map.image = tracker.image
                    page.shownPicture = picker.picture
                    page.settle()
                }
            }
        },
        SystemToast {
            id: offline
            body: qsTr("Not sent: Tengram is reconnecting. Try again in a moment.")
        },
        // A tap chooses (dismissOnSelection, as TimerAction's), so the confirm button goes.
        SystemListDialog {
            id: periods
            title: qsTr("Share my live location for")
            confirmButton.label: ""
            cancelButton.label: qsTr("Cancel")
            dismissOnSelection: true
            onFinished: {
                if ((value == SystemUiResult.ItemSelection || value == SystemUiResult.ConfirmButtonSelection)
                        && selectedIndices.length > 0) {
                    page.sendLive([900, 3600, 28800, 0][selectedIndices[0]])
                }
            }
        }
    ]
    Container {
        background: Color.create(nav.pageColor)
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        // An address or a place's name: Enter (the keyboard's Search) asks, the places found take the map's room, and a tap
        // on one takes the map there. Emptied, the field gives the map back.
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            leftPadding: ui.du(1)
            rightPadding: ui.du(1)
            topPadding: ui.du(1)
            bottomPadding: ui.du(1)
            TextField {
                id: field
                hintText: qsTr("Search an address or a place")
                input.submitKey: SubmitKey.Search
                input.onSubmitted: picker.find(field.text)
                onTextChanging: {
                    if (text == "") {
                        picker.clearPlaces()
                    }
                }
            }
        }
        Label {
            visible: picker.placesNote != ""
            horizontalAlignment: HorizontalAlignment.Fill
            leftMargin: ui.du(2)
            rightMargin: ui.du(2)
            multiline: true
            text: picker.placesNote == "none" ? qsTr("No place found")
                  : picker.placesNote == "offline" ? qsTr("No answer from the place search. Check the connection.")
                  : qsTr("The place search failed. Try again.")
            textFormat: TextFormat.Plain
            textStyle.textAlign: TextAlign.Center
            textStyle.color: ui.palette.secondaryTextOnPlain
        }
        ListView {
            id: found
            property string pressColor: nav.pressColor
            horizontalAlignment: HorizontalAlignment.Fill
            visible: picker.places.length > 0
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            dataModel: ArrayDataModel {
                id: placesModel
            }
            listItemComponents: ListItemComponent {
                Container {
                    id: item
                    horizontalAlignment: HorizontalAlignment.Fill
                    background: item.ListItem.active ? Color.create(item.ListItem.view.pressColor) : Color.Transparent
                    // A list item is as wide as what it holds unless something in it asks for the row's width: the two lines are
                    // one column with a quota in a left-to-right row, as the country list's name is (alone, the rows and their
                    // dividers ended with the text, the user's screenshot, 2026-11-27).
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        minHeight: ui.du(9)
                        leftPadding: ui.du(2)
                        rightPadding: ui.du(2)
                        topPadding: ui.du(1)
                        bottomPadding: ui.du(1)
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        Container {
                            verticalAlignment: VerticalAlignment.Center
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                            Label {
                                bottomMargin: 0
                                multiline: false
                                text: ListItemData.title
                                textFormat: TextFormat.Plain
                            }
                            Label {
                                topMargin: 0
                                visible: ListItemData.detail != ""
                                multiline: true
                                text: ListItemData.detail
                                textFormat: TextFormat.Plain
                                textStyle.base: SystemDefaults.TextStyles.SmallText
                                textStyle.color: ui.palette.secondaryTextOnPlain
                            }
                        }
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        preferredHeight: ui.px(2)
                        background: ui.palette.plain
                    }
                }
            }
            onTriggered: picker.goTo(indexPath[0])
        }
        // The places are OpenStreetMap's (its Nominatim search), which asks to be named where they show.
        Label {
            visible: picker.places.length > 0
            horizontalAlignment: HorizontalAlignment.Center
            text: "© OpenStreetMap contributors"
            textFormat: TextFormat.Plain
            textStyle.base: SystemDefaults.TextStyles.SmallText
            textStyle.color: ui.palette.secondaryTextOnPlain
        }
        // The map's room, while no places are listed.
        Container {
            visible: picker.places.length == 0
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            horizontalAlignment: HorizontalAlignment.Fill
            layout: DockLayout {}
            // The map, and under it the note of what goes on or went wrong: under, not over, so the note never covers the
            // map's buttons (on the Classic's 720 px it lay on the lowest one). With no map yet the box is gone and the column
            // is a spinner over the note, in the middle of the room (a small spinner in the corner of an empty page read as
            // a page that was broken, the user, 2026-11-27).
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: page.shownPicture == "" ? VerticalAlignment.Center : VerticalAlignment.Fill
                ActivityIndicator {
                    visible: page.shownPicture == "" && picker.busy
                    running: page.shownPicture == "" && picker.busy
                    horizontalAlignment: HorizontalAlignment.Center
                    preferredWidth: ui.du(12)
                    preferredHeight: ui.du(12)
                }
                Container {
                    id: box
                    visible: page.shownPicture != ""
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    horizontalAlignment: HorizontalAlignment.Fill
                    layout: DockLayout {}
                    attachedObjects: LayoutUpdateHandler {
                        onLayoutFrameChanged: {
                            page.boxW = layoutFrame.width
                            page.boxH = layoutFrame.height
                        }
                    }
                    // The map follows the finger (touches on what is over it bubble up to here, as on CallSlider's track); let
                    // go, the centre moves by as much (MapPicker.moved) and the picture stays where it was left until the new
                    // centre's map is drawn (settle). The square picture is drawn at the box's longer side (AspectFill), so
                    // both shares are of that side. Under 2 du it was a tap, and nothing moves.
                    onTouch: {
                        if (event.isDown()) {
                            page.dragging = true
                            page.downX = event.windowX
                            page.downY = event.windowY
                        } else if (event.isMove() && page.dragging) {
                            map.translationX = page.baseX + event.windowX - page.downX
                            map.translationY = page.baseY + event.windowY - page.downY
                        } else if ((event.isUp() || event.isCancel()) && page.dragging) {
                            page.dragging = false
                            // The last move's offset, not the up's position: a cancel's may be anywhere.
                            var dx = map.translationX - page.baseX
                            var dy = map.translationY - page.baseY
                            var side = Math.max(page.boxW, page.boxH)
                            if (Math.max(Math.abs(dx), Math.abs(dy)) < ui.du(2) || side <= 0) {
                                map.translationX = page.baseX
                                map.translationY = page.baseY
                            } else {
                                page.baseX = map.translationX
                                page.baseY = map.translationY
                                picker.moved(dx / side, dy / side)
                            }
                        }
                    }
                    // Its picture is the tracker's, set when the file is loaded: no imageSource here.
                    ImageView {
                        id: map
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFill
                        loadEffect: ImageViewLoadEffect.None
                        // No implicit animation: the picture follows the finger, and jumps back at once when the new map is drawn.
                        attachedObjects: ImplicitAnimationController {
                            enabled: false
                        }
                    }
                    // The pin's tip is the image's centre (scripts/gen-pin.py): it points at the map's centre, the point sent.
                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: ui.du(12)
                        preferredHeight: ui.du(12)
                        scalingMethod: ScalingMethod.AspectFit
                        loadEffect: ImageViewLoadEffect.None
                        imageSource: "asset:///images/map_pin.png"
                    }
                    // What Send location sends, on a chip of black at 50 % as a video's length (MediaBubble), 1 du in from the
                    // corner (padding: a docked control's margins are ignored).
                    Container {
                        horizontalAlignment: HorizontalAlignment.Left
                        verticalAlignment: VerticalAlignment.Top
                        leftPadding: ui.du(1)
                        topPadding: ui.du(1)
                        Container {
                            background: Color.create("#80000000")
                            leftPadding: ui.du(1)
                            rightPadding: ui.du(1)
                            topPadding: ui.du(1)
                            bottomPadding: ui.du(1)
                            Label {
                                multiline: false
                                text: picker.mine ? qsTr("Your position") + (picker.accuracyText != "" ? " · " + qsTr("accurate to %1").arg(picker.accuracyText) : "")
                                                  : qsTr("The point under the pin")
                                textFormat: TextFormat.Plain
                                textStyle.base: SystemDefaults.TextStyles.SmallText
                                textStyle.color: Color.White
                            }
                        }
                    }
                    // Zoom in, out, and back to where the phone is: the chat's jump buttons (JumpButton.qml), at the right edge.
                    Container {
                        horizontalAlignment: HorizontalAlignment.Right
                        verticalAlignment: VerticalAlignment.Center
                        rightPadding: ui.du(1)
                        JumpButton {
                            glyph: "+"
                            onTapped: picker.zoomBy(1)
                        }
                        JumpButton {
                            glyph: "−"
                            onTapped: picker.zoomBy(-1)
                        }
                        JumpButton {
                            image: "asset:///images/locate.png"
                            onTapped: picker.locate()
                        }
                    }
                    // A position or a map on its way, over the map's corner.
                    Container {
                        horizontalAlignment: HorizontalAlignment.Right
                        verticalAlignment: VerticalAlignment.Top
                        rightPadding: ui.du(1)
                        topPadding: ui.du(1)
                        ActivityIndicator {
                            running: page.shownPicture != "" && picker.busy
                            preferredWidth: ui.du(6)
                            preferredHeight: ui.du(6)
                        }
                    }
                }
                // What goes on with the position or the map, and for a problem the button that does something about it.
                Container {
                    visible: page.noteText != ""
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(1)
                    bottomPadding: ui.du(1)
                    Label {
                        horizontalAlignment: HorizontalAlignment.Fill
                        multiline: true
                        text: page.noteText
                        textFormat: TextFormat.Plain
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.textAlign: TextAlign.Center
                        textStyle.color: ui.palette.textOnPlain
                    }
                    Button {
                        visible: picker.problem != ""
                        horizontalAlignment: HorizontalAlignment.Center
                        text: picker.problem == "location_off" ? qsTr("Location settings") : qsTr("Try again")
                        onClicked: picker.problem == "location_off" ? picker.openSettings()
                                   : picker.problem == "map" ? picker.reload() : picker.locate()
                    }
                }
            }
        }
    }
}
