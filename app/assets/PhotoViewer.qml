import bb.cascades 1.4
import bb.system 1.2
import hyperberry 1.0

// A received photo, full screen on black (step 6.1); pinch zooms from 1 to 4 times and a
// dragged finger pans. path is the file's absolute path, set right after createObject().
// ponytail: the whole photo is decoded at once; a tiled viewer if a large one runs short of memory.
Page {
    id: page
    property string path
    attachedObjects: [
        MediaOpener {
            id: saver
        },
        SystemToast {
            id: savedToast
        }
    ]
    actions: [
        ActionItem {
            title: qsTr("Save")
            imageSource: "asset:///images/save_action.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: {
                savedToast.body = saver.saveToPictures(page.path) != "" ? qsTr("Saved to Pictures")
                                                                       : qsTr("Couldn't save the photo")
                savedToast.show()
            }
        },
        // The uri follows the path: the query is asked again when it changes (the SDK's idiom).
        InvokeActionItem {
            id: share
            title: qsTr("Share")
            query {
                mimeType: "image/jpeg"
                invokeActionId: "bb.action.SHARE"
                uri: "file://" + page.path
                onQueryChanged: share.query.updateQuery()
            }
        }
    ]
    Container {
        id: root
        // The page's size, for the picture: inside a ScrollView it would size to its pixels.
        property real w: 0
        property real h: 0
        background: Color.Black
        layout: DockLayout {
        }
        attachedObjects: LayoutUpdateHandler {
            onLayoutFrameChanged: {
                root.w = layoutFrame.width
                root.h = layoutFrame.height
            }
        }
        ScrollView {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            scrollViewProperties {
                scrollMode: ScrollMode.Both
                pinchToZoomEnabled: true
                minContentScale: 1
                maxContentScale: 4
            }
            ImageView {
                preferredWidth: root.w
                preferredHeight: root.h
                imageSource: "file://" + page.path
                scalingMethod: ScalingMethod.AspectFit
                loadEffect: ImageViewLoadEffect.None
            }
        }
    }
}
