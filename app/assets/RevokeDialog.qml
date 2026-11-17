import bb.system 1.2

// Delete for everyone's question, out of ChatPage.qml to keep that file short. chatCtl is the page's
// ChatController. Not named ctl: in `ctl: ctl` QML would read this property.
SystemDialog {
    property string msgId
    property variant chatCtl
    signal notSent
    title: qsTr("Delete for everyone?")
    body: qsTr("It will be deleted for everyone in this chat.")
    confirmButton.label: qsTr("Delete")
    cancelButton.label: qsTr("Cancel")
    onFinished: {
        if (value == SystemUiResult.ConfirmButtonSelection && !chatCtl.deleteForEveryone(msgId)) {
            notSent()
        }
    }
}
