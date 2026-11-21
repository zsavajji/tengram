import bb.cascades 1.4

// About, from the swipe-down menu beside Settings, laid out as My profile (ProfilePage.qml): the
// logo for the picture, the app's name, its version where the number goes, then what it is, the
// author's GitHub, the open source libraries with their licences and the app's own licence, each opening in the browser.
Page {
    id: page
    // The logo's animation plays while this page is on top (main.qml's onTopChanged).
    property bool playsStickers: true
    titleBar: PageBar {
        title: qsTr("About")
    }
    Container {
        // The page colour the user picked (Settings > Appearance > Dark background).
        background: Color.create(nav.pageColor)
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        ScrollView {
            horizontalAlignment: HorizontalAlignment.Fill // filling the page from the start: as wide as its
            verticalAlignment: VerticalAlignment.Fill     // content, it opened narrow and grew (the user, 2026-11-20)
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(3)
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    // 16 du is the icon's own 144 px on the Q10. The welcome page's animation (the user, 2026-11-21:
                    // "add the tangram animation in the about"): the tangram square springs into the icon once, drawn
                    // at the view's own px, and again on a tap (Settings::playLogo).
                    ImageView {
                        id: logo
                        horizontalAlignment: HorizontalAlignment.Center
                        preferredWidth: ui.du(16)
                        preferredHeight: ui.du(16)
                        scalingMethod: ScalingMethod.AspectFit
                        loadEffect: ImageViewLoadEffect.None
                        onCreationCompleted: settings.playLogo(logo, Math.round(ui.du(16)))
                        gestureHandlers: TapHandler {
                            onTapped: settings.playLogo(logo, Math.round(ui.du(16)))
                        }
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
                // What the app and its engine are built from (scripts/build-deps.sh, engine/build.sh's tgcalls and tg_owt,
                // libtgs.so's rlottie, the fetch-*.sh scripts, app.pro), with
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
                    text: "tgcalls · LGPL 3.0"
                    url: "https://github.com/TelegramMessenger/tgcalls"
                }
                LinkRow {
                    text: "tg_owt (WebRTC) · BSD 3-Clause"
                    url: "https://github.com/desktop-app/tg_owt"
                }
                LinkRow {
                    text: "libopus 1.6.1 · BSD 3-Clause"
                    url: "https://opus-codec.org"
                }
                LinkRow {
                    text: "rlottie · LGPL 2.1; FreeType License, MIT"
                    url: "https://github.com/desktop-app/rlottie"
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
                    text: "OpenMAX IL headers (Android's) · Apache 2.0, Khronos (MIT-style)"
                    url: "https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/headers/media_plugin/media/openmax"
                }
                LinkRow {
                    text: "Noto Color Emoji · SIL Open Font License 1.1"
                    url: "https://github.com/googlefonts/noto-emoji"
                }
                LinkRow {
                    text: "Unicode emoji data 18.0 · Unicode License v3"
                    url: "https://www.unicode.org/emoji/"
                }
                Header {
                    title: qsTr("License")
                }
                LinkRow {
                    text: "Tengram · GNU GPL 3.0"
                    url: "https://www.gnu.org/licenses/gpl-3.0.html"
                }
            }
        }
    }
}
