import bb.cascades 1.4

// Settings (step 5.4), now a list of groups, each its own page: saved as they change, so there is no Save
// button. Each setting lives in `settings`.
Page {
    id: page
    titleBar: PageBar {
        title: qsTr("Settings")
    }
    attachedObjects: [
        ComponentDefinition {
            id: profileDef
            source: "ProfilePage.qml"
        },
        ComponentDefinition {
            id: devicesDef
            source: "DevicesPage.qml"
        },
        ComponentDefinition {
            id: privacyDef
            source: "PrivacyPage.qml"
        },
        ComponentDefinition {
            id: appearanceDef
            source: "AppearanceSettingsPage.qml"
        },
        ComponentDefinition {
            id: chatSettingsDef
            source: "ChatSettingsPage.qml"
        },
        ComponentDefinition {
            id: notificationDef
            source: "NotificationSettingsPage.qml"
        },
        ComponentDefinition {
            id: dataDef
            source: "DataSettingsPage.qml"
        },
        ComponentDefinition {
            id: translationDef
            source: "TranslationSettingsPage.qml"
        }
    ]
    onCreationCompleted: {
        var items = []
        // Off while unlinked, as the menu's Profile entry is: there is no account to show.
        if (engine.linked) {
            items.push({ title: qsTr("Account"), description: qsTr("Name, bio, picture, log out"), target: "account" })
            items.push({ title: qsTr("Devices"), description: qsTr("Active sessions, link a desktop device"), target: "devices" })
            items.push({
                title: qsTr("Privacy and security"),
                description: qsTr("Password, blocked users, who sees what"),
                target: "privacy"
            })
        }
        items.push({ title: qsTr("Appearance"), description: qsTr("Theme, text size"), target: "appearance" })
        items.push({ title: qsTr("Chats"), description: qsTr("Enter key, proximity sensor"), target: "chats" })
        items.push({
            title: qsTr("Notifications"),
            description: qsTr("Preview, background delivery, start at boot"),
            target: "notifications"
        })
        items.push({ title: qsTr("Data and storage"), description: qsTr("Mobile data, cache"), target: "data" })
        items.push({ title: qsTr("Translation"), description: qsTr("Service, API key"), target: "translation" })
        rows.append(items)
    }
    function openPage(target) {
        if (target == "account") {
            nav.push(profileDef.createObject())
        } else if (target == "devices") {
            nav.push(devicesDef.createObject())
        } else if (target == "privacy") {
            nav.push(privacyDef.createObject())
        } else if (target == "appearance") {
            nav.push(appearanceDef.createObject())
        } else if (target == "chats") {
            nav.push(chatSettingsDef.createObject())
        } else if (target == "notifications") {
            nav.push(notificationDef.createObject())
        } else if (target == "data") {
            nav.push(dataDef.createObject())
        } else if (target == "translation") {
            nav.push(translationDef.createObject())
        }
    }
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        ListView {
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            dataModel: ArrayDataModel {
                id: rows
            }
            listItemComponents: ListItemComponent {
                StandardListItem {
                    title: ListItemData.title
                    description: ListItemData.description
                }
            }
            onTriggered: page.openPage(dataModel.data(indexPath).target)
        }
    }
}
