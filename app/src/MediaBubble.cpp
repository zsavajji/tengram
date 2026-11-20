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
#include "Settings.h"
#include "StickerPlayer.h"

using namespace bb::cascades;

namespace {

// The QML file's name: the translations of MediaBubble.qml's strings (tengram_it.ts) apply.
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

// The ring of a fetch, as big as its spinner, centred when its container is a dock.
ImageView *ring(float size)
{
    ImageView *image = new ImageView();
    image->setHorizontalAlignment(HorizontalAlignment::Center);
    image->setVerticalAlignment(VerticalAlignment::Center);
    image->setPreferredSize(size, size);
    image->setVisible(false);
    return image;
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
    // The length as the time on a picture (MessageCard::placeTime): a chip of black at 50 %, 1 du in from its corner
    // (padding: a docked label's margins are ignored), the time's text size (the user, 2026-11-19).
    m_lengthBox = new Container();
    m_lengthBox->setHorizontalAlignment(wide ? HorizontalAlignment::Left : HorizontalAlignment::Right);
    m_lengthBox->setVerticalAlignment(VerticalAlignment::Bottom);
    m_lengthBox->setLeftPadding(m_ui->du(1));
    m_lengthBox->setRightPadding(m_ui->du(1));
    m_lengthBox->setBottomPadding(m_ui->du(1));
    Container *lengthChip = new Container();
    lengthChip->setBackground(Card::color("#80000000"));
    lengthChip->setLeftPadding(m_ui->du(1));
    lengthChip->setRightPadding(m_ui->du(1));
    lengthChip->setTopPadding(m_ui->du(0.5));
    lengthChip->setBottomPadding(m_ui->du(0.5));
    m_length = new Label();
    m_length->setTextFormat(TextFormat::Plain);
    m_length->textStyle()->setBase(SystemDefaults::TextStyles::smallText());
    m_length->textStyle()->setFontSize(FontSize::PercentageValue);
    m_length->textStyle()->setColor(Color::White);
    lengthChip->add(m_length);
    m_lengthBox->add(lengthChip);

    // A spoiler (step 6.6d): the picture under this is blurred (MediaData) until a tap.
    m_cover = cover();
    m_cover->setBackground(Card::color("#66000000"));
    Label *spoiler = docked(HorizontalAlignment::Center, VerticalAlignment::Center);
    spoiler->setText(Card::tr(kContext, QT_TRANSLATE_NOOP("MediaBubble", "Spoiler")));
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
    m_photoRing = ring(m_ui->du(9));

    m_photo->add(m_thumb);
    m_photo->add(m_none);
    m_photo->add(m_play);
    m_photo->add(m_cover);
    m_photo->add(m_dim);
    // The corners' masks (setCorner), over the picture and what dims it, under the time and the spinner. One image, the top
    // left's, turned by 90 degrees (clockwise) for each next corner: a turned square keeps its box, so the dock still puts it.
    for (int i = 0; i < 4; i++) {
        m_corner[i] = new ImageView();
        m_corner[i]->setHorizontalAlignment(i == 0 || i == 3 ? HorizontalAlignment::Left : HorizontalAlignment::Right);
        m_corner[i]->setVerticalAlignment(i < 2 ? VerticalAlignment::Top : VerticalAlignment::Bottom);
        m_corner[i]->setScalingMethod(ScalingMethod::Fill);
        m_corner[i]->setLoadEffect(ImageViewLoadEffect::None);
        m_corner[i]->setImageSource(Card::url("asset:///images/corner.png"));
        m_corner[i]->setRotationZ(90 * i);
        m_corner[i]->setVisible(false);
        m_photo->add(m_corner[i]);
    }
    // The time's corner, 1 du in from the edges; empty unless the card puts the time in it.
    m_stamp = new Container();
    m_stamp->setHorizontalAlignment(HorizontalAlignment::Right);
    m_stamp->setVerticalAlignment(VerticalAlignment::Bottom);
    m_stamp->setRightPadding(m_ui->du(1));
    m_stamp->setBottomPadding(m_ui->du(1));
    m_photo->add(m_lengthBox); // over the corners' masks
    m_photo->add(m_stamp);
    m_photo->add(m_photoSpin);
    m_photo->add(m_photoRing);

    // The sticker's picture, fit in a square; always an Image in thumb, an empty one without a
    // picture, as for a photo, so a recycled card never keeps the previous row's.
    m_stickerBox = new Container();
    m_stickerBox->setTopMargin(0);
    m_stickerBox->setLayout(new DockLayout());
    m_stickerImage = new ImageView();
    m_stickerImage->setScalingMethod(ScalingMethod::AspectFit);
    m_stickerImage->setLoadEffect(ImageViewLoadEffect::None);
    // The time's corner, empty unless the card puts the time in it, as a picture's m_stamp.
    m_stickerStamp = new Container();
    m_stickerStamp->setHorizontalAlignment(HorizontalAlignment::Right);
    m_stickerStamp->setVerticalAlignment(VerticalAlignment::Bottom);
    m_stickerBox->add(m_stickerImage);
    m_stickerBox->add(m_stickerStamp);
    // Until it is here: no picture and no thumbnail.
    m_stickerText = new Label();
    m_stickerText->setTopMargin(0);
    m_stickerText->setBottomMargin(0);
    m_stickerText->setText(Card::tr(kContext, QT_TRANSLATE_NOOP("MediaBubble", "Sticker")));

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
    m_fileRing = ring(m_ui->du(6));
    m_fileRing->setLeftMargin(m_ui->du(1));
    m_file->add(m_icon);
    m_file->add(m_emoji);
    m_file->add(words);
    m_file->add(m_fileSpin);
    m_file->add(m_fileRing);

    m_root->add(m_photo);
    m_root->add(m_stickerBox);
    m_root->add(m_stickerText);
    m_root->add(m_file);
    setRoot(m_root);

    // The fetch, as the QML bound it.
    if (!connect(list, SIGNAL(mediaLoadingIdChanged()), this, SLOT(refresh())))
        qWarning("cards: the list has no mediaLoadingIdChanged");
    if (!connect(list, SIGNAL(mediaProgressChanged()), this, SLOT(refresh()))
        || !connect(list, SIGNAL(mediaProgressTextChanged()), this, SLOT(refresh())))
        qWarning("cards: the list has no mediaProgressChanged");
    if (!connect(list, SIGNAL(mediaUploadsChanged()), this, SLOT(refresh())))
        qWarning("cards: the list has no mediaUploadsChanged");
}

void MediaBubble::setCorner(int corner, const Color &behind, int radius)
{
    ImageView *mask = m_corner[corner];
    mask->setVisible(radius > 0);
    if (radius > 0) {
        mask->setPreferredSize(m_ui->px(radius), m_ui->px(radius));
        mask->setFilterColor(behind);
    }
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

    // 72 % of small text at the list's text size, as the card's time.
    m_length->textStyle()->setFontSizeValue(qRound(72 * Card::textScale(m_list)));
    const Color quiet = m_ui->palette()->secondaryTextOnPlain();
    m_none->setBackground(Card::color(Card::pageColor(m_list)));
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
    m_fileRing->setFilterColor(m_ui->palette()->textOnPlain()); // white would vanish on the light theme
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
        m_noneText->setText(video ? Card::tr(kContext, QT_TRANSLATE_NOOP("MediaBubble", "Video")) : Card::tr(kContext, QT_TRANSLATE_NOOP("MediaBubble", "Photo")));
        m_play->setVisible(video && !spoiler);
        m_cover->setVisible(spoiler);
    }

    m_stickerBox->setVisible(m_sticker && hasThumb);
    if (m_sticker) {
        // 30 du at Normal, Settings > Chats > Sticker size; set at each show, so the cards follow a change.
        // ponytail: drawn at 180 px (kStickerPx) and scaled, so Big is softer; draw at the view's size if it shows.
        const float side = m_ui->du(30 * Settings::stickerScale());
        m_stickerImage->setPreferredSize(side, side);
    }
    m_stickerText->setVisible(m_sticker && !hasThumb);
    const bool tgs = m_sticker && flag("tgs");
    const QString stickerId = tgs ? str("id") : QString();
    StickerPlayer *player = StickerPlayer::instance();
    // A new row in this card (a re-read of the same row happens at every update): the other row's sticker stops first.
    const bool newRow = stickerId != m_stickerId;
    if (newRow) {
        player->release(m_stickerImage);
        m_stickerId = stickerId;
    }
    const Image still = m_msg.value("thumb").value<Image>();
    if (m_sticker && !player->playing(m_stickerImage))
        m_stickerImage->setImage(still);
    if (newRow && tgs && Settings::stickerPlayMode() != 2)
        player->play(m_stickerImage, still, str("path"), Settings::stickerPlayMode() == 0);

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
                        : location ? Card::tr(kContext, QT_TRANSLATE_NOOP("MediaBubble", "Show on map"))
                        : media == "audio" ? Card::tr(kContext, QT_TRANSLATE_NOOP("MediaBubble", "Audio file"))
                        : (fileName.isEmpty() ? Card::tr(kContext, QT_TRANSLATE_NOOP("MediaBubble", "Document")) : fileName));

        // The size, and the length of an audio file when it is known; a contact's number.
        const QString contactInfo = str("contactInfo");
        m_info->setVisible(!location && (!contact || !contactInfo.isEmpty()));
        QStringList parts;
        if (!str("sizeText").isEmpty())
            parts << str("sizeText");
        if (!str("durationText").isEmpty())
            parts << str("durationText");
        m_infoText = contact ? contactInfo : parts.join(QString::fromUtf8(" \xC2\xB7 "));
    }
    refresh();
}

// The fetch, or the upload of a message being sent: the spinner of whichever part is shown, the ring once bytes have
// arrived, and the text of how many.
void MediaBubble::refresh()
{
    if (m_msg.isEmpty())
        return;
    // A send that failed (state 4) keeps its last entry: the ring is for state 0, sending, only.
    const QVariantMap up = m_msg.value("state").toInt() == 0
        ? m_list->property("mediaUploads").toMap().value(str("id")).toMap() : QVariantMap();
    const bool loading = !up.isEmpty() || m_list->property("mediaLoadingId").toString() == str("id");
    const qreal progress = !up.isEmpty() ? up.value("progress").toReal()
                           : loading ? m_list->property("mediaProgress").toReal() : 0;
    const bool filling = progress > 0;
    const QString words = !up.isEmpty() ? up.value("text").toString()
                          : loading ? m_list->property("mediaProgressText").toString() : QString();
    const bool video = str("media") == "video" && !flag("spoiler");
    const bool file = !m_picture && !m_sticker;
    m_photoSpin->setRunning(loading && m_picture && !filling);
    m_photoRing->setVisible(loading && m_picture && filling);
    m_dim->setVisible(loading && m_picture);
    // The "Photo" / "Video" of a picture with no thumbnail sits where the spinner and the ring do: it gives way to them
    // (a video being sent has no thumbnail, the user 2026-11-19).
    m_noneText->setVisible(!loading);
    m_play->setVisible(video && !loading);
    // The video's length gives way to the text, as a file's size line does.
    const QString length = words.isEmpty() ? str("durationText") : words;
    m_lengthBox->setVisible(video && !length.isEmpty());
    m_length->setText(length);
    m_fileSpin->setRunning(loading && file && !filling);
    m_fileSpin->setVisible(!(loading && file && filling));
    m_fileRing->setVisible(loading && file && filling);
    m_info->setText(words.isEmpty() ? m_infoText : words);
    if (filling) {
        const QString frame = QString("asset:///images/ring/ring_%1.png")
                                  .arg(qRound(progress * 24), 2, 10, QLatin1Char('0'));
        (m_picture ? m_photoRing : m_fileRing)->setImageSource(QUrl(frame));
    }
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
    } else if (flag("tgs") && Settings::stickerPlayMode() != 2) {
        // Plays again (Play once), or from the start (Loop).
        StickerPlayer::instance()->play(m_stickerImage, m_msg.value("thumb").value<Image>(), str("path"),
                                        Settings::stickerPlayMode() == 0);
    }
}
