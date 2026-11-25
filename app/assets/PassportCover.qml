import bb.cascades 1.4

// The Passport's Active Frame (the user, 2026-11-25). Its home screen shows a big frame (440 x 486) or, with more apps
// open, a short one (440 x 195), where a lone SceneCover gave way to a picture of the app's screen. So a MultiCover: the
// big one is Cover.qml (four rows there), the short one four counts side by side under their icons: private chats,
// groups, channels, missed calls (ChatListModel::updateFrame). main.qml sets it on the Passport only; the other phones
// keep Cover.qml alone. Created by main.qml, so nav, chats and settings are in its scope.
MultiCover {
    Cover {
        id: big
        MultiCover.level: CoverDetailLevel.High
    }
    SceneCover {
        MultiCover.level: CoverDetailLevel.Medium
        content: Container {
            background: big.grey ? Color.create("#181818") : big.panel
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            leftPadding: 10
            rightPadding: 10
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            CoverCount {
                count: chats.frameCounts[0]
                icon: "asset:///images/message.png"
                quiet: big.quiet
                accent: big.accent
            }
            CoverCount {
                count: chats.frameCounts[1]
                icon: "asset:///images/contacts_action.png"
                quiet: big.quiet
                accent: big.accent
            }
            CoverCount {
                count: chats.frameCounts[2]
                icon: "asset:///images/megaphone_action.png"
                quiet: big.quiet
                accent: big.accent
            }
            CoverCount {
                count: chats.frameCounts[3]
                icon: "file:///usr/share/icons/ic_phone.png"
                quiet: big.quiet
                accent: big.accent
            }
        }
    }
}
