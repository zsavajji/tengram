#include "MessageCard.h"

#include <QtCore/qmath.h>

#include <bb/cascades/ActionSet>
#include <bb/cascades/ActiveTextHandler>
#include <bb/cascades/ActiveTextTriggeredEvent>
#include <bb/cascades/Button>
#include <bb/cascades/Container>
#include <bb/cascades/DockLayout>
#include <bb/cascades/ContextMenuHandler>
#include <bb/cascades/FontStyle>
#include <bb/cascades/FontWeight>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/ImageView>
#include <bb/cascades/ImageViewLoadEffect>
#include <bb/cascades/Label>
#include <bb/cascades/LabelAutoSizeProperties>
#include <bb/cascades/LayoutUpdateHandler>
#include <bb/cascades/ListView>
#include <bb/cascades/ScalingMethod>
#include <bb/cascades/SystemDefaults>
#include <bb/cascades/TapHandler>
#include <bb/cascades/TextContentFlag>
#include <bb/cascades/TextContentProperties>
#include <bb/cascades/TextFormat>
#include <bb/cascades/TextStyleDefinition>
#include <bb/cascades/UIConfig>
#include <bb/cascades/UIPalette>
#include <bb/cascades/VerticalAlignment>

#include "AlbumBubble.h"
#include "CardStyle.h"
#include "MediaBubble.h"
#include "PollBubble.h"
#include "Settings.h"
#include "VoiceBubble.h"

using namespace bb::cascades;

namespace {
// The QML file's name: the translations of MessageItem.qml's strings (tengram_it.ts) apply.
const char *const kContext = "MessageItem";
}

MessageCard::MessageCard(ListView *list, bool rich, bool picture)
    : m_list(list)
    , m_ui(list->ui())
    , m_rich(rich)
    , m_pressed(false)
    , m_timePlace(-1)
    , m_timeTop(-1)
    , m_colMax(-1)
    , m_tick(-1)
    , m_downX(0)
    , m_downY(0)
    , m_swipe(0)
    , m_styled(false)
    , m_restyledOut(false)
    , m_round(false)
    , m_scale(0)
    , m_body(0)
    , m_small(0)
    , m_row(0)
    , m_avatar(0)
    , m_photo(0)
    , m_disc(0)
    , m_initials(0)
    , m_nameLine(0)
    , m_names(0)
    , m_sender(0)
    , m_tagRoom(0)
    , m_tag(0)
    , m_fwd(0)
    , m_fwdIcon(0)
    , m_fwdText(0)
    , m_quote(0)
    , m_bar(0)
    , m_quoteBg(0)
    , m_quoteShape(-1)
    , m_quoteName(0)
    , m_quoteText(0)
    , m_quoteThumb(0)
    , m_label(0)
    , m_join(0)
    , m_voice(0)
    , m_media(0)
    , m_album(0)
    , m_link(0)
    , m_poll(0)
    , m_timer(0)
    , m_barL(0)
    , m_barR(0)
    , m_bang(0)
    , m_chip(0)
    , m_chipSpare(0)
    , m_edited(Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "edited")) + " ")
    , m_starMark(QString::fromUtf8("\xE2\x98\x85 "))
    , m_trMark(QString::fromUtf8("\xE2\x87\x84 "))
    , m_reply(0)
    , m_react(0)
    , m_forward(0)
    , m_votes(0)
    , m_download(0)
    , m_copy(0)
    , m_translate(0)
    , m_edit(0)
    , m_revoke(0)
    , m_deleteMe(0)
{
    // The card's own node and its root: a list item is as wide as its content unless it fills.
    // Theirs 1.5 du from the left edge and ours from the right.
    setHorizontalAlignment(HorizontalAlignment::Fill);
    m_root = new Container();
    m_root->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_root->setLeftPadding(m_ui->du(1.5));
    m_root->setRightPadding(m_ui->du(1.5));

    // The card: the text column, and the time and ticks where placeTime() puts them, never on a row
    // of their own, so a one-line message is one line tall (Q10, 2026-10-31: a row of its own made
    // the cards too tall). The labels in the card set their margins: unset, Cascades puts about 20 px
    // between them.
    m_card = new Container();
    m_card->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_card->setLayout(Card::across());

    // As wide as what it holds, up to the width fillFoot() allows it: with a quota it took the
    // whole row, so a one-letter message was as wide as the screen. The picture card is the
    // picture's width, so there the column takes what the time leaves.
    m_col = new Container();
    if (picture)
        Card::quota(m_col, 1);

    // The text, and over its last line's end the time: fillColumn() ends the text with blanks as
    // wide as the time, so the line stops short of it, or the blanks wrap and the time has a line
    // to itself, as in Telegram (the user, 2026-11-14: beside the first line, the time kept a
    // column that was empty below it). Filled, so the time is at the column's right edge.
    m_words = new Container();
    m_words->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_words->setTopMargin(0);
    m_words->setLayout(new DockLayout());
    m_text = new Label();
    m_text->setMultiline(true);
    // A mention's link is ours: the system can't open it, so the page does.
    ActiveTextHandler *links = new ActiveTextHandler(m_text);
    m_text->setActiveTextHandler(links);
    connect(links, SIGNAL(triggered(bb::cascades::ActiveTextTriggeredEvent *)), this,
            SLOT(onLink(bb::cascades::ActiveTextTriggeredEvent *)));
    m_words->add(m_text);
    m_col->add(m_words);

    m_timeBox = new Container();
    m_timeBox->setLayout(Card::across());

    // One label for "edited" and the time: a separate one never showed on the Q10 (2026-10-31). A
    // percentage of its base, SmallText: the same at Normal, and growing with the text size as the body does.
    m_time = new Label();
    m_time->setVerticalAlignment(VerticalAlignment::Center);
    m_time->setTextFormat(TextFormat::Plain);
    m_time->textStyle()->setBase(SystemDefaults::TextStyles::smallText());
    m_time->textStyle()->setFontSize(FontSize::PercentageValue);
    m_ticks = new ImageView();
    m_ticks->setVerticalAlignment(VerticalAlignment::Center);
    // Sized by fillFoot() with the time's text; 1 du from it, not the 20 px Cascades puts there.
    m_ticks->setScalingMethod(ScalingMethod::AspectFit);
    m_ticks->setLeftMargin(m_ui->du(1));
    // A self-destructing message's clock (m_timer) is made by makeTimer() when one first needs it.
    m_timeBox->add(m_time);
    m_timeBox->add(m_ticks);

    m_card->add(m_col);

    // The foot, BBM's (Z30 screenshots, user 2026-11-08): the accent bar, 1 du, and over its outer
    // end the tail, a triangle in the card's colour under the card's corner, theirs left, ours right.
    // The bar runs under the tail, so the tail's slant cuts its end; its other end has the card's
    // rounded corner (bar_l/bar_r, nine-sliced and tinted).
    m_foot = new Container();
    m_foot->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_foot->setLayout(new DockLayout());
    // The bars (m_barL, m_barR) are made by makeBars() when a card first shows one: never in Round mode, the default.
    m_tailL = makeTail("asset:///images/tail_l.png");
    m_tailL->setHorizontalAlignment(HorizontalAlignment::Left);
    m_tailR = makeTail("asset:///images/tail_r.png");
    m_tailR->setHorizontalAlignment(HorizontalAlignment::Right);
    m_foot->add(m_tailL);
    m_foot->add(m_tailR);

    // The card's colour, BBM's rounded top corners: a white nine-slice under the card, tinted as
    // the tail is. Cascades has no corner radius.
    Container *body = new Container();
    body->setHorizontalAlignment(HorizontalAlignment::Fill);
    body->setLayout(new DockLayout());
    m_bg = new ImageView();
    m_bg->setImageSource(Card::url("asset:///images/card.amd"));
    m_bg->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_bg->setVerticalAlignment(VerticalAlignment::Fill);
    body->add(m_bg);

    // The picture card: the head and the card around the picture, which MessageCardBubbles puts
    // between them. No margins between the three: the paddings space them.
    if (picture) {
        m_frame = new Container();
        m_frame->setHorizontalAlignment(HorizontalAlignment::Fill);
        m_head = new Container();
        m_head->setHorizontalAlignment(HorizontalAlignment::Fill);
        m_head->setBottomMargin(0);
        m_card->setTopMargin(0);
        m_frame->add(m_head);
        m_frame->add(m_card);
    } else {
        m_frame = m_card;
        m_head = 0;
    }
    body->add(m_frame);

    // The card and its foot, as wide as the card's content, and a spacer at the other side: its
    // quota stretches the row across the list, which the text column's did, and pushes ours right. In a group the
    // sender's tile comes first (makeTile), and the card sits at the row's bottom, beside the tile's.
    m_row = new Container();
    m_row->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_row->setLayout(Card::across());
    m_gapL = new Container();
    Card::quota(m_gapL, 1);
    m_gapR = new Container();
    Card::quota(m_gapR, 1);
    Container *box = new Container();
    box->setVerticalAlignment(VerticalAlignment::Bottom);
    if (picture)
        box->setMaxWidth(Card::width(m_ui));
    box->add(body);
    box->add(m_foot);
    m_row->add(m_gapL);
    m_row->add(box);
    m_row->add(m_gapR);
    m_root->add(m_row);
    setRoot(m_root);
    // The new message's slide (ChatPage's added and slideIn): the card of the row the list's slideId names tells the page how
    // tall it came out, which is how far the list slides.
    m_laidHeight = 0;
    LayoutUpdateHandler *laid = new LayoutUpdateHandler(this);
    connect(laid, SIGNAL(layoutFrameChanged(QRectF)), this, SLOT(onLaidOut(QRectF)));

    // The long-press menu (step 3.3): the actions are made at the first populating, which comes just
    // before it opens, and their enabled states set at each. The set is the same on every row, with
    // items disabled rather than left out: ActionItem has no visible, and rows are recycled.
    m_menu = new ActionSet();
    addActionSet(m_menu);
    ContextMenuHandler *handler = new ContextMenuHandler(this);
    setContextMenuHandler(handler);
    connect(handler, SIGNAL(populating(bb::cascades::ContextMenuShowingEvent *)), this,
            SLOT(onPopulating()));
    connect(handler, SIGNAL(visualStateChanged(bb::cascades::ContextMenuVisualState::Type)), this,
            SLOT(onMenuState(bb::cascades::ContextMenuVisualState::Type)));
    connect(this, SIGNAL(touch(bb::cascades::TouchEvent *)), this, SLOT(onTouch(bb::cascades::TouchEvent *)));

    // The search match is tinted: the page sets foundId.
    if (!connect(list, SIGNAL(foundIdChanged()), this, SLOT(paintBackground())))
        qWarning("cards: the list has no foundIdChanged");
    // The picture's corners are masks in the page's colour (paintCorners): a theme or Dark background change paints them again.
    if (!connect(list, SIGNAL(pageColorChanged()), this, SLOT(paintBackground())))
        qWarning("cards: the list has no pageColorChanged");
}

ImageView *MessageCard::makeTail(const char *source)
{
    ImageView *tail = new ImageView();
    tail->setImageSource(Card::url(source));
    tail->setPreferredSize(m_ui->du(2), m_ui->du(2));
    tail->setScalingMethod(ScalingMethod::Fill);
    tail->setLeftMargin(0);
    tail->setRightMargin(0);
    return tail;
}

void MessageCard::show(const QVariantMap &msg)
{
    m_msg = msg;
    reportSlide(m_list->property("slideId").toString()); // a reused card that won't be laid out again (reportSlide)
    const bool out = flag("out");
    const bool deleted = flag("deleted");

    // The sender's tile: a group's incoming message, which has a sender id. The name line only shows on the oldest of
    // a person's messages in a row, so a card that never shows it never makes it.
    const bool tiled = !out && !str("senderId").isEmpty();

    // The parts this message needs and the card hasn't made yet.
    if (named() && !m_sender)
        makeSender();
    if (tiled && !m_avatar)
        makeTile();
    if (m_msg.value("forwarded").toInt() > 0 && !m_fwd)
        makeForwarded();
    if (!str("quote").isEmpty() && !m_quote)
        makeQuote();
    if (m_rich && !str("label").isEmpty() && !m_label)
        makeLabel();
    if (m_rich && !str("invite").isEmpty() && !deleted && !m_join)
        makeJoin();
    if (m_rich)
        makeBubbles();
    if (out && m_msg.value("state").toInt() == 4 && !m_bang)
        makeBang();
    const int chips = m_msg.value("reactions").toList().size();
    if (chips > 0 && !m_chip)
        makeChip();
    if (chips > m_chipBoxes.size())
        growChips(chips);
    restyle(out);

    // Theirs at the left and ours at the right. A hidden spacer takes no part in the layout.
    m_gapL->setVisible(out);
    m_gapR->setVisible(!out);
    fillTile(tiled);
    fillColumn();
    if (m_rich)
        fillBubbles();
    fillFoot();
    fillMenu();
    paintBackground();
}

// Pressed: the page's pressColor. A search match: the primary colour at 20 % over the page, so it reads
// in both themes (25 % until the user found it strong, 2026-11-17). Else Telegram's bubble colour, ours or theirs
// (the user's ask, 2026-11-18); a sticker has none, it sits on the page (user). An empty foundId matches nothing.
void MessageCard::paintBackground()
{
    const QString found = Card::foundId(m_list);
    const bool match = !found.isEmpty() && found == str("id");
    const Color c = m_pressed ? Card::color(Card::pressColor(m_list))
                  : match ? Card::color("#3324a1de")
                  : flag("out") ? Card::color(Card::outColor(m_list)) : Card::color(Card::inColor(m_list));
    m_bg->setVisible(m_pressed || match || str("media") != "sticker");
    m_bg->setFilterColor(c);
    m_tailL->setFilterColor(c);
    m_tailR->setFilterColor(c);
    if (m_media)
        paintCorners(c);
}

// The picture's rounded corners (the user, 2026-11-19: they were square in both shapes), as masks in what is behind each
// (MediaBubble::setCorner); `card` is the card's colour as painted. A picture that is not wide (not in the picture card) has
// the card's padding around it: all four corners are in the card's colour, 4 px in Square mode and 12 in Round. A wide picture
// spans the card's width, so a corner of it is the card's own where the head isn't above it (top) or the caption isn't under it
// (bottom; the time sits on the picture then): in the page's colour, with the radius the card's corner has there. That is, in
// Square mode, card.png's 7 px at the top and none at the bottom, where the foot's bar goes on; in Round mode kRoundCardR, but none at
// the tail's corner (the bottom one on the card's side). With the head above or the caption under it a corner is on the card's
// straight side: square.
// The Round cards' corner, card_round_*.png's (scripts/gen-round.py's CARD_R).
static const int kRoundCardR = 28;

void MessageCard::paintCorners(const Color &card)
{
    int radii[4];
    if (!m_head) {
        radii[0] = radii[1] = radii[2] = radii[3] = m_round ? 12 : 4;
    } else {
        const int top = m_head->isVisible() ? 0 : m_round ? kRoundCardR : 7;
        const int bottom = m_card->isVisible() || !m_round ? 0 : kRoundCardR;
        const bool out = flag("out");
        radii[0] = radii[1] = top;
        radii[2] = out ? 0 : bottom; // bottom right: ours has the tail there
        radii[3] = out ? bottom : 0;
    }
    const Color behind = m_head ? Card::color(Card::pageColor(m_list)) : card;
    for (int i = 0; i < 4; i++)
        m_media->setCorner(i, behind, radii[i]);
}

void MessageCard::select(bool)
{
}

void MessageCard::activate(bool activated)
{
    m_pressed = activated;
    paintBackground();
}

void MessageCard::reset(bool, bool activated)
{
    m_pressed = activated;
    paintBackground();
}

// Everything the text size and the theme's colours decide, set again only when one of them changed
// or a part was made. The gap goes above each card: in the bottom-to-top list that also spaces the
// first card from its day's header (Q10, 2026-10-31). It and the card's padding grow with the text
// size (Q10, 2026-11-01: 140 % looked cramped). 2 px over 1.5 and 1 du: the text sat a little
// tight to the card's edges (Q10). Cards are recycled between ours and theirs, so the side is one of the causes:
// the quiet text is Telegram's blue-grey on our bubble.
void MessageCard::restyle(bool out)
{
    const float scale = Card::textScale(m_list);
    const int body = Card::bodySize(m_list);
    const int small = Card::smallSize(m_list);
    const Color plain = m_ui->palette()->plain();
    const bool round = Card::isRound(m_list);
    if (m_styled && scale == m_scale && body == m_body && small == m_small && plain == m_plain && out == m_restyledOut
        && round == m_round)
        return;
    m_styled = true;
    m_restyledOut = out;
    m_round = round;
    m_scale = scale;
    m_body = body;
    m_small = small;
    m_plain = plain;

    // Round mode (the user's ask, 2026-11-19): kRoundCardR corners on the card, but not the one at the tail, which is the side's
    // (theirs left, ours right); the tail stays and the accent bar goes (fillFoot), a 1 du bar can't carry a 20 px corner.
    m_bg->setImageSource(Card::url(!round ? "asset:///images/card.amd"
                                   : out ? "asset:///images/card_round_r.amd" : "asset:///images/card_round_l.amd"));
    const Color quiet = out ? Card::color(Card::outMeta(m_list)) : m_ui->palette()->secondaryTextOnPlain();
    if (m_barL) // else makeBars() tints them
        m_barL->setFilterColor(m_ui->palette()->secondaryTextOnPlain());
    // The primary 20 % darker, quieter under our cards (the user, 2026-11-17).
    if (m_barR)
        m_barR->setFilterColor(Card::color("#1D81B2"));
    const float side = Card::side(m_ui, scale, round);
    // The gap above the card: less in Round mode, its cards sat too far apart (the user, 2026-11-19).
    m_root->setTopPadding(m_ui->du((round ? 1 : 1.5) * scale));
    m_card->setLeftPadding(side);
    m_card->setRightPadding(side);
    m_card->setTopPadding(m_ui->du(1 * scale)); // 2 px less: the name and time a little higher (Q10, 0.0.1.212)
    m_card->setBottomPadding(m_ui->du(1 * scale) + m_ui->px(2));
    if (m_head) {
        m_head->setLeftPadding(side);
        m_head->setRightPadding(side);
        m_head->setTopPadding(m_ui->du(1 * scale));
        m_head->setBottomPadding(m_ui->du(1 * scale) + m_ui->px(2));
    }
    Card::style(m_text, m_list, body);
    // 72 % of SmallText, about two thirds of the body text (the user, 2026-11-14: the time and ticks
    // smaller; it was 85 % since 2026-11-08). The ticks follow it, so they are sized again.
    m_time->textStyle()->setFontSizeValue(qRound(72 * scale));
    if (m_timer)
        m_timer->setPreferredSize(m_ui->du(2.2 * scale), m_ui->du(2.2 * scale));
    m_tick = -1;
    if (m_sender)
        Card::style(m_sender, m_list, small);
    if (m_tag) {
        // The tag is text a person chose, so the emoji style as the name's, but regular and quiet. Its copy takes the
        // same room and no colour.
        Card::style(m_tag, m_list, small);
        m_tag->textStyle()->setColor(quiet);
        Card::style(m_tagRoom, m_list, small);
        m_tagRoom->textStyle()->setColor(Color::Transparent);
    }
    if (m_avatar) {
        // As the chat list's tile and initials (ChatRows.cpp). The height is fillTile's: a spacer has none.
        m_avatar->setPreferredWidth(m_ui->du(7 * scale));
        Card::style(m_initials, m_list, qRound(118 * scale));
        m_initials->textStyle()->setColor(Color::White);
    }
    if (m_fwd) {
        Card::style(m_fwdText, m_list, small);
        m_fwdText->textStyle()->setFontStyle(FontStyle::Italic);
        m_fwdText->textStyle()->setColor(quiet);
        m_fwdIcon->setFilterColor(quiet);
        m_fwdIcon->setPreferredSize(m_ui->du(2.2 * scale), m_ui->du(2.2 * scale));
    }
    if (m_quote) {
        m_bar->setFilterColor(m_ui->palette()->primary());
        Card::style(m_quoteName, m_list, small);
        Card::style(m_quoteText, m_list, small);
        m_quoteText->textStyle()->setColor(quiet);
    }
    if (m_label) {
        Card::style(m_label, m_list, body);
        m_label->textStyle()->setColor(quiet);
    }
    if (m_chip) {
        foreach (Label *label, m_chipLabels)
            Card::style(label, m_list, body); // the text's size, not the small one (the user, 2026-11-08)
    }
}

// The name line, first in the column (or the picture card's head), where the name alone was. The labels have no quota:
// in a card as wide as its content one takes the whole row (see m_col). So the line is a dock, the name at its left and
// the tag at its right, and the name's box ends in a transparent copy of the tag, which keeps the two apart when the
// line is no wider than both. The index counts below (`m_sender ? 1 : 0`) stand: the line is that one child.
void MessageCard::makeSender()
{
    m_nameLine = new Container();
    m_nameLine->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_nameLine->setLayout(new DockLayout());
    m_nameLine->setBottomMargin(m_ui->du(0.5) + m_ui->px(2)); // +2 px (Q10, 0.0.1.212)
    m_names = new Container();
    m_names->setHorizontalAlignment(HorizontalAlignment::Left);
    m_names->setLayout(Card::across());
    m_sender = new Label();
    m_sender->setBottomMargin(0);
    m_sender->setRightMargin(0);
    m_sender->textStyle()->setFontWeight(FontWeight::Bold);
    m_tagRoom = new Label();
    m_tagRoom->setTopMargin(0);
    m_tagRoom->setBottomMargin(0);
    m_tagRoom->setLeftMargin(m_ui->du(1));
    m_tagRoom->setRightMargin(0);
    m_tag = new Label();
    m_tag->setHorizontalAlignment(HorizontalAlignment::Right);
    m_tag->setTopMargin(0);
    m_tag->setBottomMargin(0);
    m_tag->setRightMargin(0);
    m_names->add(m_sender);
    m_names->add(m_tagRoom);
    m_nameLine->add(m_names);
    m_nameLine->add(m_tag);
    (m_head ? m_head : m_col)->insert(0, m_nameLine);
    m_styled = false;
}

// The sender's tile at the left of their messages in a group: the chat list's, a square of the same size with the
// picture over the initials (ChatRows.cpp), as a column of the row. Bottom aligned, so a run's tiles line up with the
// bubbles' bottoms; fillTile says when it has a picture and when it is only room.
void MessageCard::makeTile()
{
    m_avatar = new Container();
    m_avatar->setVerticalAlignment(VerticalAlignment::Bottom);
    m_avatar->setRightMargin(m_ui->du(1));
    m_avatar->setLayout(new DockLayout());
    // Round mode's colour: a tinted disc under the picture, as ChatRows.cpp's tile has.
    m_disc = new ImageView();
    m_disc->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_disc->setVerticalAlignment(VerticalAlignment::Fill);
    m_disc->setScalingMethod(ScalingMethod::AspectFit);
    m_disc->setLoadEffect(ImageViewLoadEffect::None);
    m_disc->setImageSource(Card::url("asset:///images/circle.png"));
    m_photo = new ImageView();
    m_photo->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_photo->setVerticalAlignment(VerticalAlignment::Fill);
    m_photo->setScalingMethod(ScalingMethod::AspectFill);
    m_photo->setLoadEffect(ImageViewLoadEffect::None);
    m_initials = new Label();
    m_initials->setHorizontalAlignment(HorizontalAlignment::Center);
    m_initials->setVerticalAlignment(VerticalAlignment::Center);
    m_avatar->add(m_disc);
    m_avatar->add(m_photo);
    m_avatar->add(m_initials);
    // A tap on the picture: the sender's page (onTile), as a mention's link opens it (the user's ask, 2026-11-19).
    TapHandler *tap = new TapHandler(m_avatar);
    m_avatar->addGestureHandler(tap);
    connect(tap, SIGNAL(tapped(bb::cascades::TapEvent *)), this, SLOT(onTile()));
    m_row->insert(0, m_avatar);
    m_styled = false;
}

// What the tile takes of the row's width, and so how far the reactions' chip, under the row, is moved in.
float MessageCard::tileSpace() const
{
    return m_ui->du(7 * m_scale) + m_ui->du(1);
}

// on: the message has a tile at all (a group's incoming one). Then with the run's newest message it shows the picture
// or, with none, the initials on the sender's colour, as the chat list's tile does; every other message of the run keeps
// the column's width and nothing else, an empty spacer. Everything is set on each call: cards are recycled, and a
// picture left from another sender would show.
void MessageCard::fillTile(bool on)
{
    if (!m_avatar)
        return;
    m_avatar->setVisible(on);
    const bool tile = on && flag("showTile");
    const QString photo = tile ? str("senderPhoto") : QString();
    // Round mode: a disc, and the picture's round copy (roundPhotoSoon: made on a worker, "" until it is there). Until then
    // the tile shows as a sender with no picture does, the coloured disc and the initials, not the square picture: the
    // decode, mask and PNG save were 606 of 828 ms of one open's fills on the UI thread (the Classic, 1.0.0.99), and
    // MessageModel::onPreviews draws the card again when the copy is made. Square mode shows the picture as it is.
    const bool round = Card::isRound(m_list);
    const QString disc = round && !photo.isEmpty() ? roundPhotoSoon(photo) : QString();
    const QString shownPhoto = round ? disc : photo;
    if (shownPhoto != m_photoPath) {
        m_photoPath = shownPhoto;
        m_photo->setImageSource(shownPhoto.isEmpty() ? QUrl() : QUrl(QString::fromLatin1("file://") + shownPhoto));
    }
    m_photo->setVisible(!shownPhoto.isEmpty());
    m_disc->setVisible(round && tile && shownPhoto.isEmpty());
    m_initials->setVisible(tile && shownPhoto.isEmpty());
    m_initials->setText(tile ? str("senderInitials") : QString());
    if (tile) {
        m_avatar->setPreferredHeight(m_ui->du(7 * m_scale));
        if (round) {
            m_avatar->resetBackground();
            m_disc->setFilterColor(Card::color(str("senderColor")));
        } else {
            m_avatar->setBackground(Card::color(str("senderColor")));
        }
    } else {
        m_avatar->resetPreferredHeight(); // the spacer is only as tall as the card
        m_avatar->resetBackground();
    }
}

// A forward's line, as WhatsApp's: under the sender, over the quote.
void MessageCard::makeForwarded()
{
    m_fwd = new Container();
    m_fwd->setLayout(Card::across());
    m_fwd->setTopMargin(0);
    m_fwd->setBottomMargin(m_ui->du(0.5));
    m_fwdIcon = new ImageView();
    m_fwdIcon->setVerticalAlignment(VerticalAlignment::Center);
    m_fwdIcon->setScalingMethod(ScalingMethod::AspectFit);
    m_fwdIcon->setLoadEffect(ImageViewLoadEffect::None);
    m_fwdIcon->setRightMargin(m_ui->du(0.5));
    m_fwdText = new Label();
    m_fwdText->setVerticalAlignment(VerticalAlignment::Center);
    m_fwd->add(m_fwdIcon);
    m_fwd->add(m_fwdText);
    (m_head ? m_head : m_col)->insert(m_sender ? 1 : 0, m_fwd);
    m_styled = false;
}

// The message this one replies to, on a tint a little off the card's colour.
void MessageCard::makeQuote()
{
    // The box: its tinted image behind a row of the bar, the words and a story's picture. Images, not a background, so its
    // edges can be round (4 px in Round mode, the user's ask, 2026-11-20): Cascades can't round a container. scripts/gen-round.py
    // draws them; show() picks Round's or Square's.
    m_quote = new Container();
    m_quote->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_quote->setTopMargin(0);
    m_quote->setBottomMargin(m_ui->du(0.5));
    m_quote->setLayout(new DockLayout());
    m_quoteBg = new ImageView();
    m_quoteBg->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_quoteBg->setVerticalAlignment(VerticalAlignment::Fill);
    m_quoteBg->setScalingMethod(ScalingMethod::Fill);
    m_quoteBg->setLoadEffect(ImageViewLoadEffect::None);
    Container *row = new Container();
    row->setHorizontalAlignment(HorizontalAlignment::Fill);
    row->setLayout(Card::across());

    // 5 px: the bar image's slices (4 px of round corners and 1 of straight edge) meet exactly.
    m_bar = new ImageView();
    m_bar->setVerticalAlignment(VerticalAlignment::Fill);
    m_bar->setScalingMethod(ScalingMethod::Fill);
    m_bar->setLoadEffect(ImageViewLoadEffect::None);
    m_bar->setMinWidth(m_ui->px(5));
    m_bar->setMaxWidth(m_ui->px(5));

    // No quota: it stretched the card to its widest, and the quote is as wide as its words or the
    // card (the user, 2026-11-14). The bar's least width keeps it: a two-line quote squeezed it to
    // nothing while it had only a preferred one (0.0.1.136, Q10).
    // 1 du on every side, the message text's own air (the user, 2026-11-20; 0.5 above and below until then).
    Container *words = new Container();
    words->setLeftMargin(m_ui->du(1));
    words->setRightPadding(m_ui->du(1));
    words->setTopPadding(m_ui->du(1));
    words->setBottomPadding(m_ui->du(1));

    // Whose message it was, in groups.
    m_quoteName = new Label();
    m_quoteName->setBottomMargin(0);
    m_quoteName->textStyle()->setFontWeight(FontWeight::Bold);
    m_quoteText = new Label();
    m_quoteText->setTopMargin(0);
    m_quoteText->setMultiline(true);
    m_quoteText->autoSize()->setMaxLineCount(2);

    words->add(m_quoteName);
    words->add(m_quoteText);
    row->add(m_bar);
    row->add(words);
    // A story's picture, when the quote is one (docs/protocol.md, Story quotes): a tap on the quote plays the story.
    m_quoteThumb = new ImageView();
    m_quoteThumb->setVerticalAlignment(VerticalAlignment::Center);
    m_quoteThumb->setPreferredWidth(m_ui->du(8));
    m_quoteThumb->setPreferredHeight(m_ui->du(8));
    m_quoteThumb->setMinWidth(m_ui->du(8));
    m_quoteThumb->setScalingMethod(ScalingMethod::AspectFill);
    m_quoteThumb->setLoadEffect(ImageViewLoadEffect::None);
    m_quoteThumb->setVisible(false);
    row->add(m_quoteThumb);
    m_quote->add(m_quoteBg);
    m_quote->add(row);
    TapHandler *tap = new TapHandler(m_quote);
    m_quote->addGestureHandler(tap);
    connect(tap, SIGNAL(tapped(bb::cascades::TapEvent *)), this, SLOT(onQuote()));
    (m_head ? m_head : m_col)->insert((m_sender ? 1 : 0) + (m_fwd ? 1 : 0), m_quote);
    m_styled = false;
}

// After the voice, media and album bubbles, before the text.
void MessageCard::makeLabel()
{
    m_label = new Label();
    m_label->setTopMargin(0);
    m_label->setBottomMargin(0);
    m_label->textStyle()->setFontStyle(FontStyle::Italic);
    m_col->insert(m_col->indexOf(m_words), m_label);
    m_styled = false;
}

// An invite we received (step 6.6f), after its text.
void MessageCard::makeJoin()
{
    m_join = new Button();
    m_join->setTopMargin(m_ui->du(1));
    m_join->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_join->setText(Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "Join group")));
    connect(m_join, SIGNAL(clicked()), this, SLOT(onJoin()));
    place(m_join, m_poll);
}

// Our message that failed: a red "!" after the ticks.
void MessageCard::makeBang()
{
    m_bang = new Label();
    m_bang->setVerticalAlignment(VerticalAlignment::Center);
    m_bang->setText("!");
    m_bang->textStyle()->setBase(SystemDefaults::TextStyles::subtitleText());
    m_bang->textStyle()->setFontWeight(FontWeight::Bold);
    m_bang->textStyle()->setColor(Card::color("#FF3333"));
    m_timeBox->add(m_bang);
}

// The reactions (step 5.11), under the card at its outer edge (fillFoot), with the card's own left padding so theirs
// line up with its text: an area of rows of chips (fillChips). The area has a hidden spare for the chips in no row.
void MessageCard::makeChip()
{
    m_chip = new Container();
    m_chip->setHorizontalAlignment(HorizontalAlignment::Left);
    m_chip->setTopMargin(m_ui->du(0.5));
    m_chip->setRightPadding(m_ui->du(1));
    m_chipSpare = new Container();
    m_chipSpare->setVisible(false);
    m_chip->add(m_chipSpare);
    m_root->add(m_chip);
    m_styled = false;
}

// Makes chips until the pool holds n; a tap on one is onChip(). Before restyle(), which sets the labels' size.
void MessageCard::growChips(int n)
{
    while (m_chipBoxes.size() < n) {
        // Square, or in Round mode a pill (the user's ask, 2026-11-19): pill.amd stretched under the label and tinted, as
        // the service lines' (ServiceCard); the paddings are on an inner Container, so the pill covers them too.
        Container *box = new Container();
        box->setLayout(new DockLayout());
        box->setRightMargin(m_ui->du(0.5));
        ImageView *pill = new ImageView();
        pill->setHorizontalAlignment(HorizontalAlignment::Fill);
        pill->setVerticalAlignment(VerticalAlignment::Fill);
        pill->setScalingMethod(ScalingMethod::Fill);
        pill->setLoadEffect(ImageViewLoadEffect::None);
        pill->setImageSource(Card::url("asset:///images/pill.amd"));
        pill->setVisible(false);
        Container *padded = new Container();
        padded->setLeftPadding(m_ui->du(1));
        padded->setRightPadding(m_ui->du(1));
        padded->setTopPadding(m_ui->du(0.5));
        padded->setBottomPadding(m_ui->du(0.5));
        Label *label = new Label();
        label->setTopMargin(0);
        label->setBottomMargin(0);
        label->setLeftMargin(0);
        label->setRightMargin(0);
        padded->add(label);
        box->add(pill);
        box->add(padded);
        TapHandler *tap = new TapHandler(box);
        box->addGestureHandler(tap);
        connect(tap, SIGNAL(tapped(bb::cascades::TapEvent *)), this, SLOT(onChip()));
        m_chipSpare->add(box);
        m_chipBoxes << box;
        m_chipLabels << label;
        m_chipPills << pill;
        m_chipAt << -1;
    }
    m_styled = false;
}

// A chip's width, reckoned a little over: the emoji and the space after it, the count's digits, the padding and the
// margin after it.
// ponytail: a reckoning, not measured on the phone; a chip wider than it (a long emoji sequence) runs past the column's
// end, and one narrower leaves a gap before the wrap. Measuring takes a layout pass per chip.
float MessageCard::chipWidth(int count) const
{
    return m_ui->du((2.6 + 1 + 1.55 * QString::number(count).size()) * m_scale) + m_ui->du(2.5);
}

// The chips, one for each emoji of the message: "👍 5", ours on the primary colour. They go into rows, a row ending
// where the next chip would pass the column's width; the rows and chips are moved only when that changes.
void MessageCard::fillChips()
{
    const QVariantList chips = m_msg.value("reactions").toList();
    const int n = chips.size();
    const bool tile = m_avatar && m_avatar->isVisible();
    m_chip->setVisible(n > 0);
    // Under the card's outer corner: ours is at the right now. Not under the tile before it.
    m_chip->setHorizontalAlignment(flag("out") ? HorizontalAlignment::Right : HorizontalAlignment::Left);
    // In padding: a top-to-bottom stack ignores a child's left margin (the chips sat under the tile, the user,
    // 2026-11-18). The side lines them up with the card's text.
    m_chip->setLeftPadding(Card::side(m_ui, m_scale, m_round) + (tile ? tileSpace() : 0));
    if (n == 0)
        return;

    const float most = Card::width(m_ui) - 2 * Card::side(m_ui, m_scale, m_round) - (tile ? tileSpace() : 0);
    QList<int> at;
    float used = 0;
    int row = 0;
    for (int i = 0; i < n; ++i) {
        const float width = chipWidth(chips.at(i).toMap().value("n").toInt());
        if (used > 0 && used + width > most) {
            ++row;
            used = 0;
        }
        used += width;
        at << row;
    }
    for (int i = n; i < m_chipBoxes.size(); ++i)
        at << -1;
    while (m_chipRows.size() <= row) {
        Container *line = new Container();
        line->setLayout(Card::across());
        if (!m_chipRows.isEmpty())
            line->setTopMargin(m_ui->du(0.5));
        m_chip->add(line);
        m_chipRows << line;
    }
    for (int r = 0; r < m_chipRows.size(); ++r)
        m_chipRows.at(r)->setVisible(r <= row);
    if (at != m_chipAt) {
        for (int i = 0; i < m_chipBoxes.size(); ++i)
            (m_chipAt.at(i) < 0 ? m_chipSpare : m_chipRows.at(m_chipAt.at(i)))->remove(m_chipBoxes.at(i));
        for (int i = 0; i < m_chipBoxes.size(); ++i)
            (at.at(i) < 0 ? m_chipSpare : m_chipRows.at(at.at(i)))->add(m_chipBoxes.at(i));
        m_chipAt = at;
    }

    const bool round = Card::isRound(m_list);
    for (int i = 0; i < n; ++i) {
        const QVariantMap chip = chips.at(i).toMap();
        const bool mine = chip.value("me").toBool();
        Container *box = m_chipBoxes.at(i);
        box->setProperty("emoji", chip.value("e"));
        box->setProperty("mine", mine);
        const Color fill = mine ? m_ui->palette()->primary() : m_plain;
        m_chipPills.at(i)->setVisible(round);
        m_chipPills.at(i)->setFilterColor(fill);
        if (round)
            box->resetBackground();
        else
            box->setBackground(fill);
        m_chipLabels.at(i)->setText(chip.value("e").toString() + " " + QString::number(chip.value("n").toInt()));
        if (mine)
            m_chipLabels.at(i)->textStyle()->setColor(Color::White);
        else
            m_chipLabels.at(i)->textStyle()->resetColor();
    }
}

// The sender, the quote, the text and what hangs on it. A part a message doesn't need is hidden,
// as it was set for an earlier one.
void MessageCard::fillColumn()
{
    const bool deleted = flag("deleted");
    const QString sender = str("sender");
    const bool hasName = named();
    if (m_nameLine) {
        m_nameLine->setVisible(hasName);
        if (hasName) {
            const QString tint = str("senderColor");
            m_sender->setText(sender);
            m_sender->textStyle()->setColor(Card::color(tint.isEmpty() ? QString("#FFFFFF") : tint));
            const QString tag = str("senderTag");
            m_tag->setVisible(!tag.isEmpty());
            m_tagRoom->setVisible(!tag.isEmpty());
            m_tag->setText(tag);
            m_tagRoom->setText(tag);
        }
    }

    // WhatsApp's "many times" is a score of 5 and up.
    const int fwd = deleted ? 0 : m_msg.value("forwarded").toInt();
    if (m_fwd) {
        m_fwd->setVisible(fwd > 0);
        if (fwd > 0) {
            m_fwdText->setText(fwd >= 5 ? Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "Forwarded many times")) : Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "Forwarded")));
            m_fwdIcon->setImageSource(Card::url(fwd >= 5 ? "asset:///images/forward_many.png" : "asset:///images/forward.png"));
        }
    }

    const QString quote = str("quote");
    if (m_quote) {
        m_quote->setVisible(!quote.isEmpty());
        if (!quote.isEmpty()) {
            const QString name = str("quoteName");
            const QString tint = str("quoteColor");
            // The tint is a translucent "#AARRGGBB": its colour tints the white box, its alpha is the box's opacity.
            const QString box = Card::quoteTint(m_list);
            bool ok = false;
            const int alpha = box.length() == 9 ? box.mid(1, 2).toInt(&ok, 16) : 255;
            m_quoteBg->setFilterColor(Card::color(box.length() == 9 ? "#" + box.mid(3) : box));
            m_quoteBg->setOpacity(ok ? alpha / 255.0f : 1.0f);
            const int shape = m_round ? 1 : 0;
            if (shape != m_quoteShape) {
                m_quoteShape = shape;
                m_quoteBg->setImageSource(Card::url(m_round ? "asset:///images/quote.amd" : "asset:///images/quote_sq.amd"));
                m_bar->setImageSource(Card::url(m_round ? "asset:///images/quote_bar.amd" : "asset:///images/quote_bar_sq.amd"));
            }
            // A story's quote is labelled so, and shows the story's picture when it came with one.
            const bool story = flag("quoteStory");
            m_quoteName->setVisible(story || !name.isEmpty());
            m_quoteName->setText(story ? Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "Story")) : name);
            m_quoteName->textStyle()->setColor(tint.isEmpty() || story ? m_ui->palette()->primary() : Card::color(tint));
            m_quoteText->setText(Card::noKeycaps(quote));
            const bool thumb = story && flag("hasQuoteThumb");
            m_quoteThumb->setVisible(thumb);
            if (thumb)
                m_quoteThumb->setImage(m_msg.value("quoteThumb").value<Image>());
        }
    }

    if (m_label) {
        const QString label = str("label");
        m_label->setVisible(!label.isEmpty());
        if (!label.isEmpty())
            m_label->setText(label);
    }

    // With mentions or WhatsApp's styles the html has them linked or styled (MessageKinds::html, which
    // drops the keycaps too); else the plain text. Links, numbers and addresses are tappable, opened by the system
    // (the user's request, 0.0.1.132: a YouTube link did nothing), but not on a deleted message's note.
    const QString html = str("html");
    const QString text = str("text");
    if (m_rich)
        m_words->setVisible(!text.isEmpty());
    // Room for the time at the last line's end: U+2800, the blank Braille cell, which only the
    // phone's fallback font has (DejaVu Sans, 0.73 em: 3.2 du at 100 % on the Q10). It isn't a
    // space, so the line keeps it, and the run wraps as a word does. Right-to-left text ends at
    // the left, so there the blanks take a line of their own.
    QString room = text.section('\n', -1).isRightToLeft() ? QString(html.isEmpty() ? "\n" : "<br/>") : QString(" ");
    room += QString(qCeil((timeWidth() + m_ui->du(0.5)) / m_ui->du(3.1 * m_scale)), QChar(0x2800));
    m_text->setTextFormat(html.isEmpty() ? TextFormat::Plain : TextFormat::Html);
    m_text->setText((html.isEmpty() ? Card::noKeycaps(text) : html) + room);
    m_text->content()->setFlags(deleted ? TextContentFlag::Default : TextContentFlag::ActiveText);
    m_text->textStyle()->setFontStyle(deleted ? FontStyle::Italic : FontStyle::Default);
    m_text->textStyle()->setColor(deleted ? m_ui->palette()->secondaryTextOnPlain()
                                          : m_ui->palette()->textOnPlain());

    if (m_join)
        m_join->setVisible(!str("invite").isEmpty() && !deleted);

    // The picture card: the head when there's a sender or quote, the card when there's a caption
    // (or no picture: a deleted one).
    if (m_head) {
        m_head->setVisible(hasName || fwd > 0 || !quote.isEmpty());
        m_card->setVisible(!text.isEmpty() || !str("label").isEmpty() || str("media").isEmpty());
    }
}

// What goes before the time: the seconds a self-destructing message has left (or its timer's), a star for a starred message,
// the translation's arrows, then "edited".
QString MessageCard::timeLead() const
{
    const QString ttl = str("ttlText");
    return (ttl.isEmpty() ? QString() : ttl + QString::fromUtf8(" \xC2\xB7 ")) + (flag("starred") ? m_starMark : QString())
           + (flag("translated") ? m_trMark : QString()) + (flag("edited") ? m_edited : QString());
}

// The accent bars, made when a card first shows one (fillFoot): every card made them, and Round mode, the default, never
// shows them. Put before the tails, where the constructor added them, so the drawing order stays; tinted as restyle() does.
void MessageCard::makeBars()
{
    m_barL = new ImageView();
    m_barL->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_barL->setVerticalAlignment(VerticalAlignment::Top);
    m_barL->setPreferredHeight(m_ui->du(1));
    m_barL->setImageSource(Card::url("asset:///images/bar_l.amd"));
    m_barL->setFilterColor(m_ui->palette()->secondaryTextOnPlain());
    m_barR = new ImageView();
    m_barR->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_barR->setVerticalAlignment(VerticalAlignment::Top);
    m_barR->setPreferredHeight(m_ui->du(1));
    m_barR->setImageSource(Card::url("asset:///images/bar_r.amd"));
    m_barR->setFilterColor(Card::color("#1D81B2"));
    m_foot->insert(0, m_barL);
    m_foot->insert(1, m_barR);
}

// A self-destructing message's clock (a secret chat's), made when a card first has one: its seconds are the time label's
// lead (timeLead). Sized as restyle() sizes it, at the card's scale, tinted by fillFoot(); first in the time box.
void MessageCard::makeTimer()
{
    m_timer = new ImageView();
    m_timer->setVerticalAlignment(VerticalAlignment::Center);
    m_timer->setScalingMethod(ScalingMethod::AspectFit);
    m_timer->setLeftMargin(0);
    m_timer->setRightMargin(m_ui->du(0.5));
    m_timer->setImageSource(Card::url("asset:///images/clock.png"));
    m_timer->setPreferredSize(m_ui->du(2.2 * m_scale), m_ui->du(2.2 * m_scale));
    m_timer->setVisible(false);
    m_timeBox->insert(0, m_timer);
}

// The time (the star and "edited" before it) and our message's state, as in ChatRows.cpp: sending, sent, delivered,
// read, or a red "!" when it failed; then the foot and the reactions' chips.
void MessageCard::fillFoot()
{
    const bool out = flag("out");
    const int state = m_msg.value("state").toInt();
    m_time->setText(timeLead() + str("time"));
    const bool timed = !str("ttlText").isEmpty();
    if (timed && !m_timer)
        makeTimer();
    if (m_timer)
        m_timer->setVisible(timed);
    if (timed)
        m_time->setLeftMargin(0);
    else
        m_time->resetLeftMargin();
    // Where the time goes: on a picture with no caption, at the text's end, under a voice note's
    // waveform, at a sticker's bottom right on the picture's chip (as Telegram; beside it, it pushed the sticker off the
    // edge, the user's screenshot 2026-11-20), or else (a file, a sticker still on the server) beside the column's first line.
    // Top-aligned boxes put the time's baseline above the first line's: 7 px under a name or quote
    // (small), 12 px under body text, measured at 85 % and 105 % on the Q10, then scaled with the
    // text size setting; 4 px more since the time is at 72 %, reckoned, not measured.
    // ponytail: linear in the scale, not measured at 90-140 %.
    // In the picture card the head holds them, so the caption is the card's first line. +2 px for
    // the time at 85 %, whose baseline sits higher (Q10, 0.0.1.212: +3 was 1 px low; 0.0.1.216: 2 more).
    const bool small = !m_head && (named() || (!flag("deleted") && m_msg.value("forwarded").toInt() > 0) || !str("quote").isEmpty());
    const bool onPicture = m_head && !m_card->isVisible();
    const bool onSticker = m_media && str("media") == "sticker" && flag("hasThumb");
    // An album with no caption: on its last tile, as on a photo (beside it, it took the time's width from the tiles,
    // cut at the left, the user's screenshot 2026-11-21).
    const bool onAlbum = m_album && !m_msg.value("album").toList().isEmpty();
    const int place = onPicture ? 2 : onSticker ? 4 : m_words->isVisible() ? 1 : onAlbum ? 5 : m_voice && flag("voice") ? 3 : 0;
    placeTime(place);
    const bool chip = place == 2 || place == 4 || place == 5;
    // The top padding is this one's, on a picture too; set only when it changed.
    const float top = chip ? m_ui->du(0.5) : place == 0 ? m_ui->px((small ? 11 : 16) * m_scale) : 0;
    if (top != m_timeTop) {
        m_timeTop = top;
        m_timeBox->setTopPadding(top);
    }
    // White on the picture's dark chip.
    const Color quiet = chip ? Color::White
                      : out ? Card::color(Card::outMeta(m_list)) : m_ui->palette()->secondaryTextOnPlain();
    m_time->textStyle()->setColor(quiet);
    if (m_timer)
        m_timer->setFilterColor(quiet);

    const bool ticked = out && state < 4;
    m_ticks->setVisible(ticked);
    if (ticked) {
        const int tick = state == 0 ? 0 : state == 1 ? 1 : 2;
        if (tick != m_tick) {
            m_tick = tick;
            // 2.8 du tall at 100 %, 25 px on the Q10, under the images' 27 px so they aren't blown up (2.4 until the user
            // asked for a little bigger, 2026-11-19); the two ticks are wider.
            m_ticks->setPreferredSize(m_ui->du((tick == 2 ? 3.6 : 2.8) * m_scale), m_ui->du(2.8 * m_scale));
            m_ticks->setImageSource(Card::url(tick == 0 ? "asset:///images/clock.png"
                                        : tick == 1 ? "asset:///images/tick.png" : "asset:///images/ticks.png"));
        }
        // One tick sent, two read: Telegram tells them by the count, so the colour is the time's (the engine never
        // sends "delivered").
        m_ticks->setFilterColor(quiet);
    }
    if (m_bang)
        m_bang->setVisible(out && state == 4);

    // The column's width at most: the card's less its paddings, and less the time when that is
    // beside it. Without it a long text asks for the whole row and the stack takes the excess from
    // both, so the time is cut.
    if (!m_head) {
        const float most = Card::width(m_ui) - 2 * Card::side(m_ui, m_scale, m_round)
                           - (place == 0 ? m_ui->du(1) + timeWidth() : 0);
        if (most != m_colMax) {
            m_colMax = most;
            m_col->setMaxWidth(most);
        }
    }

    // No bar in Round mode: the tail alone hangs under the card's square corner (restyle).
    if (!m_round && !m_barL)
        makeBars();
    if (m_barL) {
        m_barL->setVisible(!out && !m_round);
        m_barR->setVisible(out && !m_round);
    }
    m_foot->setVisible(str("media") != "sticker");
    m_tailL->setVisible(!out);
    m_tailR->setVisible(out);

    if (m_chip)
        fillChips();
}


// The time and ticks' width, which can't be asked, reckoned a little over: 1.55 du a character at
// 100 % ("11:16 PM" was 117 px on the Q10 at 85 %, so 1.37 du each at 72 %), the two ticks' 3.6 du (fillFoot's, which
// this must follow: at 3.1 after the ticks grew, our text ran into the time, the user's screenshot, 2026-11-19) and
// their 1 du margin, and the "!" with the 2.5 du before it.
// ponytail: a reckoning; a time wider than it (a script with wide digits) touches the text's end.
float MessageCard::timeWidth() const
{
    const bool out = flag("out");
    const int state = m_msg.value("state").toInt();
    const int chars = str("time").size() + timeLead().size();
    return chars * m_ui->du(1.55 * m_scale) + (out && state < 4 ? m_ui->du(1 + 3.6 * m_scale) : 0)
           + (str("ttlText").isEmpty() ? 0 : m_ui->du(2.7 * m_scale))
           + (out && state == 4 ? m_ui->du(4) : 0);
}

// The time and ticks: at the bottom right of the text, over the blanks that end it; under a voice
// note's waveform, at the right of its length; else beside the column's first line; on a photo with
// no caption at its bottom right, on a dark chip so they read on any picture, as Android shows them. Moved only when the place changes, since
// rows are recycled.
void MessageCard::placeTime(int place)
{
    if (place == m_timePlace)
        return;
    Container *const homes[] = { m_card, m_words, m_media ? m_media->stamp() : 0, m_voice ? m_voice->stamp() : 0,
                                 m_media ? m_media->stickerStamp() : 0, m_album ? m_album->stamp() : 0 };
    if (m_timePlace >= 0)
        homes[m_timePlace]->remove(m_timeBox);
    homes[place]->add(m_timeBox);
    m_timePlace = place;

    const bool onPicture = place == 2 || place == 4 || place == 5; // a sticker's and an album's have the picture's chip
    const float chip = onPicture ? m_ui->du(1) : 0;
    m_timeBox->setHorizontalAlignment(place == 1 ? HorizontalAlignment::Right : HorizontalAlignment::Left);
    m_timeBox->setVerticalAlignment(place == 1 ? VerticalAlignment::Bottom : VerticalAlignment::Top);
    m_timeBox->setLeftMargin(place == 0 ? m_ui->du(1) : 0);
    m_timeBox->setLeftPadding(chip);
    m_timeBox->setRightPadding(chip);
    m_timeBox->setBottomPadding(onPicture ? m_ui->du(0.5) : 0);
    if (onPicture)
        m_timeBox->setBackground(Card::color("#80000000"));
    else
        m_timeBox->resetBackground();
}

// A new message at the bottom (the user's ask, 2026-11-20): the page names its row in slideId when it goes in, and the card
// that shows it reports each height it is laid out at (slideMeasured): a text's first layout can come before its lines wrap,
// so the page slides once the height has held (multi-line messages slid short, the user, 0.1.0.70). Read here rather than in
// show(): the list may make the card before the page has named the row, and it lays it out after. Every card's layout
// passes through; most stop at the empty slideId.
void MessageCard::onLaidOut(const QRectF &frame)
{
    m_laidHeight = frame.height();
    reportSlide(m_list->property("slideId").toString());
}

// Also from show() and MessageCards::slideRow: a card reused at the same size and place isn't laid out again, so it says its
// last height itself; a layout that follows with another height reports again, and the page slides by the latest.
// A text of ours slides in as a local row (MessageModel::addLocal, id "local-n") and the engine's row takes its place under its
// own id, possibly before this card has been measured: the row carries the local id as `local`, so the card is still the one.
void MessageCard::reportSlide(const QString &id)
{
    if (id.isEmpty() || (id != str("id") && id != str("local")) || m_laidHeight <= 0)
        return;
    Card::call(m_list, "slideMeasured", m_laidHeight);
}
