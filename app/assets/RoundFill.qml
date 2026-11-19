import bb.cascades 1.4

// A button's shape in Round mode (Settings > Appearance > Shape, the user's ask, 2026-11-19): Cascades has no corner radius,
// so the colour a square button has as its `background` is this image instead, first in a DockLayout under the button's
// content, with `filterColor` set to that colour and the Container's own background left off. A disc (circle.png, scaled
// to fit the box), or a pill (pill.amd, nine-sliced so the ends stay round at any size: it is stretched to the box,
// Fill, since drawn at its own 42 px it was a small disc behind the text, the user, 2026-11-19). Shown only in Round mode:
// a list item, which may not reach `settings`, sets `visible` from its view (SendFilesPage's cells). The box is the
// Container's whole, so the padding goes in an inner Container, not on the one that holds this.
ImageView {
    property bool pill: false
    visible: settings.round
    horizontalAlignment: HorizontalAlignment.Fill
    verticalAlignment: VerticalAlignment.Fill
    imageSource: pill ? "asset:///images/pill.amd" : "asset:///images/circle.png"
    scalingMethod: pill ? ScalingMethod.Fill : ScalingMethod.AspectFit
    loadEffect: ImageViewLoadEffect.None
}
