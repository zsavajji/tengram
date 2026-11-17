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
    case "bad_code":
        return qsTr("That code isn't right. Check it and type it again.")
    case "bad_password":
        return qsTr("That password isn't right.")
    case "bad_state":
        return qsTr("Telegram is still busy with the last step. Try again in a moment.")
    case "td":
        // Flood wait: "429 Too Many Requests: retry after 38".
        var wait = /retry after (\d+)/.exec(f.message || "")
        if (wait && (f.message || "").indexOf("429") == 0)
            return qsTr("Telegram asks you to wait %1 minutes before trying again.").arg(Math.ceil(wait[1] / 60))
        break
    }
    return qsTr("That didn't work: %1").arg(f.message)
}
