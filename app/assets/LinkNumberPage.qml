import bb.cascades 1.4
import "errors.js" as Errors
import "countries.js" as Countries
import "phone.js" as Phone

// Linking by pairing code (PLAN.md §7): pick the country, type the phone's number, the engine
// returns a code, and the linking page below shows it in large type. A number typed or pasted
// with "+" or "00" picks its own country (user, 2026-10-31).
Page {
    id: page
    property bool asking: false
    property string failedCmd: engine.failure.cmd || ""
    property string linkState: engine.link.state || ""
    // The chosen country, from phone.js; the phone's region to start with.
    property variant country: Phone.byRegion(Phone.all(Countries.data, phoneLocale.language), phoneLocale.region)
    // When the picker closed: the Enter that chose a country can reach the number field too.
    property double pickedAt: 0

    titleBar: TitleBar {
        title: qsTr("Link with phone number")
    }
    actions: [
        ActionItem {
            title: qsTr("Get code")
            ActionBar.placement: ActionBarPlacement.Signature
            enabled: !page.asking && page.country != null && number.text != ""
            onTriggered: page.submit()
        }
    ]
    attachedObjects: ComponentDefinition {
        id: countryDef
        source: "CountryPage.qml"
    }

    function submit() {
        if (!country || number.text == "" || asking)
            return
        asking = true
        engine.request("link.code", {"phone": Phone.international(country, number.text)})
    }
    function pickCountry() {
        var picker = countryDef.createObject()
        picker.picked.connect(function (c) {
            page.country = c
            page.pickedAt = Date.now()
            nav.pop()
            number.requestFocus()
        })
        nav.push(picker)
    }
    // An international number, typed or pasted: its country goes to the row above, and the
    // field keeps the rest.
    function detect(text) {
        var t = text.replace(/^\s+/, "")
        if (t.charAt(0) != "+" && t.substr(0, 2) != "00")
            return
        var digits = t.replace(/\D/g, "")
        if (t.charAt(0) != "+")
            digits = digits.substr(2)
        var found = Phone.detect(Phone.all(Countries.data, phoneLocale.language), digits, phoneLocale.region)
        if (found) {
            country = found.country
            number.text = found.rest
        }
    }
    // The request ended in failure; on success the state change pops this page.
    onFailedCmdChanged: {
        if (failedCmd == "link.code")
            asking = false
    }
    onLinkStateChanged: {
        if (linkState == "code")
            nav.pop()
    }
    onCreationCompleted: number.requestFocus()

    Container {
        Container {
            topPadding: ui.du(2)
            leftPadding: ui.du(2)
            rightPadding: ui.du(2)
            horizontalAlignment: HorizontalAlignment.Fill
            Label {
                multiline: true
                text: qsTr("Type the number of the phone that has WhatsApp.")
            }
        }
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            topPadding: ui.du(1)
            leftPadding: ui.du(2)
            rightPadding: ui.du(2)
            Container {
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                // The country, at the number's start (user, 2026-10-31): a box like a button,
                // as tall as the field, that opens the picker.
                Container {
                    id: countryBox
                    property bool pressed: false
                    verticalAlignment: VerticalAlignment.Fill
                    minWidth: ui.du(14)
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    background: pressed ? ui.palette.primaryBase : ui.palette.plain
                    layout: DockLayout {}
                    gestureHandlers: TapHandler {
                        onTapped: page.pickCountry()
                    }
                    onTouch: countryBox.pressed = event.isDown() || event.isMove()
                    onTouchExit: countryBox.pressed = false
                    Label {
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        text: page.country ? page.country.flag + " +" + page.country.code : qsTr("Country")
                        textStyle.base: nav.emojiStyle
                    }
                }
                TextField {
                    id: number
                    leftMargin: ui.du(1)
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    inputMode: TextFieldInputMode.PhoneNumber
                    hintText: qsTr("Phone number")
                    input.submitKey: SubmitKey.Submit
                    input.onSubmitted: {
                        if (Date.now() - page.pickedAt > 1000)
                            page.submit()
                    }
                    onTextChanging: page.detect(text)
                }
            }
            Label {
                visible: engine.failure.cmd == "link.code"
                multiline: true
                text: Errors.errorText(engine.failure)
            }
        }
        // The wait takes the middle of the space left.
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            layout: DockLayout {}
            Busy {
                running: page.asking
                text: qsTr("Asking WhatsApp for a code…")
            }
        }
    }
}
