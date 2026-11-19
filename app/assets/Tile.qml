import bb.cascades 1.4

// The tile of a person or a chat in a list or a title bar, as the chat list's (ChatRows.cpp): the initials on the colour
// with the picture over them. A square, or in Round mode (Settings > Appearance > Shape, the user's ask, 2026-11-19) a
// disc: Cascades has no corner radius and no clipping, so the colour is circle.png tinted and the picture is the round
// copy settings.roundPhoto makes of the file (the square picture if it can't). The caller sets the size, alignment and margins.
// `look` is what says the Shape and makes the copies: `settings`. A list item's context has no ids, and may have no
// settings (the lists carry the shared emoji style for it the same way), so a list passes its own `look: settings` and the
// row hands it on; a list without one leaves the tile square.
Container {
    id: tile
    property variant look: settings
    // The colour as "#RRGGBB", "" for none.
    property string tint
    property string initials
    // The picture's file, "" for none.
    property string photo
    property variant emojiStyle
    property int initialsSize: 118
    property bool round: !!tile.look && tile.look.round
    layout: DockLayout {}
    background: tile.round || tile.tint == "" ? Color.Transparent : Color.create(tile.tint)
    ImageView {
        visible: tile.round && tile.photo == ""
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        imageSource: "asset:///images/circle.png"
        scalingMethod: ScalingMethod.AspectFit
        loadEffect: ImageViewLoadEffect.None
        filterColor: tile.tint == "" ? Color.Transparent : Color.create(tile.tint)
    }
    Label {
        visible: !tile.round || tile.photo == ""
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        text: tile.initials
        textFormat: TextFormat.Plain
        textStyle.base: tile.emojiStyle
        textStyle.fontSize: FontSize.PercentageValue
        textStyle.fontSizeValue: tile.initialsSize
        textStyle.color: Color.White
    }
    // The picture over the initials; `tile.round` is in its expression, so a flip of the Shape evaluates it again.
    ImageView {
        visible: tile.photo != ""
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        imageSource: tile.photo == "" ? "" : "file://" + (tile.round ? (tile.look.roundPhoto(tile.photo) || tile.photo) : tile.photo)
        scalingMethod: ScalingMethod.AspectFill
        loadEffect: ImageViewLoadEffect.None
    }
}
