// The English-for-people text of a failed request (EngineLink.failure), by the engine's code.
function errorText(f) {
    switch (f.code) {
    case "bad_phone":
        return qsTr("That doesn't look like a phone number. Type it with the country code, as in +39 333 123 4567.")
    case "offline":
        return qsTr("You're offline. Connect to use HyperBerry.")
    case "no_engine":
        return qsTr("HyperBerry's engine isn't running yet. Try again in a moment.")
    case "linked":
        return qsTr("This BlackBerry is already linked.")
    }
    return qsTr("That didn't work: %1").arg(f.message)
}

// A failed call.start (docs/plan/phase7.md "The contract"), worded for a toast.
function callError(f) {
    switch (f.code) {
    case "busy":
        return qsTr("You're already on a call.")
    case "offline":
        return qsTr("You're offline. Connect to call.")
    case "unlinked":
        return qsTr("Link this BlackBerry to WhatsApp to call.")
    case "simulated":
        return qsTr("The simulator can't place calls.")
    case "bad_chat":
        return qsTr("You can only call a person, not a group.")
    case "no_engine":
        return qsTr("HyperBerry's engine isn't running yet. Try again in a moment.")
    }
    return qsTr("Couldn't place the call: %1").arg(f.message)
}
