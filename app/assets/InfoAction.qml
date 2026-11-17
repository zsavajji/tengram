import bb.cascades 1.4

// ⋮ → Contact info / Group info / Channel info, and the title bar's tap (step 5.8): the info page of a person, a
// group or a channel, out of ChatPage.qml to keep that file short. The page sets the title (its translations
// are the page's). Added to the menu for people, groups and channels (ActionItem has no visible).
ActionItem {
    id: action
    // The chat page's chat (a ChatListModel row) and ChatController.
    property variant chat
    property variant chatCtl
    imageSource: "asset:///images/contacts_action.png"
    onTriggered: showInfo()
    attachedObjects: [
        ComponentDefinition {
            id: infoDef
            source: "GroupInfoPage.qml"
        },
        ComponentDefinition {
            id: contactDef
            source: "ContactInfoPage.qml"
        },
        ComponentDefinition {
            id: channelDef
            source: "ChannelInfoPage.qml"
        }
    ]
    // A person's page asks the engine itself, with a fresh row for the mute state; a group's gets the
    // controller, so it reads what the chat page already asked for. The chat page stays under it, so
    // it outlives the info page.
    function showInfo() {
        if (!chat || chat.kind == 2) {
            return
        }
        if (chat.kind == 0 || chat.kind == 3) {
            var c = (chat.kind == 0 ? contactDef : channelDef).createObject()
            var row = chats.chat(chat.jid)
            // No row for a contact picked with no chat yet.
            c.chat = row.jid ? row : chat
            nav.push(c)
            return
        }
        var p = infoDef.createObject()
        p.chat = chat
        p.ctl = chatCtl
        nav.push(p)
    }
    // A mention's link in a message (MessageCard.cpp): the person's page, with the row built as
    // GroupInfoPage's openPerson does: the one-to-one chat's, else one like the contact picker's.
    // A member brings the initials and colour; for someone who isn't one (any more) the name is the
    // link's "@name" and the tile one initial on a fixed colour.
    // ponytail: that tile is plain; the controller's initials() and pickColor() if it shows.
    function showPerson(jid, name) {
        var row = chats.chat(jid)
        if (!row.jid) {
            var ps = chatCtl && chatCtl.groupInfo.participants ? chatCtl.groupInfo.participants : []
            var p = {}
            for (var i = 0; i < ps.length; i++) {
                if (ps[i].jid == jid) {
                    p = ps[i]
                }
            }
            var n = p.name || name.replace(/^@/, "")
            row = {"kind": 0, "jid": jid, "name": n, "initials": p.initials || n.charAt(0).toUpperCase() || "#",
                   "color": p.color || "#087099"}
        }
        var c = contactDef.createObject()
        c.chat = row
        nav.push(c)
    }
}
