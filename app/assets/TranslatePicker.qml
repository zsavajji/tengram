import bb.cascades 1.4
import "languages.js" as Languages

// The language a chat's incoming messages are translated into, picked in a select (Off, then languages.js): ChatList's
// row menu and the chat page's menu both use it. OK sends chat.set. A card over a dimmed page, like TranslateDialog;
// the hundred options are made at the first pick(), not with the page that holds the dialog.
Dialog {
    id: dlg
    property string jid
    // The screen's height, from the dim layer that fills it: the card takes at most 80 % of it.
    property real screenH: 0
    // The chat's language (current, "" for off) selected at each show.
    function pick(chat, current) {
        jid = chat
        if (langs.count() == 0) {
            langs.add(option(qsTr("Off"), ""))
            var all = Languages.list()
            for (var i = 0; i < all.length; i++) {
                langs.add(option(all[i][1], all[i][0]))
            }
        }
        langs.selectedIndex = 0
        for (var j = 1; j < langs.count(); j++) {
            if (langs.at(j).value == current) {
                langs.selectedIndex = j
            }
        }
        open()
    }
    function option(text, value) {
        var o = optionDef.createObject()
        o.text = text
        o.value = value
        return o
    }
    onOpened: okButton.requestFocus()
    attachedObjects: ComponentDefinition {
        id: optionDef
        Option {}
    }
    Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        // The dim layer is its own sibling under the card: a tap on the card never reaches it.
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: Color.create("#B3000000")
            gestureHandlers: TapHandler {
                onTapped: dlg.close()
            }
            attachedObjects: LayoutUpdateHandler {
                onLayoutFrameChanged: dlg.screenH = layoutFrame.height
            }
        }
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(2)
            rightMargin: ui.du(2)
            maxHeight: dlg.screenH > 0 ? dlg.screenH * 0.8 : 100000
            background: Color.create(nav.pageColor)
            topPadding: ui.du(2)
            bottomPadding: ui.du(2)
            leftPadding: ui.du(2)
            rightPadding: ui.du(2)
            Label {
                text: qsTr("Translate incoming messages")
                multiline: true
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: nav.bodySize * 1.1
                textStyle.fontWeight: FontWeight.W500
            }
            // The open select is taller than the card: it scrolls inside it.
            ScrollView {
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }
                DropDown {
                    id: langs
                    // TranslateDialog's words, already in the ten languages.
                    title: qsTranslate("TranslateDialog", "Translate to")
                }
            }
            Container {
                topMargin: ui.du(2)
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                Button {
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    text: qsTr("Cancel")
                    onClicked: dlg.close()
                }
                Button {
                    id: okButton
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    text: qsTr("OK")
                    onClicked: {
                        engine.request("chat.set", {"chat": dlg.jid, "translate": langs.at(langs.selectedIndex).value})
                        dlg.close()
                    }
                }
            }
        }
    }
}
