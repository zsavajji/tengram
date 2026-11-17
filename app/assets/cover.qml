import bb.cascades 1.4

// The Active Frame (PLAN.md step 5.2). The Q10 shows all of its 310 x 211 px, above the system's
// title strip (screenshot, 0.0.1.76): the count at 250 % with the labels' default spacing pushed
// the last line under the strip. Margins are set, since unset ones put about 20 px between
// labels. It takes no touch. Created by main.qml, so nav and chats are in its scope.
SceneCover {
    content: Container {
        background: Color.Black
        preferredWidth: 310
        preferredHeight: 211
        layout: DockLayout {
        }
        // Unread: the count, and the chats' names under it (the user's design, 2026-11-01).
        Container {
            visible: chats.unreadTotal > 0
            horizontalAlignment: HorizontalAlignment.Fill
            leftPadding: 16
            rightPadding: 16
            topPadding: 12
            // The count and its word side by side: the count's line alone is about 86 px tall at
            // 200 %, and the word under it left the second line touching the strip (0.0.1.78).
            // Sizes follow the stock frames' small type (0.0.1.80 looked oversized beside
            // Settings').
            Container {
                topMargin: 0
                bottomMargin: 0
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                Label {
                    text: chats.unreadTotal
                    textStyle.base: SystemDefaults.TextStyles.BodyText
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: 130
                    textStyle.color: Color.create("#1BD862") // as the idle state's name
                    topMargin: 0
                    bottomMargin: 0
                }
                Label {
                    verticalAlignment: VerticalAlignment.Bottom
                    leftMargin: 12
                    text: qsTr("unread")
                    textStyle.base: SystemDefaults.TextStyles.SmallText
                    textStyle.fontSize: FontSize.PercentageValue
                    textStyle.fontSizeValue: 85
                    textStyle.color: Color.Gray
                    topMargin: 0
                    bottomMargin: 0
                }
            }
            // Newest first, no message text. The emoji style, so names' emoji draw (they do, in a
            // cover: 0.0.1.76), at the chat list's preview size; up to three lines, then "…".
            Label {
                text: chats.unreadChats
                textStyle.base: nav.emojiStyle
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: 74
                textStyle.color: Color.White
                multiline: true
                autoSize.maxLineCount: 3
                topMargin: 8
                bottomMargin: 0
            }
        }
        // Nothing unread, or not linked: centred, in small type, the name in the primary colour
        // (the user's, 2026-11-01). The bar descriptor's primary, written out: a cover's scene may
        // not share the app's palette.
        Container {
            visible: chats.unreadTotal == 0
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                text: "HyperBerry"
                textStyle.base: SystemDefaults.TextStyles.BodyText
                textStyle.color: Color.create("#1BD862")
                topMargin: 0
                bottomMargin: 0
            }
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                text: engine.linked ? qsTr("Nothing new to see") : qsTr("Not logged in")
                textStyle.base: SystemDefaults.TextStyles.SmallText
                textStyle.fontSize: FontSize.PercentageValue
                textStyle.fontSizeValue: 85
                textStyle.color: Color.Gray
                topMargin: 4
                bottomMargin: 0
            }
        }
    }
}
