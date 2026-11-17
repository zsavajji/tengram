import bb.cascades 1.4

// The "@" picker of a group chat, above the reply bar: typing "@" at the end of the composer's text
// lists the members who have a name starting with what follows it; a tap writes "@name " and
// remembers who, and ChatPage's send hands the engine the people the text still names (take).
// people are the group's participants (jid, name, me, ...), field the composer (its TextArea;
// not named composer, which a binding `composer: composer` could read as its own).
// ponytail: only an "@" typed at the end of the text is matched, not one in the middle, and the
// caption of a staged file gets no mentions; a cursor-aware match if either is missed.
Container {
    id: bar
    property variant people: []
    property variant field
    // The {jid, name} picked so far; a variant array doesn't notify on a push, so it is replaced.
    property variant mentioned: []
    property int count: 0
    visible: count > 0
    horizontalAlignment: HorizontalAlignment.Fill
    background: ui.palette.plain
    attachedObjects: [
        ArrayDataModel {
            id: matches
        }
    ]
    // Members not us with a word of the name starting with what follows the "@", any case: all of
    // them for a bare "@" (ContactPicker::filter's rule).
    function find(text) {
        var m = /(^|\s)@([^\s@]*)$/.exec(text)
        matches.clear()
        if (m && people) {
            var start = m[2].toLowerCase()
            for (var i = 0; i < people.length; i++) {
                var p = people[i]
                // A member with no name has nothing to write after the "@".
                var words = !p.me && p.name ? p.name.split(" ") : []
                for (var k = 0; k < words.length; k++) {
                    if (words[k].toLowerCase().indexOf(start) == 0) {
                        matches.append(p)
                        break
                    }
                }
            }
        }
        count = matches.size()
        // Nothing typed any more (sent, or cleared): forget who was picked. Not in take: a send that
        // failed keeps its text, and must keep its mentions.
        if (text == "") {
            mentioned = []
        }
    }
    function pick(p) {
        // The function form: a name may hold a "$", which a string would read as a pattern.
        field.text = field.text.replace(/@[^\s@]*$/, function () {
            return "@" + p.name + " "
        })
        var list = mentioned.slice(0)
        list.push({"jid": p.jid, "name": p.name})
        mentioned = list
        matches.clear()
        count = 0
        field.requestFocus()
        field.editor.cursorPosition = field.text.length
    }
    // The picked people whose "@name" the text still has.
    function take(text) {
        var out = []
        for (var i = 0; i < mentioned.length; i++) {
            if (text.indexOf("@" + mentioned[i].name) >= 0) {
                out.push(mentioned[i])
            }
        }
        return out
    }
    ListView {
        property variant emojiStyle: nav.emojiStyle
        property int bodySize: nav.bodySize
        property string pressColor: nav.pressColor
        // At most four rows (a row and its hairline); the rest scroll.
        preferredHeight: Math.min(bar.count, 4) * (ui.du(9.4) + ui.px(2))
        dataModel: matches
        onTriggered: bar.pick(matches.data(indexPath))
        listItemComponents: ListItemComponent {
            type: ""
            Container {
                id: row
                horizontalAlignment: HorizontalAlignment.Fill
                background: row.ListItem.active ? Color.create(row.ListItem.view.pressColor) : Color.Transparent
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    minHeight: ui.du(9.4)
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    layout: DockLayout {}
                    Label {
                        verticalAlignment: VerticalAlignment.Center
                        text: ListItemData.name
                        textFormat: TextFormat.Plain
                        textStyle.base: row.ListItem.view.emojiStyle
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: row.ListItem.view.bodySize
                    }
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.px(2)
                    background: ui.palette.plain
                }
            }
        }
    }
}
