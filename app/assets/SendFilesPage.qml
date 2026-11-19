import bb.cascades 1.4

// Several files picked to send (the user's ask, 2026-11-18): a carousel of them, one large at a time with the next
// peeking (a 3-column grid made them small on the Classic, and the checkboxes under it didn't fit), a cross on each
// that drops it, a tap on a picture or a video opens the system's editor. Under it two square toggles, Album and As files, on one
// row, and the caption. No title bar: it didn't fit with one.
// The page only collects: send() goes to the chat page (ChatPage.sendFiles), which sends and pops this one.
// items are {path, png, info} (png: the PNG a JPEG was made from, "" for none; info: MediaOpener.fileInfo's), caption
// the composer's text, media the chat page's MediaOpener (the editor's answer comes to its pictureEdited). All three
// are set right after createObject(), media first.
Page {
    id: page
    property variant items
    property string caption: ""
    property variant media
    // The cell the editor was opened on, until it hands a file back (edited).
    property int editing: -1
    signal send(variant items, bool group, bool asFile, string caption)
    // No title bar: on the Classic the page didn't fit with one (the user, 2026-11-18).
    // kind: 0 a picture (with its preview), 1 a video, 2 anything else. info keeps no image: the model holds plain maps.
    function entry(path, png, info) {
        var ext = path.substring(path.lastIndexOf(".") + 1).toLowerCase()
        var preview = ext == "jpg" || ext == "jpeg" ? media.composePreview(path) : ""
        var video = ext == "mp4" || ext == "3gp" || ext == "mov"
        return {"path": path, "png": png, "preview": preview, "kind": preview != "" ? 0 : video ? 1 : 2,
                "info": {"name": info.name, "sizeText": info.sizeText, "picture": info.picture}}
    }
    onItemsChanged: {
        files.clear()
        for (var i = 0; i < items.length; i++) {
            files.append(entry(items[i].path, items[i].png, items[i].info))
        }
    }
    // The cross: the file goes; the last one takes the page with it.
    function drop(index) {
        files.removeAt(index)
        if (files.size() == 0 && nav.top == page) {
            nav.pop()
        }
    }
    function edit(index) {
        editing = index
        var item = files.value(index)
        media.editPicture(item.png != "" ? item.png : item.path)
    }
    // The editor's file takes the place of the one it was opened on, a PNG made a JPEG as ChatPage.stage does.
    function edited(path) {
        if (editing < 0 || editing >= files.size()) {
            return
        }
        var png = ""
        if (/\.png$/i.test(path)) {
            var jpg = media.asJpeg(path)
            if (jpg) {
                png = path
                path = jpg
            }
        }
        var info = media.fileInfo(path)
        if (info.name) {
            files.replace(editing, entry(path, png, info))
        }
        editing = -1
    }
    function all() {
        var list = []
        for (var i = 0; i < files.size(); i++) {
            list.push(files.value(i))
        }
        return list
    }
    // A square toggle: the primary colour when on (the UI is square, the user); a pill in Round mode (the user's ask, 2026-11-19).
    function chipColor(on) {
        return on ? ui.palette.primary : Color.create("#40808080")
    }
    property bool group: true
    property bool asFile: false
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        // The carousel takes a share of the page, a little under all that's left (the user, 2026-11-18): 58 % left
        // 140 px empty on the Classic, too small; the toggles and the caption take about 22 %.
        attachedObjects: LayoutUpdateHandler {
            onLayoutFrameChanged: carousel.preferredHeight = Math.round(layoutFrame.height * 0.72)
        }
        ListView {
            id: carousel
            // A cell is the list's height and its width less a strip where the next file shows. The last cell is wider
            // by that strip, so it too can line up with the list's leading edge: narrower, the snap couldn't reach it
            // and pulled it back, a jump at every step with two files (the user, 2026-11-18).
            property real peek: ui.du(7)
            // A list item has its own context, without nav (ReferenceError on the phone): the cells read these.
            property variant emojiStyle: nav.emojiStyle
            property real bodySize: nav.bodySize
            property real smallSize: nav.smallSize
            property real cellWidth: 0
            property int count: 0
            // The pencil and the cross are discs in Round mode: a list item reaches settings only through its view.
            property bool round: settings.round
            attachedObjects: LayoutUpdateHandler {
                onLayoutFrameChanged: carousel.cellWidth = layoutFrame.width - carousel.peek
            }
            // The cells reach the page through this list (ListItem.view).
            function drop(index) {
                page.drop(index)
            }
            function edit(index) {
                page.edit(index)
            }
            dataModel: ArrayDataModel {
                id: files
                onItemAdded: carousel.count = size()
                onItemRemoved: carousel.count = size()
                onItemsChanged: carousel.count = size()
            }
            layout: StackListLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            snapMode: SnapMode.LeadingEdge
            flickMode: FlickMode.SingleItem
            scrollIndicatorMode: ScrollIndicatorMode.None
            listItemComponents: [
                ListItemComponent {
                    Container {
                        id: cell
                        property bool last: cell.ListItem.indexPath[0] == cell.ListItem.view.count - 1
                        preferredWidth: cell.ListItem.view.cellWidth + (cell.last ? cell.ListItem.view.peek : 0)
                        preferredHeight: cell.ListItem.view.preferredHeight
                        topPadding: ui.du(1)
                        bottomPadding: ui.du(1)
                        leftPadding: ui.du(1)
                        rightPadding: cell.last ? cell.ListItem.view.peek : 0
                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment: VerticalAlignment.Fill
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                            layout: DockLayout {
                            }
                            background: Color.create("#20808080")
                            // The whole picture, from the cached preview (MediaOpener.composePreview).
                            ImageView {
                                visible: ListItemData.kind == 0
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment: VerticalAlignment.Fill
                                imageSource: ListItemData.preview != "" ? "file://" + ListItemData.preview : ""
                                scalingMethod: ScalingMethod.AspectFit
                                loadEffect: ImageViewLoadEffect.None
                                gestureHandlers: TapHandler {
                                    onTapped: cell.ListItem.view.edit(cell.ListItem.indexPath[0])
                                }
                            }
                            // A video, a document or an audio file: its mark, name and size. A tap edits a video.
                            Container {
                                visible: ListItemData.kind != 0
                                gestureHandlers: TapHandler {
                                    onTapped: if (ListItemData.kind == 1) cell.ListItem.view.edit(cell.ListItem.indexPath[0])
                                }
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment: VerticalAlignment.Center
                                leftPadding: ui.du(2)
                                rightPadding: ui.du(2)
                                Label {
                                    horizontalAlignment: HorizontalAlignment.Center
                                    // U+1F3AC, the clapper board, or U+1F4CE, the paperclip.
                                    text: ListItemData.kind == 1 ? "🎬" : "📎"
                                    textFormat: TextFormat.Plain
                                    textStyle.base: cell.ListItem.view.emojiStyle
                                    textStyle.fontSize: FontSize.PercentageValue
                                    textStyle.fontSizeValue: 300
                                }
                                Label {
                                    horizontalAlignment: HorizontalAlignment.Center
                                    multiline: true
                                    text: ListItemData.info.name
                                    textFormat: TextFormat.Plain
                                    textStyle.base: cell.ListItem.view.emojiStyle
                                    textStyle.textAlign: TextAlign.Center
                                    textStyle.fontSize: FontSize.PercentageValue
                                    textStyle.fontSizeValue: cell.ListItem.view.bodySize
                                }
                                Label {
                                    horizontalAlignment: HorizontalAlignment.Center
                                    multiline: false
                                    text: ListItemData.info.sizeText
                                    textFormat: TextFormat.Plain
                                    textStyle.base: cell.ListItem.view.emojiStyle
                                    textStyle.fontSize: FontSize.PercentageValue
                                    textStyle.fontSizeValue: cell.ListItem.view.smallSize
                                    textStyle.color: ui.palette.secondaryTextOnPlain
                                }
                            }
                            // Which file of how many, in the lower left corner.
                            Container {
                                horizontalAlignment: HorizontalAlignment.Left
                                verticalAlignment: VerticalAlignment.Bottom
                                leftPadding: ui.du(1)
                                rightPadding: ui.du(1)
                                background: Color.create("#99000000")
                                Label {
                                    text: (cell.ListItem.indexPath[0] + 1) + " / " + cell.ListItem.view.count
                                    textFormat: TextFormat.Plain
                                    textStyle.color: Color.White
                                    textStyle.fontSize: FontSize.PercentageValue
                                    textStyle.fontSizeValue: cell.ListItem.view.bodySize
                                }
                            }
                            // A tap edits a picture or a video: a pencil, a square in the lower right corner, says so (the user, 2026-11-18).
                            // The padded box is inside a Container of its own, so that the disc of Round mode covers the padding too.
                            Container {
                                visible: ListItemData.kind <= 1
                                horizontalAlignment: HorizontalAlignment.Right
                                verticalAlignment: VerticalAlignment.Bottom
                                background: cell.ListItem.view.round ? Color.Transparent : Color.create("#99000000")
                                layout: DockLayout {}
                                gestureHandlers: TapHandler {
                                    onTapped: cell.ListItem.view.edit(cell.ListItem.indexPath[0])
                                }
                                RoundFill {
                                    visible: cell.ListItem.view.round
                                    filterColor: Color.create("#99000000")
                                }
                                Container {
                                    leftPadding: ui.du(1)
                                    rightPadding: ui.du(1)
                                    topPadding: ui.du(1)
                                    bottomPadding: ui.du(1)
                                    ImageView {
                                        imageSource: "file:///usr/share/icons/ic_edit.png"
                                        preferredWidth: ui.du(5)
                                        preferredHeight: ui.du(5)
                                        scalingMethod: ScalingMethod.AspectFit
                                        filterColor: Color.White
                                    }
                                }
                            }
                            // The cross, a square in the corner, as the pencil is; a sibling of the picture, so its tap isn't the picture's.
                            Container {
                                horizontalAlignment: HorizontalAlignment.Right
                                verticalAlignment: VerticalAlignment.Top
                                background: cell.ListItem.view.round ? Color.Transparent : Color.create("#99000000")
                                layout: DockLayout {}
                                gestureHandlers: TapHandler {
                                    onTapped: cell.ListItem.view.drop(cell.ListItem.indexPath[0])
                                }
                                RoundFill {
                                    visible: cell.ListItem.view.round
                                    filterColor: Color.create("#99000000")
                                }
                                Container {
                                    leftPadding: ui.du(2)
                                    rightPadding: ui.du(2)
                                    topPadding: ui.du(1.5)
                                    bottomPadding: ui.du(1.5)
                                    Label {
                                        text: "✕"
                                        textFormat: TextFormat.Plain
                                        textStyle.color: Color.White
                                    }
                                }
                            }
                        }
                    }
                }
            ]
        }
        // The two choices, square toggles on one row; pills in Round mode.
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            leftPadding: ui.du(1)
            rightPadding: ui.du(1)
            topPadding: ui.du(1)
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            Container {
                rightMargin: ui.du(1)
                background: settings.round ? Color.Transparent : page.chipColor(page.group)
                layout: DockLayout {}
                gestureHandlers: TapHandler {
                    onTapped: page.group = !page.group
                }
                RoundFill {
                    pill: true
                    filterColor: page.chipColor(page.group)
                }
                Container {
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(1)
                    bottomPadding: ui.du(1)
                    Label {
                        text: (page.group ? "✓ " : "") + qsTr("Album")
                        textFormat: TextFormat.Plain
                        textStyle.color: page.group ? Color.White : ui.palette.textOnPlain
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: nav.bodySize
                    }
                }
            }
            Container {
                background: settings.round ? Color.Transparent : page.chipColor(page.asFile)
                layout: DockLayout {}
                gestureHandlers: TapHandler {
                    onTapped: page.asFile = !page.asFile
                }
                RoundFill {
                    pill: true
                    filterColor: page.chipColor(page.asFile)
                }
                Container {
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(1)
                    bottomPadding: ui.du(1)
                    Label {
                        text: (page.asFile ? "✓ " : "") + qsTr("As files")
                        textFormat: TextFormat.Plain
                        textStyle.color: page.asFile ? Color.White : ui.palette.textOnPlain
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: nav.bodySize
                    }
                }
            }
        }
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            leftPadding: ui.du(1)
            rightPadding: ui.du(1)
            topPadding: ui.du(1)
            bottomPadding: ui.du(1)
            TextArea {
                id: captionField
                hintText: qsTr("Caption")
                text: page.caption
                textFormat: TextFormat.Plain
                textStyle.base: nav.emojiStyle
                autoSize.maxLineCount: 2
            }
        }
    }
    actions: [
        ActionItem {
            title: qsTr("Send")
            imageSource: "file:///usr/share/icons/bb_action_sendtextmessage.png"
            ActionBar.placement: ActionBarPlacement.Signature
            onTriggered: page.send(page.all(), page.group, page.asFile, captionField.text.trim())
        }
    ]
}
