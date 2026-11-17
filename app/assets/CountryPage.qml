import bb.cascades 1.4
import "countries.js" as Countries
import "phone.js" as Phone

// The linking page's country picker (step 2.3): every country with its flag and calling code,
// in the phone's language. Typing filters by name or code; Enter takes the first match.
// picked(country) hands the choice back.
Page {
    id: page
    signal picked(variant country)

    titleBar: TitleBar {
        title: qsTr("Country")
    }

    function filter(text) {
        var t = text.toLowerCase().replace(/^\s+|\s+$/g, "")
        var digits = t.replace(/\D/g, "")
        var all = Phone.all(Countries.data, phoneLocale.language)
        var rows = []
        for (var i = 0; i < all.length; i++) {
            var c = all[i]
            if (t == "" || c.name.toLowerCase().indexOf(t) >= 0 || c.region.toLowerCase() == t
                    || (digits != "" && String(c.code).indexOf(digits) == 0))
                rows.push(c)
        }
        model.clear()
        model.append(rows)
    }
    onCreationCompleted: {
        filter("")
        search.requestFocus()
    }

    Container {
        TextField {
            id: search
            hintText: qsTr("Search by name or code")
            input.submitKey: SubmitKey.Search
            input.onSubmitted: {
                if (model.size() > 0)
                    page.picked(model.value(0))
            }
            onTextChanging: page.filter(text)
        }
        ListView {
            property variant emojiStyle: nav.emojiStyle
            property string pressColor: nav.pressColor
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            dataModel: ArrayDataModel {
                id: model
            }
            listItemComponents: ListItemComponent {
                Container {
                    id: item
                    horizontalAlignment: HorizontalAlignment.Fill
                    background: item.ListItem.active ? Color.create(item.ListItem.view.pressColor) : Color.Transparent
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        minHeight: ui.du(9)
                        leftPadding: ui.du(2)
                        rightPadding: ui.du(2)
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        Label {
                            verticalAlignment: VerticalAlignment.Center
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                            text: ListItemData.flag + "  " + ListItemData.name
                            textStyle.base: item.ListItem.view.emojiStyle
                        }
                        Label {
                            verticalAlignment: VerticalAlignment.Center
                            text: "+" + ListItemData.code
                            textStyle.color: ui.palette.secondaryTextOnPlain
                        }
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        preferredHeight: ui.px(2)
                        background: ui.palette.plain
                    }
                }
            }
            onTriggered: page.picked(dataModel.data(indexPath))
        }
    }
}
