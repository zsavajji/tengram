import bb.cascades 1.4

// A page's title bar in the app's colour (nav.headerColor) with white text, as the chat list's and the chat's: one bar
// colour across the app (the user's ask, 2026-11-19). 10.3's stock bar takes no colour (TitleBarAppearance.Branded drew
// it as always), so this is a FreeForm bar with the colour as its container's background. Used as a stock TitleBar is:
// `titleBar: PageBar { title: qsTr("…") }`.
TitleBar {
    id: bar
    kind: TitleBarKind.FreeForm
    scrollBehavior: TitleBarScrollBehavior.Sticky
    kindProperties: FreeFormTitleBarKindProperties {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: Color.create(nav.headerColor)
            leftPadding: ui.du(2)
            rightPadding: ui.du(2)
            layout: DockLayout {}
            Label {
                verticalAlignment: VerticalAlignment.Center
                text: bar.title
                textFormat: TextFormat.Plain
                textStyle.base: SystemDefaults.TextStyles.TitleText
                textStyle.color: Color.White
            }
        }
    }
}
