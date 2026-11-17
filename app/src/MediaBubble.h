#ifndef MEDIABUBBLE_H
#define MEDIABUBBLE_H

#include <bb/cascades/Color>
#include <bb/cascades/CustomControl>
#include <QString>
#include <QVariantMap>

namespace bb {
namespace cascades {
class ActivityIndicator;
class Container;
class ImageView;
class Label;
class ListView;
class UIConfig;
}
}

// A photo, video, audio file, document, sticker, location or contact in a card (step 6.1), mirroring
// MediaBubble.qml. A picture is the message's thumbnail, a video with a play mark and its length; a
// file is a row of an icon, its name and its size. A tap opens it. list is the chat page's list,
// which holds the fetch (mediaLoadingId) and opens the file (openMedia). A sticker (step 6.5b) is
// its picture alone, small, with no caption and no tap: the model fetches its file by itself. A
// location (6.5c) is its map picture as a photo's, or without one a row to tap; the card's label
// and text under it are the card's, and a tap asks which maps app opens it. A contact (6.5d) is a
// row of the system's add-contact icon, its names and its number (or "N contacts"); a tap imports
// it into the Contacts app. A spoiler (6.6d) is a photo or video under a blurred cover with
// "Spoiler" on it and no play mark or length: a tap takes the cover off (the model's unspoil), the
// next opens it. The parts are all made here and shown by what the message is. Wide, in the
// picture card, a photo or video spans the card with no air around it, and stamp() is the corner
// at its bottom right where the card puts the time when there's no caption.
class MediaBubble : public bb::cascades::CustomControl
{
    Q_OBJECT
public:
    MediaBubble(bb::cascades::ListView *list, bool wide = false);

    bb::cascades::Container *stamp() const { return m_stamp; }

    // msg is a "msg" row of MessageModel with a media: photo, video, sticker, audio, document,
    // location or contact.
    void show(const QVariantMap &msg);

private slots:
    void refresh();
    void onTap();

private:
    QString str(const char *key) const { return m_msg.value(key).toString(); }
    bool flag(const char *key) const { return m_msg.value(key).toBool(); }
    void restyle();

    bb::cascades::ListView *m_list;
    bb::cascades::UIConfig *m_ui;
    QVariantMap m_msg;
    bool m_picture;
    bool m_sticker;
    float m_wide; // the picture's width in the picture card, 0 elsewhere

    // What the styling was last set from: restyle() runs again when one of them changes.
    bool m_styled;
    int m_body;
    int m_small;
    bb::cascades::Color m_plain;

    bb::cascades::Container *m_root;
    bb::cascades::Container *m_photo;
    bb::cascades::ImageView *m_thumb;
    bb::cascades::Container *m_none;
    bb::cascades::Label *m_noneText;
    bb::cascades::ImageView *m_play;
    bb::cascades::Label *m_length;
    bb::cascades::Container *m_cover;
    bb::cascades::Container *m_dim;
    bb::cascades::Container *m_stamp;
    bb::cascades::ActivityIndicator *m_photoSpin;
    bb::cascades::ImageView *m_stickerImage;
    bb::cascades::Label *m_stickerText;
    bb::cascades::Container *m_file;
    bb::cascades::ImageView *m_icon;
    bb::cascades::Label *m_emoji;
    bb::cascades::Label *m_name;
    bb::cascades::Label *m_info;
    bb::cascades::ActivityIndicator *m_fileSpin;
};

#endif
