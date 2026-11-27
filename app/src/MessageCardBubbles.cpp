#include "MessageCard.h"

#include <bb/cascades/ActiveTextHandler>
#include <bb/cascades/ActiveTextTriggeredEvent>
#include <bb/cascades/Button>
#include <bb/cascades/Color>
#include <bb/cascades/Container>
#include <bb/cascades/FontWeight>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/ImageView>
#include <bb/cascades/Label>
#include <bb/cascades/ScalingMethod>
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
#include "LinkPreview.h"
#include "MediaBubble.h"
#include "PollBubble.h"
#include "VoiceBubble.h"

using namespace bb::cascades;

// The rich card's bubbles: where each goes in the column, and when it shows.

// Puts a part made late in its place in the column: before the first of the parts that follow it
// there, or last.
void MessageCard::place(Control *part, Control *a, Control *b, Control *c, Control *d)
{
    Control *const next[] = { a, b, c, d };
    for (int i = 0; i < 4; i++) {
        if (next[i] && m_col->indexOf(next[i]) >= 0) {
            m_col->insert(m_col->indexOf(next[i]), part);
            return;
        }
    }
    m_col->add(part);
}

// The bubbles this message needs and the card hasn't made yet, each built only for the rows that
// need it, as MessageItem.qml's ControlDelegates were (step 8.6 removed it): a bubble in every card would weigh down every
// row (4.1). In the column: voice, media, album, [label, text], link preview, [invite], poll. An
// album's media is "", so MediaBubble isn't made for it.
void MessageCard::makeBubbles()
{
    const bool deleted = flag("deleted");
    if (flag("voice") && !m_voice) {
        m_voice = new VoiceBubble(m_list);
        place(m_voice, m_media, m_album, m_label, m_words);
    }
    if (!str("media").isEmpty() && !m_media) {
        // In the picture card it spans the card, between the head and the caption.
        m_media = new MediaBubble(m_list, m_head != 0);
        if (m_head)
            m_frame->insert(1, m_media);
        else
            place(m_media, m_album, m_label, m_words);
    }
    if (!m_msg.value("album").toList().isEmpty() && !m_album) {
        m_album = new AlbumBubble(m_list);
        place(m_album, m_label, m_words);
    }
    if (!str("linkTitle").isEmpty() && !deleted && !m_link) {
        m_link = new LinkPreview(m_list);
        place(m_link, m_join, m_poll);
    }
    if (flag("poll") && !deleted && !m_poll) {
        m_poll = new PollBubble(m_list);
        place(m_poll);
    }
}

// Each bubble shows this message, or hides when it has none of its kind.
void MessageCard::fillBubbles()
{
    const bool deleted = flag("deleted");
    const bool on[] = { flag("voice"), !str("media").isEmpty(), !m_msg.value("album").toList().isEmpty(),
                        !str("linkTitle").isEmpty() && !deleted, flag("poll") && !deleted };
    if (m_voice) {
        m_voice->setVisible(on[0]);
        if (on[0])
            m_voice->show(m_msg);
    }
    if (m_media) {
        m_media->setVisible(on[1]);
        if (on[1])
            m_media->show(m_msg);
    }
    if (m_album) {
        m_album->setVisible(on[2]);
        if (on[2])
            m_album->show(m_msg);
    }
    if (m_link) {
        m_link->setVisible(on[3]);
        if (on[3])
            m_link->show(m_msg);
    }
    if (m_poll) {
        m_poll->setVisible(on[4]);
        if (on[4])
            m_poll->show(m_msg);
    }
}

// A text's code blocks (data parts: MessageRows, Markup::parts), each a box of its own above the card's text, as
// Telegram's: an accent bar at its left, its language and a copy mark over the code when it has a language, the code in
// the phone's monospace font; the text before a block is a label like the card's own. A tap on a box copies its code
// (onBlock). Made again at each fill, and only by a message that has a block.
void MessageCard::fillBlocks(const QVariantList &parts)
{
    if (m_blocks) {
        m_blocks->removeAll();
        m_blocks->setVisible(!parts.isEmpty());
    }
    if (parts.isEmpty())
        return;
    if (!m_blocks) {
        m_blocks = new Container();
        m_blocks->setHorizontalAlignment(HorizontalAlignment::Fill);
        m_blocks->setTopMargin(0);
        m_blocks->setBottomMargin(0);
        m_col->insert(m_col->indexOf(m_words), m_blocks);
    }
    const Color ink = m_ui->palette()->textOnPlain();
    foreach (const QVariant &v, parts) {
        const QVariantMap part = v.toMap();
        const QString text = part.value("text").toString();
        if (!part.value("code").toBool()) {
            Label *label = new Label();
            label->setMultiline(true);
            Card::style(label, m_list, m_body);
            label->textStyle()->setColor(ink);
            label->setTopMargin(0);
            label->setBottomMargin(m_ui->du(0.5));
            label->setLeftMargin(0);
            label->setRightMargin(0);
            ActiveTextHandler *links = new ActiveTextHandler(label);
            label->setActiveTextHandler(links);
            connect(links, SIGNAL(triggered(bb::cascades::ActiveTextTriggeredEvent *)), this,
                    SLOT(onLink(bb::cascades::ActiveTextTriggeredEvent *)));
            const QString html = part.value("html").toString();
            label->setTextFormat(html.isEmpty() ? TextFormat::Plain : TextFormat::Html);
            label->setText(html.isEmpty() ? Card::noKeycaps(text) : html);
            label->content()->setFlags(TextContentFlag::ActiveText);
            m_blocks->add(label);
            continue;
        }
        Container *box = new Container();
        box->setHorizontalAlignment(HorizontalAlignment::Fill);
        box->setLayout(Card::across());
        box->setBackground(Color::fromARGB(0x40000000));
        box->setTopMargin(0);
        box->setBottomMargin(m_ui->du(0.75));
        box->setProperty("code", text);

        Container *bar = new Container();
        bar->setVerticalAlignment(VerticalAlignment::Fill);
        bar->setPreferredWidth(m_ui->du(0.5));
        bar->setMinWidth(m_ui->du(0.5));
        bar->setBackground(m_ui->palette()->primary());
        bar->setTopMargin(0);
        bar->setBottomMargin(0);
        bar->setLeftMargin(0);
        bar->setRightMargin(0);
        box->add(bar);

        Container *inside = new Container();
        Card::quota(inside, 1);
        inside->setLeftMargin(0);
        const QString lang = part.value("lang").toString();
        if (!lang.isEmpty()) {
            Container *head = new Container();
            head->setHorizontalAlignment(HorizontalAlignment::Fill);
            head->setLayout(Card::across());
            head->setBackground(Color::fromARGB(0x20FFFFFF));
            head->setLeftPadding(m_ui->du(1));
            head->setRightPadding(m_ui->du(1));
            head->setTopPadding(m_ui->du(0.25));
            head->setBottomPadding(m_ui->du(0.25));
            Label *name = new Label();
            name->setText(lang);
            Card::style(name, m_list, qRound(m_body * 0.8));
            name->textStyle()->setFontWeight(FontWeight::Bold);
            name->textStyle()->setColor(m_ui->palette()->primary());
            name->setVerticalAlignment(VerticalAlignment::Center);
            Card::quota(name, 1);
            head->add(name);
            ImageView *copy = new ImageView();
            copy->setImageSource(Card::url("asset:///images/copy.png"));
            copy->setPreferredSize(m_ui->du(3), m_ui->du(3));
            copy->setScalingMethod(ScalingMethod::AspectFit);
            copy->setVerticalAlignment(VerticalAlignment::Center);
            copy->setFilterColor(ink);
            head->add(copy);
            inside->add(head);
        }
        Container *padded = new Container();
        padded->setLeftPadding(m_ui->du(1));
        padded->setRightPadding(m_ui->du(1));
        padded->setTopPadding(m_ui->du(0.5));
        padded->setBottomPadding(m_ui->du(0.5));
        Label *code = new Label();
        code->setMultiline(true);
        Card::style(code, m_list, qRound(m_body * 0.85));
        code->textStyle()->setFontFamily("DejaVu Sans Mono, Andale Mono, monospace");
        code->textStyle()->setColor(ink);
        code->setTextFormat(TextFormat::Plain);
        code->setText(text);
        padded->add(code);
        inside->add(padded);
        box->add(inside);

        TapHandler *tap = new TapHandler(box);
        box->addGestureHandler(tap);
        connect(tap, SIGNAL(tapped(bb::cascades::TapEvent *)), this, SLOT(onBlock()));
        m_blocks->add(box);
    }
}
