import bb.cascades 1.4

// The Active Frame (PLAN.md step 5.2), HyperBerry's design of 2026-11-17 (its cover.qml) with three rows (the user,
// 2026-11-20). The Q10 shows all of its 310 x 211 px, above the system's title strip (screenshot, 0.0.1.76), so
// nothing may run past the bottom. Idle: the app's glyph, and "All caught up" low on the left. Active: three equal
// rows on a panel, private chats, groups and channels (CoverRow.qml), each counting what the Hub notifies: an
// unmuted chat's unread messages, a muted one's unread mentions and replies to us (ChatListModel::updateFrame).
// Margins are set, since unset ones put about 20 px between labels. Type is sized in percent: at 100 % BodyText
// and SmallText draw capitals 20 px tall in a cover (HyperBerry, measured on the Q10). It takes no touch. Created
// by main.qml, so nav, chats and settings are in its scope.
SceneCover {
    id: cover
    // It follows the theme: Bright's page and text, Dark's black. Written out: a cover's scene may not share the
    // app's palette.
    property bool bright: settings.theme == 1
    property variant ink: bright ? Color.create("#111111") : Color.White
    property variant quiet: bright ? Color.create("#5E5E5E") : Color.Gray
    property variant accent: Color.create(bright ? "#168ACD" : "#24A1DE")
    // The panel lifts the rows off the home screen's wallpaper behind the frame; the idle page is plain.
    property variant panel: bright ? Color.White : Color.create("#2B2B2B")
    // Dark with Dark background on Grey: the frame is the app's grey #181818, idle or not.
    property bool grey: !bright && settings.darkGrey
    property variant line: bright ? Color.create("#D0D0D0") : Color.create("#464646")
    property bool idle: chats.unreadTotal == 0
    content: Container {
        background: grey ? Color.create("#181818") : idle ? (bright ? Color.White : Color.Black) : panel
        preferredWidth: 310
        preferredHeight: 211
        layout: DockLayout {
        }
        // Idle. The glyph (gen-icon.py) is white and tinted. Padding, not margins: margins never space from a
        // container's edge.
        Container {
            visible: idle
            horizontalAlignment: HorizontalAlignment.Left
            verticalAlignment: VerticalAlignment.Top
            leftPadding: 18
            topPadding: 17
            ImageView {
                imageSource: "asset:///images/cover_glyph.png"
                filterColor: accent
            }
        }
        Container {
            visible: idle
            horizontalAlignment: HorizontalAlignment.Left
            verticalAlignment: VerticalAlignment.Bottom
            leftPadding: 18
            bottomPadding: 20
            Label {
                text: engine.linked ? qsTr("All caught up") : "Tengram"
                textStyle.base: SystemDefaults.TextStyles.BodyText
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: 95
                textStyle.color: ink
                topMargin: 0
                bottomMargin: 0
            }
            Label {
                text: engine.linked ? qsTr("No new messages") : qsTr("Not logged in")
                textStyle.base: SystemDefaults.TextStyles.SmallText
                textStyle.fontSize: FontSize.PercentageValue
                // 47: at 55 HyperBerry's Italian line ran to the frame's edge.
                textStyle.fontSizeValue: 47
                textStyle.fontWeight: FontWeight.Bold
                textStyle.color: quiet
                topMargin: 4
                bottomMargin: 0
            }
        }
        // Active: three rows of the same height, a separator between each.
        Container {
            visible: !idle
            preferredWidth: 310
            preferredHeight: 211
            layout: StackLayout {
            }
            CoverRow {
                count: chats.frameCounts[0]
                label: qsTr("Private chats")
                detail: chats.frameNames[0]
                ink: cover.ink
                quiet: cover.quiet
                accent: cover.accent
                emojiStyle: nav.emojiStyle
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                preferredHeight: 2
                background: line
                topMargin: 0
                bottomMargin: 0
            }
            CoverRow {
                count: chats.frameCounts[1]
                label: qsTr("Groups")
                detail: chats.frameNames[1]
                ink: cover.ink
                quiet: cover.quiet
                accent: cover.accent
                emojiStyle: nav.emojiStyle
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                preferredHeight: 2
                background: line
                topMargin: 0
                bottomMargin: 0
            }
            CoverRow {
                count: chats.frameCounts[2]
                label: qsTr("Channels")
                detail: chats.frameNames[2]
                ink: cover.ink
                quiet: cover.quiet
                accent: cover.accent
                emojiStyle: nav.emojiStyle
            }
        }
    }
}
