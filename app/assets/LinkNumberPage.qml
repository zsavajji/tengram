import bb.cascades 1.4
import "errors.js" as Errors
import "countries.js" as Countries
import "phone.js" as Phone

// Linking by phone number, the first step after the welcome page: pick the country, type the phone's number,
// Telegram sends a code. Get code (or the QR button) opens the linking page (LinkPage.qml) over this one, when
// the link state turns "code" (or "email", "emailCode", "register" or "premium", the sign-up of a number new
// to Telegram); it asks for the rest, and Back from there comes back here.
// A number typed or pasted with "+" or "00" picks its own country (user, 2026-10-31).
Page {
    id: page
    property bool asking: false
    property string failedCmd: engine.failure.cmd || ""
    property string linkState: engine.link.state || ""
    // While a logout from "qr" runs, the state is "linking" or "qr": wait for the next one.
    property bool canAsk: linkState == "unlinked" || linkState == "code" || linkState == "password"
                             || linkState == "email" || linkState == "emailCode" || linkState == "register" || linkState == "premium"
    // The chosen country, from phone.js; the phone's region to start with, then the country Telegram places our IP
    // address in (the link body's `country`, the user, 2026-11-21) unless the user chose one meanwhile.
    property variant country: Phone.byRegion(Phone.all(Countries.data, phoneLocale.language), phoneLocale.region)
    // The user picked a country, or typed a number that carries its own.
    property bool chosen: false
    // The number field's text as it is typed: a TextField's text changes only when it loses focus, so Get code stayed off
    // until the field was left (the user, 2026-11-21), and submit() could read it stale.
    property string typed: ""
    property string ipCountry: engine.link.country || ""
    // When the picker closed: the Enter that chose a country can reach the number field too.
    property double pickedAt: 0

    titleBar: PageBar {
        title: qsTr("Link with phone number")
    }
    actions: [
        ActionItem {
            title: qsTr("Get code")
            imageSource: "file:///usr/share/icons/bb_action_sendtextmessage.png"
            ActionBar.placement: ActionBarPlacement.Signature
            enabled: page.canAsk && !page.asking && page.country != null && page.typed.replace(/\s/g, "") != ""
            onTriggered: page.submit()
        },
        ActionItem {
            title: qsTr("Log in with QR code")
            imageSource: "asset:///images/qr_action.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: page.canAsk && !page.asking
            onTriggered: page.openLink(true)
        }
    ]
    attachedObjects: [
        ComponentDefinition {
            id: countryDef
            source: "CountryPage.qml"
        },
        ComponentDefinition {
            id: linkDef
            source: "LinkPage.qml"
        }
    ]

    // The linking page over this one; qr: it asks Telegram for a QR code.
    function openLink(qr) {
        if (nav.top != page)
            return
        var link = linkDef.createObject()
        link.wantQr = qr
        nav.push(link)
    }

    function submit() {
        var text = typed
        if (!canAsk || !country || text.replace(/\s/g, "") == "" || asking)
            return
        asking = true
        // Still "+" or "00" at the start: detect() placed no country (a test server's 99966…), so it goes as typed.
        var intl = /^\s*(\+|00)/.test(text)
        engine.request("link.phone", {"phone": intl ? "+" + text.replace(/^\s*00/, "").replace(/\D/g, "")
                                                    : Phone.international(country, text)})
    }
    function pickCountry() {
        var picker = countryDef.createObject()
        picker.picked.connect(function (c) {
            page.country = c
            page.chosen = true
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
            chosen = true
            number.text = found.rest
            typed = found.rest
        }
    }
    function useIpCountry() {
        if (chosen || ipCountry == "")
            return
        var c = Phone.byRegion(Phone.all(Countries.data, phoneLocale.language), ipCountry)
        if (c)
            country = c
    }
    onIpCountryChanged: useIpCountry()
    // The request ended in failure; on success the state change opens the linking page.
    onFailedCmdChanged: {
        if (failedCmd == "link.phone")
            asking = false
    }
    onLinkStateChanged: {
        // Only while asking: a refused number asked from "code" goes back to "code" after its failure.
        if ((linkState == "code" || linkState == "email" || linkState == "emailCode" || linkState == "register"
             || linkState == "premium") && asking) {
            asking = false
            openLink(false)
        }
    }
    onCreationCompleted: {
        useIpCountry()
        number.requestFocus()
    }
    // Back here from the linking page (its Back, or its Link with phone number) while a QR code waits: TDLib takes no
    // phone number then, so the wait ends (a logout ends nothing, there is no session yet) and the state goes back to
    // "unlinked", where Get code works.
    property bool onTop: nav.top == page
    onOnTopChanged: {
        if (onTop && linkState == "qr")
            engine.request("link.logout", {})
    }

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
                text: qsTr("Type your phone number. A number that isn't on Telegram yet signs up.")
            }
            Label {
                multiline: true
                topMargin: ui.du(1)
                textStyle.color: ui.palette.secondaryTextOnPlain
                text: qsTr("Already have an account and prefer to log in with a QR code? Press the button at the bottom.")
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
                    onTextChanging: {
                        page.typed = text
                        page.detect(text)
                    }
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
