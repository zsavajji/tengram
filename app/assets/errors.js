// The English-for-people text of a failed request (EngineLink.failure), by the engine's code.
function errorText(f) {
    switch (f.code) {
    case "bad_phone":
        return qsTr("That doesn't look like a phone number. Type it with the country code, as in +39 333 123 4567.")
    case "offline":
        return qsTr("You're offline. Connect to use Tengram.")
    case "no_engine":
        return qsTr("Tengram's engine isn't running yet. Try again in a moment.")
    case "linked":
        return qsTr("This BlackBerry is already linked.")
    }
    return qsTr("That didn't work: %1").arg(f.message)
}
