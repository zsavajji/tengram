import bb.cascades 1.4

// A switch line of a settings page (Data and storage's): the title, and the switch at the right. `checked` is the
// switch's; the page binds it to a setting and writes the setting back on `toggled`, which follows every change of the
// switch. `active` false greys the switch out and the title with it.
Container {
    id: row
    property string title
    property alias checked: toggle.checked
    property alias active: toggle.enabled
    signal toggled(bool on)
    horizontalAlignment: HorizontalAlignment.Fill
    topPadding: ui.du(1.5)
    layout: StackLayout {
        orientation: LayoutOrientation.LeftToRight
    }
    Container {
        layoutProperties: StackLayoutProperties {
            spaceQuota: 1
        }
        verticalAlignment: VerticalAlignment.Center
        // The switch greys itself out; the title follows.
        opacity: row.active ? 1 : 0.5
        Label {
            text: row.title
            bottomMargin: 0
        }
    }
    ToggleButton {
        id: toggle
        verticalAlignment: VerticalAlignment.Center
        onCheckedChanged: row.toggled(checked)
    }
}
