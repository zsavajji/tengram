import bb.system 1.2
import "languages.js" as Languages

// The language a chat's incoming messages are translated into, picked from a list: ChatList's row menu and the chat
// page's menu both use it. Confirming sends chat.set. Not a Cascades control, like RevokeDialog: a SystemListDialog
// has no onCreationCompleted, and filling it from there failed the whole page (0.0.1.76), so pick() fills it.
SystemListDialog {
    property string jid
    title: qsTr("Translate incoming messages")
    confirmButton.label: qsTr("OK")
    cancelButton.label: qsTr("Cancel")
    // Rebuilt at each show, with the chat's language (current, "" for off) selected.
    function pick(chat, current) {
        jid = chat
        clearList()
        appendItem(qsTr("Off"), true, !current)
        var langs = Languages.list()
        for (var i = 0; i < langs.length; i++) {
            appendItem(langs[i][1], true, current == langs[i][0])
        }
        show()
    }
    onFinished: {
        if (value != SystemUiResult.ConfirmButtonSelection) {
            return
        }
        var i = selectedIndices[0]
        engine.request("chat.set", {"chat": jid, "translate": i > 0 ? Languages.list()[i - 1][0] : ""})
    }
}
