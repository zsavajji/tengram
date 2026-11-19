import bb.cascades 1.4

// About, from the swipe-down menu beside Settings, laid out as My profile (ProfilePage.qml): the
// logo for the picture, the app's name, its version where the number goes, then what it is and the
// author's GitHub, opening in the browser.
Page {
    id: page
    titleBar: PageBar {
        title: qsTr("About")
    }
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ScrollView {
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(3)
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    // 16 du is the icon's own 144 px on the Q10.
                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Center
                        preferredWidth: ui.du(16)
                        preferredHeight: ui.du(16)
                        scalingMethod: ScalingMethod.AspectFit
                        imageSource: "asset:///images/icon-144.png"
                    }
                    Label {
                        horizontalAlignment: HorizontalAlignment.Center
                        bottomMargin: 0
                        text: "Tengram"
                        textFormat: TextFormat.Plain
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: nav.bodySize * 1.3
                    }
                    Label {
                        horizontalAlignment: HorizontalAlignment.Center
                        topMargin: 0
                        text: appVersion
                        textFormat: TextFormat.Plain
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: nav.bodySize
                        textStyle.color: ui.palette.secondaryTextOnPlain
                    }
                }
                Header {
                    title: qsTr("About")
                }
                // Padding, not margins: margins only space siblings, never from the container's edge.
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(1.5)
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    bottomPadding: ui.du(1.5)
                    Label {
                        horizontalAlignment: HorizontalAlignment.Fill
                        multiline: true
                        text: qsTr("Telegram for BlackBerry 10.")
                        textFormat: TextFormat.Plain
                        textStyle.fontSize: FontSize.PercentageValue
                        textStyle.fontSizeValue: nav.bodySize
                    }
                }
                Header {
                    title: qsTr("Developer")
                }
                LinkRow {
                    text: "github.com/zsavajji"
                    url: "https://github.com/zsavajji"
                }
                // What the app and its engine are built from (scripts/build-deps.sh, the fetch-*.sh scripts, app.pro), with
                // their licences; a row opens the project's page. Names and licences stay untranslated.
                Header {
                    title: qsTr("Open source libraries")
                }
                LinkRow {
                    text: "TDLib (TDLight) · Boost Software License 1.0"
                    url: "https://github.com/tdlight-team/tdlight"
                }
                LinkRow {
                    text: "OpenSSL 3.5.7 · Apache License 2.0"
                    url: "https://www.openssl.org"
                }
                LinkRow {
                    text: "zlib · zlib License"
                    url: "https://zlib.net"
                }
                LinkRow {
                    text: "SQLCipher (TDLib's, on SQLite) · BSD 3-Clause; SQLite public domain"
                    url: "https://www.zetetic.net/sqlcipher/"
                }
                LinkRow {
                    text: "Qt 4.8.6 SQLite driver · LGPL 2.1"
                    url: "https://github.com/qt/qt/tree/v4.8.6/src/sql/drivers/sqlite"
                }
                LinkRow {
                    text: "libopus 1.6.1 · BSD 3-Clause"
                    url: "https://opus-codec.org"
                }
                LinkRow {
                    text: "libwebp 1.4.0 · BSD 3-Clause"
                    url: "https://chromium.googlesource.com/webm/libwebp"
                }
                LinkRow {
                    text: "QR Code generator 1.8.0 (Nayuki) · MIT"
                    url: "https://www.nayuki.io/page/qr-code-generator-library"
                }
                LinkRow {
                    text: "OpenMAX IL 1.1.2 headers · Khronos (MIT-style)"
                    url: "https://www.khronos.org/openmax/il/"
                }
                LinkRow {
                    text: "Noto Color Emoji · SIL Open Font License 1.1"
                    url: "https://github.com/googlefonts/noto-emoji"
                }
            }
        }
    }
}
