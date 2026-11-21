import bb.cascades 1.4
import bb.system 1.2
import tengram 1.0

// The rows of a chat list, for ChatsPage and ArchivedPage (and, as topicList, a forum's topics): the model is set by the page, which
// takes chatTapped. The rows are C++ (ChatRows.cpp): they reach the work through the list's
// functions and properties, and the long-press menu calls the functions.
ListView {
    id: list
    // The shared emoji style and sizes (main.qml), set by the page.
    property variant emojiStyle
    property int bodySize
    property int smallSize
    property real textScale: settings.textScale
    property bool compact: settings.compact
    // Round mode (ChatRows.cpp): the rows' pictures are discs.
    property bool round: settings.round
    property string pressColor: nav.pressColor
    property string secretColor: nav.secretColor
    // True on the Archived page, where "Archive" reads "Unarchive".
    property bool archivedList: false
    // True on a list of channels only: the row's menu has Unfollow for Archive, and no pin or mute. No page sets it
    // since the channels moved into the main list.
    property bool channelList: false
    // True on a forum's topics (TopicsPage): a flat model with no headers and no long-press menu, its rows the same.
    property bool topicList: false
    signal chatTapped(variant chat)
    // A failed move of a pin shows as a toast, once per failure (request() of the same cmd clears failure).
    property string failedCmd: engine.failure.cmd || ""
    onFailedCmdChanged: {
        if (failedCmd == "chat.pinOrder" && !archivedList && !topicList) {
            moveFailed.show()
        }
    }

    function set(jid, args) {
        args.chat = jid
        engine.request("chat.set", args)
    }
    function toggleUnread(chat) {
        set(chat.jid, {"unread": chat.unread == 0})
    }
    function togglePin(chat) {
        set(chat.jid, {"pin": !chat.pinned})
    }
    // Moves a pinned chat one place up (step -1) or down (1) among the main list's pins: the pins in their order, with it
    // swapped with its neighbour, go to the engine, and chats.db's order changes with TDLib's answer.
    function movePin(chat, step) {
        var jids = dataModel.pinnedJids()
        var i = jids.indexOf(chat.jid)
        var j = i + step
        if (i < 0 || j < 0 || j >= jids.length) {
            return
        }
        jids[i] = jids[j]
        jids[j] = chat.jid
        engine.request("chat.pinOrder", {"ids": jids})
    }
    function toggleArchive(chat) {
        if (channelList) {
            unfollowDialog.jid = chat.jid
            unfollowDialog.body = chat.name
            unfollowDialog.show()
            return
        }
        set(chat.jid, {"archive": !archivedList})
    }
    function toggleMute(chat) {
        if (chat.muted) {
            set(chat.jid, {"mute": 0})
        } else {
            muteDialog.jid = chat.jid
            // Filled here: a SystemListDialog isn't a Cascades control and has no
            // onCreationCompleted; assigning one failed the whole page (0.0.1.76).
            if (!muteDialog.filled) {
                muteDialog.appendItem(qsTr("8 hours"), true, true)
                muteDialog.appendItem(qsTr("1 week"))
                muteDialog.appendItem(qsTr("Always"))
                muteDialog.filled = true
            }
            muteDialog.show()
        }
    }
    function pickTranslate(chat) {
        translateDialog.pick(chat.jid, chat.translate)
    }
    attachedObjects: [
        TranslatePicker {
            id: translateDialog
        },
        SystemToast {
            id: moveFailed
            body: qsTr("Couldn't move this chat")
        },
        SystemDialog {
            id: unfollowDialog
            property string jid
            title: qsTr("Unfollow this channel?")
            confirmButton.label: qsTr("Unfollow")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value == SystemUiResult.ConfirmButtonSelection) {
                    engine.request("channel.unfollow", {"chat": jid})
                }
            }
        },
        SystemListDialog {
            id: muteDialog
            property string jid
            property bool filled: false
            title: qsTr("Mute for…")
            confirmButton.label: qsTr("Mute")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (value != SystemUiResult.ConfirmButtonSelection) {
                    return
                }
                var i = selectedIndices[0]
                var now = Math.floor(new Date().getTime() / 1000)
                list.set(jid, {"mute": i == 0 ? now + 8 * 3600 : i == 1 ? now + 7 * 24 * 3600 : -1})
            }
        }
    ]
    onTriggered: {
        if (indexPath.length < 2 && !topicList) {
            return
        }
        chatTapped(dataModel.data(indexPath))
    }
    listItemProvider: ChatRows {}
}
