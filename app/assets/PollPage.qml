import bb.cascades 1.4
import bb.system 1.2

// Create poll (step 6.6e): the question, the options (two to start, up to WhatsApp's 12) and whether
// several can be picked. The engine checks WhatsApp's limits (poll.create); a failure to send shows
// as a toast on the chat page (ReactBar), which this page has gone back to by then.
Page {
    id: page
    property string jid
    titleBar: TitleBar {
        title: qsTr("Create poll")
    }
    ScrollView {
        Container {
            topPadding: ui.du(2)
            bottomPadding: ui.du(2)
            leftPadding: ui.du(2)
            rightPadding: ui.du(2)
            horizontalAlignment: HorizontalAlignment.Fill
            Label {
                text: qsTr("Question")
            }
            TextField {
                id: question
                hintText: qsTr("Ask something")
                input.submitKey: SubmitKey.Next
                input.submitKeyFocusBehavior: SubmitKeyFocusBehavior.Next
            }
            Label {
                text: qsTr("Options")
                topMargin: ui.du(2)
            }
            Container {
                id: options
                horizontalAlignment: HorizontalAlignment.Fill
            }
            Button {
                id: addButton
                text: qsTr("Add option")
                topMargin: ui.du(1)
                horizontalAlignment: HorizontalAlignment.Fill
                onClicked: page.addOption().requestFocus()
            }
            Container {
                topMargin: ui.du(2)
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                Label {
                    text: qsTr("Allow several answers")
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                }
                ToggleButton {
                    id: several
                    verticalAlignment: VerticalAlignment.Center
                }
            }
        }
    }
    actions: [
        ActionItem {
            title: qsTr("Send")
            imageSource: "file:///usr/share/icons/bb_action_sendtextmessage.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: page.send()
        }
    ]
    attachedObjects: [
        ComponentDefinition {
            id: optionDef
            TextField {
                topMargin: ui.du(1)
                horizontalAlignment: HorizontalAlignment.Fill
                input.submitKey: SubmitKey.Next
                input.submitKeyFocusBehavior: SubmitKeyFocusBehavior.Next
            }
        },
        SystemToast {
            id: tooFew
            body: qsTr("A poll needs a question and two options")
        },
        SystemToast {
            id: offline
            body: qsTr("Not sent: HyperBerry is reconnecting. Try again in a moment.")
        }
    ]
    onCreationCompleted: {
        addOption()
        addOption()
    }
    // Called after the push, so the Q10's keyboard types into the question at once.
    function focusQuestion() {
        question.requestFocus()
    }
    // A new option field, to WhatsApp's 12. Returns it, so Add option can focus it.
    function addOption() {
        var f = optionDef.createObject()
        f.hintText = qsTr("Option %1").arg(options.count() + 1)
        options.add(f)
        addButton.enabled = options.count() < 12
        return f
    }
    function send() {
        var list = []
        for (var i = 0; i < options.count(); i++) {
            var t = options.at(i).text.trim()
            if (t != "") {
                list.push(t)
            }
        }
        if (question.text.trim() == "" || list.length < 2) {
            tooFew.show()
            return
        }
        if (!engine.request("poll.create", {"chat": page.jid, "question": question.text.trim(), "options": list, "multiple": several.checked})) {
            offline.show()
            return
        }
        nav.pop()
    }
}
