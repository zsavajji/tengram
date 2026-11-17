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
#include <bb/cascades/Label>
#include <bb/cascades/LabelAutoSizeProperties>
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

#include "CardStyle.h"
#include "MediaBubble.h"
#include "PollBubble.h"
#include "VoiceBubble.h"

using namespace bb::cascades;

namespace {
// The QML file's name: the translations of MessageItem.qml's strings (hyperberry_it.ts) apply.
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
    , m_scale(0)
    , m_body(0)
    , m_small(0)
    , m_sender(0)
    , m_fwd(0)
    , m_fwdIcon(0)
    , m_fwdText(0)
    , m_quote(0)
    , m_bar(0)
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
    , m_bang(0)
    , m_chip(0)
    , m_reactions(0)
    , m_edited(Card::tr(kContext, "edited") + " ")
    , m_starMark(QString::fromUtf8("\xE2\x98\x85 "))
    , m_reply(0)
    , m_react(0)
    , m_votes(0)
    , m_copy(0)
    , m_star(0)
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
    m_barL = new ImageView();
    m_barL->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_barL->setVerticalAlignment(VerticalAlignment::Top);
    m_barL->setPreferredHeight(m_ui->du(1));
    m_barL->setImageSource(Card::url("asset:///images/bar_l.amd"));
    m_barR = new ImageView();
    m_barR->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_barR->setVerticalAlignment(VerticalAlignment::Top);
    m_barR->setPreferredHeight(m_ui->du(1));
    m_barR->setImageSource(Card::url("asset:///images/bar_r.amd"));
    m_tailL = makeTail("asset:///images/tail_l.png");
    m_tailL->setHorizontalAlignment(HorizontalAlignment::Left);
    m_tailR = makeTail("asset:///images/tail_r.png");
    m_tailR->setHorizontalAlignment(HorizontalAlignment::Right);
    m_foot->add(m_barL);
    m_foot->add(m_barR);
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
    // quota stretches the row across the list, which the text column's did, and pushes ours right.
    Container *row = new Container();
    row->setHorizontalAlignment(HorizontalAlignment::Fill);
    row->setLayout(Card::across());
    m_gapL = new Container();
    Card::quota(m_gapL, 1);
    m_gapR = new Container();
    Card::quota(m_gapR, 1);
    Container *box = new Container();
    if (picture)
        box->setMaxWidth(Card::width(m_ui));
    box->add(body);
    box->add(m_foot);
    row->add(m_gapL);
    row->add(box);
    row->add(m_gapR);
    m_root->add(row);
    setRoot(m_root);

    // The long-press menu (step 3.3): the actions are made at the first populating, which comes just
    // before it opens, and their enabled states set at each. The set is the same on every row, with
    // items disabled rather than left out: ActionItem has no visible, and rows are recycled.
    m_menu = new ActionSet();
    addActionSet(m_menu);
    ContextMenuHandler *handler = new ContextMenuHandler(this);
    setContextMenuHandler(handler);
    connect(handler, SIGNAL(populating(bb::cascades::ContextMenuShowingEvent *)), this,
            SLOT(onPopulating()));
    connect(this, SIGNAL(touch(bb::cascades::TouchEvent *)), this, SLOT(onTouch(bb::cascades::TouchEvent *)));

    // The search match is tinted: the page sets foundId.
    if (!connect(list, SIGNAL(foundIdChanged()), this, SLOT(paintBackground())))
        qWarning("cards: the list has no foundIdChanged");
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
    const bool out = flag("out");
    const bool deleted = flag("deleted");

    // The parts this message needs and the card hasn't made yet.
    if (!str("sender").isEmpty() && !m_sender)
        makeSender();
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
    if (m_msg.value("reactionCount").toInt() > 0 && !m_chip)
        makeChip();
    restyle();

    // Theirs at the left and ours at the right. A hidden spacer takes no part in the layout.
    m_gapL->setVisible(out);
    m_gapR->setVisible(!out);
    fillColumn();
    if (m_rich)
        fillBubbles();
    fillFoot();
    fillMenu();
    paintBackground();
}

// Pressed: the page's pressColor. A search match: the primary colour at 25 % over the page, so it
// reads in both themes. Else the page's own colour; a sticker has none, it sits on the page (user).
// An empty foundId matches nothing.
void MessageCard::paintBackground()
{
    const QString found = Card::foundId(m_list);
    const bool match = !found.isEmpty() && found == str("id");
    const Color c = m_pressed ? Card::color(Card::pressColor(m_list))
                  : match ? Card::color("#401bd862") : m_ui->palette()->plain();
    m_bg->setVisible(m_pressed || match || str("media") != "sticker");
    m_bg->setFilterColor(c);
    m_tailL->setFilterColor(c);
    m_tailR->setFilterColor(c);
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
// tight to the card's edges (Q10).
void MessageCard::restyle()
{
    const float scale = Card::textScale(m_list);
    const int body = Card::bodySize(m_list);
    const int small = Card::smallSize(m_list);
    const Color plain = m_ui->palette()->plain();
    if (m_styled && scale == m_scale && body == m_body && small == m_small && plain == m_plain)
        return;
    m_styled = true;
    m_scale = scale;
    m_body = body;
    m_small = small;
    m_plain = plain;

    const Color quiet = m_ui->palette()->secondaryTextOnPlain();
    m_barL->setFilterColor(quiet);
    m_barR->setFilterColor(m_ui->palette()->primary());
    const float side = Card::side(m_ui, scale);
    m_root->setTopPadding(m_ui->du(1.5 * scale));
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
    m_tick = -1;
    if (m_sender)
        Card::style(m_sender, m_list, small);
    if (m_fwd) {
        Card::style(m_fwdText, m_list, small);
        m_fwdText->textStyle()->setFontStyle(FontStyle::Italic);
        m_fwdText->textStyle()->setColor(quiet);
        m_fwdIcon->setFilterColor(quiet);
        m_fwdIcon->setPreferredSize(m_ui->du(2.2 * scale), m_ui->du(2.2 * scale));
    }
    if (m_quote) {
        m_bar->setBackground(m_ui->palette()->primary());
        Card::style(m_quoteName, m_list, small);
        Card::style(m_quoteText, m_list, small);
        m_quoteText->textStyle()->setColor(quiet);
    }
    if (m_label) {
        Card::style(m_label, m_list, body);
        m_label->textStyle()->setColor(quiet);
    }
    if (m_chip) {
        m_chip->setBackground(plain);
        m_chip->setLeftPadding(side);
        Card::style(m_reactions, m_list, body); // the text's size, not the small one (the user, 2026-11-08)
    }
}

void MessageCard::makeSender()
{
    m_sender = new Label();
    m_sender->setBottomMargin(m_ui->du(0.5) + m_ui->px(2)); // +2 px (Q10, 0.0.1.212)
    m_sender->textStyle()->setFontWeight(FontWeight::Bold);
    (m_head ? m_head : m_col)->insert(0, m_sender);
    m_styled = false;
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
    m_quote = new Container();
    m_quote->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_quote->setTopMargin(0);
    m_quote->setBottomMargin(m_ui->du(0.5));
    m_quote->setLayout(Card::across());

    m_bar = new Container();
    m_bar->setVerticalAlignment(VerticalAlignment::Fill);
    m_bar->setMinWidth(m_ui->du(0.5));
    m_bar->setMaxWidth(m_ui->du(0.5));

    // No quota: it stretched the card to its widest, and the quote is as wide as its words or the
    // card (the user, 2026-11-14). The bar's least width keeps it: a two-line quote squeezed it to
    // nothing while it had only a preferred one (0.0.1.136, Q10).
    Container *words = new Container();
    words->setLeftMargin(m_ui->du(1));
    words->setRightPadding(m_ui->du(1));
    words->setTopPadding(m_ui->du(0.5));
    words->setBottomPadding(m_ui->du(0.5));

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
    m_quote->add(m_bar);
    m_quote->add(words);
    // A story's picture, when the quote is one (docs/protocol.md, Story quotes): a tap on the quote plays the story.
    m_quoteThumb = new ImageView();
    m_quoteThumb->setVerticalAlignment(VerticalAlignment::Center);
    m_quoteThumb->setPreferredWidth(m_ui->du(8));
    m_quoteThumb->setPreferredHeight(m_ui->du(8));
    m_quoteThumb->setMinWidth(m_ui->du(8));
    m_quoteThumb->setScalingMethod(ScalingMethod::AspectFill);
    m_quoteThumb->setLoadEffect(ImageViewLoadEffect::None);
    m_quoteThumb->setVisible(false);
    m_quote->add(m_quoteThumb);
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
    m_join->setText(Card::tr(kContext, "Join group"));
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

// The reactions (step 5.11), under the card at its outer edge (fillFoot), with the card's own
// left padding so theirs line up with its text. A tap says who reacted.
void MessageCard::makeChip()
{
    m_chip = new Container();
    m_chip->setHorizontalAlignment(HorizontalAlignment::Left);
    m_chip->setTopMargin(m_ui->du(0.5));
    m_chip->setRightPadding(m_ui->du(1));
    m_chip->setTopPadding(m_ui->du(0.5));
    m_chip->setBottomPadding(m_ui->du(0.5));
    TapHandler *tap = new TapHandler(m_chip);
    m_chip->addGestureHandler(tap);
    connect(tap, SIGNAL(tapped(bb::cascades::TapEvent *)), this, SLOT(onWho()));

    m_reactions = new Label();
    m_reactions->setTopMargin(0);
    m_reactions->setBottomMargin(0);
    m_chip->add(m_reactions);
    m_root->add(m_chip);
    m_styled = false;
}

// The sender, the quote, the text and what hangs on it. A part a message doesn't need is hidden,
// as it was set for an earlier one.
void MessageCard::fillColumn()
{
    const bool deleted = flag("deleted");
    const QString sender = str("sender");
    if (m_sender) {
        m_sender->setVisible(!sender.isEmpty());
        if (!sender.isEmpty()) {
            const QString tint = str("senderColor");
            m_sender->setText(sender);
            m_sender->textStyle()->setColor(Card::color(tint.isEmpty() ? QString("#FFFFFF") : tint));
        }
    }

    // WhatsApp's "many times" is a score of 5 and up.
    const int fwd = deleted ? 0 : m_msg.value("forwarded").toInt();
    if (m_fwd) {
        m_fwd->setVisible(fwd > 0);
        if (fwd > 0) {
            m_fwdText->setText(fwd >= 5 ? Card::tr(kContext, "Forwarded many times") : Card::tr(kContext, "Forwarded"));
            m_fwdIcon->setImageSource(Card::url(fwd >= 5 ? "asset:///images/forward_many.png" : "asset:///images/forward.png"));
        }
    }

    const QString quote = str("quote");
    if (m_quote) {
        m_quote->setVisible(!quote.isEmpty());
        if (!quote.isEmpty()) {
            const QString name = str("quoteName");
            const QString tint = str("quoteColor");
            m_quote->setBackground(Card::color(Card::quoteTint(m_list)));
            // A story's quote is labelled so, and shows the story's picture when it came with one.
            const bool story = flag("quoteStory");
            m_quoteName->setVisible(story || !name.isEmpty());
            m_quoteName->setText(story ? Card::tr(kContext, "Story") : name);
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
        m_head->setVisible(!sender.isEmpty() || fwd > 0 || !quote.isEmpty());
        m_card->setVisible(!text.isEmpty() || !str("label").isEmpty() || str("media").isEmpty());
    }
}

// What goes before the time: a star for a starred message, then "edited".
QString MessageCard::timeLead() const
{
    return (flag("starred") ? m_starMark : QString()) + (flag("edited") ? m_edited : QString());
}

// The time (the star and "edited" before it) and our message's state, as in ChatRows.cpp: sending, sent, delivered,
// read, or a red "!" when it failed; then the foot and the reactions' chip.
void MessageCard::fillFoot()
{
    const bool out = flag("out");
    const int state = m_msg.value("state").toInt();
    m_time->setText(timeLead() + str("time"));
    // Where the time goes: on a picture with no caption, at the text's end, under a voice note's
    // waveform, or else (a sticker, a file) beside the column's first line.
    // Top-aligned boxes put the time's baseline above the first line's: 7 px under a name or quote
    // (small), 12 px under body text, measured at 85 % and 105 % on the Q10, then scaled with the
    // text size setting; 4 px more since the time is at 72 %, reckoned, not measured.
    // ponytail: linear in the scale, not measured at 90-140 %.
    // In the picture card the head holds them, so the caption is the card's first line. +2 px for
    // the time at 85 %, whose baseline sits higher (Q10, 0.0.1.212: +3 was 1 px low; 0.0.1.216: 2 more).
    const bool small = !m_head && (!str("sender").isEmpty() || (!flag("deleted") && m_msg.value("forwarded").toInt() > 0) || !str("quote").isEmpty());
    const bool onPicture = m_head && !m_card->isVisible();
    const int place = onPicture ? 2 : m_words->isVisible() ? 1 : m_voice && flag("voice") ? 3 : 0;
    placeTime(place);
    // The top padding is this one's, on a picture too; set only when it changed.
    const float top = onPicture ? m_ui->du(0.5) : place == 0 ? m_ui->px((small ? 11 : 16) * m_scale) : 0;
    if (top != m_timeTop) {
        m_timeTop = top;
        m_timeBox->setTopPadding(top);
    }
    // White on the picture's dark chip.
    const Color quiet = onPicture ? Color::White : m_ui->palette()->secondaryTextOnPlain();
    m_time->textStyle()->setColor(quiet);

    const bool ticked = out && state < 4;
    m_ticks->setVisible(ticked);
    if (ticked) {
        const int tick = state == 0 ? 0 : state == 1 ? 1 : 2;
        if (tick != m_tick) {
            m_tick = tick;
            // 2.4 du tall at 100 %, 22 px on the Q10, where the images' 27 px were; the two ticks are wider.
            m_ticks->setPreferredSize(m_ui->du((tick == 2 ? 3.1 : 2.4) * m_scale), m_ui->du(2.4 * m_scale));
            m_ticks->setImageSource(Card::url(tick == 0 ? "asset:///images/clock.png"
                                        : tick == 1 ? "asset:///images/tick.png" : "asset:///images/ticks.png"));
        }
        m_ticks->setFilterColor(state == 3 ? m_ui->palette()->primary() : quiet);
    }
    if (m_bang)
        m_bang->setVisible(out && state == 4);

    // The column's width at most: the card's less its paddings, and less the time when that is
    // beside it. Without it a long text asks for the whole row and the stack takes the excess from
    // both, so the time is cut.
    if (!m_head) {
        const float most = Card::width(m_ui) - 2 * Card::side(m_ui, m_scale)
                           - (place == 0 ? m_ui->du(1) + timeWidth() : 0);
        if (most != m_colMax) {
            m_colMax = most;
            m_col->setMaxWidth(most);
        }
    }

    m_barL->setVisible(!out);
    m_barR->setVisible(out);
    m_foot->setVisible(str("media") != "sticker");
    m_tailL->setVisible(!out);
    m_tailR->setVisible(out);

    const int count = m_msg.value("reactionCount").toInt();
    if (m_chip) {
        m_chip->setVisible(count > 0);
        // Under the card's outer corner: ours is at the right now.
        m_chip->setHorizontalAlignment(out ? HorizontalAlignment::Right : HorizontalAlignment::Left);
        if (count > 0)
            m_reactions->setText(str("reactions") + (count > 1 ? QString(" %1").arg(count) : QString()));
    }
}


// The time and ticks' width, which can't be asked, reckoned a little over: 1.55 du a character at
// 100 % ("11:16 PM" was 117 px on the Q10 at 85 %, so 1.37 du each at 72 %), the ticks' 3.1 du and
// their 1 du margin, and the "!" with the 2.5 du before it.
// ponytail: a reckoning; a time wider than it (a script with wide digits) touches the text's end.
float MessageCard::timeWidth() const
{
    const bool out = flag("out");
    const int state = m_msg.value("state").toInt();
    const int chars = str("time").size() + timeLead().size();
    return chars * m_ui->du(1.55 * m_scale) + (out && state < 4 ? m_ui->du(1 + 3.1 * m_scale) : 0)
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
    Container *const homes[] = { m_card, m_words, m_media ? m_media->stamp() : 0, m_voice ? m_voice->stamp() : 0 };
    if (m_timePlace >= 0)
        homes[m_timePlace]->remove(m_timeBox);
    homes[place]->add(m_timeBox);
    m_timePlace = place;

    const bool onPicture = place == 2;
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
