import bb.cascades 1.4

// Several files picked to send (the user's ask, 2026-11-18): a grid of them, a cross on each that drops it, a tap on a
// picture opens the system's editor. Below the grid, whether they go as one album, whether as files, and the caption.
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
    titleBar: TitleBar {
        title: qsTr("Send files")
    }
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
    Container {
        ListView {
            id: grid
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
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
            }
            layout: GridListLayout {
                columnCount: 3
                cellAspectRatio: 1
            }
            listItemComponents: [
                ListItemComponent {
                    Container {
                        id: cell
                        layout: DockLayout {
                        }
                        background: ui.palette.plain
                        // The picture, from the cached preview (MediaOpener.composePreview).
                        ImageView {
                            visible: ListItemData.kind == 0
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment: VerticalAlignment.Fill
                            imageSource: ListItemData.preview != "" ? "file://" + ListItemData.preview : ""
                            scalingMethod: ScalingMethod.AspectFill
                            loadEffect: ImageViewLoadEffect.None
                            gestureHandlers: TapHandler {
                                onTapped: cell.ListItem.view.edit(cell.ListItem.indexPath[0])
                            }
                        }
                        // A video, a document or an audio file: its mark, name and size.
                        Container {
                            visible: ListItemData.kind != 0
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment: VerticalAlignment.Center
                            leftPadding: ui.du(1)
                            rightPadding: ui.du(1)
                            Label {
                                horizontalAlignment: HorizontalAlignment.Center
                                // U+1F3AC, the clapper board, or U+1F4CE, the paperclip.
                                text: ListItemData.kind == 1 ? "🎬" : "📎"
                                textFormat: TextFormat.Plain
                                textStyle.base: nav.emojiStyle
                                textStyle.fontSize: FontSize.PercentageValue
                                textStyle.fontSizeValue: 150
                            }
                            Label {
                                horizontalAlignment: HorizontalAlignment.Fill
                                multiline: false
                                text: ListItemData.info.name
                                textFormat: TextFormat.Plain
                                textStyle.base: nav.emojiStyle
                                textStyle.fontSize: FontSize.PercentageValue
                                textStyle.fontSizeValue: nav.smallSize
                            }
                            Label {
                                horizontalAlignment: HorizontalAlignment.Center
                                multiline: false
                                text: ListItemData.info.sizeText
                                textFormat: TextFormat.Plain
                                textStyle.base: nav.emojiStyle
                                textStyle.fontSize: FontSize.PercentageValue
                                textStyle.fontSizeValue: nav.smallSize
                                textStyle.color: ui.palette.secondaryTextOnPlain
                            }
                        }
                        // The cross, a square in the corner; a sibling of the picture, so its tap isn't the picture's.
                        Container {
                            horizontalAlignment: HorizontalAlignment.Right
                            verticalAlignment: VerticalAlignment.Top
                            leftPadding: ui.du(1.5)
                            rightPadding: ui.du(1.5)
                            topPadding: ui.du(1)
                            bottomPadding: ui.du(1)
                            background: Color.create("#99000000")
                            gestureHandlers: TapHandler {
                                onTapped: cell.ListItem.view.drop(cell.ListItem.indexPath[0])
                            }
                            Label {
                                text: "✕"
                                textFormat: TextFormat.Plain
                                textStyle.base: nav.emojiStyle
                                textStyle.color: Color.White
                            }
                        }
                    }
                }
            ]
        }
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            leftPadding: ui.du(1.5)
            rightPadding: ui.du(1.5)
            CheckBox {
                id: groupBox
                text: qsTr("Group as album")
                checked: true
            }
            CheckBox {
                id: filesBox
                text: qsTr("Send as files")
                checked: false
            }
            TextArea {
                id: captionField
                hintText: qsTr("Caption")
                text: page.caption
                textFormat: TextFormat.Plain
                textStyle.base: nav.emojiStyle
                autoSize.maxLineCount: 3
            }
        }
    }
    actions: [
        ActionItem {
            title: qsTr("Send")
            imageSource: "file:///usr/share/icons/bb_action_sendtextmessage.png"
            ActionBar.placement: ActionBarPlacement.Signature
            onTriggered: page.send(page.all(), groupBox.checked, filesBox.checked, captionField.text.trim())
        }
    ]
}
