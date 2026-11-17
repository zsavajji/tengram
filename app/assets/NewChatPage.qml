import bb.cascades 1.4
import hyperberry 1.0

// New chat (PLAN.md §7, step 3.6): the address book with type-to-filter. A tap, or Enter in the
// search field, picks the first contact shown; ChatsPage opens that chat, or an empty one the
// first message creates.
Page {
    id: page
    signal picked(variant chat)
    // Group info's Add participant picks with it too.
    property string title: qsTr("New chat")
    titleBar: TitleBar {
        title: page.title
    }
    attachedObjects: ContactPicker {
        id: picker
        link: engine
    }
    onCreationCompleted: picker.load()
    // Called after the push, so on the Q10 typing filters at once (as ChatPage.focusComposer).
    function focusSearch() {
        search.requestFocus()
    }
    Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            // Padding, not margins: margins only space siblings, never from the container's edge.
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                leftPadding: ui.du(1.5)
                rightPadding: ui.du(1.5)
                topPadding: ui.du(1.5)
                bottomPadding: ui.du(1.5)
                TextField {
                    id: search
                    hintText: qsTr("Search contacts")
                    input.submitKey: SubmitKey.Search
                    input.onSubmitted: {
                        if (picker.shown > 0) {
                            page.picked(picker.model.data([0]))
                        }
                    }
                    onTextChanging: picker.filter(text)
                }
            }
            ListView {
                // The rows reach the shared emoji style and sizes through the view, as the chat list's do.
                property variant emojiStyle: nav.emojiStyle
                property int bodySize: nav.bodySize
                property int smallSize: nav.smallSize
                property string pressColor: nav.pressColor
                dataModel: picker.model
                onTriggered: page.picked(dataModel.data(indexPath))
                listItemComponents: ListItemComponent {
                    type: ""
                    ContactRow {
                        contact: ListItemData
                    }
                }
            }
        }
        Busy {
            running: !picker.loaded
            text: qsTr("Loading…")
        }
        Label {
            visible: picker.loaded && picker.shown == 0
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            multiline: true
            textStyle.textAlign: TextAlign.Center
            textStyle.color: ui.palette.secondaryTextOnPlain
            text: picker.total == 0 ? qsTr("No contacts yet. Your phone sends them shortly after linking.") : qsTr("No contact matches.")
        }
    }
}
