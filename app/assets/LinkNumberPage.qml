import bb.cascades 1.4
import "errors.js" as Errors
import "countries.js" as Countries
import "phone.js" as Phone

// Linking by phone number: pick the country, type the phone's number, Telegram sends a code, and
// this page pops when the link state turns "code"; the linking page below then asks for the code.
// A number typed or pasted with "+" or "00" picks its own country (user, 2026-10-31).
Page {
    id: page
    property bool asking: false
    property string failedCmd: engine.failure.cmd || ""
    property string linkState: engine.link.state || ""
    // While a logout from "qr" runs, the state is "linking" or "qr": wait for the next one.
    property bool canAsk: linkState == "unlinked" || linkState == "code" || linkState == "password"
    // The chosen country, from phone.js; the phone's region to start with.
    property variant country: Phone.byRegion(Phone.all(Countries.data, phoneLocale.language), phoneLocale.region)
    // When the picker closed: the Enter that chose a country can reach the number field too.
    property double pickedAt: 0

    titleBar: PageBar {
        title: qsTr("Link with phone number")
    }
    actions: [
        ActionItem {
            title: qsTr("Get code")
            ActionBar.placement: ActionBarPlacement.Signature
            enabled: page.canAsk && !page.asking && page.country != null && number.text != ""
            onTriggered: page.submit()
        }
    ]
    attachedObjects: ComponentDefinition {
        id: countryDef
        source: "CountryPage.qml"
    }

    function submit() {
        if (!canAsk || !country || number.text == "" || asking)
            return
        asking = true
        engine.request("link.phone", {"phone": Phone.international(country, number.text)})
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
        if (failedCmd == "link.phone")
            asking = false
    }
    onLinkStateChanged: {
        // Only while asking: a refused number asked from "code" goes back to "code" after its failure.
        if (linkState == "code" && asking)
            nav.pop()
    }
    onCreationCompleted: number.requestFocus()

    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        Container {
            topPadding: ui.du(2)
            leftPadding: ui.du(2)
            rightPadding: ui.du(2)
            horizontalAlignment: HorizontalAlignment.Fill
            Label {
                multiline: true
                text: qsTr("Type the number of the phone that has Telegram.")
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
                // as tall as the field, that opens the picker. The Classic's trackpad reaches it: FocusPolicy.Key, not KeyAndTouch,
                // so a touch leaves the focus (and the keyboard) in the field; lit as while pressed, and a click (Enter) opens the picker.
                TapArea {
                    id: countryBox
                    property bool pressed: false
                    verticalAlignment: VerticalAlignment.Fill
                    minWidth: ui.du(14)
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    focusPolicy: FocusPolicy.Key
                    background: pressed || focused ? ui.palette.primaryBase : ui.palette.plain
                    layout: DockLayout {}
                    onClicked: page.pickCountry()
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
                visible: engine.failure.cmd == "link.phone"
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
                text: qsTr("Asking Telegram for a code…")
            }
        }
    }
}
