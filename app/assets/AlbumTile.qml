import bb.cascades 1.4

// One square of an album (step 6.5g), in the card (AlbumBubble) and on the album's page (AlbumPage):
// the member's thumbnail, aspect fill, and a play mark on a video as MediaBubble draws it. The size
// is not set here: the card sets it, and the page's grid gives its cells'. member is a message map; its
// thumb is always an Image, empty without a thumbnail, so a recycled tile never keeps the picture of
// the row it showed before. more, "+N", darkens the tile under that text (the one that opens the page).
Container {
    id: tile
    property variant member
    property string more: ""
    // The chat list's mediaLoadingId: the member being fetched shows a spinner, as in MediaBubble.
    property string loadingId: ""
    signal tapped
    layout: DockLayout {
    }
    // The page's colour behind a tile with no thumbnail.
    background: ui.palette.background
    gestureHandlers: TapHandler {
        onTapped: tile.tapped()
    }
    ImageView {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        image: tile.member.thumb
        scalingMethod: ScalingMethod.AspectFill
        loadEffect: ImageViewLoadEffect.None
    }
    ImageView {
        visible: tile.member.media == "video"
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        preferredWidth: ui.du(6)
        preferredHeight: ui.du(6)
        imageSource: "asset:///images/play.png"
        opacity: 0.85
    }
    ActivityIndicator {
        visible: tile.loadingId == tile.member.id
        running: tile.loadingId == tile.member.id
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
    }
    Container {
        visible: tile.more != ""
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        background: Color.create("#99000000")
        layout: DockLayout {
        }
        Label {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            text: tile.more
            textFormat: TextFormat.Plain
            textStyle.fontSize: FontSize.XXLarge
            textStyle.fontWeight: FontWeight.Bold
            textStyle.color: Color.White
        }
    }
}
