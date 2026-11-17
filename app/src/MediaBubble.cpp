#include "MediaBubble.h"

#include <bb/cascades/ActivityIndicator>
#include <bb/cascades/Container>
#include <bb/cascades/DockLayout>
#include <bb/cascades/FontSize>
#include <bb/cascades/FontStyle>
#include <bb/cascades/FontWeight>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/Image>
#include <bb/cascades/ImageView>
#include <bb/cascades/ImageViewLoadEffect>
#include <bb/cascades/Label>
#include <bb/cascades/ListView>
#include <bb/cascades/ScalingMethod>
#include <bb/cascades/SystemDefaults>
#include <bb/cascades/TapHandler>
#include <bb/cascades/TextFormat>
#include <bb/cascades/TextStyleDefinition>
#include <bb/cascades/UIConfig>
#include <bb/cascades/UIPalette>
#include <bb/cascades/VerticalAlignment>
#include <QStringList>

#include "CardStyle.h"
#include "MessageModel.h"

using namespace bb::cascades;

namespace {

// The QML file's name: the translations of MediaBubble.qml's strings (hyperberry_it.ts) apply.
const char *const kContext = "MediaBubble";

// A plain label placed in its container's dock.
Label *docked(HorizontalAlignment::Type horizontal, VerticalAlignment::Type vertical)
{
    Label *label = new Label();
    label->setHorizontalAlignment(horizontal);
    label->setVerticalAlignment(vertical);
    label->setTextFormat(TextFormat::Plain);
    return label;
}

// A box over the whole of its container, with its own dock.
Container *cover()
{
    Container *box = new Container();
    box->setHorizontalAlignment(HorizontalAlignment::Fill);
    box->setVerticalAlignment(VerticalAlignment::Fill);
    box->setLayout(new DockLayout());
    return box;
}

// The spinner of a fetch, centred when its container is a dock.
ActivityIndicator *spinner()
{
    ActivityIndicator *spin = new ActivityIndicator();
    spin->setHorizontalAlignment(HorizontalAlignment::Center);
    spin->setVerticalAlignment(VerticalAlignment::Center);
    return spin;
}

}

MediaBubble::MediaBubble(ListView *list, bool wide)
    : m_list(list)
    , m_ui(list->ui())
    , m_picture(false)
    , m_sticker(false)
    , m_wide(0)
    , m_styled(false)
    , m_body(0)
    , m_small(0)
{
    // A caption follows a picture, with no margin of its own: the bottom margin is set by show().
    setTopMargin(0);
    m_root = new Container();
    TapHandler *tap = new TapHandler(m_root);
    m_root->addGestureHandler(tap);
    connect(tap, SIGNAL(tapped(bb::cascades::TapEvent *)), this, SLOT(onTap()));

    // A picture: the thumbnail, or the page's colour with its name, and what is over it.
    m_photo = new Container();
    m_photo->setLayout(new DockLayout());
    // Wide: the card's width.
    if (wide) {
        m_wide = Card::width(m_ui);
        m_photo->setHorizontalAlignment(HorizontalAlignment::Fill);
    }
    m_photo->setPreferredWidth(wide ? m_wide : m_ui->du(40));
    m_thumb = new ImageView();
    m_thumb->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_thumb->setVerticalAlignment(VerticalAlignment::Fill);
    m_thumb->setScalingMethod(ScalingMethod::AspectFill);
    m_thumb->setLoadEffect(ImageViewLoadEffect::None);

    // No thumbnail: the card is plain, so the box is the page's colour.
    m_none = cover();
    m_noneText = docked(HorizontalAlignment::Center, VerticalAlignment::Center);
    m_none->add(m_noneText);

    // The play mark of the Stories tiles (StoryTile.qml), not the U+25B6 glyph, which showed as an emoji.
    m_play = new ImageView();
    m_play->setHorizontalAlignment(HorizontalAlignment::Center);
    m_play->setVerticalAlignment(VerticalAlignment::Center);
    m_play->setPreferredSize(m_ui->du(12), m_ui->du(12)); // 2x the Stories tiles' (the user, 2026-11-12)
    m_play->setImageSource(Card::url("asset:///images/play.png"));
    m_play->setOpacity(0.85f);
    // At the left in the picture card: the time takes the right, as on Android.
    m_length = docked(wide ? HorizontalAlignment::Left : HorizontalAlignment::Right, VerticalAlignment::Bottom);
    m_length->textStyle()->setBase(SystemDefaults::TextStyles::smallText());
    m_length->textStyle()->setColor(Color::White);

    // A spoiler (step 6.6d): the picture under this is blurred (MediaData) until a tap.
    m_cover = cover();
    m_cover->setBackground(Card::color("#66000000"));
    Label *spoiler = docked(HorizontalAlignment::Center, VerticalAlignment::Center);
    spoiler->setText(Card::tr(kContext, "Spoiler"));
    spoiler->textStyle()->setFontWeight(FontWeight::Bold);
    spoiler->textStyle()->setColor(Color::White);
    m_cover->add(spoiler);
    // A picture or video being fetched: the preview is dimmed so the spinner shows (it was white, small and lost on
    // the picture, the user 2026-11-12), and the play mark gives way to it.
    m_dim = cover();
    m_dim->setBackground(Card::color("#99000000"));
    m_dim->setVisible(false);
    m_photoSpin = spinner();
    m_photoSpin->setPreferredSize(m_ui->du(9), m_ui->du(9));

    m_photo->add(m_thumb);
    m_photo->add(m_none);
    m_photo->add(m_play);
    m_photo->add(m_length);
    m_photo->add(m_cover);
    m_photo->add(m_dim);
    // The time's corner, 1 du in from the edges; empty unless the card puts the time in it.
    m_stamp = new Container();
    m_stamp->setHorizontalAlignment(HorizontalAlignment::Right);
    m_stamp->setVerticalAlignment(VerticalAlignment::Bottom);
    m_stamp->setRightPadding(m_ui->du(1));
    m_stamp->setBottomPadding(m_ui->du(1));
    m_photo->add(m_stamp);
    m_photo->add(m_photoSpin);

    // The sticker's picture, fit in a square; always an Image in thumb, an empty one without a
    // picture, as for a photo, so a recycled card never keeps the previous row's.
    m_stickerImage = new ImageView();
    m_stickerImage->setTopMargin(0);
    m_stickerImage->setPreferredWidth(m_ui->du(30));
    m_stickerImage->setPreferredHeight(m_ui->du(30));
    m_stickerImage->setScalingMethod(ScalingMethod::AspectFit);
    m_stickerImage->setLoadEffect(ImageViewLoadEffect::None);
    // Until it is here: no picture and no thumbnail.
    m_stickerText = new Label();
    m_stickerText->setTopMargin(0);
    m_stickerText->setBottomMargin(0);
    m_stickerText->setText(Card::tr(kContext, "Sticker"));

    // The files: an icon, the name over the size, and the spinner. It fills the card, so a long name
    // is cut by the column and the spinner is at the right.
    m_file = new Container();
    m_file->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_file->setLayout(Card::across());
    // The system's icon in the emoji's place, for a contact and a location.
    m_icon = new ImageView();
    m_icon->setVerticalAlignment(VerticalAlignment::Center);
    m_icon->setPreferredWidth(m_ui->du(5));
    m_icon->setPreferredHeight(m_ui->du(5));
    m_icon->setScalingMethod(ScalingMethod::AspectFit);
    m_emoji = new Label();
    m_emoji->setVerticalAlignment(VerticalAlignment::Center);

    Container *words = new Container();
    words->setVerticalAlignment(VerticalAlignment::Center);
    words->setLeftMargin(m_ui->du(1));
    Card::quota(words, 1);
    m_name = new Label();
    m_name->setBottomMargin(0);
    m_name->setMultiline(false);
    // The size, and the length of an audio file when it is known; a contact's number.
    m_info = new Label();
    m_info->setTopMargin(0);
    m_info->setBottomMargin(0);
    m_info->setMultiline(false);
    words->add(m_name);
    words->add(m_info);

    m_fileSpin = new ActivityIndicator();
    m_fileSpin->setVerticalAlignment(VerticalAlignment::Center);
    m_fileSpin->setLeftMargin(m_ui->du(1));
    m_file->add(m_icon);
    m_file->add(m_emoji);
    m_file->add(words);
    m_file->add(m_fileSpin);

    m_root->add(m_photo);
    m_root->add(m_stickerImage);
    m_root->add(m_stickerText);
    m_root->add(m_file);
    setRoot(m_root);

    // The fetch, as the QML bound it.
    if (!connect(list, SIGNAL(mediaLoadingIdChanged()), this, SLOT(refresh())))
        qWarning("cards: the list has no mediaLoadingIdChanged");
}

// What the text size and the theme's colours decide, set again only when one of them changed.
void MediaBubble::restyle()
{
    const int body = Card::bodySize(m_list);
    const int small = Card::smallSize(m_list);
    const Color plain = m_ui->palette()->plain();
    if (m_styled && body == m_body && small == m_small && plain == m_plain)
        return;
    m_styled = true;
    m_body = body;
    m_small = small;
    m_plain = plain;

    const Color quiet = m_ui->palette()->secondaryTextOnPlain();
    m_none->setBackground(m_ui->palette()->background());
    Card::style(m_noneText, m_list, body);
    m_noneText->textStyle()->setFontStyle(FontStyle::Italic);
    m_noneText->textStyle()->setColor(quiet);
    Card::style(m_stickerText, m_list, body);
    m_stickerText->textStyle()->setFontStyle(FontStyle::Italic);
    m_stickerText->textStyle()->setColor(quiet);
    Card::style(m_emoji, m_list, 200);
    Card::style(m_name, m_list, body);
    Card::style(m_info, m_list, small);
    m_info->textStyle()->setColor(quiet);
}

void MediaBubble::show(const QVariantMap &msg)
{
    m_msg = msg;
    const QString media = str("media");
    const bool location = media == "location";
    const bool contact = media == "contact";
    const bool video = media == "video";
    const bool spoiler = flag("spoiler");
    const bool hasThumb = flag("hasThumb");
    m_picture = media == "photo" || video || (location && hasThumb);
    m_sticker = media == "sticker";
    restyle();

    // A caption follows a picture, with no margin of its own.
    setBottomMargin(str("text").isEmpty() || m_wide ? 0 : m_ui->du(0.5));
    // Air above and below a picture, so it doesn't touch the card's name line or caption (user);
    // none in the picture card, where it spans the card.
    const float air = (m_picture || m_sticker) && !m_wide ? m_ui->du(1) : 0;
    m_root->setTopPadding(air);
    m_root->setBottomPadding(air);

    m_photo->setVisible(m_picture);
    if (m_picture) {
        // A tall photo is cut at 50 du; AspectFill crops it. In the picture card it is square,
        // cropped the same way, and seen whole when opened (the user, 2026-11-08).
        m_photo->setPreferredHeight(m_wide ? m_wide
                                           : qMin(m_ui->du(50), m_ui->du(40) * float(m_msg.value("ratio").toDouble())));
        m_thumb->setVisible(hasThumb);
        m_thumb->setImage(m_msg.value("thumb").value<Image>());
        m_none->setVisible(!hasThumb);
        m_noneText->setText(video ? Card::tr(kContext, "Video") : Card::tr(kContext, "Photo"));
        m_play->setVisible(video && !spoiler);
        m_length->setVisible(video && !str("durationText").isEmpty() && !spoiler);
        m_length->setText(str("durationText"));
        m_cover->setVisible(spoiler);
    }

    m_stickerImage->setVisible(m_sticker && hasThumb);
    m_stickerText->setVisible(m_sticker && !hasThumb);
    if (m_sticker)
        m_stickerImage->setImage(m_msg.value("thumb").value<Image>());

    const bool file = !m_picture && !m_sticker;
    m_file->setVisible(file);
    if (file) {
        // "" for the files, which are not loaded then.
        const char *icon = contact ? "file:///usr/share/icons/ic_add_contact.png"
                           : location ? "file:///usr/share/icons/ic_map_all.png" : "";
        m_icon->setVisible(*icon != 0);
        if (*icon)
            m_icon->setImageSource(Card::url(icon));
        m_emoji->setVisible(*icon == 0);
        // U+1F3B5 and U+1F4C4
        m_emoji->setText(QString::fromUtf8(media == "audio" ? "\xF0\x9F\x8E\xB5" : "\xF0\x9F\x93\x84"));

        const QString fileName = str("fileName");
        m_name->setText(contact ? str("summary")
                        : location ? Card::tr(kContext, "Show on map")
                        : media == "audio" ? Card::tr(kContext, "Audio file")
                        : (fileName.isEmpty() ? Card::tr(kContext, "Document") : fileName));

        // The size, and the length of an audio file when it is known; a contact's number.
        const QString contactInfo = str("contactInfo");
        m_info->setVisible(!location && (!contact || !contactInfo.isEmpty()));
        QStringList parts;
        if (!str("sizeText").isEmpty())
            parts << str("sizeText");
        if (!str("durationText").isEmpty())
            parts << str("durationText");
        m_info->setText(contact ? contactInfo : parts.join(QString::fromUtf8(" \xC2\xB7 ")));
    }
    refresh();
}

// The fetch: the spinner of whichever part is shown.
void MediaBubble::refresh()
{
    if (m_msg.isEmpty())
        return;
    const bool loading = m_list->property("mediaLoadingId").toString() == str("id");
    m_photoSpin->setRunning(loading && m_picture);
    m_dim->setVisible(loading && m_picture);
    m_play->setVisible(str("media") == "video" && !flag("spoiler") && !loading);
    m_fileSpin->setRunning(loading && !m_picture && !m_sticker);
}

void MediaBubble::onTap()
{
    if (flag("spoiler")) {
        // The cover comes off; the next tap opens it.
        MessageModel *model = qobject_cast<MessageModel *>(m_list->dataModel());
        if (model)
            model->unspoil(str("id"));
    } else if (!m_sticker) {
        Card::call(m_list, "openMedia", m_msg);
    }
}
