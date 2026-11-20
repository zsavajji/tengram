#include "MessageCards.h"

#include <bb/cascades/ActivityIndicator>
#include <bb/cascades/Container>
#include <bb/cascades/DockLayout>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/ImageView>
#include <bb/cascades/ImageViewLoadEffect>
#include <bb/cascades/Label>
#include <bb/cascades/ListView>
#include <bb/cascades/ScalingMethod>
#include <bb/cascades/TextAlign>
#include <bb/cascades/UIConfig>
#include <bb/cascades/UIPalette>
#include <bb/cascades/VerticalAlignment>

#include "CardStyle.h"
#include "MessageCard.h"

using namespace bb::cascades;

MessageCards::MessageCards(QObject *parent)
    : ListItemProvider(parent)
{
}

VisualNode *MessageCards::createItem(ListView *list, const QString &type)
{
    // A text size or Shape change refills every card: its paddings, sizes and corners were set from the old one.
    if (m_list != list) {
        m_list = list;
        if (!connect(list, SIGNAL(textScaleChanged()), this, SIGNAL(itemUpdatesNeeded())))
            qWarning("cards: the list has no textScaleChanged");
        if (!connect(list, SIGNAL(roundChanged()), this, SIGNAL(itemUpdatesNeeded())))
            qWarning("cards: the list has no roundChanged");
    }
    // Telegram's floating date pill, for the day headers too (the user's ask, 2026-11-18).
    if (type == "header")
        return new ServiceCard(list);
    if (type == "service")
        return new ServiceCard(list);
    // Not a MessageCard, so not in m_cards (that list is for slideRow, which casts nothing but those).
    if (type == "loading")
        return new LoadingCard(list);
    // "text" is the light card; "picture" the full one with a photo, video or map across it; "msg", and
    // anything else, the full one.
    MessageCard *card = new MessageCard(list, type != "text", type == "picture");
    m_cards << card;
    return card;
}

void MessageCards::slideRow(const QString &id)
{
    for (int i = m_cards.size() - 1; i >= 0; --i) {
        MessageCard *card = m_cards.at(i);
        if (!card)
            m_cards.removeAt(i); // gone with the page
        else
            card->reportSlide(id);
    }
}

void MessageCards::updateItem(ListView *, VisualNode *listItem, const QString &type,
                              const QVariantList &, const QVariant &data)
{
    if (type == "header")
        static_cast<ServiceCard *>(listItem)->showText(data.toString());
    else if (type == "service")
        static_cast<ServiceCard *>(listItem)->show(data.toMap());
    else if (type == "loading")
        return; // a LoadingCard has nothing to fill (and is not a MessageCard: the cast below would crash)
    else
        static_cast<MessageCard *>(listItem)->show(data.toMap());
}

// A list item is as wide as its content, so Center alone left it on the left: the spacers'
// spaceQuota stretches the row across the list, as the cards' text column does. Telegram's pill: one
// square container in the list's serviceColor with white text (the user's ask, 2026-11-18; it had a
// 2 px border), the colour set in show() so a theme change shows. In Round mode (the user's ask, 2026-11-19) it is a pill:
// Cascades has no corner radius, so the colour is pill.amd stretched under the text (m_pill, tinted), and the Container's
// own background is off; the text's paddings are on an inner Container, so the pill covers them too.
ServiceCard::ServiceCard(ListView *list)
    : m_list(list)
{
    UIConfig *ui = list->ui();
    setHorizontalAlignment(HorizontalAlignment::Fill);

    Container *root = new Container();
    root->setHorizontalAlignment(HorizontalAlignment::Fill);
    root->setTopPadding(ui->du(1.5));
    root->setBottomPadding(0);
    root->setLayout(Card::across());

    Container *left = new Container();
    Card::quota(left, 1);

    m_chip = new Container();
    m_chip->setLayout(new DockLayout());
    m_pill = new ImageView();
    m_pill->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_pill->setVerticalAlignment(VerticalAlignment::Fill);
    m_pill->setScalingMethod(ScalingMethod::Fill);
    m_pill->setLoadEffect(ImageViewLoadEffect::None);
    m_pill->setImageSource(Card::url("asset:///images/pill.amd"));
    m_pill->setVisible(false);
    Container *padded = new Container();
    padded->setLeftPadding(ui->du(2));
    padded->setRightPadding(ui->du(2));
    padded->setTopPadding(ui->du(1));
    padded->setBottomPadding(ui->du(1));

    m_text = new Label();
    m_text->setHorizontalAlignment(HorizontalAlignment::Center);
    m_text->setMultiline(true);
    m_text->textStyle()->setTextAlign(TextAlign::Center);

    Container *right = new Container();
    Card::quota(right, 1);

    padded->add(m_text);
    m_chip->add(m_pill);
    m_chip->add(padded);
    root->add(left);
    root->add(m_chip);
    root->add(right);
    setRoot(root);
}

void ServiceCard::show(const QVariantMap &msg)
{
    showText(msg.value("text").toString());
}

void ServiceCard::showText(const QString &text)
{
    const Color service = Card::color(Card::serviceColor(m_list));
    const bool round = Card::isRound(m_list);
    m_pill->setVisible(round);
    m_pill->setFilterColor(service);
    if (round)
        m_chip->resetBackground();
    else
        m_chip->setBackground(service);
    Card::style(m_text, m_list, Card::smallSize(m_list));
    m_text->textStyle()->setColor(Color::White); // after the base, as the message card does
    m_text->setText(text);
}

// A list item is as wide as its content unless it fills, so the root fills the row and the spinner is centred in it. No
// background: the page's colour shows. The spinner runs for as long as the row exists (the model drops the row when the wait ends).
LoadingCard::LoadingCard(ListView *list)
{
    UIConfig *ui = list->ui();
    setHorizontalAlignment(HorizontalAlignment::Fill);

    Container *root = new Container();
    root->setHorizontalAlignment(HorizontalAlignment::Fill);
    root->setTopPadding(ui->du(2));
    root->setBottomPadding(ui->du(2));

    ActivityIndicator *spin = new ActivityIndicator();
    spin->setHorizontalAlignment(HorizontalAlignment::Center);
    spin->setPreferredWidth(ui->du(6));
    spin->setPreferredHeight(ui->du(6));
    spin->setRunning(true);

    root->add(spin);
    setRoot(root);
}
