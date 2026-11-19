#include "MessageCards.h"

#include <bb/cascades/Container>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/Label>
#include <bb/cascades/ListView>
#include <bb/cascades/TextAlign>
#include <bb/cascades/UIConfig>
#include <bb/cascades/UIPalette>

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
    // "text" is the light card; "picture" the full one with a photo, video or map across it; "msg", and
    // anything else, the full one.
    return new MessageCard(list, type != "text", type == "picture");
}

void MessageCards::updateItem(ListView *, VisualNode *listItem, const QString &type,
                              const QVariantList &, const QVariant &data)
{
    if (type == "header")
        static_cast<ServiceCard *>(listItem)->showText(data.toString());
    else if (type == "service")
        static_cast<ServiceCard *>(listItem)->show(data.toMap());
    else
        static_cast<MessageCard *>(listItem)->show(data.toMap());
}

// A list item is as wide as its content, so Center alone left it on the left: the spacers'
// spaceQuota stretches the row across the list, as the cards' text column does. Telegram's pill: one
// square container in the list's serviceColor with white text (the user's ask, 2026-11-18; it had a
// 2 px border), the colour set in show() so a theme change shows.
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
    m_chip->setLeftPadding(ui->du(2));
    m_chip->setRightPadding(ui->du(2));
    m_chip->setTopPadding(ui->du(1));
    m_chip->setBottomPadding(ui->du(1));

    m_text = new Label();
    m_text->setHorizontalAlignment(HorizontalAlignment::Center);
    m_text->setMultiline(true);
    m_text->textStyle()->setTextAlign(TextAlign::Center);

    Container *right = new Container();
    Card::quota(right, 1);

    m_chip->add(m_text);
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
    m_chip->setBackground(Card::color(Card::serviceColor(m_list)));
    Card::style(m_text, m_list, Card::smallSize(m_list));
    m_text->textStyle()->setColor(Color::White); // after the base, as the message card does
    m_text->setText(text);
}
